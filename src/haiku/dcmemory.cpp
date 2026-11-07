/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/dcmemory.cpp
// Purpose:     wxMemoryDCImpl implementation for Haiku
// Author:      Based on Haiku API; no Qt!
// Copyright:   (c) 2025 wxWidgets team
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"
#include "wx/log.h"
#include "wx/dcmemory.h"
#include "wx/haiku/dcmemory.h"

#include <InterfaceKit.h>
#include <SupportKit.h>
#include <cstring>
#include <map>
#include <mutex>

void wxMissingImplementation(const char fileName[], unsigned lineNumber,
    const char feature[]);

#define wxMISSING_IMPLEMENTATION(feature) \
    wxMissingImplementation(__FILE__, __LINE__, feature)

#define wxMISSING_FUNCTION() \
    wxMISSING_IMPLEMENTATION(__func__)

#if defined(WX_DEBUG_HAIKU) && WX_DEBUG_HAIKU
    #define HAIKU_LOG(msg, ...) \
        wxLogDebug("[HAIKU-DCMEM:%s:%d] " msg, __func__, __LINE__, ##__VA_ARGS__)
#else
    #define HAIKU_LOG(msg, ...) ((void)0)
#endif

static void HaikuDumpBitmap(const char* label, BBitmap* bmp)
{
#if defined(WX_DEBUG_HAIKU) && WX_DEBUG_HAIKU
    if (!bmp)
    {
        wxLogDebug("%s: bitmap=(nil)", label);
        return;
    }

    const BRect b = bmp->Bounds();

    wxLogDebug(
        "%s: ptr=%p valid=%d bits=%p bitsLen=%ld bytesPerRow=%ld colorSpace=%ld "
        "bounds=(%.1f,%.1f,%.1f,%.1f) size=(%.1f x %.1f)",
        label,
        bmp,
        bmp->IsValid(),
        bmp->Bits(),
        (long)bmp->BitsLength(),
        (long)bmp->BytesPerRow(),
        (long)bmp->ColorSpace(),
        b.left, b.top, b.right, b.bottom,
        b.Width() + 1.0f, b.Height() + 1.0f
    );
#else
    wxUnusedVar(label);
    wxUnusedVar(bmp);
#endif
}

void HaikuDumpView(const char* label, BView* v)
{
    if (!v)
        return;

    BWindow* win = v->Window();
    if (!win)
        return;

    if (!win->LockLooper())
        return;

    BRect frame = v->Frame();
    BRect bounds = v->Bounds();

    wxLogDebug("%s: ptr=%p frame=(%.1f,%.1f,%.1f,%.1f) bounds=(%.1f,%.1f,%.1f,%.1f)",
               label, v,
               frame.left, frame.top, frame.right, frame.bottom,
               bounds.left, bounds.top, bounds.right, bounds.bottom);

    win->UnlockLooper();
}

// ----------------------------------------------------------------------------
// Copy pixels between compatible 32-bit Haiku bitmaps.
// ----------------------------------------------------------------------------

bool wxHaikuCopyBitmapPixels(BBitmap* dst, const BBitmap* src)
{
    if (!dst || !src || !dst->IsValid() || !src->IsValid())
        return false;

    const BRect srcBounds = src->Bounds();
    const BRect dstBounds = dst->Bounds();

    if (!srcBounds.IsValid() || !dstBounds.IsValid())
        return false;

    const int width = srcBounds.IntegerWidth() + 1;
    const int height = srcBounds.IntegerHeight() + 1;

    if (width <= 0 || height <= 0 ||
        width != dstBounds.IntegerWidth() + 1 ||
        height != dstBounds.IntegerHeight() + 1)
        return false;

    const color_space srcFormat = src->ColorSpace();
    const color_space dstFormat = dst->ColorSpace();

    // Haiku's current generic gray1 writer and reader disagree on polarity.
    // Use the native format's documented MSB-first, set-bit-is-black layout.
    if (srcFormat==B_GRAY1 && (dstFormat==B_RGB32 || dstFormat==B_RGBA32))
    {
        const auto* input=static_cast<const uint8*>(src->Bits());
        auto* output=static_cast<uint8*>(dst->Bits());
        for(int y=0;y<height;++y)for(int x=0;x<width;++x)
        {
            const uint8 value=(input[y*src->BytesPerRow()+x/8] & (0x80>>(x%8)))?0:255;
            uint8* pixel=output+y*dst->BytesPerRow()+x*4;
            pixel[0]=pixel[1]=pixel[2]=value;pixel[3]=255;
        }
        return true;
    }
    if (dstFormat==B_GRAY1 && (srcFormat==B_RGB32 || srcFormat==B_RGBA32))
    {
        const auto* input=static_cast<const uint8*>(src->Bits());
        auto* output=static_cast<uint8*>(dst->Bits());
        std::memset(output,0,dst->BitsLength());
        for(int y=0;y<height;++y)for(int x=0;x<width;++x)
        {
            const uint8* pixel=input+y*src->BytesPerRow()+x*4;
            if(pixel[2]*77+pixel[1]*150+pixel[0]*29 < 128*256)
                output[y*dst->BytesPerRow()+x/8] |= 0x80>>(x%8);
        }
        return true;
    }
    if (srcFormat != dstFormat || srcFormat == B_GRAY1)
        return dst->ImportBits(src) == B_OK;

    if ((srcFormat != B_RGB32 && srcFormat != B_RGBA32) ||
        (dstFormat != B_RGB32 && dstFormat != B_RGBA32))
        return false;

    const size_t rowBytes = static_cast<size_t>(width) * 4;

    const int32 srcBytesPerRow = src->BytesPerRow();
    const int32 dstBytesPerRow = dst->BytesPerRow();

    if (srcBytesPerRow <= 0 || dstBytesPerRow <= 0)
        return false;

    const size_t srcStride = static_cast<size_t>(srcBytesPerRow);
    const size_t dstStride = static_cast<size_t>(dstBytesPerRow);

    if (srcStride < rowBytes || dstStride < rowBytes)
        return false;

    if (!src->Bits() || !dst->Bits())
        return false;

    // Check the exact accessible range, including row stride.
    const size_t rowsBeforeLast = static_cast<size_t>(height - 1);
    const size_t srcLength = static_cast<size_t>(src->BitsLength());
    const size_t dstLength = static_cast<size_t>(dst->BitsLength());

    if (srcLength < rowBytes || dstLength < rowBytes)
        return false;

    if (rowsBeforeLast > (srcLength - rowBytes) / srcStride ||
        rowsBeforeLast > (dstLength - rowBytes) / dstStride)
        return false;

    const auto* srcBits =
        static_cast<const uint8*>(src->Bits());

    auto* dstBits =
        static_cast<uint8*>(dst->Bits());

    for (int y = 0; y < height; ++y)
    {
        std::memcpy(
            dstBits + static_cast<size_t>(y) * dstStride,
            srcBits + static_cast<size_t>(y) * srcStride,
            rowBytes
        );
    }

    return true;
}

namespace {
struct ActiveMemoryBitmap { BView* view; BBitmap* drawing; };
std::map<BBitmap*, ActiveMemoryBitmap>& ActiveMemoryBitmaps()
{
    static auto* values = new std::map<BBitmap*, ActiveMemoryBitmap>;
    return *values;
}
std::recursive_mutex& ActiveMemoryMutex()
{
    static auto* mutex = new std::recursive_mutex;
    return *mutex;
}
}
void wxHaikuSyncMemoryBitmap(BBitmap* bitmap)
{
    std::lock_guard<std::recursive_mutex> lock(ActiveMemoryMutex());
    const auto it = ActiveMemoryBitmaps().find(bitmap);
    if (it == ActiveMemoryBitmaps().end()) return;
    BView* view = it->second.view;
    if (view->LockLooper())
    {
        view->Sync();
        wxHaikuCopyBitmapPixels(bitmap, it->second.drawing);
        view->UnlockLooper();
    }
}

// ----------------------------------------------------------------------------
// wxMemoryDCImpl
// ----------------------------------------------------------------------------

wxIMPLEMENT_CLASS(wxMemoryDCImpl, wxHaikuDCImpl);

wxMemoryDCImpl::wxMemoryDCImpl(wxMemoryDC* owner)
    : wxHaikuDCImpl(owner)
{
    m_view = nullptr;
    m_bitmap = nullptr;
    m_selected = wxNullBitmap;
    m_ok = false;
}

wxMemoryDCImpl::wxMemoryDCImpl(wxMemoryDC* owner, wxBitmap& bitmap)
    : wxHaikuDCImpl(owner)
{
    m_view = nullptr;
    m_bitmap = nullptr;
    m_selected = wxNullBitmap;
    m_ok = false;
    DoSelect(bitmap);
}

wxMemoryDCImpl::wxMemoryDCImpl(wxMemoryDC* owner, wxDC* WXUNUSED(dc))
    : wxHaikuDCImpl(owner)
{
    m_view = nullptr;
    m_bitmap = nullptr;
    m_selected = wxNullBitmap;
    m_ok = false;
}

wxMemoryDCImpl::~wxMemoryDCImpl()
{
    // Remove clipping state before releasing the drawing view.
    DestroyClippingRegion();

    // Synchronize pixels and release temporary drawing resources.
    ReleaseDrawingBitmap();

    // Release our reference to the selected bitmap.
    m_selected = wxNullBitmap;
}

#if wxUSE_GRAPHICS_CONTEXT
void wxHaikuDetachGraphicsView(BView* view);
#endif

void wxMemoryDCImpl::ReleaseDrawingBitmap()
{
    BView* view = m_view;
    BBitmap* drawingBitmap = m_drawingBitmap;
#if wxUSE_GRAPHICS_CONTEXT
    if (view) wxHaikuDetachGraphicsView(view);
#endif

    if (drawingBitmap)
    {
        // Complete pending drawing before copying pixels back.
        bool synchronized = (view == nullptr);

        if (view && view->LockLooper())
        {
            view->Sync();
            view->UnlockLooper();
            synchronized = true;
        }

        // The selected wxBitmap shares its native storage with
        // the bitmap originally passed to SelectObject().
        BBitmap* selectedBitmap =
            m_selected.IsOk() ? m_selected.GetBBitmap() : nullptr;

        if (selectedBitmap)
        {
            std::lock_guard<std::recursive_mutex> lock(ActiveMemoryMutex());
            ActiveMemoryBitmaps().erase(selectedBitmap);
        }

        if (synchronized && selectedBitmap)
        {
            if (!wxHaikuCopyBitmapPixels(selectedBitmap, drawingBitmap))
            {
                wxLogDebug("Haiku wxMemoryDC: bitmap copy-back failed");
            }
        }
        else if (!synchronized)
        {
            wxLogDebug("Haiku wxMemoryDC: unable to synchronize drawing");
        }

        if (view)
        {
            // Lock the bitmap before modifying its child hierarchy.
            if (!drawingBitmap->Lock())
            {
                wxLogDebug(
                    "Haiku wxMemoryDC: cannot lock bitmap for cleanup"
                );

                // Do not destroy a bitmap with an attached view.
                // This exceptional path intentionally leaves the native
                // resources allocated rather than risking invalid access.
                m_view = nullptr;
                m_bitmap = nullptr;
                m_drawingBitmap = nullptr;
                m_ok = false;
                return;
            }

            const bool removed = drawingBitmap->RemoveChild(view);
            drawingBitmap->Unlock();

            if (!removed)
            {
                wxLogDebug(
                    "Haiku wxMemoryDC: failed to detach drawing view"
                );

                // Preserve the native objects rather than risk
                // deleting a view still attached to its bitmap.
                m_view = nullptr;
                m_bitmap = nullptr;
                m_drawingBitmap = nullptr;
                m_ok = false;
                return;
            }

            delete view;
        }

        delete drawingBitmap;
    }

    m_view = nullptr;
    m_bitmap = nullptr;
    m_drawingBitmap = nullptr;
    m_ok = false;
}

void wxMemoryDCImpl::DoSelect(const wxBitmap& bitmap)
{
    // Preserve the selected bitmap before releasing the previous one.
    // This also handles selecting the same bitmap again.
    wxBitmap newSelection = bitmap;

    DestroyClippingRegion();
    ReleaseDrawingBitmap();

    m_selected = newSelection;
    m_ok = false;

    if (!m_selected.IsOk())
        return;

    BBitmap* original = m_selected.GetBBitmap();

    if (!original || !original->IsValid())
        return;

    const BRect bounds = original->Bounds();

    if (!bounds.IsValid())
        return;

    // Only active memory DCs need view-capable bitmaps.
    BBitmap* drawing = new BBitmap(bounds, B_RGBA32, true);

    if (!drawing || drawing->InitCheck() != B_OK)
    {
        delete drawing;
        wxLogDebug("Haiku wxMemoryDC: drawing bitmap allocation failed");
        return;
    }

    if (!wxHaikuCopyBitmapPixels(drawing, original))
    {
        delete drawing;
        wxLogDebug("Haiku wxMemoryDC: initial pixel copy failed");
        return;
    }

    BView* view = new BView(
        bounds,
        "memdcview",
        B_FOLLOW_NONE,
        B_WILL_DRAW
    );

    if (!view)
    {
        delete drawing;
        return;
    }

    if (!drawing->Lock())
    {
        delete view;
        delete drawing;
        wxLogDebug("Haiku wxMemoryDC: unable to lock drawing bitmap");
        return;
    }

    drawing->AddChild(view);

    // AddChild() returns void. Verify that the view was attached.
    const bool attached =
        drawing->CountChildren() == 1 &&
        drawing->ChildAt(0) == view;

    drawing->Unlock();

    if (!attached)
    {
        wxLogDebug("Haiku wxMemoryDC: drawing view attachment failed");

        // Do not delete a view that may still belong to the bitmap.
        // The bitmap destructor handles any attached children.
        if (view->Parent() == nullptr)
            delete view;

        delete drawing;
        return;
    }

    // The drawing bitmap must remain alive while the view is attached.
    m_drawingBitmap = drawing;
    m_bitmap = drawing;
    m_view = view;
    m_ok = true;
    {
        std::lock_guard<std::recursive_mutex> lock(ActiveMemoryMutex());
        ActiveMemoryBitmaps()[original] = {view, drawing};
    }

    SetPen(m_pen);
    SetBrush(m_brush);
    SetFont(m_font);
    SetLogicalFunction(m_logicalFunction);
    ComputeScaleAndOrigin();
}

void wxMemoryDCImpl::DoGetSize(int* width, int* height) const
{
 

    if (m_selected.IsOk())
    {
        if (width)  *width  = m_selected.GetWidth();
        if (height) *height = m_selected.GetHeight();
    }
    else
    {
        if (width)  *width  = 0;
        if (height) *height = 0;
    }

   
}

wxBitmap wxMemoryDCImpl::DoGetAsBitmap(const wxRect* subrect) const
{
    if (!m_selected.IsOk())
        return wxNullBitmap;

    // Drawing occurs in the temporary view-capable bitmap.
    // Copy the completed pixels into the selected wxBitmap before export.
    if (m_drawingBitmap && m_view && m_ok)
    {
        if (m_view->LockLooper())
        {
            m_view->Sync();
            m_view->UnlockLooper();

            BBitmap* original = m_selected.GetBBitmap();

            if (original &&
                !wxHaikuCopyBitmapPixels(original, m_drawingBitmap))
            {
                wxLogDebug(
                    "Haiku wxMemoryDC: bitmap export copy failed"
                );
            }
        }
        else
        {
            wxLogDebug(
                "Haiku wxMemoryDC: unable to synchronize bitmap export"
            );
        }
    }

    if (!subrect)
        return m_selected;

    return m_selected.GetSubBitmap(*subrect);
}

const wxBitmap& wxMemoryDCImpl::GetSelectedBitmap() const
{

    return m_selected;
}

wxBitmap& wxMemoryDCImpl::GetSelectedBitmap()
{
 
    return m_selected;
}

void wxMemoryDCImpl::SetLayoutDirection(wxLayoutDirection dir)
{
  
    wxDCImpl::SetLayoutDirection(dir);
  
}
