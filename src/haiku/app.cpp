/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/app.cpp
// Purpose:     wxApp implementation for Haiku using BApplication
// Author:      Based on Qt port; adapted for Haiku
// Created:     2025
// Copyright:   (c) 2025 wxWidgets team
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#ifndef WX_PRECOMP
    #include "wx/app.h"
    #include "wx/apptrait.h"
    #include "wx/private/init.h"
    #include "wx/utils.h"
#endif

#include "wx/app.h"
#include "wx/haiku/sockhaiku.h"
#include "wx/haiku/private.h"
#include "wx/log.h"

#include <Application.h>
#include <Messenger.h>
#include <mutex>
#include "wx/haiku/private/dispatch.h"
#include <AppFileInfo.h>
#include <File.h>
#include <MimeType.h>
#include <image.h>
#include <Looper.h>
#include <String.h>
#if defined(WX_DEBUG_HAIKU) && WX_DEBUG_HAIKU
    #define HAIKU_LOG(msg, ...) \
        wxLogDebug("[HAIKU-APP:%s:%d] " msg, __func__, __LINE__, ##__VA_ARGS__)
#else
    #define HAIKU_LOG(msg, ...) ((void)0)
#endif


namespace
{
std::recursive_mutex gs_dispatchMutex;
thread_local int gs_dispatchDepth = 0;
thread_local void (*gs_releaseGLContext)() = nullptr;

int ReleaseNativeLocks(BLooper* looper)
{
    if (!looper || looper->LockingThread() != find_thread(nullptr))
        return 0;
    const int count = looper->CountLocks();
    for (int i = 0; i < count; ++i)
        looper->Unlock();
    return count;
}
void RestoreNativeLocks(BLooper* looper, int count)
{
    for (int i = 0; i < count; ++i)
        looper->Lock();
}
}

void wxHaikuSetGLContextRelease(void (*release)())
{
    gs_releaseGLContext = release;
}

bool wxHaikuIsGuiDispatchThread()
{
    return gs_dispatchDepth > 0;
}

wxHaikuGuiDispatchLock::wxHaikuGuiDispatchLock(BLooper* looper)
{
    // A callback waiting for wx dispatch must not hold its looper lock:
    // the current wx handler may need that same window/application looper.
    const int count = gs_dispatchDepth == 0 ? ReleaseNativeLocks(looper) : 0;
    gs_dispatchMutex.lock();
    ++gs_dispatchDepth;
    RestoreNativeLocks(looper, count);
}
wxHaikuGuiDispatchLock::~wxHaikuGuiDispatchLock()
{
    --gs_dispatchDepth;
    if (gs_dispatchDepth == 0 && gs_releaseGLContext)
    {
        auto release = gs_releaseGLContext;
        gs_releaseGLContext = nullptr;
        release();
    }
    gs_dispatchMutex.unlock();
}
wxHaikuGuiDispatchPause::wxHaikuGuiDispatchPause()
    : m_looper(BLooper::LooperForThread(find_thread(nullptr))),
      m_nativeLocks(0), m_dispatchLocks(gs_dispatchDepth)
{
    if (!m_dispatchLocks)
        return;
    // The main wx thread can own be_app while dispatching it manually.
    // LooperForThread() identifies the looper's thread, not its lock owner.
    if (be_app && be_app->LockingThread() == find_thread(nullptr))
        m_looper = be_app;
    m_nativeLocks = ReleaseNativeLocks(m_looper);
    gs_dispatchDepth = 0;
    for (int i = 0; i < m_dispatchLocks; ++i)
        gs_dispatchMutex.unlock();
}
wxHaikuGuiDispatchPause::~wxHaikuGuiDispatchPause()
{
    for (int i = 0; i < m_dispatchLocks; ++i)
        gs_dispatchMutex.lock();
    gs_dispatchDepth = m_dispatchLocks;
    RestoreNativeLocks(m_looper, m_nativeLocks);
}

class wxHaikuApplication : public BApplication
{
public:
    explicit wxHaikuApplication(const char* signature) : BApplication(signature) {}
    void DispatchMessage(BMessage* message, BHandler* target) override
    {
        // Native Quit() can terminate a looper thread without unwinding C++
        // guards. Protect wx close handling in QuitRequested(), not Quit().
        if (message && message->what == B_QUIT_REQUESTED)
        {
            BApplication::DispatchMessage(message, target);
            return;
        }
        BMessenger destination(target);
        wxHaikuGuiDispatchLock guard(this);
        // A target can be removed while the native lock is temporarily released.
        // IsValid() checks the port, not the handler token. The port may
        // remain valid after a timer/view handler was removed during the wait.
        BHandler* const resolved = target ? destination.Target(nullptr) : nullptr;
        if (target && (!resolved || resolved->Looper() != this))
            return;
        BApplication::DispatchMessage(message, resolved);
    }
};

// Forward declaration for timer messages (defined in timer.cpp)
extern "C" void wxHandleHaikuMessage(BMessage* msg);

wxIMPLEMENT_DYNAMIC_CLASS(wxApp, wxEvtHandler);
wxCursor g_globalCursor(wxCURSOR_DEFAULT);
wxApp::wxApp()
    : m_bApp(nullptr)
    , m_fdioManager(nullptr)
{
    HAIKU_LOG("APP", "wxApp ctor this=%p", this);
    WXAppConstructed();
}

extern void wxHaikuReleaseNativeCursors();
extern void wxHaikuReleaseNativeBitmaps();

wxApp::~wxApp()
{
    HAIKU_LOG("APP", "wxApp dtor this=%p bapp=%p fdio=%p",
                this, m_bApp.get(), m_fdioManager);
    delete m_fdioManager;
    wxHaikuReleaseNativeCursors();
    wxHaikuReleaseNativeBitmaps();
}

bool wxApp::Initialize(int& argc, wxChar** argv)
{
    HAIKU_LOG("APP", "Initialize ENTER this=%p argc=%d", this, argc);

    if (!wxAppBase::Initialize(argc, argv))
    {
        HAIKU_LOG("APP", "Initialize FAILED in wxAppBase");
        return false;
    }

    wxConvCurrent = &wxConvUTF8;

    wxString signature = "application/x-vnd.wxwidgets.app";
    if (!GetVendorName().IsEmpty() && !GetAppName().IsEmpty())
    {
        signature = wxString::Format("application/x-vnd.%s.%s",
                                     GetVendorName().Lower(),
                                     GetAppName().Lower());
    }

    // The executable's resource signature identifies it to Tracker and Deskbar.
    // Read it before constructing BApplication so registration agrees with the
    // application icon and avoids grouping unrelated programs under wxwidgets.app.
    int32 imageCookie = 0;
    image_info imageInfo;
    while (get_next_image_info(B_CURRENT_TEAM, &imageCookie, &imageInfo) == B_OK)
    {
        if (imageInfo.type != B_APP_IMAGE)
            continue;
        BFile executable(imageInfo.name, B_READ_ONLY);
        BAppFileInfo fileInfo(&executable);
        char resourceSignature[B_MIME_TYPE_LENGTH] = {};
        if (fileInfo.GetSignature(resourceSignature) == B_OK
            && BMimeType::IsValid(resourceSignature)
            && strncmp(resourceSignature, "application/", 12) == 0)
            signature = wxString::FromUTF8(resourceSignature);
        break;
    }

    HAIKU_LOG("APP", "Initialize appName='%s' vendor='%s' signature='%s'",
                GetAppName(), GetVendorName(), signature);

    try
    {
        m_bApp = std::make_unique<wxHaikuApplication>(signature.utf8_str());
        HAIKU_LOG("APP", "BApplication created ptr=%p looper=%p",
                    m_bApp.get(), m_bApp ? m_bApp->Looper() : nullptr);
    }
    catch (...)
    {
        wxLogError("Failed to create BApplication with signature: %s", signature);
        HAIKU_LOG("APP", "BApplication creation threw exception");
        return false;
    }

    m_fdioManager = new HaikuFDIOManager();
    HAIKU_LOG("APP", "FDIO manager created ptr=%p", m_fdioManager);
    HAIKU_LOG("APP", "Initialize EXIT success");
    return true;
}

int wxApp::OnRun()
{
    HAIKU_LOG("OnRun ENTER");

    if (!m_bApp)
    {
        HAIKU_LOG("OnRun ERROR no BApplication");
        return -1;
    }

    return wxAppBase::OnRun();
}

void wxApp::ExitMainLoop()
{
    HAIKU_LOG("ExitMainLoop called");

    wxAppBase::ExitMainLoop();
}

int wxApp::OnExit()
{
    wxHaikuGuiDispatchLock guard(be_app);
    HAIKU_LOG("APP", "OnExit ENTER fdio=%p", m_fdioManager);
    delete m_fdioManager;
    m_fdioManager = nullptr;
    const int rc = wxAppBase::OnExit();
    HAIKU_LOG("APP", "OnExit EXIT rc=%d", rc);
    return rc;
}

void wxApp::OnEventLoopEnter(wxEventLoopBase* loop)
{
    HAIKU_LOG("APP", "OnEventLoopEnter loop=%p", loop);
}

void wxApp::OnEventLoopExit(wxEventLoopBase* loop)
{
    HAIKU_LOG("APP", "OnEventLoopExit loop=%p", loop);
}

void wxApp::DoSetAppName()
{
    // Vendor/app name is baked into the BApplication signature at Initialize() time.
    // BApplication has no runtime API to change it afterwards.
    HAIKU_LOG("APP", "DoSetAppName appName='%s' bapp=%p", GetAppName(), m_bApp.get());
}

void wxApp::DoSetVendorName()
{
  if (!GetVendorName().IsEmpty())
        wxLogVerbose("Vendor name set to: %s", GetVendorName());
  
  
    HAIKU_LOG("APP", "DoSetVendorName vendor='%s'", GetVendorName());
}

void wxApp::ProcessSocketEvents()
{
   // HAIKU_LOG("APP", "ProcessSocketEvents fdio=%p", m_fdioManager);
    if (m_fdioManager)
        m_fdioManager->ProcessEvents();
}

bool wxApp::ProcessIdle()
{
    wxHaikuGuiDispatchLock guard(be_app);
  //  HAIKU_LOG("ProcessIdle");

    ProcessSocketEvents();
    return wxAppBase::ProcessIdle();
}

void wxApp::WakeUpIdle()
{
 //   HAIKU_LOG("APP", "WakeUpIdle bapp= looper=");

    if (m_bApp && m_bApp->Looper())
        m_bApp->Looper()->PostMessage('WXID');
}

void wxApp::CleanUp()
{
    wxHaikuGuiDispatchLock guard(be_app);
    HAIKU_LOG("APP", "CleanUp ENTER");
    wxAppBase::CleanUp();
    HAIKU_LOG("APP", "CleanUp EXIT");
}

wxAppTraits* wxApp::CreateTraits()
{
    return wxAppBase::CreateTraits();
}







