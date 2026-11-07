/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/printdlg.cpp
// Author:      wxWidgets Haiku port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"
#include <algorithm>

#if wxUSE_PRINTING_ARCHITECTURE

#ifndef WX_PRECOMP
    #include "wx/cmndata.h"
    #include "wx/dcprint.h"
#endif

#include "wx/haiku/printdlg.h"

#include <PrintJob.h>
#include <Message.h>
#include "wx/paper.h"
#include <cmath>

// ----------------------------------------------------------------------------
// wxHaikuPrintNativeData
// ----------------------------------------------------------------------------

wxIMPLEMENT_CLASS(wxHaikuPrintNativeData, wxPrintNativeDataBase);

wxHaikuPrintNativeData::wxHaikuPrintNativeData() = default;
wxHaikuPrintNativeData::~wxHaikuPrintNativeData() = default;

bool wxHaikuPrintNativeData::TransferTo(wxPrintData& data)
{
    if ( !m_valid )
        return false;

    const char* printer = nullptr;
    if ( m_settings.FindString("printer_name", &printer) == B_OK && printer )
        data.SetPrinterName(wxString::FromUTF8(printer));

    int32 copies = 1, orientation = 0;
    if (m_settings.FindInt32("copies", &copies) == B_OK && copies > 0) data.SetNoCopies(copies);
    if (m_settings.FindInt32("orientation", &orientation) == B_OK)
        data.SetOrientation(orientation ? wxLANDSCAPE : wxPORTRAIT);

    BRect paper;
    if (m_settings.FindRect("paper_rect", &paper) == B_OK)
    {
        double width = paper.Width(), height = paper.Height();
        if (data.GetOrientation() == wxLANDSCAPE) std::swap(width, height);
        const wxSize size(static_cast<int>(std::lround(width * 25.4 / 72.0)),
                          static_cast<int>(std::lround(height * 25.4 / 72.0)));
        data.SetPaperSize(size);
        data.SetPaperId(wxPAPER_NONE);
        if (wxThePrintPaperDatabase)
        {
            if (auto* type = wxThePrintPaperDatabase->FindPaperType(wxSize(size.x * 10, size.y * 10)))
                data.SetPaperId(type->GetId());
        }
    }

    if ( data.GetOrientation() != wxPORTRAIT && data.GetOrientation() != wxLANDSCAPE )
        data.SetOrientation(wxPORTRAIT);

    return true;
}

bool wxHaikuPrintNativeData::TransferFrom(const wxPrintData& data)
{
    // The public legacy fields are also used by Haiku's libprint JobData.
    // Preserve all unrelated, driver-specific settings.
    m_settings.RemoveName("copies");
    m_settings.AddInt32("copies", std::max(1, data.GetNoCopies()));
    m_settings.RemoveName("orientation");
    m_settings.AddInt32("orientation", data.GetOrientation() == wxLANDSCAPE ? 1 : 0);

    const wxSize size = data.GetPaperSize();
    double widthMM = size.x, heightMM = size.y;
    if (wxThePrintPaperDatabase && data.GetPaperId() != wxPAPER_NONE)
        if (auto* type = wxThePrintPaperDatabase->FindPaperType(data.GetPaperId()))
        {
            widthMM = type->GetSize().x / 10.0;
            heightMM = type->GetSize().y / 10.0;
        }
    if (widthMM > 0 && heightMM > 0)
    {
        float width = widthMM * 72.0 / 25.4, height = heightMM * 72.0 / 25.4;
        if (data.GetOrientation() == wxLANDSCAPE) std::swap(width, height);
        BRect paper(0, 0, width, height), previous, printable;
        const bool haveMargins = m_settings.FindRect("paper_rect", &previous) == B_OK &&
                                 m_settings.FindRect("printable_rect", &printable) == B_OK;
        if (haveMargins)
        {
            const float right = previous.right - printable.right;
            const float bottom = previous.bottom - printable.bottom;
            printable.right = width - right; printable.bottom = height - bottom;
            printable = printable & paper;
        }
        // A new printer has not supplied hardware margins yet. ConfigPage/
        // ConfigJob will provide them; do not invent a printable area.
        m_settings.RemoveName("paper_rect"); m_settings.AddRect("paper_rect", paper);
        if (haveMargins)
        {
            m_settings.RemoveName("printable_rect"); m_settings.AddRect("printable_rect", printable);
        }
    }

    if ( !data.GetPrinterName().empty() )
    {
        m_settings.RemoveName("printer_name");
        m_settings.AddString("printer_name", data.GetPrinterName().utf8_str());
    }

    m_valid = m_settings.HasRect("paper_rect") && m_settings.HasRect("printable_rect");
    return true;
}

// ----------------------------------------------------------------------------
// wxHaikuPrintDialog
// ----------------------------------------------------------------------------

wxIMPLEMENT_CLASS(wxHaikuPrintDialog, wxPrintDialogBase);

wxHaikuPrintDialog::wxHaikuPrintDialog(wxWindow* parent, wxPrintDialogData* data)
    : wxPrintDialogBase(parent, wxID_ANY, _("Print")),
      m_printDialogData(data ? *data : wxPrintDialogData())
{
}

wxHaikuPrintDialog::wxHaikuPrintDialog(wxWindow* parent, wxPrintData* data)
    : wxPrintDialogBase(parent, wxID_ANY, _("Print")),
      m_printDialogData(data ? wxPrintDialogData(*data) : wxPrintDialogData())
{
}

wxHaikuPrintDialog::~wxHaikuPrintDialog()
{
    delete m_dc;
    delete m_printJob;
}

int wxHaikuPrintDialog::ShowModal()
{
    wxPrintData& pd = m_printDialogData.GetPrintData();

    // Use a stack native wrapper (this wxWidgets branch doesn't expose
    // wxPrintData::SetNativeData()).
    auto& native = *static_cast<wxHaikuPrintNativeData*>(pd.GetNativeData());
    native.TransferFrom(pd);

    if ( !m_printJob )
        m_printJob = new BPrintJob("wxWidgets Print Job");

    // Pass pointer that remains valid during ConfigJob().
    BMessage& settings = native.GetSettings();
    if (native.IsOk()) m_printJob->SetSettings(new BMessage(settings));

    if ( m_printJob->ConfigJob() != B_OK )
        return wxID_CANCEL;

    if ( BMessage* newSettings = m_printJob->Settings() )
    {
        native.SetSettings(*newSettings);
        delete newSettings;
    }

    native.TransferTo(pd);
    int32 first = 1, last = 0;
    const BMessage& accepted = native.GetSettings();
    if (accepted.FindInt32("first_page", &first) == B_OK &&
        accepted.FindInt32("last_page", &last) == B_OK && last >= first && last < INT_MAX)
    {
        m_printDialogData.SetAllPages(false);
        m_printDialogData.SetFromPage(first);
        m_printDialogData.SetToPage(last);
    }

    // Create/replace DC for the caller.
    delete m_dc;
    m_dc = new wxPrinterDC(pd);

    return m_dc && m_dc->IsOk() ? wxID_OK : wxID_CANCEL;
}

wxDC* wxHaikuPrintDialog::GetPrintDC()
{
    wxDC* dc = m_dc;
    m_dc = nullptr;
    return dc;
}

// ----------------------------------------------------------------------------
// wxHaikuPageSetupDialog
// ----------------------------------------------------------------------------

wxIMPLEMENT_CLASS(wxHaikuPageSetupDialog, wxPageSetupDialogBase);

wxHaikuPageSetupDialog::wxHaikuPageSetupDialog(wxWindow* parent, wxPageSetupDialogData* data)
    : wxPageSetupDialogBase(parent, wxID_ANY, _("Page Setup")),
      m_pageData(data ? *data : wxPageSetupDialogData())
{
}

wxHaikuPageSetupDialog::~wxHaikuPageSetupDialog()
{
    delete m_printJob;
}

int wxHaikuPageSetupDialog::ShowModal()
{
    wxPrintData& pd = m_pageData.GetPrintData();

    auto& native = *static_cast<wxHaikuPrintNativeData*>(pd.GetNativeData());
    native.TransferFrom(pd);

    if ( !m_printJob )
        m_printJob = new BPrintJob("wxWidgets Page Setup");

    BMessage& settings = native.GetSettings();
    if (native.IsOk()) m_printJob->SetSettings(new BMessage(settings));

    if ( m_printJob->ConfigPage() != B_OK )
        return wxID_CANCEL;

    if ( BMessage* newSettings = m_printJob->Settings() )
    {
        native.SetSettings(*newSettings);
        delete newSettings;
    }

    native.TransferTo(pd);

    m_pageData.SetPaperSize(pd.GetPaperSize());
    BRect paper, printable;
    if (settings.FindRect("paper_rect", &paper) == B_OK &&
        settings.FindRect("printable_rect", &printable) == B_OK)
    {
        const auto mm = [](float points) { return static_cast<int>(std::lround(points * 25.4 / 72.0)); };
        m_pageData.SetMarginTopLeft(wxPoint(mm(printable.left - paper.left), mm(printable.top - paper.top)));
        m_pageData.SetMarginBottomRight(wxPoint(mm(paper.right - printable.right), mm(paper.bottom - printable.bottom)));
    }
    return wxID_OK;
}

#endif // wxUSE_PRINTING_ARCHITECTURE
