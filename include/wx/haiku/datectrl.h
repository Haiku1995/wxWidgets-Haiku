#ifndef _WX_HAIKU_DATECTRL_H_
#define _WX_HAIKU_DATECTRL_H_

//#include "wx/defs.h"
//#include "wx/datetimectrl.h"
//#include "wx/datectrl.h" // Ensure wxDP_DEFAULT, wxDP_SHOWCENTURY, etc. are available

class BTextControl;
class BMessage;
class BView;
class BButton;
class BHandler;
class wxHaikuDatePickerHandler ;
namespace BPrivate { class BCalendarView; }


// ----------------------------------------------------------------------------
// wxDatePickerCtrl for Haiku
// ----------------------------------------------------------------------------

class WXDLLIMPEXP_ADV wxDatePickerCtrl : public wxDatePickerCtrlBase
{
public:
    // Constructors
   wxDatePickerCtrl() =default;
    wxDatePickerCtrl(wxWindow *parent, wxWindowID id, const wxDateTime& dt = wxDefaultDateTime,
                     const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize,
                     long style = wxDP_DEFAULT | wxDP_SHOWCENTURY,
                     const wxValidator& validator = wxDefaultValidator,
                     const wxString& name = wxDatePickerCtrlNameStr)
    {
        Create(parent, id, dt, pos, size, style, validator, name);
    }

    virtual ~wxDatePickerCtrl();

    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxDateTime& dt = wxDefaultDateTime,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = wxDP_DEFAULT | wxDP_SHOWCENTURY,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxDatePickerCtrlNameStr);

    virtual void SetValue(const wxDateTime& dt) override;
    virtual wxDateTime GetValue() const override;

    virtual void SetRange(const wxDateTime& dt1, const wxDateTime& dt2) override;
    virtual bool GetRange(wxDateTime *dt1, wxDateTime *dt2) const override;

    // Custom functions to handle the calendar popup
    void ShowCalendarPopup();
    void HideCalendarPopup();
    BTextControl* GetBTextControl() const;
    // Event handler for Haiku messages
    void HandleHaikuMessage(BMessage* msg);
  void OnTextChanged();
private:
    BTextControl* m_dateTextControl = nullptr;
    BButton* m_calendarButton = nullptr;
    BPrivate::BCalendarView* m_calendarView = nullptr;
    BView* m_containerView = nullptr;           // Moved up
    wxHaikuDatePickerHandler* m_handler = nullptr; // Moved down
    wxDateTime m_value;
    wxDateTime m_min{1, wxDateTime::Jan, 1900};
    wxDateTime m_max{31, wxDateTime::Dec, 2099};

    void OnCalendarDateSelected(BMessage* msg);
  

    wxDECLARE_DYNAMIC_CLASS_NO_COPY(wxDatePickerCtrl);
};

#endif // _WX_HAIKU_DATECTRL_H_
