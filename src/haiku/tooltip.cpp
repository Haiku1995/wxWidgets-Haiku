/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/tooltip.cpp
// Purpose:     wxToolTip implementation for Haiku
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#ifndef WX_PRECOMP
    #include "wx/window.h"
#endif

#include "wx/tooltip.h"
#include "wx/haiku/private.h"
#include "wx/haiku/private/utils.h"
#include <interface/View.h> // BView

// ---------------------------------------------------------------------------
// Static API (not implemented, same as Qt)
// ---------------------------------------------------------------------------

/* static */ void wxToolTip::Enable(bool WXUNUSED(flag))
{
    wxMISSING_FUNCTION();
}

/* static */ void wxToolTip::SetDelay(long WXUNUSED(milliseconds))
{
    wxMISSING_FUNCTION();
}

/* static */ void wxToolTip::SetAutoPop(long WXUNUSED(milliseconds))
{
    wxMISSING_FUNCTION();
}

/* static */ void wxToolTip::SetReshow(long WXUNUSED(milliseconds))
{
    wxMISSING_FUNCTION();
}

// ---------------------------------------------------------------------------
// wxToolTip
// ---------------------------------------------------------------------------

wxToolTip::wxToolTip(const wxString& tip)
{
    m_window = nullptr;
    SetTip(tip);
}

void wxToolTip::SetTip(const wxString& tip)
{
    m_text = tip;

    if (m_window)
    {
        BView* view = reinterpret_cast<BView*>(m_window->GetHandle());
        if (view)
            view->SetToolTip(wxToUTF8(m_text));
    }
}

const wxString& wxToolTip::GetTip() const
{
    return m_text;
}

void wxToolTip::SetWindow(wxWindow* win)
{
    wxCHECK_RET(win != nullptr, "window should not be null");

    m_window = win;

    BView* view = reinterpret_cast<BView*>(m_window->GetHandle());
    if (view)
        view->SetToolTip(wxToUTF8(m_text));
}
