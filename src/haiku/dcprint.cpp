/////////////////////////////////////////////////////////////////////////////
// wxPrinterDCImpl for Haiku. Licence: wxWindows licence.
/////////////////////////////////////////////////////////////////////////////
#include "wx/wxprec.h"
#if wxUSE_PRINTING_ARCHITECTURE
#include "wx/haiku/dcprint.h"
#include "wx/haiku/printdlg.h"
#include <Bitmap.h>
#include <PrintJob.h>
#include <View.h>
#include <cmath>

class wxHaikuRasterPrintView : public BView
{
public:
    explicit wxHaikuRasterPrintView(BRect rect)
        : BView(rect, "wxPrinterView", B_FOLLOW_NONE, B_WILL_DRAW) {}
    void Draw(BRect) override { if (bitmap) DrawBitmap(bitmap, bitmap->Bounds(), Bounds()); }
    BBitmap* bitmap = nullptr;
    bool pageActive = false;
};

wxIMPLEMENT_ABSTRACT_CLASS(wxPrinterDCImpl, wxHaikuDCImpl);

wxPrinterDCImpl::wxPrinterDCImpl(wxPrinterDC* owner, const wxPrintData& data)
    : wxHaikuDCImpl(owner), m_printData(data)
{
    m_ok = false;
    auto* native = static_cast<wxHaikuPrintNativeData*>(m_printData.GetNativeData());
    native->TransferFrom(m_printData);
    if (!native->IsOk()) return;
    m_printJob = new BPrintJob("wxWidgets Document");
    m_printJob->SetSettings(new BMessage(native->GetSettings()));
    m_paperRect = m_printJob->PaperRect();
    const BRect printable = m_printJob->PrintableRect();
    if (!printable.IsValid() || !std::isfinite(printable.Width()) ||
        !std::isfinite(printable.Height()) || printable.Width() > 20000 ||
        printable.Height() > 20000) return;
    BRect bounds(0, 0, printable.Width(), printable.Height());
    auto* bitmap = new BBitmap(bounds, B_BITMAP_ACCEPTS_VIEWS, B_RGB32);
    if (bitmap->InitCheck() != B_OK) { delete bitmap; return; }
    auto* view = new wxHaikuRasterPrintView(bounds);
    view->bitmap = bitmap;
    bitmap->AddChild(view);
    m_printView = m_view = view;
    m_resolution = 72; // Native page rectangles use points.
    m_ok = true;
}
wxPrinterDCImpl::~wxPrinterDCImpl()
{
    if (m_printJob) m_printJob->CancelJob();
    auto* view = static_cast<wxHaikuRasterPrintView*>(m_printView);
    m_view = nullptr;
    if (view)
    {
        if (view->pageActive && view->LockLooper())
        {
            view->Sync();
            view->pageActive = false;
            view->UnlockLooper();
        }
        delete view->bitmap;
    }
    m_printView = nullptr;
    delete m_printJob;
}
bool wxPrinterDCImpl::StartDoc(const wxString&)
{
    if (!m_ok || !m_printJob) return false;
    m_printJob->BeginJob();
    m_ok = m_printJob->CanContinue();
    return m_ok;
}
void wxPrinterDCImpl::EndDoc()
{
    if (!m_ok || !m_printJob) return;
    m_printJob->CommitJob();
    m_ok = false;
}
void wxPrinterDCImpl::CancelDoc()
{
    if (m_printJob) m_printJob->CancelJob();
    m_ok = false;
}
void wxPrinterDCImpl::StartPage()
{
    auto* view = static_cast<wxHaikuRasterPrintView*>(m_printView);
    if (!m_ok || !view || view->pageActive || !view->LockLooper()) return;
    view->PushState();
    view->SetDrawingMode(B_OP_COPY);
    view->SetHighColor(255,255,255,255);
    view->FillRect(view->Bounds());
    view->PopState();
    view->pageActive = true;
    view->UnlockLooper();
}
void wxPrinterDCImpl::EndPage()
{
    auto* view = static_cast<wxHaikuRasterPrintView*>(m_printView);
    if (!m_ok || !view || !view->pageActive || !view->LockLooper()) return;
    view->Sync();
    view->pageActive = false;
    view->UnlockLooper();
    m_printJob->DrawView(view, view->bitmap->Bounds(), BPoint(0, 0));
    m_printJob->SpoolPage();
    m_ok = m_printJob->CanContinue();
}
wxRect wxPrinterDCImpl::GetPaperRect() const
{
    const BRect printable = m_printJob ? m_printJob->PrintableRect() : BRect();
    return wxRect(std::lround(m_paperRect.left - printable.left),
                  std::lround(m_paperRect.top - printable.top),
                  std::lround(m_paperRect.Width()+1), std::lround(m_paperRect.Height()+1));
}
int wxPrinterDCImpl::GetResolution() const { return m_resolution; }
wxSize wxPrinterDCImpl::GetPPI() const { return wxSize(m_resolution, m_resolution); }
void wxPrinterDCImpl::DoGetSize(int* width, int* height) const
{
    if (width) *width = m_printView ? std::lround(static_cast<wxHaikuRasterPrintView*>(m_printView)->bitmap->Bounds().Width()+1) : 0;
    if (height) *height = m_printView ? std::lround(static_cast<wxHaikuRasterPrintView*>(m_printView)->bitmap->Bounds().Height()+1) : 0;
}
void wxPrinterDCImpl::DoGetSizeMM(int* width, int* height) const
{
    int w,h; DoGetSize(&w,&h);
    if (width) *width = std::lround(w*25.4/72);
    if (height) *height = std::lround(h*25.4/72);
}
#endif
