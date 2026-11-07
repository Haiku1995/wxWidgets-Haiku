/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/gauge.cpp
// Purpose:     wxGauge implementation using BStatusBar
// Author:      Adapted for Haiku
// Licence:     wxWindows Licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#if wxUSE_GAUGE

#ifndef WX_PRECOMP
    #include "wx/gauge.h"
#endif

#include "wx/gauge.h"
#include "wx/haiku/private.h"
#include "wx/log.h"

#include <interface/StatusBar.h>
#include <algorithm>

//wxIMPLEMENT_DYNAMIC_CLASS(wxGauge, wxGaugeBase);

static BRect wxToBRect(const wxPoint& pos, const wxSize& size)
{
    const wxPoint p = (pos == wxDefaultPosition) ? wxPoint(0, 0) : pos;

    if (size == wxDefaultSize)
    {
        return BRect((float)p.x, (float)p.y,
                     (float)(p.x + 150 - 1),
                     (float)(p.y + 20 - 1));
    }

    return BRect((float)p.x, (float)p.y,
                 (float)(p.x + size.x - 1),
                 (float)(p.y + size.y - 1));
}

wxGauge::wxGauge()
    : m_statusBar(nullptr),
      m_range(0),
      m_value(0)
{
}

wxGauge::wxGauge(wxWindow* parent, wxWindowID id, int range,
                 const wxPoint& pos, const wxSize& size,
                 long style, const wxValidator& validator, const wxString& name)
    : m_statusBar(nullptr),
      m_range(0),
      m_value(0)
{
    Create(parent, id, range, pos, size, style, validator, name);
}

wxGauge::~wxGauge()
{
    // Native lifetime is managed through the adopted wx handle path.
}

bool wxGauge::Create(wxWindow* parent, wxWindowID id, int range,
                     const wxPoint& pos, const wxSize& size,
                     long style, const wxValidator& validator, const wxString& name)
{
    m_statusBar = new BStatusBar(wxToBRect(pos, size), name.utf8_str());

    if (!m_statusBar)
        return false;

    m_statusBar->SetMaxValue((float)range);
    m_statusBar->SetTo(0.0f);

    if (!(style & wxGA_TEXT))
    {
        m_statusBar->SetText("");
        m_statusBar->SetTrailingText("");
    }

    // Same connection pattern as button/stattext/slider:
    // create native view first, adopt it, then let wx finish setup.
    HaikuAdoptView(m_statusBar, nullptr, m_statusBar);

    if (!wxGaugeBase::Create(parent, id, range, pos, size, style, validator, name))
        return false;

    m_range = range;
    m_value = 0;

    return true;
}

void wxGauge::SetValue(int pos)
{
    if (!m_statusBar)
        return;

    pos = std::clamp(pos, 0, m_range);
    m_value = pos;

    BWindow* win = m_statusBar->Window();
    if (!win)
    {
        m_statusBar->SetTo((float)pos);
        return;
    }

    const bool alreadyLocked = win->IsLocked();
    if (!alreadyLocked && !win->LockLooper())
        return;

    m_statusBar->SetTo((float)pos);

    if (!alreadyLocked)
        win->UnlockLooper();
}

int wxGauge::GetValue() const
{
    return m_value;
}

void wxGauge::SetRange(int range)
{
    if (!m_statusBar)
        return;

    if (range < 0)
        range = 0;

    m_range = range;

    if (m_value > m_range)
        m_value = m_range;

    BWindow* win = m_statusBar->Window();
    if (!win)
    {
        m_statusBar->SetMaxValue((float)range);
        m_statusBar->SetTo((float)m_value);
        return;
    }

    const bool alreadyLocked = win->IsLocked();
    if (!alreadyLocked && !win->LockLooper())
        return;

    m_statusBar->SetMaxValue((float)range);
    m_statusBar->SetTo((float)m_value);

    if (!alreadyLocked)
        win->UnlockLooper();
}

int wxGauge::GetRange() const
{
    return m_range;
}

BStatusBar* wxGauge::GetStatusBar() const
{
    return m_statusBar;
}

#endif // wxUSE_GAUGE
