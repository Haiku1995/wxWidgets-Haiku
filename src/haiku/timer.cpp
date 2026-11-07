/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/timer.cpp
// Purpose:     wxTimer implementation using Haiku message runners
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#if wxUSE_TIMER

#include "wx/haiku/private/timer.h"
#include <Application.h>
#include <algorithm>
#include <Autolock.h>
#include <memory>
#include <Handler.h>
#include <Message.h>
#include <Messenger.h>
#include <vector>

// Each Start() gets a fresh native handler/token. Removing it in Stop()
// invalidates already queued messages, including messages from a prior run.
// Native looper locking serializes notification with Stop() and destruction.
class wxHaikuTimerHandler : public BHandler
{
public:
    explicit wxHaikuTimerHandler(wxHaikuTimerImpl* timer)
        : BHandler("wxHaikuTimerHandler"),
          m_timer(timer),
          m_interval(static_cast<bigtime_t>(timer->GetInterval()) * 1000),
          m_deadline(system_time() + m_interval)
    {
    }

    bigtime_t GetDeadline() const { return m_deadline; }

    void MessageReceived(BMessage* message) override
    {
        if (message->what != 'TMR_')
        {
            BHandler::MessageReceived(message);
            return;
        }

        const bigtime_t now = system_time();
        if (m_deadline <= now)
            Fire(now);
    }

    void Fire(bigtime_t now)
    {
        wxHaikuTimerImpl* const timer = m_timer;
        if (timer->IsOneShot())
            timer->Stop();
        else
            m_deadline += ((now - m_deadline) / m_interval + 1) * m_interval;

        // A native message can arrive after a nested loop handled its tick.
        // The deadline above has already moved past the consumed occurrence.
        // Stop() may have deleted this handler. Notify() may delete the timer.
        timer->Notify();
    }

private:
    wxHaikuTimerImpl* const m_timer;
    const bigtime_t m_interval;
    bigtime_t m_deadline;
};

namespace
{
// Access is serialized by the application looper lock, as are the handlers.
std::vector<wxHaikuTimerHandler*> gs_timers;
}

bool wxHaikuDispatchDueTimer()
{
    if (!be_app || be_app->Thread() != find_thread(nullptr))
        return false;

    BAutolock lock(be_app);
    if (!lock.IsLocked())
        return false;

    const auto next = std::min_element(gs_timers.begin(), gs_timers.end(),
        [](const wxHaikuTimerHandler* lhs, const wxHaikuTimerHandler* rhs)
        {
            return lhs->GetDeadline() < rhs->GetDeadline();
        });
    const bigtime_t now = system_time();
    if (next == gs_timers.end() || (*next)->GetDeadline() > now)
        return false;

    // The callback may alter the list or enter another nested loop.
    (*next)->Fire(now);
    return true;
}

wxHaikuTimerImpl::wxHaikuTimerImpl(wxTimer* timer)
    : wxTimerImpl(timer), m_runner(nullptr), m_handler(nullptr)
{
}

wxHaikuTimerImpl::~wxHaikuTimerImpl()
{
    Stop();
}

bool wxHaikuTimerImpl::Start(int millisecs, bool oneShot)
{
    if (!wxTimerImpl::Start(millisecs, oneShot))
        return false;

    if (!be_app || GetInterval() <= 0)
        return false;

    BAutolock lock(be_app);
    if (!lock.IsLocked())
        return false;

    m_handler = new wxHaikuTimerHandler(this);
    be_app->AddHandler(m_handler);
    gs_timers.push_back(static_cast<wxHaikuTimerHandler*>(m_handler));

    const BMessage message('TMR_');
    m_runner = new BMessageRunner(BMessenger(m_handler), &message,
                                 static_cast<bigtime_t>(GetInterval()) * 1000,
                                 IsOneShot() ? 1 : -1);
    if (m_runner->InitCheck() != B_OK)
    {
        Stop();
        return false;
    }

    return true;
}

void wxHaikuTimerImpl::Stop()
{
    BLooper* const looper = m_handler ? m_handler->Looper() : nullptr;
    std::unique_ptr<BAutolock> lock(looper ? new BAutolock(looper) : nullptr);
    if (lock && !lock->IsLocked())
        return;

    delete m_runner;
    m_runner = nullptr;

    if (m_handler)
    {
        const auto it = std::find(gs_timers.begin(), gs_timers.end(), m_handler);
        if (it != gs_timers.end())
            gs_timers.erase(it);
        if (looper)
            looper->RemoveHandler(m_handler);
        delete m_handler;
        m_handler = nullptr;
    }
}

bool wxHaikuTimerImpl::IsRunning() const
{
    return m_runner != nullptr;
}

#endif // wxUSE_TIMER
