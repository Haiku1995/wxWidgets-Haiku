/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/pen.cpp
// Purpose:     wxPen implementation for Haiku
// Author:      Based on Qt; adapted for Haiku by [Your Name]
// Created:     2025-10-14
// Copyright:   (c) 2025 wxWidgets team
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#ifndef WX_PRECOMP
    #include "wx/pen.h"
    #include "wx/colour.h"
#endif

#include "wx/haiku/private.h"
#include <interface/View.h>
#include <support/SupportDefs.h>

// ----------------------------------------------------------------------------
// wxPenRefData - stores native pen info for Haiku
// ----------------------------------------------------------------------------

class wxPenRefData : public wxGDIRefData
{
public:
    wxPenRefData()
        : m_width(1),
          m_style(wxPENSTYLE_SOLID),
          m_cap(wxCAP_ROUND),
          m_join(wxJOIN_ROUND),
          m_dashes(nullptr),
          m_dashCount(0),
          m_stipple(nullptr)
    {
    }

    wxPenRefData(const wxPenRefData& data)
        : wxGDIRefData(),
          m_colour(data.m_colour),
          m_width(data.m_width),
          m_style(data.m_style),
          m_cap(data.m_cap),
          m_join(data.m_join),
          m_dashes(data.m_dashes),
          m_dashCount(data.m_dashCount),
          m_stipple(data.m_stipple)
    {
    }

    wxColour    m_colour;
    int         m_width;
    wxPenStyle  m_style;
    wxPenCap    m_cap;
    wxPenJoin   m_join;
    const wxDash* m_dashes;
    int         m_dashCount;
    wxBitmap*   m_stipple;
};

// ----------------------------------------------------------------------------
// wxPen
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(wxPen, wxGDIObject);

#define M_PENDATA ((wxPenRefData*)m_refData)

wxPen::wxPen()
{
    // Default pens are invalid until initialized (also used by wxDCPenChanger).
}

wxPen::wxPen(const wxColour& col, int width, wxPenStyle style)
{
    m_refData = new wxPenRefData;
    M_PENDATA->m_colour = col;
    M_PENDATA->m_width = width;
    M_PENDATA->m_style = style;
}

wxPen::wxPen(const wxPenInfo& info)
{
    m_refData = new wxPenRefData;
    M_PENDATA->m_colour = info.GetColour();
    M_PENDATA->m_width = info.GetWidth();
    M_PENDATA->m_style = info.GetStyle();
    M_PENDATA->m_cap   = info.GetCap();
    M_PENDATA->m_join  = info.GetJoin();
    M_PENDATA->m_dashCount = info.GetDashes(const_cast<wxDash**>(&M_PENDATA->m_dashes));
}

bool wxPen::operator==(const wxPen& pen) const
{
    if (m_refData == pen.m_refData)
        return true;
    if (!m_refData || !pen.m_refData)
        return false;

    const wxPenRefData* a = M_PENDATA;
    const wxPenRefData* b = (wxPenRefData*)pen.m_refData;

    if (a->m_colour != b->m_colour ||
        a->m_width != b->m_width ||
        a->m_style != b->m_style ||
        a->m_cap != b->m_cap ||
        a->m_join != b->m_join)
        return false;

    if (a->m_dashCount != b->m_dashCount)
        return false;

    if (a->m_dashCount > 0 && memcmp(a->m_dashes, b->m_dashes, a->m_dashCount * sizeof(wxDash)) != 0)
        return false;

    return true;
}

bool wxPen::operator!=(const wxPen& pen) const
{
    return !(*this == pen);
}

void wxPen::SetColour(const wxColour& col)
{
    AllocExclusive();
    M_PENDATA->m_colour = col;
}

void wxPen::SetColour(unsigned char r, unsigned char g, unsigned char b)
{
    AllocExclusive();
    M_PENDATA->m_colour.Set(r, g, b);
}

void wxPen::SetWidth(int width)
{
    AllocExclusive();
    M_PENDATA->m_width = width;
}

void wxPen::SetStyle(wxPenStyle style)
{
    AllocExclusive();
    M_PENDATA->m_style = style;
}

void wxPen::SetStipple(const wxBitmap& stipple)
{
   // wxLogWarning("wxPen::SetStipple: Stippled pens not supported on Haiku");
}

void wxPen::SetDashes(int nb_dashes, const wxDash* dash)
{
    AllocExclusive();
    M_PENDATA->m_dashCount = nb_dashes;
    M_PENDATA->m_dashes = dash;
}

void wxPen::SetJoin(wxPenJoin join)
{
    AllocExclusive();
    M_PENDATA->m_join = join;
}

void wxPen::SetCap(wxPenCap cap)
{
    AllocExclusive();
    M_PENDATA->m_cap = cap;
}

wxColour wxPen::GetColour() const
{
    wxCHECK_MSG(IsOk(), wxNullColour, "invalid pen");
    return M_PENDATA->m_colour;
}

wxBitmap* wxPen::GetStipple() const
{
    return M_PENDATA->m_stipple;
}

wxPenStyle wxPen::GetStyle() const
{
    wxCHECK_MSG(IsOk(), wxPENSTYLE_INVALID, "invalid pen");
    return M_PENDATA->m_style;
}

wxPenJoin wxPen::GetJoin() const
{
    wxCHECK_MSG(IsOk(), wxJOIN_INVALID, "invalid pen");
    return M_PENDATA->m_join;
}

wxPenCap wxPen::GetCap() const
{
    wxCHECK_MSG(IsOk(), wxCAP_INVALID, "invalid pen");
    return M_PENDATA->m_cap;
}

int wxPen::GetWidth() const
{
    wxCHECK_MSG(IsOk(), -1, "invalid pen");
    return M_PENDATA->m_width;
}

int wxPen::GetDashes(wxDash** ptr) const
{
    wxCHECK_MSG(IsOk(), -1, "invalid pen");
    if (ptr)
        *ptr = const_cast<wxDash*>(M_PENDATA->m_dashes);
    return M_PENDATA->m_dashCount;
}

// ----------------------------------------------------------------------------
// Haiku-specific integration
// ----------------------------------------------------------------------------

void wxPen::ApplyToView(BView* view) const
{
    if (!view || !IsOk())
        return;

    const wxColour& c = M_PENDATA->m_colour;
    view->SetHighColor(c.Red(), c.Green(), c.Blue(), c.Alpha());
    view->SetPenSize(static_cast<float>(M_PENDATA->m_width));

    // Dash patterns are limited in Haiku’s BView — no API for custom patterns
}

// ----------------------------------------------------------------------------
// wxGDIRefData helpers
// ----------------------------------------------------------------------------

wxGDIRefData* wxPen::CreateGDIRefData() const
{
    return new wxPenRefData;
}

wxGDIRefData* wxPen::CloneGDIRefData(const wxGDIRefData* data) const
{
    return new wxPenRefData(*static_cast<const wxPenRefData*>(data));
}
