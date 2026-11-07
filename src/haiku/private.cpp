///////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/private.cpp
// Purpose:     Private helpers for Haiku port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#ifdef __HAIKU__

#include "wx/haiku/private.h"
#include "wx/window.h"
#include "wx/toplevel.h"
#include "wx/log.h"

#include <View.h>
#include <Window.h>
#include <Message.h>

#include <unordered_map>
#include <mutex>

// ---------------------------------------------------------------------------
// Native → wxWidgets maps
// ---------------------------------------------------------------------------

static std::unordered_map<BView*, wxWindow*> gs_viewMap;
static std::unordered_map<BWindow*, wxTopLevelWindow*> gs_windowMap;
static std::mutex gs_nativeMapMutex;

// ---------------------------------------------------------------------------
// wxWindow <-> BView helpers
// ---------------------------------------------------------------------------

wxWindow* wxCreateWindowFromBView(wxWindow* parent,
                                 BView* view,
                                 int id)
{
    if (!view)
        return nullptr;

    wxWindow* win = new wxWindow();

    if (!win->Create(parent,
                     id,
                     wxDefaultPosition,
                     wxDefaultSize,
                     0))
    {
        delete win;
        return nullptr;
    }

    // Only map — NO attaching here
    std::lock_guard<std::mutex> lock(gs_nativeMapMutex);
    gs_viewMap[view] = win;

    return win;
}

wxWindow* wxFindWindowFromBView(BView* view)
{
    std::lock_guard<std::mutex> lock(gs_nativeMapMutex);
    auto it = gs_viewMap.find(view);
    return it != gs_viewMap.end() ? it->second : nullptr;
}

void wxRemoveWindowForBView(BView* view)
{
    std::lock_guard<std::mutex> lock(gs_nativeMapMutex);
    gs_viewMap.erase(view);
}

// ---------------------------------------------------------------------------
// wxTopLevelWindow <-> BWindow helpers
// ---------------------------------------------------------------------------

wxTopLevelWindow* wxCreateTLWFromBWindow(BWindow* bwin,
                                        int id)
{
    if (!bwin)
        return nullptr;

    wxTopLevelWindow* tlw = new wxTopLevelWindow();

    if (!tlw->Create(nullptr,
                     id,
                     wxEmptyString,
                     wxDefaultPosition,
                     wxDefaultSize,
                     0))
    {
        delete tlw;
        return nullptr;
    }

    // Store mapping
    std::lock_guard<std::mutex> lock(gs_nativeMapMutex);
    gs_windowMap[bwin] = tlw;

    return tlw;
}

wxTopLevelWindow* wxFindTLWFromBWindow(BWindow* bwin)
{
    std::lock_guard<std::mutex> lock(gs_nativeMapMutex);
    auto it = gs_windowMap.find(bwin);
    return it != gs_windowMap.end() ? it->second : nullptr;
}

void wxRemoveTLWForBWindow(BWindow* bwin)
{
    std::lock_guard<std::mutex> lock(gs_nativeMapMutex);
    gs_windowMap.erase(bwin);
}

// ---------------------------------------------------------------------------
// Bitmap conversion
// ---------------------------------------------------------------------------

wxBitmap wxBitmapFromBBitmap(BBitmap* bb)
{
    if (!bb || !bb->IsValid() ||
        (bb->ColorSpace() != B_RGB32 && bb->ColorSpace() != B_RGBA32))
        return wxNullBitmap;

    const bool hasAlpha = bb->ColorSpace() == B_RGBA32;

    const uint8* bits =
        static_cast<const uint8*>(bb->Bits());

    int w = bb->Bounds().IntegerWidth() + 1;
    int h = bb->Bounds().IntegerHeight() + 1;
    int bpr = bb->BytesPerRow();

    wxImage image(w, h, false);
    image.InitAlpha();

    uint8* dst = image.GetData();
    uint8* alpha = image.GetAlpha();

    for (int y = 0; y < h; ++y)
    {
        const uint8* src = bits + y * bpr;
        for (int x = 0; x < w; ++x)
        {
            dst[0] = src[2]; // R
            dst[1] = src[1]; // G
            dst[2] = src[0]; // B
            // B_RGB32's fourth byte is padding, not transparency.
            alpha[x] = hasAlpha ? src[3] : 255;

            src += 4;
            dst += 3;
        }
        alpha += w;
    }

    return wxBitmap(image);
}

// ---------------------------------------------------------------------------
// Global Haiku message hook
// ---------------------------------------------------------------------------

extern "C" void wxHandleHaikuMessage(BMessage* msg)
{
    if (!msg)
        return;

    switch (msg->what)
    {
        case 'TMR_':
        case 'TIP_':
            break;

        default:
            wxLogDebug("Unhandled Haiku message: '%c%c%c%c'",
                       (char)(msg->what >> 24),
                       (char)(msg->what >> 16),
                       (char)(msg->what >> 8),
                       (char)(msg->what));
            break;
    }
}

#endif // __HAIKU__
