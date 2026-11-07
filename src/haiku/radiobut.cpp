/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/radiobut.cpp
// Purpose:     wxRadioButton implementation for Haiku using BRadioButton
// Author:      Adapted for Haiku
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"
#include "control_lock.h"

#ifndef WX_PRECOMP
    #include "wx/radiobut.h"
#endif

#include "wx/radiobut.h"
#include "wx/haiku/private.h"
#include "wx/stockitem.h"

#include <RadioButton.h>
#include <Message.h>
#include <View.h>

//wxIMPLEMENT_DYNAMIC_CLASS(wxRadioButton, wxRadioButtonBase);

static BRect wxToBRect(const wxPoint& pos, const wxSize& size)
{
    const wxPoint p = (pos == wxDefaultPosition) ? wxPoint(0, 0) : pos;

    if (size == wxDefaultSize)
    {
        return BRect((float)p.x, (float)p.y,
                     (float)(p.x + 80 - 1),
                     (float)(p.y + 25 - 1));
    }

    return BRect((float)p.x, (float)p.y,
                 (float)(p.x + size.x - 1),
                 (float)(p.y + size.y - 1));
}

class wxHaikuRadioButton : public BRadioButton
{
public:
    wxHaikuRadioButton(BRect frame,
                       const char* name,
                       const char* label,
                       wxRadioButton* wxRadio)
        : BRadioButton(frame, name, label, new BMessage('wRad')),
          m_wxRadio(wxRadio)
    {
        SetTarget(this);
    }

    void AttachedToWindow() override
    {
        BRadioButton::AttachedToWindow();
        SetTarget(this);
    }

    void SetValue(int32 value) override
    {
        // Native sibling grouping ignores wxRB_GROUP and wxRB_SINGLE.
        m_wxRadio->SetValue(value != B_CONTROL_OFF);
    }

    void MouseUp(BPoint where) override
    {
        const bool notify = IsEnabled() && IsTracking() &&
                            Bounds().Contains(where) && Value() == B_CONTROL_ON;
        BRadioButton::MouseUp(where);
        // The native asynchronous path invokes only when the value changes.
        // wxWidgets also reports a click on an already selected button.
        if (notify)
            Invoke();
    }

    void MessageReceived(BMessage* msg) override
    {
        if (msg && msg->what == 'wRad' && m_wxRadio)
        {
            wxCommandEvent event(wxEVT_RADIOBUTTON, m_wxRadio->GetId());
            event.SetInt(Value() == B_CONTROL_ON ? 1 : 0);
            event.SetEventObject(m_wxRadio);
            m_wxRadio->GetEventHandler()->ProcessEvent(event);
            return;
        }

        BRadioButton::MessageReceived(msg);
    }

private:
    wxRadioButton* m_wxRadio;
};

wxRadioButton::wxRadioButton(wxWindow* parent,
                             wxWindowID id,
                             const wxString& label,
                             const wxPoint& pos,
                             const wxSize& size,
                             long style,
                             const wxValidator& validator,
                             const wxString& name)
{
    Create(parent, id, label, pos, size, style, validator, name);
}

bool wxRadioButton::Create(wxWindow* parent,
                           wxWindowID id,
                           const wxString& label,
                           const wxPoint& pos,
                           const wxSize& size,
                           long style,
                           const wxValidator& validator,
                           const wxString& name)
{
    wxString effectiveLabel = label;
    if (effectiveLabel.IsEmpty() && wxIsStockID(id))
        effectiveLabel = wxGetStockLabel(id);

    m_bRadioButton = new wxHaikuRadioButton(
        wxToBRect(pos, size),
        name.utf8_str(),
        effectiveLabel.utf8_str(),
        this);

    // Same connection pattern as wxButton / wxStaticText:
    // adopt native view first, then let wx base create finish setup.
    HaikuAdoptView(m_bRadioButton, nullptr, m_bRadioButton);

    if (!wxRadioButtonBase::Create(parent, id, pos, size, style, validator, name))
        return false;

    if (!(style & wxRB_SINGLE) && !GetPreviousInGroup())
        SetValue(true);

    return true;
}

void wxRadioButton::SetValue(bool value)
{
    wxHaikuControlLock lock(m_bRadioButton);
    if (!lock.IsOk())
        return;

    if (!m_bRadioButton)
        return;

    if (value && !HasFlag(wxRB_SINGLE))
    {
        for (wxRadioButton* radio = GetFirstInGroup(); radio;
             radio = radio->GetNextInGroup())
        {
            if (radio != this)
                radio->GetBRadioButton()->BControl::SetValue(B_CONTROL_OFF);
        }
    }
    // Bypass BRadioButton's grouping; the wx group was updated above.
    m_bRadioButton->BControl::SetValue(value ? B_CONTROL_ON : B_CONTROL_OFF);
}

bool wxRadioButton::GetValue() const
{
    wxHaikuControlLock lock(m_bRadioButton);
    if (!lock.IsOk())
        return false;

    return m_bRadioButton && m_bRadioButton->Value() == B_CONTROL_ON;
}

void wxRadioButton::SetLabel(const wxString& label)
{
    wxHaikuControlLock lock(m_bRadioButton);
    if (!lock.IsOk())
        return;

    wxRadioButtonBase::SetLabel(label);

    if (m_bRadioButton)
        m_bRadioButton->SetLabel(label.utf8_str());
}

wxString wxRadioButton::GetLabel() const
{
    wxHaikuControlLock lock(m_bRadioButton);
    if (!lock.IsOk())
        return wxString();

    if (m_bRadioButton)
        return wxString::FromUTF8(m_bRadioButton->Label());

    return wxEmptyString;
}

wxSize wxRadioButton::DoGetBestSize() const
{
    wxHaikuControlLock lock(m_bRadioButton);
    if (!lock.IsOk())
        return wxSize(80, 25);

    if (m_bRadioButton)
    {
        BSize s = m_bRadioButton->PreferredSize();
        return wxSize((int)(s.width + 1), (int)(s.height + 1));
    }

    return wxSize(80, 25);
}
