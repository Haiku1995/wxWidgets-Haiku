// include/wx/haiku/button.h
#ifndef _WX_HAIKU_BUTTON_H_
#define _WX_HAIKU_BUTTON_H_

//#include "wx/button.h"  // pulls in wxButtonBase
#include <Button.h>

class WXDLLIMPEXP_CORE wxButton : public wxButtonBase
{
public:
    wxButton() = default;

    wxButton(wxWindow* parent,
             wxWindowID id,
             const wxString& label = wxEmptyString,
             const wxPoint& pos = wxDefaultPosition,
             const wxSize& size = wxDefaultSize,
             long style = 0,
             const wxValidator& validator = wxDefaultValidator,
             const wxString& name = wxASCII_STR(wxButtonNameStr));

    bool Create(wxWindow* parent,
                wxWindowID id,
                const wxString& label = wxEmptyString,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = 0,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxButtonNameStr));

    virtual wxWindow* SetDefault() override;
    void SetLabel(const wxString& label) override;
    int HaikuGetEventType() const override { return wxEVT_BUTTON; }

protected:
    virtual wxSize DoGetBestSize() const override;
    virtual bool DoGetAuthNeeded() const override;
    virtual void DoSetAuthNeeded(bool show) override;

private:
    BButton* m_bButton = nullptr;

    wxDECLARE_DYNAMIC_CLASS_NO_COPY(wxButton);
};

#endif // _WX_HAIKU_BUTTON_H_
