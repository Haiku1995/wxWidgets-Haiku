/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/bitmap.cpp
// Purpose:     wxBitmap implementation for Haiku
// Author:      wxWidgets team
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#ifndef WX_PRECOMP
    #include "wx/bitmap.h"
    #include "wx/image.h"
    #include "wx/log.h"
    #include "wx/rawbmp.h"
    #include "wx/colour.h"
    #include "wx/palette.h"
    #include "wx/mstream.h"
    #include "wx/filefn.h"
#endif

#include "wx/haiku/private.h"
#include "wx/rawbmp.h"

#include <set>
#include <map>
#include <mutex>
#include <Bitmap.h>
#include "wx/cursor.h"
#include "wx/imagbmp.h"
#include <memory>

namespace {
// Static plugin bitmaps can outlive wxApp. The bookkeeping must remain
// available until those late bitmap destructors have run.
std::set<wxBitmapRefData*>& LiveBitmapData()
{
    static auto* data = new std::set<wxBitmapRefData*>;
    return *data;
}
std::map<const wxBitmapRefData*, std::unique_ptr<BBitmap>>& RawBitmapData()
{
    static auto* data = new std::map<const wxBitmapRefData*, std::unique_ptr<BBitmap>>;
    return *data;
}
std::recursive_mutex& BitmapDataMutex()
{
    static auto* mutex = new std::recursive_mutex;
    return *mutex;
}
}
#include <TranslationUtils.h>
#include <File.h>
#include <TranslatorRoster.h>

/////////////////////////////////////////////////////////////////////////////
// wxBitmapRefData
/////////////////////////////////////////////////////////////////////////////

wxBitmapRefData::wxBitmapRefData(int width, int height, int depth)
    : m_bitmap(nullptr)
    , m_mask(nullptr)
#if wxUSE_PALETTE
    , m_palette(nullptr)
#endif
{
    std::lock_guard<std::recursive_mutex> lock(BitmapDataMutex());
    LiveBitmapData().insert(this);
    wxUnusedVar(width);
    wxUnusedVar(height);
    wxUnusedVar(depth);
}

wxBitmapRefData::wxBitmapRefData(const wxBitmapRefData& data)
    : wxGDIRefData()
    , m_bitmap(nullptr)
    , m_mask(nullptr)
#if wxUSE_PALETTE
    , m_palette(nullptr)
#endif
{
    std::lock_guard<std::recursive_mutex> lock(BitmapDataMutex());
    LiveBitmapData().insert(this);
    if (data.m_bitmap && data.m_bitmap->InitCheck() == B_OK)
        m_bitmap = new BBitmap(*data.m_bitmap);

    if (data.m_mask)
        m_mask = new wxMask(*data.m_mask);

#if wxUSE_PALETTE
    if (data.m_palette)
        m_palette = new wxPalette(*data.m_palette);
#endif
}

wxBitmapRefData::~wxBitmapRefData()
{
    std::lock_guard<std::recursive_mutex> lock(BitmapDataMutex());
    LiveBitmapData().erase(this);
    RawBitmapData().erase(this);
    delete m_bitmap;
    delete m_mask;
#if wxUSE_PALETTE
    delete m_palette;
#endif
}

/////////////////////////////////////////////////////////////////////////////
// wxBitmap
/////////////////////////////////////////////////////////////////////////////

// Called before BApplication tears down its app_server connection.
void wxHaikuReleaseNativeBitmaps()
{
    std::lock_guard<std::recursive_mutex> lock(BitmapDataMutex());
    for (wxBitmapRefData* data : LiveBitmapData())
    {
        wxDELETE(data->m_bitmap);
        wxDELETE(data->m_mask);
    }
}
wxIMPLEMENT_DYNAMIC_CLASS(wxBitmap, wxObject);

// Memory DCs draw into a view-capable staging bitmap. Native consumers
// must see completed pixels even before the wxBitmap is deselected.
void wxHaikuSyncMemoryBitmap(BBitmap* bitmap);
bool wxHaikuCopyBitmapPixels(BBitmap* destination, const BBitmap* source);
BBitmap* wxBitmap::GetBBitmap() const
{
    auto* data = static_cast<wxBitmapRefData*>(GetRefData());
    BBitmap* bitmap = data ? data->m_bitmap : nullptr;
    if (bitmap) wxHaikuSyncMemoryBitmap(bitmap);
    return bitmap;
}

wxBitmap::wxBitmap(const wxCursor& cursor)
{
    *this = cursor.GetBitmap();
}

wxBitmap::wxBitmap(int width, int height, int depth)
{
    Create(width, height, depth);
}

wxBitmap::wxBitmap(const char bits[], int width, int height, int depth)
{
    if ( !bits || width <= 0 || height <= 0 )
        return;

    if ( depth != 1 )
    {
        if ( Create(width, height, depth) )
        {
            wxBitmapRefData* ref = static_cast<wxBitmapRefData*>(GetRefData());
            if ( ref && ref->m_bitmap )
            {
                const size_t dstLen = ref->m_bitmap->BitsLength();
                memcpy(ref->m_bitmap->Bits(),
                       bits,
                       wxMin(dstLen, (size_t)(height * ref->m_bitmap->BytesPerRow())));
            }
        }
        return;
    }

    if ( !Create(width, height, 1) )
        return;

    wxBitmapRefData* ref = static_cast<wxBitmapRefData*>(GetRefData());
    if ( !ref || !ref->m_bitmap )
        return;

    BBitmap colour(BRect(0,0,width-1,height-1),B_RGB32);
    if (colour.InitCheck()!=B_OK) return;
    uint8* dst = static_cast<uint8*>(colour.Bits());
    const int dstBpr = colour.BytesPerRow();
    const int srcBpr = (width + 7) / 8;

    for ( int y = 0; y < height; ++y )
    {
        for ( int x = 0; x < width; ++x )
        {
            const unsigned char byte = (unsigned char)bits[y * srcBpr + x / 8];
            const bool bitSet = (byte & (1 << (x % 8))) != 0;

            const uint8 c = bitSet ? 0 : 255;
            const int off = y * dstBpr + x * 4;

            // Haiku B_RGBA32 byte layout is BGRA in memory.
            dst[off + 0] = c;
            dst[off + 1] = c;
            dst[off + 2] = c;
            dst[off + 3] = 255;
        }
    }
    wxHaikuCopyBitmapPixels(ref->m_bitmap,&colour);
}



wxBitmap::wxBitmap(const wxString& filename, wxBitmapType type)
{
    LoadFile(filename, type);
}

wxBitmap::wxBitmap(const wxImage& image, int depth, double scale)
{
    wxUnusedVar(depth);
    wxUnusedVar(scale);

    const int w = image.GetWidth();
    const int h = image.GetHeight();

    if (!image.IsOk()) return;
    if (depth == 1)
    {
        wxBitmap colour(image, 24, scale);
        if (Create(w,h,1)) wxHaikuCopyBitmapPixels(GetBBitmap(),colour.GetBBitmap());
        if (colour.GetMask()) SetMask(new wxMask(*colour.GetMask()));
        return;
    }
    if (!Create(w,h,image.HasAlpha() ? 32 : 24)) return;

    wxBitmapRefData* ref = static_cast<wxBitmapRefData*>(GetRefData());
    if ( !ref || !ref->m_bitmap )
        return;

    BBitmap* bmp = ref->m_bitmap;

    uint8* dest = static_cast<uint8*>(bmp->Bits());
    const int bpr = bmp->BytesPerRow();

    const uint8* src = image.GetData();
    const uint8* alpha = image.GetAlpha();

    for ( int y = 0; y < h; ++y )
    {
        for ( int x = 0; x < w; ++x )
        {
            const int dst = y * bpr + x * 4;
            const int s = (y * w + x) * 3;

            dest[dst + 0] = src[s + 2];
            dest[dst + 1] = src[s + 1];
            dest[dst + 2] = src[s + 0];
            dest[dst + 3] = alpha ? alpha[y * w + x] : 255;
        }
    }
    if (image.HasMask())
        SetMask(new wxMask(*this,wxColour(image.GetMaskRed(),image.GetMaskGreen(),image.GetMaskBlue())));
}

wxBitmap::wxBitmap(const wxImage& image, const wxDC& dc)
{
    wxUnusedVar(dc);
    *this = wxBitmap(image, wxBITMAP_SCREEN_DEPTH);
}

void wxBitmap::InitStandardHandlers()
{
    // Do nothing — handled by wxWidgets core
}

bool wxBitmap::Create(int width, int height, int depth)
{
    wxUnusedVar(depth);

    if ( width <= 0 || height <= 0 )
        return false;

    UnRef();

    BRect rect(0, 0, width - 1, height - 1);
    const color_space format = depth == 1 ? B_GRAY1 : depth == 32 ? B_RGBA32 : B_RGB32;
    BBitmap* bitmap = new BBitmap(rect, format, false);

    if ( !bitmap || bitmap->InitCheck() != B_OK )
    {
        delete bitmap;
        return false;
    }

    memset(bitmap->Bits(), 0, bitmap->BitsLength());
    if (format == B_RGB32)
        for (int row=0; row<height; ++row)
            for (int col=0; col<width; ++col)
                static_cast<uint8*>(bitmap->Bits())[row*bitmap->BytesPerRow()+col*4+3]=255;

    wxBitmapRefData* ref = new wxBitmapRefData(width, height, depth);
    ref->m_bitmap = bitmap;
    SetRefData(ref);

    return true;
}

bool wxBitmap::Create(int width, int height, const wxDC& dc)
{
    wxUnusedVar(dc);
    return Create(width, height, wxBITMAP_SCREEN_DEPTH);
}

int wxBitmap::GetWidth() const
{
    wxBitmapRefData* ref = static_cast<wxBitmapRefData*>(GetRefData());
    return ref && ref->m_bitmap
            ? (int)ref->m_bitmap->Bounds().IntegerWidth() + 1
            : 0;
}

int wxBitmap::GetHeight() const
{
    wxBitmapRefData* ref = static_cast<wxBitmapRefData*>(GetRefData());
    return ref && ref->m_bitmap
            ? (int)ref->m_bitmap->Bounds().IntegerHeight() + 1
            : 0;
}

int wxBitmap::GetDepth() const
{
    wxBitmapRefData* ref = static_cast<wxBitmapRefData*>(GetRefData());
    if ( !ref || !ref->m_bitmap )
        return 1;

    switch ( ref->m_bitmap->ColorSpace() )
    {
        case B_GRAY1:
            return 1;

        case B_CMAP8:
        case B_GRAY8:
            return 8;

        case B_RGB16:
        case B_RGB15:
        case B_RGBA15:
        case B_RGB16_BIG:
            return 16;

        case B_RGB32:
        case B_RGBA32:
        case B_RGB32_BIG:
        case B_RGBA32_BIG:
        default:
            return 32;
    }
}

bool wxBitmap::HasAlpha() const
{
    auto* data=static_cast<wxBitmapRefData*>(GetRefData());
    if (!data || !data->m_bitmap) return false;
    const color_space format=data->m_bitmap->ColorSpace();
    return format==B_RGBA32 || format==B_RGBA32_BIG;
}
bool wxBitmap::UseAlpha(bool use)
{
    if (!IsOk() || GetDepth()!=32) return false;
    if (HasAlpha()==use) return true;
    GetBBitmap();
    AllocExclusive();
    auto* data=static_cast<wxBitmapRefData*>(GetRefData());
    auto* converted=new BBitmap(data->m_bitmap->Bounds(),use?B_RGBA32:B_RGB32);
    if (converted->InitCheck()!=B_OK || converted->ImportBits(data->m_bitmap)!=B_OK)
    { delete converted; return false; }
    delete data->m_bitmap;data->m_bitmap=converted;
    return true;
}

wxMask* wxBitmap::GetMask() const
{
    wxBitmapRefData* ref = static_cast<wxBitmapRefData*>(GetRefData());
    return ref ? ref->m_mask : nullptr;
}

void wxBitmap::SetMask(wxMask* mask)
{
    AllocExclusive();

    wxBitmapRefData* ref = static_cast<wxBitmapRefData*>(GetRefData());
    if ( ref )
    {
        delete ref->m_mask;
        ref->m_mask = mask;
    }
}

bool wxBitmap::LoadFile(const wxString& name, wxBitmapType type)
{
    wxUnusedVar(type);

    UnRef();

    // Preserve the on-disk depth even when a translator expands pixels.
    BFile input(name.utf8_str(), B_READ_ONLY);
    uint8 header[30];
    const bool monochrome = input.Read(header, sizeof header) == sizeof header &&
        header[0] == 'B' && header[1] == 'M' && header[28] == 1 && header[29] == 0;

    BBitmap* bb = BTranslationUtils::GetBitmapFile(name.utf8_str());
    if ( !bb || bb->InitCheck() != B_OK )
    {
        delete bb;
#if wxUSE_IMAGE
        // Native translators do not support every wxImage format (including
        // RLE-compressed BMP). Honour the requested handler as a fallback.
        wxImage image;
        if (!image.LoadFile(name, type))
            return false;
        *this = wxBitmap(image, monochrome ? 1 : wxBITMAP_SCREEN_DEPTH);
        return IsOk();
#else
        return false;
#endif
    }

    if (monochrome)
    {
        auto* mono=new BBitmap(bb->Bounds(),B_GRAY1);
        if(mono->InitCheck()==B_OK && wxHaikuCopyBitmapPixels(mono,bb)){delete bb;bb=mono;}
        else delete mono;
    }
    wxBitmapRefData* ref = new wxBitmapRefData();
    ref->m_bitmap = bb;
    SetRefData(ref);

    return true;
}

bool wxBitmap::SaveFile(const wxString& name,
                        wxBitmapType type,
                        const wxPalette* palette) const
{
    wxUnusedVar(palette);

#if wxUSE_IMAGE
    if ( !IsOk() )
        return false;

    wxImage img = ConvertToImage();
    if ( !img.IsOk() )
        return false;

    if (GetDepth()==1 && type==wxBITMAP_TYPE_BMP)
        img.SetOption(wxIMAGE_OPTION_BMP_FORMAT,wxBMP_1BPP);
    return img.SaveFile(name, type);
#else
    wxUnusedVar(name);
    wxUnusedVar(type);
    return false;
#endif
}

#if wxUSE_IMAGE
wxImage wxBitmap::ConvertToImage() const
{
    GetBBitmap(); // Synchronize any active memory DC before reading pixels.

    wxBitmapRefData* ref = static_cast<wxBitmapRefData*>(GetRefData());
    if ( !ref || !ref->m_bitmap )
        return wxImage();

    const int w = GetWidth(), h = GetHeight();
    wxImage img(w,h);
    if (HasAlpha()) img.InitAlpha();
    BBitmap* native=ref->m_bitmap;
    std::unique_ptr<BBitmap> converted;
    if (native->ColorSpace()!=B_RGB32 && native->ColorSpace()!=B_RGBA32)
    {
        converted.reset(new BBitmap(native->Bounds(),HasAlpha()?B_RGBA32:B_RGB32));
        if (converted->InitCheck()!=B_OK || !wxHaikuCopyBitmapPixels(converted.get(),native)) return wxImage();
        native=converted.get();
    }
    const uint8* src=static_cast<const uint8*>(native->Bits());
    const int bpr=native->BytesPerRow();
    uint8* data=img.GetData();uint8* alpha=img.GetAlpha();
    for (int y=0;y<h;++y) for (int x=0;x<w;++x)
    {
        const int off=y*bpr+x*4, pixel=y*w+x;
        data[pixel*3]=src[off+2];data[pixel*3+1]=src[off+1];data[pixel*3+2]=src[off];
        if(alpha)alpha[pixel]=src[off+3];
    }
    if (ref->m_mask)
    {
        const wxImage mask=ref->m_mask->GetBitmap().ConvertToImage();
        unsigned char red=1,green=2,blue=3;
        if (mask.IsOk() && img.FindFirstUnusedColour(&red,&green,&blue))
        {
            for(int y=0;y<h;++y)for(int x=0;x<w;++x)
                if(mask.GetRed(x,y)==0) img.SetRGB(x,y,red,green,blue);
            img.SetMaskColour(red,green,blue);
        }
    }

#if wxUSE_PALETTE
    if ( ref->m_palette )
        img.SetPalette(*ref->m_palette);
#endif

    return img;
}
#endif // wxUSE_IMAGE

void* wxBitmap::GetRawData(wxPixelDataBase& data, int bpp)
{
    GetBBitmap();
    if ((bpp != 32 && bpp != 1) || !IsOk())
        return nullptr;

    // Writable raw access must not modify another wxBitmap sharing the data.
    AllocExclusive();
    wxBitmapRefData* ref = static_cast<wxBitmapRefData*>(GetRefData());
    if (ref && ref->m_bitmap && bpp == 32 && ref->m_bitmap->ColorSpace() == B_GRAY1)
    {
        std::lock_guard<std::recursive_mutex> lock(BitmapDataMutex());
        auto expanded = std::make_unique<BBitmap>(ref->m_bitmap->Bounds(), B_RGB32);
        if (expanded->InitCheck() != B_OK || !wxHaikuCopyBitmapPixels(expanded.get(), ref->m_bitmap))
            return nullptr;
        data.m_ptOrigin = wxPoint(0, 0);
        data.m_width = GetWidth(); data.m_height = GetHeight();
        data.m_stride = expanded->BytesPerRow();
        void* bits = expanded->Bits();
        RawBitmapData()[ref] = std::move(expanded);
        return bits;
    }
    if (!ref || !ref->m_bitmap ||
        (ref->m_bitmap->ColorSpace() != B_RGB32 &&
         ref->m_bitmap->ColorSpace() != B_RGBA32 &&
         ref->m_bitmap->ColorSpace() != B_GRAY1) ||
        (bpp==1 && ref->m_bitmap->ColorSpace()!=B_GRAY1) ||
        (bpp==32 && ref->m_bitmap->ColorSpace()==B_GRAY1))
        return nullptr;

    data.m_ptOrigin = wxPoint(0, 0);
    data.m_width = GetWidth();
    data.m_height = GetHeight();
    data.m_stride = ref->m_bitmap->BytesPerRow();
    return ref->m_bitmap->Bits();
}

void wxBitmap::UngetRawData(wxPixelDataBase& data)
{
    wxUnusedVar(data);
    std::lock_guard<std::recursive_mutex> lock(BitmapDataMutex());
    auto* ref = static_cast<wxBitmapRefData*>(GetRefData());
    auto it = RawBitmapData().find(ref);
    if (it != RawBitmapData().end())
    {
        wxHaikuCopyBitmapPixels(ref->m_bitmap, it->second.get());
        RawBitmapData().erase(it);
    }
}

wxGDIRefData* wxBitmap::CreateGDIRefData() const
{
    return new wxBitmapRefData;
}

wxGDIRefData* wxBitmap::CloneGDIRefData(const wxGDIRefData* data) const
{
    return new wxBitmapRefData(*static_cast<const wxBitmapRefData*>(data));
}

wxBitmap wxBitmap::GetSubBitmap(const wxRect& rect) const
{
    wxCHECK_MSG(IsOk(), wxBitmap(), "invalid bitmap");
    wxCHECK_MSG(rect.width>0 && rect.height>0 && rect.x>=0 && rect.y>=0 &&
                rect.x<=GetWidth() && rect.y<=GetHeight() &&
                rect.width<=GetWidth()-rect.x && rect.height<=GetHeight()-rect.y,
                wxBitmap(), "sub-bitmap rectangle out of bounds");
    // Crop colour and mask independently. ConvertToImage encodes transparency
    // using a replacement colour, which must not overwrite hidden RGB pixels.
    GetBBitmap();
    wxBitmap pixels = *this;
    pixels.SetMask(nullptr);
    const wxImage image = pixels.ConvertToImage().GetSubImage(rect);
    if (!image.IsOk()) return wxBitmap();
    wxBitmap result(image, GetDepth() == 1 ? 1 : wxBITMAP_SCREEN_DEPTH);
    if (GetMask())
    {
        wxBitmap mask = GetMask()->GetBitmap().GetSubBitmap(rect);
        result.SetMask(new wxMask(mask));
    }
    return result;
}


#if wxUSE_PALETTE
wxPalette* wxBitmap::GetPalette() const
{
    wxBitmapRefData* ref = static_cast<wxBitmapRefData*>(GetRefData());
    return ref ? ref->m_palette : nullptr;
}

void wxBitmap::SetPalette(const wxPalette& palette)
{
    AllocExclusive();

    wxBitmapRefData* ref = static_cast<wxBitmapRefData*>(GetRefData());
    if ( !ref )
        return;

    delete ref->m_palette;
    ref->m_palette = new wxPalette(palette);
}
#endif // wxUSE_PALETTE

/////////////////////////////////////////////////////////////////////////////
// wxMask
/////////////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(wxMask, wxObject);

wxMask::wxMask()
    : m_bitmap(nullptr)
{
}

wxMask::wxMask(const wxMask& mask)
    : m_bitmap(nullptr)
{
    if ( mask.m_bitmap && mask.m_bitmap->InitCheck() == B_OK )
        m_bitmap = new BBitmap(*mask.m_bitmap);
}

wxMask::wxMask(const wxBitmap& bitmap, const wxColour& colour)
    : m_bitmap(nullptr)
{
    InitFromColour(bitmap, colour);
}

#if wxUSE_PALETTE
wxMask::wxMask(const wxBitmap& bitmap, int paletteIndex)
    : m_bitmap(nullptr)
{
    wxImage img = bitmap.ConvertToImage();
    if ( !img.IsOk() )
        return;

    if ( img.HasPalette() )
    {
        const wxPalette& pal = img.GetPalette();
        if ( paletteIndex >= 0 && paletteIndex < pal.GetColoursCount() )
        {
            unsigned char r, g, b;
            pal.GetRGB(paletteIndex, &r, &g, &b);
            InitFromColour(bitmap, wxColour(r, g, b));
        }
    }
}
#endif

wxMask::wxMask(const wxBitmap& bitmap)
    : m_bitmap(nullptr)
{
    InitFromMonoBitmap(bitmap);
}

wxMask::wxMask(BBitmap* haikuBitmap)
    : m_bitmap(haikuBitmap)
{
}

wxMask::~wxMask()
{
    delete m_bitmap;
}

wxBitmap wxMask::GetBitmap() const
{
    if (!m_bitmap || m_bitmap->InitCheck() != B_OK) return wxBitmap();
    wxBitmap result(m_bitmap->Bounds().IntegerWidth()+1,
                    m_bitmap->Bounds().IntegerHeight()+1, 1);
    // B_GRAY8 stores 0 for transparent and 255 for opaque.
    const uint8* src = static_cast<const uint8*>(m_bitmap->Bits());
    BBitmap* native = result.GetBBitmap();
    uint8* dst = static_cast<uint8*>(native->Bits());
    std::memset(dst, 0, native->BitsLength());
    for (int y=0; y<result.GetHeight(); ++y)
        for (int x=0; x<result.GetWidth(); ++x)
            if (src[y*m_bitmap->BytesPerRow()+x] < 128)
                dst[y*native->BytesPerRow()+x/8] |= 0x80 >> (x%8);
    return result;
}

bool wxMask::InitFromColour(const wxBitmap& bitmap, const wxColour& colour)
{
    delete m_bitmap;
    m_bitmap = nullptr;

    wxImage img = bitmap.ConvertToImage();
    if ( !img.IsOk() )
        return false;

    const int w = img.GetWidth();
    const int h = img.GetHeight();

    m_bitmap = new BBitmap(BRect(0, 0, w - 1, h - 1), B_GRAY8);
    if ( !m_bitmap || m_bitmap->InitCheck() != B_OK )
    {
        delete m_bitmap;
        m_bitmap = nullptr;
        return false;
    }

    uint8* bits = static_cast<uint8*>(m_bitmap->Bits());
    const int bpr = m_bitmap->BytesPerRow();
    const uint8* data = img.GetData();

    for ( int y = 0; y < h; ++y )
    {
        for ( int x = 0; x < w; ++x )
        {
            const int off = (y * w + x) * 3;
            const bool match =
                data[off + 0] == colour.Red() &&
                data[off + 1] == colour.Green() &&
                data[off + 2] == colour.Blue();

            bits[y * bpr + x] = match ? 0 : 255;
        }
    }

    return true;
}

bool wxMask::InitFromMonoBitmap(const wxBitmap& bitmap)
{
    delete m_bitmap;
    m_bitmap = nullptr;

    wxImage img = bitmap.ConvertToImage();
    if ( !img.IsOk() )
        return false;

    const int w = img.GetWidth();
    const int h = img.GetHeight();

    m_bitmap = new BBitmap(BRect(0, 0, w - 1, h - 1), B_GRAY8);
    if ( !m_bitmap || m_bitmap->InitCheck() != B_OK )
    {
        delete m_bitmap;
        m_bitmap = nullptr;
        return false;
    }

    uint8* bits = static_cast<uint8*>(m_bitmap->Bits());
    const int bpr = m_bitmap->BytesPerRow();
    const uint8* data = img.GetData();

    for ( int y = 0; y < h; ++y )
    {
        for ( int x = 0; x < w; ++x )
        {
            const int off = (y * w + x) * 3;
            const uint8 gray = static_cast<uint8>(0.299 * data[off + 0] +
                                                  0.587 * data[off + 1] +
                                                  0.114 * data[off + 2]);
            bits[y * bpr + x] = gray;
        }
    }

    return true;
}

void wxMask::FreeData()
{
    delete m_bitmap;
    m_bitmap = nullptr;
}
