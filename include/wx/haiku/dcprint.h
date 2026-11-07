/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/dcprint.h
// Purpose:     wxPrinterDCImpl for Haiku
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_DCPRINT_H_
#define _WX_HAIKU_DCPRINT_H_

#include "wx/dcprint.h"
#include "wx/cmndata.h"
#include "wx/haiku/dc.h"

#include <Rect.h>

class BPrintJob;
class BView;

class WXDLLIMPEXP_CORE wxPrinterDCImpl : public wxHaikuDCImpl
{
public:
    wxPrinterDCImpl(wxPrinterDC* owner, const wxPrintData& data);
    ~wxPrinterDCImpl() override;

    bool StartDoc(const wxString& message) override;
    void EndDoc() override;
    void CancelDoc();
    void StartPage() override;
    void EndPage() override;

    wxRect GetPaperRect() const override;
    int GetResolution() const override;
    wxSize GetPPI() const override;

    void DoGetSize(int* width, int* height) const override;
    void DoGetSizeMM(int* width, int* height) const override;

protected:
    wxPrintData m_printData;

    BPrintJob* m_printJob = nullptr;
    BView*     m_printView = nullptr;

    BRect m_paperRect;
    int   m_resolution = 300;

private:
    wxDECLARE_CLASS(wxPrinterDCImpl);
    wxDECLARE_NO_COPY_CLASS(wxPrinterDCImpl);
};

#endif // _WX_HAIKU_DCPRINT_H_
