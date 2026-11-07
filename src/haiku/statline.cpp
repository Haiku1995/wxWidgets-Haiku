/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/statline.cpp
// Purpose:     wxStaticLine for Haiku
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#if wxUSE_STATLINE

#ifndef WX_PRECOMP
    #include "wx/statline.h"
#endif

#include "wx/statline.h"
#include "wx/haiku/private.h"

#include <View.h>
#include <Rect.h>
#include <InterfaceDefs.h>
#include <InterfaceKit.h>

//wxIMPLEMENT_DYNAMIC_CLASS(wxStaticLine, wxStaticLineBase);

static BRect wxToBRect(const wxPoint& pos, const wxSize& size, bool isHorizontal)
{
    const wxPoint p = (pos == wxDefaultPosition) ? wxPoint(0, 0) : pos;

    const int defW = isHorizontal ? 100 : 2;
    const int defH = isHorizontal ? 2 : 100;

    if (size == wxDefaultSize)
    {
        return BRect((float)p.x, (float)p.y,
                     (float)(p.x + defW - 1),
                     (float)(p.y + defH - 1));
    }

    return BRect((float)p.x, (float)p.y,
                 (float)(p.x + size.x - 1),
                 (float)(p.y + size.y - 1));
}

class wxHaikuStaticLineView : public BView
{
public:
    wxHaikuStaticLineView(BRect frame, bool isHorizontal)
        : BView(frame, "wxStaticLine", B_FOLLOW_NONE, B_WILL_DRAW),
          m_isHorizontal(isHorizontal)
    {
        SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
    }

    void Draw(BRect WXUNUSED(updateRect)) override
    {
        SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
                                B_DARKEN_2_TINT));

        if (m_isHorizontal)
        {
            const float y = Bounds().Height() / 2.0f;
            StrokeLine(BPoint(0.0f, y), BPoint(Bounds().Width(), y));
        }
        else
        {
            const float x = Bounds().Width() / 2.0f;
            StrokeLine(BPoint(x, 0.0f), BPoint(x, Bounds().Height()));
        }
    }

private:
    bool m_isHorizontal;
};

wxStaticLine::wxStaticLine(wxWindow* parent, wxWindowID id,
                           const wxPoint& pos, const wxSize& size,
                           long style, const wxString& name)
{
    Create(parent, id, pos, size, style, name);
}

bool wxStaticLine::Create(wxWindow* parent, wxWindowID id,
                          const wxPoint& pos, const wxSize& size,
                          long style, const wxString& name)
{
    const bool isHorizontal = (style & wxLI_VERTICAL) == 0;

    BView* view = new wxHaikuStaticLineView(
        wxToBRect(pos, size, isHorizontal),
        isHorizontal);

    HaikuAdoptView(view, nullptr, view);

    if (!wxStaticLineBase::Create(parent, id, pos, size, style,
                                  wxDefaultValidator, name))
    {
        return false;
    }

    return true;
}

#endif // wxUSE_STATLINE
