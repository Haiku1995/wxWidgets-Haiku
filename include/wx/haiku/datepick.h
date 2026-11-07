#ifndef WX_HAIKU_DATEPICK_H
#define WX_HAIKU_DATEPICK_H

#include "wx/datepickerctrl.h"
#include "wx/haiku/window.h"

class BCalendarView;

class wxDatePickerCtrlHaiku : public wxDatePickerCtrl
{
public:
    wxDatePickerCtrlHaiku();
    wxDatePickerCtrlHaiku(wxWindow* parent, wxWindowID id,
                          const wxDateTime& dt = wxDefaultDateTime,
                          const wxPoint& pos = wxDefaultPosition,
                          const wxSize& size = wxDefaultSize,
                          long style = wxDP_DEFAULT | wxDP_SHOWCENTURY,
                          const wxValidator& validator = wxDefaultValidator,
                          const wxString& name = wxDatePickerCtrlNameStr);
    virtual ~wxDatePickerCtrlHaiku();

    virtual bool Create(wxWindow* parent, wxWindowID id,
                       const wxDateTime& dt = wxDefaultDateTime,
                       const wxPoint& pos = wxDefaultPosition,
                       const wxSize& size = wxDefaultSize,
                       long style = wxDP_DEFAULT | wxDP_SHOWCENTURY,
                       const wxValidator& validator = wxDefaultValidator,
                       const wxString& name = wxDatePickerCtrlNameStr) override;
    virtual void SetValue(const wxDateTime& dt) override;
    virtual wxDateTime GetValue() const override;

    bool HaikuHandleDateChange(BView* view, BMessage* event);

private:
    BCalendarView* m_calendarView;

    wxDECLARE_DYNAMIC_CLASS(wxDatePickerCtrlHaiku);
};

#endif // WX_HAIKU_DATEPICK_H