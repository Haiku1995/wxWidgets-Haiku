/////////////////////////////////////////////////////////////////////////////
// Name:        converter.cpp
// Purpose:     Utility functions to convert between Haiku OS and wxWidgets types
// Author:      Generated for Haiku/wxWidgets interoperability
// Licence:     MIT licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/haiku/converter.h"
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/colour.h>
#include <wx/log.h> // for wxLogDebug

// Haiku headers
#include <Bitmap.h>
#include <Rect.h>
#include <Point.h>
#include <Size.h>
#include <GraphicsDefs.h>
#include <cstring>

namespace HaikuWxConverter {

// ============================================================================
// Color Space Conversion
// ============================================================================

int GetWxDepthFromColorSpace(color_space cs) {
    switch (cs) {
        case B_GRAY1:       return 1;
        case B_GRAY8:
        case B_CMAP8:       return 8;
        case B_RGB15:
        case B_RGBA15:      return 15;
        case B_RGB16:       return 16;
        case B_RGB24:
        case B_RGB24_BIG:   return 24;
        case B_RGB32:
        case B_RGBA32:
        case B_RGB32_BIG:
        case B_RGBA32_BIG:  return 32;
        default:            return 24; // Default fallback
    }
}

color_space GetColorSpaceFromWxDepth(int depth, bool hasAlpha) {
    if (depth == 1)
        return B_GRAY1;
    else if (depth == 8)
        return B_GRAY8;
    else if (depth == 15)
        return hasAlpha ? B_RGBA15 : B_RGB15;
    else if (depth == 16)
        return B_RGB16;
    else if (depth == 24)
        return B_RGB24;
    else if (depth == 32)
        return hasAlpha ? B_RGBA32 : B_RGB32;
    else
        return B_RGB32; // Default fallback
}

// ============================================================================
// Rectangle Conversion
// ============================================================================

wxRect BRectToWxRect(const BRect& brect) {
    return wxRect(
        static_cast<int>(brect.left),
        static_cast<int>(brect.top),
        static_cast<int>(brect.Width() + 1),
        static_cast<int>(brect.Height() + 1)
    );
}

BRect WxRectToBRect(const wxRect& wxrect) {
    return BRect(
        static_cast<float>(wxrect.x),
        static_cast<float>(wxrect.y),
        static_cast<float>(wxrect.x + wxrect.width - 1),
        static_cast<float>(wxrect.y + wxrect.height - 1)
    );
}

// ============================================================================
// Point Conversion
// ============================================================================

wxPoint BPointToWxPoint(const BPoint& bpoint) {
    return wxPoint(
        static_cast<int>(bpoint.x),
        static_cast<int>(bpoint.y)
    );
}

BPoint WxPointToBPoint(const wxPoint& wxpoint) {
    return BPoint(
        static_cast<float>(wxpoint.x),
        static_cast<float>(wxpoint.y)
    );
}

// ============================================================================
// Size Conversion
// ============================================================================

wxSize BSizeToWxSize(const BSize& bsize) {
    return wxSize(
        static_cast<int>(bsize.Width()),
        static_cast<int>(bsize.Height())
    );
}

BSize WxSizeToBSize(const wxSize& wxsize) {
    return BSize(
        static_cast<float>(wxsize.GetWidth()),
        static_cast<float>(wxsize.GetHeight())
    );
}

// ============================================================================
// Color Conversion
// ============================================================================

wxColour RgbColorToWxColour(const rgb_color& color) {
    return wxColour(color.red, color.green, color.blue, color.alpha);
}

rgb_color WxColourToRgbColor(const wxColour& color) {
    rgb_color result;
    result.red = color.Red();
    result.green = color.Green();
    result.blue = color.Blue();
    result.alpha = color.Alpha();
    return result;
}

// ============================================================================
// Bitmap Conversion - BBitmap to wxBitmap
// ============================================================================

wxBitmap BBitmapToWxBitmap(const BBitmap& bbitmap) {
    if (!bbitmap.IsValid()) {
        wxLogDebug("BBitmapToWxBitmap: Invalid BBitmap");
        return wxBitmap();
    }

    BRect bounds = bbitmap.Bounds();
    int width = static_cast<int>(bounds.Width() + 1);
    int height = static_cast<int>(bounds.Height() + 1);
    color_space cs = bbitmap.ColorSpace();

    wxImage image(width, height, false);
    const uint8* bits = static_cast<const uint8*>(bbitmap.Bits());
    int32 bpr = bbitmap.BytesPerRow();

    bool hasAlpha = (cs == B_RGBA32 || cs == B_RGBA32_BIG ||
                     cs == B_RGBA15);

    if (hasAlpha) {
        image.InitAlpha();
    }

    unsigned char* imgData = image.GetData();
    unsigned char* alphaData = hasAlpha ? image.GetAlpha() : nullptr;

    for (int y = 0; y < height; y++) {
        const uint8* row = bits + (y * bpr);

        for (int x = 0; x < width; x++) {
            int imgIdx = (y * width + x) * 3;

            switch (cs) {
                case B_RGB32:
                case B_RGBA32: {
                    // Little-endian BGRA: [B, G, R, A]
                    int pixelIdx = x * 4;
                    imgData[imgIdx + 0] = row[pixelIdx + 2]; // R
                    imgData[imgIdx + 1] = row[pixelIdx + 1]; // G
                    imgData[imgIdx + 2] = row[pixelIdx + 0]; // B
                    if (alphaData) {
                        alphaData[y * width + x] = row[pixelIdx + 3]; // A
                    }
                    break;
                }
                case B_RGB32_BIG:
                case B_RGBA32_BIG: {
                    // Big-endian ARGB: [A, R, G, B]
                    int pixelIdx = x * 4;
                    if (alphaData) {
                        alphaData[y * width + x] = row[pixelIdx + 0]; // A
                    }
                    imgData[imgIdx + 0] = row[pixelIdx + 1]; // R
                    imgData[imgIdx + 1] = row[pixelIdx + 2]; // G
                    imgData[imgIdx + 2] = row[pixelIdx + 3]; // B
                    break;
                }
                case B_RGB24: {
                    // BGR
                    int pixelIdx = x * 3;
                    imgData[imgIdx + 0] = row[pixelIdx + 2]; // R
                    imgData[imgIdx + 1] = row[pixelIdx + 1]; // G
                    imgData[imgIdx + 2] = row[pixelIdx + 0]; // B
                    break;
                }
                case B_RGB24_BIG: {
                    // RGB
                    int pixelIdx = x * 3;
                    imgData[imgIdx + 0] = row[pixelIdx + 0]; // R
                    imgData[imgIdx + 1] = row[pixelIdx + 1]; // G
                    imgData[imgIdx + 2] = row[pixelIdx + 2]; // B
                    break;
                }
                case B_GRAY8: {
                    uint8 gray = row[x];
                    imgData[imgIdx + 0] = gray;
                    imgData[imgIdx + 1] = gray;
                    imgData[imgIdx + 2] = gray;
                    break;
                }
                case B_CMAP8: {
                    // TODO: Use BBitmap::ColorMap() for accurate palette conversion
                    // Fallback: treat index as grayscale intensity
                    uint8 idx = row[x];
                    imgData[imgIdx + 0] = idx;
                    imgData[imgIdx + 1] = idx;
                    imgData[imgIdx + 2] = idx;
                    break;
                }
                case B_RGB15: {
                    const uint16* pixelPtr = reinterpret_cast<const uint16*>(row + x * 2);
                    uint16 pixel = *pixelPtr;
                    unsigned char r = ((pixel >> 10) & 0x1F) * 255 / 31;
                    unsigned char g = ((pixel >> 5)  & 0x1F) * 255 / 31;
                    unsigned char b = ((pixel)       & 0x1F) * 255 / 31;
                    imgData[imgIdx + 0] = r;
                    imgData[imgIdx + 1] = g;
                    imgData[imgIdx + 2] = b;
                    break;
                }
                case B_RGBA15: {
                    const uint16* pixelPtr = reinterpret_cast<const uint16*>(row + x * 2);
                    uint16 pixel = *pixelPtr;
                    unsigned char a = ((pixel >> 15) & 0x01) ? 255 : 0;
                    unsigned char r = ((pixel >> 10) & 0x1F) * 255 / 31;
                    unsigned char g = ((pixel >> 5)  & 0x1F) * 255 / 31;
                    unsigned char b = ((pixel)       & 0x1F) * 255 / 31;
                    imgData[imgIdx + 0] = r;
                    imgData[imgIdx + 1] = g;
                    imgData[imgIdx + 2] = b;
                    if (alphaData) {
                        alphaData[y * width + x] = a;
                    }
                    break;
                }
                case B_RGB16: {
                    const uint16* pixelPtr = reinterpret_cast<const uint16*>(row + x * 2);
                    uint16 pixel = *pixelPtr;
                    unsigned char r = ((pixel >> 11) & 0x1F) * 255 / 31;
                    unsigned char g = ((pixel >> 5)  & 0x3F) * 255 / 63;
                    unsigned char b = ((pixel)       & 0x1F) * 255 / 31;
                    imgData[imgIdx + 0] = r;
                    imgData[imgIdx + 1] = g;
                    imgData[imgIdx + 2] = b;
                    break;
                }
                default: {
                    wxLogDebug("BBitmapToWxBitmap: Unsupported color space %d", static_cast<int>(cs));
                    // Magenta fallback for debugging
                    imgData[imgIdx + 0] = 255;
                    imgData[imgIdx + 1] = 0;
                    imgData[imgIdx + 2] = 255;
                    break;
                }
            }
        }
    }

    // wxBitmap(wxImage) automatically handles depth/alpha
    return wxBitmap(image);
}

// ============================================================================
// Bitmap Conversion - wxBitmap to BBitmap
// ============================================================================

BBitmap* WxBitmapToBBitmap(const wxBitmap& wxbitmap) {
    if (!wxbitmap.IsOk()) {
        wxLogDebug("WxBitmapToBBitmap: Invalid wxBitmap");
        return nullptr;
    }

    int width = wxbitmap.GetWidth();
    int height = wxbitmap.GetHeight();
    wxImage image = wxbitmap.ConvertToImage(); // Always safe

    bool hasAlpha = image.HasAlpha();
    // Prefer 32-bit if alpha is present, otherwise 24-bit
    color_space cs = hasAlpha ? B_RGBA32 : B_RGB32;

    BRect bounds(0, 0, width - 1, height - 1);
    BBitmap* bbitmap = new BBitmap(bounds, cs);

    if (bbitmap->InitCheck() != B_OK) {
        wxLogDebug("WxBitmapToBBitmap: Failed to create BBitmap");
        delete bbitmap;
        return nullptr;
    }

    const unsigned char* imgData = image.GetData();
    const unsigned char* alphaData = hasAlpha ? image.GetAlpha() : nullptr;
    uint8* bits = static_cast<uint8*>(bbitmap->Bits());
    int32 bpr = bbitmap->BytesPerRow();

    for (int y = 0; y < height; y++) {
        uint8* row = bits + (y * bpr);
        for (int x = 0; x < width; x++) {
            int imgIdx = (y * width + x) * 3;
            unsigned char r = imgData[imgIdx + 0];
            unsigned char g = imgData[imgIdx + 1];
            unsigned char b = imgData[imgIdx + 2];
            unsigned char a = alphaData ? alphaData[y * width + x] : 255;

            // Using B_RGBA32 (BGRA on little-endian)
            int pixelIdx = x * 4;
            row[pixelIdx + 0] = b;
            row[pixelIdx + 1] = g;
            row[pixelIdx + 2] = r;
            row[pixelIdx + 3] = a;
        }
    }

    return bbitmap;
}

// ============================================================================
// Utility Functions
// ============================================================================

bool CopyBBitmapToWxBitmap(const BBitmap& source, wxBitmap& dest) {
    dest = BBitmapToWxBitmap(source);
    return dest.IsOk();
}

bool CopyWxBitmapToBBitmap(const wxBitmap& source, BBitmap*& dest) {
    if (dest) {
        delete dest;
        dest = nullptr;
    }
    dest = WxBitmapToBBitmap(source);
    return (dest != nullptr && dest->IsValid());
}

} // namespace HaikuWxConverter
