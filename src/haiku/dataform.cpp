/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/dataform.cpp
// Purpose:     wxDataFormat implementation for Haiku
// Author:      Based on Qt; adapted for Haiku
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#include "wx/dataobj.h"

// ----------------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------------

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
        case wxDF_BITMAP:      return "image/bmp";

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
