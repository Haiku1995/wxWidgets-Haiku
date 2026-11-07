/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/dnd.cpp
// Purpose:     Drag and drop support for Haiku
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#if wxUSE_DRAG_AND_DROP

#include "wx/dnd.h"
#include "wx/dataobj.h"
#include "wx/scopedarray.h"
#include "wx/window.h"
#include "wx/app.h"
#include "wx/weakref.h"
#include "wx/haiku/private/dispatch.h"
#include <Looper.h>
#include <MessageQueue.h>
#include <Window.h>
#include <Entry.h>
#include <Path.h>
#include <OS.h>
#include <atomic>

#include <Message.h>
#include <View.h>
#include <Point.h>
#include <memory>

namespace
{

static const uint32 WX_HAIKU_DND_MESSAGE = 'WxDn';
static const uint32 WX_HAIKU_DND_REPLY = 'WxDr';

wxDragResult DefaultResult(const BMessage& message)
{
    int32 flags = wxDrag_CopyOnly;
    message.FindInt32("wx:drag_flags", &flags);
    if (modifiers() & B_CONTROL_KEY) return wxDragCopy;
    return (flags & wxDrag_AllowMove) &&
           (((flags & wxDrag_DefaultMove) == wxDrag_DefaultMove) || (modifiers() & B_SHIFT_KEY))
               ? wxDragMove : wxDragCopy;
}

void ReplyToSource(BMessage* message, wxDragResult result)
{
    BMessage reply(WX_HAIKU_DND_REPLY);
    reply.AddInt32("wx:drag_result", result);
    message->SendReply(&reply);
}

class DragCompletion : public BLooper
{
public:
    DragCompletion() : BLooper("wx drag completion") {}
    std::atomic<int> result{wxDragError};
    void MessageReceived(BMessage* message) override
    {
        int32 value;
        if (message->what == WX_HAIKU_DND_REPLY &&
            message->FindInt32("wx:drag_result", &value) == B_OK)
            result.store(value);
        else
            BLooper::MessageReceived(message);
    }
};

// DoDragDrop can be called on a BWindow's own callback thread. Its normal
// looper cannot dispatch until we return, so consume only pending drop messages
// here, using the native hit-tested view and its closest wx owner.
void DispatchSourceWindowDrops(BWindow* window)
{
    if (!window || window->Thread() != find_thread(nullptr)) return;
    window->UpdateIfNeeded(); // Also imports pending messages from its port.
    BMessageQueue* queue = window->MessageQueue();
    for (;;)
    {
        BMessage* dropped = nullptr;
        queue->Lock();
        for (int32 i = 0; BMessage* pending = queue->FindMessage(i); ++i)
            if (pending->WasDropped())
            {
                dropped = pending;
                queue->RemoveMessage(dropped);
                break;
            }
        queue->Unlock();
        if (!dropped) break;
        BPoint point = window->ConvertFromScreen(dropped->DropPoint());
        BView* view = window->FindView(point);
        bool handled = false;
        for (; view; view = view->Parent())
            if (wxWindowHaiku* owner = wxWindowHaiku::HaikuRetrieveWindowPointer(view))
            {
                if (owner->IsEnabled() && owner->GetDropTarget())
                {
                    owner->HaikuHandleDropMessage(owner->GetHandle(), dropped);
                    handled = true;
                }
                break;
            }
        if (!handled) ReplyToSource(dropped, wxDragNone);
        delete dropped;
    }
}

bool AddDataFormatToMessage(wxDataObject* dataObject,
                            BMessage& message,
                            const wxDataFormat& format)
{
    if ( !dataObject || format == wxFormatInvalid )
        return false;

    const size_t dataSize = dataObject->GetDataSize(format);
    if ( dataSize == 0 )
        return false;

    const wxString mimeType = format.GetMimeType();
    if ( mimeType.empty() )
        return false;

    wxCharBuffer mime = mimeType.utf8_str();
    if ( !mime.data() || !mime.data()[0] )
        return false;

    wxCharBuffer buffer(dataSize);
    if ( !buffer.data() )
        return false;

    if ( !dataObject->GetDataHere(format, buffer.data()) )
        return false;

    return message.AddData(mime.data(), B_MIME_TYPE, buffer.data(), dataSize) == B_OK;
}
bool FillMessageFromDataObject(wxDataObject* dataObject, BMessage& message)
{
    if ( !dataObject )
        return false;

    const size_t count = dataObject->GetFormatCount();
    if ( count == 0 )
        return false;

    wxScopedArray<wxDataFormat> formats(count);
    dataObject->GetAllFormats(formats.get());

    bool addedAny = false;
    if (auto* files = dynamic_cast<wxFileDataObject*>(dataObject))
    {
        files->HaikuAddDataTo(message);
        return !files->GetFilenames().empty();
    }
    for ( size_t i = 0; i < count; ++i )
    {
        if ( AddDataFormatToMessage(dataObject, message, formats[i]) )
            addedAny = true;
    }

    return addedAny;
}

bool ExtractMessageData(const BMessage* message,
                        const wxDataFormat& format,
                        const void** data,
                        ssize_t* size)
{
    if ( !message || format == wxFormatInvalid || !data || !size )
        return false;

    const wxString mimeType = format.GetMimeType();
    if ( mimeType.empty() )
        return false;

    wxCharBuffer mime = mimeType.utf8_str();
    if ( !mime.data() || !mime.data()[0] )
        return false;

    return message->FindData(mime.data(), B_ANY_TYPE, data, size) == B_OK;
}

} // anonymous namespace

// ============================================================================
// wxDropTarget::Impl
// ============================================================================

class wxDropTarget::Impl
{
public:
    explicit Impl(wxDropTarget* owner)
        : m_owner(owner),
          m_view(nullptr),
          m_pendingMessage(nullptr),
          m_dragInside(false)
    {
    }

    void ConnectTo(BView* view)
    {
        m_view = view;
    }

    void Disconnect()
    {
        m_view = nullptr;
        m_pendingMessage = nullptr;
        m_dragInside = false;
    }

    bool HandleDragOver(BView* view, BMessage* message, const BPoint& where)
    {
        if ( !m_owner || !view || !message )
            return false;

        m_pendingMessage.reset(new BMessage(*message));

        if ( !CanDropHere() )
            return false;

        const wxCoord x = (wxCoord)where.x;
        const wxCoord y = (wxCoord)where.y;

        const wxDragResult def = DefaultResult(*message);

        if ( !m_dragInside )
        {
            m_dragInside = true;
            const wxDragResult entered = m_owner->OnEnter(x, y, def);
            if (entered == wxDragNone || entered == wxDragError)
                return false;
        }

        const wxDragResult result = m_owner->OnDragOver(x, y, def);
        return result == wxDragCopy || result == wxDragMove || result == wxDragLink;
    }

    bool HandleDrop(BView* view, BMessage* message)
    {
        if ( !m_owner || !view || !message )
            return false;

        m_pendingMessage.reset(new BMessage(*message));

        if ( !message->WasDropped() )
            return false;

        if ( !CanDropHere() )
        {
            ReplyToSource(message, wxDragNone);
            HandleLeave();
            return false;
        }

        BPoint where = message->DropPoint();
        view->ConvertFromScreen(&where);

        const wxCoord x = (wxCoord)where.x;
        const wxCoord y = (wxCoord)where.y;

        if (!m_dragInside && !HandleDragOver(view, message, where))
        {
            ReplyToSource(message, wxDragNone);
            HandleLeave();
            return false;
        }
        const wxDragResult accepted = m_owner->OnDragOver(x, y, DefaultResult(*message));
        if ( (accepted != wxDragCopy && accepted != wxDragMove && accepted != wxDragLink) ||
             !m_owner->OnDrop(x, y) )
        {
            ReplyToSource(message, wxDragNone);
            m_dragInside = false;
            m_pendingMessage = nullptr;
            return false;
        }

        const wxDragResult result = m_owner->OnData(x, y, accepted);
        ReplyToSource(message, result);

        m_dragInside = false;
        m_pendingMessage = nullptr;

        return result == wxDragCopy || result == wxDragMove || result == wxDragLink;
    }

    void HandleLeave()
    {
        if ( m_dragInside && m_owner )
            m_owner->OnLeave();

        m_dragInside = false;
        m_pendingMessage = nullptr;
    }

    const BMessage* GetPendingMessage() const
    {
        return m_pendingMessage.get();
    }

    wxDataFormat GetMatchingPair() const
    {
        if ( !m_pendingMessage || !m_owner || !m_owner->GetDataObject() )
            return wxFormatInvalid;

        wxDataObject* dataObject = m_owner->GetDataObject();

        const size_t count = dataObject->GetFormatCount(wxDataObject::Set);
        if ( count == 0 )
            return wxFormatInvalid;

        wxScopedArray<wxDataFormat> formats(count);
        dataObject->GetAllFormats(formats.get(), wxDataObject::Set);

        for ( size_t i = 0; i < count; ++i )
        {
            const wxDataFormat format(formats[i]);
            if (format == wxDF_FILENAME && m_pendingMessage->HasRef("refs"))
                return format;
            const wxString mimeType = format.GetMimeType();
            if ( mimeType.empty() )
                continue;

            wxCharBuffer mime = mimeType.utf8_str();
            if ( !mime.data() || !mime.data()[0] )
                continue;

            type_code typeFound = 0;
            int32 foundCount = 0;
            if ( m_pendingMessage->GetInfo(mime.data(), &typeFound, &foundCount) == B_OK )
                return format;
        }

        return wxFormatInvalid;
    }

private:
    bool CanDropHere() const
    {
        return GetMatchingPair() != wxFormatInvalid;
    }

    wxDropTarget* m_owner;
    BView* m_view;
    std::unique_ptr<BMessage> m_pendingMessage;
    bool m_dragInside;
};

// ============================================================================
// wxDropTarget
// ============================================================================

wxDropTarget::wxDropTarget(wxDataObject* dataObject)
    : wxDropTargetBase(dataObject),
      m_impl(new Impl(this))
{
}

wxDropTarget::~wxDropTarget()
{
    delete m_impl;
}

bool wxDropTarget::OnDrop(wxCoord WXUNUSED(x), wxCoord WXUNUSED(y))
{
    return true;
}

wxDragResult wxDropTarget::OnData(wxCoord WXUNUSED(x),
                                  wxCoord WXUNUSED(y),
                                  wxDragResult def)
{
    return GetData() ? def : wxDragNone;
}

bool wxDropTarget::GetData()
{
    if ( !m_dataObject )
        return false;

    const wxDataFormat format = GetMatchingPair();
    if ( format == wxFormatInvalid )
        return false;

    const BMessage* message = m_impl->GetPendingMessage();
    if ( !message )
        return false;

    if (format == wxDF_FILENAME)
    {
        if (auto* files = dynamic_cast<wxFileDataObject*>(m_dataObject))
            return files->HaikuSetDataFrom(*message);
        wxFileDataObject files;
        if (files.HaikuSetDataFrom(*message))
        {
            const size_t length = files.GetDataSize();
            wxCharBuffer buffer(length);
            return files.GetDataHere(buffer.data()) &&
                   m_dataObject->SetData(format, length, buffer.data());
        }
    }

    const void* data = nullptr;
    ssize_t size = 0;
    if ( !ExtractMessageData(message, format, &data, &size) || !data || size <= 0 )
        return false;

    return m_dataObject->SetData(format, (size_t)size, data);
}

wxDataFormat wxDropTarget::GetMatchingPair() const
{
    return m_impl->GetMatchingPair();
}

void wxDropTarget::ConnectTo(BView* view)
{
    m_impl->ConnectTo(view);
}

void wxDropTarget::Disconnect()
{
    m_impl->Disconnect();
}

bool wxDropTarget::HandleDragOver(BView* view, BMessage* message, const BPoint& where)
{
    return m_impl->HandleDragOver(view, message, where);
}

bool wxDropTarget::HandleDrop(BView* view, BMessage* message)
{
    return m_impl->HandleDrop(view, message);
}

void wxDropTarget::HandleLeave()
{
    m_impl->HandleLeave();
}

// ============================================================================
// wxDropSource
// ============================================================================

wxDropSource::wxDropSource(wxWindow* win,
                           const wxCursorBundle& copy,
                           const wxCursorBundle& move,
                           const wxCursorBundle& none)
    : wxDropSourceBase(copy, move, none),
      m_parentWindow(win)
{
}

wxDropSource::wxDropSource(wxDataObject& data,
                           wxWindow* win,
                           const wxCursorBundle& copy,
                           const wxCursorBundle& move,
                           const wxCursorBundle& none)
    : wxDropSource(win, copy, move, none)
{
    SetData(data);
}

wxDragResult wxDropSource::DoDragDrop(int flags)
{
    wxCHECK_MSG(m_data != nullptr, wxDragNone, "No data in wxDropSource");
    wxCHECK_MSG(m_parentWindow != nullptr, wxDragNone, "No parent window in wxDropSource");

    wxWeakRef<wxWindow> sourceWindow(m_parentWindow);
    BView* view = m_parentWindow->GetHandle();
    wxCHECK_MSG(view != nullptr, wxDragNone, "No native Haiku view in wxDropSource");

    BMessage message(WX_HAIKU_DND_MESSAGE);

    if ( !FillMessageFromDataObject(m_data, message) )
        return wxDragNone;

    message.AddInt32("wx:drag_flags", flags);
    auto* completion = new DragCompletion;
    completion->Run();
    if (!view->LockLooper())
    {
        if (completion->Lock()) completion->Quit();
        return wxDragNone;
    }
    BPoint where;
    uint32 buttons;
    view->GetMouse(&where, &buttons, false);
    BRect dragRect(where.x - 12, where.y - 8, where.x + 12, where.y + 8);
    BWindow* sourceNative = view->Window();
    view->DragMessage(&message, dragRect, completion);
    view->UnlockLooper();

    // Native drags are asynchronous. Release serialized/native dispatch while
    // waiting so the source window can also act as the drop destination.
    bigtime_t releasedAt = 0;
    while (completion->result.load() == wxDragError)
    {
        if (!sourceWindow) break;
        if (sourceNative && sourceNative->Thread() == find_thread(nullptr))
            DispatchSourceWindowDrops(sourceNative);
        {
            wxHaikuGuiDispatchPause pause;
            snooze(10000);
            get_mouse(&where, &buttons);
        }
        if (!buttons)
        {
            if (!releasedAt) releasedAt = system_time();
            // Unaccepted/native destinations may send no completion reply.
            if (system_time() - releasedAt > 500000) break;
        }
    }
    wxDragResult result = static_cast<wxDragResult>(completion->result.load());
    if (result == wxDragError) result = wxDragNone;
    if (result != wxDragCopy && result != wxDragMove && result != wxDragLink)
        result = wxDragNone;
    if (result == wxDragMove && !(flags & wxDrag_AllowMove)) result = wxDragNone;
    if (completion->Lock()) completion->Quit();
    return result;
}

#endif // wxUSE_DRAG_AND_DROP
