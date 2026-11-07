/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/dcclient.cpp
// Purpose:     wxWindowDCImpl, wxClientDCImpl, wxPaintDCImpl for Haiku
// Author:      Based on Qt; adapted for Haiku
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#include <InterfaceKit.h>
#include <Region.h>

#ifndef WX_PRECOMP
    #include "wx/log.h"
    #include "wx/window.h"
#endif

#include "wx/dcclient.h"
#include "wx/haiku/dcclient.h"
#include "wx/window.h"

// Avoid wx vararg logging here; use raw stdout only for Haiku debug traces.
#if defined(WX_DEBUG_HAIKU) && WX_DEBUG_HAIKU
    #define HAIKU_LOG_RAW(msg, ...) \
        do { printf("[HAIKU-DC:%s:%d] " msg "\n", __func__, __LINE__, ##__VA_ARGS__); fflush(stdout); } while (0)
#else
    #define HAIKU_LOG_RAW(msg, ...) ((void)0)
#endif

// ----------------------------------------------------------------------------
// wxWindowDCImpl
// ----------------------------------------------------------------------------

wxIMPLEMENT_CLASS(wxWindowDCImpl, wxHaikuDCImpl);

wxWindowDCImpl::wxWindowDCImpl(wxDC* owner)
    : wxHaikuDCImpl(owner)
{
    m_ok = false;
}

wxWindowDCImpl::wxWindowDCImpl(wxDC* owner, wxWindow* win)
    : wxHaikuDCImpl(owner),
      m_window(win)
{
    HAIKU_LOG_RAW("ENTRY: wxWindowDCImpl(owner, win) win=%p", win);

    m_view = nullptr;
    m_ok = false;

    if ( !win )
        return;

    // On Haiku, DCs should target the actual draw view, not just the generic
    // handle, because windows may have wrapper/native/scroll views.
    if ( wxWindowHaiku* hwin = wxDynamicCast(win, wxWindowHaiku) )
        m_view = hwin->HaikuGetDrawView();
    else
        m_view = static_cast<BView*>(win->GetHandle());

    m_ok = (m_view != nullptr);
    if ( !m_ok )
        return;

    if ( BWindow* bwin = m_view->Window() )
    {
        if ( bwin->LockLooper() )
        {
            BFont bfont;
            m_view->GetFont(&bfont);
            m_bfont = bfont;
            bwin->UnlockLooper();
        }
    }

    // Do not call ComputeScaleAndOrigin() here.
    // Derived classes set up logical origin first, then compute transform once.
}

wxWindowDCImpl::~wxWindowDCImpl()
{
    DestroyClippingRegion();
    // Window DC transforms must not persist on the native view. In particular,
    // a measuring DC used by a scrolled control can leave a scroll translation
    // that Haiku inherits when it starts the next native Draw callback.
    if (m_view)
    {
        BWindow* window = m_view->Window();
        if (!window || window->LockLooper())
        {
            m_view->SetTransform(BAffineTransform());
            if (window) window->UnlockLooper();
        }
    }
    // BView is owned by the window — do not delete.
    // Avoid flushing here: the DC destructor can run in sensitive repaint paths,
    // and flushing is not required for correctness.
}

void wxWindowDCImpl::DoGetSize(int* width, int* height) const
{
    wxCHECK_RET(m_window, "wxWindowDCImpl without a window");
    m_window->GetSize(width, height);
}

// ----------------------------------------------------------------------------
// wxClientDCImpl
// ----------------------------------------------------------------------------

wxIMPLEMENT_CLASS(wxClientDCImpl, wxWindowDCImpl);

wxClientDCImpl::wxClientDCImpl(wxDC* owner)
    : wxWindowDCImpl(owner)
{
}

wxClientDCImpl::wxClientDCImpl(wxDC* owner, wxWindow* win)
    : wxWindowDCImpl(owner, win)
{
    HAIKU_LOG_RAW("ENTRY: wxClientDCImpl(owner, win) win=%p", win);

    if ( !m_ok || !win )
        return;

    const wxPoint wxOrigin = win->GetClientAreaOrigin();
    if ( wxOrigin.x != 0 || wxOrigin.y != 0 )
        SetLogicalOrigin(-wxOrigin.x, -wxOrigin.y);

    ComputeScaleAndOrigin();
}

void wxClientDCImpl::DoGetSize(int* width, int* height) const
{
    wxCHECK_RET(m_window, "wxClientDCImpl without a window");
    m_window->GetClientSize(width, height);
}

// ----------------------------------------------------------------------------
// wxPaintDCImpl
// ----------------------------------------------------------------------------

wxIMPLEMENT_CLASS(wxPaintDCImpl, wxWindowDCImpl);

wxPaintDCImpl::wxPaintDCImpl(wxDC* owner)
    : wxWindowDCImpl(owner)
{
}

wxPaintDCImpl::wxPaintDCImpl(wxDC* owner, wxWindow* win)
    : wxWindowDCImpl(owner, win)
{
    wxCHECK_RET(m_view, "null view in wxPaintDC");

    if ( !win )
        return;

    const wxPoint wxOrigin = win->GetClientAreaOrigin();
    if ( wxOrigin.x != 0 || wxOrigin.y != 0 )
        SetLogicalOrigin(-wxOrigin.x, -wxOrigin.y);

    ComputeScaleAndOrigin();
}
