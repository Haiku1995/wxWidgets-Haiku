/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/colour.cpp
// Purpose:     wxColourImpl class for wxHaiku
// Author:      Haiku port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#ifndef WX_PRECOMP
    #include "wx/colour.h"
#endif // WX_PRECOMP

#include "wx/haiku/colour.h"

#include <GraphicsDefs.h> // rgb_color

// ----------------------------------------------------------------------------
// wxColourImpl (Haiku backend)
// ----------------------------------------------------------------------------

wxColourImpl::wxColourImpl(const rgb_color& color)
{
    InitRGBA(color.red, color.green, color.blue, color.alpha);
}

bool wxColourImpl::operator==(const wxColourImpl& other) const
{
    return m_red   == other.m_red   &&
           m_green == other.m_green &&
           m_blue  == other.m_blue  &&
           m_alpha == other.m_alpha;
}

int wxColourImpl::GetPixel() const
{
    // Haiku has no native pixel value concept (same as Qt backend)
    return 0;
}

rgb_color wxColourImpl::GetColor() const
{
    if ( m_valid )
    {
        rgb_color c;
        c.red   = m_red;
        c.green = m_green;
        c.blue  = m_blue;
        c.alpha = m_alpha;
        return c;
    }

    return rgb_color{ 0, 0, 0, 0 };
}

void wxColourImpl::InitRGBA(ChannelType r,
                            ChannelType g,
                            ChannelType b,
                            ChannelType a)
{
    m_red   = r;
    m_green = g;
    m_blue  = b;
    m_alpha = a;
    m_valid = true;
}
