/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/renderer.cpp
// Purpose:     wxRendererNative implementation using Haiku’s BControlLook
// Author:      Based on Haiku's BControlLook; adapted for wxWidgets
// Created:     [Date]
// Copyright:   (c) 2025 wxWidgets team
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#include "wx/renderer.h"
#ifndef WX_PRECOMP
    #include "wx/app.h"
    #include "wx/window.h"
    #include "wx/dcclient.h"
#endif

#include "wx/apptrait.h"
#include "wx/headerctrl.h"     // for wxHDR_SORT_ICON_*
#include "wx/haiku/private/converter.h"

#include <ControlLook.h>
#include <View.h>
#include <Window.h>

//using BPrivate::BControlLook;  // makes referencing easier
//extern BControlLook* be_control_look;  // ensure this extern

class WXDLLEXPORT wxRendererHaiku : public wxDelegateRendererNative
{
public:
    virtual int  DrawHeaderButton(wxWindow *win,
                                  wxDC& dc,
                                  const wxRect& rect,
                                  int flags = 0,
                                  wxHeaderSortIconType sortArrow = wxHDR_SORT_ICON_NONE,
                                  wxHeaderButtonParams* params = nullptr) override;

    virtual int GetHeaderButtonHeight(wxWindow *win) override;
    virtual int GetHeaderButtonMargin(wxWindow *win) override;

    virtual void DrawTreeItemButton(wxWindow *win,
                                    wxDC& dc,
                                    const wxRect& rect,
                                    int flags = 0) override;

    virtual void DrawSplitterBorder(wxWindow *win,
                                    wxDC& dc,
                                    const wxRect& rect,
                                    int flags = 0) override;

    virtual void DrawSplitterSash(wxWindow *win,
                                  wxDC& dc,
                                  const wxSize& size,
                                  wxCoord position,
                                  wxOrientation orient,
                                  int flags = 0) override;

    virtual void DrawCheckBox(wxWindow *win,
                              wxDC& dc,
                              const wxRect& rect,
                              int flags = 0) override;

    virtual void DrawCheckMark(wxWindow *win,
                               wxDC& dc,
                               const wxRect& rect,
                               int flags = 0) override;

    virtual wxSize GetCheckMarkSize(wxWindow *win) override;

    virtual void DrawPushButton(wxWindow *win,
                                wxDC& dc,
                                const wxRect& rect,
                                int flags = 0) override;

    virtual void DrawItemSelectionRect(wxWindow *win,
                                       wxDC& dc,
                                       const wxRect& rect,
                                       int flags = 0) override;

    virtual void DrawChoice(wxWindow* win,
                            wxDC& dc,
                            const wxRect& rect,
                            int flags = 0) override;

    virtual void DrawComboBox(wxWindow* win,
                              wxDC& dc,
                              const wxRect& rect,
                              int flags = 0) override;

    virtual void DrawGauge(wxWindow* win,
                           wxDC& dc,
                           const wxRect& rect,
                           int value,
                           int max,
                           int flags = 0) override;

    virtual void DrawRadioBitmap(wxWindow* win,
                                 wxDC& dc,
                                 const wxRect& rect,
                                 int flags = 0 ) override;

    virtual void DrawTextCtrl(wxWindow* win,
                              wxDC& dc,
                              const wxRect& rect,
                              int flags = 0) override;

    virtual void DrawFocusRect(wxWindow* win,
                               wxDC& dc,
                               const wxRect& rect,
                               int flags = 0) override;

    virtual wxSize GetCheckBoxSize(wxWindow *win, int flags = 0) override;

    virtual wxSplitterRenderParams GetSplitterParams(const wxWindow *win) override;

    // Common helper for combo/choice
    void DoDrawComboBox(wxWindow* win,
                        wxDC& dc,
                        const wxRect& rect,
                        int flags,
                        bool editable = true);
};

// ----------------------------------------------------------------------------
// Helper: Extract BView from wxDC
// ----------------------------------------------------------------------------
// Native control looks access the view transform and drawing state. A DC
// locks individual operations, so renderer calls must hold their own lock.
class HaikuRendererViewLock
{
public:
    explicit HaikuRendererViewLock(BView* view)
        : m_window(view ? view->Window() : nullptr),
          m_locked(view && (!m_window || m_window->LockLooper())) {}
    ~HaikuRendererViewLock()
    {
        if (m_window && m_locked)
            m_window->UnlockLooper();
    }
    bool IsLocked() const { return m_locked; }
private:
    BWindow* m_window;
    bool m_locked;
};

static BView* GetBViewFromDC(wxDC& dc)
{
    // Simplified: assume handle is BView*
    return static_cast<BView*>( dc.GetHandle() );
}

// ----------------------------------------------------------------------------
// wxRendererHaiku implementations
// ----------------------------------------------------------------------------

wxRendererNative& wxRendererNative::GetDefault()
{
    static wxRendererHaiku s_rendererHaiku;
    return s_rendererHaiku;
}

int wxRendererHaiku::DrawHeaderButton(wxWindow* /*win*/,
                                      wxDC& dc,
                                      const wxRect& rect,
                                      int flags,
                                      wxHeaderSortIconType sortArrow,
                                      wxHeaderButtonParams* WXUNUSED(params))
{
    BView* view = GetBViewFromDC(dc);
    HaikuRendererViewLock viewLock(view);
    if (!viewLock.IsLocked() || !be_control_look)
        return 0;

    BRect brect(rect.x, rect.y,
                rect.x + rect.width - 1,
                rect.y + rect.height - 1);

    rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);
    rgb_color background = base;

    uint32 lookFlags = 0;
    if (flags & wxCONTROL_DISABLED)
        lookFlags |= BControlLook::B_DISABLED;
    if (flags & wxCONTROL_CURRENT)
        lookFlags |= BControlLook::B_FOCUSED;
    if (flags & wxCONTROL_PRESSED)
        lookFlags |= BControlLook::B_ACTIVATED;
    if (flags & wxCONTROL_ISDEFAULT)
        lookFlags |= BControlLook::B_DEFAULT_BUTTON;
    if (flags & wxCONTROL_SPECIAL)
        lookFlags |= BControlLook::B_HOVER;

    // draw frame and background
    be_control_look->DrawButtonFrame(view, brect, brect, base, background, lookFlags);
    be_control_look->DrawButtonBackground(view, brect, brect, base, lookFlags);

    // sort arrow
    if (sortArrow != wxHDR_SORT_ICON_NONE)
    {
        BRect arrowRect = brect;
        arrowRect.left = arrowRect.right - 10;
        arrowRect.top += 2;
        arrowRect.bottom -= 2;

        uint32 direction = (sortArrow == wxHDR_SORT_ICON_DOWN)
                             ? BControlLook::B_DOWN_ARROW
                             : BControlLook::B_UP_ARROW;

        be_control_look->DrawArrowShape(view,
                                        arrowRect,
                                        arrowRect,
                                        base,
                                        direction,
                                        lookFlags);
    }

    return rect.width;
}

int wxRendererHaiku::GetHeaderButtonHeight(wxWindow* /*win*/)
{
    return 20;
}

int wxRendererHaiku::GetHeaderButtonMargin(wxWindow* /*win*/)
{
    return 4;
}

void wxRendererHaiku::DrawTreeItemButton(wxWindow* /*win*/,
                                         wxDC& dc,
                                         const wxRect& rect,
                                         int flags)
{
    BView* view = GetBViewFromDC(dc);
    HaikuRendererViewLock viewLock(view);
    if (!viewLock.IsLocked() || !be_control_look)
        return;

    BRect brect(rect.x, rect.y,
                rect.x + rect.width - 1,
                rect.y + rect.height - 1);

    rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);

    uint32 direction = (flags & wxCONTROL_EXPANDED)
                         ? BControlLook::B_DOWN_ARROW
                         : BControlLook::B_RIGHT_ARROW;

    be_control_look->DrawArrowShape(view,
                                    brect,
                                    brect,
                                    base,
                                    direction,
                                    0);
}

void wxRendererHaiku::DrawPushButton(wxWindow* /*win*/,
                                     wxDC& dc,
                                     const wxRect& rect,
                                     int flags)
{
    BView* view = GetBViewFromDC(dc);
    HaikuRendererViewLock viewLock(view);
    if (!viewLock.IsLocked() || !be_control_look)
        return;

    BRect brect(rect.x, rect.y,
                rect.x + rect.width - 1,
                rect.y + rect.height - 1);

    rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);
    // Use a button‑background system color (if defined) or fallback
  //  rgb_color background = ui_color(B_BUTTON_BACKGROUND_COLOR);
  rgb_color background = ui_color(B_CONTROL_BACKGROUND_COLOR);

    uint32 lookFlags = 0;
    if (flags & wxCONTROL_PRESSED)
        lookFlags |= BControlLook::B_ACTIVATED;
    else if (flags & wxCONTROL_CURRENT)
        lookFlags |= BControlLook::B_HOVER;

    // draw frame with radius 2.0 (for example)
    be_control_look->DrawButtonFrame(view, brect, brect,
                                     2.0f, base, background,
                                     lookFlags, BControlLook::B_ALL_BORDERS);

    be_control_look->DrawButtonBackground(view, brect, brect,
                                          base, lookFlags, BControlLook::B_ALL_BORDERS);
}

void wxRendererHaiku::DrawCheckBox(wxWindow* /*win*/,
                                   wxDC& dc,
                                   const wxRect& rect,
                                   int flags)
{
    BView* view = GetBViewFromDC(dc);
    HaikuRendererViewLock viewLock(view);
    if (!viewLock.IsLocked() || !be_control_look)
        return;

    BRect brect(rect.x, rect.y,
                rect.x + rect.width - 1,
                rect.y + rect.height - 1);

    rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);

    uint32 lookFlags = 0;
    if (flags & wxCONTROL_PRESSED)
        lookFlags |= BControlLook::B_ACTIVATED;
    else if (flags & wxCONTROL_CURRENT)
        lookFlags |= BControlLook::B_HOVER;

    // no extra checked flag in this API, so simply pass flags
    be_control_look->DrawCheckBox(view, brect, brect, base, lookFlags);
}

void wxRendererHaiku::DrawCheckMark(wxWindow* /*win*/,
                                    wxDC& dc,
                                    const wxRect& rect,
                                    int flags)
{
    BView* view = GetBViewFromDC(dc);
    HaikuRendererViewLock viewLock(view);
    if (!viewLock.IsLocked() || !be_control_look)
        return;

    BRect brect(rect.x, rect.y,
                rect.x + rect.width - 1,
                rect.y + rect.height - 1);

    rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);

    uint32 lookFlags = 0;
    if (flags & wxCONTROL_DISABLED)
        lookFlags |= BControlLook::B_DISABLED;

    //be_control_look->DrawCheckMark(view, brect, brect, base, lookFlags);
    be_control_look->DrawCheckBox(view, brect, brect, base,1 );
}

wxSize wxRendererHaiku::GetCheckBoxSize(wxWindow* /*win*/, int /*flags*/)
{
    return wxSize(13,13);
}

wxSize wxRendererHaiku::GetCheckMarkSize(wxWindow* /*win*/)
{
    return wxSize(9,9);
}

void wxRendererHaiku::DrawRadioBitmap(wxWindow* /*win*/,
                                      wxDC& dc,
                                      const wxRect& rect,
                                      int flags)
{
    BView* view = GetBViewFromDC(dc);
    HaikuRendererViewLock viewLock(view);
    if (!viewLock.IsLocked() || !be_control_look)
        return;

    BRect brect(rect.x, rect.y,
                rect.x + rect.width - 1,
                rect.y + rect.height - 1);

    rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);

    uint32 lookFlags = 0;
    if (flags & wxCONTROL_PRESSED)
        lookFlags |= BControlLook::B_ACTIVATED;
    else if (flags & wxCONTROL_CURRENT)
        lookFlags |= BControlLook::B_HOVER;

    // no extra param for “checked” in this API overload
    be_control_look->DrawRadioButton(view, brect, brect, base, lookFlags);
}

void wxRendererHaiku::DrawComboBox(wxWindow* win,
                                   wxDC& dc,
                                   const wxRect& rect,
                                   int flags)
{
    DoDrawComboBox(win, dc, rect, flags, /*editable?*/ true);
}

void wxRendererHaiku::DrawChoice(wxWindow* win,
                                 wxDC& dc,
                                 const wxRect& rect,
                                 int flags)
{
    DoDrawComboBox(win, dc, rect, flags, /*editable?*/ false);
}

void wxRendererHaiku::DoDrawComboBox(wxWindow* /*win*/,
                                     wxDC& dc,
                                     const wxRect& rect,
                                     int flags,
                                     bool editable)
{
    BView* view = GetBViewFromDC(dc);
    HaikuRendererViewLock viewLock(view);
    if (!viewLock.IsLocked() || !be_control_look)
        return;

    BRect brect(rect.x, rect.y,
                rect.x + rect.width - 1,
                rect.y + rect.height - 1);

    rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);

    uint32 lookFlags = 0;
    if (flags & wxCONTROL_DISABLED)
        lookFlags |= BControlLook::B_DISABLED;
    if (flags & wxCONTROL_CURRENT)
        lookFlags |= BControlLook::B_FOCUSED;
    if (flags & wxCONTROL_PRESSED)
        lookFlags |= BControlLook::B_ACTIVATED;
    if (flags & wxCONTROL_SPECIAL)
        lookFlags |= BControlLook::B_HOVER;

    be_control_look->DrawMenuFieldFrame(view, brect, brect, base, base, lookFlags);
    be_control_look->DrawMenuFieldBackground(view, brect, brect, base, /*popupIndicator*/true, lookFlags);
}

void wxRendererHaiku::DrawTextCtrl(wxWindow* /*win*/,
                                   wxDC& dc,
                                   const wxRect& rect,
                                   int flags)
{
    BView* view = GetBViewFromDC(dc);
    HaikuRendererViewLock viewLock(view);
    if (!viewLock.IsLocked() || !be_control_look)
        return;

    BRect brect(rect.x, rect.y,
                rect.x + rect.width - 1,
                rect.y + rect.height - 1);

    rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);

    uint32 lookFlags = 0;
    if (flags & wxCONTROL_DISABLED)
        lookFlags |= BControlLook::B_DISABLED;

    be_control_look->DrawTextControlBorder(view, brect, brect, base, lookFlags);
}

void wxRendererHaiku::DrawItemSelectionRect(wxWindow* /*win*/,
                                            wxDC& dc,
                                            const wxRect& rect,
                                            int flags)
{
    BView* view = GetBViewFromDC(dc);
    HaikuRendererViewLock viewLock(view);
    if (!viewLock.IsLocked() || !be_control_look)
        return;

    BRect brect(rect.x, rect.y,
                rect.x + rect.width - 1,
                rect.y + rect.height - 1);

    rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);

    uint32 lookFlags = 0;
    if (flags & wxCONTROL_SELECTED)
        lookFlags |= BControlLook::B_FOCUSED;  // approximate
    if (flags & wxCONTROL_CURRENT)
        lookFlags |= BControlLook::B_HOVER;

    // Draw focus border (approximation)
    be_control_look->DrawButtonFrame(view, brect, brect, base, base, lookFlags);
}

void wxRendererHaiku::DrawFocusRect(wxWindow* /*win*/,
                                    wxDC& dc,
                                    const wxRect& rect,
                                    int /*flags*/)
{
    BView* view = GetBViewFromDC(dc);
    HaikuRendererViewLock viewLock(view);
    if (!viewLock.IsLocked() || !be_control_look)
        return;

    BRect brect(rect.x, rect.y,
                rect.x + rect.width - 1,
                rect.y + rect.height - 1);

    rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);

    uint32 lookFlags = BControlLook::B_FOCUSED;
    be_control_look->DrawButtonFrame(view, brect, brect, base, base, lookFlags);
}

void wxRendererHaiku::DrawSplitterBorder(wxWindow* /*win*/,
                                         wxDC& dc,
                                         const wxRect& rect,
                                         int /*flags*/)
{
    BView* view = GetBViewFromDC(dc);
    HaikuRendererViewLock viewLock(view);
    if (!viewLock.IsLocked())
        return;

    BRect brect(rect.x, rect.y,
                rect.x + rect.width - 1,
                rect.y + rect.height - 1);

    view->SetHighColor(ui_color(B_PANEL_BACKGROUND_COLOR));
    view->FillRect(brect);
}

void wxRendererHaiku::DrawSplitterSash(wxWindow* /*win*/,
                                       wxDC& dc,
                                       const wxSize& size,
                                       wxCoord position,
                                       wxOrientation orient,
                                       int /*flags*/)
{
    BView* view = GetBViewFromDC(dc);
    HaikuRendererViewLock viewLock(view);
    if (!viewLock.IsLocked() || !be_control_look)
        return;

    BRect brect;
    if (orient == wxHORIZONTAL)
        brect = BRect(position, 0, position + 4, size.y);
    else
        brect = BRect(0, position, size.x, position + 4);

    rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);
    be_control_look->DrawSplitter(view, brect, brect, base, orient == wxHORIZONTAL ? B_HORIZONTAL : B_VERTICAL);
}

void wxRendererHaiku::DrawGauge(wxWindow* win,
                                wxDC& dc,
                                const wxRect& rect,
                                int value,
                                int max,
                                int flags)
{
    BView* view = GetBViewFromDC(dc);
    HaikuRendererViewLock viewLock(view);
    if (!viewLock.IsLocked() || !be_control_look)
        return;

    BRect brect(rect.x, rect.y,
                rect.x + rect.width - 1,
                rect.y + rect.height - 1);

    rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);
    rgb_color bar = { 0, 120, 215, 255 }; // Custom bar color (Windows 10 blue)

    // Clamp values to safe ranges
    if (max <= 0)
        max = 1;

    float progress = static_cast<float>(value) / max;
    if (progress < 0.0f)
        progress = 0.0f;
    else if (progress > 1.0f)
        progress = 1.0f;

    uint32 lookFlags = 0;

    be_control_look->DrawStatusBar(view,
                                   brect,
                                   brect,         // updateRect (same as brect here)
                                   base,
                                   bar,
                                   progress);
}


wxSplitterRenderParams wxRendererHaiku::GetSplitterParams(const wxWindow* /*win*/)
{
    return wxSplitterRenderParams(4, 0, true);
}


