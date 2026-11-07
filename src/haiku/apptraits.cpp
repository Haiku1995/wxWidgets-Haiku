/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/apptraits.cpp
// Purpose:     wxAppTraits implementation for Haiku
// Author:      Based on Unix/Qt; adapted for Haiku
// Created:     2026-01-19
// Copyright:   (c) 2026 wxWidgets team
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/apptrait.h"

#ifndef WX_PRECOMP
    #include "wx/utils.h"
#endif // WX_PRECOMP


#include "wx/apptrait.h"
#include "wx/haiku/private/timer.h"
#include "wx/msgout.h"
#include "wx/log.h"
#include "wx/stdpaths.h"
#include "wx/evtloop.h"
#include "wx/timer.h"
#include <Alert.h>
#include <Application.h>
#include <unistd.h>
#include <sys/wait.h>
#include <cerrno>
#include <memory>
#include "wx/unix/private/execute.h"
#include "wx/haiku/private/dispatch.h"

//wxGUIAppTraits::wxGUIAppTraits() = default;
//wxGUIAppTraits::~wxGUIAppTraits() = default;

wxEventLoopBase* wxGUIAppTraits::CreateEventLoop()
{
    return new wxEventLoop;
}

#if wxUSE_TIMER
wxTimerImpl* wxGUIAppTraits::CreateTimerImpl(wxTimer* timer)
{
    return new wxHaikuTimerImpl(timer);
}
#endif

wxPortId wxGUIAppTraits::GetToolkitVersion(int* majVer,
                                           int* minVer,
                                           int* microVer) const
{
    if (majVer) *majVer = 1;
    if (minVer) *minVer = 0;
    if (microVer) *microVer = 0;

    return wxPORT_HAIKU;
}
int wxGUIAppTraits::WaitForChild(wxExecuteData& execData)
{
    // A Unix console loop cannot service Haiku's GUI notification sources,
    // especially during OnInit(). Drain both pipes and check child status
    // directly while still processing queued GUI callbacks.
    std::unique_ptr<wxWindowDisabler> disabler;
    if (!(execData.m_flags & wxEXEC_NODISABLE))
        disabler.reset(new wxWindowDisabler);
    while (execData.m_pid)
    {
        {
            wxHaikuGuiDispatchLock guard(be_app);
            wxTheApp->CheckSignal();
            if (!execData.m_pid)
                break;
#if wxUSE_STREAMS
            if (execData.IsRedirected())
            {
                execData.m_bufOut.Update();
                execData.m_bufErr.Update();
            }
#endif
            int status = 0;
            const pid_t pid = waitpid(execData.m_pid, &status, WNOHANG);
            if (pid > 0)
            {
                const int code = WIFEXITED(status)
                    ? static_cast<signed char>(WEXITSTATUS(status))
                    : WIFSIGNALED(status) ? -WTERMSIG(status) : -1;
                execData.OnExit(code);
                break;
            }
            if (pid < 0 && errno != EINTR)
            {
                wxLogLastError("waitpid");
                execData.OnExit(-1);
                break;
            }
            if (!(execData.m_flags & wxEXEC_NOEVENTS))
            {
                wxTheApp->ProcessPendingEvents();
                if (wxApp::IsMainLoopRunning())
                    wxTheApp->ProcessIdle();
            }
        }
        wxHaikuGuiDispatchPause pause;
        snooze(10000);
    }
    return execData.m_exitcode;
}


wxString wxGUIAppTraits::GetDesktopEnvironment() const
{
    return "Haiku Desktop";
}

bool wxGUIAppTraits::ShowAssertDialog(const wxString& msg)
{
    BAlert* alert = new BAlert("Assertion Failed", msg.utf8_str(), "Abort");
    alert->Go();
    return false; // don't suppress
}


#if wxUSE_THREADS && defined(__WXGTK__)
void wxGUIAppTraits::MutexGuiEnter()
{
    wxGUIAppTraitsBase::MutexGuiEnter();
}

void wxGUIAppTraits::MutexGuiLeave()
{
    wxGUIAppTraitsBase::MutexGuiLeave();
}
#endif // wxUSE_THREADS && __WXGTK__
/*
bool wxGUIAppTraits::HasStderr()
{
     return isatty(STDERR_FILENO) != 0;
}

bool wxGUIAppTraits::CanUseStderr()
{
    return true;
}
*/
