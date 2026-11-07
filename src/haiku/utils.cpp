#include "wx/haiku/private.h"
#include "wx/utils.h"
#include "wx/window.h"
#include "wx/log.h"
#include "wx/apptrait.h"

#include <Application.h>
#include <Screen.h>
#include <OS.h>
#include <Roster.h>
#include <Path.h>
#include <Beep.h>
#include <Window.h>
#include <stdlib.h>
#include "wx/haiku/private/viewmap.h"
// ----------------------------------------------------------------------------
// Global utilities
// ----------------------------------------------------------------------------



void wxMissingImplementation(const char fileName[], unsigned lineNumber, const char feature[])
{
    fprintf(stderr, "%s(%u): Missing implementation of \"%s\"\n", fileName, lineNumber, feature);
}

//#if wxUSE_GUI
void wxGetMousePosition(int* x, int* y)
{
    BPoint pt;
    uint32 buttons;
    get_mouse(&pt, &buttons);
    if (x) *x = (int)pt.x;
    if (y) *y = (int)pt.y;
}
//ndif

//#if wxUSE_GUI
wxMouseState wxGetMouseState()
{
    BPoint pt;
    uint32 buttons;
    get_mouse(&pt, &buttons);

    wxMouseState state;
    state.SetPosition(wxPoint((int)pt.x, (int)pt.y));
    const uint32 mods = modifiers();
    state.SetShiftDown((mods & B_SHIFT_KEY) != 0);
    state.SetControlDown((mods & B_CONTROL_KEY) != 0);
    state.SetAltDown((mods & B_OPTION_KEY) != 0);
    state.SetMetaDown((mods & B_COMMAND_KEY) != 0);
    state.SetLeftDown(buttons & B_PRIMARY_MOUSE_BUTTON);
    state.SetRightDown(buttons & B_SECONDARY_MOUSE_BUTTON);
    state.SetMiddleDown(buttons & B_TERTIARY_MOUSE_BUTTON);
    state.SetAux1Down(buttons & B_MOUSE_BUTTON(3));
    state.SetAux2Down(buttons & B_MOUSE_BUTTON(4));

    return state;
}
//#endif

wxWindow* wxFindWindowAtPoint(const wxPoint&)
{
    wxMissingImplementation(__FILE__, __LINE__, "wxFindWindowAtPoint");
    return nullptr;
}

bool wxGetKeyState(wxKeyCode key)
{
    // Preserve existing modifier-key handling.
    const uint32 mods = ::modifiers();

    switch (key)
    {
        case WXK_CONTROL: return (mods & B_CONTROL_KEY) != 0;
        case WXK_SHIFT:   return (mods & B_SHIFT_KEY) != 0;
        case WXK_ALT:     return (mods & B_OPTION_KEY) != 0;
        case WXK_CAPITAL: return (mods & B_CAPS_LOCK) != 0;
        case WXK_NUMLOCK: return (mods & B_NUM_LOCK) != 0;
        default: break;
    }

    // Initial implementation: ordinary ASCII keys.
    int wanted = static_cast<int>(key);

    if (wanted < 32 || wanted > 126)
        return false;

    // Normalize letters to lowercase.
    if (wanted >= 'A' && wanted <= 'Z')
        wanted += 'a' - 'A';

    key_info info = {};

    if (get_key_info(&info) != B_OK)
        return false;

    key_map* map = nullptr;
    char* buffer = nullptr;

    get_key_map(&map, &buffer);

    if (!map || !buffer)
    {
        free(map);
        free(buffer);
        return false;
    }

    bool pressed = false;

    for (int physical = 0; physical < 128; ++physical)
    {
        const int32 offset = map->normal_map[physical];

        if (offset < 0)
            continue;

        const unsigned char* entry =
            reinterpret_cast<const unsigned char*>(buffer + offset);

        if (entry[0] != 1)
            continue;

        int character = entry[1];

        if (character >= 'A' && character <= 'Z')
            character += 'a' - 'A';

        if (character != wanted)
            continue;

        if ((info.key_states[physical / 8] &
             (1 << (7 - (physical % 8)))) != 0)
        {
            pressed = true;
            break;
        }
    }

    free(map);
    free(buffer);

    return pressed;
}

void wxBell()
{
    beep();
}

wxWindow* wxGetActiveWindow()
{
    if (!be_app) return nullptr;
    for (int32 i = 0; BWindow* win = be_app->WindowAt(i); ++i)
    {
        if (!win->Lock()) continue;
        wxWindow* result = nullptr;
        if (win->IsActive())
            for (int32 j = 0; BView* view = win->ChildAt(j); ++j)
                if ((result = wxHaiku::GetWxFromView(view))) break;
        win->Unlock();
        if (result) return wxGetTopLevelParent(result);
    }
    return nullptr;
}

bool wxLaunchDefaultApplication(const wxString& path, int WXUNUSED(flags))
{
    BPath bpath(wxToUTF8(path));
    entry_ref ref;
    if (get_ref_for_path(bpath.Path(), &ref) != B_OK)
        return false;

    return be_roster->Launch(&ref) == B_OK;
}

bool wxLaunchDefaultBrowser(const wxString& url)
{
    entry_ref ref;
    if (!be_roster || get_ref_for_path("/bin/open", &ref) != B_OK) return false;
    const wxCharBuffer encoded = url.utf8_str();
    const char* args[] = {encoded.data()};
    return be_roster->Launch(&ref, 1, args) == B_OK;
}

wxString wxGUIAppTraits::GetPlatformDescription() const
{
    return "Haiku OS native backend";
}
