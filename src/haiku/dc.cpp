/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/dc.cpp
// Purpose:     wxDCImpl implementation for Haiku using BView
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#ifndef WX_PRECOMP
    #include "wx/dc.h"
    #include "wx/bitmap.h"
    #include "wx/font.h"
    #include "wx/region.h"
    #include "wx/icon.h"
#endif

#include "wx/haiku/dc.h"
#include "wx/dcmemory.h"
#include "wx/arrstr.h"
#include "wx/log.h"

#include <InterfaceKit.h>
#include <GraphicsDefs.h>
#include <interface/Polygon.h>
#include <interface/View.h>
#include <interface/AffineTransform.h>
#include <interface/Shape.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>
#include <memory>

void wxMissingImplementation(const char fileName[], unsigned lineNumber,
    const char feature[]);

#define wxMISSING_IMPLEMENTATION(feature) \
    wxMissingImplementation(__FILE__, __LINE__, feature)

#define wxMISSING_FUNCTION() \
    wxMISSING_IMPLEMENTATION(__func__)

#if defined(WX_DEBUG_HAIKU) && WX_DEBUG_HAIKU
    #define HAIKU_LOG(msg, ...) \
        wxLogDebug(wxS("[HAIKU-DC:%s:%d] ") msg, __func__, __LINE__, ##__VA_ARGS__)
    #define HAIKU_LOG_RAW(msg, ...) \
        wxLogDebug(wxS("[HAIKU-DC:%s:%d] ") msg, __func__, __LINE__, ##__VA_ARGS__)
#else
    #define HAIKU_LOG(msg, ...) ((void)0)
    #define HAIKU_LOG_RAW(msg, ...) ((void)0)
#endif


struct HaikuViewLock
{
    HaikuViewLock(BView* view)
        : m_window(view ? view->Window() : nullptr),
          m_locked(m_window && !m_window->IsLocked() && m_window->LockLooper())
    {
        //HAIKU_LOG(wxS("HaikuViewLock ctor view= window= locked="));//
      //    view, m_window, m_locked);
    }

    ~HaikuViewLock()
    {
        if (m_locked)
        {
         //  HAIKU_LOG(wxS("HaikuViewLock dtor unlock window=%p"), m_window);

            m_window->UnlockLooper();
        }
    }

    bool ok() const { return !m_window || m_locked || m_window->IsLocked(); }

    BWindow* m_window;
    bool     m_locked;
};

static void HaikuDumpView(const char* label, BView* v)
{
#if defined(WX_DEBUG_HAIKU) && WX_DEBUG_HAIKU
    if (!v) return;

    // FIX: You MUST lock before calling Frame() or Bounds()
    HaikuViewLock lock(v);
    if (!lock.ok()) {
        wxLogDebug(wxS("%s: [VIEW LOCKED - CANNOT DUMP]"), label);
        return;
    }

    const BRect f = v->Frame();
    const BRect b = v->Bounds();

    wxLogDebug(
        wxS("%s: ptr=%p name='%s' hidden=%d parent=%p window=%p "
            "frame=(%.1f,%.1f,%.1f,%.1f) size=(%.1f x %.1f) "
            "bounds=(%.1f,%.1f,%.1f,%.1f) boundsSize=(%.1f x %.1f)"),
        label,
        v,
        v->Name() ? v->Name() : "",
        v->IsHidden(),
        v->Parent(),
        v->Window(),
        f.left, f.top, f.right, f.bottom,
        f.Width() + 1.0f, f.Height() + 1.0f,
        b.left, b.top, b.right, b.bottom,
        b.Width() + 1.0f, b.Height() + 1.0f
    );
#else
    wxUnusedVar(label);
    wxUnusedVar(v);
#endif
}

static inline bool wxHaikuShouldStroke(const wxPen& pen)
{
    return pen.IsOk() &&
           pen.GetStyle() != wxPENSTYLE_TRANSPARENT;
}

static inline bool wxHaikuShouldFill(const wxBrush& brush)
{
    return brush.IsOk() &&
           brush.GetStyle() != wxBRUSHSTYLE_TRANSPARENT;
}

static inline float wxHaikuPenSize(const wxPen& pen)
{
    const int w = pen.GetWidth();
    return w > 0 ? (float)w : 1.0f;
}

static inline float wxAngleDegFromCenter(wxCoord xc, wxCoord yc,
                                         wxCoord x,  wxCoord y)
{
    const double dx = double(x) - double(xc);
    const double dy = -(double(y) - double(yc));
    double a = atan2(dy, dx) * 180.0 / M_PI;
    if (a < 0.0) a += 360.0;
    return (float)a;
}

// ----------------------------------------------------------------------------
// helpers
// ----------------------------------------------------------------------------

static inline int32_t wxFillModeToHaiku(wxPolygonFillMode fillStyle)
{
    return (fillStyle == wxWINDING_RULE) ? B_NONZERO : B_EVEN_ODD;
}

static inline rgb_color wxColourToRGB(const wxColour& c)
{
    return { (uint8)c.Red(), (uint8)c.Green(), (uint8)c.Blue(), (uint8)c.Alpha() };
}

static uint8 wxHaikuRasterByte(wxRasterOperationMode op, uint8 src, uint8 dst)
{
    switch (op)
    {
        case wxCLEAR: return 0;
        case wxXOR: return src ^ dst;
        case wxINVERT: return ~dst;
        case wxOR_REVERSE: return src | ~dst;
        case wxAND_REVERSE: return src & ~dst;
        case wxCOPY: return src;
        case wxAND: return src & dst;
        case wxAND_INVERT: return ~src & dst;
        case wxNO_OP: return dst;
        case wxNOR: return ~src & ~dst;
        case wxEQUIV: return ~src ^ dst;
        case wxSRC_INVERT: return ~src;
        case wxOR_INVERT: return ~src | dst;
        case wxNAND: return ~src | ~dst;
        case wxOR: return src | dst;
        case wxSET: return 255;
    }
    return dst;
}

// Render the source and coverage independently of the destination, then combine
// RGB bytes. Haiku's composition XOR is not wxDC's bitwise XOR.
// The caller holds the destination looper lock for the entire operation.
class wxHaikuRasterOperation
{
public:
    wxHaikuRasterOperation(BView*& view, BBitmap* destination,
                          wxRasterOperationMode operation)
        : m_slot(view), m_original(view), m_destination(destination), m_operation(operation)
    {
        if (operation == wxNO_OP || !view)
            return;
        if (operation == wxCOPY || (!destination && operation == wxINVERT))
        {
            m_ok = true;
            return;
        }
        if (!destination || (destination->ColorSpace() != B_RGB32
                             && destination->ColorSpace() != B_RGBA32))
        {
            wxLogDebug("This raster operation requires a 32-bit bitmap-backed Haiku DC");
            return;
        }
        m_original->Sync();
        m_scratch.reset(new BBitmap(destination->Bounds(), B_RGBA32, true));
        if (!m_scratch->IsValid())
            return;
        std::memset(m_scratch->Bits(), 0, m_scratch->BitsLength());
        m_render = new BView(destination->Bounds(), "wxRasterSource", B_FOLLOW_NONE, B_WILL_DRAW);
        m_scratch->AddChild(m_render);
        if (!m_render->LockLooper())
            return;
        m_locked = true;
        BRegion clip;
        m_original->GetClippingRegion(&clip);
        m_render->ConstrainClippingRegion(&clip);
        m_render->SetTransform(m_original->Transform());
        BFont font;
        m_original->GetFont(&font);
        m_render->SetFont(&font);
        m_render->SetDrawingMode(B_OP_COPY);
        m_render->SetHighColor(m_original->HighColor());
        m_render->SetLowColor(m_original->LowColor());
        m_render->SetLineMode(m_original->LineCapMode(), m_original->LineJoinMode());
        m_render->SetFillRule(m_original->FillRule());
        m_slot = m_render;
        m_ok = true;
    }

    ~wxHaikuRasterOperation()
    {
        if (IsActive())
        {
            m_render->Sync();
            const int width = m_destination->Bounds().IntegerWidth() + 1;
            const int height = m_destination->Bounds().IntegerHeight() + 1;
            for (int y = 0; y < height; ++y)
            {
                const auto* src = static_cast<const uint8*>(m_scratch->Bits())
                    + y * m_scratch->BytesPerRow();
                auto* dst = static_cast<uint8*>(m_destination->Bits())
                    + y * m_destination->BytesPerRow();
                for (int x = 0; x < width; ++x, src += 4, dst += 4)
                {
                    if (!src[3])
                        continue;
                    for (int c = 0; c < 3; ++c)
                        dst[c] = wxHaikuRasterByte(m_operation, src[c], dst[c]);
                }
            }
        }
        m_slot = m_original;
        if (m_locked)
            m_render->UnlockLooper();
    }

    bool IsOk() const { return m_ok; }
    bool IsActive() const { return m_ok && m_render; }

private:
    BView*& m_slot;
    BView* m_original;
    BBitmap* m_destination;
    wxRasterOperationMode m_operation;
    std::unique_ptr<BBitmap> m_scratch;
    BView* m_render = nullptr;
    bool m_locked = false;
    bool m_ok = false;
};

static std::vector<double> wxHaikuDashes(const wxPen& pen)
{
    std::vector<double> lengths;
    switch (pen.GetStyle())
    {
        case wxPENSTYLE_DOT: lengths = {1, 2}; break;
        case wxPENSTYLE_SHORT_DASH: lengths = {3, 2}; break;
        case wxPENSTYLE_LONG_DASH: lengths = {6, 2}; break;
        case wxPENSTYLE_DOT_DASH: lengths = {6, 2, 1, 2}; break;
        case wxPENSTYLE_USER_DASH:
        {
            wxDash* dashes = nullptr;
            const int count = pen.GetDashes(&dashes);
            for (int i = 0; dashes && i < count; ++i)
                lengths.push_back(std::max(1, static_cast<int>(dashes[i])));
            if (lengths.size() % 2)
            {
                const auto copy = lengths;
                lengths.insert(lengths.end(), copy.begin(), copy.end());
            }
            break;
        }
        default: break;
    }
    for (double& length : lengths)
        length *= wxHaikuPenSize(pen);
    return lengths;
}

static void wxHaikuStrokePath(BView* view, const wxPen& pen,
                             const std::vector<BPoint>& points, bool closed)
{
    if (points.empty())
        return;
    const cap_mode cap = pen.GetCap() == wxCAP_BUTT ? B_BUTT_CAP
        : pen.GetCap() == wxCAP_PROJECTING ? B_SQUARE_CAP : B_ROUND_CAP;
    const join_mode join = pen.GetJoin() == wxJOIN_MITER ? B_MITER_JOIN
        : pen.GetJoin() == wxJOIN_BEVEL ? B_BEVEL_JOIN : B_ROUND_JOIN;
    view->SetLineMode(cap, join);
    const auto dashes = wxHaikuDashes(pen);
    if (dashes.empty())
    {
        if (wxHaikuPenSize(pen)==1 && pen.GetStyle()==wxPENSTYLE_SOLID)
        {
            const size_t count=closed?points.size():points.size()-1;
            for(size_t i=0;i<count;++i)
            {
                const BPoint a=points[i],b=points[(i+1)%points.size()];
                long long x=wxRound(a.x),y=wxRound(a.y),endX=wxRound(b.x),endY=wxRound(b.y);
                const long long dx=std::abs(endX-x),dy=-std::abs(endY-y);
                const int sx=x<endX?1:-1,sy=y<endY?1:-1;
                long long error=dx+dy;
                for(;;)
                {
                    view->FillRect(BRect(x,y,x,y));
                    if(x==endX && y==endY) break;
                    const long long twice=2*error;
                    if(twice>=dy) { error+=dy;x+=sx; }
                    if(twice<=dx) { error+=dx;y+=sy; }
                }
            }
        }
        else view->StrokePolygon(points.data(), points.size(), closed);
        return;
    }
    size_t dash = 0;
    double remaining = dashes[0];
    const size_t segments = closed ? points.size() : points.size() - 1;
    for (size_t i = 0; i < segments; ++i)
    {
        const BPoint a = points[i], b = points[(i + 1) % points.size()];
        const double dx = b.x - a.x, dy = b.y - a.y;
        const double length = std::hypot(dx, dy);
        double offset = 0;
        while (offset < length)
        {
            const double step = std::min(remaining, length - offset);
            if (!(dash % 2))
                view->StrokeLine(BPoint(a.x + dx * offset / length,
                                        a.y + dy * offset / length),
                                 BPoint(a.x + dx * (offset + step) / length,
                                        a.y + dy * (offset + step) / length));
            offset += step;
            remaining -= step;
            if (remaining < 1e-6)
            {
                dash = (dash + 1) % dashes.size();
                remaining = dashes[dash];
            }
        }
    }
}

static std::vector<BPoint> wxHaikuArcPoints(const BRect& rect,
                                          double start, double span)
{
    const double rx = rect.Width() / 2, ry = rect.Height() / 2;
    const double cx = rect.left + rx, cy = rect.top + ry;
    const int count = std::max(1, static_cast<int>(std::ceil(
        std::abs(span) * M_PI / 180 * std::sqrt(std::max(rx, ry) * 2 + 1))));
    std::vector<BPoint> points;
    for (int i = 0; i <= count; ++i)
    {
        const double angle = (start + span * i / count) * M_PI / 180;
        points.emplace_back(cx + rx * std::cos(angle), cy - ry * std::sin(angle));
    }
    return points;
}

// Convert a separate wxMask into alpha without changing the source bitmap.
static std::unique_ptr<BBitmap> wxHaikuMaskedBitmap(const BBitmap* source,
    const wxMask* mask, int offsetX = 0, int offsetY = 0)
{
    const wxBitmap maskBitmap = mask->GetBitmap();
    const wxImage maskImage = maskBitmap.ConvertToImage();
    if (!maskImage.IsOk())
        return nullptr;
    auto result = std::unique_ptr<BBitmap>(new BBitmap(source->Bounds(), B_RGBA32));
    if (!result->IsValid() || result->ImportBits(source) != B_OK)
        return nullptr;
    const int width = source->Bounds().IntegerWidth() + 1;
    const int height = source->Bounds().IntegerHeight() + 1;
    for (int y = 0; y < height; ++y)
    {
        auto* row = static_cast<uint8*>(result->Bits()) + y * result->BytesPerRow();
        for (int x = 0; x < width; ++x)
        {
            const int mx = x + offsetX, my = y + offsetY;
            bool opaque = false;
            if (mx >= 0 && my >= 0 && mx < maskBitmap.GetWidth()
                && my < maskBitmap.GetHeight())
            {
                opaque = maskImage.GetRed(mx, my) != 0;
            }
            if (!opaque)
                row[x * 4 + 3] = 0;
            else if (source->ColorSpace() != B_RGBA32)
                row[x * 4 + 3] = 255;
        }
    }
    return result;
}

// Native B_ALPHA_OVERLAY truncates colour channels. For unscaled bitmap
// copies into memory DCs use rounded, straight-alpha source-over arithmetic.
// Other transforms continue through the native renderer.
static bool wxHaikuCompositeBitmap(BView* view, BBitmap* destination,
    const BBitmap* source, const BRect& srcRect, const BRect& dstRect)
{
    if (!destination || source->ColorSpace()!=B_RGBA32 ||
        (destination->ColorSpace()!=B_RGBA32 && destination->ColorSpace()!=B_RGB32) ||
        !view->Transform().IsIdentity() || srcRect.Width()!=dstRect.Width() ||
        srcRect.Height()!=dstRect.Height() || dstRect.left!=std::floor(dstRect.left) ||
        dstRect.top!=std::floor(dstRect.top)) return false;
    view->Sync();
    BRegion clip; view->GetClippingRegion(&clip);
    const BRect bounds = destination->Bounds();
    const BRect area = dstRect & bounds;
    for (int y=(int)area.top; area.IsValid() && y<=area.bottom; ++y)
        for (int x=(int)area.left; x<=area.right; ++x)
        {
            if (!clip.Contains(BPoint(x,y))) continue;
            const int sx=(int)srcRect.left+x-(int)dstRect.left;
            const int sy=(int)srcRect.top+y-(int)dstRect.top;
            const auto* src=static_cast<const uint8*>(source->Bits())+sy*source->BytesPerRow()+sx*4;
            auto* dst=static_cast<uint8*>(destination->Bits())+y*destination->BytesPerRow()+x*4;
            const unsigned sa=src[3], da=destination->ColorSpace()==B_RGBA32?dst[3]:255;
            if (!sa) continue;
            const unsigned weight=sa*255+da*(255-sa);
            for (int c=0;c<3;++c)
                dst[c]=(src[c]*sa*255+dst[c]*da*(255-sa)+weight/2)/weight;
            dst[3]=(weight+127)/255;
        }
    return true;
}

// ----------------------------------------------------------------------------
// RAII looper lock helper
// ----------------------------------------------------------------------------


wxIMPLEMENT_CLASS(wxHaikuDCImpl, wxDCImpl);

// ----------------------------------------------------------------------------
// ctor / dtor
// ----------------------------------------------------------------------------

wxHaikuDCImpl::wxHaikuDCImpl(wxDC* owner)
    : wxDCImpl(owner),
      m_view(nullptr),
      m_bitmap(nullptr),
      m_clipDepth(0),
      m_ppi(96, 96),
      m_backgroundMode(wxBRUSHSTYLE_TRANSPARENT)
{
      
    SetFont(*wxNORMAL_FONT);
    SetPen(*wxBLACK_PEN);
    SetBrush(*wxWHITE_BRUSH);
    SetBackground(*wxWHITE_BRUSH);
}

wxHaikuDCImpl::~wxHaikuDCImpl()
{
   // HAIKU_LOG(wxS("dtor this=%p view=%p bitmap=%p clipDepth=%d"),
     //         this, m_view, m_bitmap, m_clipDepth);
}


// ----------------------------------------------------------------------------
// capabilities
// ----------------------------------------------------------------------------

bool wxHaikuDCImpl::CanDrawBitmap() const
{
    HAIKU_LOG(wxS("CanDrawBitmap this=%p"), this);
    return true;
}

bool wxHaikuDCImpl::CanGetTextExtent() const
{
    HAIKU_LOG(wxS("CanGetTextExtent this=%p"), this);
    return true;
}

// ----------------------------------------------------------------------------
// size / metrics
// ----------------------------------------------------------------------------

void wxHaikuDCImpl::DoGetSize(int* width, int* height) const
{
    HAIKU_LOG(wxS("DoGetSize ENTER this=%p view=%p"), this, m_view);

    if (!m_view)
    {
        if (width)  *width  = 0;
        if (height) *height = 0;
        HAIKU_LOG(wxS("DoGetSize no view -> (0,0)"));
        return;
    }

    HaikuViewLock guard(m_view);
    if (!guard.ok())
    {
        if (width)  *width  = 0;
        if (height) *height = 0;
        HAIKU_LOG(wxS("DoGetSize failed to lock -> (0,0)"));
        return;
    }

    BRect r = m_view->Bounds();
    if (width)  *width  = (int)r.Width()  + 1;
    if (height) *height = (int)r.Height() + 1;

    HAIKU_LOG(wxS("DoGetSize EXIT size=(%d x %d)"),
              width ? *width : -1, height ? *height : -1);
}

void wxHaikuDCImpl::ComputeScaleAndOrigin()
{
    wxDCImpl::ComputeScaleAndOrigin();
 //   HAIKU_LOG(wxS("ComputeScaleAndOrigin ENTER" ));

    if (!m_view)
        return;

    HaikuViewLock guard(m_view);
    if (!guard.ok())
        return;

 
    m_view->SetTransform(BAffineTransform());

    if (GetLayoutDirection() == wxLayout_RightToLeft)
    {
        const float w = m_view->Bounds().Width() + 1.0f;
   //     HAIKU_LOG(wxS("ComputeScaleAndOrigin RTL width=%.1f"), w);
        m_view->TranslateBy((double)w, 0.0);
        m_view->ScaleBy(-1.0, 1.0);
    }

    m_view->TranslateBy(
        (double)(m_deviceOriginX + m_deviceLocalOriginX),
        (double)(m_deviceOriginY + m_deviceLocalOriginY)
    );

    m_scaleX = m_logicalScaleX * m_userScaleX;
    m_scaleY = m_logicalScaleY * m_userScaleY;

  //  HAIKU_LOG(wxS("ComputeScaleAndOrigin scale=(%.4f, %.4f) sign=(%d, %d) "
    //              "devOrigin=(%d,%d) localOrigin=(%d,%d) logicalOrigin=(%d,%d)"),
      //        m_scaleX, m_scaleY, (int)m_signX, (int)m_signY,
        //      m_deviceOriginX, m_deviceOriginY,
          //    m_deviceLocalOriginX, m_deviceLocalOriginY,
            //  m_logicalOriginX, m_logicalOriginY);

    m_view->ScaleBy(
        (double)(m_scaleX * m_signX),
        (double)(m_scaleY * m_signY)
    );

    m_view->TranslateBy(
        (double)(-m_logicalOriginX),
        (double)(-m_logicalOriginY)
    );

    // Clipping is stored in device coordinates and survives transform changes.

 //   HaikuDumpView("ComputeScaleAndOrigin after", m_view);
  //  HAIKU_LOG(wxS("ComputeScaleAndOrigin EXIT"));
}

void wxHaikuDCImpl::DoGetSizeMM(int* width, int* height) const
{
  //  HAIKU_LOG(wxS("DoGetSizeMM ENTER ppi=(%d,%d)"), m_ppi.x, m_ppi.y);

    int w, h;
    DoGetSize(&w, &h);

    const int ppiX = (m_ppi.x > 0) ? m_ppi.x : 96;
    const int ppiY = (m_ppi.y > 0) ? m_ppi.y : 96;

    if (width)
        *width  = (w * 254 + ppiX * 5) / (ppiX * 10);
    if (height)
        *height = (h * 254 + ppiY * 5) / (ppiY * 10);

 //   HAIKU_LOG(wxS("DoGetSizeMM EXIT sizeMM=(%d x %d)"),
   //           width ? *width : -1, height ? *height : -1);
}

int wxHaikuDCImpl::GetDepth() const
{
    //HAIKU_LOG(wxS("GetDepth -> 32"));
    return 32;
}

wxSize wxHaikuDCImpl::GetPPI() const
{
    //HAIKU_LOG(wxS("GetPPI -> (%d,%d)"), m_ppi.x, m_ppi.y);
    return m_ppi;
}


// ----------------------------------------------------------------------------
// coordinate transforms
// ----------------------------------------------------------------------------

wxPoint wxHaikuDCImpl::DeviceToLogical(wxCoord x, wxCoord y) const
{
   // HAIKU_LOG(wxS("DeviceToLogical in=(%d,%d)"), (int)x, (int)y);

    const double sx = m_scaleX * m_signX;
    const double sy = m_scaleY * m_signY;

    const double invSx = (sx != 0.0) ? (1.0 / sx) : 0.0;
    const double invSy = (sy != 0.0) ? (1.0 / sy) : 0.0;

    const double dx = double(x) - double(m_deviceOriginX + m_deviceLocalOriginX);
    const double dy = double(y) - double(m_deviceOriginY + m_deviceLocalOriginY);

    const double lx = dx * invSx + double(m_logicalOriginX);
    const double ly = dy * invSy + double(m_logicalOriginY);

    wxPoint pt(wxRound(lx), wxRound(ly));
 //   HAIKU_LOG(wxS("DeviceToLogical out=(%d,%d)"), pt.x, pt.y);
    return pt;
}

wxPoint wxHaikuDCImpl::LogicalToDevice(wxCoord x, wxCoord y) const
{
   //HAIKU_LOG(wxS("LogicalToDevice in=(%d,%d)"), (int)x, (int)y);

    const double sx = m_scaleX * m_signX;
    const double sy = m_scaleY * m_signY;

    const double lx = double(x) - double(m_logicalOriginX);
    const double ly = double(y) - double(m_logicalOriginY);

    const double dx = lx * sx + double(m_deviceOriginX + m_deviceLocalOriginX);
    const double dy = ly * sy + double(m_deviceOriginY + m_deviceLocalOriginY);

    wxPoint pt(wxRound(dx), wxRound(dy));
  //  HAIKU_LOG(wxS("LogicalToDevice out=(%d,%d)"), pt.x, pt.y);
    return pt;
}

wxSize wxHaikuDCImpl::DeviceToLogicalRel(int x, int y) const
{
    return wxDCImpl::DeviceToLogicalRel(x, y);
}

wxSize wxHaikuDCImpl::LogicalToDeviceRel(int x, int y) const
{
    return wxDCImpl::LogicalToDeviceRel(x, y);
}

// ----------------------------------------------------------------------------
// logical function / drawing mode
// ----------------------------------------------------------------------------

static drawing_mode wxToHaikuDrawingMode(wxRasterOperationMode rop)
{
    switch (rop)
    {
        case wxCOPY:   return B_OP_COPY;
        case wxINVERT: return B_OP_INVERT;
        // Other logical modes use the bitmap software fallback.
        default:       return B_OP_COPY;
    }
}

void wxHaikuDCImpl::SetLogicalFunction(wxRasterOperationMode function)
{
  

    wxCHECK_RET(function >= wxCLEAR && function <= wxSET, "Invalid raster operation");
    wxDCImpl::m_logicalFunction = m_logicalFunction = function;

    if (!m_view)
        return;

    const drawing_mode dm = wxToHaikuDrawingMode(function);



    HaikuViewLock guard(m_view);
    if (guard.ok())
        m_view->SetDrawingMode(dm);
}

// ----------------------------------------------------------------------------
// attributes
// ----------------------------------------------------------------------------

void wxHaikuDCImpl::SetFont(const wxFont& font)
{
  //  HAIKU_LOG(wxS("SetFont ENTER this=%p fontOk=%d view=%p"), this, font.IsOk(), m_view);

    wxDCImpl::m_font = m_font = font;
    m_bfont = font.IsOk() ? font.GetBFont() : *be_plain_font;

    if (!m_view)
        return;

    HaikuViewLock guard(m_view);
    if (guard.ok())
        m_view->SetFont(&m_bfont);

 //   HAIKU_LOG(wxS("SetFont EXIT"));
}

void wxHaikuDCImpl::SetPen(const wxPen& pen)
{
  
    wxDCImpl::m_pen = m_pen = pen;
    m_penColor = pen.IsOk() ? wxColourToRGB(pen.GetColour()) : rgb_color{};
}


void wxHaikuDCImpl::SetBrush(const wxBrush& brush)
{
    
    wxDCImpl::m_brush = m_brush = brush;
    m_brushColor = brush.IsOk() ? wxColourToRGB(brush.GetColour()) : rgb_color{};
}

void wxHaikuDCImpl::SetBackground(const wxBrush& brush)
{
   

    wxDCImpl::m_backgroundBrush = m_backgroundBrush = brush;
}

void wxHaikuDCImpl::SetBackgroundMode(int mode)
{
   
    wxDCImpl::m_backgroundMode = m_backgroundMode = mode;
}

#if wxUSE_PALETTE
void wxHaikuDCImpl::SetPalette(const wxPalette& WXUNUSED(palette))
{
 //   HAIKU_LOG("SetPalette not implemented");
    wxMISSING_IMPLEMENTATION(__func__);
}
#endif

// ----------------------------------------------------------------------------
// text metrics
// ----------------------------------------------------------------------------

wxCoord wxHaikuDCImpl::GetCharHeight() const
{
    font_height fh;
    m_bfont.GetHeight(&fh);
    const wxCoord h = (wxCoord)(fh.ascent + fh.descent + fh.leading);
 //   HAIKU_LOG("GetCharHeight -> %d", (int)h);
    return h;
}

wxCoord wxHaikuDCImpl::GetCharWidth() const
{
    const wxCoord w = (wxCoord)m_bfont.StringWidth("M");
 //   HAIKU_LOG("GetCharWidth -> %d", (int)w);
    return w;
}

void wxHaikuDCImpl::DoGetTextExtent(const wxString& text,
                                    wxCoord* x, wxCoord* y,
                                    wxCoord* descent,
                                    wxCoord* externalLeading,
                                    const wxFont* font) const
{

 //   HAIKU_LOG("DoGetTextExtent text='%s' font=%p", text, font);

    BFont f = font ? font->GetBFont() : m_bfont;

    if (x)
        *x = wxRound(f.StringWidth(text.utf8_str()));

    if (y || descent || externalLeading)
    {
        font_height fh;
        f.GetHeight(&fh);

        if (y)
            *y = text.empty() ? 0 : wxRound(fh.ascent + fh.descent + fh.leading);
        if (descent)
            *descent = wxRound(fh.descent);
        if (externalLeading)
            *externalLeading = wxRound(fh.leading);
    }

 //  HAIKU_LOG("DoGetTextExtent result w=%d h=%d descent=%d leading=%d",
//              x ? *x : -1, y ? *y : -1,
  //            descent ? *descent : -1,
    //          externalLeading ? *externalLeading : -1);
}

// ----------------------------------------------------------------------------
// clipping
// ----------------------------------------------------------------------------

bool wxHaikuDCImpl::DoGetClippingRect(wxRect& rect) const
{
    wxRect device;
    if (m_isClipBoxValid)
        device = m_clippingRegion.GetBox();
    else
    {
        int w, h;
        DoGetSize(&w, &h);
        device = wxRect(0, 0, w, h);
    }
    if (device.IsEmpty())
        rect = wxRect();
    else
    {
        const wxPoint a = DeviceToLogical(device.x, device.y);
        const wxPoint b = DeviceToLogical(device.x + device.width,
                                          device.y + device.height);
        rect = wxRect(std::min(a.x, b.x), std::min(a.y, b.y),
                      std::abs(b.x - a.x), std::abs(b.y - a.y));
    }
    return m_isClipBoxValid;
}

void wxHaikuDCImpl::DoSetClippingRegion(wxCoord x, wxCoord y,
                                      wxCoord w, wxCoord h)
{
    if (w < 0) { x += w + 1; w = -w; }
    if (h < 0) { y += h + 1; h = -h; }
    const wxPoint a = LogicalToDevice(x, y);
    const wxPoint b = LogicalToDevice(x + w, y + h);
    DoSetDeviceClippingRegion(wxRegion(std::min(a.x, b.x),
        std::min(a.y, b.y), std::abs(b.x - a.x), std::abs(b.y - a.y)));
}

void wxHaikuDCImpl::DoSetDeviceClippingRegion(const wxRegion& region)
{
    if (!m_view)
        return;
    HaikuViewLock guard(m_view);
    if (!guard.ok())
        return;

    // Save the original native clipping once. Further clips intersect it.
    if (m_clipDepth == 0)
    {
        // Haiku composes a pushed state's transform with its parent state.
        m_view->SetTransform(BAffineTransform());
        m_view->PushState();
        m_clipDepth = 1;
    }
    wxRegion combined = region;
    if (m_isClipBoxValid)
        combined.Intersect(m_clippingRegion);
    else
    {
        int w, h;
        DoGetSize(&w, &h);
        combined.Intersect(wxRect(0, 0, w, h));
    }
    m_view->SetTransform(BAffineTransform());
    BRegion native;
    m_view->GetClippingRegion(&native);
    native.IntersectWith(&combined.GetHaikuRegion());
    m_view->ConstrainClippingRegion(&native);
    m_clippingRegion = combined;
    m_isClipBoxValid = true;
    m_clipping = true;
    ComputeScaleAndOrigin();
}

void wxHaikuDCImpl::DestroyClippingRegion()
{
    if (m_view && m_clipDepth)
    {
        HaikuViewLock guard(m_view);
        if (!guard.ok())
            return;
        m_view->PopState();
        m_clipDepth = 0;
        // PopState must not undo attribute or transform changes made since clipping.
        m_view->SetFont(&m_bfont);
        m_view->SetDrawingMode(wxToHaikuDrawingMode(m_logicalFunction));
        ComputeScaleAndOrigin();
    }
    m_clippingRegion.Clear();
    m_isClipBoxValid = false;
    wxDCImpl::DestroyClippingRegion();
}

// ----------------------------------------------------------------------------
// clearing
// ----------------------------------------------------------------------------

void wxHaikuDCImpl::Clear()
{
    if (!m_view || (m_backgroundBrush.IsOk()
        && m_backgroundBrush.GetStyle() == wxBRUSHSTYLE_TRANSPARENT))
        return;
    HaikuViewLock guard(m_view);
    if (!guard.ok())
        return;
    const wxBrush bg = m_backgroundBrush.IsOk() ? m_backgroundBrush : *wxWHITE_BRUSH;
    m_view->SetTransform(BAffineTransform());
    m_view->PushState();
    m_view->SetDrawingMode(B_OP_COPY);
    m_view->SetHighColor(wxColourToRGB(bg.GetColour()));
    m_view->FillRect(m_view->Bounds());
    m_view->PopState();
    ComputeScaleAndOrigin();
}

// ----------------------------------------------------------------------------
// drawing primitives
// ----------------------------------------------------------------------------

void wxHaikuDCImpl::DoDrawPoint(wxCoord x, wxCoord y)
{
    if (AreAutomaticBoundingBoxUpdatesEnabled())
        CalcBoundingBox(x, y);

  //  HAIKU_LOG("DoDrawPoint (%d,%d) view=%p", (int)x, (int)y, m_view);

    if (!m_view || !wxHaikuShouldStroke(m_pen))
        return;

    HaikuViewLock guard(m_view);
    if (!guard.ok())
        return;
    wxHaikuRasterOperation raster(m_view, m_bitmap, m_logicalFunction);
    if (!raster.IsOk())
        return;

    m_view->SetHighColor(m_penColor);
    m_view->SetPenSize(wxHaikuPenSize(m_pen));
    m_view->StrokeLine(BPoint((float)x, (float)y), BPoint((float)x, (float)y));
    m_view->SetPenSize(1.0f);
}

void wxHaikuDCImpl::DoDrawLine(wxCoord x1, wxCoord y1,
                               wxCoord x2, wxCoord y2)
{
    if (AreAutomaticBoundingBoxUpdatesEnabled())
        CalcBoundingBox(x1, y1, x2, y2);

   // HAIKU_LOG("DoDrawLine (%d,%d)->(%d,%d) view=%p",
   //           (int)x1, (int)y1, (int)x2, (int)y2, m_view);

    if (!m_view || !wxHaikuShouldStroke(m_pen))
        return;

    HaikuViewLock guard(m_view);
    if (!guard.ok())
        return;
    wxHaikuRasterOperation raster(m_view, m_bitmap, m_logicalFunction);
    if (!raster.IsOk())
        return;

    m_view->SetHighColor(m_penColor);
    m_view->SetPenSize(wxHaikuPenSize(m_pen));
    if (wxHaikuPenSize(m_pen)==1 && m_pen.GetStyle()==wxPENSTYLE_SOLID)
    {
        // wxDC integer hairlines have full pixel coverage, unlike the
        // antialiased round endpoints produced by BView::StrokePolygon.
        long long px=x1, py=y1;
        const long long dx=std::abs((long long)x2-x1), dy=-std::abs((long long)y2-y1);
        const int sx=x1<x2?1:-1, sy=y1<y2?1:-1;
        long long error=dx+dy;
        for (;;)
        {
            m_view->FillRect(BRect(px,py,px,py));
            if (px==x2 && py==y2) break;
            const long long twice=2*error;
            if (twice>=dy) { error+=dy; px+=sx; }
            if (twice<=dx) { error+=dx; py+=sy; }
        }
    }
    else
        wxHaikuStrokePath(m_view, m_pen, {BPoint(x1, y1), BPoint(x2, y2)}, false);
    m_view->SetPenSize(1.0f);
}

void wxHaikuDCImpl::DoDrawPolygon(int n, const wxPoint points[],
                                  wxCoord xoffset, wxCoord yoffset,
                                  wxPolygonFillMode fillStyle)
{
    if (AreAutomaticBoundingBoxUpdatesEnabled())
        for (int i = 0; points && i < n; ++i) CalcBoundingBox(points[i].x + xoffset, points[i].y + yoffset);

  //  HAIKU_LOG("DoDrawPolygon n=%d offset=(%d,%d) fillStyle=%d view=%p",
   //           n, (int)xoffset, (int)yoffset, (int)fillStyle, m_view);

    if (!m_view || n <= 0 || !points)
        return;

    HaikuViewLock guard(m_view);
    if (!guard.ok())
        return;
    wxHaikuRasterOperation raster(m_view, m_bitmap, m_logicalFunction);
    if (!raster.IsOk())
        return;

    std::vector<BPoint> pts;
    pts.reserve(n);
    for (int i = 0; i < n; ++i)
    {
   //     HAIKU_LOG("DoDrawPolygon pt[%d]=(%d,%d)", i, points[i].x, points[i].y);
       pts.emplace_back((float)(points[i].x + xoffset),
                         (float)(points[i].y + yoffset));
    }

    const int32 oldRule = m_view->FillRule();
    m_view->SetFillRule(wxFillModeToHaiku(fillStyle));

    if (wxHaikuShouldFill(m_brush))
    {
        m_view->SetHighColor(m_brushColor);
        if (m_brush.GetStyle()==wxBRUSHSTYLE_SOLID)
        {
            std::vector<wxPoint> polygon;
            for (const auto& point:pts) polygon.emplace_back(wxRound(point.x),wxRound(point.y));
            wxRegion region(polygon.size(),polygon.data(),fillStyle);
            for (wxRegionIterator it(region);it;++it)
            {
                const wxRect rect=it.GetRect();
                m_view->FillRect(BRect(rect.x,rect.y,rect.GetRight(),rect.GetBottom()));
            }
        }
        else m_view->FillPolygon(pts.data(), (int32)n, B_SOLID_HIGH);
    }

    if (wxHaikuShouldStroke(m_pen))
    {
        m_view->SetHighColor(m_penColor);
        m_view->SetPenSize(wxHaikuPenSize(m_pen));
        wxHaikuStrokePath(m_view, m_pen, pts, true);
        m_view->SetPenSize(1.0f);
    }

    m_view->SetFillRule(oldRule);
}

void wxHaikuDCImpl::DoDrawPolyPolygon(int n,
                                      const int count[],
                                      const wxPoint points[],
                                      wxCoord xoffset,
                                      wxCoord yoffset,
                                      wxPolygonFillMode fillStyle)
{
   // HAIKU_LOG("DoDrawPolyPolygon polys=%d offset=(%d,%d) fillStyle=%d view=%p",
        //      n, (int)xoffset, (int)yoffset, (int)fillStyle, m_view);

    if (!m_view || n <= 0 || !count || !points)
        return;

    if (n == 1)
    {
        DoDrawPolygon(count[0], points, xoffset, yoffset, fillStyle);
        return;
    }

    HaikuViewLock guard(m_view);
    if (!guard.ok())
        return;
    wxHaikuRasterOperation raster(m_view, m_bitmap, m_logicalFunction);
    if (!raster.IsOk())
        return;

    const int32 oldRule = m_view->FillRule();
    m_view->SetFillRule(wxFillModeToHaiku(fillStyle));

    if (AreAutomaticBoundingBoxUpdatesEnabled())
    {
        int offset = 0;
        for (int polygon = 0; polygon < n; ++polygon)
            for (int i = 0; i < count[polygon]; ++i, ++offset)
                CalcBoundingBox(points[offset].x + xoffset, points[offset].y + yoffset);
    }
    BShape shape;
    int idx = 0;

    for (int poly = 0; poly < n; ++poly)
    {
        const int c = count[poly];
     //   HAIKU_LOG("DoDrawPolyPolygon poly=%d count=%d", poly, c);

        if (c <= 0)
            continue;

        const wxPoint& start = points[idx];
        shape.MoveTo(BPoint((float)(start.x + xoffset),
                            (float)(start.y + yoffset)));
        ++idx;

        for (int k = 1; k < c; ++k, ++idx)
        {
            shape.LineTo(BPoint((float)(points[idx].x + xoffset),
                                (float)(points[idx].y + yoffset)));
        }

        const wxPoint& last = points[idx - 1];
        if (last != start)
        {
            shape.LineTo(BPoint((float)(start.x + xoffset),
                                (float)(start.y + yoffset)));
        }

        shape.Close();
    }

    if (wxHaikuShouldFill(m_brush))
    {
        m_view->SetHighColor(m_brushColor);
        m_view->FillShape(&shape, B_SOLID_HIGH);
    }

    if (wxHaikuShouldStroke(m_pen))
    {
        m_view->SetHighColor(m_penColor);
        m_view->SetPenSize(wxHaikuPenSize(m_pen));
        if (wxHaikuDashes(m_pen).empty())
            m_view->StrokeShape(&shape, B_SOLID_HIGH);
        else
        {
            int offset = 0;
            for (int polygon = 0; polygon < n; ++polygon)
            {
                std::vector<BPoint> path;
                for (int i = 0; i < count[polygon]; ++i, ++offset)
                    path.emplace_back(points[offset].x + xoffset, points[offset].y + yoffset);
                wxHaikuStrokePath(m_view, m_pen, path, true);
            }
        }
        m_view->SetPenSize(1.0f);
    }

    m_view->SetFillRule(oldRule);
}

void wxHaikuDCImpl::DoDrawArc(wxCoord x1, wxCoord y1,
                              wxCoord x2, wxCoord y2,
                              wxCoord xc, wxCoord yc)
{
 //   HAIKU_LOG("DoDrawArc p1=(%d,%d) p2=(%d,%d) center=(%d,%d) view=%p",
  //            (int)x1, (int)y1, (int)x2, (int)y2, (int)xc, (int)yc, m_view);

    if (!m_view)
        return;

    const double dx = double(x1) - double(xc);
    const double dy = double(y1) - double(yc);
    const double r  = sqrt(dx * dx + dy * dy);
    if (r <= 0.0)
        return;

    const float start = wxAngleDegFromCenter(xc, yc, x1, y1);
    float end = wxAngleDegFromCenter(xc, yc, x2, y2);
    float span = end - start;
    if (span <= 0.0f)
        span += 360.0f;

   // HAIKU_LOG("DoDrawArc radius=%.2f start=%.2f span=%.2f", r, start, span);

    if (AreAutomaticBoundingBoxUpdatesEnabled())
        CalcBoundingBox(static_cast<int>(std::floor(xc - r)), static_cast<int>(std::floor(yc - r)),
                        static_cast<int>(std::ceil(xc + r)), static_cast<int>(std::ceil(yc + r)));
    const float rf = (float)r;
    BRect rect((float)xc - rf, (float)yc - rf,
               (float)xc + rf, (float)yc + rf);

    HaikuViewLock guard(m_view);
    if (!guard.ok())
        return;
    wxHaikuRasterOperation raster(m_view, m_bitmap, m_logicalFunction);
    if (!raster.IsOk())
        return;

    if (wxHaikuShouldFill(m_brush))
    {
        m_view->SetHighColor(m_brushColor);
        m_view->FillArc(rect, start, span);
    }

    if (wxHaikuShouldStroke(m_pen))
    {
        m_view->SetHighColor(m_penColor);
        m_view->SetPenSize(wxHaikuPenSize(m_pen));
        if (wxHaikuDashes(m_pen).empty())
            m_view->StrokeArc(rect, start, span);
        else
            wxHaikuStrokePath(m_view, m_pen, wxHaikuArcPoints(rect, start, span), false);
        m_view->SetPenSize(1.0f);
    }
}

void wxHaikuDCImpl::DoDrawEllipticArc(wxCoord x, wxCoord y,
                                      wxCoord w, wxCoord h,
                                      double sa, double ea)
{
    if (AreAutomaticBoundingBoxUpdatesEnabled())
        CalcBoundingBox(x, y, x + w, y + h);

   // HAIKU_LOG("DoDrawEllipticArc rect=(%d,%d %dx%d) start=%.2f end=%.2f view=%p",
     //         (int)x, (int)y, (int)w, (int)h, sa, ea, m_view);

    if (!m_view)
        return;

    float start = (float)sa;
    float end   = (float)ea;
    while (end < start)
        end += 360.0f;
    const float span = end == start ? 360.0f : end - start;

    BRect rect((float)x, (float)y,
               (float)(x + w - 1), (float)(y + h - 1));

    HaikuViewLock guard(m_view);
    if (!guard.ok())
        return;
    wxHaikuRasterOperation raster(m_view, m_bitmap, m_logicalFunction);
    if (!raster.IsOk())
        return;

    if (wxHaikuShouldFill(m_brush))
    {
        m_view->SetHighColor(m_brushColor);
        m_view->FillArc(rect, start, span);
    }

    if (wxHaikuShouldStroke(m_pen))
    {
        m_view->SetHighColor(m_penColor);
        m_view->SetPenSize(wxHaikuPenSize(m_pen));
        if (wxHaikuDashes(m_pen).empty())
            m_view->StrokeArc(rect, start, span);
        else
            wxHaikuStrokePath(m_view, m_pen, wxHaikuArcPoints(rect, start, span), false);
        m_view->SetPenSize(1.0f);
    }
}

// ---------------------------------------------------------------------------
// pixel access
// ---------------------------------------------------------------------------

static inline bool wxGetPixelFromBitmap32(const BBitmap* bmp, int x, int y,
                                          wxColour* col)
{
    if (!bmp || !col)
        return false;

    const BRect b = bmp->Bounds();
    const int w = (int)b.Width()  + 1;
    const int h = (int)b.Height() + 1;

    if (x < 0 || y < 0 || x >= w || y >= h)
        return false;

    const color_space cs = bmp->ColorSpace();
    if (cs != B_RGB32 && cs != B_RGBA32)
        return false;

    const uint8* bits = static_cast<const uint8*>(bmp->Bits());
    if (!bits)
        return false;

    const int32 bpr = bmp->BytesPerRow();
    const uint8* p  = bits + y * bpr + x * 4;

    const uint8 b_ = p[0];
    const uint8 g_ = p[1];
    const uint8 r_ = p[2];
    const uint8 a_ = (cs == B_RGBA32) ? p[3] : 255;

    col->Set(r_, g_, b_, a_);
    return true;
}

bool wxHaikuDCImpl::DoGetPixel(wxCoord x, wxCoord y, wxColour* col) const
{
   // HAIKU_LOG("DoGetPixel (%d,%d) bitmap=%p", (int)x, (int)y, m_bitmap);

    wxCHECK_MSG(col, false, "DoGetPixel() requires a non-null colour pointer");

    if (m_bitmap && m_view)
    {
        HaikuViewLock guard(m_view);
        if (!guard.ok())
            return false;
        m_view->Sync();
        const wxPoint p = LogicalToDevice(x, y);
        return wxGetPixelFromBitmap32(m_bitmap, p.x, p.y, col);
    }

    return false;
}

bool wxHaikuDCImpl::DoFloodFill(wxCoord x, wxCoord y, const wxColour& col,
                                wxFloodFillStyle style)
{
   // HAIKU_LOG("DoFloodFill x=%d y=%d colour=(%d,%d,%d,%d) style=%d",
    //          (int)x, (int)y,
     //         col.Red(), col.Green(), col.Blue(), col.Alpha(),
     //         (int)style);

#if wxUSE_IMAGE
    extern bool wxDoFloodFill(wxDC* dc, wxCoord x, wxCoord y,
                              const wxColour& col, wxFloodFillStyle style);
    return wxDoFloodFill(GetOwner(), x, y, col, style);
#else
    wxUnusedVar(x);
    wxUnusedVar(y);
    wxUnusedVar(col);
    wxUnusedVar(style);
    return false;
#endif
}

void wxHaikuDCImpl::DoDrawRectangle(wxCoord x, wxCoord y,
                                    wxCoord width, wxCoord height)
{
    if (AreAutomaticBoundingBoxUpdatesEnabled())
        CalcBoundingBox(x, y, x + width, y + height);

 //   HAIKU_LOG(wxS("DoDrawRectangle rect=(%d,%d %dx%d) view=%p"),
   //           (int)x, (int)y, (int)width, (int)height, m_view);

    if (!m_view)
        return;

    BRect r((float)x, (float)y,
            (float)(x + width - 1), (float)(y + height - 1));

    HaikuViewLock guard(m_view);
    if (!guard.ok())
        return;
    wxHaikuRasterOperation raster(m_view, m_bitmap, m_logicalFunction);
    if (!raster.IsOk())
        return;

    if (wxHaikuShouldFill(m_brush))
    {
        m_view->SetHighColor(m_brushColor);
        m_view->FillRect(r);
    }

    if (wxHaikuShouldStroke(m_pen))
    {
        m_view->SetHighColor(m_penColor);
        m_view->SetPenSize(wxHaikuPenSize(m_pen));
        wxHaikuStrokePath(m_view, m_pen, {r.LeftTop(), r.RightTop(), r.RightBottom(), r.LeftBottom()}, true);
        m_view->SetPenSize(1.0f);
    }
}

void wxHaikuDCImpl::DoDrawRoundedRectangle(wxCoord x, wxCoord y,
                                           wxCoord width, wxCoord height,
                                           double radius)
{
    if (AreAutomaticBoundingBoxUpdatesEnabled())
        CalcBoundingBox(x, y, x + width, y + height);

  //  HAIKU_LOG(wxS("DoDrawRoundedRectangle rect=(%d,%d %dx%d) radius=%.2f view=%p"),
  //            (int)x, (int)y, (int)width, (int)height, radius, m_view);

    if (!m_view)
        return;

    BRect r((float)x, (float)y,
            (float)(x + width - 1), (float)(y + height - 1));
    const float rad = radius < 0 ? -radius * std::min(width, height) : radius;

    HaikuViewLock guard(m_view);
    if (!guard.ok())
        return;
    wxHaikuRasterOperation raster(m_view, m_bitmap, m_logicalFunction);
    if (!raster.IsOk())
        return;

    if (wxHaikuShouldFill(m_brush))
    {
        m_view->SetHighColor(m_brushColor);
        m_view->FillRoundRect(r, rad, rad);
    }

    if (wxHaikuShouldStroke(m_pen))
    {
        m_view->SetHighColor(m_penColor);
        m_view->SetPenSize(wxHaikuPenSize(m_pen));
        if (wxHaikuDashes(m_pen).empty())
            m_view->StrokeRoundRect(r, rad, rad);
        else
        {
            const float radius = std::max(0.0f, std::min(rad, std::min(r.Width(), r.Height()) / 2));
            std::vector<BPoint> path;
            const BPoint centres[] = {
                BPoint(r.right - radius, r.top + radius),
                BPoint(r.left + radius, r.top + radius),
                BPoint(r.left + radius, r.bottom - radius),
                BPoint(r.right - radius, r.bottom - radius)};
            for (int i = 0; i < 4; ++i)
            {
                const BPoint c = centres[i];
                const auto arc = wxHaikuArcPoints(BRect(c.x - radius, c.y - radius,
                    c.x + radius, c.y + radius), i * 90, 90);
                path.insert(path.end(), arc.begin(), arc.end());
            }
            wxHaikuStrokePath(m_view, m_pen, path, true);
        }
        m_view->SetPenSize(1.0f);
    }
}

void wxHaikuDCImpl::DoDrawEllipse(wxCoord x, wxCoord y,
                                  wxCoord width, wxCoord height)
{
    if (AreAutomaticBoundingBoxUpdatesEnabled())
        CalcBoundingBox(x, y, x + width, y + height);

   // HAIKU_LOG("DoDrawEllipse rect=(%d,%d %dx%d) view=%p",
     //         (int)x, (int)y, (int)width, (int)height, m_view);

    if (!m_view)
        return;

    BRect r((float)x, (float)y,
            (float)(x + width - 1), (float)(y + height - 1));

    HaikuViewLock guard(m_view);
    if (!guard.ok())
        return;
    wxHaikuRasterOperation raster(m_view, m_bitmap, m_logicalFunction);
    if (!raster.IsOk())
        return;

    if (!wxHaikuShouldFill(m_brush) && wxHaikuShouldStroke(m_pen) &&
        wxHaikuPenSize(m_pen)==1 && m_pen.GetStyle()==wxPENSTYLE_SOLID &&
        width>0 && height>0 && width<=65535 && height<=65535)
    {
        // Integer midpoint outline: preserve the wxDC endpoint convention
        // without leaking fractional coverage into surrounding pixels.
        long long left=x, right=(long long)x+width, top=y, bottom=(long long)y+height;
        long long a=width, b=height, odd=b&1;
        long long dx=4*(1-a)*b*b, dy=4*(odd+1)*a*a;
        long long error=dx+dy+odd*a*a;
        top+=(b+1)/2; bottom=top-odd;
        a=8*a*a; odd=8*b*b;
        m_view->SetHighColor(m_penColor);
        auto pixel=[&](long long px,long long py) { m_view->FillRect(BRect(px,py,px,py)); };
        do
        {
            pixel(right,top); pixel(left,top); pixel(left,bottom); pixel(right,bottom);
            const long long twice=2*error;
            if (twice<=dy) { ++top; --bottom; error+=dy+=a; }
            if (twice>=dx || 2*error>dy) { ++left; --right; error+=dx+=odd; }
        } while (left<=right);
        while (top-bottom<b)
        {
            pixel(left-1,top); pixel(right+1,top++);
            pixel(left-1,bottom); pixel(right+1,bottom--);
        }
        return;
    }

    if (wxHaikuShouldFill(m_brush))
    {
        m_view->SetHighColor(m_brushColor);
        m_view->FillEllipse(r);
    }

    if (wxHaikuShouldStroke(m_pen))
    {
        m_view->SetHighColor(m_penColor);
        m_view->SetPenSize(wxHaikuPenSize(m_pen));
        if (wxHaikuDashes(m_pen).empty())
            m_view->StrokeEllipse(r);
        else
            wxHaikuStrokePath(m_view, m_pen, wxHaikuArcPoints(r, 0, 360), true);
        m_view->SetPenSize(1.0f);
    }
}

// ---------------------------------------------------------------------------
// Cross hair
// ---------------------------------------------------------------------------

void wxHaikuDCImpl::DoCrossHair(wxCoord x, wxCoord y)
{
  //  HAIKU_LOG("DoCrossHair (%d,%d) view=%p", (int)x, (int)y, m_view);

    if (!m_view || !wxHaikuShouldStroke(m_pen))
        return;

    int w = 0, h = 0;
    DoGetSize(&w, &h);

    HaikuViewLock guard(m_view);
    if (!guard.ok())
        return;
    wxHaikuRasterOperation raster(m_view, m_bitmap, m_logicalFunction);
    if (!raster.IsOk())
        return;

    m_view->SetHighColor(m_penColor);
    m_view->SetPenSize(wxHaikuPenSize(m_pen));

    m_view->StrokeLine(BPoint(0.0f, (float)y),
                       BPoint((float)(w - 1), (float)y));
    m_view->StrokeLine(BPoint((float)x, 0.0f),
                       BPoint((float)x, (float)(h - 1)));

    m_view->SetPenSize(1.0f);
}

void wxHaikuDCImpl::DoDrawText(const wxString& text, wxCoord x, wxCoord y)
{
    const wxScopedCharBuffer textUtf8 = text.utf8_str();

  //  HAIKU_LOG_RAW("DoDrawText view=%p text='%s' pos=(%d,%d)",
     ////             m_view,
        //          textUtf8.data() ? textUtf8.data() : "",
         //         (int)x, (int)y);

    DoDrawRotatedText(text, x, y, 0.0);
}

void wxHaikuDCImpl::DoDrawRotatedText(const wxString& text, wxCoord x, wxCoord y, double angle)
{
    if (!m_view || text.empty())
        return;
    HaikuViewLock guard(m_view);
    if (!guard.ok())
        return;
    font_height fh;
    m_bfont.GetHeight(&fh);
    const float lineHeight = std::ceil(fh.ascent + fh.descent + fh.leading);
    const auto lines = wxSplit(text, '\n', '\0');
    float maxWidth = 0;
    m_view->PushState();
    // wxDC text drawing ignores the logical function.
    m_view->SetDrawingMode(B_OP_COPY);
    m_view->TranslateBy(x, y);
    if (angle != 0)
        m_view->RotateBy(-angle);
    m_view->SetFont(&m_bfont);
    float top = 0;
    for (const wxString& line : lines)
    {
        const wxScopedCharBuffer utf8 = line.utf8_str();
        const float width = std::ceil(m_bfont.StringWidth(utf8.data()));
        maxWidth = std::max(maxWidth, width);
        if (m_backgroundMode == wxSOLID && width > 0)
        {
            m_view->SetHighColor(wxColourToRGB(m_textBackgroundColour));
            m_view->FillRect(BRect(0, top, width - 1, top + lineHeight - 1));
        }
        m_view->SetHighColor(wxColourToRGB(m_textForegroundColour));
        m_view->DrawString(utf8.data(), BPoint(0, top + fh.ascent));
        top += lineHeight;
    }
    m_view->PopState();
    if (AreAutomaticBoundingBoxUpdatesEnabled())
    {
        const double radians = angle * M_PI / 180;
        const double c = std::cos(radians), s = std::sin(radians);
        for (const BPoint& p : {BPoint(0, 0), BPoint(maxWidth, 0),
                               BPoint(0, top), BPoint(maxWidth, top)})
            CalcBoundingBox(wxRound(x + p.x * c + p.y * s),
                            wxRound(y - p.x * s + p.y * c));
    }
}
void wxHaikuDCImpl::DoDrawLines(int n, const wxPoint points[],
                                wxCoord xoffset, wxCoord yoffset)
{
    if (AreAutomaticBoundingBoxUpdatesEnabled())
        for (int i = 0; points && i < n; ++i) CalcBoundingBox(points[i].x + xoffset, points[i].y + yoffset);

  //  HAIKU_LOG("DoDrawLines n=%d offset=(%d,%d) view=%p",
    //          n, (int)xoffset, (int)yoffset, m_view);

    if (!m_view || n <= 0 || !points || !wxHaikuShouldStroke(m_pen))
        return;

    HaikuViewLock guard(m_view);
    if (!guard.ok())
        return;
    wxHaikuRasterOperation raster(m_view, m_bitmap, m_logicalFunction);
    if (!raster.IsOk())
        return;

    m_view->SetHighColor(m_penColor);
    m_view->SetPenSize(wxHaikuPenSize(m_pen));

    std::vector<BPoint> path;
    for (int i = 0; i < n; ++i)
        path.emplace_back(points[i].x + xoffset, points[i].y + yoffset);
    wxHaikuStrokePath(m_view, m_pen, path, false);
    m_view->SetPenSize(1.0f);
}

// ---------------------------------------------------------------------------
// Bitmaps / Icons
// ---------------------------------------------------------------------------

void wxHaikuDCImpl::DoDrawIcon(const wxIcon& icon, wxCoord x, wxCoord y)
{
  //  HAIKU_LOG("DoDrawIcon icon=%p pos=(%d,%d)", icon.GetBBitmap(), (int)x, (int)y);
    DoDrawBitmap(icon, x, y, true);
}

void wxHaikuDCImpl::DoDrawBitmap(const wxBitmap& bmp, wxCoord x, wxCoord y, bool useMask)
{
    if (!m_view || !bmp.IsOk())
        return;
    BBitmap* native = bmp.GetBBitmap();
    if (!native || !native->IsValid())
        return;
    std::unique_ptr<BBitmap> masked;
    if (useMask && bmp.GetMask())
    {
        masked = wxHaikuMaskedBitmap(native, bmp.GetMask());
        if (!masked)
            return;
        native = masked.get();
    }
    HaikuViewLock guard(m_view);
    if (!guard.ok())
        return;
    const BRect bitmapRect = native->Bounds();
    if (m_logicalFunction==wxCOPY && wxHaikuCompositeBitmap(m_view,m_bitmap,native,
            bitmapRect,BRect(x,y,x+bitmapRect.Width(),y+bitmapRect.Height())))
    {
        if (AreAutomaticBoundingBoxUpdatesEnabled())
            CalcBoundingBox(wxPoint(x,y),wxSize(bmp.GetWidth(),bmp.GetHeight()));
        return;
    }
    wxHaikuRasterOperation raster(m_view, m_bitmap, m_logicalFunction);
    if (!raster.IsOk())
        return;
    m_view->PushState();
    if (native->ColorSpace() == B_RGBA32)
    {
        m_view->SetDrawingMode(B_OP_ALPHA);
        m_view->SetBlendingMode(B_PIXEL_ALPHA, raster.IsActive() ? B_ALPHA_COMPOSITE : B_ALPHA_OVERLAY);
    }
    const BRect src = native->Bounds();
    m_view->DrawBitmap(native, src, BRect(x, y, x + src.Width(), y + src.Height()));
    m_view->PopState();
    if (AreAutomaticBoundingBoxUpdatesEnabled())
        CalcBoundingBox(wxPoint(x, y), wxSize(bmp.GetWidth(), bmp.GetHeight()));
}

// ---------------------------------------------------------------------------
// Blit
// ---------------------------------------------------------------------------

bool wxHaikuDCImpl::DoBlit(wxCoord xdest, wxCoord ydest, wxCoord width, wxCoord height,
                           wxDC* source, wxCoord xsrc, wxCoord ysrc,
                           wxRasterOperationMode rop, bool useMask,
                           wxCoord xsrcMask, wxCoord ysrcMask)
{
    if (!m_view || !source || width < 0 || height < 0)
        return false;
    if (rop == wxNO_OP || width == 0 || height == 0)
        return true;
    if (rop < wxCLEAR || rop > wxSET)
        return false;
    auto* srcImpl = dynamic_cast<wxHaikuDCImpl*>(source->GetImpl());
    if (!srcImpl || !srcImpl->m_bitmap)
        return false;
    BBitmap* srcBmp = srcImpl->m_bitmap;
    {
        HaikuViewLock sourceLock(srcImpl->m_view);
        if (!sourceLock.ok())
            return false;
        if (srcImpl->m_view)
            srcImpl->m_view->Sync();
    }
    const wxPoint origin = source->LogicalToDevice(xsrc, ysrc);
    const wxSize size = source->LogicalToDeviceRel(width, height);
    if (size.x <= 0 || size.y <= 0)
        return false;
    std::unique_ptr<BBitmap> masked;
    if (useMask)
    {
        auto* memory = dynamic_cast<wxMemoryDC*>(source);
        const wxMask* mask = memory ? memory->GetSelectedBitmap().GetMask() : nullptr;
        if (mask)
        {
            const wxPoint maskOrigin = source->LogicalToDevice(
                xsrcMask == wxDefaultCoord ? xsrc : xsrcMask,
                ysrcMask == wxDefaultCoord ? ysrc : ysrcMask);
            masked = wxHaikuMaskedBitmap(srcBmp, mask,
                                         maskOrigin.x - origin.x, maskOrigin.y - origin.y);
            if (!masked)
                return false;
            srcBmp = masked.get();
        }
    }
    // Snapshot self-blits so overlapping destinations cannot overwrite the source.
    std::unique_ptr<BBitmap> snapshot;
    if (srcBmp == m_bitmap)
    {
        snapshot.reset(new BBitmap(*srcBmp));
        if (!snapshot->IsValid())
            return false;
        srcBmp = snapshot.get();
    }
    BRect srcRect(origin.x, origin.y, origin.x + size.x - 1, origin.y + size.y - 1);
    const BRect clipped = srcRect & srcBmp->Bounds();
    if (!clipped.IsValid())
        return true;
    const double sx = double(width) / size.x, sy = double(height) / size.y;
    BRect dstRect(xdest + (clipped.left - srcRect.left) * sx,
                  ydest + (clipped.top - srcRect.top) * sy,
                  xdest + (clipped.right - srcRect.left + 1) * sx - 1,
                  ydest + (clipped.bottom - srcRect.top + 1) * sy - 1);
    HaikuViewLock guard(m_view);
    if (!guard.ok())
        return false;
    if (rop==wxCOPY && wxHaikuCompositeBitmap(m_view,m_bitmap,srcBmp,clipped,dstRect))
    {
        if (AreAutomaticBoundingBoxUpdatesEnabled())
            CalcBoundingBox(wxPoint(xdest,ydest),wxSize(width,height));
        return true;
    }
    wxHaikuRasterOperation raster(m_view, m_bitmap, rop);
    if (!raster.IsOk())
        return false;
    m_view->PushState();
    m_view->SetDrawingMode(raster.IsActive() ? B_OP_COPY : wxToHaikuDrawingMode(rop));
    if ((rop == wxCOPY || raster.IsActive()) && srcBmp->ColorSpace() == B_RGBA32)
    {
        m_view->SetDrawingMode(B_OP_ALPHA);
        m_view->SetBlendingMode(B_PIXEL_ALPHA, raster.IsActive() ? B_ALPHA_COMPOSITE : B_ALPHA_OVERLAY);
    }
    m_view->DrawBitmap(srcBmp, clipped, dstRect);
    m_view->PopState();
    if (AreAutomaticBoundingBoxUpdatesEnabled())
        CalcBoundingBox(wxPoint(xdest, ydest), wxSize(width, height));
    return true;
}

bool wxHaikuDCImpl::DoGetPartialTextExtents(const wxString& text, wxArrayInt& widths) const
{
    widths.clear();
    if (text.empty()) return true;
    std::vector<float> advances(text.length());
    m_bfont.GetEscapements(text.utf8_str(), static_cast<int32>(text.length()), advances.data());
    double position = 0;
    for (float advance : advances)
    {
        position += advance * m_bfont.Size();
        widths.Add(wxRound(position));
    }
    return true;
}
