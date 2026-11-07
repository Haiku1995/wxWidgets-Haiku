///////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/dataobj2.h
// Purpose:     wxHaiku wxBitmapDataObject and wxFileDataObject class declarations
// Author:      Adapted from Peter Most's wxQt implementation
// Copyright:   (c) wxWidgets dev team, adapted for Haiku
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_DATAOBJ2_H_
#define _WX_HAIKU_DATAOBJ2_H_
#include <vector>

    
// ----------------------------------------------------------------------------
// wxBitmapDataObject (Haiku): stores bitmap as PNG bytes
// ----------------------------------------------------------------------------
class WXDLLIMPEXP_CORE wxBitmapDataObject : public wxBitmapDataObjectBase
{
public:
    wxBitmapDataObject();
    wxBitmapDataObject(const wxBitmap& bitmap);

    void HaikuAddDataTo(BMessage& msg) const override;
    bool HaikuSetDataFrom(const BMessage& msg) override;

private:
    static bool BitmapToPngBytes(const wxBitmap& bmp, wxMemoryBuffer& out);
    static bool PngBytesToBitmap(const void* data, size_t len, wxBitmap& out);
};

// ----------------------------------------------------------------------------
// wxFileDataObject (Haiku): stores filenames as text/uri-list + text/plain
// ----------------------------------------------------------------------------
class WXDLLIMPEXP_CORE wxFileDataObject : public wxFileDataObjectBase
{
public:
    wxFileDataObject();
    void AddFile(const wxString &filename);
    void HaikuAddDataTo(BMessage& msg) const override;
    bool HaikuSetDataFrom(const BMessage& msg) override;
private :
   //wxArrayString files;
};

#endif // _WX_HAIKU_DATAOBJ2_H_
