/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/accel.cpp
// Author:      [Your Name]
// Copyright:   (c) 2025 wxWidgets dev team
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#if wxUSE_ACCEL

#include "wx/accel.h"
#include <InterfaceKit.h>            // BWindow, BMessage, etc.
#include <InterfaceDefs.h>
// ----------------------------------------------------------------------------
// wxAccelRefData: the data used by wxAcceleratorTable
// ----------------------------------------------------------------------------

class wxAccelRefData : public wxObjectRefData
{
public:
    wxAccelRefData() = default;

    wxAccelRefData(const wxAccelRefData& data)
        : wxObjectRefData()
        , m_accels(data.m_accels)
    {
    }

    std::vector<wxAcceleratorEntry> m_accels;
};

#define M_ACCELDATA ((wxAccelRefData *)m_refData)

// ============================================================================
// Implementation
// ============================================================================

wxIMPLEMENT_DYNAMIC_CLASS(wxAcceleratorTable, wxObject);

// Helper: Convert wx key code to Haiku uchar
static uchar wxKeyToHaikuKey(int wxKey)
{
    // Haiku uses ASCII for printable keys, and special codes for others.
    // For simplicity, assume wxKey is already in ASCII or standard virtual key.
    // Real implementation would map wxKEY_XXX to Haiku codes (e.g., B_F1_KEY = 0x100 + 60)
    if (wxKey >= 0x20 && wxKey <= 0x7E) {
        return static_cast<uchar>(wxKey);
    }

    // Map common function keys (example)
    switch (wxKey) {
        case WXK_F1:  return B_F1_KEY;
        case WXK_F2:  return B_F2_KEY;
        case WXK_F3:  return B_F3_KEY;
        case WXK_F4:  return B_F4_KEY;
        case WXK_F5:  return B_F5_KEY;
        case WXK_F6:  return B_F6_KEY;
        case WXK_F7:  return B_F7_KEY;
        case WXK_F8:  return B_F8_KEY;
        case WXK_F9:  return B_F9_KEY;
        case WXK_F10: return B_F10_KEY;
        case WXK_F11: return B_F11_KEY;
        case WXK_F12: return B_F12_KEY;
        case WXK_ESCAPE: return B_ESCAPE;
        case WXK_BACK:   return B_BACKSPACE;
        case WXK_TAB:    return B_TAB;
        case WXK_RETURN: return B_ENTER;
        case WXK_SPACE:  return B_SPACE;
        default:
            return 0; // unsupported
    }
}

// Helper: Convert wx modifier flags to Haiku modifiers
static uint32 wxModifiersToHaiku(int wxMods)
{
    uint32 haikuMods = 0;
    if (wxMods & wxACCEL_SHIFT)   haikuMods |= B_SHIFT_KEY;
    if (wxMods & wxACCEL_CTRL)    haikuMods |= B_CONTROL_KEY;
    if (wxMods & wxACCEL_ALT)     haikuMods |= B_OPTION_KEY;   // Alt = Option on Haiku
    // wxACCEL_CMD aliases wxACCEL_CTRL here: do not add a second native modifier.
    return haikuMods;
}

wxAcceleratorTable::wxAcceleratorTable()
{
}

wxAcceleratorTable::wxAcceleratorTable(int n, const wxAcceleratorEntry entries[])
{
    if (n <= 0) return;

    wxCHECK_RET(n > 0, "Invalid number of accelerator entries");

    m_refData = new wxAccelRefData;
    for (int i = 0; i < n; ++i) {
        M_ACCELDATA->m_accels.emplace_back(entries[i]);
    }
}

// Apply this accelerator table to a given Haiku BWindow
void wxAcceleratorTable::ApplyToWindow(BWindow* window, BHandler* target) const
{
    if (!IsOk() || !window) return;

    for (const auto& entry : M_ACCELDATA->m_accels) {
        uchar key = wxKeyToHaikuKey(entry.GetKeyCode());
        if (key == 0) continue; // skip unmappable keys

        uint32 mods = wxModifiersToHaiku(entry.GetFlags());
        int cmd = entry.GetCommand();

        // Create a BMessage that will be posted when shortcut is triggered
        BMessage* msg = new BMessage('ACMD');
        // Keep this payload in sync with wxHaikuShortcutHandler.  Using a
        // second field name silently discarded every native shortcut.
        msg->AddInt32("wxHaiku_Command", cmd);

        // Register the shortcut with the window
        window->AddShortcut(key, mods, msg, target ? target : window);
    }
}

void wxAcceleratorTable::RemoveFromWindow(BWindow* window) const
{
    if (!IsOk() || !window)
        return;

    for (const auto& entry : M_ACCELDATA->m_accels)
    {
        const uchar key = wxKeyToHaikuKey(entry.GetKeyCode());
        if (key != 0)
            window->RemoveShortcut(key, wxModifiersToHaiku(entry.GetFlags()));
    }
}

wxObjectRefData* wxAcceleratorTable::CreateRefData() const
{
    return new wxAccelRefData;
}

wxObjectRefData* wxAcceleratorTable::CloneRefData(const wxObjectRefData* data) const
{
    return new wxAccelRefData(*static_cast<const wxAccelRefData*>(data));
}

bool wxAcceleratorTable::IsOk() const
{
    return m_refData != nullptr;
}

#endif // wxUSE_ACCEL
