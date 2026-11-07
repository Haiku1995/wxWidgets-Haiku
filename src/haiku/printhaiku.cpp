/////////////////////////////////////////////////////////////////////////////
// Native Haiku printing. Licence: wxWindows licence.
/////////////////////////////////////////////////////////////////////////////
#include "wx/wxprec.h"
#if wxUSE_PRINTING_ARCHITECTURE
#include "wx/print.h"
#include "wx/haiku/dcprint.h"
#include "wx/haiku/printdlg.h"
#include <memory>

wxHaikuPrinter::wxHaikuPrinter(wxPrintDialogData* data)
    : wxPrinterBase(data), m_printJob(nullptr), m_dc(nullptr) {}

bool wxHaikuPrinter::Setup(wxWindow* parent)
{
    wxPageSetupDialogData data(m_printDialogData.GetPrintData());
    wxHaikuPageSetupDialog dialog(parent, &data);
    if (dialog.ShowModal() != wxID_OK) return false;
    m_printDialogData.GetPrintData() = dialog.GetPageSetupDialogData().GetPrintData();
    return true;
}

wxDC* wxHaikuPrinter::PrintDialog(wxWindow* parent)
{
    wxHaikuPrintDialog dialog(parent, &m_printDialogData);
    if (dialog.ShowModal() != wxID_OK) { sm_lastError = wxPRINTER_CANCELLED; return nullptr; }
    m_printDialogData = dialog.GetPrintDialogData();
    return dialog.GetPrintDC();
}

bool wxHaikuPrinter::Print(wxWindow* parent, wxPrintout* printout, bool prompt)
{
    sm_lastError = wxPRINTER_ERROR;
    if (!printout) return false;
    std::unique_ptr<wxDC> dc(prompt ? PrintDialog(parent)
                                   : new wxPrinterDC(m_printDialogData.GetPrintData()));
    if (!dc || !dc->IsOk()) return false;
    auto* impl = static_cast<wxPrinterDCImpl*>(dc->GetImpl());
    printout->SetDC(dc.get());
    printout->SetPPIScreen(72,72);
    printout->SetPPIPrinter(dc->GetPPI());
    const wxSize pixels = dc->GetSize(), mm = dc->GetSizeMM();
    printout->SetPageSizePixels(pixels.x,pixels.y);
    printout->SetPageSizeMM(mm.x,mm.y);
    printout->SetPaperRectPixels(impl->GetPaperRect());
    printout->OnPreparePrinting();
    int minPage,maxPage,from,to;
    printout->GetPageInfo(&minPage,&maxPage,&from,&to);
    if (!m_printDialogData.GetAllPages())
    {
        from = wxMax(minPage,m_printDialogData.GetFromPage());
        to = wxMin(maxPage,m_printDialogData.GetToPage());
    }
    else { from=minPage;to=maxPage; }
    if (from < 1 || to < from)
    {
        printout->SetDC(nullptr); return false;
    }
    printout->OnBeginPrinting();
    bool ok = printout->OnBeginDocument(from,to);
    if (ok)
    {
        bool printed = false;
        for (int page=from; page<=to; ++page)
        {
            if (!printout->HasPage(page)) continue;
            dc->StartPage();
            ok = printout->OnPrintPage(page);
            dc->EndPage();
            printed = true;
            if (!ok || !dc->IsOk()) { ok=false; break; }
        }
        ok = ok && printed;
        if (!ok) impl->CancelDoc();
        printout->OnEndDocument();
    }
    else impl->CancelDoc();
    printout->OnEndPrinting();
    printout->SetDC(nullptr);
    sm_lastError = ok ? wxPRINTER_NO_ERROR : wxPRINTER_ERROR;
    return ok;
}

wxHaikuPrintPreview::wxHaikuPrintPreview(wxPrintout* printout,
    wxPrintout* printing, wxPrintDialogData* data)
    : wxPrintPreviewBase(printout, printing, data) {}
wxHaikuPrintPreview::wxHaikuPrintPreview(wxPrintout* printout,
    wxPrintout* printing, wxPrintData* data)
    : wxPrintPreviewBase(printout, printing, data) {}
bool wxHaikuPrintPreview::Print(bool interactive)
{
    wxHaikuPrinter printer(&m_printDialogData);
    return printer.Print(nullptr, GetPrintoutForPrinting(), interactive);
}
void wxHaikuPrintPreview::DetermineScaling() {}
#endif
