/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/calctrl.cpp
// Purpose:     wxCalendarCtrl implementation for wxHaiku using BCalendarView
// Author:      (based on wxQt port and Haiku BCalendarView)
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#if wxUSE_CALENDARCTRL
/*
#include "wx/calctrl.h"
#include "wx/haiku/private.h"           // for wxHaikuConvertDate / helpers
//#include "calctrl.h"                    // our header

#include "wx/haiku/CalendarView.h"               // Haiku's BCalendarView

#include <Font.h>
#include <String.h>

// Helper date conversion functions (you can place these in wx/haiku/private.h)
static wxDateTime wxHaikuConvertDate(const BDate& bdate)
{
    if (!bdate.IsValid())
        return wxDefaultDateTime;
    return wxDateTime(bdate.Day(),
                      static_cast<wxDateTime::Month>(bdate.Month() - 1),
                      bdate.Year());
}

static BDate wxHaikuConvertWxDate(const wxDateTime& wxdate)
{
    if (!wxdate.IsValid())
        return BDate();
    return BDate(wxdate.GetYear(),
                 wxdate.GetMonth() + 1,
                 wxdate.GetDay());
}

// ────────────────────────────────────────────────────────────────────────────
// wxHaikuCalendarView — subclass of BCalendarView with wx integration
// ────────────────────────────────────────────────────────────────────────────

class wxHaikuCalendarView : public BPrivate::BCalendarView
{
public:
    wxHaikuCalendarView(const char* name, uint32 flags = B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE | B_PULSE_NEEDED)
        : BCalendarView(name, flags),
          m_handler(nullptr),
          m_showHolidays(true),
          m_monthChangeEnabled(true)
    {
        m_headerFg = ui_color(B_PANEL_TEXT_COLOR);
        m_headerBg = ui_color(B_PANEL_BACKGROUND_COLOR);
        m_holidayFg = { 200, 0, 0, 255 };     // reddish
        m_holidayBg = { 255, 240, 240, 255 }; // very light red
        ClearDateAttrs();
    }

    wxCalendarCtrl* GetHandler() const { return m_handler; }
    void SetHandler(wxCalendarCtrl* handler) { m_handler = handler; }

    void SetHeaderColours(const wxColour& fg, const wxColour& bg)
    {
        m_headerFg = { fg.Red(), fg.Green(), fg.Blue(), 255 };
        m_headerBg = { bg.Red(), bg.Green(), bg.Blue(), 255 };
        Invalidate();
    }

    wxColour GetHeaderColourFg() const { return wxColour(m_headerFg.red, m_headerFg.green, m_headerFg.blue); }
    wxColour GetHeaderColourBg() const { return wxColour(m_headerBg.red, m_headerBg.green, m_headerBg.blue); }

    void SetHolidayColours(const wxColour& fg, const wxColour& bg)
    {
        m_holidayFg = { fg.Red(), fg.Green(), fg.Blue(), 255 };
        m_holidayBg = { bg.Red(), bg.Green(), bg.Blue(), 255 };
        Invalidate();
    }

    wxColour GetHolidayColourFg() const { return wxColour(m_holidayFg.red, m_holidayFg.green, m_holidayFg.blue); }
    wxColour GetHolidayColourBg() const { return wxColour(m_holidayBg.red, m_holidayBg.green, m_holidayBg.blue); }

    void SetShowHolidays(bool show) { m_showHolidays = show; Invalidate(); }

    void EnableMonthChange(bool enable) { m_monthChangeEnabled = enable; }

    void SetDateRange(const wxDateTime& lower, const wxDateTime& upper)
    {
        m_minDate = lower.IsValid() ? wxHaikuConvertWxDate(lower) : BDate();
        m_maxDate = upper.IsValid() ? wxHaikuConvertWxDate(upper) : BDate();
        Invalidate();
    }

    bool GetDateRange(wxDateTime* lower, wxDateTime* upper) const
    {
        if (lower) *lower = wxHaikuConvertDate(m_minDate);
        if (upper) *upper = wxHaikuConvertDate(m_maxDate);
        return true;
    }

    // Custom drawing — override to apply attributes & holidays
    virtual void DrawDay(BView* owner, BRect frame, const char* text,
                         bool isSelected, bool isEnabled,
                         bool focus, bool highlight) override;

    virtual status_t Invoke(BMessage* msg = nullptr) override;

private:
    struct DateAttr {
        rgb_color fg, bg;
        bool bold;
        bool hasFg : 1;
        bool hasBg : 1;
        bool hasBold : 1;
        DateAttr() : bold(false), hasFg(false), hasBg(false), hasBold(false) {}
    } m_dateAttrs[31];

    rgb_color m_headerFg, m_headerBg;
    rgb_color m_holidayFg, m_holidayBg;

    bool m_showHolidays;
    bool m_monthChangeEnabled;

    BDate m_minDate;
    BDate m_maxDate;

    wxCalendarCtrl* m_handler;

    void ClearDateAttrs()
    {
        for (int i = 0; i < 31; ++i)
            m_dateAttrs[i] = DateAttr();
    }
};

void wxHaikuCalendarView::DrawDay(BView* owner, BRect frame, const char* text,
                                  bool isSelected, bool isEnabled,
                                  bool focus, bool highlight)
{
    rgb_color textColor = ui_color(isSelected ? B_LIST_SELECTED_ITEM_TEXT_COLOR : B_LIST_ITEM_TEXT_COLOR);
    rgb_color bgColor   = ui_color(isSelected ? B_LIST_SELECTED_BACKGROUND_COLOR : B_LIST_BACKGROUND_COLOR);

    int day = isEnabled ? atoi(text) : 0;

    // Apply custom attributes first (they have highest priority)
    if (day >= 1 && day <= 31)
    {
        const DateAttr& da = m_dateAttrs[day - 1];
        if (da.hasFg)   textColor = da.fg;
        if (da.hasBg)   bgColor   = da.bg;
        if (da.hasBold) highlight = da.bold;
    }

    // Then apply holiday colours if enabled and day is weekend
    if (m_showHolidays && isEnabled && day >= 1 && day <= 31)
    {
        BDate d = Date();
        d.SetDay(day);
        if (d.IsValid() && (d.DayOfWeek() == B_WEEKDAY_SATURDAY || d.DayOfWeek() == B_WEEKDAY_SUNDAY))
        {
            textColor = m_holidayFg;
            if (!isSelected)
                bgColor = m_holidayBg;
        }
    }

    // Disabled tint
    if (!isEnabled)
    {
        float tint = (textColor.red + textColor.green + textColor.blue > 380) ? B_DARKEN_2_TINT : B_LIGHTEN_2_TINT;
        textColor = tint_color(textColor, tint);
    }

    // Background
    owner->SetHighColor(bgColor);
    owner->FillRect(frame.InsetByCopy(1,1));

    // Focus ring
    if (focus)
    {
        rgb_color focusCol = keyboard_navigation_color();
        owner->SetHighColor(focusCol);
        owner->StrokeRect(frame.InsetByCopy(1,1));
        if (!isSelected)
            textColor = focusCol;
    }

    // Text
    owner->SetHighColor(textColor);

    BFont font(be_plain_font);
    if (highlight)
        font.SetFace(B_BOLD_FACE);
    owner->SetFont(&font);

    float tw = owner->StringWidth(text);
    float th = FontHeight(owner);
    BPoint pt(frame.left + (frame.Width() - tw)/2,
              frame.top  + (frame.Height() + th/2)/2);

    owner->DrawString(text, pt);
}

status_t wxHaikuCalendarView::Invoke(BMessage* msg)
{
    BDate oldDate = Date();

    status_t st = BCalendarView::Invoke(msg);

    if (m_handler && st == B_OK)
    {
        BDate newDate = Date();

        wxDateTime oldDt = wxHaikuConvertDate(oldDate);
        wxDateTime newDt = wxHaikuConvertDate(newDate);

        // Month/year changed → clear per-day attributes (wx behaviour)
        if (oldDate.Month() != newDate.Month() || oldDate.Year() != newDate.Year())
            ClearDateAttrs();

        m_handler->GenerateAllChangeEvents(oldDt);

        // Double-click / Enter / Space usually selects same month → double-click event
        if (newDate.Month() == oldDate.Month() && newDate.Year() == oldDate.Year())
            m_handler->GenerateEvent(wxEVT_CALENDAR_DOUBLECLICKED);
    }

    return st;
}

// ────────────────────────────────────────────────────────────────────────────
// wxCalendarCtrl implementation
// ────────────────────────────────────────────────────────────────────────────

//wxIMPLEMENT_DYNAMIC_CLASS(wxCalendarCtrl, wxCalendarCtrlBase);

void wxCalendarCtrl::Init()
{
    for (size_t i = 0; i < WXSIZEOF(m_attrs); ++i)
        m_attrs[i] = nullptr;

    m_calendar = nullptr;
}

wxCalendarCtrl::~wxCalendarCtrl()
{
    for (size_t i = 0; i < WXSIZEOF(m_attrs); ++i)
        delete m_attrs[i];
}

bool wxCalendarCtrl::Create(wxWindow *parent, wxWindowID id,
                            const wxDateTime& date,
                            const wxPoint& pos, const wxSize& size,
                            long style, const wxString& name)
{
    m_calendar = new wxHaikuCalendarView("wxCalendarCtrl", B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE | B_PULSE_NEEDED);
    m_calendar->SetHandler(this);

    float prefW, prefH;
    m_calendar->GetPreferredSize(&prefW, &prefH);

    if (!wxWindow::Create(parent, id, pos, size, style, name))
        return false;

    m_calendar->MoveTo(0, 0);
    m_calendar->ResizeTo(prefW, prefH);

    // Sync initial colours
    m_colHeaderFg = m_calendar->GetHeaderColourFg();
    m_colHeaderBg = m_calendar->GetHeaderColourBg();
    m_colHolidayFg = m_calendar->GetHolidayColourFg();
    m_colHolidayBg = m_calendar->GetHolidayColourBg();

    UpdateStyle();

    if (date.IsValid())
        SetDate(date);

    return true;
}

void wxCalendarCtrl::UpdateStyle()
{
    if (!m_calendar)
        return;

    m_calendar->SetStartOfWeek(WeekStartsOnMonday() ? B_WEEKDAY_MONDAY : B_WEEKDAY_SUNDAY);
    m_calendar->SetWeekNumberHeaderVisible(HasFlag(wxCAL_SHOW_WEEK_NUMBERS));
    m_calendar->SetShowHolidays(HasFlag(wxCAL_SHOW_HOLIDAYS));
    m_calendar->EnableMonthChange(HasFlag(wxCAL_SEQUENTIAL_MONTH_SELECTION) || !HasFlag(wxCAL_NO_MONTH_CHANGE));
}

bool wxCalendarCtrl::SetDate(const wxDateTime& date)
{
    if (!date.IsValid() || !m_calendar)
        return false;

    BDate bdate = wxHaikuConvertWxDate(date);
    if (!bdate.IsValid())
        return false;

    return m_calendar->SetDate(bdate);
}

wxDateTime wxCalendarCtrl::GetDate() const
{
    if (!m_calendar)
        return wxDefaultDateTime;

    return wxHaikuConvertDate(m_calendar->Date());
}

bool wxCalendarCtrl::SetDateRange(const wxDateTime& lower, const wxDateTime& upper)
{
    if (!m_calendar)
        return false;

    m_calendar->SetDateRange(lower, upper);
    return true;
}

bool wxCalendarCtrl::GetDateRange(wxDateTime *lower, wxDateTime *upper) const
{
    if (!m_calendar)
        return false;

    return m_calendar->GetDateRange(lower, upper);
}

bool wxCalendarCtrl::EnableMonthChange(bool enable)
{
    if (!wxCalendarCtrlBase::EnableMonthChange(enable))
        return false;

    if (m_calendar)
        m_calendar->EnableMonthChange(enable);

    return true;
}

void wxCalendarCtrl::Mark(size_t day, bool mark)
{
    wxCHECK_RET(day > 0 && day <= 31, "invalid day");

    if (!m_calendar)
        return;

    // Mark = bold text
    wxCalendarDateAttr attr = *GetAttr(day);
    attr.SetBold(mark);
    SetAttr(day, new wxCalendarDateAttr(attr));
}

void wxCalendarCtrl::SetHoliday(size_t day)
{
    wxCHECK_RET(day > 0 && day <= 31, "invalid day");

    if (!HasFlag(wxCAL_SHOW_HOLIDAYS) || !m_calendar)
        return;

    // For simplicity we just make it use holiday colour (already weekend logic exists)
    // You can extend DrawDay further if you need special holiday marks
    wxCalendarDateAttr attr = *GetAttr(day);
    attr.SetTextColour(m_colHolidayFg);
    attr.SetBackgroundColour(m_colHolidayBg);
    SetAttr(day, new wxCalendarDateAttr(attr));
}

void wxCalendarCtrl::SetHolidayColours(const wxColour& fg, const wxColour& bg)
{
    m_colHolidayFg = fg;
    m_colHolidayBg = bg;

    if (m_calendar)
        m_calendar->SetHolidayColours(fg, bg);
}

void wxCalendarCtrl::SetHeaderColours(const wxColour& fg, const wxColour& bg)
{
    m_colHeaderFg = fg;
    m_colHeaderBg = bg;

    if (m_calendar)
        m_calendar->SetHeaderColours(fg, bg);
}

wxCalendarDateAttr *wxCalendarCtrl::GetAttr(size_t day) const
{
    wxCHECK_MSG(day > 0 && day <= 31, nullptr, "invalid day");
    return m_attrs[day-1];
}

void wxCalendarCtrl::SetAttr(size_t day, wxCalendarDateAttr *attr)
{
    wxCHECK_RET(day > 0 && day <= 31, "invalid day");

    delete m_attrs[day-1];
    m_attrs[day-1] = attr;

    if (m_calendar)
        m_calendar->Invalidate();   // force redraw — could be optimized later
}

void wxCalendarCtrl::RefreshHolidays()
{
    if (m_calendar)
        m_calendar->Invalidate();
}

void wxCalendarCtrl::SetWindowStyleFlag(long style)
{
    wxCalendarCtrlBase::SetWindowStyleFlag(style);
    UpdateStyle();
}
*/
#endif // wxUSE_CALENDARCTRL
