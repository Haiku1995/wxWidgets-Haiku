/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/apptraits.h
// Purpose:     wxAppTraits implementation for Haiku
// Author:      Based on Unix/Qt; adapted for Haiku
// Created:     2026-01-19
// Copyright:   (c) 2026 wxWidgets team
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_APPTRAITS_H_
#define _WX_HAIKU_APPTRAITS_H_


class WXDLLIMPEXP_BASE wxConsoleAppTraits : public wxConsoleAppTraitsBase
{
public:
#if wxUSE_CONSOLE_EVENTLOOP
    virtual wxEventLoopBase *CreateEventLoop() override;
#endif // wxUSE_CONSOLE_EVENTLOOP
#if wxUSE_TIMER
    virtual wxTimerImpl *CreateTimerImpl(wxTimer *timer) override;
#endif
};

#if wxUSE_GUI


#if defined(__WXGTK__) || defined(__WXQT__) || defined(__WXHAIKU__)
    #define wxHAS_GUI_FDIOMANAGER
    #define wxHAS_GUI_PROCESS_CALLBACKS
#endif // ports using wxFDIOManager

#if defined(__WXMAC__)
    #define wxHAS_GUI_PROCESS_CALLBACKS
    #define wxHAS_GUI_SOCKET_MANAGER
#endif

class WXDLLIMPEXP_CORE wxGUIAppTraits : public wxGUIAppTraitsBase
{
public:
    virtual wxEventLoopBase *CreateEventLoop() override;
    virtual int WaitForChild(wxExecuteData& execData) override;
#if wxUSE_TIMER
    virtual wxTimerImpl *CreateTimerImpl(wxTimer *timer) override;
#endif
#if wxUSE_THREADS && defined(__WXGTK__)
    virtual void MutexGuiEnter() override;
    virtual void MutexGuiLeave() override;
#endif

    wxPortId GetToolkitVersion(int *majVer = nullptr,
                               int *minVer = nullptr,
                               int *microVer = nullptr) const override;

    virtual wxString GetDesktopEnvironment() const override;

#if defined(__WXGTK__) || defined(__WXQT__)  || defined(__WXHAIKU__)
    virtual wxString GetPlatformDescription() const override;
#endif

#if defined(__WXGTK__) || defined(__WXHAIKU__)
    virtual bool ShowAssertDialog(const wxString& msg) override;
#endif

#if wxUSE_SOCKETS

#ifdef wxHAS_GUI_SOCKET_MANAGER
    virtual wxSocketManager *GetSocketManager() override;
#endif

#ifdef wxHAS_GUI_FDIOMANAGER
    virtual wxFDIOManager *GetFDIOManager() override;
#endif

#endif // wxUSE_SOCKETS

#if wxUSE_EVENTLOOP_SOURCE
    virtual wxEventLoopSourcesManagerBase* GetEventLoopSourcesManager() override;
#endif
};

#endif // wxUSE_GUI




#endif // _WX_HAIKU_APPTRAITS_H_
