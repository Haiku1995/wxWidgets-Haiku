/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/dc.h
// Purpose:     wxDC implementation for Haiku
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_DC_H_
#define _WX_HAIKU_DC_H_

#include "wx/dc.h"
#include "wx/bitmap.h"
#include "wx/brush.h"
#include "wx/font.h"
#include "wx/icon.h"
#include "wx/palette.h"
#include "wx/pen.h"
#include "wx/region.h"
#include "wx/gdicmn.h"

#include <GraphicsDefs.h>
#include <Font.h>

class BView;
class BBitmap;

// ----------------------------------------------------------------------------
// wxHaikuDCImpl
// ----------------------------------------------------------------------------

class WXDLLIMPEXP_CORE wxHaikuDCImpl : public wxDCImpl
{
public:
    explicit wxHaikuDCImpl(wxDC* owner);
    ~wxHaikuDCImpl() override;

    bool CanDrawBitmap() const override;
    bool CanGetTextExtent() const override;

    void DoGetSize(int* width, int* height) const override;
    void DoGetSizeMM(int* width, int* height) const override;

    int GetDepth() const override;
    wxSize GetPPI() const override;

    void SetFont(const wxFont& font) override;
    void SetPen(const wxPen& pen) override;
    void SetBrush(const wxBrush& brush) override;
    void SetBackground(const wxBrush& brush) override;
    void SetBackgroundMode(int mode) override;

#if wxUSE_PALETTE
    void SetPalette(const wxPalette& palette) override;
#endif

    wxPoint DeviceToLogical(wxCoord x, wxCoord y) const override;
    wxPoint LogicalToDevice(wxCoord x, wxCoord y) const override;
    wxSize DeviceToLogicalRel(int x, int y) const override;
    wxSize LogicalToDeviceRel(int x, int y) const override;

    void SetLogicalFunction(wxRasterOperationMode function) override;

    wxCoord GetCharHeight() const override;
    wxCoord GetCharWidth() const override;

    bool DoGetPartialTextExtents(const wxString& text, wxArrayInt& widths) const override;
    void DoGetTextExtent(const wxString& string,
                         wxCoord* x,
                         wxCoord* y,
                         wxCoord* descent = nullptr,
                         wxCoord* externalLeading = nullptr,
                         const wxFont* font = nullptr) const override;

    void Clear() override;

    bool DoGetClippingRect(wxRect& rect) const override;
    void DoSetClippingRegion(wxCoord x,
                             wxCoord y,
                             wxCoord width,
                             wxCoord height) override;
    void DoSetDeviceClippingRegion(const wxRegion& region) override;
    void DestroyClippingRegion() override;

    bool DoFloodFill(wxCoord x,
                     wxCoord y,
                     const wxColour& col,
                     wxFloodFillStyle style = wxFLOOD_SURFACE) override;

    bool DoGetPixel(wxCoord x, wxCoord y, wxColour* col) const override;

    void DoDrawPoint(wxCoord x, wxCoord y) override;
    void DoDrawLine(wxCoord x1, wxCoord y1, wxCoord x2, wxCoord y2) override;

    void DoDrawArc(wxCoord x1,
                   wxCoord y1,
                   wxCoord x2,
                   wxCoord y2,
                   wxCoord xc,
                   wxCoord yc) override;

    void DoDrawEllipticArc(wxCoord x,
                           wxCoord y,
                           wxCoord w,
                           wxCoord h,
                           double sa,
                           double ea) override;

    void DoDrawRectangle(wxCoord x,
                         wxCoord y,
                         wxCoord width,
                         wxCoord height) override;

    void DoDrawRoundedRectangle(wxCoord x,
                                wxCoord y,
                                wxCoord width,
                                wxCoord height,
                                double radius) override;

    void DoDrawEllipse(wxCoord x,
                       wxCoord y,
                       wxCoord width,
                       wxCoord height) override;

    void DoCrossHair(wxCoord x, wxCoord y) override;

    void DoDrawIcon(const wxIcon& icon, wxCoord x, wxCoord y) override;

    void DoDrawBitmap(const wxBitmap& bmp,
                      wxCoord x,
                      wxCoord y,
                      bool useMask = false) override;

    void DoDrawText(const wxString& text, wxCoord x, wxCoord y) override;

    void DoDrawRotatedText(const wxString& text,
                           wxCoord x,
                           wxCoord y,
                           double angle) override;

    bool DoBlit(wxCoord xdest,
                wxCoord ydest,
                wxCoord width,
                wxCoord height,
                wxDC* source,
                wxCoord xsrc,
                wxCoord ysrc,
                wxRasterOperationMode rop = wxCOPY,
                bool useMask = false,
                wxCoord xsrcMask = wxDefaultCoord,
                wxCoord ysrcMask = wxDefaultCoord) override;

    void DoDrawLines(int n,
                     const wxPoint points[],
                     wxCoord xoffset,
                     wxCoord yoffset) override;

    void DoDrawPolygon(int n,
                       const wxPoint points[],
                       wxCoord xoffset,
                       wxCoord yoffset,
                       wxPolygonFillMode fillStyle = wxODDEVEN_RULE) override;

    void DoDrawPolyPolygon(int n,
                           const int count[],
                           const wxPoint points[],
                           wxCoord xoffset,
                           wxCoord yoffset,
                           wxPolygonFillMode fillStyle) override;

    void ComputeScaleAndOrigin() override;

    void* GetHandle() const override { return m_view; }

protected:
    BView*   m_view = nullptr;
    BBitmap* m_bitmap = nullptr;

    BFont m_bfont;

    int m_clipDepth = 0;

    wxPen    m_pen;
    wxBrush  m_brush;
    wxBrush  m_backgroundBrush;
    wxFont   m_font;
    wxRegion m_clippingRegion;

    rgb_color m_penColor {};
    rgb_color m_brushColor {};

    bool m_isClipBoxValid = false;
    int  m_backgroundMode = wxBRUSHSTYLE_TRANSPARENT;
    wxRasterOperationMode m_logicalFunction = wxCOPY;
    wxSize m_ppi = wxSize(96, 96);

private:
    wxDECLARE_CLASS(wxHaikuDCImpl);
    wxDECLARE_NO_COPY_CLASS(wxHaikuDCImpl);
};

#endif // _WX_HAIKU_DC_H_
