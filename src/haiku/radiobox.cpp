/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/radiobox.cpp
// Purpose:     wxRadioBox Haiku implementation (group of BRadioButtons)
// Author:      Adapted for Haiku
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#include "wx/radiobox.h"
#include "wx/haiku/private.h"
#include "wx/tooltip.h"

#include <InterfaceKit.h>
#include <Autolock.h>
#include <vector>
#include <cmath>
#include <algorithm>

wxIMPLEMENT_DYNAMIC_CLASS(wxRadioBox, wxControl);

static BRect wxToBRect(const wxPoint& pos, const wxSize& size)
{
    const wxPoint p = (pos == wxDefaultPosition) ? wxPoint(0, 0) : pos;

    if (size == wxDefaultSize)
    {
        return BRect((float)p.x, (float)p.y,
                     (float)(p.x + 140 - 1),
                     (float)(p.y + 100 - 1));
    }

    return BRect((float)p.x, (float)p.y,
                 (float)(p.x + size.x - 1),
                 (float)(p.y + size.y - 1));
}

static int wxRadioBoxGetRowsOrColsCount(int itemCount, int majorDim)
{
    if (majorDim <= 0)
        return itemCount > 0 ? itemCount : 1;

    return majorDim;
}

namespace
{

static const uint32 kRadioBoxMsg = 'wRBx';

class wxHaikuRadioBoxButton : public BRadioButton
{
public:
    wxHaikuRadioBoxButton(BRect frame,
                          const char* name,
                          const char* label,
                          wxRadioBox* owner,
                          int index)
        : BRadioButton(frame, name, label, new BMessage(kRadioBoxMsg),
                       B_FOLLOW_LEFT | B_FOLLOW_TOP,
                       B_WILL_DRAW | B_NAVIGABLE),
          m_owner(owner),
          m_index(index)
    {
        SetTarget(this);
    }

    void AttachedToWindow() override
    {
        BRadioButton::AttachedToWindow();
        SetTarget(this);
    }

    void MessageReceived(BMessage* msg) override
    {
        if (msg && msg->what == kRadioBoxMsg && m_owner)
        {
            if (Value() == B_CONTROL_ON)
            {
                m_owner->SetSelection(m_index);

                wxCommandEvent evt(wxEVT_RADIOBOX, m_owner->GetId());
                evt.SetInt(m_index);
                evt.SetEventObject(m_owner);
                m_owner->ProcessWindowEvent(evt);
            }
            return;
        }

        BRadioButton::MessageReceived(msg);
    }

private:
    wxRadioBox* m_owner;
    int m_index;
};

} // namespace

wxRadioBox::wxRadioBox(wxWindow* parent,
                       wxWindowID id,
                       const wxString& title,
                       const wxPoint& pos,
                       const wxSize& size,
                       int n,
                       const wxString choices[],
                       int majorDim,
                       long style,
                       const wxValidator& val,
                       const wxString& name)
{
    Create(parent, id, title, pos, size, n, choices, majorDim, style, val, name);
}

wxRadioBox::wxRadioBox(wxWindow* parent,
                       wxWindowID id,
                       const wxString& title,
                       const wxPoint& pos,
                       const wxSize& size,
                       const wxArrayString& choices,
                       int majorDim,
                       long style,
                       const wxValidator& val,
                       const wxString& name)
{
    Create(parent, id, title, pos, size, choices, majorDim, style, val, name);
}

bool wxRadioBox::Create(wxWindow* parent,
                        wxWindowID id,
                        const wxString& title,
                        const wxPoint& pos,
                        const wxSize& size,
                        int n,
                        const wxString choices[],
                        int majorDim,
                        long style,
                        const wxValidator& val,
                        const wxString& name)
{
    wxCHECK_MSG(parent, false, "wxRadioBox requires a parent");

    BRect r = wxToBRect(pos, size);
    m_groupView = new BBox(r,
                           name.utf8_str(),
                           B_FOLLOW_ALL_SIDES,
                           B_WILL_DRAW | B_FRAME_EVENTS,
                           B_FANCY_BORDER);
    m_groupView->SetLabel(title.utf8_str());

    HaikuAdoptView(m_groupView, nullptr, m_groupView);

    if (!wxControl::Create(parent, id, pos, size, style, val, name))
        return false;

    m_selection = wxNOT_FOUND;

    AddChoices(n, choices, style, majorDim);
    SetMajorDim(wxRadioBoxGetRowsOrColsCount(n, majorDim), style);

    if (GetCount() > 0)
        SetSelection(0);

    SetLabel(title);
    InvalidateBestSize();
    SetInitialSize(size);

    return true;
}

bool wxRadioBox::Create(wxWindow* parent,
                        wxWindowID id,
                        const wxString& title,
                        const wxPoint& pos,
                        const wxSize& size,
                        const wxArrayString& choices,
                        int majorDim,
                        long style,
                        const wxValidator& val,
                        const wxString& name)
{
    return Create(parent, id, title, pos, size,
                  (int)choices.size(),
                  choices.empty() ? nullptr : &choices[0],
                  majorDim, style, val, name);
}

void wxRadioBox::AddChoices(int count, const wxString choices[], int style, int majorDim)
{
    BAutolock lock(m_groupView ? m_groupView->Looper() : nullptr);
    if (m_groupView && m_groupView->Looper() && !lock.IsLocked())
        return;

    m_buttons.clear();

    if (!m_groupView || count <= 0)
        return;

    const bool specifyRows = (style & wxRA_SPECIFY_ROWS) != 0;
    const int majorCount = wxRadioBoxGetRowsOrColsCount(count, majorDim);

    int rows = 0;
    int cols = 0;

    if (specifyRows)
    {
        rows = majorCount;
        cols = (count + rows - 1) / rows;
    }
    else
    {
        cols = majorCount;
        rows = (count + cols - 1) / cols;
    }

    const float cellW = 120.0f;
    const float cellH = 24.0f;

    for (int i = 0; i < count; ++i)
    {
        int row = 0;
        int col = 0;

        if (specifyRows)
        {
            row = i % rows;
            col = i / rows;
        }
        else
        {
            row = i / cols;
            col = i % cols;
        }

        // Leave room for BBox's caption and border. BGroupView didn't draw
        // the wxRadioBox title at all; BBox supplies the native group frame.
        const float originX = 8.0f;
        const float originY = m_groupView->TopBorderOffset() + 4.0f;
        BRect frame(originX + col * cellW,
                    originY + row * cellH,
                    originX + col * cellW + cellW - 1,
                    originY + row * cellH + cellH - 1);

        const wxScopedCharBuffer utf8 = choices[i].utf8_str();
        const char* clabel = utf8.data();

        auto* button = new wxHaikuRadioBoxButton(
            frame,
            clabel ? clabel : "",
            clabel ? clabel : "",
            this,
            i
        );

        m_groupView->AddChild(button);
        m_buttons.push_back(button);

    }
}

unsigned int wxRadioBox::GetCount() const
{
    return static_cast<unsigned int>(m_buttons.size());
}

wxString wxRadioBox::GetString(unsigned int n) const
{
    BAutolock lock(m_groupView ? m_groupView->Looper() : nullptr);
    if (m_groupView && m_groupView->Looper() && !lock.IsLocked())
        return wxEmptyString;

    if (n < m_buttons.size() && m_buttons[n])
        return wxString::FromUTF8(m_buttons[n]->Label());

    return wxEmptyString;
}

void wxRadioBox::SetString(unsigned int n, const wxString& s)
{
    BAutolock lock(m_groupView ? m_groupView->Looper() : nullptr);
    if (m_groupView && m_groupView->Looper() && !lock.IsLocked())
        return;

    if (n < m_buttons.size() && m_buttons[n])
        m_buttons[n]->SetLabel(s.utf8_str());
}

void wxRadioBox::SetSelection(int n)
{
    BAutolock lock(m_groupView ? m_groupView->Looper() : nullptr);
    if (m_groupView && m_groupView->Looper() && !lock.IsLocked())
        return;

    if (n < 0 || static_cast<size_t>(n) >= m_buttons.size())
        return;

    for (size_t i = 0; i < m_buttons.size(); ++i)
    {
        if (m_buttons[i])
            m_buttons[i]->SetValue(i == static_cast<size_t>(n) ? B_CONTROL_ON
                                                               : B_CONTROL_OFF);
    }

    m_selection = n;

}

int wxRadioBox::GetSelection() const
{
    return m_selection;
}

bool wxRadioBox::Enable(unsigned int n, bool enable)
{
    BAutolock lock(m_groupView ? m_groupView->Looper() : nullptr);
    if (m_groupView && m_groupView->Looper() && !lock.IsLocked())
        return false;

    if (n >= m_buttons.size() || !m_buttons[n])
        return false;

    m_buttons[n]->SetEnabled(enable);
    return true;
}

bool wxRadioBox::Enable(bool enable)
{
    BAutolock lock(m_groupView ? m_groupView->Looper() : nullptr);
    if (m_groupView && m_groupView->Looper() && !lock.IsLocked())
        return false;

    for (auto* b : m_buttons)
    {
        if (b)
            b->SetEnabled(enable);
    }

    return wxControl::Enable(enable);
}

bool wxRadioBox::Show(unsigned int n, bool show)
{
    if (n >= m_buttons.size() || !m_buttons[n])
        return false;

    // Showing one item in a hidden group makes only that item visible.
    if (show && !IsShown())
    {
        for (unsigned int i = 0; i < GetCount(); ++i)
            Show(i, false);
        wxControl::Show(true);
    }

    BRadioButton* button = m_buttons[n];
    BAutolock lock(button->Looper());
    if (button->Looper() && !lock.IsLocked())
        return false;
    const bool hidden = button->IsHidden(button);
    if (show && hidden)
        button->Show();
    else if (!show && !hidden)
        button->Hide();

    return true;
}

bool wxRadioBox::Show(bool show)
{
    const bool changed = wxControl::Show(show);
    for (unsigned int i = 0; i < GetCount(); ++i)
        Show(i, show);
    return changed;
}

bool wxRadioBox::IsItemEnabled(unsigned int n) const
{
    BAutolock lock(m_groupView ? m_groupView->Looper() : nullptr);
    if (m_groupView && m_groupView->Looper() && !lock.IsLocked())
        return false;

    if (n >= m_buttons.size() || !m_buttons[n])
        return false;

    return m_buttons[n]->IsEnabled();
}

bool wxRadioBox::IsItemShown(unsigned int n) const
{
    BAutolock lock(m_groupView ? m_groupView->Looper() : nullptr);
    if (m_groupView && m_groupView->Looper() && !lock.IsLocked())
        return false;

    if (n >= m_buttons.size() || !m_buttons[n])
        return false;

    return !m_buttons[n]->IsHidden(m_buttons[n]);
}

#if wxUSE_TOOLTIPS
void wxRadioBox::DoSetItemToolTip(unsigned int item, wxToolTip* tooltip)
{
    BAutolock lock(m_groupView ? m_groupView->Looper() : nullptr);
    if (m_groupView && m_groupView->Looper() && !lock.IsLocked())
        return;

    if (item >= m_buttons.size() || !m_buttons[item])
        return;

    if (tooltip)
        m_buttons[item]->SetToolTip(tooltip->GetTip().utf8_str());
    else
        m_buttons[item]->SetToolTip("");
}
#endif

void wxRadioBox::SetLabel(const wxString& label)
{
    BAutolock lock(m_groupView ? m_groupView->Looper() : nullptr);
    if (m_groupView && m_groupView->Looper() && !lock.IsLocked())
        return;

    wxControlBase::SetLabel(label);
    if (m_groupView)
        m_groupView->SetLabel(label.utf8_str());
}

wxString wxRadioBox::GetLabel() const
{
    return wxControlBase::GetLabel();
}

BBox* wxRadioBox::GetGroupView() const
{
    return m_groupView;
}

bool wxRadioBox::HaikuHandleMouseEvent(BView* view, BPoint WXUNUSED(point), uint32 kind)
{
    if (kind != B_MOUSE_DOWN)
        return false;

    for (size_t i = 0; i < m_buttons.size(); ++i)
    {
        if (m_buttons[i] == view)
        {
            if (static_cast<int>(i) != m_selection)
            {
                SetSelection(static_cast<int>(i));

                wxCommandEvent evt(wxEVT_RADIOBOX, GetId());
                evt.SetInt((int)i);
                evt.SetEventObject(this);
                ProcessWindowEvent(evt);
            }

            return true;
        }
    }

    return false;
}

wxSize wxRadioBox::DoGetBestSize() const
{
    if (!m_groupView)
        return wxSize(1, 1);
    BAutolock lock(m_groupView->Looper());
    float width = 16, height = m_groupView->TopBorderOffset() + 8;
    if (m_groupView->Label())
        width = std::max(width, m_groupView->StringWidth(m_groupView->Label()) + 24);
    for (BRadioButton* button : m_buttons)
    {
        const BRect frame = button->Frame();
        width = std::max(width, frame.right + 9);
        height = std::max(height, frame.bottom + 9);
    }
    return wxSize(static_cast<int>(std::ceil(width)),
                  static_cast<int>(std::ceil(height)));
}
