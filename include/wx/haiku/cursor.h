#ifndef _WX_HAIKU_CURSOR_H_
#define _WX_HAIKU_CURSOR_H_

#include "wx/gdicmn.h"      // for wxCursorType, wxBitmapType
#include "wx/bitmap.h"      // for wxBitmap
//#include "wx/cursor.h"      // ensures wxCursorBase and wxCursorType are defined

class BCursor;
typedef BCursor* wxCursorHandle;

class WXDLLIMPEXP_CORE wxCursor : public wxCursorBase
{
public:
    wxCursor() = default;
    wxCursor(wxStockCursor id) { InitFromStock(id); }
    wxCursor(const wxBitmap& bitmap, const wxPoint& hotSpot)
        : wxCursor(bitmap, hotSpot.x, hotSpot.y) { }
    wxCursor(const wxBitmap& bitmap, int hotSpotX = 0, int hotSpotY = 0);
#if wxUSE_IMAGE
    wxCursor( const wxImage & image );
    wxCursor(const char* const* xpmData);
#endif // wxUSE_IMAGE
    wxCursor(const wxString& name, wxBitmapType type, const wxPoint& hotSpot)
        : wxCursor(name, type, hotSpot.x, hotSpot.y) { }
    wxCursor(const wxString& name,
             wxBitmapType type = wxCURSOR_DEFAULT_TYPE,
             int hotSpotX = 0, int hotSpotY = 0);

    virtual wxPoint GetHotSpot() const override;
    wxCursorHandle GetHandle() const;
    wxBitmap GetBitmap() const;

protected:
    void InitFromStock( wxStockCursor cursorId );
    void InitFromBitmap(const wxBitmap& bmp, int hotSpotX, int hotSpotY);
#if wxUSE_IMAGE
    void InitFromImage( const wxImage & image );
#endif

private:
    void Init();
    virtual wxGDIRefData *CreateGDIRefData() const override;
    wxNODISCARD virtual wxGDIRefData *CloneGDIRefData(const wxGDIRefData *data) const override;

    wxDECLARE_DYNAMIC_CLASS(wxCursor);
};

//WXDLLIMPEXP_CORE wxCursor wxHaikuCreateStockCursor(wxCursorType type);

#endif // _WX_HAIKU_CURSOR_H_
