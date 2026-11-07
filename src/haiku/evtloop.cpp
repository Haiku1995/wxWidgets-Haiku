/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/evtloop.cpp
// Author:      Haiku wxWidgets port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"
#include "wx/haiku/private/dispatch.h"
#if wxUSE_TIMER
    #include "wx/haiku/private/timer.h"
#endif
#include <cstdio>
#include <atomic>
#include <memory>
#include <map>
#include <mutex>
#include <sys/select.h>
#include <errno.h>

#include "wx/evtloop.h"
#include "wx/app.h"
#include "wx/apptrait.h"
#include "wx/private/eventloopsourcesmanager.h"
#include "wx/private/selectdispatcher.h"
#include "wx/private/fdioeventloopsourcehandler.h"
#include "wx/evtloopsrc.h"

#include <Application.h>
#include <Window.h>
#include <MessageQueue.h>
#include "wx/window.h"
#include <Looper.h>
#include <Message.h>
#include <MessageRunner.h>
#include <MessageFilter.h>
#include <Messenger.h>
#include <SupportDefs.h>
#include <OS.h>



// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

class wxHaikuEventLoopBase;

// ---------------------------------------------------------------------------
// Idle runner
// ---------------------------------------------------------------------------

class wxHaikuIdleRunner : public BMessageFilter, public wxRefCounter
{
public:
    explicit wxHaikuIdleRunner(wxHaikuEventLoopBase* owner);
    ~wxHaikuIdleRunner() override;

    filter_result Filter(BMessage* message, BHandler** target) override;

    void ScheduleIdleCheck();
    void IdleTimeout();

private:
    BMessenger TargetMessenger() const;

    wxHaikuEventLoopBase* m_owner;
    BMessageRunner*      m_runner;
};

enum
{
    MSG_WX_HAIKU_IDLE = 'wXid'
};

static wxObjectDataPtr<wxHaikuIdleRunner> gs_idleRunner;

// Only the outer wx loop owns the native application loop.
static std::atomic<wxHaikuEventLoopBase*> gs_nativeLoop{nullptr};
static std::mutex gs_modalExitMutex;
static std::map<wxHaikuEventLoopBase*, int> gs_modalExitCodes;

// ---------------------------------------------------------------------------
// wxHaikuIdleRunner implementation
// ---------------------------------------------------------------------------

wxHaikuIdleRunner::wxHaikuIdleRunner(wxHaikuEventLoopBase* owner)
    : BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE)
    , m_owner(owner)
    , m_runner(nullptr)
{
}

wxHaikuIdleRunner::~wxHaikuIdleRunner()
{
    delete m_runner;
}

filter_result
wxHaikuIdleRunner::Filter(BMessage* message, BHandler** WXUNUSED(target))
{
    if (message && message->what == MSG_WX_HAIKU_IDLE)
    {
        IdleTimeout();
        return B_SKIP_MESSAGE;
    }

    if (!m_runner)
        ScheduleIdleCheck();

    return B_DISPATCH_MESSAGE;
}

void wxHaikuIdleRunner::IdleTimeout()
{
    wxHaikuGuiDispatchLock guard(be_app);
    // The one-shot runner has fired. Release it before deciding whether to
    // schedule another idle pass.
    delete m_runner;
    m_runner = nullptr;
    if (wxTheApp)
        wxTheApp->ProcessPendingEvents();

    if (wxTheApp && wxTheApp->ProcessIdle())
    {
        ScheduleIdleCheck();
    }
    else
    {
        delete m_runner;
        m_runner = nullptr;
    }
}

BMessenger wxHaikuIdleRunner::TargetMessenger() const
{
    if (m_owner && m_owner->m_looper)
        return BMessenger(m_owner->m_looper);

    if (be_app)
        return BMessenger(be_app);

    return BMessenger();
}

void wxHaikuIdleRunner::ScheduleIdleCheck()
{
    if (m_runner)
        return;

    BMessenger msgr = TargetMessenger();
    if (!msgr.IsValid())
        return;

    BMessage msg(MSG_WX_HAIKU_IDLE);
    m_runner = new BMessageRunner(msgr, &msg, 1, 1);
}

// ---------------------------------------------------------------------------
// wxHaikuEventLoopBase
// ---------------------------------------------------------------------------

wxHaikuEventLoopBase::wxHaikuEventLoopBase()
{
    // Create a new instance held by the smart pointer
    m_idleRunner.reset(new wxHaikuIdleRunner(this));

    // DoRun() dispatches be_app when it exists, so the idle filter must
    // be attached to that same looper; otherwise deferred wx destruction
    // (wxPendingDelete) is never processed.
    m_looper = be_app ? static_cast<BLooper*>(be_app)
                      : new BLooper("wx_haiku_event_loop");
    if (m_looper->Lock()) {
        // Use .get() to pass the raw BMessageFilter*
        m_looper->AddCommonFilter(m_idleRunner.get());
        m_looper->Unlock();
    }
}



wxHaikuEventLoopBase::~wxHaikuEventLoopBase()
{
    if (m_looper)
    {
        if (m_looper->Lock())
        {
            // 1. Explicitly remove the filter from the Haiku looper FIRST
            if (m_idleRunner)
                m_looper->RemoveCommonFilter(m_idleRunner.get());
            
            // 2. Only Quit if it's not the global be_app
            if (m_looper != be_app)
                m_looper->Quit(); // self-destructs
            else
                m_looper->Unlock();
        }
    }

    // 3. NOW it is safe to clear the smart pointer
    m_idleRunner.reset(nullptr);
    m_looper = nullptr;
}







// A modal callback entered on a BWindow thread suspends that window's native
// message loop. Keep exposure painting alive, and consume input while its wx
// owner is disabled so it cannot be replayed after the modal loop returns.
void wxHaikuServiceModalWindow()
{
    BWindow* window = dynamic_cast<BWindow*>(
        BLooper::LooperForThread(find_thread(nullptr)));
    if (!window || !window->Lock())
        return;

    // This public API also moves pending native-port messages into the queue.
    // It must run on the window's own thread and with serialized wx dispatch.
    window->UpdateIfNeeded();

    wxWindowHaiku* owner = wxWindowHaiku::HaikuRetrieveWindowPointer(window->ChildAt(0));
    if (owner && !owner->IsEnabled())
    {
        BMessageQueue* queue = window->MessageQueue();
        if (queue->Lock())
        {
            for (int32 i = queue->CountMessages() - 1; i >= 0; --i)
            {
                BMessage* message = queue->FindMessage(i);
                switch (message->what)
                {
                    case B_MOUSE_DOWN: case B_MOUSE_UP: case B_MOUSE_MOVED:
                    case B_MOUSE_WHEEL_CHANGED: case B_KEY_DOWN: case B_KEY_UP:
                    case B_UNMAPPED_KEY_DOWN: case B_UNMAPPED_KEY_UP:
                        queue->RemoveMessage(message);
                        delete message;
                        break;
                    default:
                        break;
                }
            }
            queue->Unlock();
        }
    }
    window->Unlock();
}

int wxHaikuEventLoopBase::DoRun()
{
    wxHaikuEventLoopBase* expected = nullptr;
    if (!gs_nativeLoop.compare_exchange_strong(expected, this))
    {
        // Each native BWindow already has a running looper. A modal wx loop
        // must not call BApplication::Run() again or quit it on dismissal.
        fprintf(stderr, "[MODAL423] nested loop enter thread=%ld\n", (long)find_thread(nullptr));
        for (;;)
        {
            wxHaikuGuiDispatchLock guard;
            wxHaikuServiceModalWindow();
            if (m_shouldExit)
                break;
#if wxUSE_TIMER
            // On the application thread, the native application loop is
            // suspended inside this call, so it cannot deliver timer messages.
            const bool firedTimer = wxHaikuDispatchDueTimer();
            if (m_shouldExit)
                break;
#endif
            if (wxTheApp)
            {
                wxTheApp->ProcessPendingEvents();
                wxTheApp->ProcessIdle();
            }
            if (!m_shouldExit)
            {
                // Release the originating window looper and the wx GUI lock
                // so the application and modal window can deliver callbacks.
                wxHaikuGuiDispatchPause pause;
#if wxUSE_TIMER
                if (firedTimer)
                    continue;
#endif
                snooze(10000);
            }
        }
        int rc = 0;
        {
            std::lock_guard<std::mutex> lock(gs_modalExitMutex);
            rc = gs_modalExitCodes[this];
            gs_modalExitCodes.erase(this);
        }
        fprintf(stderr, "[MODAL423] nested loop exit rc=%d\n", rc);
        return rc;
    }

    struct ResetNativeLoop
    {
        ~ResetNativeLoop() { gs_nativeLoop.store(nullptr); }
    } resetNativeLoop;
    ScheduleIdleCheck();
    if (be_app)
        be_app->Run();
    else if (m_looper)
        m_looper->Run();

    return 0;
}

void wxHaikuEventLoopBase::DoStop(int rc)
{
    if (gs_nativeLoop.load() != this)
    {
        {
            std::lock_guard<std::mutex> lock(gs_modalExitMutex);
            gs_modalExitCodes[this] = rc;
        }
        WakeUp();
        return;
    }
    if (be_app)
        be_app->PostMessage(B_QUIT_REQUESTED);
    else if (m_looper)
        m_looper->PostMessage(B_QUIT_REQUESTED);
}

bool wxHaikuEventLoopBase::Pending() const
{
    // BLooper owns the native queue.  Reporting it empty unconditionally
    // breaks wx's nested/modal loop machinery and defers pending wx events.
    if (!m_looper || !m_looper->Lock()) return false;
    const bool pending = m_looper->IsMessageWaiting();
    m_looper->Unlock();
    return pending;
}

bool wxHaikuEventLoopBase::Dispatch()
{
    return be_app || m_looper;
}

int wxHaikuEventLoopBase::DispatchTimeout(unsigned long timeout)
{
    snooze(static_cast<bigtime_t>(timeout) * 1000);
    return true;
}

void wxHaikuEventLoopBase::WakeUp()
{
    BMessenger msgr(be_app ? be_app : m_looper);
    if (msgr.IsValid())
    {
        BMessage msg('wake');
        msgr.SendMessage(&msg);
    }
}

void wxHaikuEventLoopBase::DoYieldFor(long eventsToProcess)
{
    {
        wxHaikuGuiDispatchPause pause;
        snooze(1000);
    }
#if wxUSE_TIMER
    if (eventsToProcess & wxEVT_CATEGORY_TIMER)
    {
        wxHaikuGuiDispatchLock guard(be_app);
        wxHaikuDispatchDueTimer();
    }
#endif
    wxEventLoopBase::DoYieldFor(eventsToProcess);
}

void wxHaikuEventLoopBase::ScheduleIdleCheck()
{
    if (IsInsideRun() && !m_shouldExit && m_idleRunner)
        m_idleRunner->ScheduleIdleCheck();
}

// ---------------------------------------------------------------------------
// GUI event loop
// ---------------------------------------------------------------------------

#if wxUSE_GUI
wxGUIEventLoop::wxGUIEventLoop()
{
    if (be_app && m_looper != be_app)
    {
        // Clean up the private looper created by the base class constructor
        if (m_looper && m_looper->Lock())
        {
            m_looper->RemoveCommonFilter(m_idleRunner.get());
            m_looper->Quit();
        }

        m_looper = be_app;
        
        // Attach our loop's filter to be_app
        if (m_looper->Lock())
        {
            m_looper->AddCommonFilter(m_idleRunner.get());
            m_looper->Unlock();
        }
    }
}
#endif



//////////////////////////////////////////////////
//loopsource
// ----------------------------------------------------------------------------
// wxHaikuEventLoopSource
// ----------------------------------------------------------------------------

namespace {
struct wxHaikuFDState
{
    std::atomic<bool> active{true};
    std::atomic<bool> queued{false};
    wxHaikuEventLoopSource* source;
    int fd;
    int flags;
};
std::mutex gs_fdMutex;
std::map<wxHaikuEventLoopSource*, std::shared_ptr<wxHaikuFDState>> gs_fdStates;
}

wxHaikuEventLoopSource::wxHaikuEventLoopSource(wxFDIODispatcher* dispatcher,
    wxFDIOHandler* fdioHandler, int fd, wxEventLoopSourceHandler* handler, int flags)
    : wxEventLoopSource(handler, flags), m_dispatcher(dispatcher),
      m_fdioHandler(fdioHandler), m_fd(fd), m_flags(flags),
      m_thread(-1), m_shouldExit(false)
{
    wxCHECK_RET(dispatcher && fdioHandler, "invalid dispatcher or handler");
    auto state = std::make_shared<wxHaikuFDState>();
    state->source = this;
    state->fd = fd;
    state->flags = flags;
    {
        std::lock_guard<std::mutex> lock(gs_fdMutex);
        gs_fdStates[this] = state;
    }
    auto* argument = new std::shared_ptr<wxHaikuFDState>(state);
    m_thread = spawn_thread(MonitorThread, "wxHaikuEventLoopSource", B_NORMAL_PRIORITY, argument);
    if (m_thread >= 0)
        resume_thread(m_thread);
    else
    {
        delete argument;
        wxLogError("Failed to spawn wxHaikuEventLoopSource monitor thread");
    }
}

wxHaikuEventLoopSource::~wxHaikuEventLoopSource()
{
    {
        std::lock_guard<std::mutex> lock(gs_fdMutex);
        auto found = gs_fdStates.find(this);
        if (found != gs_fdStates.end())
        {
            found->second->active = false;
            gs_fdStates.erase(found);
        }
    }
    if (m_thread >= 0)
    {
        status_t result;
        wait_for_thread(m_thread, &result);
    }
    if (m_dispatcher)
        m_dispatcher->UnregisterFD(m_fd);
    delete m_fdioHandler;
}

int32 wxHaikuEventLoopSource::MonitorThread(void* argument)
{
    std::unique_ptr<std::shared_ptr<wxHaikuFDState>> owner(
        static_cast<std::shared_ptr<wxHaikuFDState>*>(argument));
    const auto state = *owner;
    while (state->active)
    {
        if (state->queued) { snooze(1000); continue; }
        fd_set reads, writes, exceptions;
        FD_ZERO(&reads); FD_ZERO(&writes); FD_ZERO(&exceptions);
        if (state->flags & wxEVENT_SOURCE_INPUT) FD_SET(state->fd, &reads);
        if (state->flags & wxEVENT_SOURCE_OUTPUT) FD_SET(state->fd, &writes);
        if (state->flags & wxEVENT_SOURCE_EXCEPTION) FD_SET(state->fd, &exceptions);
        timeval timeout{0, 100000};
        int ready = select(state->fd + 1, &reads, &writes, &exceptions, &timeout);
        if (ready < 0) { if (errno == EINTR) continue; break; }
        if (!ready || !state->active || !wxTheApp) continue;
        const bool read = FD_ISSET(state->fd, &reads);
        const bool write = FD_ISSET(state->fd, &writes);
        const bool exception = FD_ISSET(state->fd, &exceptions);
        state->queued = true;
        wxTheApp->CallAfter([state, read, write, exception]() {
            // A callback may delete its source. Check before every subsequent call.
            if (state->active && read) state->source->GetHandler()->OnReadWaiting();
            if (state->active && write) state->source->GetHandler()->OnWriteWaiting();
            if (state->active && exception) state->source->GetHandler()->OnExceptionWaiting();
            state->queued = false;
        });
    }
    return 0;
}

// end loop source
////////////////////////////////////////////////////
// ---------------------------------------------------------------------------
// FD watching (event loop sources manager)
// ---------------------------------------------------------------------------

#if wxUSE_EVENTLOOP_SOURCE

// Bridge to handle FD events via wxEventLoopSourceHandler
class wxHaikuFDIOHandler : public wxFDIOHandler
{
public:
    explicit wxHaikuFDIOHandler(wxEventLoopSourceHandler* handler)
        : m_handler(handler) {}

    void OnReadWaiting() override   { m_handler->OnReadWaiting(); }
    void OnWriteWaiting() override  { m_handler->OnWriteWaiting(); }
    void OnExceptionWaiting() override { m_handler->OnExceptionWaiting(); }

private:
    wxEventLoopSourceHandler* m_handler;
};

// Event loop sources manager for Haiku
class wxHaikuEventLoopSourcesManager final : public wxEventLoopSourcesManagerBase
{
public:
    wxEventLoopSource* AddSourceForFD(int fd,
                                      wxEventLoopSourceHandler* handler,
                                      int flags) override
    {
        wxCHECK_MSG(fd >= 0 && fd < FD_SETSIZE, nullptr, "invalid fd");
        wxCHECK_MSG(handler && (flags & wxEVENT_SOURCE_ALL), nullptr, "invalid handler or flags");

        wxFDIOHandler* fdioHandler = new wxHaikuFDIOHandler(handler);

        if (!wxFDIODispatcher::Get()->RegisterFD(fd, fdioHandler, flags))
        {
            delete fdioHandler;
            return nullptr;
        }

        return new wxHaikuEventLoopSource(wxFDIODispatcher::Get(),
                                  fdioHandler,
                                  fd,
                                  handler,
                                  flags);

    }
};

wxEventLoopSourcesManagerBase* wxGUIAppTraits::GetEventLoopSourcesManager()
{
    static wxHaikuEventLoopSourcesManager s_eventLoopSourcesManager;
    return &s_eventLoopSourcesManager;
}

#if !wxUSE_CONSOLE_EVENTLOOP

wxEventLoopSourcesManagerBase* wxAppTraits::GetEventLoopSourcesManager()
{
    static wxHaikuEventLoopSourcesManager s_eventLoopSourcesManager;
    return &s_eventLoopSourcesManager;
}

#endif // !wxUSE_CONSOLE_EVENTLOOP

#endif // wxUSE_EVENTLOOP_SOURCE
