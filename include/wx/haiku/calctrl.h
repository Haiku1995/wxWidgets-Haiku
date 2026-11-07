/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/calctrl.h
// Purpose:     wxCalendarCtrl for wxHaiku
// Author:      (based on wxQt port and Haiku BCalendarView)
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_CALCTRL_H_
#define _WX_HAIKU_CALCTRL_H_

#include "wx/calctrl.h"
#include <private/shared/CalendarView.h> // Haiku BCalendarView

class BCalendarView;
class wxHaikuCalendarView;

class WXDLLIMPEXP_ADV wxCalendarCtrl : public wxCalendarCtrlBase
{
public:
    wxCalendarCtrl() { Init(); }
    wxCalendarCtrl(wxWindow *parent,
                   wxWindowID id,
                   const wxDateTime& date = wxDefaultDateTime,
                   const wxPoint& pos = wxDefaultPosition,
                   const wxSize& size = wxDefaultSize,
                   long style = wxCAL_SHOW_HOLIDAYS,
                   const wxString& name = wxASCII_STR(wxCalendarNameStr))
    {
        Init();
        Create(parent, id, date, pos, size, style, name);
    }

    virtual ~wxCalendarCtrl();

    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxDateTime& date = wxDefaultDateTime,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = wxCAL_SHOW_HOLIDAYS,
                const wxString& name = wxASCII_STR(wxCalendarNameStr));

    // calendar operations
    virtual bool SetDate(const wxDateTime& date) override;
    virtual wxDateTime GetDate() const override;

    virtual bool SetDateRange(const wxDateTime& lowerdate = wxDefaultDateTime,
                              const wxDateTime& upperdate = wxDefaultDateTime) override;
    virtual bool GetDateRange(wxDateTime *lowerdate, wxDateTime *upperdate) const override;

    virtual bool EnableMonthChange(bool enable = true) override;

    virtual void Mark(size_t day, bool mark) override;

    // holidays colours
    virtual void SetHoliday(size_t day) override;
    virtual void SetHolidayColours(const wxColour& colFg, const wxColour& colBg) override;
    virtual const wxColour& GetHolidayColourFg() const override { return m_colHolidayFg; }
    virtual const wxColour& GetHolidayColourBg() const override { return m_colHolidayBg; }

    // header colours
    virtual void SetHeaderColours(const wxColour& colFg, const wxColour& colBg) override;
    virtual const wxColour& GetHeaderColourFg() const override { return m_colHeaderFg; }
    virtual const wxColour& GetHeaderColourBg() const override { return m_colHeaderBg; }

    // day attributes
    virtual wxCalendarDateAttr *GetAttr(size_t day) const override;
    virtual void SetAttr(size_t day, wxCalendarDateAttr *attr) override;
    virtual void ResetAttr(size_t day) override { SetAttr(day, nullptr); }

    virtual void SetWindowStyleFlag(long style) override;

    // internal
    wxHaikuCalendarView* GetHaikuCalendarView() const { return m_calendar; }

protected:
    virtual void RefreshHolidays() override;

private:
    void Init();
    void UpdateStyle();

    wxHaikuCalendarView* m_calendar;

    wxColour m_colHeaderFg,
             m_colHeaderBg,
             m_colHolidayFg,
             m_colHolidayBg;

    wxCalendarDateAttr *m_attrs[31];

    wxDECLARE_DYNAMIC_CLASS(wxCalendarCtrl);
};

#endif // _WX_HAIKU_CALCTRL_H_
