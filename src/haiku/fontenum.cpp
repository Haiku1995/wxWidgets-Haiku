/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/fontenum.cpp
// Author:      Alan Touran (based on Peter Most)
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#if wxUSE_FONTENUM

#include "wx/fontenum.h"
#include "wx/haiku/private/converter.h"

#include <InterfaceKit.h>

bool wxFontEnumerator::EnumerateFacenames(wxFontEncoding WXUNUSED(encoding), bool fixedWidthOnly)
{
    int32 familyCount = count_font_families();
    font_family family;
    font_style style;
    uint32 flags;

    for (int32 i = 0; i < familyCount; ++i)
    {
        if (get_font_family(i, &family, &flags) == B_OK)
        {
            // Check if this family has fixed-width fonts (if requested)
            bool includeFamily = true;
            if (fixedWidthOnly)
            {
                includeFamily = false;
                int32 styleCount = count_font_styles(family);
                for (int32 j = 0; j < styleCount; ++j)
                {
                    if (get_font_style(family, j, &style, &flags) == B_OK)
                    {
                        BFont testFont;
                        testFont.SetFamilyAndStyle(family, style);
                        if (testFont.IsFixed())
                        {
                            includeFamily = true;
                            break;
                        }
                    }
                }
            }

            if (includeFamily)
            {
                if (!OnFacename(wxString::FromUTF8(family)))
                    break;
            }
        }
    }

    return true;
}

bool wxFontEnumerator::EnumerateEncodings(const wxString& WXUNUSED(facename))
{
    // Haiku fonts use UTF-8 by default; encoding enumeration not relevant.
    return false;
}

#ifdef wxHAS_UTF8_FONTS
//bool wxFontEnumerator::EnumerateEncodingsUTF8(const wxString& WXUNUSED(facename))
//{
    // Haiku always uses UTF-8 for font rendering.
  //  return false;
//}
#endif

#endif // wxUSE_FONTENUM
