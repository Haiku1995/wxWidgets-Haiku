///////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/graphics.cpp
// Purpose:     wxGraphicsContext implementation for Haiku
// Author:      Haiku port based on Qt pattern, enhanced
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#if wxUSE_GRAPHICS_CONTEXT

#include "wx/graphics.h"
#include "wx/haiku/private.h"

#ifndef WX_PRECOMP
    #include "wx/bitmap.h"
    #include "wx/icon.h"
    #include "wx/dcclient.h"
    #include "wx/dcmemory.h"
    #include "wx/dcprint.h"
    #include "wx/window.h"
#endif

#include "wx/private/graphics.h"

#include <View.h>
#include <Bitmap.h>
#include <Region.h>
#include <Shape.h>
#include <InterfaceKit.h>
#include <Gradient.h>
#include <GradientRadialFocus.h>
#include <Font.h>
#include <Point.h>
#include <AffineTransform.h>
#include <OS.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>
#include <functional>
#include <set>
#include <mutex>

// ============================================================================
// Helpers
// ============================================================================

inline rgb_color wxToHaikuColor(const wxColour& c)
{
    rgb_color color;
    color.red   = c.Red();
    color.green = c.Green();
    color.blue  = c.Blue();
    color.alpha = c.Alpha();
    return color;
}

class wxHaikuAutoViewLock
{
public:
    explicit wxHaikuAutoViewLock(BView* view)
        : m_view(view),
          m_window(view ? view->Window() : nullptr),
          m_locked(false)
    {
        // IsLocked() only tells us that some thread owns the looper lock.
        // Drawing through a BView is valid only while this thread owns it.
        if (m_window && m_window->LockingThread() != find_thread(nullptr))
            m_locked = m_window->LockLooper();
    }

    ~wxHaikuAutoViewLock()
    {
        if (m_locked && m_window)
            m_window->UnlockLooper();
    }

    bool ok() const
    {
        return m_view && (!m_window || m_locked ||
                          m_window->LockingThread() == find_thread(nullptr));
    }

private:
    BView*   m_view;
    BWindow* m_window;
    bool     m_locked;
};



static inline bool wxHaikuHasPositiveSize(wxDouble w, wxDouble h)
{
    return w > 0.0 && h > 0.0;
}

static inline BRect wxToBRect(wxDouble x, wxDouble y, wxDouble w, wxDouble h)
{
    return BRect((float)x, (float)y,
                 (float)(x + w - 1.0), (float)(y + h - 1.0));
}

// ============================================================================
// Bitmap data
// ============================================================================

class WXDLLIMPEXP_CORE wxHaikuBitmapData : public wxGraphicsBitmapData
{
public:
    wxHaikuBitmapData(wxGraphicsRenderer* renderer, const wxBitmap& bmp)
        : wxGraphicsBitmapData(renderer), m_bitmap(bmp)
    {
        if (bmp.IsOk() && bmp.GetMask()) {
            wxImage image=bmp.ConvertToImage();
            wxImage mask=bmp.GetMask()->GetBitmap().ConvertToImage();
            if(image.IsOk() && mask.IsOk()) {
                if(!image.HasAlpha()) image.InitAlpha();
                for(int y=0;y<std::min(image.GetHeight(),mask.GetHeight());++y)
                    for(int x=0;x<std::min(image.GetWidth(),mask.GetWidth());++x)
                        if(!mask.GetRed(x,y)) image.SetAlpha(x,y,0);
                image.SetMask(false);
                m_bitmap=wxBitmap(image);
            }
        }
    }

    const wxBitmap& GetBitmap() const
    {
        return m_bitmap;
    }

    wxImage ConvertToImage() const
    {
        wxImage image = m_bitmap.ConvertToImage();
        if (image.HasAlpha())
            for (int y=0; y<image.GetHeight(); ++y)
                for (int x=0; x<image.GetWidth(); ++x)
                    if (!image.GetAlpha(x,y)) image.SetRGB(x,y,0,0,0);
        return image;
    }

    void* GetNativeBitmap() const override
    {
        wxBitmapRefData* ref =
            static_cast<wxBitmapRefData*>(m_bitmap.GetRefData());

        return ref ? static_cast<void*>(ref->m_bitmap) : nullptr;
    }

private:
    wxBitmap m_bitmap;
};

// ============================================================================
// Pen / Brush / Font / Matrix data
// ============================================================================

class WXDLLIMPEXP_CORE wxHaikuPenData : public wxGraphicsObjectRefData
{
public:
    wxHaikuPenData(wxGraphicsRenderer* renderer, const wxGraphicsPenInfo& info)
        : wxGraphicsObjectRefData(renderer),
          m_colour(info.GetColour()),
          m_width(info.GetWidth()),
          m_cap(info.GetCap()), m_join(info.GetJoin()), m_style(info.GetStyle())
    {
        const double width=std::max(1.0,m_width);
        switch(m_style) {
            case wxPENSTYLE_DOT: m_dashes={width,width*2}; break;
            case wxPENSTYLE_SHORT_DASH: m_dashes={width*3,width*3}; break;
            case wxPENSTYLE_LONG_DASH: m_dashes={width*6,width*3}; break;
            case wxPENSTYLE_DOT_DASH: m_dashes={width*3,width*3,width,width*3}; break;
            case wxPENSTYLE_USER_DASH:
                if (info.GetDash()) for(int i=0;i<info.GetDashCount();++i)
                    m_dashes.push_back(std::max(1.0, double(info.GetDash()[i])*width));
                break;
            default: break;
        }
        if (info.GetGradientType() == wxGRADIENT_LINEAR)
            m_gradient=new BGradientLinear(BPoint(info.GetX1(),info.GetY1()),BPoint(info.GetX2(),info.GetY2()));
        else if (info.GetGradientType() == wxGRADIENT_RADIAL)
            m_gradient=new BGradientRadial(BPoint(info.GetEndX(),info.GetEndY()),info.GetRadius());
        if(m_gradient) for(size_t i=0;i<info.GetStops().GetCount();++i) {
            const auto& stop=info.GetStops().Item(i);
            m_gradient->AddColor(wxToHaikuColor(stop.GetColour()),255*stop.GetPosition());
        }
        if (m_dashes.size()%2) {
            const auto copy=m_dashes; m_dashes.insert(m_dashes.end(),copy.begin(),copy.end());
        }
    }

    ~wxHaikuPenData() override { delete m_gradient; }
    wxColour m_colour;
    BGradient* m_gradient=nullptr;
    wxDouble m_width;
    wxPenCap m_cap;
    wxPenJoin m_join;
    wxPenStyle m_style;
    std::vector<double> m_dashes;
};

class WXDLLIMPEXP_CORE wxHaikuBrushData : public wxGraphicsObjectRefData
{
public:
    explicit wxHaikuBrushData(wxGraphicsRenderer* renderer, const wxBrush& brush)
        : wxGraphicsObjectRefData(renderer),
          m_colour(brush.IsOk() ? brush.GetColour() : wxColour(0,0,0)),
          m_gradient(nullptr), m_style(brush.IsOk() ? brush.GetStyle() : wxBRUSHSTYLE_SOLID)
    {
        if (brush.IsOk() && brush.GetStipple()) m_stipple=*brush.GetStipple();
    }

    ~wxHaikuBrushData() override
    {
        delete m_gradient;
    }

    void CreateLinearGradientBrush(wxDouble x1, wxDouble y1,
                                   wxDouble x2, wxDouble y2,
                                   const wxGraphicsGradientStops& stops)
    {
        delete m_gradient;
        m_gradient = new BGradientLinear(BPoint((float)x1, (float)y1),
                                         BPoint((float)x2, (float)y2));

        for (size_t i = 0; i < stops.GetCount(); ++i)
        {
            const wxGraphicsGradientStop& stop = stops.Item(i);
            m_gradient->AddColor(wxToHaikuColor(stop.GetColour()),
                                 255.0f * stop.GetPosition());
        }
    }

    void CreateRadialGradientBrush(wxDouble sx, wxDouble sy,
                                   wxDouble ex,
                                   wxDouble ey,
                                   wxDouble radius,
                                   const wxGraphicsGradientStops& stops)
    {
        delete m_gradient;
        m_gradient = new BGradientRadial(BPoint((float)ex,(float)ey),(float)radius);
        m_focalGradient = sx != ex || sy != ey;
        m_focusX=sx; m_focusY=sy; m_centreX=ex; m_centreY=ey; m_radius=radius;
        m_stops=stops;

        for (size_t i = 0; i < stops.GetCount(); ++i)
        {
            const wxGraphicsGradientStop& stop = stops.Item(i);
            m_gradient->AddColor(wxToHaikuColor(stop.GetColour()),
                                 255.0f * stop.GetPosition());
        }
    }

    wxColour   m_colour;
    BGradient* m_gradient;
    wxBrushStyle m_style;
    wxBitmap m_stipple;
    wxGraphicsMatrix m_matrix;
    bool m_focalGradient=false;
    double m_focusX=0,m_focusY=0,m_centreX=0,m_centreY=0,m_radius=0;
    wxGraphicsGradientStops m_stops;

private:
    wxDECLARE_NO_COPY_CLASS(wxHaikuBrushData);
};

class wxHaikuFontData : public wxGraphicsObjectRefData
{
public:
    wxHaikuFontData(wxGraphicsRenderer* renderer,
                    const wxFont& font,
                    const wxColour& col)
        : wxGraphicsObjectRefData(renderer),
          m_font(font),
          m_colour(col)
    {
        m_native = font.GetBFont();
    }

    wxHaikuFontData(wxGraphicsRenderer* renderer,
                    double px,
                    const wxString& facename,
                    int flags,
                    const wxColour& col)
        : wxGraphicsObjectRefData(renderer),
          m_colour(col)
    {
        m_font = wxFont(static_cast<int>(px),
                        wxFONTFAMILY_DEFAULT,
                        (flags & wxFONTFLAG_ITALIC)
                            ? wxFONTSTYLE_ITALIC
                            : wxFONTSTYLE_NORMAL,
                        (flags & wxFONTFLAG_BOLD)
                            ? wxFONTWEIGHT_BOLD
                            : wxFONTWEIGHT_NORMAL,
                        (flags & wxFONTFLAG_UNDERLINED) != 0,
                        facename);
        m_native = m_font.GetBFont();
        m_native.SetSize(px);
        if (flags & wxFONTFLAG_STRIKETHROUGH) m_native.SetFace(m_native.Face() | B_STRIKEOUT_FACE);
    }

    wxFont   m_font;
    BFont    m_native;
    wxColour m_colour;
};

class WXDLLIMPEXP_CORE wxHaikuMatrixData : public wxGraphicsMatrixData
{
public:
    explicit wxHaikuMatrixData(wxGraphicsRenderer* renderer)
        : wxGraphicsMatrixData(renderer),
          m_transform(new BAffineTransform())
    {
    }

    wxHaikuMatrixData(wxGraphicsRenderer* renderer,
                      const BAffineTransform& transform)
        : wxGraphicsMatrixData(renderer),
          m_transform(new BAffineTransform(transform))
    {
    }

    ~wxHaikuMatrixData() override
    {
        delete m_transform;
    }

    wxGraphicsObjectRefData* Clone() const override
    {
        wxHaikuMatrixData* newMatrix = new wxHaikuMatrixData(m_renderer);
        *newMatrix->m_transform = *m_transform;
        return newMatrix;
    }

    void Concat(const wxGraphicsMatrixData* t) override
    {
        const wxHaikuMatrixData* rhs =
            static_cast<const wxHaikuMatrixData*>(t);

        *m_transform = m_transform->Multiply(*rhs->m_transform);
    }

    void Set(wxDouble a = 1.0, wxDouble b = 0.0,
             wxDouble c = 0.0, wxDouble d = 1.0,
             wxDouble tx = 0.0, wxDouble ty = 0.0) override
    {
        *m_transform = BAffineTransform(a, b, c, d, tx, ty);
    }

    void Get(wxDouble* a, wxDouble* b,
             wxDouble* c, wxDouble* d,
             wxDouble* tx, wxDouble* ty) const override
    {
        double x1 = 1.0, y1 = 0.0;
        double x2 = 0.0, y2 = 1.0;
        double x0 = 0.0, y0 = 0.0;

        m_transform->Apply(&x1, &y1);
        m_transform->Apply(&x2, &y2);
        m_transform->Apply(&x0, &y0);

        if (a)  *a  = x1 - x0;
        if (b)  *b  = y1 - y0;
        if (c)  *c  = x2 - x0;
        if (d)  *d  = y2 - y0;
        if (tx) *tx = x0;
        if (ty) *ty = y0;
    }

    void Invert() override
    {
        m_transform->Invert();
    }

    bool IsEqual(const wxGraphicsMatrixData* t) const override
    {
        const wxHaikuMatrixData* rhs =
            static_cast<const wxHaikuMatrixData*>(t);

        return *m_transform == *rhs->m_transform;
    }

    bool IsIdentity() const override
    {
        return m_transform->IsIdentity();
    }

    void Translate(wxDouble dx, wxDouble dy) override
    {
        m_transform->PreTranslateBy(dx, dy);
    }

    void Scale(wxDouble xScale, wxDouble yScale) override
    {
        m_transform->PreScaleBy(xScale, yScale);
    }

    void Rotate(wxDouble angle) override
    {
        m_transform->PreRotateBy(angle);
    }

    void TransformPoint(wxDouble* x, wxDouble* y) const override
    {
        m_transform->Apply(x, y);
    }

    void TransformDistance(wxDouble* dx, wxDouble* dy) const override
    {
        double ox = 0.0, oy = 0.0;
        double x  = *dx;
        double y  = *dy;

        m_transform->Apply(&x, &y);
        m_transform->Apply(&ox, &oy);

        *dx = x - ox;
        *dy = y - oy;
    }

    void* GetNativeMatrix() const override
    {
        return static_cast<void*>(m_transform);
    }

    const BAffineTransform& GetBAffineTransform() const
    {
        return *m_transform;
    }

private:
    BAffineTransform* m_transform;

    wxDECLARE_NO_COPY_CLASS(wxHaikuMatrixData);
};

class wxHaikuShapeTransformer : public BShapeIterator
{
public:
    wxHaikuShapeTransformer(BShape& target, const wxHaikuMatrixData& matrix)
        : m_target(target), m_matrix(matrix)
    {
    }

    status_t IterateMoveTo(BPoint* point) override
    {
        BPoint copy=*point; Transform(copy);
        return m_target.MoveTo(copy);
    }

    status_t IterateLineTo(int32 count, BPoint* points) override
    {
        for (int32 i = 0; i < count; ++i)
        {
            BPoint copy=points[i]; Transform(copy);
            const status_t status = m_target.LineTo(copy);
            if (status != B_OK)
                return status;
        }
        return B_OK;
    }

    status_t IterateBezierTo(int32 count, BPoint* points) override
    {
        for (int32 i = 0; i < count; ++i)
        {
            BPoint controls[3]={points[i*3],points[i*3+1],points[i*3+2]};
            Transform(controls[0]);
            Transform(controls[1]);
            Transform(controls[2]);
            const status_t status = m_target.BezierTo(controls);
            if (status != B_OK)
                return status;
        }
        return B_OK;
    }

    status_t IterateClose() override
    {
        return m_target.Close();
    }

    status_t IterateArcTo(float& rx, float& ry, float& angle,
                          bool largeArc, bool counterClockWise,
                          BPoint& point) override
    {
        // BShape represents elliptical arcs parametrically. Preserve their
        // parameters for translation/rotation and transform the endpoint;
        // the general affine case is still better represented by curves.
        BPoint copy=point; Transform(copy);
        return m_target.ArcTo(rx, ry, angle, largeArc, counterClockWise, copy);
    }

private:
    void Transform(BPoint& point) const
    {
        double x = point.x, y = point.y;
        m_matrix.TransformPoint(&x, &y);
        point.Set((float)x, (float)y);
    }

    BShape& m_target;
    const wxHaikuMatrixData& m_matrix;
};

// ============================================================================
// Path data
// ============================================================================


class wxHaikuFlattenedPath : public BShapeIterator
{
public:
    std::vector<std::vector<BPoint>> contours;
    status_t IterateMoveTo(BPoint* point) override
    {
        contours.push_back({*point});
        return B_OK;
    }
    status_t IterateLineTo(int32 count, BPoint* points) override
    {
        for (int32 i = 0; i < count; ++i) Add(points[i]);
        return B_OK;
    }
    status_t IterateBezierTo(int32 count, BPoint* points) override
    {
        for (int32 i = 0; i < count; ++i)
        {
            if (contours.empty()) contours.push_back({B_ORIGIN});
            Flatten(contours.back().back(), points[i*3], points[i*3+1], points[i*3+2], 0);
        }
        return B_OK;
    }
    status_t IterateClose() override
    {
        if (!contours.empty() && !contours.back().empty())
            Add(contours.back().front());
        return B_OK;
    }
    bool Contains(double x, double y, wxPolygonFillMode rule) const
    {
        int winding = 0;
        for (const auto& contour : contours)
        {
            if (contour.size() < 3) continue;
            for (size_t i=0; i<contour.size(); ++i)
            {
                const BPoint a=contour[i], b=contour[(i+1)%contour.size()];
                const double cross=(b.x-a.x)*(y-a.y)-(x-a.x)*(b.y-a.y);
                if (a.y <= y && b.y > y && cross > 0) ++winding;
                else if (a.y > y && b.y <= y && cross < 0) --winding;
            }
        }
        return rule == wxODDEVEN_RULE ? (winding % 2 != 0) : winding != 0;
    }
private:
    void Add(BPoint point)
    {
        if (contours.empty()) contours.push_back({B_ORIGIN});
        contours.back().push_back(point);
    }
    void Flatten(BPoint a, BPoint b, BPoint c, BPoint d, unsigned depth)
    {
        const double dx=d.x-a.x, dy=d.y-a.y;
        const double length=std::hypot(dx,dy);
        const double error=length>0 ? std::max(std::fabs(dx*(a.y-b.y)-dy*(a.x-b.x)),
            std::fabs(dx*(a.y-c.y)-dy*(a.x-c.x)))/length :
            std::max(std::hypot(b.x-a.x,b.y-a.y),std::hypot(c.x-a.x,c.y-a.y));
        if (error <= 0.01 || depth >= 18) { Add(d); return; }
        const BPoint ab((a.x+b.x)*0.5f,(a.y+b.y)*0.5f), bc((b.x+c.x)*0.5f,(b.y+c.y)*0.5f), cd((c.x+d.x)*0.5f,(c.y+d.y)*0.5f);
        const BPoint abc((ab.x+bc.x)*0.5f,(ab.y+bc.y)*0.5f), bcd((bc.x+cd.x)*0.5f,(bc.y+cd.y)*0.5f), mid((abc.x+bcd.x)*0.5f,(abc.y+bcd.y)*0.5f);
        Flatten(a,ab,abc,mid,depth+1); Flatten(mid,bcd,cd,d,depth+1);
    }
};

class wxHaikuGraphicsPathData : public wxGraphicsPathData
{
public:
    explicit wxHaikuGraphicsPathData(wxGraphicsRenderer* renderer)
        : wxGraphicsPathData(renderer)
    {
    }

    wxGraphicsObjectRefData* Clone() const override
    {
        wxHaikuGraphicsPathData* p = new wxHaikuGraphicsPathData(m_renderer);
        p->m_shape = m_shape;
        p->m_box = m_box;
        p->m_hasCurrent = m_hasCurrent;
        p->m_current = m_current;
        p->m_subpathStart = m_subpathStart;
        p->m_hasGeometry = m_hasGeometry;
        return p;
    }

    void AddPath(const wxGraphicsPathData* path) override
    {
        const wxHaikuGraphicsPathData* hp =
            static_cast<const wxHaikuGraphicsPathData*>(path);

        if (!hp || !hp->m_hasGeometry)
            return;

        m_shape.AddShape(&hp->m_shape);

        IncludePoint(hp->m_box.m_x, hp->m_box.m_y);
        IncludePoint(hp->m_box.m_x + hp->m_box.m_width,
                     hp->m_box.m_y + hp->m_box.m_height);

        m_current = hp->m_current;
        m_subpathStart = hp->m_subpathStart;
        m_hasCurrent = hp->m_hasCurrent;
    }

    void MoveToPoint(wxDouble x, wxDouble y) override
    {
        m_shape.MoveTo(BPoint((float)x, (float)y));
        m_current = wxPoint2DDouble(x, y);
        m_subpathStart = m_current;
        m_hasCurrent = true;
    }

    void AddLineToPoint(wxDouble x, wxDouble y) override
    {
        if (!m_hasCurrent)
        {
            MoveToPoint(x, y);
            return;
        }

        m_shape.LineTo(BPoint((float)x, (float)y));
        IncludePoint(m_current.m_x,m_current.m_y);
        IncludePoint(x, y);
        m_current = wxPoint2DDouble(x, y);
    }

    void AddCurveToPoint(wxDouble cx1, wxDouble cy1,
                         wxDouble cx2, wxDouble cy2,
                         wxDouble x, wxDouble y) override
    {
        if (!m_hasCurrent) MoveToPoint(cx1, cy1);
        IncludePoint(m_current.m_x,m_current.m_y);
        m_shape.BezierTo(BPoint((float)cx1, (float)cy1),
                         BPoint((float)cx2, (float)cy2),
                         BPoint((float)x, (float)y));
        IncludePoint(cx1, cy1);
        IncludePoint(cx2, cy2);
        IncludePoint(x, y);

        m_current = wxPoint2DDouble(x, y);
        m_hasCurrent = true;
    }

    void AddQuadCurveToPoint(wxDouble cx, wxDouble cy,
                             wxDouble x, wxDouble y) override
    {
        if (!m_hasCurrent) MoveToPoint(cx,cy);
        IncludePoint(m_current.m_x,m_current.m_y);

        const wxDouble c1x = m_current.m_x + 2.0 * (cx - m_current.m_x) / 3.0;
        const wxDouble c1y = m_current.m_y + 2.0 * (cy - m_current.m_y) / 3.0;
        const wxDouble c2x = x + 2.0 * (cx - x) / 3.0;
        const wxDouble c2y = y + 2.0 * (cy - y) / 3.0;
        m_shape.BezierTo(BPoint((float)c1x, (float)c1y),
                         BPoint((float)c2x, (float)c2y),
                         BPoint((float)x, (float)y));
        IncludePoint(cx, cy);
        IncludePoint(x, y);

        m_current = wxPoint2DDouble(x, y);
        m_hasCurrent = true;
    }

    void AddArc(wxDouble x, wxDouble y, wxDouble r,
                wxDouble startAngle, wxDouble endAngle, bool clockwise) override
    {
        if (r < 0 || !std::isfinite(r) || !std::isfinite(startAngle) || !std::isfinite(endAngle))
            return;
        const double tau=2.0*M_PI;
        double sweep=endAngle-startAngle;
        if (clockwise) {
            if (sweep >= tau) sweep=tau;
            else { sweep=std::fmod(sweep,tau); if (sweep<0) sweep+=tau; }
        } else {
            if (sweep <= -tau) sweep=-tau;
            else { sweep=std::fmod(sweep,tau); if (sweep>0) sweep-=tau; }
        }
        const double sx=x+std::cos(startAngle)*r, sy=y+std::sin(startAngle)*r;
        if (!m_hasCurrent) MoveToPoint(sx,sy);
        else AddLineToPoint(sx,sy);
        const int segments=std::max(1,(int)std::ceil(std::fabs(sweep)/(M_PI/2.0)));
        const double step=sweep/segments;
        double angle=startAngle;
        for (int i=0;i<segments;++i) {
            const double next=angle+step, k=4.0/3.0*std::tan(step/4.0);
            AddCurveToPoint(x+r*(std::cos(angle)-k*std::sin(angle)),
                           y+r*(std::sin(angle)+k*std::cos(angle)),
                           x+r*(std::cos(next)+k*std::sin(next)),
                           y+r*(std::sin(next)-k*std::cos(next)),
                           x+r*std::cos(next),y+r*std::sin(next));
            angle=next;
        }
    }

    void AddArcToPoint(wxDouble x1, wxDouble y1, wxDouble x2, wxDouble y2, wxDouble r) override
    {
        if (!m_hasCurrent) MoveToPoint(0,0);
        wxGraphicsPathData::AddArcToPoint(x1,y1,x2,y2,r);
    }

    void AddRectangle(wxDouble x, wxDouble y, wxDouble w, wxDouble h) override
    {
        wxGraphicsPathData::AddRectangle(x,y,w,h);
    }

    void AddCircle(wxDouble x, wxDouble y, wxDouble r) override
    {
        MoveToPoint(x+r,y); AddArc(x,y,r,0,2*M_PI,true); CloseSubpath();
    }

    void AddEllipse(wxDouble x, wxDouble y, wxDouble w, wxDouble h) override
    {
        wxGraphicsPathData::AddEllipse(x,y,w,h);
    }

    void AddRoundedRectangle(wxDouble x, wxDouble y, wxDouble w, wxDouble h, wxDouble radius) override
    {
        wxGraphicsPathData::AddRoundedRectangle(x,y,w,h,std::fabs(radius));
    }

    void CloseSubpath() override
    {
        if (m_hasCurrent) { m_shape.Close(); m_current = m_subpathStart; }
    }

    void GetCurrentPoint(wxDouble* x, wxDouble* y) const override
    {
        if (x)
            *x = m_hasCurrent ? m_current.m_x : 0.0;

        if (y)
            *y = m_hasCurrent ? m_current.m_y : 0.0;
    }

    void Transform(const wxGraphicsMatrixData* matrix) override
    {
        if (!m_hasGeometry)
            return;

        const wxHaikuMatrixData* m =
            static_cast<const wxHaikuMatrixData*>(matrix);
        if (!m)
            return;

        BShape transformed;
        wxHaikuShapeTransformer transformer(transformed, *m);
        if (transformer.Iterate(&m_shape) != B_OK)
            return;

        m_shape = transformed;
        const BRect bounds = m_shape.Bounds();
        m_box = wxRect2DDouble(bounds.left, bounds.top,
                               bounds.Width(), bounds.Height());

        if (m_hasCurrent) {
            m->TransformPoint(&m_current.m_x, &m_current.m_y);
            m->TransformPoint(&m_subpathStart.m_x, &m_subpathStart.m_y);
        }
    }

    void* GetNativePath() const override
    {
        return const_cast<BShape*>(&m_shape);
    }

    void UnGetNativePath(void* p) const override
    {
        wxUnusedVar(p);
    }

    void GetBox(wxDouble* x, wxDouble* y,
                wxDouble* w, wxDouble* h) const override
    {
        if (x)
            *x = m_hasGeometry ? m_box.m_x : 0.0;

        if (y)
            *y = m_hasGeometry ? m_box.m_y : 0.0;

        if (w)
            *w = m_hasGeometry ? m_box.m_width : 0.0;

        if (h)
            *h = m_hasGeometry ? m_box.m_height : 0.0;
    }

    wxRect2DDouble GetBoxRect() const
    {
        return m_hasGeometry ? m_box : wxRect2DDouble();
    }

    BShape* GetBShape()
    {
        return &m_shape;
    }

    bool Contains(wxDouble x, wxDouble y, wxPolygonFillMode fillStyle) const override
    {
        if (!m_hasGeometry) return false;
        wxHaikuFlattenedPath flattened;
        return flattened.Iterate(const_cast<BShape*>(&m_shape)) == B_OK &&
               flattened.Contains(x,y,fillStyle);
    }

    bool HasGeometry() const
    {
        return m_hasGeometry;
    }

private:
    void IncludePoint(wxDouble x, wxDouble y)
    {
        if (!m_hasGeometry)
        {
            m_box = wxRect2DDouble(x, y, 0.0, 0.0);
            m_hasGeometry = true;
            return;
        }

        const wxDouble left   = std::min(m_box.m_x, x);
        const wxDouble top    = std::min(m_box.m_y, y);
        const wxDouble right  = std::max(m_box.m_x + m_box.m_width, x);
        const wxDouble bottom = std::max(m_box.m_y + m_box.m_height, y);

        m_box = wxRect2DDouble(left, top, right - left, bottom - top);
    }

    void IncludeRect(wxDouble x, wxDouble y, wxDouble w, wxDouble h)
    {
        IncludePoint(x, y);
        IncludePoint(x + w, y + h);
    }

private:
    BShape          m_shape;
    wxRect2DDouble  m_box;
    wxPoint2DDouble m_current;
    wxPoint2DDouble m_subpathStart;
    bool            m_hasCurrent = false;
    bool            m_hasGeometry = false;
};

// ============================================================================
// Graphics context
// ============================================================================

class WXDLLIMPEXP_CORE wxHaikuGraphicsContext : public wxGraphicsContext
{
public:
    wxHaikuGraphicsContext(wxGraphicsRenderer* renderer,
                           BView* view,
                           BBitmap* ownedBitmap = nullptr,
                           bool bitmapLocked = false,
                           wxImage* image = nullptr)
        : wxGraphicsContext(renderer),
          m_view(view),
          m_ownedBitmap(ownedBitmap),
          m_bitmapLocked(bitmapLocked),
          m_image(image)
    {
        if (!m_ownedBitmap)
        {
            std::lock_guard<std::recursive_mutex> lock(ContextMutex());
            BorrowedContexts().insert(this);
        }
        if (!m_view)
            return;

        wxHaikuAutoViewLock lock(m_view);
        if (lock.ok()) {
            m_view->GetClippingRegion(&m_initialClip);
            m_clip=m_initialClip;
            m_baseTransform=m_view->Transform();
            m_view->PushState();
            m_width=m_view->Bounds().Width()+1; m_height=m_view->Bounds().Height()+1;
            m_view->SetDrawingMode(B_OP_ALPHA);
            m_view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_COMPOSITE_SOURCE_OVER);
            m_savedNativeState = true;
            m_composition = wxCOMPOSITION_OVER;
            m_antialias = wxANTIALIAS_DEFAULT;
            m_interpolation = wxINTERPOLATION_DEFAULT;
        }
    }

    static std::set<wxHaikuGraphicsContext*>& BorrowedContexts()
    {
        static auto* contexts = new std::set<wxHaikuGraphicsContext*>;
        return *contexts;
    }
    static std::recursive_mutex& ContextMutex()
    {
        static auto* mutex = new std::recursive_mutex;
        return *mutex;
    }
    static void DetachBorrowedView(BView* view)
    {
        std::lock_guard<std::recursive_mutex> lock(ContextMutex());
        // Contexts can outlive memory-DC selection. Restore native state while
        // the owner still holds a live view, then stop using that view.
        for (auto* context : BorrowedContexts())
            if (context->m_view == view)
            {
                wxHaikuAutoViewLock nativeLock(view);
                if (nativeLock.ok())
                {
                    while (context->m_layerDepth) { view->EndLayer(); --context->m_layerDepth; }
                    while (!context->m_states.empty()) { view->PopState(); context->m_states.pop_back(); }
                    view->Sync();
                    if (context->m_savedNativeState) view->PopState();
                }
                context->m_savedNativeState = false;
                context->m_view = nullptr;
            }
    }
    ~wxHaikuGraphicsContext() override
    {
        std::lock_guard<std::recursive_mutex> contextLock(ContextMutex());
        BorrowedContexts().erase(this);
        if (m_view)
        {
            wxHaikuAutoViewLock lock(m_view);
            if (lock.ok()) {
                while (m_layerDepth) { m_view->EndLayer(); --m_layerDepth; }
                while (!m_states.empty()) { m_view->PopState(); m_states.pop_back(); }
                m_view->Sync();
                if (m_savedNativeState) m_view->PopState();
            }
        }

        if (m_ownedBitmap)
        {
            CopyBitmapToImage();

            if (m_view)
                m_ownedBitmap->RemoveChild(m_view);

            if (m_bitmapLocked)
                m_ownedBitmap->Unlock();

            delete m_view;
            delete m_ownedBitmap;
        }
    }

    void ConcatTransform(const wxGraphicsMatrix& matrix) override
    {
        const wxHaikuMatrixData* md =
            static_cast<const wxHaikuMatrixData*>(matrix.GetRefData());

        if (!m_view || !md)
            return;

        wxHaikuAutoViewLock lock(m_view);
        if (!lock.ok())
            return;

        BAffineTransform current = m_view->Transform();
        current.Multiply(md->GetBAffineTransform());
        m_view->SetTransform(current);
    }

    void SetTransform(const wxGraphicsMatrix& matrix) override
    {
        const wxHaikuMatrixData* md =
            static_cast<const wxHaikuMatrixData*>(matrix.GetRefData());

        if (!m_view || !md)
            return;

        wxHaikuAutoViewLock lock(m_view);
        if (!lock.ok())
            return;

        m_view->SetTransform(md->GetBAffineTransform());
    }

    wxGraphicsMatrix GetTransform() const override
    {
        if (!m_view)
            return GetRenderer()->CreateMatrix();

        wxHaikuAutoViewLock lock(m_view);
        if (!lock.ok())
            return GetRenderer()->CreateMatrix();

        wxGraphicsMatrix matrix;
        matrix.SetRefData(new wxHaikuMatrixData(GetRenderer(),
                                                m_view->Transform()));
        return matrix;
    }

    void PushState() override
    {
        if (!m_view)
            return;

        wxHaikuAutoViewLock lock(m_view);
        if (lock.ok()) {
            const BAffineTransform transform=m_view->Transform();
            m_states.push_back({m_pen,m_brush,m_font,m_composition,m_antialias,m_interpolation,m_clip,transform});
            // Keep the inherited DC transform in the outer state only.
            m_view->SetTransform(BAffineTransform());
            // Native child states intersect their parent's clip. Keep that
            // parent neutral so ResetClip() can expand back to the initial
            // clip inside a pushed wx state.
            m_view->ConstrainClippingRegion(&m_initialClip);
            m_view->PushState();
            m_view->SetTransform(transform);
            m_view->ConstrainClippingRegion(&m_clip);
        }
    }

    void PopState() override
    {
        if (!m_view)
            return;

        wxHaikuAutoViewLock lock(m_view);
        if (lock.ok() && !m_states.empty()) {
            const State state=m_states.back(); m_states.pop_back();
            m_view->PopState();
            m_view->SetTransform(state.transform);
            m_pen=state.pen; m_brush=state.brush; m_font=state.font;
            m_composition=state.composition; m_antialias=state.antialias;
            m_interpolation=state.interpolation; m_clip=state.clip;
            m_view->ConstrainClippingRegion(&m_clip);
        }
    }

    void Clip(const wxRegion& region) override
    {
        wxHaikuAutoViewLock lock(m_view);
        if(!lock.ok()) return;
        BRegion clipping;
        for(wxRegionIterator it(region);it;++it) {
            const wxRect rect=it.GetRect();
            RasterizeClipRectangle(rect.x,rect.y,rect.width,rect.height,clipping);
        }
        m_clip.IntersectWith(&clipping); m_view->ConstrainClippingRegion(&m_clip);
    }
    void Clip(wxDouble x,wxDouble y,wxDouble w,wxDouble h) override
    {
        wxHaikuAutoViewLock lock(m_view);
        if(!lock.ok()) return;
        BRegion clipping; RasterizeClipRectangle(x,y,w,h,clipping);
        m_clip.IntersectWith(&clipping); m_view->ConstrainClippingRegion(&m_clip);
    }
    void ResetClip() override
    {
        wxHaikuAutoViewLock lock(m_view);
        if(lock.ok()) { m_clip=m_initialClip; m_view->ConstrainClippingRegion(&m_clip); }
    }

    void GetClipBox(wxDouble* x, wxDouble* y,
                    wxDouble* w, wxDouble* h) override
    {
        if (!m_view)
            return;

        wxHaikuAutoViewLock lock(m_view);
        if (!lock.ok())
            return;

        const BRegion& clipping=m_clip;
        const BRect b = clipping.CountRects() ? clipping.Frame()
                                              : BRect();

        double left=0,top=0,right=0,bottom=0;
        if (clipping.CountRects()) {
            BAffineTransform inverse=m_baseTransform;
            inverse.Multiply(m_view->Transform()); inverse.Invert();
            double px[4]={b.left,b.right+1.0,b.right+1.0,b.left};
            double py[4]={b.top,b.top,b.bottom+1.0,b.bottom+1.0};
            for(int i=0;i<4;++i) {
                inverse.Apply(&px[i],&py[i]);
                if(i==0) { left=right=px[i];top=bottom=py[i]; }
                else { left=std::min(left,px[i]);right=std::max(right,px[i]);
                       top=std::min(top,py[i]);bottom=std::max(bottom,py[i]); }
            }
        }
        if(x) *x=left;
        if(y) *y=top;
        if(w) *w=right-left;
        if(h) *h=bottom-top;
    }

    void SetPen(const wxGraphicsPen& pen) override { m_pen=pen; }
    void SetBrush(const wxGraphicsBrush& brush) override { m_brush=brush; }
    void SetFont(const wxGraphicsFont& font) override
    {
        m_font=font;
        wxHaikuAutoViewLock lock(m_view);
        if (lock.ok()) ApplyFont();
    }

    void StrokeLine(wxDouble x1,wxDouble y1,wxDouble x2,wxDouble y2) override
    { wxGraphicsContext::StrokeLine(x1,y1,x2,y2); }
    void StrokeLines(size_t n,const wxPoint2DDouble* points) override
    { if (n>=2 && points) wxGraphicsContext::StrokeLines(n,points); }
    void StrokeLines(size_t n,const wxPoint2DDouble* begin,const wxPoint2DDouble* end) override
    { if (n && begin && end) wxGraphicsContext::StrokeLines(n,begin,end); }

    void DoDrawText(const wxString& str,
                    wxDouble x,
                    wxDouble y) override
    {
        if (!m_view)
            return;

        wxHaikuAutoViewLock lock(m_view);
        if (!lock.ok())
            return;

        if (!ApplyFont() || str.empty()) return;
        font_height height; m_view->GetFontHeight(&height);
        m_view->MovePenTo((float)x, (float)(y+height.ascent));
        m_view->DrawString(str.utf8_str());
    }

    void DoDrawRotatedText(const wxString& str, wxDouble x, wxDouble y, wxDouble angle) override
    {
        wxHaikuAutoViewLock lock(m_view);
        if (!lock.ok() || !ApplyFont() || str.empty()) return;
        m_view->PushState();
        m_view->TranslateBy(x,y); m_view->RotateBy(-angle);
        font_height height; m_view->GetFontHeight(&height);
        m_view->MovePenTo(0,height.ascent); m_view->DrawString(str.utf8_str());
        m_view->PopState();
    }

    void Translate(wxDouble dx, wxDouble dy) override
    {
        if (!m_view)
            return;

        wxHaikuAutoViewLock lock(m_view);
        if (lock.ok())
            m_view->TranslateBy(dx, dy);
    }

    void Scale(wxDouble xscale, wxDouble yscale) override
    {
        if (!m_view)
            return;

        wxHaikuAutoViewLock lock(m_view);
        if (lock.ok())
            m_view->ScaleBy(xscale, yscale);
    }

    void Rotate(wxDouble angle) override
    {
        if (!m_view)
            return;

        wxHaikuAutoViewLock lock(m_view);
        if (lock.ok())
            m_view->RotateBy(angle);
    }

    void StrokePath(const wxGraphicsPath& path) override
    {
        if (!m_view)
            return;

        wxHaikuGraphicsPathData* pdata =
            static_cast<wxHaikuGraphicsPathData*>(path.GetRefData());

        if (!pdata || !pdata->HasGeometry())
            return;

        wxHaikuAutoViewLock lock(m_view);
        if (!lock.ok())
            return;

        if (!ApplyPen()) return;
        auto* pen=static_cast<wxHaikuPenData*>(m_pen.GetRefData());
        if (m_antialias==wxANTIALIAS_NONE && DrawAliasedPath(path,true,wxODDEVEN_RULE)) return;
        if (pen->m_dashes.empty()) {
            if (pen->m_gradient) m_view->StrokeShape(pdata->GetBShape(),*pen->m_gradient);
            else m_view->StrokeShape(pdata->GetBShape());
        }
        else {
            wxHaikuFlattenedPath flat; flat.Iterate(pdata->GetBShape());
            BShape dashed;
            for (const auto& contour:flat.contours) {
                size_t index=0; double remaining=pen->m_dashes[0]; bool open=false;
                for(size_t i=1;i<contour.size();++i) {
                    const BPoint a=contour[i-1],b=contour[i];
                    const double length=std::hypot(b.x-a.x,b.y-a.y);
                    double offset=0;
                    while(offset<length) {
                        const double amount=std::min(remaining,length-offset);
                        const BPoint from(a.x+(b.x-a.x)*offset/length,a.y+(b.y-a.y)*offset/length);
                        const BPoint to(a.x+(b.x-a.x)*(offset+amount)/length,a.y+(b.y-a.y)*(offset+amount)/length);
                        if(index%2==0) { if(!open) dashed.MoveTo(from); dashed.LineTo(to); open=true; }
                        else open=false;
                        offset+=amount; remaining-=amount;
                        if(remaining<=1e-9) { index=(index+1)%pen->m_dashes.size(); remaining=pen->m_dashes[index]; open=false; }
                    }
                }
            }
            if(pen->m_gradient) m_view->StrokeShape(&dashed,*pen->m_gradient);
            else m_view->StrokeShape(&dashed);
        }
    }

    void FillPath(const wxGraphicsPath& path,
                  wxPolygonFillMode fillStyle = wxODDEVEN_RULE) override
    {
        if (!m_view)
            return;

        wxHaikuGraphicsPathData* pdata =
            static_cast<wxHaikuGraphicsPathData*>(path.GetRefData());

        if (!pdata || !pdata->HasGeometry())
            return;

        wxHaikuAutoViewLock lock(m_view);
        if (!lock.ok())
            return;

        wxHaikuBrushData* brush =
            m_brush.IsNull() ? nullptr :
            static_cast<wxHaikuBrushData*>(m_brush.GetRefData());
        if (!brush || brush->m_style == wxBRUSHSTYLE_TRANSPARENT || m_composition == wxCOMPOSITION_DEST) return;
        if (m_antialias==wxANTIALIAS_NONE && DrawAliasedPath(path,false,fillStyle)) return;
        m_view->SetFillRule(fillStyle == wxODDEVEN_RULE ? B_EVEN_ODD : B_NONZERO);
        if (brush->m_focalGradient) {
            DrawFocalGradient(*brush,pdata->GetBShape());
        } else if (brush->m_gradient) {
            // Express the shape in gradient coordinates, then restore its
            // geometry with the view transform. This also handles shear and
            // nonuniformly scaled radial gradients.
            if (!brush->m_matrix.IsNull() && !brush->m_matrix.IsIdentity()) {
                const auto* matrix=static_cast<const wxHaikuMatrixData*>(brush->m_matrix.GetRefData());
                BAffineTransform inverse=matrix->GetBAffineTransform(); inverse.Invert();
                wxHaikuMatrixData inverseData(GetRenderer(),inverse);
                BShape shape; wxHaikuShapeTransformer transformer(shape,inverseData);
                if (transformer.Iterate(pdata->GetBShape()) != B_OK) return;
                m_view->PushState();
                BAffineTransform combined=m_view->Transform(); combined.Multiply(matrix->GetBAffineTransform());
                m_view->SetTransform(combined); m_view->FillShape(&shape,*brush->m_gradient);
                m_view->PopState();
            } else m_view->FillShape(pdata->GetBShape(), *brush->m_gradient);
        } else if (brush->m_stipple.IsOk()) {
            BBitmap* bitmap=brush->m_stipple.GetBBitmap();
            if (!bitmap) return;
            const BRect box=pdata->GetBShape()->Bounds();
            const int width=brush->m_stipple.GetWidth(),height=brush->m_stipple.GetHeight();
            m_view->PushState(); m_view->ClipToShape(pdata->GetBShape());
            for (double y=std::floor(box.top/height)*height;y<=box.bottom;y+=height)
                for(double x=std::floor(box.left/width)*width;x<=box.right;x+=width)
                    m_view->DrawBitmap(bitmap,BPoint(x,y));
            m_view->PopState();
        } else {
            m_view->SetHighColor(wxToHaikuColor(brush->m_colour));
            m_view->SetLowColor(0,0,0,0);
            pattern hatch=B_SOLID_HIGH;
            for(int y=0;y<8;++y) {
                uint8 row=0;
                for(int x=0;x<8;++x) {
                    bool on=true;
                    switch(brush->m_style) {
                        case wxBRUSHSTYLE_HORIZONTAL_HATCH:on=y==0;break;
                        case wxBRUSHSTYLE_VERTICAL_HATCH:on=x==0;break;
                        case wxBRUSHSTYLE_CROSS_HATCH:on=x==0||y==0;break;
                        case wxBRUSHSTYLE_FDIAGONAL_HATCH:on=(x+y)%8==0;break;
                        case wxBRUSHSTYLE_BDIAGONAL_HATCH:on=(x-y+8)%8==0;break;
                        case wxBRUSHSTYLE_CROSSDIAG_HATCH:on=(x+y)%8==0||(x-y+8)%8==0;break;
                        default:break;
                    }
                    if(on) row|=uint8(0x80>>x);
                }
                hatch.data[y]=row;
            }
            m_view->FillShape(pdata->GetBShape(),hatch);
        }
    }

    void DrawBitmap(const wxGraphicsBitmap& bmp,
                    wxDouble x,
                    wxDouble y,
                    wxDouble w,
                    wxDouble h)
    {
        if (!m_view)
            return;

        const wxGraphicsBitmapData* data = bmp.GetBitmapData();

        if (!data)
            return;

        const wxHaikuBitmapData* hdata =
            static_cast<const wxHaikuBitmapData*>(data);

        DrawBitmap(hdata->GetBitmap(), x, y, w, h);
    }

    void DrawBitmap(const wxBitmap& bmp,
                    wxDouble x,
                    wxDouble y,
                    wxDouble w,
                    wxDouble h)
    {
        if (!m_view || !bmp.IsOk() || !wxHaikuHasPositiveSize(w,h))
            return;

        if(bmp.GetMask()) {
            DrawBitmap(GetRenderer()->CreateBitmap(bmp),x,y,w,h); return;
        }
        BBitmap* hb = static_cast<BBitmap*>(bmp.GetBBitmap());

        if (!hb)
            return;

        wxHaikuAutoViewLock lock(m_view);
        if (!lock.ok())
            return;

        if (m_composition == wxCOMPOSITION_DEST) return;
        const BRect src = hb->Bounds();
        const BRect dst = wxToBRect(x, y, w, h);

        m_view->DrawBitmap(hb, src, dst,
            m_interpolation == wxINTERPOLATION_NONE ? 0 : B_FILTER_BITMAP_BILINEAR);
    }

    void DrawIcon(const wxIcon& icon,
                  wxDouble x,
                  wxDouble y,
                  wxDouble w,
                  wxDouble h) override
    {
        DrawBitmap(icon, x, y, w, h);
    }

    void GetTextExtent(const wxString& text,
                       wxDouble* width,
                       wxDouble* height,
                       wxDouble* descent,
                       wxDouble* externalLeading) const override
    {
        if (!m_view)
            return;

        wxHaikuAutoViewLock lock(m_view);
        if (!lock.ok())
            return;

        if (!m_font.IsNull()) {
            const auto* font=static_cast<const wxHaikuFontData*>(m_font.GetRefData());
            m_view->SetFont(&font->m_native);
        }
        font_height fh;
        m_view->GetFontHeight(&fh);

        if (width)
            *width = m_view->StringWidth(text.utf8_str());

        if (height)
            *height = text.empty() ? 0 : fh.ascent + fh.descent;

        if (descent)
            *descent = fh.descent;

        if (externalLeading)
            *externalLeading = fh.leading;
    }

    void GetPartialTextExtents(const wxString& text,
                               wxArrayDouble& widths) const override
    {
        widths.clear();

        if (!m_view)
            return;

        wxHaikuAutoViewLock lock(m_view);
        if (!lock.ok())
            return;

        if (!m_font.IsNull()) {
            const auto* font=static_cast<const wxHaikuFontData*>(m_font.GetRefData());
            m_view->SetFont(&font->m_native);
        }
        for (size_t i = 0; i < text.length(); ++i)
        {
            widths.push_back(
                m_view->StringWidth(text.substr(0, i + 1).utf8_str()));
        }
    }

    bool ShouldOffset() const override
    {
        return false;
    }

    bool SetAntialiasMode(wxAntialiasMode mode) override
    {
        if (mode!=wxANTIALIAS_DEFAULT && mode!=wxANTIALIAS_NONE) return false;
        m_antialias=mode; return true;
    }
    bool SetInterpolationQuality(wxInterpolationQuality quality) override
    {
        if (quality != wxINTERPOLATION_DEFAULT && quality != wxINTERPOLATION_NONE &&
            quality != wxINTERPOLATION_FAST && quality != wxINTERPOLATION_GOOD) return false;
        m_interpolation=quality; return true;
    }
    bool SetCompositionMode(wxCompositionMode op) override
    {
        // B_OP_COPY forces opaque alpha for vector fills. Several other native
        // Porter-Duff operators produce incorrectly scaled RGB on RGBA targets.
        // Advertise only the modes whose RGBA output matches wx semantics.
        if (op!=wxCOMPOSITION_OVER && op!=wxCOMPOSITION_CLEAR && op!=wxCOMPOSITION_DEST)
            return false;
        wxHaikuAutoViewLock lock(m_view);
        if (!lock.ok()) return false;
        m_view->SetDrawingMode(B_OP_ALPHA);
        m_view->SetBlendingMode(B_PIXEL_ALPHA,op==wxCOMPOSITION_CLEAR ?
                               B_ALPHA_COMPOSITE_CLEAR : B_ALPHA_COMPOSITE_SOURCE_OVER);
        m_composition=op; return true;
    }

    void BeginLayer(wxDouble opacity) override
    {
        wxHaikuAutoViewLock lock(m_view);
        if (lock.ok()) {
            m_view->BeginLayer((uint8)std::lround(std::max(0.0,std::min(1.0,opacity))*255));
            ++m_layerDepth;
        }
    }
    void EndLayer() override
    {
        wxHaikuAutoViewLock lock(m_view);
        if (lock.ok() && m_layerDepth) { m_view->EndLayer(); --m_layerDepth; }
    }

    void ClearRectangle(wxDouble x,
                        wxDouble y,
                        wxDouble w,
                        wxDouble h) override
    {
        if (!m_view || !wxHaikuHasPositiveSize(w,h))
            return;

        wxHaikuAutoViewLock lock(m_view);
        if (!lock.ok())
            return;

        m_view->PushState();
        m_view->SetDrawingMode(B_OP_ALPHA);
        m_view->SetBlendingMode(B_PIXEL_ALPHA,B_ALPHA_COMPOSITE_CLEAR);
        BShape shape; shape.MoveTo(BPoint(x,y)); shape.LineTo(BPoint(x+w,y));
        shape.LineTo(BPoint(x+w,y+h));shape.LineTo(BPoint(x,y+h));shape.Close();
        m_view->FillShape(&shape);
        m_view->PopState();
    }

    void Flush() override
    {
        if (!m_view)
            return;

        wxHaikuAutoViewLock lock(m_view);
        if (lock.ok())
            m_view->Sync();
    }

    void* GetNativeContext() override
    {
        return m_view;
    }

private:
    bool DrawAliasedPath(const wxGraphicsPath& path,bool stroke,wxPolygonFillMode rule)
    {
        // BView has no switch for binary vector coverage. Render colour and
        // opaque coverage separately, quantize only the coverage, then composite
        // once in device coordinates. This preserves gradients, dashes and alpha.
        if(!m_clip.CountRects()) return true;
        BView* original=m_view;
        const BAffineTransform base=m_baseTransform;
        const BAffineTransform local=original->Transform();
        BAffineTransform effective=base; effective.Multiply(local);
        const auto* data=static_cast<const wxHaikuGraphicsPathData*>(path.GetRefData());
        const BRect shape=const_cast<wxHaikuGraphicsPathData*>(data)->GetBShape()->Bounds();
        double px[4]={shape.left,shape.right,shape.right,shape.left};
        double py[4]={shape.top,shape.top,shape.bottom,shape.bottom};
        double minX=0,minY=0,maxX=0,maxY=0;
        for(int i=0;i<4;++i)
        {
            effective.Apply(&px[i],&py[i]);
            if(!std::isfinite(px[i]) || !std::isfinite(py[i])) return false;
            if(i==0) { minX=maxX=px[i];minY=maxY=py[i]; }
            else { minX=std::min(minX,px[i]);maxX=std::max(maxX,px[i]);
                   minY=std::min(minY,py[i]);maxY=std::max(maxY,py[i]); }
        }
        double padding=2;
        if(stroke)
        {
            const auto* pen=static_cast<wxHaikuPenData*>(m_pen.GetRefData());
            double ox=0,oy=0,ux=1,uy=0,vx=0,vy=1;
            effective.Apply(&ox,&oy);effective.Apply(&ux,&uy);effective.Apply(&vx,&vy);
            // Conservatively include the native default miter limit.
            padding+=10*std::max(1.0,pen->m_width)*
                std::max(std::hypot(ux-ox,uy-oy),std::hypot(vx-ox,vy-oy));
        }
        const BRect clip=m_clip.Frame();
        const double left=std::max(double(clip.left),std::floor(minX-padding));
        const double top=std::max(double(clip.top),std::floor(minY-padding));
        const double right=std::min(double(clip.right),std::ceil(maxX+padding));
        const double bottom=std::min(double(clip.bottom),std::ceil(maxY+padding));
        if(right<left || bottom<top) return true;
        const int width=(int)(right-left+1),height=(int)(bottom-top+1);
        const BRect bounds(0,0,width-1,height-1),deviceBounds(left,top,right,bottom);
        BAffineTransform shifted(1,0,0,1,-left,-top);shifted.Multiply(effective);
        BRegion scratchClip=m_clip;scratchClip.OffsetBy(-left,-top);
        std::unique_ptr<BBitmap> colour(new BBitmap(bounds,B_RGBA32,true));
        std::unique_ptr<BBitmap> coverage(new BBitmap(bounds,B_RGBA32,true));
        if(!colour->IsValid() || !coverage->IsValid()) return false;
        const wxGraphicsPen originalPen=m_pen;
        const wxGraphicsBrush originalBrush=m_brush;
        const wxAntialiasMode antialias=m_antialias;
        const wxCompositionMode composition=m_composition;
        struct Restore { std::function<void()> fn; ~Restore() { fn(); } } restore{[&]() {
            m_view=original; m_baseTransform=base; m_pen=originalPen; m_brush=originalBrush;
            m_antialias=antialias; m_composition=composition;
        }};
        auto render=[&](BBitmap* bitmap,bool mask) {
            if(!bitmap->Lock()) return false;
            std::memset(bitmap->Bits(),0,bitmap->BitsLength());
            auto* view=new BView(bounds,"wxBinaryCoverage",B_FOLLOW_NONE,B_WILL_DRAW);
            bitmap->AddChild(view);
            view->ConstrainClippingRegion(&scratchClip);
            view->SetTransform(shifted);
            view->SetDrawingMode(B_OP_ALPHA);
            view->SetBlendingMode(B_PIXEL_ALPHA,B_ALPHA_COMPOSITE_SOURCE_OVER);
            m_view=view; m_baseTransform=BAffineTransform();
            m_antialias=wxANTIALIAS_DEFAULT; m_composition=wxCOMPOSITION_OVER;
            m_pen=originalPen; m_brush=originalBrush;
            if(mask)
            {
                if(stroke)
                {
                    auto* pen=static_cast<wxHaikuPenData*>(originalPen.GetRefData());
                    wxGraphicsPenInfo info(wxColour(255,255,255));
                    info.Width(pen->m_width).Cap(pen->m_cap).Join(pen->m_join).Style(pen->m_style);
                    m_pen=GetRenderer()->CreatePen(info);
                    if(m_pen.IsNull()) { bitmap->Unlock(); return false; }
                    static_cast<wxHaikuPenData*>(m_pen.GetRefData())->m_dashes=pen->m_dashes;
                }
                else m_brush=GetRenderer()->CreateBrush(wxBrush(wxColour(255,255,255)));
            }
            if(stroke) StrokePath(path); else FillPath(path,rule);
            view->Sync(); bitmap->Unlock();
            return true;
        };
        if(!render(colour.get(),false) || !render(coverage.get(),true)) return false;
        for(int y=0;y<height;++y)
            for(int x=0;x<width;++x)
            {
                auto* pixel=static_cast<uint8*>(colour->Bits())+y*colour->BytesPerRow()+x*4;
                const auto* mask=static_cast<const uint8*>(coverage->Bits())+y*coverage->BytesPerRow()+x*4;
                if(mask[3]<128) { pixel[3]=0;continue; }
                pixel[3]=std::min(255,(pixel[3]*255+mask[3]/2)/mask[3]);
                // Some native renderers store premultiplied edge coverage.
                // The white coverage pass exposes that attenuation directly.
                for(int c=0;c<3;++c)
                    if(mask[c] && mask[c]<255)
                        pixel[c]=std::min(255,(pixel[c]*255+mask[c]/2)/mask[c]);
            }
        m_view=original; m_baseTransform=base; m_antialias=antialias;
        m_composition=composition; m_pen=originalPen; m_brush=originalBrush;
        BAffineTransform device=base; device.Invert();
        original->SetTransform(device);
        if(composition==wxCOMPOSITION_CLEAR)
        {
            // Native CLEAR ignores bitmap alpha. Restrict its destination to
            // the binary path coverage so transparent padding is not erased.
            BRegion erased;
            for(int y=0;y<height;++y)
            {
                int start=-1;
                const auto* row=static_cast<const uint8*>(coverage->Bits())+y*coverage->BytesPerRow();
                for(int x=0;x<width;++x)
                {
                    const bool covered=row[x*4+3]>=128;
                    if(covered && start<0) start=x;
                    if(!covered && start>=0) { erased.Include(BRect(start+left,y+top,x-1+left,y+top));start=-1; }
                }
                if(start>=0) erased.Include(BRect(start+left,y+top,right,y+top));
            }
            erased.IntersectWith(&m_clip);
            original->ConstrainClippingRegion(&erased);
        }
        original->DrawBitmap(colour.get(),bounds,deviceBounds);
        original->Sync();
        if(composition==wxCOMPOSITION_CLEAR) original->ConstrainClippingRegion(&m_clip);
        original->SetTransform(local);
        return true;
    }

    void RasterizeClipRectangle(double x,double y,double w,double h,BRegion& clipping)
    {
        // Graphics rectangles use continuous extents (unlike wxDC's inclusive
        // integer endpoint convention).
        if(w<0) { x+=w;w=-w; }
        if(h<0) { y+=h;h=-h; }
        if(w<=0 || h<=0 || !std::isfinite(x) || !std::isfinite(y) ||
           !std::isfinite(w) || !std::isfinite(h) || !m_initialClip.CountRects()) return;
        double px[4]={x,x+w,x+w,x},py[4]={y,y,y+h,y+h};
        BAffineTransform transform=m_baseTransform;
        transform.Multiply(m_view->Transform());
        double minY=0,maxY=0;
        for(int i=0;i<4;++i) {
            transform.Apply(&px[i],&py[i]);
            if(!std::isfinite(px[i]) || !std::isfinite(py[i])) return;
            if(i==0) minY=maxY=py[i];
            else { minY=std::min(minY,py[i]);maxY=std::max(maxY,py[i]); }
        }
        const BRect bounds=m_initialClip.Frame();
        if(maxY<=bounds.top || minY>=bounds.bottom+1) return;
        const int first=(int)std::max(double(bounds.top),std::ceil(minY-0.5));
        const int last=(int)std::min(double(bounds.bottom),std::ceil(maxY-0.5)-1);
        for(int row=first;row<=last;++row) {
            const double scan=row+0.5; double left=0,right=0;int count=0;
            for(int i=0;i<4;++i) {
                const int j=(i+1)%4;
                if ((py[i]<=scan && py[j]>scan) || (py[j]<=scan && py[i]>scan)) {
                    const double intersection=px[i]+(scan-py[i])*(px[j]-px[i])/(py[j]-py[i]);
                    if(!count) left=right=intersection;
                    else { left=std::min(left,intersection);right=std::max(right,intersection); }
                    ++count;
                }
            }
            if(count>=2) {
                const double start=std::max(double(bounds.left),std::ceil(left-0.5));
                const double end=std::min(double(bounds.right),std::ceil(right-0.5)-1);
                if(start<=end) clipping.Include(BRect(start,row,end,row));
            }
        }
    }
    bool ApplyPen()
    {
        if (m_pen.IsNull() || m_composition == wxCOMPOSITION_DEST) return false;
        auto* pen=static_cast<wxHaikuPenData*>(m_pen.GetRefData());
        if (pen->m_style == wxPENSTYLE_TRANSPARENT) return false;
        if(pen->m_colour.IsOk()) m_view->SetHighColor(wxToHaikuColor(pen->m_colour));
        m_view->SetPenSize(pen->m_width == 0 ? 1.0f : (float)pen->m_width);
        m_view->SetLineMode(pen->m_cap == wxCAP_ROUND ? B_ROUND_CAP :
                           pen->m_cap == wxCAP_PROJECTING ? B_SQUARE_CAP : B_BUTT_CAP,
                           pen->m_join == wxJOIN_ROUND ? B_ROUND_JOIN :
                           pen->m_join == wxJOIN_BEVEL ? B_BEVEL_JOIN : B_MITER_JOIN);
        return true;
    }
    bool ApplyFont()
    {
        if (m_font.IsNull() || m_composition == wxCOMPOSITION_DEST) return false;
        auto* font=static_cast<wxHaikuFontData*>(m_font.GetRefData());
        m_view->SetFont(&font->m_native);
        m_view->SetHighColor(wxToHaikuColor(font->m_colour));
        return true;
    }
    void DrawFocalGradient(const wxHaikuBrushData& brush,BShape* shape)
    {
        // Haiku's current Painter ignores BGradientRadialFocus::Focal().
        // Rasterize the focal gradient, retaining native shape antialiasing and
        // clipping/composition for the final bitmap draw.
        const BRect bounds=shape->Bounds();
        if(!bounds.IsValid() || brush.m_radius<=0) return;
        const double left=std::floor(bounds.left),top=std::floor(bounds.top);
        const double spanX=std::ceil(bounds.right)-left+1,spanY=std::ceil(bounds.bottom)-top+1;
        if (!std::isfinite(spanX) || !std::isfinite(spanY)) return;
        const int width=(int)std::min(4096.0,spanX),height=(int)std::min(4096.0,spanY);
        if(width<=0||height<=0) return;
        BBitmap bitmap(BRect(0,0,width-1,height-1),B_RGBA32);
        if(!bitmap.IsValid()) return;
        BAffineTransform inverse;
        if(!brush.m_matrix.IsNull()) {
            inverse=static_cast<const wxHaikuMatrixData*>(brush.m_matrix.GetRefData())->GetBAffineTransform();
            inverse.Invert();
        }
        double fx=brush.m_focusX,fy=brush.m_focusY;
        const double distance=std::hypot(fx-brush.m_centreX,fy-brush.m_centreY);
        if(distance>=brush.m_radius) {
            fx=brush.m_centreX+(fx-brush.m_centreX)*brush.m_radius*(1-1e-6)/distance;
            fy=brush.m_centreY+(fy-brush.m_centreY)*brush.m_radius*(1-1e-6)/distance;
        }
        const double ox=fx-brush.m_centreX,oy=fy-brush.m_centreY;
        auto* bits=static_cast<uint8*>(bitmap.Bits());
        for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
            double px=left+(x+0.5)*spanX/width,py=top+(y+0.5)*spanY/height;
            inverse.Apply(&px,&py);
            const double dx=px-fx,dy=py-fy,a=dx*dx+dy*dy,b=ox*dx+oy*dy;
            const double discriminant=b*b-a*(ox*ox+oy*oy-brush.m_radius*brush.m_radius);
            double t=a>1e-12 ? a/(-b+std::sqrt(std::max(0.0,discriminant))) : 0;
            t=std::max(0.0,std::min(1.0,t));
            size_t index=1;
            while(index+1<brush.m_stops.GetCount()&&t>brush.m_stops.Item(index).GetPosition()) ++index;
            const auto& from=brush.m_stops.Item(index-1);const auto& to=brush.m_stops.Item(index);
            const double distance=to.GetPosition()-from.GetPosition();
            const double fraction=distance>0?std::max(0.0,std::min(1.0,(t-from.GetPosition())/distance)):1;
            const wxColour c1=from.GetColour(),c2=to.GetColour();
            const double alpha=c1.Alpha()*(1-fraction)+c2.Alpha()*fraction;
            uint8* pixel=bits+y*bitmap.BytesPerRow()+x*4;
            const auto channel=[&](unsigned char c,unsigned char d){return alpha>0?uint8(std::lround((c*c1.Alpha()*(1-fraction)+d*c2.Alpha()*fraction)/alpha)):uint8(0);};
            pixel[0]=channel(c1.Blue(),c2.Blue());pixel[1]=channel(c1.Green(),c2.Green());
            pixel[2]=channel(c1.Red(),c2.Red());pixel[3]=uint8(std::lround(alpha));
        }
        m_view->PushState();m_view->ClipToShape(shape);
        m_view->DrawBitmap(&bitmap,bitmap.Bounds(),wxToBRect(left,top,spanX,spanY),B_FILTER_BITMAP_BILINEAR);
        m_view->PopState();
    }
    void CopyBitmapToImage()
    {
        if (!m_image || !m_ownedBitmap || !m_image->IsOk() ||
            m_image->GetWidth() <= 0 || m_image->GetHeight() <= 0)
        {
            return;
        }

        if (!m_image->HasAlpha())
            m_image->InitAlpha();

        uint8* const source = static_cast<uint8*>(m_ownedBitmap->Bits());
        unsigned char* const data = m_image->GetData();
        unsigned char* const alpha = m_image->GetAlpha();
        if (!source || !data || !alpha)
            return;

        const int width = m_image->GetWidth();
        const int height = m_image->GetHeight();
        const int bpr = m_ownedBitmap->BytesPerRow();
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const uint8* const src = source + y * bpr + x * 4;
                const int dst = (y * width + x) * 3;
                // B_RGBA32 is BGRA in memory on Haiku.
                data[dst + 0] = src[2];
                data[dst + 1] = src[1];
                data[dst + 2] = src[0];
                alpha[y * width + x] = src[3];
            }
        }
    }

    BView*          m_view = nullptr;
    BBitmap*        m_ownedBitmap = nullptr;
    bool            m_bitmapLocked = false;
    wxImage*        m_image = nullptr;
    struct State {
        wxGraphicsPen pen; wxGraphicsBrush brush; wxGraphicsFont font;
        wxCompositionMode composition; wxAntialiasMode antialias;
        wxInterpolationQuality interpolation; BRegion clip; BAffineTransform transform;
    };
    std::vector<State> m_states;
    unsigned m_layerDepth=0;
    bool m_savedNativeState=false;
    BAffineTransform m_baseTransform;
    BRegion m_initialClip;
    BRegion m_clip;
};

// ============================================================================
// Renderer
// ============================================================================

void wxHaikuDetachGraphicsView(BView* view)
{
    wxHaikuGraphicsContext::DetachBorrowedView(view);
}

class wxHaikuGraphicsRenderer : public wxGraphicsRenderer
{
public:
    static wxHaikuGraphicsRenderer* Get()
    {
        static wxHaikuGraphicsRenderer instance;
        return &instance;
    }

    wxGraphicsContext* CreateContext(const wxWindowDC& dc) override
    {
        return dc.GetHandle() ? new wxHaikuGraphicsContext(this,reinterpret_cast<BView*>(dc.GetHandle())) : nullptr;
    }

    wxGraphicsContext* CreateContext(const wxMemoryDC& dc) override
    {
        return dc.GetHandle() ? new wxHaikuGraphicsContext(this,reinterpret_cast<BView*>(dc.GetHandle())) : nullptr;
    }

    wxGraphicsContext* CreateContext(const wxPrinterDC& dc) override
    {
        return dc.GetHandle() ? new wxHaikuGraphicsContext(this,reinterpret_cast<BView*>(dc.GetHandle())) : nullptr;
    }

    wxGraphicsContext* CreateContextFromNativeContext(void* context) override
    {
        return context ? new wxHaikuGraphicsContext(this, reinterpret_cast<BView*>(context)) : nullptr;
    }

    wxGraphicsContext* CreateContextFromNativeWindow(void* window) override
    {
        return window ? new wxHaikuGraphicsContext(this, reinterpret_cast<BView*>(window)) : nullptr;
    }

    wxGraphicsContext* CreateContextFromImage(wxImage& image) override
    {
        if (!image.IsOk() || image.GetWidth() <= 0 || image.GetHeight() <= 0)
            return nullptr;

        const BRect bounds(0, 0,
                           image.GetWidth() - 1,
                           image.GetHeight() - 1);

        BBitmap* bmp = new BBitmap(bounds, B_RGBA32, true);
        BView* view = new BView(bounds,
                                "haiku_image",
                                B_FOLLOW_ALL,
                                B_WILL_DRAW);

        if (!bmp->IsValid()) { delete view; delete bmp; return nullptr; }
        const bool locked = bmp->Lock();
        if (locked) bmp->AddChild(view);

        if (!locked)
        {
            delete view;
            delete bmp;
            return nullptr;
        }

        const unsigned char* const data = image.GetData();
        const unsigned char* const alpha = image.GetAlpha();
        uint8* const destination = static_cast<uint8*>(bmp->Bits());
        const int bpr = bmp->BytesPerRow();
        for (int y = 0; y < image.GetHeight(); ++y)
        {
            for (int x = 0; x < image.GetWidth(); ++x)
            {
                const int src = (y * image.GetWidth() + x) * 3;
                uint8* const dst = destination + y * bpr + x * 4;
                dst[0] = data[src + 2];
                dst[1] = data[src + 1];
                dst[2] = data[src + 0];
                dst[3] = alpha ? alpha[y * image.GetWidth() + x] : 255;
            }
        }

        return new wxHaikuGraphicsContext(this, view, bmp, locked, &image);
    }

    wxGraphicsContext* CreateContext(wxWindow* window) override
    {
        if (window && window->GetHandle())
        {
            return new wxHaikuGraphicsContext(
                this,
                reinterpret_cast<BView*>(window->GetHandle()));
        }

        return nullptr;
    }

    wxGraphicsContext* CreateMeasuringContext() override
    {
        const BRect bounds(0, 0, 100, 100);

        BBitmap* bmp = new BBitmap(bounds, B_RGBA32, true);
        BView* view = new BView(bounds,
                                "measure",
                                B_FOLLOW_ALL,
                                B_WILL_DRAW);

        if (!bmp->IsValid()) { delete view; delete bmp; return nullptr; }
        const bool locked = bmp->Lock();
        if (locked) bmp->AddChild(view);

        if (!locked) { delete view; delete bmp; return nullptr; }
        return new wxHaikuGraphicsContext(this, view, bmp, locked);
    }

    wxGraphicsPath CreatePath() override
    {
        wxGraphicsPath path;
        path.SetRefData(new wxHaikuGraphicsPathData(this));
        return path;
    }

    wxGraphicsMatrix CreateMatrix(wxDouble a,
                                  wxDouble b,
                                  wxDouble c,
                                  wxDouble d,
                                  wxDouble tx,
                                  wxDouble ty) override
    {
        wxGraphicsMatrix m;

        wxHaikuMatrixData* data = new wxHaikuMatrixData(this);
        data->Set(a, b, c, d, tx, ty);

        m.SetRefData(data);
        return m;
    }

    wxGraphicsPen CreatePen(const wxGraphicsPenInfo& info) override
    {
        wxGraphicsPen pen;
        if (!std::isfinite(info.GetWidth()) || info.GetWidth()<0 ||
            (info.GetGradientType()==wxGRADIENT_NONE && !info.GetColour().IsOk())) return pen;
        // The native API cannot transform a stroke gradient independently of
        // its stroke geometry. Reject these variants instead of drawing a
        // silently incorrect solid pen.
        if (info.GetGradientType()!=wxGRADIENT_NONE && !info.GetMatrix().IsNull() &&
            !info.GetMatrix().IsIdentity()) return pen;
        if (info.GetGradientType()==wxGRADIENT_RADIAL && (info.GetRadius()<=0 ||
            info.GetStartX()!=info.GetEndX() || info.GetStartY()!=info.GetEndY())) return pen;
        if (info.GetStyle()>=wxPENSTYLE_FIRST_HATCH || info.GetStyle()==wxPENSTYLE_STIPPLE ||
            info.GetStyle()==wxPENSTYLE_STIPPLE_MASK || info.GetStyle()==wxPENSTYLE_STIPPLE_MASK_OPAQUE) return pen;
        pen.SetRefData(new wxHaikuPenData(this, info));
        return pen;
    }

    wxGraphicsBrush CreateBrush(const wxBrush& brush) override
    {
        wxGraphicsBrush b;
        if (!brush.IsOk()) return b;
        b.SetRefData(new wxHaikuBrushData(this, brush));
        return b;
    }

    wxGraphicsBrush CreateLinearGradientBrush(wxDouble x1,
                                              wxDouble y1,
                                              wxDouble x2,
                                              wxDouble y2,
                                              const wxGraphicsGradientStops& stops,
                                              const wxGraphicsMatrix& matrix) override
    {
        wxGraphicsBrush b;
        if (!matrix.IsNull()) {
            double a,bv,c,d; matrix.Get(&a,&bv,&c,&d);
            if (!std::isfinite(a*d-bv*c) || a*d-bv*c==0) return b;
        }

        wxHaikuBrushData* bd =
            new wxHaikuBrushData(this, wxBrush());

        bd->CreateLinearGradientBrush(x1, y1, x2, y2, stops);

        bd->m_matrix=matrix;
        b.SetRefData(bd);
        return b;
    }

    wxGraphicsBrush CreateRadialGradientBrush(wxDouble sx,
                                              wxDouble sy,
                                              wxDouble ex,
                                              wxDouble ey,
                                              wxDouble radius,
                                              const wxGraphicsGradientStops& stops,
                                              const wxGraphicsMatrix& matrix) override
    {
        wxGraphicsBrush b;
        if (radius<=0 || !std::isfinite(radius)) return b;
        if (!matrix.IsNull()) {
            double a,bv,c,d; matrix.Get(&a,&bv,&c,&d);
            if (!std::isfinite(a*d-bv*c) || a*d-bv*c==0) return b;
        }

        wxHaikuBrushData* bd =
            new wxHaikuBrushData(this, wxBrush());

        bd->CreateRadialGradientBrush(sx, sy, ex, ey, radius, stops);

        bd->m_matrix=matrix;
        b.SetRefData(bd);
        return b;
    }

    wxGraphicsFont CreateFont(const wxFont& font,
                              const wxColour& col) override
    {
        wxGraphicsFont gf;
        if (!font.IsOk() || !col.IsOk()) return gf;
        gf.SetRefData(new wxHaikuFontData(this, font, col));
        return gf;
    }

    wxGraphicsFont CreateFont(double px,
                              const wxString& facename,
                              int flags,
                              const wxColour& col) override
    {
        wxGraphicsFont gf;
        if (!std::isfinite(px) || px<=0 || !col.IsOk()) return gf;
        gf.SetRefData(new wxHaikuFontData(this, px, facename, flags, col));
        return gf;
    }

    wxGraphicsFont CreateFontAtDPI(const wxFont& font,
                                   const wxRealPoint& dpi,
                                   const wxColour& col) override
    {
        wxGraphicsFont gf;

        if (!font.IsOk() || !col.IsOk() || dpi.y<=0 || !std::isfinite(dpi.y)) return gf;
        wxHaikuFontData* fd = new wxHaikuFontData(this, font, col);

        fd->m_native.SetSize(font.GetFractionalPointSize() * dpi.y / 72.0);

        gf.SetRefData(fd);
        return gf;
    }

    wxGraphicsBitmap CreateBitmap(const wxBitmap& bmp) override
    {
        wxGraphicsBitmap graphicsBmp;

        if (!bmp.IsOk())
            return graphicsBmp;

        graphicsBmp.SetRefData(new wxHaikuBitmapData(this, bmp));
        return graphicsBmp;
    }

    wxGraphicsBitmap CreateBitmapFromImage(const wxImage& image) override
    {
        wxGraphicsBitmap graphicsBmp;

        if (!image.IsOk())
            return graphicsBmp;

        wxImage normalized=image;
        if(image.HasMask()) {
            normalized=image.Copy();
            if(!normalized.HasAlpha()) normalized.InitAlpha();
            else for(int y=0;y<normalized.GetHeight();++y) for(int x=0;x<normalized.GetWidth();++x)
                if(normalized.GetRed(x,y)==image.GetMaskRed() && normalized.GetGreen(x,y)==image.GetMaskGreen() &&
                   normalized.GetBlue(x,y)==image.GetMaskBlue()) normalized.SetAlpha(x,y,0);
            normalized.SetMask(false);
        }
        wxBitmap bmp(normalized);
        graphicsBmp.SetRefData(new wxHaikuBitmapData(this, bmp));

        return graphicsBmp;
    }

    wxImage CreateImageFromBitmap(const wxGraphicsBitmap& bmp) override
    {
        wxImage image;

        const wxHaikuBitmapData* const data =
            static_cast<const wxHaikuBitmapData*>(bmp.GetGraphicsData());

        if (data)
            image = data->ConvertToImage();

        return image;
    }

    wxGraphicsBitmap CreateBitmapFromNativeBitmap(void* bitmap) override
    {
        if (!bitmap)
            return wxGraphicsBitmap();

        wxBitmap bmp;

        wxBitmapRefData* ref = new wxBitmapRefData();
        BBitmap* source=static_cast<BBitmap*>(bitmap);
        if(!source->IsValid()) { delete ref; return wxNullGraphicsBitmap; }
        std::unique_ptr<BBitmap> copy(new BBitmap(BRect(0,0,source->Bounds().IntegerWidth(),
            source->Bounds().IntegerHeight()),B_RGBA32));
        if(!copy->IsValid() || copy->ImportBits(source)!=B_OK) {
            delete ref; return wxNullGraphicsBitmap;
        }
        ref->m_bitmap = copy.release();
        ref->m_mask = nullptr;

        bmp.SetRefData(ref);

        wxGraphicsBitmap gbmp;
        gbmp.SetRefData(new wxHaikuBitmapData(this, bmp));

        return gbmp;
    }

    wxGraphicsBitmap CreateSubBitmap(const wxGraphicsBitmap& bmp, wxDouble x, wxDouble y,
                                     wxDouble w, wxDouble h) override
    {
        if (bmp.IsNull() || !std::isfinite(x) || !std::isfinite(y) ||
            !std::isfinite(w) || !std::isfinite(h)) return wxNullGraphicsBitmap;
        const auto* data=static_cast<const wxHaikuBitmapData*>(bmp.GetBitmapData());
        wxImage source=data->ConvertToImage();
        if (!source.IsOk() || x<0 || y<0 || w<=0 || h<=0 ||
            x+w>source.GetWidth() || y+h>source.GetHeight()) return wxNullGraphicsBitmap;
        const wxRect rect(wxRound(x),wxRound(y),wxRound(w),wxRound(h));
        if (rect.IsEmpty() || rect.GetRight()>=source.GetWidth() || rect.GetBottom()>=source.GetHeight())
            return wxNullGraphicsBitmap;
        return CreateBitmapFromImage(source.GetSubImage(rect));
    }

    wxString GetName() const override
    {
        return "haiku";
    }

    void GetVersion(int* major,
                    int* minor,
                    int* micro) const override
    {
        if (major) *major = 1;
        if (minor) *minor = 0;
        if (micro) *micro = 0;
    }

private:
    wxHaikuGraphicsRenderer() = default;
};

wxGraphicsRenderer* wxGraphicsRenderer::GetDefaultRenderer()
{
    return wxHaikuGraphicsRenderer::Get();
}

static struct wxHaikuRendererRegistrar
{
    wxHaikuRendererRegistrar()
    {
        wxHaikuGraphicsRenderer::Get();
    }
} gs_haikuRendererRegistrar;

#endif // wxUSE_GRAPHICS_CONTEXT
