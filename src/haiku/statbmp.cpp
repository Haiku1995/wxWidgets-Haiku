/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/statbmp.cpp
// Purpose:     wxStaticBitmap implementation for Haiku
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"
#include <Autolock.h>

//#if wxUSE_STATBMP

#ifndef WX_PRECOMP
    #include "wx/statbmp.h"
#endif

#include "wx/statbmp.h"
#include "wx/bitmap.h"
#include "wx/haiku/private.h"
#include "wx/log.h"

#include <View.h>
#include <Bitmap.h>

//wxIMPLEMENT_DYNAMIC_CLASS(wxStaticBitmap, wxStaticBitmapBase);

static BRect wxToBRect(const wxPoint& pos, const wxSize& size)
{
    const wxPoint p = (pos == wxDefaultPosition) ? wxPoint(0, 0) : pos;

    if (size == wxDefaultSize)
    {
        return BRect((float)p.x, (float)p.y,
                     (float)(p.x + 16 - 1),
                     (float)(p.y + 16 - 1));
    }

    return BRect((float)p.x, (float)p.y,
                 (float)(p.x + size.x - 1),
                 (float)(p.y + size.y - 1));
}

class wxHaikuStaticBitmapView : public BView
{
public:
    wxHaikuStaticBitmapView(BRect frame, wxStaticBitmap* owner)
        : BView(frame, "wxStaticBitmap",
                B_FOLLOW_LEFT | B_FOLLOW_TOP,
                B_WILL_DRAW | B_FRAME_EVENTS)
    {
        wxUnusedVar(owner);
        SetViewColor(B_TRANSPARENT_COLOR);
    }

    void Draw(BRect WXUNUSED(update)) override
    {
        wxBitmap bmp = m_bitmap;
        if (!bmp.IsOk())
            return;

        wxBitmapRefData* ref =
            static_cast<wxBitmapRefData*>(bmp.GetRefData());
        if (!ref || !ref->m_bitmap)
            return;

        BBitmap* bbmp = bmp.GetBBitmap();
        const BRect src = bbmp->Bounds();
        const BRect dst = Bounds();

        PushState();
        if (bmp.HasAlpha())
        {
            SetDrawingMode(B_OP_ALPHA);
            SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
        }
        DrawBitmap(bbmp, src, dst);
        PopState();
    }

    void SetBitmap(const wxBitmap& bitmap) { m_bitmap = bitmap; }

private:
    // Draw runs on the native window thread. Keep a value snapshot rather
    // than dereferencing a wx owner that may already be destroyed.
    wxBitmap m_bitmap;
};

wxStaticBitmap::wxStaticBitmap()
{
}

wxStaticBitmap::wxStaticBitmap(wxWindow* parent,
                               wxWindowID id,
                               const wxBitmapBundle& label,
                               const wxPoint& pos,
                               const wxSize& size,
                               long style,
                               const wxString& name)
{
    Create(parent, id, label, pos, size, style, name);
}

bool wxStaticBitmap::Create(wxWindow* parent,
                            wxWindowID id,
                            const wxBitmapBundle& label,
                            const wxPoint& pos,
                            const wxSize& size,
                            long style,
                            const wxString& name)
{
    m_bitmapBundle = label;

    m_haikuView = new wxHaikuStaticBitmapView(wxToBRect(pos, size), this);

    HaikuAdoptView(m_haikuView, nullptr, m_haikuView);

    if (!wxStaticBitmapBase::Create(parent, id, pos, size, style,
                                    wxDefaultValidator, name))
    {
        return false;
    }

    SetBitmap(label);
    // Default dimensions follow the bitmap, not the parent client area.
    SetInitialSize(size);
    return true;
}

void wxStaticBitmap::SetBitmap(const wxBitmapBundle& bitmap)
{
    // Native invalidation must hold the view looper lock, including when
    // another window/application looper changes the bitmap.
    BLooper* looper = m_haikuView ? m_haikuView->Looper() : nullptr;
    BAutolock lock(looper);
    if (looper && !lock.IsLocked())
        return;

    m_bitmapBundle = bitmap;

    if (m_haikuView)
    {
        static_cast<wxHaikuStaticBitmapView*>(m_haikuView)->SetBitmap(bitmap.GetBitmapFor(this));
        m_haikuView->Invalidate();
    }

    InvalidateBestSize();
}

wxBitmap wxStaticBitmap::GetBitmap() const
{
    return m_bitmapBundle.GetBitmapFor(this);
}
