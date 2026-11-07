///////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/slider.cpp
// Purpose:     wxSlider implementation for Haiku
// Author:      Based on Qt; adapted for Haiku
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#if wxUSE_SLIDER

#ifndef WX_PRECOMP
    #include "wx/slider.h"
#endif

#include "wx/slider.h"
#include "wx/haiku/private.h"

#include <Slider.h>
#include <Message.h>
#include <View.h>
#include <Autolock.h>
#include <memory>

// Native controls belong to the window looper, while wx timers run on the
// application looper. Keep the window locked for every native slider access.
class wxHaikuSliderLock
{
public:
    explicit wxHaikuSliderLock(BSlider* slider)
        : m_slider(slider),
          m_lock(slider && slider->Looper() ? new BAutolock(slider->Looper()) : nullptr)
    {
    }

    bool IsOk() const { return m_slider && (!m_lock || m_lock->IsLocked()); }

private:
    BSlider* m_slider;
    std::unique_ptr<BAutolock> m_lock;
};

//wxIMPLEMENT_DYNAMIC_CLASS(wxSlider, wxSliderBase);

static BRect wxToBRect(const wxPoint& pos, const wxSize& size, bool vertical)
{
    const wxPoint p = (pos == wxDefaultPosition) ? wxPoint(0, 0) : pos;

    const int defW = vertical ? 30 : 100;
    const int defH = vertical ? 100 : 30;

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

class wxHaikuSliderImpl : public BSlider
{
public:
    wxHaikuSliderImpl(BRect frame,
                      const char* name,
                      wxSlider* handler,
                      int minValue,
                      int maxValue,
                      int value,
                      orientation orient)
        : BSlider(frame, name, "", new BMessage('wSld'),
                  minValue, maxValue, orient),
          m_handler(handler),
          m_min(minValue),
          m_max(maxValue)
    {
        SetTarget(this);
        SetModificationMessage(new BMessage('wTrk'));
        SetValue(value);
    }

    void AttachedToWindow() override
    {
        BSlider::AttachedToWindow();
        SetTarget(this);
    }

    int LogicalValue() const
    {
        return m_handler->HasFlag(wxSL_INVERSE)
            ? (int)(int64(m_min)+m_max-Value()) : Value();
    }
    int NativeValue(int value) const
    {
        value=wxMax(m_min,wxMin(m_max,value));
        return m_handler->HasFlag(wxSL_INVERSE)
            ? (int)(int64(m_min)+m_max-value) : value;
    }
    void SendScroll(wxEventType type,int value)
    {
        wxScrollEvent scroll(type,m_handler->GetId(),value,
            m_handler->HasFlag(wxSL_VERTICAL) ? wxVERTICAL : wxHORIZONTAL);
        scroll.SetEventObject(m_handler);
        m_handler->HandleWindowEvent(scroll);
        wxCommandEvent event(wxEVT_SLIDER,m_handler->GetId());
        event.SetEventObject(m_handler);
        event.SetInt(value);
        m_handler->HandleWindowEvent(event);
    }
    void MessageReceived(BMessage* message) override
    {
        if (message && message->what=='wTrk')
        {
            int32 value=Value();
            message->FindInt32("be:value",&value);
            SendScroll(wxEVT_SCROLL_THUMBTRACK,NativeValue(value));
            return;
        }
        if (message && message->what=='wSld')
        {
            int32 value=Value();
            message->FindInt32("be:value",&value);
            SendScroll(wxEVT_SCROLL_THUMBRELEASE,NativeValue(value));
            return;
        }
        BSlider::MessageReceived(message);
    }
    void KeyDown(const char* bytes,int32 count) override
    {
        if (count != 1 || !IsEnabled()) { BSlider::KeyDown(bytes,count); return; }
        const int old=LogicalValue();
        int64 value=Value();
        wxEventType type=wxEVT_NULL;
        switch (bytes[0])
        {
            case B_LEFT_ARROW: case B_UP_ARROW:
                value-=m_handler->GetLineSize(); type=wxEVT_SCROLL_LINEUP; break;
            case B_RIGHT_ARROW: case B_DOWN_ARROW:
                value+=m_handler->GetLineSize(); type=wxEVT_SCROLL_LINEDOWN; break;
            case B_PAGE_UP:
                value-=m_handler->GetPageSize(); type=wxEVT_SCROLL_PAGEUP; break;
            case B_PAGE_DOWN:
                value+=m_handler->GetPageSize(); type=wxEVT_SCROLL_PAGEDOWN; break;
            case B_HOME: value=m_min; type=wxEVT_SCROLL_TOP; break;
            case B_END: value=m_max; type=wxEVT_SCROLL_BOTTOM; break;
            default: BSlider::KeyDown(bytes,count); return;
        }
        value=wxMax(int64(m_min),wxMin(int64(m_max),value));
        SetValue((int)value);
        if (LogicalValue()!=old) SendScroll(type,LogicalValue());
    }

    void SetLimitsCompat(int minValue, int maxValue)
    {
        m_min = minValue;
        m_max = maxValue;
        SetLimits(minValue, maxValue);
    }

    int GetMinCompat() const { return m_min; }
    int GetMaxCompat() const { return m_max; }

private:
    wxSlider* m_handler;
    int m_min;
    int m_max;
};

wxSlider::wxSlider(wxWindow* parent, wxWindowID id,
                   int value, int minValue, int maxValue,
                   const wxPoint& pos, const wxSize& size,
                   long style, const wxValidator& validator, const wxString& name)
{
    Create(parent, id, value, minValue, maxValue, pos, size, style, validator, name);
}

bool wxSlider::Create(wxWindow* parent, wxWindowID id,
                      int value, int minValue, int maxValue,
                      const wxPoint& pos, const wxSize& size,
                      long style, const wxValidator& validator, const wxString& name)
{
    const bool vertical = (style & wxSL_VERTICAL) != 0;
    const orientation orient = vertical ? B_VERTICAL : B_HORIZONTAL;

    m_slider = new wxHaikuSliderImpl(
        wxToBRect(pos, size, vertical),
        name.utf8_str(),
        this,
        minValue,
        maxValue,
        value,
        orient);

    // Same pattern as button/stattext/radiobutton.
    HaikuAdoptView(m_slider, nullptr, m_slider);

    if (!wxSliderBase::Create(parent, id, pos, size, style, validator, name))
        return false;

    // The style is available only after the wx base creation.
    SetValue(value);
    m_lineSize = 1;
    m_pageSize = (maxValue > minValue) ? wxMax(1, (maxValue - minValue) / 10) : 1;

    return true;
}

int wxSlider::GetValue() const
{
    wxHaikuSliderLock lock(m_slider);
    return lock.IsOk() ? static_cast<wxHaikuSliderImpl*>(m_slider)->LogicalValue() : 0;
}

void wxSlider::SetValue(int value)
{
    wxHaikuSliderLock lock(m_slider);
    if (lock.IsOk())
        m_slider->SetValue(static_cast<wxHaikuSliderImpl*>(m_slider)->NativeValue(value));
}

void wxSlider::SetRange(int minValue, int maxValue)
{
    wxHaikuSliderLock lock(m_slider);
    wxCHECK_RET(minValue <= maxValue,"invalid slider range");
    if (lock.IsOk())
    {
        auto* slider=static_cast<wxHaikuSliderImpl*>(m_slider);
        const int old=slider->LogicalValue();
        slider->SetLimitsCompat(minValue,maxValue);
        slider->SetValue(slider->NativeValue(old));
    }
}

int wxSlider::GetMin() const
{
    wxHaikuSliderLock lock(m_slider);
    return lock.IsOk()
        ? static_cast<wxHaikuSliderImpl*>(m_slider)->GetMinCompat()
        : 0;
}

int wxSlider::GetMax() const
{
    wxHaikuSliderLock lock(m_slider);
    return lock.IsOk()
        ? static_cast<wxHaikuSliderImpl*>(m_slider)->GetMaxCompat()
        : 100;
}

int wxSlider::GetTickFreq() const
{
    return 1;
}

void wxSlider::ClearTicks()
{
}

void wxSlider::SetTick(int WXUNUSED(tickPos))
{
}

void wxSlider::SetLineSize(int sz)
{
    wxCHECK_RET(sz > 0,"slider line size must be positive");
    m_lineSize = sz;
}

void wxSlider::SetPageSize(int sz)
{
    wxCHECK_RET(sz > 0,"slider page size must be positive");
    m_pageSize = sz;
}

int wxSlider::GetLineSize() const
{
    return m_lineSize;
}

int wxSlider::GetPageSize() const
{
    return m_pageSize;
}

void wxSlider::SetThumbLength(int WXUNUSED(len))
{
}

int wxSlider::GetThumbLength() const
{
    return 20;
}

void wxSlider::DoSetTickFreq(int WXUNUSED(n))
{
}

wxSize wxSlider::DoGetBestSize() const
{
    wxHaikuSliderLock lock(m_slider);
    if (lock.IsOk())
    {
        BSize s = m_slider->PreferredSize();
        return wxSize((int)(s.width + 1), (int)(s.height + 1));
    }

    return HasFlag(wxSL_VERTICAL) ? wxSize(30, 100)
                                  : wxSize(100, 30);
}

#endif // wxUSE_SLIDER
