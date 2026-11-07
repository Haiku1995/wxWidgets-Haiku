/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/scrolbar.cpp
// Purpose:     wxScrollBar Haiku implementation (BScrollBar wrapper)
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"
#include "control_lock.h"

#if wxUSE_SCROLLBAR

#ifdef __BORLANDC__
    #pragma hdrstop
#endif

#ifndef WX_PRECOMP
    #include "wx/scrolbar.h"
#endif

#include "wx/scrolbar.h"
#include "wx/haiku/private.h"

#include <InterfaceKit.h>
#include <Message.h>
#include <Autolock.h>
#include <memory>

//wxIMPLEMENT_DYNAMIC_CLASS(wxScrollBar, wxScrollBarBase);

static BRect wxToBRect(const wxPoint& pos, const wxSize& size, bool vertical)
{
    const wxPoint p = (pos == wxDefaultPosition) ? wxPoint(0, 0) : pos;

    const int defW = vertical ? B_V_SCROLL_BAR_WIDTH : 100;
    const int defH = vertical ? 100 : B_H_SCROLL_BAR_HEIGHT;

    if (size == wxDefaultSize)
    {
        return BRect((float)p.x, (float)p.y,
                     (float)(p.x + defW - 1),
                     (float)(p.y + defH - 1));
    }

    return BRect((float)p.x, (float)p.y,
                 (float)(p.x + size.x - 1),
                 (float)(p.y + size.y - 1));
}

// ---------------------------------------------------------------------------
// Custom BScrollBar subclass to forward scroll events to wx
// ---------------------------------------------------------------------------

class wxHaikuScrollBarImpl : public BScrollBar
{
public:
    wxHaikuScrollBarImpl(BRect frame,
                         wxScrollBar* handler,
                         orientation orient)
        : BScrollBar(frame, "wxScrollBar", nullptr,
                     0.0f, 100.0f, orient),
          m_wxHandler(handler), m_programmaticChange(false)
    {
    }

    void ValueChanged(float newValue) override
    {
        BScrollBar::ValueChanged(newValue);

        if (m_wxHandler && !m_programmaticChange)
        {
            wxScrollEvent evt(wxEVT_SCROLL_THUMBTRACK, m_wxHandler->GetId());
            evt.SetPosition((int)newValue);
            evt.SetOrientation(Orientation() == B_VERTICAL ? wxVERTICAL : wxHORIZONTAL);
            evt.SetEventObject(m_wxHandler);
            m_wxHandler->GetEventHandler()->ProcessEvent(evt);
        }
    }

    void SetProgrammaticChange(bool changing) { m_programmaticChange = changing; }

private:
    wxScrollBar* m_wxHandler;
    bool m_programmaticChange;
};

// ---------------------------------------------------------------------------
// wxScrollBar implementation
// ---------------------------------------------------------------------------

wxScrollBar::wxScrollBar(wxWindow *parent,
                         wxWindowID id,
                         const wxPoint& pos,
                         const wxSize& size,
                         long style,
                         const wxValidator& validator,
                         const wxString& name)
{
    Create(parent, id, pos, size, style, validator, name);
}

bool wxScrollBar::Create(wxWindow *parent,
                         wxWindowID id,
                         const wxPoint& pos,
                         const wxSize& size,
                         long style,
                         const wxValidator& validator,
                         const wxString& name)
{
    const bool vertical = (style & wxSB_VERTICAL) != 0;
    const orientation orient = vertical ? B_VERTICAL : B_HORIZONTAL;

    m_scrollbar = new wxHaikuScrollBarImpl(
        wxToBRect(pos, size, vertical),
        this,
        orient);

    HaikuAdoptView(m_scrollbar, nullptr, m_scrollbar);

    if (!wxScrollBarBase::Create(parent, id, pos, size, style, validator, name))
        return false;

    SetScrollbar(0, 10, 100, 10, false);
    return true;
}

// --- wxScrollBarBase virtual methods ---

int wxScrollBar::GetThumbPosition() const
{
    wxHaikuControlLock lock(m_scrollbar);
    if (!lock.IsOk())
        return m_thumbPos;

    return m_scrollbar ? (int)m_scrollbar->Value() : m_thumbPos;
}

int wxScrollBar::GetThumbSize() const
{
    return m_thumbSize;
}

int wxScrollBar::GetPageSize() const
{
    return m_pageSize;
}

int wxScrollBar::GetRange() const
{
    return m_range;
}

void wxScrollBar::SetThumbPosition(int pos)
{
    m_thumbPos = wxMax(0, wxMin(pos, wxMax(0, m_range - m_thumbSize)));

    if (m_scrollbar)
    {
        BLooper* const looper = m_scrollbar->Looper();
        std::unique_ptr<BAutolock> lock(looper ? new BAutolock(looper) : nullptr);
        if (lock && !lock->IsLocked())
            return;
        auto* const native = static_cast<wxHaikuScrollBarImpl*>(m_scrollbar);
        native->SetProgrammaticChange(true);
        native->SetValue((float)m_thumbPos);
        native->SetProgrammaticChange(false);
    }
}

void wxScrollBar::SetScrollbar(int position, int thumbSize,
                               int range, int pageSize,
                               bool WXUNUSED(refresh))
{
    m_range = wxMax(0, range);
    m_thumbSize = wxMax(0, wxMin(thumbSize, m_range));
    m_pageSize = wxMax(0, pageSize);
    m_thumbPos = wxMax(0, wxMin(position, m_range - m_thumbSize));

    if (m_scrollbar)
    {
        BLooper* const looper = m_scrollbar->Looper();
        std::unique_ptr<BAutolock> lock(looper ? new BAutolock(looper) : nullptr);
        if (lock && !lock->IsLocked())
            return;
        auto* const native = static_cast<wxHaikuScrollBarImpl*>(m_scrollbar);
        native->SetProgrammaticChange(true);
        const float maxRange = (float)(m_range - m_thumbSize);
        m_scrollbar->SetRange(0.0f, maxRange);
        m_scrollbar->SetSteps(1.0f, (float)m_pageSize);

        if (m_range > 0)
            m_scrollbar->SetProportion((float)m_thumbSize / (float)m_range);
        else
            m_scrollbar->SetProportion(1.0f);

        m_scrollbar->SetValue((float)m_thumbPos);
        native->SetProgrammaticChange(false);
    }
}

#endif // wxUSE_SCROLLBAR
