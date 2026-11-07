#ifndef _WX_HAIKU_CHECKBOX_H_
#define _WX_HAIKU_CHECKBOX_H_

//#include "wx/checkbox.h"
#include <CheckBox.h>

class WXDLLIMPEXP_CORE wxCheckBox : public wxCheckBoxBase
{
public:
    wxCheckBox() : m_haikuCheckBox(nullptr) {}
    wxCheckBox(wxWindow* parent, wxWindowID id, const wxString& label,
               const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize,
               long style = 0, const wxValidator& validator = wxDefaultValidator,
               const wxString& name = wxASCII_STR(wxCheckBoxNameStr));

    

    virtual bool Create(wxWindow* parent, wxWindowID id, const wxString& label,
                        const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize,
                        long style = 0, const wxValidator& validator = wxDefaultValidator,
                        const wxString& name = wxASCII_STR(wxCheckBoxNameStr));

    virtual void SetValue(bool state) override;
    virtual bool GetValue() const override;
   
    virtual void SetLabel(const wxString& label) override;
    virtual wxString GetLabel() const override;
   
    BCheckBox* GetHaikuCheckBox() const { return m_haikuCheckBox; }
    bool HaikuHandleClick(BView* view, BMessage* event);
protected:
    virtual void DoSet3StateValue(wxCheckBoxState state) override;
    virtual wxCheckBoxState DoGet3StateValue() const override;

//private:
    BCheckBox* m_haikuCheckBox;

    wxDECLARE_DYNAMIC_CLASS(wxCheckBox);
};

#endif // _WX_HAIKU_CHECKBOX_H_
