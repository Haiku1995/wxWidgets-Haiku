/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/settings.cpp
// Purpose:     System settings implementation for Haiku
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

//#if wxUSE_SYSTEM_SETTINGS

#include "wx/settings.h"
#include "wx/font.h"
#include "wx/log.h"
#include "wx/window.h"

#include <Application.h>
#include <ControlLook.h>
#include <InterfaceDefs.h>
#include <Screen.h>
#include <Font.h>

namespace
{

static wxColour wxFromRGBColor(const rgb_color& c)
{
    return wxColour(c.red, c.green, c.blue, c.alpha);
}

static int HaikuFontSizeToWxPointSize(float size)
{
    return wxRound(size / 72.0 * 96.0);
}

} // namespace

wxColour wxSystemSettingsNative::GetColour(wxSystemColour index)
{
    rgb_color color = ui_color(B_PANEL_BACKGROUND_COLOR);

    switch ( index )
    {
        case wxSYS_COLOUR_SCROLLBAR:
        case wxSYS_COLOUR_BACKGROUND:
        case wxSYS_COLOUR_INACTIVECAPTION:
        case wxSYS_COLOUR_MENU:
        case wxSYS_COLOUR_WINDOWFRAME:
        case wxSYS_COLOUR_ACTIVEBORDER:
        case wxSYS_COLOUR_INACTIVEBORDER:
        case wxSYS_COLOUR_BTNFACE:
#if wxSYS_COLOUR_3DFACE != wxSYS_COLOUR_BTNFACE
        case wxSYS_COLOUR_3DFACE:
#endif
        case wxSYS_COLOUR_APPWORKSPACE:
            color = ui_color(B_PANEL_BACKGROUND_COLOR);
            break;

        case wxSYS_COLOUR_WINDOW:
        case wxSYS_COLOUR_LISTBOX:
            color = ui_color(B_DOCUMENT_BACKGROUND_COLOR);
            break;

        case wxSYS_COLOUR_WINDOWTEXT:
        case wxSYS_COLOUR_LISTBOXTEXT:
            color = ui_color(B_DOCUMENT_TEXT_COLOR);
            break;

        case wxSYS_COLOUR_MENUBAR:
            color = ui_color(B_MENU_BACKGROUND_COLOR);
            break;

        case wxSYS_COLOUR_MENUTEXT:
            color = ui_color(B_MENU_ITEM_TEXT_COLOR);
            break;

        case wxSYS_COLOUR_HIGHLIGHT:
        case wxSYS_COLOUR_MENUHILIGHT:
        case wxSYS_COLOUR_ACTIVECAPTION:
            color = ui_color(B_MENU_SELECTION_BACKGROUND_COLOR);
            break;

        case wxSYS_COLOUR_HIGHLIGHTTEXT:
        case wxSYS_COLOUR_LISTBOXHIGHLIGHTTEXT:
            color = ui_color(B_MENU_SELECTED_ITEM_TEXT_COLOR);
            break;

        case wxSYS_COLOUR_GRAYTEXT:
#ifdef B_DISABLED_LABEL_TINT
            color = tint_color(ui_color(B_PANEL_TEXT_COLOR), B_DISABLED_LABEL_TINT);
#else
            color = tint_color(ui_color(B_PANEL_TEXT_COLOR), B_DARKEN_1_TINT);
#endif
            break;

        case wxSYS_COLOUR_BTNTEXT:
        case wxSYS_COLOUR_CAPTIONTEXT:
        case wxSYS_COLOUR_INACTIVECAPTIONTEXT:
            color = ui_color(B_PANEL_TEXT_COLOR);
            break;

        case wxSYS_COLOUR_3DLIGHT:
        case wxSYS_COLOUR_BTNHIGHLIGHT:
#if wxSYS_COLOUR_3DHIGHLIGHT != wxSYS_COLOUR_BTNHIGHLIGHT
        case wxSYS_COLOUR_3DHIGHLIGHT:
#endif
            color = tint_color(ui_color(B_PANEL_BACKGROUND_COLOR), B_LIGHTEN_2_TINT);
            break;

        case wxSYS_COLOUR_3DSHADOW:
#if wxSYS_COLOUR_BTNSHADOW != wxSYS_COLOUR_3DSHADOW
        case wxSYS_COLOUR_BTNSHADOW:
#endif
            color = tint_color(ui_color(B_PANEL_BACKGROUND_COLOR), B_DARKEN_1_TINT);
            break;

        case wxSYS_COLOUR_3DDKSHADOW:
            color = tint_color(ui_color(B_PANEL_BACKGROUND_COLOR), B_DARKEN_2_TINT);
            break;

        case wxSYS_COLOUR_INFOBK:
            color = ui_color(B_TOOL_TIP_BACKGROUND_COLOR);
            break;

        case wxSYS_COLOUR_INFOTEXT:
            color = ui_color(B_TOOL_TIP_TEXT_COLOR);
            break;

        case wxSYS_COLOUR_HOTLIGHT:
#ifdef B_CONTROL_HIGHLIGHT_COLOR
            color = ui_color(B_CONTROL_HIGHLIGHT_COLOR);
#else
            color = ui_color(B_KEYBOARD_NAVIGATION_COLOR);
#endif
            break;

        case wxSYS_COLOUR_GRADIENTACTIVECAPTION:
        case wxSYS_COLOUR_GRADIENTINACTIVECAPTION:
            color = ui_color(B_MENU_BACKGROUND_COLOR);
            break;

        default:
            wxLogTrace("haiku",
                       "Unknown wxSystemColour index %d, using panel background.",
                       (int)index);
            color = ui_color(B_PANEL_BACKGROUND_COLOR);
            break;
    }

    return wxFromRGBColor(color);
}

wxFont wxSystemSettingsNative::GetFont(wxSystemFont index)
{
    const BFont* sysFont = be_plain_font;

    switch ( index )
    {
        case wxSYS_OEM_FIXED_FONT:
        case wxSYS_ANSI_FIXED_FONT:
        case wxSYS_SYSTEM_FIXED_FONT:
            sysFont = be_fixed_font;
            break;

        case wxSYS_ANSI_VAR_FONT:
        case wxSYS_SYSTEM_FONT:
        case wxSYS_DEVICE_DEFAULT_FONT:
        case wxSYS_DEFAULT_GUI_FONT:
        default:
            sysFont = be_plain_font;
            break;
    }

    return wxFont(*sysFont);
}

int wxSystemSettingsNative::GetMetric(wxSystemMetric index, const wxWindow* win)
{
    wxUnusedVar(win);

    switch ( index )
    {
        case wxSYS_BORDER_X:
        case wxSYS_BORDER_Y:
        case wxSYS_EDGE_X:
        case wxSYS_EDGE_Y:
            return 1;

        case wxSYS_FRAMESIZE_X:
        case wxSYS_FRAMESIZE_Y:
            return 3;

        case wxSYS_CURSOR_X:
        case wxSYS_CURSOR_Y:
            return 16;

        case wxSYS_DCLICK_X:
        case wxSYS_DCLICK_Y:
            return 4;

        case wxSYS_DCLICK_MSEC:
        {
            bigtime_t clickSpeed;
            if ( get_click_speed(&clickSpeed) == B_OK )
                return (int)(clickSpeed / 1000);
            return 500;
        }

        case wxSYS_DRAG_X:
        case wxSYS_DRAG_Y:
            return 3;

        case wxSYS_ICON_X:
        case wxSYS_ICON_Y:
        case wxSYS_SMALLICON_X:
        case wxSYS_SMALLICON_Y:
            return 16;

        case wxSYS_HSCROLL_Y:
        case wxSYS_VSCROLL_X:
            return 16;

        case wxSYS_CAPTION_Y:
            return 20;

        case wxSYS_MENU_Y:
            return 26;

        case wxSYS_SCREEN_X:
        {
            BScreen screen;
            return (int)screen.Frame().IntegerWidth() + 1;
        }

        case wxSYS_SCREEN_Y:
        {
            BScreen screen;
            return (int)screen.Frame().IntegerHeight() + 1;
        }

        case wxSYS_NETWORK_PRESENT:
            return 1;

        case wxSYS_PENWINDOWS_PRESENT:
        case wxSYS_SWAP_BUTTONS:
            return 0;

        default:
            return -1;
    }
}

bool wxSystemSettingsNative::HasFeature(wxSystemFeature index)
{
    switch ( index )
    {
        case wxSYS_CAN_DRAW_FRAME_DECORATIONS:
        case wxSYS_CAN_ICONIZE_FRAME:
            return true;

#ifdef wxSYS_CAN_MINIMIZE_FRAME
        case wxSYS_CAN_MINIMIZE_FRAME:
            return true;
#endif

#ifdef wxSYS_CAN_MAXIMIZE_FRAME
        case wxSYS_CAN_MAXIMIZE_FRAME:
            return true;
#endif

        case wxSYS_TABLET_PRESENT:
            return false;

        default:
            return false;
    }
}

//#endif // wxUSE_SYSTEM_SETTINGS
