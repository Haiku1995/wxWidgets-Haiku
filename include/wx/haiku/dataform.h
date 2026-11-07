/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/dataform.h
// Author:      [Your Name]
// Copyright:   (c) [Your Name]
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_DATAFORM_H_
#define _WX_HAIKU_DATAFORM_H_

#include <String.h>

class WXDLLIMPEXP_CORE wxDataFormat
{
public:
    wxDataFormat(wxDataFormatId formatId = wxDF_INVALID);
    wxDataFormat(const wxString& id);

    // Standard methods
    const wxString& GetId() const;
    void SetId(const wxString& id);

    wxDataFormatId GetType() const;
    void SetType(wxDataFormatId type);
    void InitFromType(wxDataFormatId type);
    bool operator==(wxDataFormatId format) const;
    bool operator!=(wxDataFormatId format) const;
    bool operator==(const wxDataFormat& format) const;
    bool operator!=(const wxDataFormat& format) const;

    // Direct access to the underlying mime type.
    // Equivalent to "id", except "id" is supposed to be
    // invalid for standard types, whereas this should
    // always be valid (if meaningful).
    const wxString& GetMimeType() const;
    void SetMimeType(const wxString& mimeType);

private:
    wxString m_mimeType;          // MIME type for clipboard or drag-and-drop
    wxDataFormatId m_formatId;    // Format identifier, e.g., wxDF_TEXT, wxDF_HTML, etc.
};

#endif // _WX_HAIKU_DATAFORM_H_
