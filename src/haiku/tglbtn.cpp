// ============================================================================
// Name:        src/haiku/tglbtn.cpp
// Purpose:     wxToggleButton implementation for Haiku
// Author:      wxWidgets team
// Copyright:   (c) 2025 wxWidgets team
// Licence:     wxWindows licence
// ============================================================================

#include "wx/wxprec.h"
#include "control_lock.h"

#if wxUSE_TOGGLEBTN

#include "wx/tglbtn.h"
#include "wx/haiku/private.h"
#include "wx/log.h"

#include <Button.h>
#include <Message.h>
#include <Bitmap.h>

// ----------------------------------------------------------------------------
// Event type
// ----------------------------------------------------------------------------

wxDEFINE_EVENT(wxEVT_TOGGLEBUTTON, wxCommandEvent);

// ----------------------------------------------------------------------------
// Private BButton subclass
// ----------------------------------------------------------------------------

class wxHaikuToggleButton : public BButton
{
public:
    wxHaikuToggleButton(wxToggleButton* handler, const char* name, const char* label)
        : BButton(BRect(0, 0, 100, 20), name, label, new BMessage('BTNC')),
          m_handler(handler)
    {
        SetFlags(Flags() | B_NAVIGABLE);
        SetBehavior(B_BUTTON_BEHAVIOR);
        SetValue(B_CONTROL_OFF);
    }

    virtual void AttachedToWindow() override
    {
        BButton::AttachedToWindow();
        // Ensure initial state matches handler
        SetValue(m_handler->GetValue() ? B_CONTROL_ON : B_CONTROL_OFF);
    }

    virtual void MessageReceived(BMessage* msg) override
    {
        if (msg->what == 'BTNC')
        {
            // Let BButton toggle itself first
            BButton::MessageReceived(msg);

            // Now notify wx layer of the new state
            if (m_handler)
            {
                m_handler->OnButtonClicked();
            }
        }
        else
        {
            BButton::MessageReceived(msg);
        }
    }

private:
    wxToggleButton* m_handler;
};

// ----------------------------------------------------------------------------
// wxToggleButton
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(wxToggleButton, wxToggleButtonBase);

wxToggleButton::wxToggleButton()
    : m_button(nullptr)
{
}

wxToggleButton::wxToggleButton(wxWindow* parent,
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

bool wxToggleButton::Create(wxWindow* parent,
                            wxWindowID id,
                            const wxString& label,
                            const wxPoint& pos,
                            const wxSize& size,
                            long style,
                            const wxValidator& validator,
                            const wxString& name)
{
    m_button = new wxHaikuToggleButton(this, wxToUTF8(name), wxToUTF8(label));
    m_button->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
    m_button->SetTarget(m_button); // Self-targeted

    wxWindow* win = wxCreateWindowFromBView(parent, m_button, id);
    if (!win)
        return false;

    return wxToggleButtonBase::Create(parent, id, pos, size, style, validator, name);
}

void wxToggleButton::SetValue(bool state)
{
    wxHaikuControlLock lock(m_button);
    if (!lock.IsOk())
        return;

    if (m_button)
        m_button->SetValue(state ? B_CONTROL_ON : B_CONTROL_OFF);
}

bool wxToggleButton::GetValue() const
{
    wxHaikuControlLock lock(m_button);
    if (!lock.IsOk())
        return false;

    if (m_button)
        return m_button->Value() == B_CONTROL_ON;
    return false;
}

void wxToggleButton::SetLabel(const wxString& label)
{
    wxHaikuControlLock lock(m_button);
    if (!lock.IsOk())
        return;

    if (m_button)
        m_button->SetLabel(wxToUTF8(label));
}

wxString wxToggleButton::GetLabel() const
 {
    wxHaikuControlLock lock(m_button);
    if (!lock.IsOk())
        return wxString();

    if (m_button)
        return wxString::FromUTF8(m_button->Label());
    return wxString();
}

BButton* wxToggleButton::GetButton() const
{
    return m_button;
}

void wxToggleButton::OnButtonClicked()
{
    wxCommandEvent event(wxEVT_TOGGLEBUTTON, GetId());
    event.SetEventObject(this);
    event.SetInt(GetValue());

    // Send the event synchronously (like other wx ports)
    GetEventHandler()->ProcessEvent(event);
}

int wxToggleButton::HaikuGetEventType() const
{
    return wxEVT_TOGGLEBUTTON;
}

// ----------------------------------------------------------------------------
// wxBitmapToggleButton
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(wxBitmapToggleButton, wxToggleButton);

wxBitmapToggleButton::wxBitmapToggleButton()
{
}

wxBitmapToggleButton::wxBitmapToggleButton(wxWindow* parent,
                                           wxWindowID id,
                                           const wxBitmapBundle& label,
                                           const wxPoint& pos,
                                           const wxSize& size,
                                           long style,
                                           const wxValidator& validator,
                                           const wxString& name)
{
    Create(parent, id, label, pos, size, style, validator, name);
}

bool wxBitmapToggleButton::Create(wxWindow* parent,
                                  wxWindowID id,
                                  const wxBitmapBundle& label,
                                  const wxPoint& pos,
                                  const wxSize& size,
                                  long style,
                                  const wxValidator& validator,
                                  const wxString& name)
{
    if (!wxToggleButton::Create(parent, id, wxString(), pos, size, style, validator, name))
        return false;

    if (label.IsOk())
    {
        SetBitmapLabel(label);
        SetInitialSize(size); // Adjust size if needed
    }

    return true;
}

void wxBitmapToggleButton::SetBitmapLabel(const wxBitmapBundle& bitmap)
{
    wxHaikuControlLock lock(GetButton());
    if (!lock.IsOk())
        return;

    m_bitmapBundle = bitmap;
    wxBitmap bmp = m_bitmapBundle.GetBitmapFor(this);
    if (!bmp.IsOk())
        return;

    wxImage img = bmp.ConvertToImage();
    int w = img.GetWidth();
    int h = img.GetHeight();

    // Use B_RGBA32 for alpha support
    BBitmap* bbmp = new BBitmap(BRect(0, 0, w - 1, h - 1), B_RGBA32);
    if (!bbmp->IsValid())
    {
        delete bbmp;
        return;
    }

    uint8* bits = (uint8*)bbmp->Bits();
    int bpr = bbmp->BytesPerRow(); // May be > w*4 due to alignment
    uint8* imgData = img.GetData();
    uint8* imgAlpha = img.HasAlpha() ? img.GetAlpha() : nullptr;

    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            int srcIdx = (y * w + x) * 3;
            int dstIdx = y * bpr + x * 4;

            bits[dstIdx + 0] = imgData[srcIdx + 2]; // R
            bits[dstIdx + 1] = imgData[srcIdx + 1]; // G
            bits[dstIdx + 2] = imgData[srcIdx + 0]; // B

            if (imgAlpha)
                bits[dstIdx + 3] = imgAlpha[y * w + x]; // A
            else
                bits[dstIdx + 3] = 255; // Opaque
        }
    }

    if (GetButton())
        GetButton()->SetIcon(bbmp);
}

wxBitmap wxBitmapToggleButton::GetBitmapLabel() const
{
    return m_bitmapBundle.GetBitmapFor(this);
}

#endif // wxUSE_TOGGLEBTN
