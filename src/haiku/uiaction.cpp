/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/uiaction.cpp
// Purpose:     wxUIActionSimulator implementation for Haiku
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/haiku/private.h"
#include "wx/uiaction.h"
#include "wx/private/uiaction.h"
#include "wx/log.h"

#include <Application.h>
#include <Autolock.h>
#include <View.h>
#include <private/app/MessagePrivate.h>
#include <Messenger.h>
#include "wx/window.h"
#include "wx/haiku/private/dispatch.h"
#include <Message.h>
#include <OS.h>
#include <Screen.h>
#include <Window.h>
#include <WindowScreen.h>

#if wxUSE_UIACTIONSIMULATOR



class wxUIActionSimulatorHaikuImpl : public wxUIActionSimulatorImpl
{
public:


    wxUIActionSimulatorHaikuImpl() = default;

    bool MouseMove(long x, long y) override;
    bool MouseDown(int button = wxMOUSE_BTN_LEFT) override;
    bool MouseUp(int button = wxMOUSE_BTN_LEFT) override;
    bool MouseClick(int button = wxMOUSE_BTN_LEFT) override;
    bool MouseDblClick(int button = wxMOUSE_BTN_LEFT) override;
    bool DoKey(int keycode, int modifiers, bool isDown) override;
    bool MouseDragDrop(long, long, long, long, int) override { return false; }

private:
    uint32 ConvertMouseButton(int button) const;
    uint32 ConvertModifiers(int modifiers) const;
    uint32 ConvertKeyCode(int keycode) const;

    int m_pressedModifiers = 0;

    bool GetMouse(BPoint& point, uint32& buttons) const;
    bool SendMouse(uint32 what, const BPoint& point, uint32 buttons,
                   int32 clicks = 1);
    bool SendButton(int button, bool down, int32 clicks = 1);

  //  wxDECLARE_NO_COPY_CLASS(wxUIActionSimulatorHaikuImpl);
};

bool wxUIActionSimulatorHaikuImpl::GetMouse(BPoint& point, uint32& buttons) const
{
    if (!be_app)
        return false;
    wxHaikuGuiDispatchPause pause;
    for (int32 i = 0; BWindow* window = be_app->WindowAt(i); ++i)
    {
        BAutolock lock(window);
        BView* view = lock.IsLocked() ? window->ChildAt(0) : nullptr;
        if (view)
        {
            view->GetMouse(&point, &buttons, false);
            view->ConvertToScreen(&point);
            return true;
        }
    }
    return false;
}

bool wxUIActionSimulatorHaikuImpl::SendMouse(uint32 what, const BPoint& point,
                                            uint32 buttons, int32 clicks)
{
    // This is the native input stream consumed by app_server. Sending to a
    // BView alone would not update GetMouse(), which native controls use when
    // tracking a press. The private endpoint is looked up for every event so
    // an unavailable/restarted input server reports failure.
    const port_id port = find_port("input server target");
    port_info info;
    if (port < B_OK || get_port_info(port, &info) != B_OK)
        return false;

    BMessage event(what);
    event.AddInt64("when", system_time());
    event.AddPoint("where", point);
    event.AddInt32("buttons", buttons);
    event.AddInt32("clicks", clicks);
    event.AddInt32("modifiers", modifiers() | ConvertModifiers(m_pressedModifiers));
    BMessenger reply;
    const status_t status = BMessage::Private(event).SendMessage(
        port, info.team, 0, 1000000, false, reply);
    if (status != B_OK)
        return false;
    // Give app_server and the target looper time to consume the native event.
    { wxHaikuGuiDispatchPause pause; snooze(20000); }
    return true;
}

bool wxUIActionSimulatorHaikuImpl::MouseMove(long x, long y)
{
    BScreen screen;
    const BPoint point((float)x, (float)y);
    if (!screen.Frame().Contains(point))
        return false;
    BPoint current;
    uint32 buttons;
    if (!GetMouse(current, buttons))
        return false;
    if (buttons == 0)
    {
        // Keep the hardware cursor in sync using the public Game Kit API.
        // While a simulated button is held, use the event stream to preserve
        // the pressed state instead of input_server's physical device state.
        set_mouse_position(static_cast<int32>(x), static_cast<int32>(y));
        wxHaikuGuiDispatchPause pause;
        snooze(20000);
    }
    return SendMouse(B_MOUSE_MOVED, point, buttons);
}

uint32 wxUIActionSimulatorHaikuImpl::ConvertMouseButton(int button) const
{
    switch (button)
    {
        case wxMOUSE_BTN_LEFT:   return B_PRIMARY_MOUSE_BUTTON;
        case wxMOUSE_BTN_RIGHT:  return B_SECONDARY_MOUSE_BUTTON;
        case wxMOUSE_BTN_MIDDLE: return B_TERTIARY_MOUSE_BUTTON;
        // The Haiku wx event bridge currently maps only the first three.
        case wxMOUSE_BTN_AUX1:
        case wxMOUSE_BTN_AUX2:   return 0;
        default:
            wxFAIL_MSG("Unsupported mouse button");
            return 0;
    }
}

bool wxUIActionSimulatorHaikuImpl::SendButton(int button, bool down, int32 clicks)
{
    const uint32 mask = ConvertMouseButton(button);
    BPoint point;
    uint32 buttons;
    if (!mask || !GetMouse(point, buttons))
        return false;
    buttons = down ? buttons | mask : buttons & ~mask;
    return SendMouse(down ? B_MOUSE_DOWN : B_MOUSE_UP, point, buttons, clicks);
}

bool wxUIActionSimulatorHaikuImpl::MouseDown(int button)
{
    return SendButton(button, true);
}

bool wxUIActionSimulatorHaikuImpl::MouseUp(int button)
{
    return SendButton(button, false);
}

bool wxUIActionSimulatorHaikuImpl::MouseClick(int button)
{
    return MouseDown(button) && MouseUp(button);
}

bool wxUIActionSimulatorHaikuImpl::MouseDblClick(int button)
{
    return MouseClick(button) && SendButton(button, true, 2) &&
           SendButton(button, false, 2);
}
uint32 wxUIActionSimulatorHaikuImpl::ConvertModifiers(int modifiers) const
{
    uint32 mods = 0;
    if (modifiers & wxMOD_SHIFT)   mods |= B_SHIFT_KEY;
    if (modifiers & wxMOD_ALT)     mods |= B_OPTION_KEY;
    if (modifiers & wxMOD_CONTROL) mods |= B_CONTROL_KEY;
    if (modifiers & wxMOD_META)    mods |= B_COMMAND_KEY;
    return mods;
}

// Map keycodes — basic mapping only
uint32 wxUIActionSimulatorHaikuImpl::ConvertKeyCode(int keycode) const
{
    switch (keycode)
    {
        case WXK_BACK:    return B_BACKSPACE;
        case WXK_TAB:     return B_TAB;
        case WXK_RETURN:  return B_RETURN;
        case WXK_ESCAPE:  return B_ESCAPE;
        case WXK_SPACE:   return ' ';
        case WXK_DELETE:  return B_DELETE;
        case WXK_HOME:    return B_HOME;
        case WXK_END:     return B_END;
        case WXK_LEFT:    return B_LEFT_ARROW;
        case WXK_UP:      return B_UP_ARROW;
        case WXK_RIGHT:   return B_RIGHT_ARROW;
        case WXK_DOWN:    return B_DOWN_ARROW;
        case WXK_PAGEUP:  return B_PAGE_UP;
        case WXK_PAGEDOWN:return B_PAGE_DOWN;
        default:
            if (keycode >= 32 && keycode <= 126) return static_cast<uint32>(keycode);
            return 0;
    }
}

bool wxUIActionSimulatorHaikuImpl::DoKey(int keycode, int modifiers, bool isDown)
{
    int modifierKey = 0;
    switch (keycode)
    {
        case WXK_SHIFT:   modifierKey = wxMOD_SHIFT; break;
        case WXK_ALT:     modifierKey = wxMOD_ALT; break;
        case WXK_CONTROL: modifierKey = wxMOD_CONTROL; break;
        default: break;
    }
    const bool functionKey = keycode >= WXK_F1 && keycode <= WXK_F12;
    const uint32 raw = functionKey ? B_FUNCTION_KEY : ConvertKeyCode(keycode);
    if (!raw && !modifierKey)
        return false;

    int pressed = m_pressedModifiers;
    if (modifierKey)
    {
        pressed = isDown ? pressed | modifierKey : pressed & ~modifierKey;
        modifiers = pressed;
    }
    else
    {
        modifiers |= pressed;
    }

    wxWindow* focus = nullptr;
    for (int attempt = 0; attempt < 50; ++attempt)
    {
        focus = wxWindow::FindFocus();
        if (focus) break;
        wxHaikuGuiDispatchPause pause;
        snooze(10000);
    }
    BView* view = focus ? focus->GetHandle() : nullptr;
    BWindow* window = view ? view->Window() : nullptr;
    if (!window || !window->Lock()) return false;
    // Post native input to the application's actual focused view, including
    // the inner BTextView of BTextControl. This is application-local input,
    // not an input_server device and does not alter global key state.
    BView* target = window->CurrentFocus();
    BMessenger destination(target);
    window->Unlock();
    if (!target || !destination.IsValid()) return false;
    uint32 translated = raw;
    if (raw >= 'A' && raw <= 'Z' && !(modifiers & wxMOD_SHIFT)) translated += 'a'-'A';
    if (raw >= 'a' && raw <= 'z' && (modifiers & wxMOD_SHIFT)) translated -= 'a'-'A';
    if (modifiers & wxMOD_SHIFT)
    {
        const char* plain = "`1234567890-=\\[];',./";
        const char* shifted = "~!@#$%^&*()_+|{}\":<>?";
        for (size_t i = 0; plain[i]; ++i)
            if (raw == static_cast<uint32>(plain[i])) { translated = shifted[i]; break; }
    }
    if (modifiers & wxMOD_CONTROL)
    {
        if (raw >= 'A' && raw <= 'Z') translated = raw-'A'+1;
        else if (raw >= 'a' && raw <= 'z') translated = raw-'a'+1;
    }
    const wxCharBuffer bytes = raw ? wxString(wxUniChar(translated)).utf8_str()
                                   : wxCharBuffer("");
    BMessage message(isDown ? B_KEY_DOWN : B_KEY_UP);
    message.AddInt64("when", system_time());
    message.AddInt32("raw_char", raw);
    if (functionKey)
        message.AddInt32("key", B_F1_KEY + keycode - WXK_F1);
    message.AddInt32("modifiers", ConvertModifiers(modifiers));
    message.AddString("bytes", bytes.data());
    if (modifierKey)
        message.AddInt32("wx:modifier_key", keycode);
    const status_t status = destination.SendMessage(&message);
    if (status == B_OK)
        m_pressedModifiers = pressed;

    // Let the native looper run before the caller checks resulting wx events.
    { wxHaikuGuiDispatchPause pause; snooze(20000); }
    return status == B_OK;
}

wxUIActionSimulator::wxUIActionSimulator()
    : m_impl(new wxUIActionSimulatorHaikuImpl)
{
}

wxUIActionSimulator::~wxUIActionSimulator()
{
    delete m_impl;
}

#endif // wxUSE_UIACTIONSIMULATOR
