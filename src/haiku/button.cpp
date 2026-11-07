/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/button.cpp
// Purpose:     wxButton implementation for Haiku using BButton
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"
#include "control_lock.h"

#ifndef WX_PRECOMP
    #include "wx/button.h"
#endif

#include "wx/button.h"
#include "wx/haiku/private.h"
#include "wx/stockitem.h"

#include <Button.h>
#include <Message.h>
#include <View.h>
#include <ControlLook.h>
#include <LayoutUtils.h>
#include <String.h>
#include <cmath>

//#define HAIKU_LOG(msg, ...) wxLogDebug("[HAIKU-BUTTON:%s:%d] " msg, __func__, __LINE__, ##__VA_ARGS__)

static BRect wxToBRect(const wxPoint& pos, const wxSize& size)
{
    const wxPoint p = (pos == wxDefaultPosition) ? wxPoint(0, 0) : pos;

    if (size == wxDefaultSize)
        return BRect((float)p.x, (float)p.y,
                     (float)(p.x + 80 - 1), (float)(p.y + 25 - 1));

    return BRect((float)p.x, (float)p.y,
                 (float)(p.x + size.x - 1),
                 (float)(p.y + size.y - 1));
}

class wxHaikuButton : public BButton
{
public:
    wxHaikuButton(BRect frame,
                  const char* name,
                  const char* label,
                  wxButton* wxBtn)
        : BButton(frame, name, label, new BMessage('wBtn')),
          m_wxButton(wxBtn)
    {
        SetTarget(this);
    }

    void AttachedToWindow() override
    {
        BButton::AttachedToWindow();
        SetTarget(this);
    }

    void SetWxLabel(const wxString& label)
    {
        wxString text;
        m_mnemonic = wxControl::FindAccelIndex(label, &text);
        m_text = text;
        BButton::SetLabel(text.utf8_str());
        InvalidateLayout();
        Invalidate();
    }

    void Draw(BRect updateRect) override
    {
        BButton::Draw(updateRect);
        if (m_mnemonic == wxNOT_FOUND)
            return;

        // Match the native label's content rectangle, truncation and alignment.
        uint32 flags = be_control_look->Flags(this);
        if (IsDefault())
            flags |= BControlLook::B_DEFAULT_BUTTON;
        float left, top, right, bottom;
        be_control_look->GetInsets(BControlLook::B_BUTTON_FRAME,
            BControlLook::B_BUTTON_BACKGROUND, flags, left, top, right, bottom);
        BRect content = Bounds();
        content.left += left;
        content.top += top;
        content.right -= right;
        content.bottom -= bottom;

        BFont font;
        GetFont(&font);
        BString visible(Label());
        font.TruncateString(&visible, B_TRUNCATE_END, content.Width() + 1);
        const wxString prefix = m_text.Left(m_mnemonic + 1);
        if (!wxString::FromUTF8(visible.String()).StartsWith(prefix))
            return; // The mnemonic itself was elided.

        font_height height;
        font.GetHeight(&height);
        BRect textRect = BLayoutUtils::AlignOnRect(content,
            BSize(std::ceil(font.StringWidth(visible.String())) - 1,
                  std::ceil(height.ascent) + std::ceil(height.descent) - 1),
            BAlignment(B_ALIGN_CENTER, B_ALIGN_MIDDLE));
        const float x = textRect.left
            + font.StringWidth(m_text.Left(m_mnemonic).utf8_str());
        const float end = textRect.left + font.StringWidth(prefix.utf8_str());
        const float y = textRect.top + std::ceil(height.ascent) + 1;
        PushState();
        rgb_color colour = ui_color(B_CONTROL_TEXT_COLOR);
        if (!IsEnabled())
            colour = disable_color(colour, ui_color(B_CONTROL_BACKGROUND_COLOR));
        SetHighColor(colour);
        SetPenSize(1);
        StrokeLine(BPoint(x, y), BPoint(end - 1, y));
        PopState();
    }

    void MessageReceived(BMessage* msg) override
    {
        if (msg && msg->what == 'wBtn')
        {
            // The native view can outlive its wx owner. Resolve the current
            // association rather than dispatching through a stale owner pointer.
            wxButton* button = dynamic_cast<wxButton*>(
                wxWindow::HaikuRetrieveWindowPointer(this));
            if (!button)
                return;
            wxCommandEvent event(wxEVT_BUTTON, button->GetId());
            event.SetEventObject(button);
            button->GetEventHandler()->ProcessEvent(event);
            return;
        }

        BButton::MessageReceived(msg);
    }

private:
    wxButton* m_wxButton;
    wxString m_text;
    int m_mnemonic = wxNOT_FOUND;
};

// Return the preferred size of a native Haiku push button.
/* static */
wxSize wxButtonBase::GetDefaultSize(wxWindow* WXUNUSED(win))
{
    BButton button(BRect(0, 0, 79, 24),
                   "wxDefaultButton",
                   "OK",
                   nullptr);

    const BSize size = button.PreferredSize();

    if (size.width <= 0 || size.height <= 0)
        return wxSize(80, 25);

    return wxSize(static_cast<int>(std::ceil(size.width + 1)),
                  static_cast<int>(std::ceil(size.height + 1)));
}

wxButton::wxButton(wxWindow* parent,
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

bool wxButton::Create(wxWindow* parent,
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

    // Create native control first, like Qt.
    m_bButton = new wxHaikuButton(
        wxToBRect(pos, size),
        name.utf8_str(),
        "",
        this);

    // Make the native control the actual wx handle.
    HaikuAdoptView(m_bButton, nullptr, m_bButton);

    if (!wxButtonBase::Create(parent, id, pos, size, style, validator, name))
        return false;

    SetLabel(effectiveLabel);
    return true;
}

void wxButton::SetLabel(const wxString& label)
{
    BLooper* const looper = m_bButton ? m_bButton->Looper() : nullptr;
    if (looper && !looper->Lock())
        return;

    wxControl::SetLabel(label);
    if (m_bButton)
        // Bitmap-only buttons must suppress text even for stock IDs.
        static_cast<wxHaikuButton*>(m_bButton)->SetWxLabel(
            HasFlag(wxBU_NOTEXT) ? wxString() : label);
    InvalidateBestSize();

    if (looper)
        looper->Unlock();
}

wxWindow* wxButton::SetDefault()
{
    wxWindow* oldDefault = wxButtonBase::SetDefault();

    if (m_bButton)
    {
        if (BWindow* win = m_bButton->Window())
        {
            if (win->LockLooper())
            {
                m_bButton->MakeDefault(true);
                win->UnlockLooper();
            }
        }
        else
        {
            m_bButton->MakeDefault(true);
        }
    }

    return oldDefault;
}

wxSize wxButton::DoGetBestSize() const
{
    wxHaikuControlLock lock(m_bButton);
    if (!lock.IsOk())
        return wxSize(80, 25);

    if (m_bButton)
    {
        BSize s = m_bButton->PreferredSize();
        return wxSize((int)(s.width + 1), (int)(s.height + 1));
    }

    return wxSize(80, 25);
}

bool wxButton::DoGetAuthNeeded() const
{
    return false;
}

void wxButton::DoSetAuthNeeded(bool WXUNUSED(show))
{
}

