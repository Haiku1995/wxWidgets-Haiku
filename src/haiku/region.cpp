/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/region.cpp
// Purpose:     wxRegion implementation for Haiku
// Author:      Based on Qt; adapted for Haiku
// Created:     [Date]
// Copyright:   (c) 2025 wxWidgets team
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////


#include "wx/wxprec.h"
#include "wx/haiku/private.h"
#include "wx/region.h"
#include "wx/bitmap.h"
#include "wx/image.h"
#include "wx/log.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <Region.h>
#include <Bitmap.h>
#include <TranslationUtils.h>
wxIMPLEMENT_DYNAMIC_CLASS(wxRegion,wxGDIObject);

namespace
{
void IncludeSpan(BRegion& region, int y, int first, int last)
{
    if (first <= last)
        region.Include(BRect(first, y, last, y));
}

void IncludeOpaqueImageSpans(BRegion& region, const wxImage& image,
                             const wxColour* transparent, int tolerance)
{
    if (!image.IsOk())
        return;

    const int width = image.GetWidth();
    const int height = image.GetHeight();
    const unsigned char* data = image.GetData();
    const unsigned char* alpha = image.HasAlpha() ? image.GetAlpha() : nullptr;

    for (int y = 0; y < height; ++y)
    {
        int start = -1;
        for (int x = 0; x < width; ++x)
        {
            const int offset = (y * width + x) * 3;
            bool opaque;
            if (transparent)
            {
                opaque = std::abs((int)data[offset] - transparent->Red()) > tolerance ||
                         std::abs((int)data[offset + 1] - transparent->Green()) > tolerance ||
                         std::abs((int)data[offset + 2] - transparent->Blue()) > tolerance;
            }
            else
            {
                // wxMask stores transparent pixels as black and visible pixels
                // as white in the Haiku implementation.
                opaque = data[offset] != 0 || data[offset + 1] != 0 || data[offset + 2] != 0;
            }

            if (alpha && alpha[y * width + x] == 0)
                opaque = false;

            if (opaque && start == -1)
                start = x;
            else if (!opaque && start != -1)
            {
                IncludeSpan(region, y, start, x - 1);
                start = -1;
            }
        }

        if (start != -1)
            IncludeSpan(region, y, start, width - 1);
    }
}
} // anonymous namespace
// ----------------------------------------------------------------------------
// wxRegion
// ----------------------------------------------------------------------------

wxRegion::wxRegion() { }

wxRegion::wxRegion(wxCoord x, wxCoord y, wxCoord w, wxCoord h)
{
    if (w < 0) { x += w + 1; w = -w; }
    if (h < 0) { y += h + 1; h = -h; }
    m_region.Set(BRect(x, y, x + w - 1, y + h - 1));
}

wxRegion::wxRegion(const wxPoint& topLeft, const wxPoint& bottomRight)
{
    m_region.Set(BRect(topLeft.x, topLeft.y, bottomRight.x, bottomRight.y));
}

wxRegion::wxRegion(const wxRect& rect)
    : wxRegion(rect.x, rect.y, rect.width, rect.height)
{
}

wxRegion::wxRegion(size_t n, const wxPoint* points, wxPolygonFillMode fillStyle)
{
    if (!points || n < 3)
        return;

    int minY = points[0].y;
    int maxY = points[0].y;
    for (size_t i = 1; i < n; ++i)
    {
        minY = std::min(minY, points[i].y);
        maxY = std::max(maxY, points[i].y);
    }

    // BRegion is rectangle based. Rasterize each scan line into horizontal
    // spans, preserving wx's even-odd and winding fill rules.
    for (int y = minY; y <= maxY; ++y)
    {
        struct Crossing { double x; int winding; };
        std::vector<Crossing> crossings;
        const double scanY = y + 0.5;

        for (size_t i = 0; i < n; ++i)
        {
            const wxPoint& a = points[i];
            const wxPoint& b = points[(i + 1) % n];
            if (a.y == b.y || scanY < std::min(a.y, b.y) || scanY >= std::max(a.y, b.y))
                continue;

            const double x = a.x + (scanY - a.y) * (b.x - a.x) / (double)(b.y - a.y);
            crossings.push_back({x, b.y > a.y ? 1 : -1});
        }

        std::sort(crossings.begin(), crossings.end(),
                  [](const Crossing& a, const Crossing& b) { return a.x < b.x; });

        if (fillStyle == wxODDEVEN_RULE)
        {
            for (size_t i = 0; i + 1 < crossings.size(); i += 2)
                IncludeSpan(m_region, y, (int)std::ceil(crossings[i].x),
                            (int)std::ceil(crossings[i + 1].x) - 1);
        }
        else
        {
            int winding = 0;
            double start = 0.0;
            for (const Crossing& crossing : crossings)
            {
                const int oldWinding = winding;
                winding += crossing.winding;
                if (oldWinding == 0 && winding != 0)
                    start = crossing.x;
                else if (oldWinding != 0 && winding == 0)
                    IncludeSpan(m_region, y, (int)std::ceil(start),
                                (int)std::ceil(crossing.x) - 1);
            }
        }
    }
}

wxRegion::wxRegion(const wxBitmap& bmp)
{
    if (!bmp.IsOk())
        return;

    if (wxMask* mask = bmp.GetMask())
    {
        IncludeOpaqueImageSpans(m_region, mask->GetBitmap().ConvertToImage(), nullptr, 0);
        return;
    }

    m_region.Set(BRect(0, 0, bmp.GetWidth() - 1, bmp.GetHeight() - 1));
}

wxRegion::wxRegion(const wxBitmap& bmp, const wxColour& transp, int tolerance)
{
    if (!bmp.IsOk())
        return;

    IncludeOpaqueImageSpans(m_region, bmp.ConvertToImage(), &transp,
                            std::max(0, tolerance));
}

bool wxRegion::IsEmpty() const
{
    return m_region.Frame().IsValid() == false || m_region.Frame().Width() < 0;
}

void wxRegion::Clear()
{
    m_region.MakeEmpty();
}

wxGDIRefData* wxRegion::CreateGDIRefData() const
{
    return nullptr; // Not used in Haiku
}

wxGDIRefData* wxRegion::CloneGDIRefData(const wxGDIRefData*) const
{
    return nullptr;
}

bool wxRegion::DoIsEqual(const wxRegion& region) const
{
    return m_region == region.m_region;
}

bool wxRegion::DoGetBox(wxCoord& x, wxCoord& y, wxCoord& w, wxCoord& h) const
{
    if (IsEmpty()) return false;
    BRect r = m_region.Frame();
    x = (wxCoord)r.left;
    y = (wxCoord)r.top;
    w = (wxCoord)(r.Width() + 1);
    h = (wxCoord)(r.Height() + 1);
    return true;
}

wxRegionContain wxRegion::DoContainsPoint(wxCoord x, wxCoord y) const
{
    return m_region.Contains(BPoint(x, y)) ? wxInRegion : wxOutRegion;
}

wxRegionContain wxRegion::DoContainsRect(const wxRect& rect) const
{
    BRect r(rect.x, rect.y, rect.x + rect.width - 1, rect.y + rect.height - 1);

    if (
        m_region.Contains((int32)r.left,  (int32)r.top) &&
        m_region.Contains((int32)r.right, (int32)r.top) &&
        m_region.Contains((int32)r.left,  (int32)r.bottom) &&
        m_region.Contains((int32)r.right, (int32)r.bottom)
    )
        return wxInRegion;

    if (m_region.Intersects(r))
        return wxPartRegion;

    return wxOutRegion;
}

bool wxRegion::DoOffset(wxCoord x, wxCoord y)
{
    wxCHECK_MSG(IsOk(), false, "Cannot offset an invalid region");
    m_region.OffsetBy(x, y);
    return true;
}

bool wxRegion::DoUnionWithRect(const wxRect& rect)
{
    BRect r(rect.x, rect.y, rect.x + rect.width - 1, rect.y + rect.height - 1);
    m_region.Include(r);
    return true;
}

bool wxRegion::DoUnionWithRegion(const wxRegion& region)
{
    m_region.Include(&region.m_region);
    return true;
}

bool wxRegion::DoIntersect(const wxRegion& region)
{
    m_region.IntersectWith(&region.m_region);
    return true;
}

bool wxRegion::DoSubtract(const wxRegion& region)
{
    m_region.Exclude(&region.m_region);
    return true;
}

bool wxRegion::DoXor(const wxRegion& region)
{
    // Haiku doesn't support XOR; approximate via union - intersect
    BRegion temp;
    temp.Include(&m_region);
    temp.Include(&region.m_region);
    BRegion intersect;
    intersect.IntersectWith(&m_region);
    intersect.IntersectWith(&region.m_region);
    temp.Exclude(&intersect);
    m_region = temp;
    return true;
}

// --- Iterator ---
wxIMPLEMENT_DYNAMIC_CLASS(wxRegionIterator,wxObject);


wxRegionIterator::wxRegionIterator() = default;

wxRegionIterator::wxRegionIterator(const wxRegion& region)
{
    Reset(region);
}

wxRegionIterator::wxRegionIterator(const wxRegionIterator& ri)
    : m_rects(ri.m_rects), m_pos(ri.m_pos)
{
}

wxRegionIterator::~wxRegionIterator() = default;

wxRegionIterator& wxRegionIterator::operator=(const wxRegionIterator& ri)
{
    if (this != &ri)
    {
        m_rects = ri.m_rects;
        m_pos = ri.m_pos;
    }
    return *this;
}

void wxRegionIterator::Reset()
{
    m_rects.clear();
    m_pos = 0;
}

void wxRegionIterator::Reset(const wxRegion& region)
{
    m_rects.clear();
    m_pos = 0;

    BRegion copy = region.m_region;
    int32 count = copy.CountRects();
    for (int32 i = 0; i < count; ++i)
        m_rects.push_back(copy.RectAt(i));
}

wxRegionIterator& wxRegionIterator::operator++()
{
    if (m_pos < m_rects.size()) ++m_pos;
    return *this;
}

wxRegionIterator wxRegionIterator::operator++(int)
{
    wxRegionIterator tmp = *this;
    ++(*this);
    return tmp;
}
