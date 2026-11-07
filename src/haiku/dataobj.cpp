/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/dataobj.cpp
// Purpose:     wxDataObject implementation for Haiku using BMessage
// Author:      Based on Qt; adapted for Haiku
// Created:     [Date]
// Copyright:   (c) 2025 wxWidgets team
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/dataobj.h"
#include "wx/haiku/private.h"
#include "wx/haiku/converter.h"
#include "wx/bitmap.h"
#include "wx/filename.h"
#include "wx/filesys.h"
#include "wx/log.h"
#include "wx/scopedarray.h"
#include <Message.h>
#include <Bitmap.h>
#include <Entry.h>
#include <Path.h>
#include "wx/mstream.h"
typedef wxScopedArray<wxDataFormat> wxDataFormatArray;
namespace
{

wxString DataFormatIdToMimeType(wxDataFormatId formatId)
{
    switch ( formatId )
    {
        case wxDF_TEXT:        return "text/plain";
        case wxDF_UNICODETEXT: return "text/plain";
        case wxDF_HTML:        return "text/html";

        // Keep aligned with other ports: wxDF_BITMAP is historically BMP-ish.
        // You can switch this to "image/x-be-image" if you build a Haiku-native
        // clipboard using BMessage/translator, but then keep it consistent.
        case wxDF_BITMAP:      return "image/png";

        case wxDF_TIFF:        return "image/tiff";
        case wxDF_WAVE:        return "audio/x-wav";

        // For drag&drop/file list style transfers.
        case wxDF_FILENAME:    return "text/uri-list";

        // Unsupported/legacy formats: return empty MIME.
        case wxDF_METAFILE:
        case wxDF_SYLK:
        case wxDF_DIF:
        case wxDF_OEMTEXT:
        case wxDF_DIB:
        case wxDF_PALETTE:
        case wxDF_PENDATA:
        case wxDF_RIFF:
        case wxDF_ENHMETAFILE:
        case wxDF_LOCALE:
        case wxDF_PRIVATE:
        case wxDF_INVALID:
        case wxDF_MAX:
        default:
            return "";
    }
}

} // anonymous namespace

// ----------------------------------------------------------------------------
// wxDataFormat
// ----------------------------------------------------------------------------

wxDataFormat::wxDataFormat(wxDataFormatId formatId)
{
    SetType(formatId);
}

wxDataFormat::wxDataFormat(const wxString& id)
{
    SetId(id);
}

const wxString& wxDataFormat::GetMimeType() const
{
    return m_mimeType;
}

void wxDataFormat::SetMimeType(const wxString& mimeType)
{
    m_mimeType = mimeType;
    m_formatId = wxDF_INVALID;
}

void wxDataFormat::SetId(const wxString& id)
{
    SetMimeType(id);
}

const wxString& wxDataFormat::GetId() const
{
    // For Haiku we treat the "id" as MIME type string (same as Qt port).
    return m_mimeType;
}

wxDataFormatId wxDataFormat::GetType() const
{
    return m_formatId;
}

void wxDataFormat::SetType(wxDataFormatId formatId)
{
    m_mimeType = DataFormatIdToMimeType(formatId);
    m_formatId = formatId;
}

bool wxDataFormat::operator==(wxDataFormatId format) const
{
    // Match Qt semantics: compare both MIME and type.
    return m_mimeType == DataFormatIdToMimeType(format)
        && m_formatId == format;
}

bool wxDataFormat::operator!=(wxDataFormatId format) const
{
    return !operator==(format);
}

bool wxDataFormat::operator==(const wxDataFormat& format) const
{
    // If MIME types match, that's good enough (standard-vs-custom).
    if ( !m_mimeType.empty() && m_mimeType == format.m_mimeType )
        return true;

    return m_mimeType == format.m_mimeType
        && m_formatId == format.m_formatId;
}

bool wxDataFormat::operator!=(const wxDataFormat& format) const
{
    return !operator==(format);
}

inline wxString wxHaikuConvertString(const char* str)
{
    return wxString(str, wxConvUTF8);
}

inline wxString wxHaikuConvertString(const BString& str)
{
    return wxString(str.String(), wxConvUTF8);
}

inline BString wxHaikuConvertString(const wxString& str)
{
    return BString(str.utf8_str());
}
//namespace
//{



//} // anonymous namespace



// ----------------------------------------------------------------------------
// wxDataObject
// ----------------------------------------------------------------------------
wxDataObject::wxDataObject()
{
}

wxDataObject::~wxDataObject()
{
}

bool wxDataObject::IsSupportedFormat(const wxDataFormat& format, Direction dir) const
{
    const size_t count = GetFormatCount(dir);
    if (count == 1)
        return format == GetPreferredFormat(dir);

    wxDataFormatArray formats(count);
    GetAllFormats(formats.get(), dir);

    for (size_t i = 0; i < count; ++i)
    {
        if (formats[i] == format)
            return true;
    }
    return false;
}

wxString wxDataObject::WxFormatToMime(const wxDataFormat& format)
{
    switch (format.GetType())
    {
        case wxDF_TEXT:
        case wxDF_UNICODETEXT:
            return "text/plain";
        case wxDF_HTML:
            return "text/html";
        case wxDF_BITMAP:
            // We store as PNG by default in wxBitmapDataObject override,
            // but generic mapping still defines a stable mime.
            return "image/png";
        case wxDF_FILENAME:
            return "text/uri-list";
        default:
            // Custom: use id/mimetype stored in wxDataFormat
            return !format.GetMimeType().empty() ? format.GetMimeType()
                                                : format.GetId();
    }
}

wxDataFormat wxDataObject::MimeToWxFormat(const char* mime)
{
    if (!mime || !*mime)
        return wxDataFormat(wxDF_INVALID);

    if (strcmp(mime, "text/plain") == 0)
        return wxDataFormat(wxDF_TEXT);
    if (strcmp(mime, "text/html") == 0)
        return wxDataFormat(wxDF_HTML);
    if (strcmp(mime, "text/uri-list") == 0)
        return wxDataFormat(wxDF_FILENAME);
    if (strcmp(mime, "image/png") == 0)
        return wxDataFormat(wxDF_BITMAP);

    return wxDataFormat(wxString::FromUTF8(mime));
}

void wxDataObject::HaikuAddDataTo(BMessage& msg) const
{
    const size_t count = GetFormatCount(Get);
    wxDataFormatArray formats(count);
    GetAllFormats(formats.get(), Get);

    for (size_t i = 0; i < count; ++i)
    {
        const wxDataFormat fmt(formats[i]);
        const wxString mime = WxFormatToMime(fmt);

        const size_t size = GetDataSize(fmt);
        if (!size)
            continue;

        wxMemoryBuffer buf(size);
        void* dst = buf.GetWriteBuf(size);
        if (!GetDataHere(fmt, dst))
        {
            buf.UngetWriteBuf(0);
            continue;
        }
        buf.UngetWriteBuf(size);

        msg.AddData(mime.utf8_str(), B_MIME_TYPE, buf.GetData(), buf.GetDataLen());
    }
}

bool wxDataObject::HaikuSetDataFrom(const BMessage& msg)
{
    const size_t count = GetFormatCount(Set);
    wxDataFormatArray formats(count);
    GetAllFormats(formats.get(), Set);

    // Try each supported format: a present format can still be rejected.
    for (size_t i = 0; i < count; ++i)
    {
        const wxDataFormat fmt(formats[i]);
        const wxString mime = WxFormatToMime(fmt);

        const void* data = nullptr;
        ssize_t len = 0;

        if (msg.FindData(mime.utf8_str(), B_ANY_TYPE, &data, &len) == B_OK && data && len > 0)
        {
            if (SetData(fmt, (size_t)len, data))
                return true;
        }
    }

    return false;
}
void wxDataObject::HaikuSetDataSingleFormat(const BMessage& msg,
                                            const wxDataFormat& format)
{
    const wxString mime = WxFormatToMime(format);

    const void* data = nullptr;
    ssize_t len = 0;

    if (msg.FindData(mime.utf8_str(), B_ANY_TYPE, &data, &len) == B_OK &&
        data && len > 0)
    {
        SetData(format, (size_t)len, data);
    }
}

// ---------------------------- wxBitmapDataObject ---------------------------

wxBitmapDataObject::wxBitmapDataObject()
{
    
}

wxBitmapDataObject::wxBitmapDataObject(const wxBitmap& bitmap)
    : wxBitmapDataObjectBase(bitmap)
{
}

bool wxBitmapDataObject::BitmapToPngBytes(const wxBitmap& bmp, wxMemoryBuffer& out)
{
#if !wxUSE_IMAGE
    wxUnusedVar(bmp);
    wxUnusedVar(out);
    return false;
#else
    if (!bmp.IsOk())
        return false;

    wxImage img = bmp.ConvertToImage();
    if (!img.IsOk())
        return false;

    wxMemoryOutputStream mos;
    if (!img.SaveFile(mos, wxBITMAP_TYPE_PNG))
        return false;

    const size_t len = mos.GetLength();
    if (!len)
        return false;

    wxStreamBuffer* sb = mos.GetOutputStreamBuffer();
    const void* src = sb->GetBufferStart();

    out.Clear();
    out.AppendData(src, len);
    return true;
#endif
}

bool wxBitmapDataObject::PngBytesToBitmap(const void* data, size_t len, wxBitmap& outBmp)
{
#if !wxUSE_IMAGE
    wxUnusedVar(data);
    wxUnusedVar(len);
    wxUnusedVar(outBmp);
    return false;
#else
    if (!data || !len)
        return false;

    wxMemoryInputStream mis(data, len);
    wxImage img(mis, wxBITMAP_TYPE_PNG);
    if (!img.IsOk())
        return false;

    outBmp = wxBitmap(img);
    return outBmp.IsOk();
#endif
}

void wxBitmapDataObject::HaikuAddDataTo(BMessage& msg) const
{
    wxMemoryBuffer png;
    if (!BitmapToPngBytes(GetBitmap(), png))
        return;

    BBitmap* native = HaikuWxConverter::WxBitmapToBBitmap(GetBitmap());
    if (native)
    {
        BMessage archive;
        if (native->Archive(&archive) == B_OK)
            msg.AddMessage("image/bitmap", &archive);
        delete native;
    }
    // Also publish PNG for portable consumers.
    msg.AddData("image/png", B_MIME_TYPE, png.GetData(), png.GetDataLen());

    // Optional compatibility keys (harmless):
    msg.AddData("wxbitmap", B_MIME_TYPE, png.GetData(), png.GetDataLen());
}

bool wxBitmapDataObject::HaikuSetDataFrom(const BMessage& msg)
{
    BMessage archive;
    if (msg.FindMessage("image/bitmap", &archive) == B_OK)
    {
        BBitmap bitmap(&archive);
        if (bitmap.IsValid())
        {
            wxBitmap converted = HaikuWxConverter::BBitmapToWxBitmap(bitmap);
            if (converted.IsOk()) { SetBitmap(converted); return true; }
        }
    }
    // Prefer "image/png".
    const void* data = nullptr;
    ssize_t len = 0;

    if (msg.FindData("image/png", B_ANY_TYPE, &data, &len) != B_OK || !data || len <= 0)
    {
        // Fallback for your old key:
        if (msg.FindData("wxbitmap", B_ANY_TYPE, &data, &len) != B_OK || !data || len <= 0)
            return false;
    }

    wxBitmap bmp;
    if (!PngBytesToBitmap(data, (size_t)len, bmp))
        return false;

    SetBitmap(bmp);
    return true;
}

//==============================================

// ============================================================================
// wxFileDataObject (Haiku)
// ============================================================================
 wxFileDataObject:: wxFileDataObject(){}
void wxFileDataObject::AddFile(const wxString& filename)
{
    //wxArrayString f = GetFilenames();
    m_filenames.Add(filename);
    //SetFilenames(f);
}

static wxString BuildUriList(const wxArrayString& files)
{
    wxString out;
    for (const auto& file : files)
        out += wxFileSystem::FileNameToURL(wxFileName(file)) + "\r\n";
    return out;
}

static void ParseUriList(const wxString& s, wxArrayString& outFiles)
{
    for (auto line : wxSplit(s, '\n', '\0'))
    {
        line.Trim(true).Trim(false);
        if (line.empty() || line.StartsWith("#")) continue;
        if (line.StartsWith("file:"))
            outFiles.Add(wxFileSystem::URLToFileName(line).GetFullPath());
    }
}

void wxFileDataObject::HaikuAddDataTo(BMessage& msg) const
{
    const wxArrayString files = GetFilenames();
    if (files.empty())
        return;

    for (const auto& file : files)
    {
        entry_ref ref;
        if (get_ref_for_path(file.utf8_str(), &ref) == B_OK)
            msg.AddRef("refs", &ref);
    }
    const wxString uriList = BuildUriList(files);
    const wxCharBuffer utf8 = uriList.utf8_str();

    msg.AddData("text/uri-list", B_MIME_TYPE, utf8.data(), strlen(utf8.data()));
    msg.AddData("text/plain",   B_RAW_TYPE, utf8.data(), strlen(utf8.data()));
}

bool wxFileDataObject::HaikuSetDataFrom(const BMessage& msg)
{
    wxArrayString nativeFiles;
    entry_ref ref;
    for (int32 i = 0; msg.FindRef("refs", i, &ref) == B_OK; ++i)
    {
        BPath path(&ref);
        if (path.InitCheck() == B_OK)
            nativeFiles.Add(wxString::FromUTF8(path.Path()));
    }
    if (!nativeFiles.empty())
    {
        m_filenames = nativeFiles;
        return true;
    }
    const void* data = nullptr;
    ssize_t len = 0;

    if (msg.FindData("text/uri-list", B_ANY_TYPE, &data, &len) != B_OK || !data || len <= 0)
    {
        if (msg.FindData("text/plain", B_ANY_TYPE, &data, &len) != B_OK || !data || len <= 0)
            return false;
    }

    const wxString s = wxString::FromUTF8((const char*)data, (size_t)len);

    wxArrayString parsed;
    ParseUriList(s, parsed);

    if (parsed.empty())
        return false;

    m_filenames=parsed;
    return true;
} 

