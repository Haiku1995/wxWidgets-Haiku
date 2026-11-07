///////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/datectrl.cpp
// Purpose:     wxDatePickerCtrl implementation for Haiku
// Author:      Based on Qt; adapted for Haiku
// Created:     2025-04-05
// Copyright:   (c) 2025 wxWidgets team
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"
#include "control_lock.h"

#if wxUSE_DATEPICKCTRL

#include "wx/datectrl.h"
#include "wx/dateevt.h"
#include "wx/log.h"

// Haiku headers
#include <TextControl.h>
#include <private/shared/CalendarView.h>
#include <Autolock.h>
#include <Message.h>
#include <interface/InterfaceDefs.h>
#include <app/Application.h>

// ----------------------------------------------------------------------------
// Private handler to receive BMessages from Haiku controls
// ----------------------------------------------------------------------------

class wxHaikuDatePickerHandler : public BHandler
{
public:
    wxHaikuDatePickerHandler(wxDatePickerCtrl* ctrl)
        : BHandler("wxHaikuDatePickerHandler"), m_ctrl(ctrl)
    {
    }

    virtual void MessageReceived(BMessage* message) override
    {
        if (message->what == 'DATE')
        {
            if (m_ctrl)
                m_ctrl->OnTextChanged();
        }
        else
        {
            BHandler::MessageReceived(message);
        }
    }

private:
    wxDatePickerCtrl* m_ctrl;
};

// ----------------------------------------------------------------------------
// wxDatePickerCtrl implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(wxDatePickerCtrl, wxControl);

static BRect wxToBRect(const wxPoint& pos, const wxSize& size)
{
    if (size == wxDefaultSize)
    {
        // Use a reasonable default size
        return BRect(pos.x, pos.y, pos.x + 80 - 1, pos.y + 25 - 1);
    }
    return BRect(pos.x, pos.y,
                 pos.x + size.GetWidth() - 1,
                 pos.y + size.GetHeight() - 1);
}



wxDatePickerCtrl::~wxDatePickerCtrl()
{
    // Remove handler from looper if still attached
    if (m_handler)
    {
        if (BLooper* looper = m_handler->Looper())
        {
            BAutolock lock(looper);
            if (!lock.IsLocked())
                return;
            looper->RemoveHandler(m_handler);
        }
        delete m_handler;
        m_handler = nullptr;
    }
    // The adopted native view is disposed by wxWindowHaiku.
}

bool wxDatePickerCtrl::Create(wxWindow* parent,
                              wxWindowID id,
                              const wxDateTime& dt,
                              const wxPoint& pos,
                              const wxSize& size,
                              long style,
                              const wxValidator& validator,
                              const wxString& name)
{
    // 1. Initialize range and base class immediately
    m_min = wxDefaultDateTime;
    m_max = wxDefaultDateTime;

    wxCHECK_MSG(parent, false, "Date picker requires a parent");

    // 2. Efficiently create the Haiku BTextControl
    m_dateTextControl = new BTextControl(
        wxToBRect(pos, size),
        name.utf8_str(),
        nullptr,             // Use nullptr instead of "" for label
        "", 
        new BMessage('DATE'),
        B_FOLLOW_LEFT | B_FOLLOW_TOP,
        B_WILL_DRAW | B_NAVIGABLE
    );

    if (!m_dateTextControl)
        return false;

    m_dateTextControl->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

    HaikuAdoptView(m_dateTextControl);
    if (!wxDatePickerCtrlBase::Create(parent, id, pos, size, style, validator, name))
        return false;

    m_handler = new wxHaikuDatePickerHandler(this);
    {
        wxHaikuControlLock lock(m_dateTextControl);
        if (!lock.IsOk())
            return false;
        if (BLooper* looper = m_dateTextControl->Looper())
        {
            looper->AddHandler(m_handler);
            m_dateTextControl->SetTarget(m_handler);
        }
    }

    // 4. Set initial value after control is fully parented
    SetValue(dt.IsValid() || HasFlag(wxDP_ALLOWNONE) ? dt : wxDateTime::Today());

    return true;
}


void wxDatePickerCtrl::SetValue(const wxDateTime& dt)
{
    wxHaikuControlLock lock(m_dateTextControl);
    if (!lock.IsOk())
        return;

    if (!m_dateTextControl)
        return;

    wxDateTime safeDt = dt;
    if (!safeDt.IsValid())
    {
        if (HasFlag(wxDP_ALLOWNONE))
        {
            m_dateTextControl->SetText("");
            return;
        }
        else
        {
            wxFAIL_MSG("A valid date is required without wxDP_ALLOWNONE");
            return;
        }
    }

    // Programmatic values outside the range leave the current value unchanged.
    if ((m_min.IsValid() && safeDt < m_min) ||
        (m_max.IsValid() && safeDt > m_max))
        return;

    // Format as ISO date (YYYY-MM-DD)
    m_dateTextControl->SetText(safeDt.FormatISODate().utf8_str());
}

wxDateTime wxDatePickerCtrl::GetValue() const
{
    wxHaikuControlLock lock(m_dateTextControl);
    if (!lock.IsOk())
        return wxDateTime();

    if (!m_dateTextControl)
        return wxDefaultDateTime;

    const char* text = m_dateTextControl->Text();
    if (!text || strlen(text) == 0)
    {
        // Empty field: only valid if wxDP_ALLOWNONE is set
        return wxDefaultDateTime;
    }

    wxString str = wxString::FromUTF8(text);
    wxDateTime dt;
    if (dt.ParseISODate(str) && dt.IsValid())
        return dt;

    // Parsing failed â€” return today or default
    return wxDefaultDateTime;
}

void wxDatePickerCtrl::SetRange(const wxDateTime& dt1, const wxDateTime& dt2)
{
    wxCHECK_RET(!dt1.IsValid() || !dt2.IsValid() || dt1 <= dt2,
                "Invalid date range");
    wxHaikuControlLock lock(m_dateTextControl);
    if (!lock.IsOk())
        return;
    m_min = dt1;
    m_max = dt2;

    const wxDateTime current = GetValue();
    if (current.IsValid())
    {
        if (m_min.IsValid() && current < m_min)
            SetValue(m_min);
        else if (m_max.IsValid() && current > m_max)
            SetValue(m_max);
    }
}

bool wxDatePickerCtrl::GetRange(wxDateTime* dt1, wxDateTime* dt2) const
{
    wxHaikuControlLock lock(m_dateTextControl);
    if (!lock.IsOk())
        return false;
    if (dt1) *dt1 = m_min;
    if (dt2) *dt2 = m_max;
    return m_min.IsValid() || m_max.IsValid();
}

void wxDatePickerCtrl::OnTextChanged()
{
    wxDateTime dt = GetValue();
    wxDateEvent event(this, dt, wxEVT_DATE_CHANGED);
    ProcessWindowEvent(event);
}

BTextControl* wxDatePickerCtrl::GetBTextControl() const
{
    return m_dateTextControl;
}

#endif // wxUSE_DATEPICKCTRL
