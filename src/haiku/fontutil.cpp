#include "wx/wxprec.h"
#include "wx/fontutil.h"
#include <InterfaceKit.h>
#include <Font.h>
#include "wx/encinfo.h"
#include "wx/tokenzr.h"
#include "wx/wxprec.h"

#include "wx/wxprec.h"


#include "wx/fontutil.h"

#ifndef WX_PRECOMP
    #include "wx/string.h"
    #include "wx/log.h"
    #include "wx/intl.h"
    #include "wx/wxcrtvararg.h"
    #include "wx/msw/private.h"
#endif //WX_PRECOMP

#include "wx/encinfo.h"
#include "wx/fontmap.h"
#include "wx/tokenzr.h"
// ----------------------------------------------------------------------------
// wxNativeFontInfo methods for Haiku
// ----------------------------------------------------------------------------
int      charset;
void wxNativeFontInfo::Init()
{
    pointSize = 12.0;
    family = wxFONTFAMILY_DEFAULT;
    style = wxFONTSTYLE_NORMAL;
    weight = 400;
    underlined = false;
    strikethrough = false;
    faceName = "Be Fixed"; // default Haiku font
    encoding = wxFONTENCODING_UTF8;
}

bool wxNativeFontInfo::FromString(const wxString& s)
{
    wxArrayString parts = wxSplit(s, ';');
    if (parts.size() < 6)
        return false;

    faceName = parts[0];
    pointSize = wxAtof(parts[1]);
    weight = wxAtoi(parts[2]);
    style = static_cast<wxFontStyle>(wxAtoi(parts[3]));
    underlined = (parts[4] == "1");
    strikethrough = (parts[5] == "1");

    return true;
}

wxString wxNativeFontInfo::ToString() const
{
    wxString s;
    s << faceName << ";" << pointSize << ";" << weight << ";" << (int)style
      << ";" << (underlined ? "1" : "0") << ";" << (strikethrough ? "1" : "0");
    return s;
}

bool wxNativeEncodingInfo::FromString( const wxString& s )
{
    wxStringTokenizer tokenizer(s, wxT(";"));

    facename = tokenizer.GetNextToken();
    if ( facename.empty() )
        return false;

    wxString tmp = tokenizer.GetNextToken();
    if ( tmp.empty() )
    {
        // default charset
        charset = 0;
    }
    else
    {
        if ( wxSscanf( tmp, wxT("%u"), &charset ) != 1 )
            // should be a number!
            return false;
    }

    return true;
}

wxString wxNativeEncodingInfo::ToString() const
{
    wxString s(facename);
    if ( charset != 0 )
        s << wxT(';') << charset;

    return s;
}



// Other helpers can be implemented similarly without BFont
//#############################################################################

bool wxGetNativeFontEncoding(wxFontEncoding WXUNUSED(encoding),
                          wxNativeEncodingInfo *info)
{
    *info = wxNativeEncodingInfo();

    return false;
}


bool wxTestFontEncoding(const wxNativeEncodingInfo& WXUNUSED(info))
{
    return false;
}

