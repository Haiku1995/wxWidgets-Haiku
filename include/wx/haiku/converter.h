/////////////////////////////////////////////////////////////////////////////
// Name:        converter.h
// Purpose:     Utility functions to convert between Haiku OS and wxWidgets types
// Author:      Generated for Haiku/wxWidgets interoperability
// Licence:     MIT licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _HAIKU_WX_CONVERTER_H_
#define _HAIKU_WX_CONVERTER_H_

#include <wx/defs.h>
//#include <wx/rect.h>
//#include <wx/point.h>
//#include <wx/size.h>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/colour.h>

// Forward declarations for Haiku types
class BRect;
class BPoint;
class BSize;
struct rgb_color;
class BBitmap;

namespace HaikuWxConverter {

// ============================================================================
// Color Space Conversion
// ============================================================================

int GetWxDepthFromColorSpace(color_space cs);
color_space GetColorSpaceFromWxDepth(int depth, bool hasAlpha = false);

// ============================================================================
// Rectangle Conversion
// ============================================================================

wxRect BRectToWxRect(const BRect& brect);
BRect WxRectToBRect(const wxRect& wxrect);

// ============================================================================
// Point Conversion
// ============================================================================

wxPoint BPointToWxPoint(const BPoint& bpoint);
BPoint WxPointToBPoint(const wxPoint& wxpoint);

// ============================================================================
// Size Conversion
// ============================================================================

wxSize BSizeToWxSize(const BSize& bsize);
BSize WxSizeToBSize(const wxSize& wxsize);

// ============================================================================
// Color Conversion
// ============================================================================

wxColour RgbColorToWxColour(const rgb_color& color);
rgb_color WxColourToRgbColor(const wxColour& color);

// ============================================================================
// Bitmap Conversion
// ============================================================================

wxBitmap BBitmapToWxBitmap(const BBitmap& bbitmap);
BBitmap* WxBitmapToBBitmap(const wxBitmap& wxbitmap);

// ============================================================================
// Utility Functions (safe copying with error handling)
// ============================================================================

bool CopyBBitmapToWxBitmap(const BBitmap& source, wxBitmap& dest);
bool CopyWxBitmapToBBitmap(const wxBitmap& source, BBitmap*& dest);

} // namespace HaikuWxConverter

#endif // _HAIKU_WX_CONVERTER_H_
