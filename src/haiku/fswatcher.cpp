/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/fswatcher.cpp
// Purpose:     Native node-monitor backend for wxFileSystemWatcher
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////
#include "wx/wxprec.h"
#if wxUSE_FSWATCHER
#include "wx/fswatcher.h"
#include "wx/private/fswatcher.h"
#include "wx/filename.h"
#include <Directory.h>
#include <Entry.h>
#include <Path.h>
#include <Looper.h>
#include <Message.h>
#include <Messenger.h>
#include <NodeMonitor.h>
#include <map>
#include <mutex>
#include <memory>
#include <vector>

class wxFSWatcherImplHaiku;
class wxFSWNativeLooper final : public BLooper
{
public:
    explicit wxFSWNativeLooper(wxFSWatcherImplHaiku* service)
        : BLooper("wxFileSystemWatcher"), m_service(service) {}
    void MessageReceived(BMessage* message) override;
private:
    wxFSWatcherImplHaiku* m_service;
};

class wxFSWatcherImplHaiku final : public wxFSWatcherImpl
{
    struct Watch
    {
        wxString original, current;
        node_ref node;
        int events;
        wxString filespec;
    };
    using Node = std::pair<dev_t, ino_t>;
public:
    explicit wxFSWatcherImplHaiku(wxFileSystemWatcherBase* watcher)
        : wxFSWatcherImpl(watcher), m_looper(new wxFSWNativeLooper(this)) {}
    ~wxFSWatcherImplHaiku() override
    {
        // Stop delivery and destroy the native looper before the service.
        if (m_looper && m_looper->Lock())
        {
            RemoveAll();
            stop_watching(BMessenger(m_looper));
            m_looper->Quit();
            m_looper = nullptr;
        }
    }
    bool Init() override { return m_looper && m_looper->Run() >= B_OK; }
    bool DoAdd(wxSharedPtr<wxFSWatchEntry> entry) override
    {
        BEntry native(entry->GetPath().utf8_str()); node_ref node;
        if (native.GetNodeRef(&node) != B_OK) return false;
        uint32 flags = B_WATCH_NAME | B_WATCH_STAT | B_WATCH_ATTR;
        if (native.IsDirectory()) flags |= B_WATCH_DIRECTORY | B_WATCH_CHILDREN;
        std::lock_guard<std::mutex> lock(m_mutex);
        if (watch_node(&node, flags, BMessenger(m_looper)) != B_OK) return false;
        m_native.push_back({entry->GetPath(),entry->GetPath(),node,entry->GetFlags(),entry->GetFilespec()});
        Remember(native);
        m_paths[Node(node.device,node.node)] = entry->GetPath();
        if (native.IsDirectory())
        {
            BDirectory directory(&node); BEntry child;
            while (directory.GetNextEntry(&child) == B_OK)
            {
                node_ref childNode; char name[B_FILE_NAME_LENGTH];
                if (child.GetNodeRef(&childNode)==B_OK && child.GetName(name)==B_OK)
                    m_paths[Node(childNode.device,childNode.node)] = wxFileName(entry->GetPath(),wxString::FromUTF8(name)).GetFullPath();
            }
        }
        return true;
    }
    bool DoRemove(wxSharedPtr<wxFSWatchEntry> entry) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto it=m_native.begin(); it!=m_native.end(); ++it)
            if (it->original == entry->GetPath())
            {
                const bool ok = watch_node(&it->node,B_STOP_WATCHING,BMessenger(m_looper)) == B_OK;
                m_native.erase(it); return ok;
            }
        return true;
    }
    bool RemoveAll() override
    {
        const bool ok = wxFSWatcherImpl::RemoveAll();
        std::lock_guard<std::mutex> lock(m_mutex);
        m_paths.clear(); return ok;
    }
    void Receive(BMessage* message)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        int32 opcode=0,device=-1;int64 inode=-1,directory=-1,from=-1,to=-1;
        if (message->FindInt32("opcode",&opcode)!=B_OK || message->FindInt32("device",&device)!=B_OK) return;
        message->FindInt64("node",&inode);message->FindInt64("directory",&directory);
        message->FindInt64("from directory",&from);message->FindInt64("to directory",&to);
        const char* name=nullptr;message->FindString("name",&name);
        Node node(device,inode);wxString oldPath,newPath;int events=0;
        auto known=m_paths.find(node);
        if(known!=m_paths.end())oldPath=known->second;
        switch(opcode)
        {
            case B_ENTRY_CREATED:
                newPath=PathFor(device,directory,name);oldPath=newPath;events=wxFSW_EVENT_CREATE;break;
            case B_ENTRY_REMOVED:
                if(oldPath.empty())oldPath=PathFor(device,directory,name);
                newPath=oldPath;events=wxFSW_EVENT_DELETE;break;
            case B_ENTRY_MOVED:
            {
                const char* fromName=nullptr;message->FindString("from name",&fromName);
                if(oldPath.empty())oldPath=PathFor(device,from,fromName?fromName:name);
                newPath=PathFor(device,to,name);events=wxFSW_EVENT_RENAME;break;
            }
            case B_STAT_CHANGED:
            {
                int32 fields=0;message->FindInt32("fields",&fields);
                if(fields & (B_STAT_SIZE|B_STAT_MODIFICATION_TIME))events|=wxFSW_EVENT_MODIFY;
                if(fields & B_STAT_ACCESS_TIME)events|=wxFSW_EVENT_ACCESS;
                if(fields & (B_STAT_MODE|B_STAT_UID|B_STAT_GID|B_STAT_CREATION_TIME))events|=wxFSW_EVENT_ATTRIB;
                if(!fields)events=wxFSW_EVENT_MODIFY;
                newPath=oldPath;break;
            }
            case B_ATTR_CHANGED:events=wxFSW_EVENT_ATTRIB;newPath=oldPath;break;
            default:return;
        }
        if(oldPath.empty() || newPath.empty())return;
        int requested=0;
        for(auto& watch:m_native)
        {
            if(watch.node.device!=device)continue;
            if(watch.node.node==inode || watch.node.node==directory || watch.node.node==from || watch.node.node==to ||
               wxFileName(oldPath).GetPath()==watch.current || wxFileName(newPath).GetPath()==watch.current)
            {
                if(MatchesFilespec(wxFileName(newPath),watch.filespec))requested|=watch.events;
            }
        }
        events &= requested;
        if(events)
            wxQueueEvent(m_watcher->GetOwner(),new wxFileSystemWatcherEvent(events,wxFileName(oldPath),wxFileName(newPath)));
        if(opcode==B_ENTRY_REMOVED)m_paths.erase(node);
        else m_paths[node]=newPath;
        if(opcode==B_ENTRY_MOVED)
            for(auto& watch:m_native)
                if(watch.current==oldPath)watch.current=newPath;
    }
private:
    void Remember(BEntry& entry)
    {
        node_ref node; BPath path;
        if(entry.GetNodeRef(&node)==B_OK && entry.GetPath(&path)==B_OK)
            m_paths[Node(node.device,node.node)]=wxString::FromUTF8(path.Path());
    }
    wxString PathFor(dev_t device,ino_t inode,const char* name)
    {
        if(inode<0 || !name)return wxString();
        const auto known=m_paths.find(Node(device,inode));
        if(known!=m_paths.end())return wxFileName(known->second,wxString::FromUTF8(name)).GetFullPath();
        node_ref ref;ref.device=device;ref.node=inode;
        BDirectory directory(&ref);BPath path;
        return path.SetTo(&directory,name)==B_OK?wxString::FromUTF8(path.Path()):wxString();
    }
    BLooper* m_looper;
    std::mutex m_mutex;
    std::vector<Watch> m_native;
    std::map<Node,wxString> m_paths;
};
void wxFSWNativeLooper::MessageReceived(BMessage* message)
{
    if(message->what==B_NODE_MONITOR)m_service->Receive(message);
    else BLooper::MessageReceived(message);
}

// Keep the existing public class/layout used by the configured Haiku port.
wxKqueueFileSystemWatcher::wxKqueueFileSystemWatcher(){Init();}
wxKqueueFileSystemWatcher::wxKqueueFileSystemWatcher(const wxFileName& path,int events)
{if(Init())Add(path,events);}
wxKqueueFileSystemWatcher::~wxKqueueFileSystemWatcher()=default;
bool wxKqueueFileSystemWatcher::Init()
{
    m_service=new wxFSWatcherImplHaiku(this);
    return m_service->Init();
}
#endif
