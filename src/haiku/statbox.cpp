/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/statbox.cpp
// Purpose:     wxStaticBox for Haiku
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#if wxUSE_STATBOX

#ifndef WX_PRECOMP
    #include "wx/statbox.h"
#endif

#include "wx/statbox.h"
#include "wx/haiku/private.h"

#include <InterfaceKit.h>
#include <cmath>

//wxIMPLEMENT_DYNAMIC_CLASS(wxStaticBox, wxControl);

static BRect wxToBRect(const wxPoint& pos, const wxSize& size)
{
    const wxPoint p = (pos == wxDefaultPosition) ? wxPoint(0, 0) : pos;

    if (size == wxDefaultSize)
    {
        return BRect((float)p.x, (float)p.y,
                     (float)(p.x + 100 - 1),
                     (float)(p.y + 100 - 1));
    }

    return BRect((float)p.x, (float)p.y,
                 (float)(p.x + size.x - 1),
                 (float)(p.y + size.y - 1));
}

class wxHaikuBox : public BBox
{
public:
    wxHaikuBox(BRect frame, const char* label, wxStaticBox* owner)
        : BBox(frame,
               "wxStaticBox",
               B_FOLLOW_ALL_SIDES,
               B_WILL_DRAW | B_FRAME_EVENTS,
               B_FANCY_BORDER),
          m_owner(owner)
    {
        wxWindow::HaikuStoreWindowPointer(this, owner);
        SetWxLabel(wxString::FromUTF8(label));
    }

    void SetWxLabel(const wxString& label)
    {
        m_mnemonic = wxControl::FindAccelIndex(label, &m_text);
        BBox::SetLabel(m_text.utf8_str());
        Invalidate();
    }

    void Draw(BRect updateRect) override
    {
        BBox::Draw(updateRect);
        if (m_mnemonic == wxNOT_FOUND)
            return;
        font_height height;
        GetFontHeight(&height);
        const float y = height.ascent
            - std::round((height.ascent + height.descent) / 6) + 1;
        const float start = 10 + StringWidth(m_text.Left(m_mnemonic).utf8_str());
        const float end = 10 + StringWidth(m_text.Left(m_mnemonic + 1).utf8_str());
        PushState();
        SetHighColor(ui_color(B_PANEL_TEXT_COLOR));
        SetPenSize(1);
        StrokeLine(BPoint(start, y), BPoint(end - 1, y));
        PopState();
    }

private:
    wxStaticBox* m_owner;
    wxString m_text;
    int m_mnemonic = wxNOT_FOUND;
};

wxStaticBox::wxStaticBox(wxWindow *parent,
                         wxWindowID id,
                         const wxString& label,
                         const wxPoint& pos,
                         const wxSize& size,
                         long style,
                         const wxString& name)
{
    Create(parent, id, label, pos, size, style, name);
}

bool wxStaticBox::Create(wxWindow *parent,
                         wxWindowID id,
                         const wxString& label,
                         const wxPoint& pos,
                         const wxSize& size,
                         long style,
                         const wxString& name)
{
    m_haikuBox = new wxHaikuBox(
        wxToBRect(pos, size),
        label.utf8_str(),
        this);

    HaikuAdoptView(m_haikuBox, nullptr, m_haikuBox);

    if (!wxStaticBoxBase::Create(parent, id, pos, size, style,
                                 wxDefaultValidator, name))
    {
        return false;
    }

    SetLabel(label);
    return true;
}

void wxStaticBox::SetLabel(const wxString& label)
{
    BLooper* looper = m_haikuBox ? m_haikuBox->Looper() : nullptr;
    if (looper && !looper->Lock())
        return;
    wxStaticBoxBase::SetLabel(label);
    m_label = label;

    if (m_haikuBox)
        static_cast<wxHaikuBox*>(m_haikuBox)->SetWxLabel(label);
    InvalidateBestSize();
    if (looper)
        looper->Unlock();
}

wxString wxStaticBox::GetLabel() const
{
    return m_label;
}

void wxStaticBox::GetBordersForSizer(int *borderTop, int *borderOther) const
{
    wxStaticBoxBase::GetBordersForSizer(borderTop, borderOther);

    if (borderTop)
        *borderTop += GetCharHeight();
}

#endif // wxUSE_STATBOX
