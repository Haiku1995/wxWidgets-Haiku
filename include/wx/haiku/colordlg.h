#ifndef _WX_HAIKU_COLORDLG_H_
#define _WX_HAIKU_COLORDLG_H_

#include "wx/defs.h"
#include "wx/dialog.h"
#include "wx/colourdata.h"

class BColorControl;
class wxPanel;
class wxButton;
class wxHaikuColourControl;

class WXDLLIMPEXP_CORE wxColourDialog : public wxDialog
{
public:
    wxColourDialog()
        : m_bColorControl(nullptr),
          m_previewHost(nullptr),
          m_btnOK(nullptr),
          m_btnCancel(nullptr)
    {
    }

    wxColourDialog(wxWindow* parent, const wxColourData* data = nullptr)
        : m_bColorControl(nullptr),
          m_previewHost(nullptr),
          m_btnOK(nullptr),
          m_btnCancel(nullptr)
    {
        Create(parent, data);
    }

    bool Create(wxWindow* parent, const wxColourData* data = nullptr);
    virtual ~wxColourDialog();

    virtual int ShowModal() override;

    wxColourData& GetColourData();

private:
    void OnColorChanged();
    void OnOK(wxCommandEvent& event);
    void OnCancel(wxCommandEvent& event);
    void OnInitDialog(wxInitDialogEvent& event);
    void SyncNativeColorFromData();
    void SyncDataFromNative();

    BColorControl* m_bColorControl;
    wxPanel*       m_previewHost;
    wxButton*      m_btnOK;
    wxButton*      m_btnCancel;
    wxColourData   m_data;

    friend class wxHaikuColourControl;

    wxDECLARE_DYNAMIC_CLASS(wxColourDialog);
    wxDECLARE_EVENT_TABLE();
};

#endif // _WX_HAIKU_COLORDLG_H_
