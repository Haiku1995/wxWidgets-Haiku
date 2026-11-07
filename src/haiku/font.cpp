///////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/font.cpp
// Purpose:     wxFont implementation for Haiku
// Author:      Haiku wxWidgets port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#ifdef __HAIKU__

#include "wx/font.h"
#include "wx/haiku/font.h"
#include "wx/fontutil.h"
#include "wx/log.h"
#include "wx/string.h"
#include "wx/intl.h"
#include "wx/arrstr.h"
#include "wx/tokenzr.h"  // FIX: Added missing include for wxStringTokenizer

#include <Font.h>
#include <InterfaceDefs.h>

// ----------------------------------------------------------------------------
// wxFontRefData
// ----------------------------------------------------------------------------

class wxFontRefData : public wxGDIRefData
{
public:
    wxFontRefData()
    {
        m_bFont = new BFont(be_plain_font);
        m_underlined = false;
        m_strikethrough = false;
        m_faceName = wxEmptyString;
    }

    wxFontRefData(const wxFontInfo& info)
    {
        m_bFont = new BFont(be_plain_font);
        m_underlined = info.IsUnderlined();
        m_strikethrough = info.IsStrikethrough();
        m_faceName = info.GetFaceName();
        if (info.GetFamily() == wxFONTFAMILY_TELETYPE)
            *m_bFont = *be_fixed_font;
        if (!m_faceName.empty())
            m_bFont->SetFamilyAndStyle(m_faceName.utf8_str(), nullptr);
        font_family nativeFamily;
        font_style nativeStyle;
        m_bFont->GetFamilyAndStyle(&nativeFamily, &nativeStyle);
        m_faceName = wxString::FromUTF8(nativeFamily);

        if (info.IsUsingSizeInPixels())
            m_bFont->SetSize(info.GetPixelSize().GetHeight());
        else
            m_bFont->SetSize(info.GetFractionalPointSize());

        uint32 face = m_bFont->Face();
        if (info.GetStyle() == wxFONTSTYLE_ITALIC || info.GetStyle() == wxFONTSTYLE_SLANT)
            face |= B_ITALIC_FACE;
        else
            face &= ~B_ITALIC_FACE;
        
        if (info.GetWeight() >= wxFONTWEIGHT_MEDIUM)
            face |= B_BOLD_FACE;
        else
            face &= ~B_BOLD_FACE;

        m_bFont->SetFace(face);
    }

    wxFontRefData(const wxFontRefData& data)
    {
        m_bFont = new BFont(*data.m_bFont);
        m_underlined = data.m_underlined;
        m_strikethrough = data.m_strikethrough;
        m_faceName = data.m_faceName;
    }

    virtual ~wxFontRefData()
    {
        delete m_bFont;
    }

    BFont* GetBFont() const { return m_bFont; }
    bool GetUnderlined() const { return m_underlined; }
    bool GetStrikethrough() const { return m_strikethrough; }
    const wxString& GetFaceName() const { return m_faceName; }

    bool SetFaceName(const wxString& faceName)
    {
        if (faceName.empty() || m_bFont->SetFamilyAndStyle(faceName.utf8_str(), nullptr) != B_OK)
            return false;
        m_faceName = faceName;
        return true;
}

    void SetSize(double size) { m_bFont->SetSize(size); }
    
    void SetStyle(wxFontStyle style)
    {
        uint32 face = m_bFont->Face() & ~B_ITALIC_FACE;
        if (style == wxFONTSTYLE_ITALIC || style == wxFONTSTYLE_SLANT)
            face |= B_ITALIC_FACE;
        m_bFont->SetFace(face);
    }

    void SetWeight(int weight) 
    { 
        uint32 face = m_bFont->Face() & ~B_BOLD_FACE;
        if (weight >= wxFONTWEIGHT_MEDIUM)
            face |= B_BOLD_FACE;
        m_bFont->SetFace(face);
    }

    void SetUnderlined(bool u) { m_underlined = u; }
    void SetStrikethrough(bool s) { m_strikethrough = s; }

    bool operator==(const wxFontRefData& data) const
    {
        if (m_bFont->Size() != data.m_bFont->Size())
            return false;
        if (m_bFont->Face() != data.m_bFont->Face())
            return false;
            
        font_family f1, f2;
        font_style s1, s2;
        m_bFont->GetFamilyAndStyle(&f1, &s1);
        data.m_bFont->GetFamilyAndStyle(&f2, &s2);
        
        if (strcmp(f1, f2) != 0 || strcmp(s1, s2) != 0)
            return false;

        return m_underlined == data.m_underlined &&
               m_strikethrough == data.m_strikethrough;
    }

private:
    BFont* m_bFont;
    bool m_underlined;
    bool m_strikethrough;
    wxString m_faceName;
};

// ----------------------------------------------------------------------------
// wxFont — Haiku-specific
// ----------------------------------------------------------------------------
#define M_FONTREFDATA ((wxFontRefData*)m_refData)
#define M_FONTDATA (M_FONTREFDATA->GetBFont())

// Constructors
wxFont::wxFont() 
{ 
    m_refData = new wxFontRefData(); 
}

wxFont::wxFont(const wxFontInfo& info) 
{ 
    m_refData = new wxFontRefData(info); 
}

wxFont::wxFont(const wxString& nativeFontInfoString)
{
    m_refData = new wxFontRefData();
    wxNativeFontInfo info;
    if (info.FromString(nativeFontInfoString))
        DoSetNativeFontInfo(info);
}

wxFont::wxFont(const wxNativeFontInfo& info)
{
    m_refData = new wxFontRefData();
    M_FONTREFDATA->SetSize(info.GetFractionalPointSize());
    M_FONTREFDATA->SetStyle(info.GetStyle());
    M_FONTREFDATA->SetWeight(info.GetNumericWeight());
    M_FONTREFDATA->SetUnderlined(info.GetUnderlined());
    M_FONTREFDATA->SetStrikethrough(info.GetStrikethrough());
    M_FONTREFDATA->SetFaceName(info.GetFaceName());
}

wxFont::wxFont(const BFont& font)
{
    m_refData = new wxFontRefData();
    *M_FONTDATA = font;
}

wxFont::wxFont(int size,
               wxFontFamily family,
               wxFontStyle style,
               wxFontWeight weight,
               bool underlined,
               const wxString& face,
               wxFontEncoding encoding)
{
    wxFontInfo info(size);
    info.Family(family).Style(style).Weight(weight)
        .Underlined(underlined).FaceName(face).Encoding(encoding);
    m_refData = new wxFontRefData(info);
}

wxFont::wxFont(const wxSize& pixelSize,
               wxFontFamily family,
               wxFontStyle style,
               wxFontWeight weight,
               bool underlined,
               const wxString& face,
               wxFontEncoding encoding)
{
    wxFontInfo info(pixelSize);
    info.Family(family).Style(style).Weight(weight)
        .Underlined(underlined).FaceName(face).Encoding(encoding);
    m_refData = new wxFontRefData(info);
}

wxFont::wxFont(int size,
               int family,
               int style,
               int weight,
               bool underlined,
               const wxString& face,
               wxFontEncoding encoding)
{
    wxFont(size, (wxFontFamily)family, (wxFontStyle)style, 
           (wxFontWeight)weight, underlined, face, encoding);
}

// Creation
bool wxFont::Create(const wxFontInfo& info)
{
    // FIX: Cannot assign wxFontRefData due to deleted operator= in base class.
    // Replace m_refData instead.
    if (m_refData)
        m_refData->DecRef();
    m_refData = new wxFontRefData(info);
    return IsOk();
}

bool wxFont::Create(const wxString& nativeFontInfoString)
{
    wxNativeFontInfo info;
    if (info.FromString(nativeFontInfoString))
    {
        AllocExclusive();
        DoSetNativeFontInfo(info);
        return IsOk();
    }
    return false;
}

bool wxFont::Create(const wxNativeFontInfo& info)
{
    AllocExclusive();
    DoSetNativeFontInfo(info);
    return IsOk();
}

bool wxFont::Create(int size,
                    wxFontFamily family,
                    wxFontStyle style,
                    wxFontWeight weight,
                    bool underlined,
                    const wxString& face,
                    wxFontEncoding encoding)
{
    // FIX: Cannot assign wxFontRefData due to deleted operator= in base class.
    // Replace m_refData instead.
    if (m_refData)
        m_refData->DecRef();
    wxFontInfo info(size);
    info.Family(family).Style(style).Weight(weight)
        .Underlined(underlined).FaceName(face).Encoding(encoding);
    m_refData = new wxFontRefData(info);
    return IsOk();
}

bool wxFont::Create(const wxSize& pixelSize,
                    wxFontFamily family,
                    wxFontStyle style,
                    wxFontWeight weight,
                    bool underlined,
                    const wxString& face,
                    wxFontEncoding encoding)
{
    // FIX: Cannot assign wxFontRefData due to deleted operator= in base class.
    // Replace m_refData instead.
    if (m_refData)
        m_refData->DecRef();
    wxFontInfo info(pixelSize);
    info.Family(family).Style(style).Weight(weight)
        .Underlined(underlined).FaceName(face).Encoding(encoding);
    m_refData = new wxFontRefData(info);
    return IsOk();
}

// GDI helpers
wxGDIRefData* wxFont::CreateGDIRefData() const 
{ 
    return new wxFontRefData; 
}

wxGDIRefData* wxFont::CloneGDIRefData(const wxGDIRefData* data) const
{
    return new wxFontRefData(*static_cast<const wxFontRefData*>(data));
}

// Accessors
int wxFont::GetPointSize() const 
{ 
    return (int)GetFractionalPointSize(); 
}

double wxFont::GetFractionalPointSize() const 
{ 
    return M_FONTDATA->Size(); 
}

wxSize wxFont::GetPixelSize() const
{
    float size = M_FONTDATA->Size();
    return wxSize((int)(size * 0.6), (int)size);
}

wxFontStyle wxFont::GetStyle() const
{
    return (M_FONTDATA->Face() & B_ITALIC_FACE) ? wxFONTSTYLE_ITALIC : wxFONTSTYLE_NORMAL;
}

int wxFont::GetNumericWeight() const 
{ 
    return (M_FONTDATA->Face() & B_BOLD_FACE) ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL; 
}

bool wxFont::GetUnderlined() const 
{ 
    return M_FONTREFDATA->GetUnderlined(); 
}

bool wxFont::GetStrikethrough() const 
{ 
    return M_FONTREFDATA->GetStrikethrough(); 
}

wxString wxFont::GetFaceName() const 
{
    font_family family;
    font_style style;
    M_FONTDATA->GetFamilyAndStyle(&family, &style);
    return wxString::FromUTF8(family);
}

wxFontEncoding wxFont::GetEncoding() const 
{ 
    return wxFONTENCODING_UTF8; 
}

const wxNativeFontInfo* wxFont::GetNativeFontInfo() const
{
    static wxNativeFontInfo s_info;
    s_info.Init();
    s_info.SetFractionalPointSize(GetFractionalPointSize());
    s_info.SetStyle(GetStyle());
    s_info.SetNumericWeight(GetNumericWeight());
    s_info.SetUnderlined(GetUnderlined());
    s_info.SetStrikethrough(GetStrikethrough());
    s_info.SetFaceName(GetFaceName());
    s_info.SetEncoding(GetEncoding());
    return &s_info;
}

bool wxFont::IsFixedWidth() const
{
    return IsOk() && M_FONTDATA->IsFixed();
}

// Mutators
void wxFont::SetFractionalPointSize(double size) 
{ 
    AllocExclusive(); 
    M_FONTREFDATA->SetSize(size); 
}

void wxFont::SetPixelSize(const wxSize& sz) 
{ 
    AllocExclusive(); 
    M_FONTREFDATA->SetSize(sz.GetHeight()); 
}

void wxFont::SetFamily(wxFontFamily WXUNUSED(family)) 
{
    // Haiku doesn't use generic families directly
}

void wxFont::SetStyle(wxFontStyle style) 
{ 
    AllocExclusive(); 
    M_FONTREFDATA->SetStyle(style); 
}

void wxFont::SetNumericWeight(int weight) 
{ 
    AllocExclusive(); 
    M_FONTREFDATA->SetWeight(weight); 
}

bool wxFont::SetFaceName(const wxString& faceName) 
{ 
    AllocExclusive(); 
    return M_FONTREFDATA->SetFaceName(faceName); 
}

void wxFont::SetUnderlined(bool u) 
{ 
    AllocExclusive(); 
    M_FONTREFDATA->SetUnderlined(u); 
}

void wxFont::SetStrikethrough(bool s) 
{ 
    AllocExclusive(); 
    M_FONTREFDATA->SetStrikethrough(s); 
}

void wxFont::SetEncoding(wxFontEncoding WXUNUSED(enc)) 
{
    // Haiku uses UTF-8 primarily
}

// Haiku native access
BFont wxFont::GetHandle() const
{
    wxCHECK_MSG(IsOk(), *be_plain_font, "Invalid font");
    return *M_FONTDATA;
}

BFont* wxFont::GetBFont() const
{
    return M_FONTDATA;
}

// Protected
wxFontFamily wxFont::DoGetFamily() const
{
    wxString face = GetFaceName();
    if (face.Contains("Serif")) return wxFONTFAMILY_ROMAN;
    if (face.Contains("Mono") || face.Contains("Courier")) return wxFONTFAMILY_TELETYPE;
    return wxFONTFAMILY_SWISS;
}

void wxFont::DoSetNativeFontInfo(const wxNativeFontInfo& info) 
{ 
    M_FONTREFDATA->SetSize(info.GetFractionalPointSize());
    M_FONTREFDATA->SetStyle(info.GetStyle());
    M_FONTREFDATA->SetWeight(info.GetNumericWeight());
    M_FONTREFDATA->SetUnderlined(info.GetUnderlined());
    M_FONTREFDATA->SetStrikethrough(info.GetStrikethrough());
    M_FONTREFDATA->SetFaceName(info.GetFaceName());
}

// ----------------------------------------------------------------------------
// wxNativeFontInfo
// ----------------------------------------------------------------------------
/*
void wxNativeFontInfo::Init()
{
    pointSize = 12.0;
    family = wxFONTFAMILY_SWISS;
    style = wxFONTSTYLE_NORMAL;
    weight = wxFONTWEIGHT_NORMAL;
    underlined = false;
    strikethrough = false;
    faceName = wxEmptyString;
    encoding = wxFONTENCODING_UTF8;
}
*/
//int wxNativeFontInfo::GetPointSize() const
//{
  //  return (int)pointSize;
//}

double wxNativeFontInfo::GetFractionalPointSize() const
{
    return pointSize;
}

wxSize wxNativeFontInfo::GetPixelSize() const
{
    return wxSize(0, (int)pointSize);
}

wxFontStyle wxNativeFontInfo::GetStyle() const
{
    return style;
}

//wxFontWeight wxNativeFontInfo::GetWeight() const
//{
  //  return (wxFontWeight)weight;
//}

int wxNativeFontInfo::GetNumericWeight() const
{
    return weight;
}

bool wxNativeFontInfo::GetUnderlined() const
{
    return underlined;
}

bool wxNativeFontInfo::GetStrikethrough() const
{
    return strikethrough;
}

wxString wxNativeFontInfo::GetFaceName() const
{
    return faceName;
}

wxFontFamily wxNativeFontInfo::GetFamily() const
{
    return family;
}

wxFontEncoding wxNativeFontInfo::GetEncoding() const
{
    return encoding;
}

//void wxNativeFontInfo::SetPointSize(int pointsize)
//{
//    pointSize = (double)pointsize;
//}

void wxNativeFontInfo::SetFractionalPointSize(double pointsize)
{
    pointSize = pointsize;
}

void wxNativeFontInfo::SetPixelSize(const wxSize& pixelSize)
{
    pointSize = (double)pixelSize.GetHeight();
}

void wxNativeFontInfo::SetStyle(wxFontStyle style)
{
    this->style = style;
}

void wxNativeFontInfo::SetNumericWeight(int weight)
{
    this->weight = weight;
}

//void wxNativeFontInfo::SetWeight(wxFontWeight weight)
//{
//    this->weight = (int)weight;
//}

void wxNativeFontInfo::SetUnderlined(bool underlined)
{
    this->underlined = underlined;
}

void wxNativeFontInfo::SetStrikethrough(bool strikethrough)
{
    this->strikethrough = strikethrough;
}

bool wxNativeFontInfo::SetFaceName(const wxString& facename)
{
    BFont testFont(be_plain_font);
    font_family f;
    font_style s;
    testFont.GetFamilyAndStyle(&f, &s);
    if (testFont.SetFamilyAndStyle(facename.mb_str(), s) == B_OK)
    {
        faceName = facename;
        return true;
    }
    return false;
}

void wxNativeFontInfo::SetFamily(wxFontFamily family)
{
    this->family = family;
    if (faceName.empty())
    {
        switch (family)
        {
            case wxFONTFAMILY_ROMAN: faceName = "Times New Roman"; break;
            case wxFONTFAMILY_TELETYPE: faceName = "Courier New"; break;
            case wxFONTFAMILY_DECORATIVE: faceName = "Impact"; break;
            case wxFONTFAMILY_SCRIPT: faceName = "Comic Sans MS"; break;
            default: faceName = "Helvetica"; break;
        }
    }
}

void wxNativeFontInfo::SetEncoding(wxFontEncoding encoding)
{
    this->encoding = encoding;
}

//void wxNativeFontInfo::SetFaceName(const wxArrayString& facenames)
//{
  //  for (size_t i = 0; i < facenames.GetCount(); i++)
    //{
      //  if (SetFaceName(facenames[i]))
        //    return;
    //}
    // If none valid, use first available system font
   // if (!facenames.IsEmpty())
     //   faceName = facenames[0];
//}
/*
bool wxNativeFontInfo::FromString(const wxString& s)
{
    wxStringTokenizer tkz(s, ":"); 
    if (!tkz.HasMoreTokens()) return false;

    faceName = tkz.GetNextToken();
    
    if (!tkz.HasMoreTokens()) return false;
    double size;
    if (!tkz.GetNextToken().ToDouble(&size)) return false;
    pointSize = size;

    if (!tkz.HasMoreTokens()) return false;
    weight = wxAtoi(tkz.GetNextToken());

    if (!tkz.HasMoreTokens()) return false;
    style = (wxFontStyle)wxAtoi(tkz.GetNextToken());

    if (!tkz.HasMoreTokens()) return false;
    underlined = (wxAtoi(tkz.GetNextToken()) != 0);

    if (!tkz.HasMoreTokens()) return false;
    strikethrough = (wxAtoi(tkz.GetNextToken()) != 0);

    if (!tkz.HasMoreTokens()) return false;
    encoding = (wxFontEncoding)wxAtoi(tkz.GetNextToken());

    return true;
}*/
/*
wxString wxNativeFontInfo::ToString() const
{
    wxString s;
    s << faceName << ":" 
      << pointSize << ":"
      << weight << ":"
      << style << ":"
      << (underlined ? 1 : 0) << ":"
      << (strikethrough ? 1 : 0) << ":"
      << encoding;
    return s;
}
*/
bool wxNativeFontInfo::FromUserString(const wxString& s)
{
    return FromString(s);
}

wxString wxNativeFontInfo::ToUserString() const
{
    return ToString();
}

#endif // __HAIKU__
