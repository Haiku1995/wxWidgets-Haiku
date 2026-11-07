/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/dcscreen.cpp
// Purpose:     wxScreenDCImpl implementation for Haiku
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"
#include "wx/log.h"

#include "wx/dcscreen.h"
#include "wx/haiku/dcscreen.h"
#include "wx/haiku/converter.h"

#include <Screen.h>
#include <Bitmap.h>
#include <View.h>

// ----------------------------------------------------------------------------
// wxScreenDCImpl
// ----------------------------------------------------------------------------

//wxIMPLEMENT_ABSTRACT_CLASS(wxScreenDCImpl, wxWindowDCImpl);
wxIMPLEMENT_ABSTRACT_CLASS(wxScreenDCImpl, wxWindowDCImpl);

wxScreenDCImpl::wxScreenDCImpl(wxScreenDC* owner)
    : wxWindowDCImpl(owner),
      m_screenBitmap(nullptr),
      m_screenView(nullptr)
{
    // FIX: The original class grabbed a screen snapshot into m_screenBitmap
    // but never set m_view, so none of the base-class drawing methods
    // (DoDrawLine, DoDrawBitmap, etc.) worked — they all guard with
    // "if (!m_view) return;". For a screen DC we create a BView attached
    // to the screen bitmap so the inherited drawing path works correctly.
    //
    // Additionally, wxScreenDC is supposed to draw *onto* the screen, not
    // onto a snapshot. On Haiku there is no direct "draw to screen" API
    // comparable to Windows GetDC(NULL). The closest approximation is:
    //   1. Draw into a screen-format BBitmap using a BView.
    //   2. Use BScreen::GetBitmap() for pixel-reading (DoGetPixel).
    //
    // We create an off-screen BBitmap matching the screen dimensions and
    // attach a BView to it. This supports all drawing and pixel-read ops.
    // Anything drawn here won't appear on-screen directly (Haiku has no
    // raw framebuffer write API for apps), but this is the correct
    // foundation for things like wxScreenDC-based screenshot utilities.

    BScreen screen;
    if (!screen.IsValid())
    {
        wxLogError("wxScreenDCImpl: BScreen is not valid");
        m_ok = false;
        return;
    }

    BRect frame = screen.Frame();

    // Create a screen-format bitmap that accepts views.
    m_screenBitmap = new BBitmap(frame,
                                 screen.ColorSpace(),
                                 /*acceptsViews=*/true);

    if (!m_screenBitmap->IsValid())
    {
        wxLogError("wxScreenDCImpl: failed to create screen-format BBitmap");
        delete m_screenBitmap;
        m_screenBitmap = nullptr;
        m_ok = false;
        return;
    }

    // FIX: Seed the bitmap with the current screen contents so that blitting
    // FROM the screen DC returns real pixel data.
    BBitmap* snapshot = nullptr;
    if (screen.GetBitmap(&snapshot, false) == B_OK && snapshot)
    {
        // Copy snapshot pixels into our drawing bitmap.
        const int bytes = (int)std::min(snapshot->BitsLength(),
                                        m_screenBitmap->BitsLength());
        memcpy(m_screenBitmap->Bits(), snapshot->Bits(), bytes);
        delete snapshot;
    }

    // Attach a BView so inherited drawing methods can draw into the bitmap.
    m_screenView = new BView(frame, "screendcview", B_FOLLOW_NONE, B_WILL_DRAW);
    m_screenBitmap->AddChild(m_screenView);

    if (m_screenView->Parent() == nullptr && m_screenView->Window() == nullptr)
    {
        // Bitmap was not created with acceptsViews — should not happen above,
        // but be defensive.
        wxLogError("wxScreenDCImpl: BBitmap does not accept views");
        delete m_screenView;
        m_screenView = nullptr;
        delete m_screenBitmap;
        m_screenBitmap = nullptr;
        m_ok = false;
        return;
    }

    // Set m_view so the base class draw methods work.
    m_view   = m_screenView;
    m_bitmap = m_screenBitmap;  // for DoBlit / DoGetPixel
    m_ok     = true;
}

wxScreenDCImpl::~wxScreenDCImpl()
{
    // FIX: Sync the view before destroying it so all pending drawing is
    // committed to the bitmap's pixel buffer.
    if (m_screenView && m_ok)
    {
        if (m_screenView->LockLooper())
        {
            m_screenView->Sync();
            m_screenView->UnlockLooper();
        }
    }

    // Clear the alias before the base class destructor runs.
    m_view   = nullptr;
    m_bitmap = nullptr;

    // m_screenView is a child of m_screenBitmap; removing it before
    // deleting the bitmap keeps ownership clean.
    if (m_screenView && m_screenBitmap)
        m_screenBitmap->RemoveChild(m_screenView);

    delete m_screenView;
    m_screenView = nullptr;

    delete m_screenBitmap;
    m_screenBitmap = nullptr;
}

void wxScreenDCImpl::DoGetSize(int* width, int* height) const
{
    // FIX: Validate BScreen before using it. The original code used the
    // BScreen unconditionally; if the display is unavailable it returns
    // garbage from an invalid BRect.
    BScreen screen;
    if (!screen.IsValid())
    {
        if (width)  *width  = 0;
        if (height) *height = 0;
        return;
    }

    BRect frame = screen.Frame();
    if (width)  *width  = static_cast<int>(frame.Width())  + 1;
    if (height) *height = static_cast<int>(frame.Height()) + 1;
}

wxBitmap wxScreenDCImpl::DoGetAsBitmap(const wxRect* subrect) const
{
    // A wxScreenDC represents a desktop capture on Haiku. Acquire a fresh
    // snapshot here instead of returning the construction-time image so that
    // callers always receive the current desktop contents.
    BScreen screen;
    if (!screen.IsValid())
        return wxNullBitmap;

    BBitmap* snapshot = nullptr;
    if (screen.GetBitmap(&snapshot, true) != B_OK || !snapshot)
        return wxNullBitmap;

    wxBitmap bitmap = HaikuWxConverter::BBitmapToWxBitmap(*snapshot);
    delete snapshot;

    if (!bitmap.IsOk())
        return wxNullBitmap;

    if (!subrect)
        return bitmap;

    const wxRect bounds(0, 0, bitmap.GetWidth(), bitmap.GetHeight());
    const wxRect requested = bounds.Intersect(*subrect);
    return requested.IsEmpty() ? wxNullBitmap : bitmap.GetSubBitmap(requested);
}
