/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/checkbox.cpp
// Purpose:     wxCheckBox implementation for Haiku
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"
#include "control_lock.h"

#if wxUSE_CHECKBOX

#ifndef WX_PRECOMP
    #include "wx/checkbox.h"
#endif

#include "wx/checkbox.h"
#include "wx/haiku/private.h"
#include "wx/log.h"

#include <CheckBox.h>
#include <Autolock.h>
#include <Control.h>
#include <Message.h>
#include <InterfaceDefs.h>
#include <Window.h>

//wxIMPLEMENT_DYNAMIC_CLASS(wxCheckBox, wxCheckBoxBase);

static BRect wxToBRect(const wxPoint& pos, const wxSize& size)
{
    const wxPoint p = (pos == wxDefaultPosition) ? wxPoint(0, 0) : pos;

    if (size == wxDefaultSize)
    {
        return BRect((float)p.x, (float)p.y,
                     (float)(p.x + 100 - 1),
                     (float)(p.y + 20 - 1));
    }

    return BRect((float)p.x, (float)p.y,
                 (float)(p.x + size.x - 1),
                 (float)(p.y + size.y - 1));
}

class wxHaikuCheckBox : public BCheckBox
{
public:
    wxHaikuCheckBox(BRect frame, wxCheckBox* handler, const char* label)
        : BCheckBox(frame, "checkbox", label, new BMessage('CHKD')),
          m_handler(handler)
    {
        SetTarget(this);
        SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
    }

    void AttachedToWindow() override
    {
        BCheckBox::AttachedToWindow();
        SetTarget(this);

        // Preserve a programmatically selected partial state on reattachment.
        UpdateMnemonic();
    }

    void UpdateMnemonic()
    {
        if (!Window()) return;
        if (m_mnemonic) Window()->RemoveShortcut(m_mnemonic, B_OPTION_KEY | B_NO_COMMAND_KEY);
        m_mnemonic = 0;
        wxString text;
        const int index = wxControl::FindAccelIndex(m_handler->GetLabel(), &text);
        if (index < 0 || static_cast<size_t>(index) >= text.length()) return;
        uint32 key = text[index].GetValue();
        if (key >= 'A' && key <= 'Z') key += 'a'-'A';
        if (key > 127 || Window()->HasShortcut(key, B_OPTION_KEY | B_NO_COMMAND_KEY)) return;
        Window()->AddShortcut(key, B_OPTION_KEY | B_NO_COMMAND_KEY, new BMessage('wxMn'), this);
        m_mnemonic = key;
    }
    void DetachedFromWindow() override
    {
        if (Window() && m_mnemonic) Window()->RemoveShortcut(m_mnemonic, B_OPTION_KEY | B_NO_COMMAND_KEY);
        m_mnemonic = 0;
        BCheckBox::DetachedFromWindow();
    }
    void SetValue(int32 value) override
    {
        if (m_userAction && m_handler && m_handler->Is3State() && m_handler->HasFlag(wxCHK_ALLOW_3RD_STATE_FOR_USER))
        {
            if (Value() == B_CONTROL_ON) value = B_CONTROL_PARTIALLY_ON;
            else if (Value() == B_CONTROL_PARTIALLY_ON) value = B_CONTROL_OFF;
        }
        BCheckBox::SetValue(value);
    }
    void KeyDown(const char* bytes, int32 count) override
    {
        m_userAction = true;
        BCheckBox::KeyDown(bytes, count);
        m_userAction = false;
    }
    void MouseDown(BPoint point) override
    {
        m_userAction = true;
        BCheckBox::MouseDown(point);
        m_userAction = false;
    }
    void MouseUp(BPoint point) override
    {
        m_userAction = true;
        BCheckBox::MouseUp(point);
        m_userAction = false;
    }

    void MessageReceived(BMessage* msg) override
    {
        if (msg && msg->what == 'wxMn' && m_handler)
        {
            if (m_handler->IsEnabled()) KeyDown(" ", 1);
            return;
        }
        if (msg && msg->what == 'CHKD' && m_handler)
        {
            wxCommandEvent event(wxEVT_CHECKBOX, m_handler->GetId());
            event.SetInt(Value() == B_CONTROL_PARTIALLY_ON ? wxCHK_UNDETERMINED :
                         Value() == B_CONTROL_ON ? wxCHK_CHECKED : wxCHK_UNCHECKED);
            event.SetEventObject(m_handler);
            m_handler->GetEventHandler()->ProcessEvent(event);
            return;
        }

        BCheckBox::MessageReceived(msg);
    }

private:
    wxCheckBox* m_handler;
    bool m_userAction = false;
    uint32 m_mnemonic = 0;
};

wxCheckBox::wxCheckBox(wxWindow* parent,
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

bool wxCheckBox::Create(wxWindow* parent,
                        wxWindowID id,
                        const wxString& label,
                        const wxPoint& pos,
                        const wxSize& size,
                        long style,
                        const wxValidator& validator,
                        const wxString& name)
{
    WXValidateStyle(&style);

    m_labelOrig = label;
    m_haikuCheckBox = new wxHaikuCheckBox(
        wxToBRect(pos, size),
        this,
        wxControl::GetLabelText(label).utf8_str());

    HaikuAdoptView(m_haikuCheckBox, nullptr, m_haikuCheckBox);

    if (!wxCheckBoxBase::Create(parent, id, pos, size, style, validator, name))
        return false;

    return true;
}

void wxCheckBox::SetValue(bool value)
{
    if (!m_haikuCheckBox) return;
    BAutolock lock(m_haikuCheckBox->Looper());
    if (!m_haikuCheckBox->Looper() || lock.IsLocked())
        m_haikuCheckBox->SetValue(value ? B_CONTROL_ON : B_CONTROL_OFF);
}

bool wxCheckBox::GetValue() const
{
    wxHaikuControlLock lock(m_haikuCheckBox);
    if (!lock.IsOk())
        return false;

    return m_haikuCheckBox && m_haikuCheckBox->Value() != B_CONTROL_OFF;
}

void wxCheckBox::SetLabel(const wxString& label)
{
    wxCheckBoxBase::SetLabel(label);

    if (m_haikuCheckBox)
    {
        BLooper* looper = m_haikuCheckBox->Looper();
        if (!looper || looper->Lock())
        {
            m_haikuCheckBox->SetLabel(wxControl::GetLabelText(label).utf8_str());
            static_cast<wxHaikuCheckBox*>(m_haikuCheckBox)->UpdateMnemonic();
            if (looper) looper->Unlock();
        }
    }
}

wxString wxCheckBox::GetLabel() const
{
    return m_labelOrig;
}

void wxCheckBox::DoSet3StateValue(wxCheckBoxState state)
{
    int32 value = B_CONTROL_OFF;

    switch (state)
    {
        case wxCHK_UNCHECKED:
            value = B_CONTROL_OFF;
            break;

        case wxCHK_CHECKED:
            value = B_CONTROL_ON;
            break;

        case wxCHK_UNDETERMINED:
            value = B_CONTROL_PARTIALLY_ON;
            break;
    }

    if (!m_haikuCheckBox) return;
    BAutolock lock(m_haikuCheckBox->Looper());
    if (!m_haikuCheckBox->Looper() || lock.IsLocked())
        m_haikuCheckBox->SetValue(value);
}

wxCheckBoxState wxCheckBox::DoGet3StateValue() const
{
    wxHaikuControlLock lock(m_haikuCheckBox);
    if (!lock.IsOk())
        return wxCHK_UNCHECKED;

    if (!m_haikuCheckBox)
        return wxCHK_UNCHECKED;

    switch (m_haikuCheckBox->Value())
    {
        case B_CONTROL_OFF:
            return wxCHK_UNCHECKED;

        case B_CONTROL_ON:
            return wxCHK_CHECKED;

        case B_CONTROL_PARTIALLY_ON:
            return wxCHK_UNDETERMINED;

        default:
            return wxCHK_UNCHECKED;
    }
}

#endif // wxUSE_CHECKBOX
