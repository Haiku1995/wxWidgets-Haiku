/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/toplevel.cpp
// Purpose:     wxTopLevelWindow implementation for Haiku
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"
#include "wx/app.h"
#include "wx/haiku/private/dispatch.h"
#include <Messenger.h>
#include <cstdio>

#include "wx/toplevel.h"
#include "wx/icon.h"
#include "wx/dcclient.h"
#include "wx/haiku/private/converter.h"
#include "wx/log.h"
#include "wx/frame.h"

#include <AppKit.h>
#include <InterfaceKit.h>
#include <Screen.h>

#include "wx/haiku/private/framewin.h"
#include "wx/haiku/private/windowview.h"
#include "wx/haiku/private/viewmap.h"


// ----------------------------------------------------------------------------
// Debug logging
// ----------------------------------------------------------------------------

#if defined(WX_DEBUG_HAIKU) && WX_DEBUG_HAIKU
    #define HAIKU_LOG(msg, ...) \
        wxLogDebug("[HAIKU-FRAME:%s:%d] " msg, __func__, __LINE__, ##__VA_ARGS__)
#else
    #define HAIKU_LOG(msg, ...) ((void)0)
#endif

static void HaikuDumpWindow(const char* label, BWindow* w)
{
    wxUnusedVar(label);
    wxUnusedVar(w);
}

static void HaikuDumpView(const char* label, BView* v)
{
    wxUnusedVar(label);
    wxUnusedVar(v);
}

// ----------------------------------------------------------------------------
// Native BWindow subclass
// ----------------------------------------------------------------------------

class wxHaikuToplevelWindow : public BWindow
{
public:
    wxHaikuToplevelWindow(BRect frame, const char* title,
                          window_look look, window_feel feel,
                          uint32 flags,
                          wxTopLevelWindowHaiku* handler)
        : BWindow(frame, title, look, feel, flags),
          m_handler(handler)
    {
        HaikuDumpWindow("TLW ctor", this);
    }

    void SetHandler(wxTopLevelWindowHaiku* handler)
    {
        m_handler = handler;
    }

    bool QuitRequested() override
    {
        wxHaikuGuiDispatchLock guard(this);
        HaikuDumpWindow("TLW QuitRequested window", this);
        wxTopLevelWindowHaiku* handler = m_handler;
        if ( !handler )
            return true;

        const bool closed = handler->Close();
        HAIKU_LOG("QuitRequested Close=%d", closed);
        if (closed && wxTheApp)
            wxTheApp->WakeUpIdle();

        // wxWidgets owns deferred wrapper destruction; the native BWindow
        // is released by wxTopLevelWindowHaiku::~wxTopLevelWindowHaiku().
        return false;
    }

    void DispatchMessage(BMessage* message, BHandler* target) override
    {
        // Native Quit() can terminate a looper thread without unwinding C++
        // guards. Protect wx close handling in QuitRequested(), not Quit().
        if (message && message->what == B_QUIT_REQUESTED)
        {
            BWindow::DispatchMessage(message, target);
            return;
        }
        BMessenger destination(target);
        wxHaikuGuiDispatchLock guard(this);
        // IsValid() checks the port, not the handler token. The port may
        // remain valid after a timer/view handler was removed during the wait.
        BHandler* const resolved = target ? destination.Target(nullptr) : nullptr;
        if (target && (!resolved || resolved->Looper() != this))
            return;
        if (message && message->what == 'wxUp' && resolved == this)
        {
            UpdateIfNeeded();
            message->SendReply(B_REPLY);
            return;
        }
        BWindow::DispatchMessage(message, resolved);
    }

    void FrameResized(float newWidth, float newHeight) override
    {
        BWindow::FrameResized(newWidth, newHeight);
        HaikuDumpWindow("TLW after native FrameResized", this);

        wxTopLevelWindowHaiku* handler = m_handler;
        if ( !handler )
            return;

        BView* parentView = handler->HaikuGetParentView();
        if ( parentView )
        {
            parentView->ResizeTo(newWidth, newHeight);
            HaikuDumpView("TLW parent view after resize", parentView);
        }

        wxEvtHandler* evtHandler = handler->GetEventHandler();
        if ( !evtHandler )
            return;

        wxSizeEvent ev(wxSize((int)(newWidth + 1), (int)(newHeight + 1)));
        ev.SetEventObject(handler);
        evtHandler->ProcessEvent(ev);
    }

    void FrameMoved(BPoint origin) override
    {
        BWindow::FrameMoved(origin);
        HaikuDumpWindow("TLW after native FrameMoved", this);

        wxTopLevelWindowHaiku* handler = m_handler;
        if ( !handler )
            return;

        wxEvtHandler* evtHandler = handler->GetEventHandler();
        if ( !evtHandler )
            return;

        wxMoveEvent ev(wxPoint((int)origin.x, (int)origin.y));
        ev.SetEventObject(handler);
        evtHandler->ProcessEvent(ev);
    }

    void WindowActivated(bool active) override
    {
        BWindow::WindowActivated(active);
        HaikuDumpWindow("TLW after WindowActivated", this);

        wxTopLevelWindowHaiku* handler = m_handler;
        if ( !handler )
            return;

        wxEvtHandler* evtHandler = handler->GetEventHandler();
        if ( !evtHandler )
            return;

        wxActivateEvent ev(wxEVT_ACTIVATE, active);
        ev.SetEventObject(handler);
        evtHandler->ProcessEvent(ev);
    }

private:
    wxTopLevelWindowHaiku* m_handler;
};

// ----------------------------------------------------------------------------
// Local helper
// ----------------------------------------------------------------------------

static void wxHaikuClearToplevelHandler(BWindow* win)
{
    if ( !win )
        return;

    wxHaikuToplevelWindow* tlw = dynamic_cast<wxHaikuToplevelWindow*>(win);
    if ( !tlw )
        return;

    if ( win->LockLooper() )
    {
        tlw->SetHandler(nullptr);
        win->UnlockLooper();
    }
    else
    {
        tlw->SetHandler(nullptr);
    }
}


static void wxHaikuDestroyNativeToplevel(BWindow*& win)
{
    if ( !win )
        return;

    BWindow* native = win;
    win = nullptr;

    wxHaikuClearToplevelHandler(native);

    // A never-shown BWindow has no looper thread to consume a quit message.
    // Quit() deletes an unstarted looper synchronously under its lock.
    if (native->Thread() < B_OK)
    {
        if (native->LockLooper())
            native->Quit();
    }
    else
    {
        native->PostMessage(B_QUIT_REQUESTED);
    }
}

// ----------------------------------------------------------------------------
// wxTopLevelWindowHaiku
// ----------------------------------------------------------------------------

wxTopLevelWindowHaiku::wxTopLevelWindowHaiku(wxWindow* parent,
                                             wxWindowID winId,
                                             const wxString& title,
                                             const wxPoint& pos,
                                             const wxSize& size,
                                             long style,
                                             const wxString& name)
{
    Create(parent, winId, title, pos, size, style, name);
}

// Add this destructor in the cpp and declare it in the header if not declared.
wxTopLevelWindowHaiku::~wxTopLevelWindowHaiku()
{
    SendDestroyEvent();
    DestroyChildren();
    wxHaiku::SetWxForView(m_rootView, nullptr);
    HaikuAdoptView(nullptr);
    m_rootView = nullptr;
    wxTopLevelWindows.DeleteObject(this);
    wxHaikuDestroyNativeToplevel(m_haikuWindow);
}

bool wxTopLevelWindowHaiku::Create(wxWindow* parent,
                                   wxWindowID winId,
                                   const wxString& title,
                                   const wxPoint& pos,
                                   const wxSize& size,
                                   long style,
                                   const wxString& name)
{
    wxTopLevelWindows.Append(this);

    if ( !wxTopLevelWindowBase::CreateBase(parent, winId, pos, size, style,
                                           wxDefaultValidator, name) )
    {
        wxFAIL_MSG("wxTopLevelWindowHaiku CreateBase failed");
        return false;
    }

    // CreateBase stores the parent pointer but does not establish ownership.
    // Owned top-level windows need logical registration without attaching
    // their native root view to the parent's separate BWindow.
    if (parent)
        parent->wxWindowBase::AddChild(this);

    if ( m_haikuWindow )
    {
        if ( !m_rootView )
            m_rootView = GetHandle();

        SetTitle(title);
        SetWindowStyleFlag(style);

        HaikuDumpWindow("TLW Create reused native", m_haikuWindow);
        HaikuDumpView("TLW Create reused root", m_rootView);
        return true;
    }

    const wxSize s(size.x > 0 ? size.x : 400,
                   size.y > 0 ? size.y : 300);

    BRect rect(100.0f, 100.0f,
               100.0f + (float)s.GetWidth()  - 1.0f,
               100.0f + (float)s.GetHeight() - 1.0f);

    if ( pos != wxDefaultPosition )
        rect.OffsetTo((float)pos.x, (float)pos.y);

    if ( wxFrame* frame = wxDynamicCast(this, wxFrame) )
    {
        m_haikuWindow = wxHaikuCreateFrameWindow(rect,
                                                 title.utf8_str(),
                                                 B_TITLED_WINDOW_LOOK,
                                                 B_NORMAL_WINDOW_FEEL,
                                                 B_ASYNCHRONOUS_CONTROLS,
                                                 frame);

        m_rootView = wxHaikuFrameGetClientView(m_haikuWindow);
    }
    else
    {
        m_haikuWindow = new wxHaikuToplevelWindow(rect,
                                                  title.utf8_str(),
                                                  B_TITLED_WINDOW_LOOK,
                                                  B_NORMAL_WINDOW_FEEL,
                                                  B_ASYNCHRONOUS_CONTROLS,
                                                  this);

        m_rootView = new wxHaikuWindowView(m_haikuWindow->Bounds(),
                               "wx_root",
                               B_FOLLOW_ALL_SIDES,
                               B_WILL_DRAW | B_FRAME_EVENTS);
        m_rootView->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

        HaikuDumpView("TLW generic root before attach", m_rootView);

        if ( m_haikuWindow->LockLooper() )
        {
            m_haikuWindow->AddChild(m_rootView);
            m_haikuWindow->UnlockLooper();
        }
    }

    HaikuDumpWindow("TLW Create native window", m_haikuWindow);
    HaikuDumpView("TLW Create root before adopt", m_rootView);

    HaikuAdoptView(m_rootView);
    wxHaiku::SetWxForView(m_rootView, this);

    HaikuDumpView("TLW Create root after adopt", m_rootView);

    SetTitle(title);
    SetWindowStyleFlag(style);

    // A never-shown window must remain accessible from other event threads.
    // Release BWindow's initial creator lock without starting callbacks during
    // construction of its wx children.
    m_haikuWindow->UnlockLooper();

    return true;
}

BWindow* wxTopLevelWindowHaiku::GetHaikuWindow() const
{
    return m_haikuWindow;
}

BView* wxTopLevelWindowHaiku::HaikuGetParentView() const
{
    return m_rootView;
}

bool wxTopLevelWindowHaiku::Show(bool show)
{
    if ( !wxTopLevelWindowBase::Show(show) )
        return false;

    bool shownNow = false;

    if ( BWindow* w = GetHaikuWindow() )
    {
        const bool wasHidden = w->IsHidden();

        if ( w->LockLooper() )
        {
            if ( show && w->IsHidden() )
            {
                // First Show() starts the looper and consumes one lock in
                // Run(). Preserve our outer lock until layout is complete.
                if (w->Thread() < B_OK)
                    w->LockLooper();
                w->Show();
                shownNow = wasHidden;
            }
            else if ( !show && !w->IsHidden() )
            {
                w->Hide();
            }

            if ( show && m_rootView )
                m_rootView->Invalidate();

            w->UnlockLooper();
        }
    }

    if ( show && shownNow )
    {
        SendSizeEvent();
        Layout();
        Refresh();
    }
    else if ( show )
    {
        Refresh();
    }

    return true;
}

void wxTopLevelWindowHaiku::Maximize(bool maximize)
{
    BWindow* w = GetHaikuWindow();
    if ( !w )
        return;

    if ( !w->LockLooper() )
        return;

    HaikuDumpWindow("TLW Maximize before", w);

    if ( maximize )
    {
        m_savedFrame = w->Frame();

        BScreen screen(w);
        if ( screen.IsValid() )
        {
            const BRect r = screen.Frame();
            w->MoveTo(r.LeftTop());
            w->ResizeTo(r.Width(), r.Height());
        }
    }
    else
    {
        w->MoveTo(m_savedFrame.LeftTop());
        w->ResizeTo(m_savedFrame.Width(), m_savedFrame.Height());
    }

    HaikuDumpWindow("TLW Maximize after", w);
    w->UnlockLooper();

    HaikuLayoutRootView();
}

void wxTopLevelWindowHaiku::Restore()
{
    BWindow* w = GetHaikuWindow();
    if ( !w )
        return;

    if ( w->IsMinimized() )
        w->Minimize(false);

    if ( m_isFullScreen )
        ShowFullScreen(false);

    if ( IsMaximized() )
        Maximize(false);
}

void wxTopLevelWindowHaiku::Iconize(bool iconize)
{
    if ( BWindow* w = GetHaikuWindow() )
        w->Minimize(iconize);
}

bool wxTopLevelWindowHaiku::IsMaximized() const
{
    BWindow* w = GetHaikuWindow();
    if ( !w )
        return false;

    bool maximized = false;

    if ( w->LockLooper() )
    {
        BScreen screen(w);
        if ( screen.IsValid() )
        {
            const BRect screenFrame = screen.Frame();
            const BRect winFrame = w->Frame();

            maximized =
                (winFrame.Width()  >= screenFrame.Width()  - 1.0f &&
                 winFrame.Height() >= screenFrame.Height() - 1.0f);
        }

        w->UnlockLooper();
    }

    return maximized;
}

bool wxTopLevelWindowHaiku::IsIconized() const
{
    if ( BWindow* w = GetHaikuWindow() )
        return w->IsMinimized();

    return false;
}

bool wxTopLevelWindowHaiku::ShowFullScreen(bool show, long WXUNUSED(style))
{
    BWindow* w = GetHaikuWindow();
    if ( !w )
        return false;

    if ( !w->LockLooper() )
        return false;

    HaikuDumpWindow("TLW ShowFullScreen before", w);

    if ( show && !m_isFullScreen )
    {
        m_savedFrame = w->Frame();
        m_savedLook = w->Look();

        w->SetLook(B_NO_BORDER_WINDOW_LOOK);

        BScreen screen(w);
        if ( screen.IsValid() )
        {
            const BRect r = screen.Frame();
            w->MoveTo(r.LeftTop());
            w->ResizeTo(r.Width(), r.Height());
        }

        m_isFullScreen = true;
    }
    else if ( !show && m_isFullScreen )
    {
        w->SetLook(m_savedLook);
        w->MoveTo(m_savedFrame.LeftTop());
        w->ResizeTo(m_savedFrame.Width(), m_savedFrame.Height());

        m_isFullScreen = false;
    }

    HaikuDumpWindow("TLW ShowFullScreen after", w);
    w->UnlockLooper();

    return true;
}

bool wxTopLevelWindowHaiku::IsFullScreen() const
{
    return m_isFullScreen;
}

void wxTopLevelWindowHaiku::SetTitle(const wxString& title)
{
    if ( BWindow* w = GetHaikuWindow() )
    {
        if ( w->LockLooper() )
        {
            w->SetTitle(title.utf8_str());
            w->UnlockLooper();
        }

        HaikuDumpWindow("TLW after SetTitle", w);
    }
}

wxString wxTopLevelWindowHaiku::GetTitle() const
{
    wxString title;

    if ( BWindow* w = GetHaikuWindow() )
    {
        if ( w->LockLooper() )
        {
            const char* t = w->Title();
            if ( t )
                title = wxString::FromUTF8(t);

            w->UnlockLooper();
        }
    }

    return title;
}

void wxTopLevelWindowHaiku::SetIcons(const wxIconBundle& icons)
{
    // Haiku has one application icon, read from executable resources at launch.
    // Keep the wx window icon bundle without modifying the installed executable.
    wxTopLevelWindowBase::SetIcons(icons);
}

void wxTopLevelWindowHaiku::SetWindowStyleFlag(long style)
{
    wxWindow::SetWindowStyleFlag(style);

    BWindow* w = GetHaikuWindow();
    if ( !w )
        return;

    if ( !w->LockLooper() )
        return;

    if ( !m_isFullScreen )
    {
        w->SetLook(HasFlag(wxNO_BORDER)
                   ? B_NO_BORDER_WINDOW_LOOK
                   : B_TITLED_WINDOW_LOOK);
    }

    w->SetFeel(HasFlag(wxSTAY_ON_TOP)
               ? B_FLOATING_APP_WINDOW_FEEL
               : B_NORMAL_WINDOW_FEEL);

    uint32 flags = w->Flags();

    if ( HasFlag(wxRESIZE_BORDER) )
        flags &= ~B_NOT_RESIZABLE;
    else
        flags |= B_NOT_RESIZABLE;

    if ( HasFlag(wxCLOSE_BOX) || HasFlag(wxSYSTEM_MENU) )
        flags &= ~B_NOT_CLOSABLE;
    else
        flags |= B_NOT_CLOSABLE;

    if ( HasFlag(wxMINIMIZE_BOX) )
        flags &= ~B_NOT_MINIMIZABLE;
    else
        flags |= B_NOT_MINIMIZABLE;

    if ( HasFlag(wxMAXIMIZE_BOX) )
        flags &= ~B_NOT_ZOOMABLE;
    else
        flags |= B_NOT_ZOOMABLE;

    w->SetFlags(flags);
    w->UnlockLooper();

    HaikuDumpWindow("TLW after SetWindowStyleFlag", w);
}

long wxTopLevelWindowHaiku::GetWindowStyleFlag() const
{
    return wxWindow::GetWindowStyleFlag();
}

void wxTopLevelWindowHaiku::HaikuSetSizeIncrement(int WXUNUSED(width),
                                                  int WXUNUSED(height))
{
}

void wxTopLevelWindowHaiku::DoSetSizeHints(int minW, int minH,
                                           int maxW, int maxH,
                                           int incW, int incH)
{
    BWindow* w = GetHaikuWindow();
    if ( !w )
        return;

    // Haiku uses coordinate extents (pixel count minus one). Its native
    // unlimited value remains representable in app_server integer sizes;
    // FLT_MAX overflows there and can collapse a fitted dialog to one pixel.
    const float fMinW = (minW > 0)               ? (float)(minW - 1) : 0.0f;
    const float fMinH = (minH > 0)               ? (float)(minH - 1) : 0.0f;
    const float fMaxW = (maxW == wxDefaultCoord) ? B_SIZE_UNLIMITED : (float)(maxW > 0 ? maxW - 1 : 0);
    const float fMaxH = (maxH == wxDefaultCoord) ? B_SIZE_UNLIMITED : (float)(maxH > 0 ? maxH - 1 : 0);

    if ( w->LockLooper() )
    {
        w->SetSizeLimits(fMinW, fMaxW, fMinH, fMaxH);

        if ( maxW != wxDefaultCoord || maxH != wxDefaultCoord )
        {
            w->SetZoomLimits(
                (maxW == wxDefaultCoord) ? w->Bounds().Width()  : fMaxW,
                (maxH == wxDefaultCoord) ? w->Bounds().Height() : fMaxH);
        }

        w->UnlockLooper();
    }

    HaikuSetSizeIncrement(incW, incH);
    wxTopLevelWindowBase::DoSetSizeHints(minW, minH, maxW, maxH, incW, incH);
}

void wxTopLevelWindowHaiku::HaikuLayoutRootView()
{
    if ( !m_haikuWindow || !m_rootView )
        return;

    HaikuDumpWindow("TLW layout native before", m_haikuWindow);
    HaikuDumpView("TLW layout root before", m_rootView);

    if ( m_haikuWindow->LockLooper() )
    {
        m_rootView->ResizeTo(m_haikuWindow->Bounds().Width(),
                             m_haikuWindow->Bounds().Height());
        m_haikuWindow->UnlockLooper();
    }

    HaikuDumpView("TLW layout root after", m_rootView);
}
