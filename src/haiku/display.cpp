/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/display.cpp
// Author:      Peter Most
// Copyright:   (c) Peter Most
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"
#include <algorithm>

#include "wx/display.h"
#include "wx/private/display.h"
#include "wx/haiku/converter.h"

#include <Screen.h>
#include <Application.h>
#include <Deskbar.h>
#include <cstdlib>
#include <memory>
#include <stdio.h>

#if defined(WX_DEBUG_HAIKU) && WX_DEBUG_HAIKU
    #define HAIKU_DISPLAY_TRACE(fmt, ...) \
        printf("[HAIKU-DISPLAY:%s:%d] " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
#else
    #define HAIKU_DISPLAY_TRACE(...) ((void)0)
#endif

class wxDisplayImplHaiku : public wxDisplayImpl
{
public:
    wxDisplayImplHaiku(unsigned n);

    virtual wxRect GetGeometry() const override;
    virtual wxRect GetClientArea() const override;
    virtual int GetDepth() const override;
    virtual double GetScaleFactor() const override;

#if wxUSE_DISPLAY
    virtual wxArrayVideoModes GetModes(const wxVideoMode& mode) const override;
    virtual wxVideoMode GetCurrentMode() const override;
    virtual bool ChangeMode(const wxVideoMode& mode) override;
#endif // wxUSE_DISPLAY

    BScreen* GetScreen() const;
};

wxDisplayImplHaiku::wxDisplayImplHaiku(unsigned n)
    : wxDisplayImpl(n)
{
    HAIKU_DISPLAY_TRACE("ctor this=%p index=%u", this, n);
}

wxRect wxDisplayImplHaiku::GetGeometry() const
{
    screen_id sid;
    sid.id = B_MAIN_SCREEN_ID.id + GetIndex();

 //   HAIKU_DISPLAY_TRACE("GetGeometry ENTER this=%p index=%u sid=%" B_PRId32,
   //                     this, GetIndex(), sid.id);

    BScreen screen(sid);
    if (!screen.IsValid())
    {
        HAIKU_DISPLAY_TRACE("GetGeometry invalid screen sid=%" B_PRId32, sid.id);
        return wxRect(0, 0, 0, 0);
    }

    BRect frame = screen.Frame();

   // HAIKU_DISPLAY_TRACE("GetGeometry frame=(%.1f,%.1f,%.1f,%.1f) size=(%.1f x %.1f)",
     //                   frame.left, frame.top, frame.right, frame.bottom,
       //                 frame.Width() + 1.0f, frame.Height() + 1.0f);

    return wxRect(
        static_cast<int>(frame.left),
        static_cast<int>(frame.top),
        static_cast<int>(frame.Width() + 1),
        static_cast<int>(frame.Height() + 1)
    );
}

wxRect wxDisplayImplHaiku::GetClientArea() const
{
    wxRect area = GetGeometry();
    BDeskbar deskbar;
    if (!deskbar.IsRunning() || deskbar.IsAutoHide()) return area;
    const BRect bar = deskbar.Frame();
    wxRect occupied(static_cast<int>(bar.left), static_cast<int>(bar.top),
                    static_cast<int>(bar.Width()) + 1, static_cast<int>(bar.Height()) + 1);
    if (!area.Intersects(occupied)) return area;
    int left = area.GetLeft(), right = area.GetRight();
    int top = area.GetTop(), bottom = area.GetBottom();
    switch (deskbar.Location())
    {
        case B_DESKBAR_TOP:
            top = std::min(bottom + 1, occupied.GetBottom() + 1); break;
        case B_DESKBAR_BOTTOM:
            bottom = std::max(top - 1, occupied.GetTop() - 1); break;
        case B_DESKBAR_LEFT_TOP: case B_DESKBAR_LEFT_BOTTOM:
            left = std::min(right + 1, occupied.GetRight() + 1); break;
        case B_DESKBAR_RIGHT_TOP: case B_DESKBAR_RIGHT_BOTTOM:
            right = std::max(left - 1, occupied.GetLeft() - 1); break;
    }
    return wxRect(left, top, right - left + 1, bottom - top + 1);
}

int wxDisplayImplHaiku::GetDepth() const
{
    BScreen* screen = GetScreen();
    if (!screen || !screen->IsValid())
    {
        HAIKU_DISPLAY_TRACE("GetDepth invalid screen=%p -> returning 0", screen);
        return 0;
    }

    const color_space cs = screen->ColorSpace();
    const int depth = HaikuWxConverter::GetWxDepthFromColorSpace(cs);

    HAIKU_DISPLAY_TRACE("GetDepth screen=%p colorSpace=%d depth=%d",
                        screen, (int)cs, depth);

    return depth;
}

double wxDisplayImplHaiku::GetScaleFactor() const
{
    HAIKU_DISPLAY_TRACE("GetScaleFactor returning 1.0");
    return 1.0;
}

#if wxUSE_DISPLAY
static wxVideoMode wxHaikuVideoMode(const display_mode& mode)
{
    const unsigned total = mode.timing.h_total * mode.timing.v_total;
    const int refresh = total ? static_cast<int>((mode.timing.pixel_clock * 1000.0 / total) + 0.5) : 0;
    return wxVideoMode(mode.virtual_width, mode.virtual_height,
                      HaikuWxConverter::GetWxDepthFromColorSpace(static_cast<color_space>(mode.space)), refresh);
}

wxArrayVideoModes wxDisplayImplHaiku::GetModes(const wxVideoMode& match) const
{
    wxArrayVideoModes result;
    BScreen* screen = GetScreen(); display_mode* modes = nullptr; uint32 count = 0;
    if (screen->GetModeList(&modes, &count) != B_OK) return result;
    for (uint32 i = 0; i < count; ++i)
    {
        const wxVideoMode mode = wxHaikuVideoMode(modes[i]);
        if (mode.Matches(match) && result.Index(mode) == wxNOT_FOUND) result.Add(mode);
    }
    free(modes);
    return result;
}

wxVideoMode wxDisplayImplHaiku::GetCurrentMode() const
{
    display_mode mode;
    return GetScreen()->GetMode(&mode) == B_OK ? wxHaikuVideoMode(mode) : wxVideoMode();
}

bool wxDisplayImplHaiku::ChangeMode(const wxVideoMode& mode)
{
    // Mode changes require restoring the original native timing on reset.
    // Until that lifecycle is implemented, fail without changing the desktop.
    wxUnusedVar(mode);
    return false;
}
#endif // wxUSE_DISPLAY

BScreen* wxDisplayImplHaiku::GetScreen() const
{
    thread_local std::unique_ptr<BScreen> current;
    thread_local unsigned currentIndex = ~0u;
    if (!current || currentIndex != GetIndex())
    {
        screen_id id; id.id = B_MAIN_SCREEN_ID.id + GetIndex();
        current.reset(new BScreen(id));
        currentIndex = GetIndex();
    }
    BScreen& screen = *current;
    HAIKU_DISPLAY_TRACE("GetScreen returning static screen=%p valid=%d",
                        &screen, screen.IsValid() ? 1 : 0);
    return &screen;
}

#if wxUSE_DISPLAY

class wxDisplayFactoryHaiku : public wxDisplayFactory
{
public:
    virtual wxDisplayImpl* CreateDisplay(unsigned n) override;
    virtual unsigned GetCount() override;
    virtual int GetFromPoint(const wxPoint& pt) override;
};

wxDisplayImpl* wxDisplayFactoryHaiku::CreateDisplay(unsigned n)
{
    HAIKU_DISPLAY_TRACE("CreateDisplay index=%u", n);
    return new wxDisplayImplHaiku(n);
}

unsigned wxDisplayFactoryHaiku::GetCount()
{
    HAIKU_DISPLAY_TRACE("GetCount ENTER");

    unsigned count = 0;
    for (unsigned i = 0; ; i++)
    {
        screen_id sid;
        sid.id = B_MAIN_SCREEN_ID.id + i;
        BScreen screen(sid);

        HAIKU_DISPLAY_TRACE("GetCount probe i=%u sid=%" B_PRId32 " valid=%d",
                            i, sid.id, screen.IsValid() ? 1 : 0);

        if (!screen.IsValid())
            break;

        count++;
    }

    HAIKU_DISPLAY_TRACE("GetCount EXIT count=%u", count);
    return count;
}

int wxDisplayFactoryHaiku::GetFromPoint(const wxPoint& pt)
{
    HAIKU_DISPLAY_TRACE("GetFromPoint ENTER pt=(%d,%d)", pt.x, pt.y);

    for (unsigned i = 0; ; i++)
    {
        screen_id sid;
        sid.id = B_MAIN_SCREEN_ID.id + i;
        BScreen screen(sid);

        if (!screen.IsValid())
        {
            HAIKU_DISPLAY_TRACE("GetFromPoint stop at i=%u sid=%" B_PRId32 " invalid",
                                i, sid.id);
            break;
        }

        BRect frame = screen.Frame();
        const bool contains = frame.Contains(BPoint(pt.x, pt.y));

        HAIKU_DISPLAY_TRACE("GetFromPoint probe i=%u sid=%" B_PRId32
                            " frame=(%.1f,%.1f,%.1f,%.1f) contains=%d",
                            i, sid.id,
                            frame.left, frame.top, frame.right, frame.bottom,
                            contains ? 1 : 0);

        if (contains)
        {
            HAIKU_DISPLAY_TRACE("GetFromPoint HIT display=%u", i);
            return i;
        }
    }

    HAIKU_DISPLAY_TRACE("GetFromPoint MISS -> wxNOT_FOUND");
    return wxNOT_FOUND;
}

/* static */ wxDisplayFactory* wxDisplay::CreateFactory()
{
    HAIKU_DISPLAY_TRACE("CreateFactory wxUSE_DISPLAY");
    return new wxDisplayFactoryHaiku;
}

#else // wxUSE_DISPLAY

class wxDisplayFactorySingleHaiku : public wxDisplayFactorySingle
{
protected:
    virtual wxDisplayImpl* CreateSingleDisplay() override
    {
        HAIKU_DISPLAY_TRACE("CreateSingleDisplay");
        return new wxDisplayImplHaiku(0);
    }
};

/* static */ wxDisplayFactory* wxDisplay::CreateFactory()
{
    HAIKU_DISPLAY_TRACE("CreateFactory !wxUSE_DISPLAY");
    return new wxDisplayFactorySingleHaiku;
}

#endif // wxUSE_DISPLAY
