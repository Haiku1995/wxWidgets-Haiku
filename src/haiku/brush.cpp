/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/brush.cpp
// Purpose:     wxBrush implementation for Haiku
// Author:      Based on Qt port; adapted for Haiku
// Created:     [Date]
// Copyright:   (c) 2025 wxWidgets team
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/brush.h"
#include "wx/haiku/private.h"
#include "wx/bitmap.h"
#include "wx/colour.h"
#include "wx/log.h"

// ----------------------------------------------------------------------------
// wxBrushRefData: private data for wxBrush
// ----------------------------------------------------------------------------

class wxBrushRefData : public wxGDIRefData
{
public:
    wxBrushRefData()
    {
        m_color.red = 0;
        m_color.green = 0;
        m_color.blue = 0;
        m_color.alpha = 255;
        m_style = wxBRUSHSTYLE_SOLID;
    }

    wxBrushRefData(const wxBrushRefData& data)
        : wxGDIRefData()
        , m_style(data.m_style)
        , m_color(data.m_color)
        , m_stipple(data.m_stipple)
    {
    }

    bool operator==(const wxBrushRefData& data) const
    {
        return m_style == data.m_style &&
               m_color == data.m_color &&
               m_stipple.IsSameAs(data.m_stipple);
    }

    wxBrushStyle m_style;
    rgb_color    m_color;
    wxBitmap     m_stipple;
};

// ----------------------------------------------------------------------------
// wxBrush
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(wxBrush, wxGDIObject);

#define M_BRUSHDATA ((wxBrushRefData*)m_refData)
#define M_STYLE     M_BRUSHDATA->m_style
#define M_COLOR     M_BRUSHDATA->m_color
#define M_STIPPLE   M_BRUSHDATA->m_stipple

wxBrush::wxBrush()
{
    // Default brushes are invalid until initialized, as wxDCBrushChanger expects.
}
wxBrush::~wxBrush() = default;
wxBrush::wxBrush(const wxColour& col, wxBrushStyle style)
{
    m_refData = new wxBrushRefData();
    SetColour(col);
    SetStyle(style);
}

wxBrush::wxBrush(const wxColour& col, int style)
{
    m_refData = new wxBrushRefData();
    SetColour(col);
    SetStyle((wxBrushStyle)style);
}

wxBrush::wxBrush(const wxBitmap& stipple)
{
    m_refData = new wxBrushRefData();
    SetStipple(stipple);
}

void wxBrush::SetColour(const wxColour& col)
{
    AllocExclusive();
  M_COLOR.red = col.Red();
    M_COLOR.green = col.Green();
    M_COLOR.blue = col.Blue();
    M_COLOR.alpha = col.Alpha();
}

void wxBrush::SetColour(unsigned char r, unsigned char g, unsigned char b)
{
    AllocExclusive();
    M_COLOR.red = r;
    M_COLOR.green = g;
    M_COLOR.blue = b;
    M_COLOR.alpha = 255;
}

void wxBrush::SetStyle(wxBrushStyle style)
{
    AllocExclusive();
    M_STYLE = style;
}

void wxBrush::SetStipple(const wxBitmap& stipple)
{
    AllocExclusive();
    M_STIPPLE = stipple;
    M_STYLE = wxBRUSHSTYLE_STIPPLE;
}

bool wxBrush::operator==(const wxBrush& brush) const
{
    if (m_refData == brush.m_refData)
        return true;

    if (!m_refData || !brush.m_refData)
        return false;

    return (*(wxBrushRefData*)m_refData == *(wxBrushRefData*)brush.m_refData);
}

wxColour wxBrush::GetColour() const
{
    wxCHECK_MSG(IsOk(), wxNullColour, wxT("invalid brush"));

    return wxColour(M_COLOR);
}

wxBrushStyle wxBrush::GetStyle() const
{
    wxCHECK_MSG(IsOk(), wxBRUSHSTYLE_INVALID, "invalid brush");

    return M_STYLE;
}

wxBitmap* wxBrush::GetStipple() const
{
    wxCHECK_MSG(IsOk(), nullptr, "invalid brush");

    if (M_STYLE == wxBRUSHSTYLE_STIPPLE && M_STIPPLE.IsOk())
        return new wxBitmap(M_STIPPLE);
    else
        return new wxBitmap();
}

wxGDIRefData* wxBrush::CreateGDIRefData() const
{
    return new wxBrushRefData;
}

wxGDIRefData* wxBrush::CloneGDIRefData(const wxGDIRefData* data) const
{
    return new wxBrushRefData(*static_cast<const wxBrushRefData*>(data));
}
