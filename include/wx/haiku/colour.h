/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/colour.h
// Purpose:     wxColourImpl class for wxHaiku
// Author:      Haiku port
// Created:     2026-xx-xx
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_COLOUR_H_
#define _WX_HAIKU_COLOUR_H_

#include <SupportDefs.h>
#include <GraphicsDefs.h>

class WXDLLIMPEXP_CORE wxColourImpl : public wxColourBase
{
public:
    wxColourImpl() = default;
    wxColourImpl(const rgb_color& color);

    bool IsOk() const override { return m_valid; }

    ChannelType Red()   const override { return m_red;   }
    ChannelType Green() const override { return m_green; }
    ChannelType Blue()  const override { return m_blue;  }
    ChannelType Alpha() const override { return m_alpha; }

    bool operator==(const wxColourImpl& other) const;
    bool operator!=(const wxColourImpl& other) const
    {
        return !(*this == other);
    }

    int GetPixel() const;

    rgb_color GetColor() const;

protected:
    void InitRGBA(ChannelType r,
                  ChannelType g,
                  ChannelType b,
                  ChannelType a) override;

private:
    ChannelType m_red   = 0;
    ChannelType m_green = 0;
    ChannelType m_blue  = 0;
    ChannelType m_alpha = wxALPHA_TRANSPARENT;

    bool m_valid = false;
};

#endif // _WX_HAIKU_COLOUR_H_
