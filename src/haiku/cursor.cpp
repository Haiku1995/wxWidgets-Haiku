/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/cursor.cpp
// Purpose:     wxCursor implementation for Haiku
// Author:      Haiku port adaptation
// Copyright:   (c) wxWidgets dev team
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#ifdef __BORLANDC__
    #pragma hdrstop
#endif

#ifndef WX_PRECOMP
    #include "wx/cursor.h"
    #include "wx/utils.h"
    #include "wx/bitmap.h"
    #include "wx/image.h"
    #include "wx/log.h"
#endif


#include "wx/haiku/converter.h"
#include <Cursor.h>
#include <Bitmap.h>
#include <Point.h>
#include <Application.h>
#include <set>
#include <mutex>

// ============================================================================
// Global cursor functions (match Qt signatures and order exactly)
// ============================================================================

static int gs_busyCount = 0;
static wxCursor gs_busyCursor;
static wxCursor gs_normalCursor;

static void ApplyCursor(const wxCursor& cursor)
{
    if (!be_app) return;
    if (cursor.GetHandle())
        be_app->SetCursor(cursor.GetHandle());
    else
    {
        BCursor arrow(B_CURSOR_ID_SYSTEM_DEFAULT);
        be_app->SetCursor(&arrow);
    }
}

void wxSetCursor(const wxCursorBundle& cursors)
{
    gs_normalCursor = cursors.GetCursorForMainWindow();
    if (!gs_busyCount) ApplyCursor(gs_normalCursor);
}

void wxBeginBusyCursor(const wxCursor* cursor)
{
    if (gs_busyCount++ != 0) return;
    gs_busyCursor = cursor && cursor->IsOk() ? *cursor : wxCursor(wxCURSOR_WAIT);
    ApplyCursor(gs_busyCursor);
}

bool wxIsBusy() { return gs_busyCount > 0; }

void wxEndBusyCursor()
{
    wxCHECK_RET(gs_busyCount > 0, "Unbalanced wxEndBusyCursor");
    if (--gs_busyCount != 0) return;
    ApplyCursor(gs_normalCursor);
    gs_busyCursor = wxNullCursor;
}

// ============================================================================
// wxCursorRefData
// ============================================================================

class wxCursorRefData;

namespace
{
std::set<wxCursorRefData*>& LiveCursorData()
{
    static auto* data = new std::set<wxCursorRefData*>;
    return *data;
}

std::recursive_mutex& CursorDataMutex()
{
    static auto* mutex = new std::recursive_mutex;
    return *mutex;
}
}

class wxCursorRefData : public wxGDIRefData
{
public:
    wxCursorRefData() : m_cursor(nullptr)
    {
        std::lock_guard<std::recursive_mutex> lock(CursorDataMutex());
        LiveCursorData().insert(this);
    }

    wxCursorRefData(const wxCursorRefData& data)
        : wxGDIRefData()
        , m_cursor(nullptr)
        , m_hotSpot(data.m_hotSpot)
#if wxUSE_IMAGE
        , m_image(data.m_image)
#endif
    {
        std::lock_guard<std::recursive_mutex> lock(CursorDataMutex());
        if (data.m_cursor)
            m_cursor = new BCursor(*data.m_cursor);
        LiveCursorData().insert(this);
    }

    explicit wxCursorRefData(BCursor* c) : m_cursor(c)
    {
        std::lock_guard<std::recursive_mutex> lock(CursorDataMutex());
        LiveCursorData().insert(this);
    }

    ~wxCursorRefData() override
    {
        std::lock_guard<std::recursive_mutex> lock(CursorDataMutex());
        LiveCursorData().erase(this);
        delete m_cursor;
    }

    BCursor* m_cursor;
    wxPoint m_hotSpot{0, 0};
#if wxUSE_IMAGE
    wxImage m_image;
#endif
};

void wxHaikuReleaseNativeCursors()
{
    std::lock_guard<std::recursive_mutex> lock(CursorDataMutex());

    for (wxCursorRefData* data : LiveCursorData())
    {
        delete data->m_cursor;
        data->m_cursor = nullptr;
    }
}

// ============================================================================
// wxCursor
// ============================================================================

//wxIMPLEMENT_DYNAMIC_CLASS(wxCursor, wxCursorBase);
wxIMPLEMENT_DYNAMIC_CLASS(wxCursor, wxGDIObject);
// ----------------------------------------------------------------------------
// Constructors
// ----------------------------------------------------------------------------

wxCursor::wxCursor(const wxBitmap& bitmap, int hotSpotX, int hotSpotY)
{
    InitFromBitmap(bitmap, hotSpotX, hotSpotY);
}

wxCursor::wxCursor(const wxString& cursor_file,
                   wxBitmapType type,
                   int hotSpotX, int hotSpotY)
{
#if wxUSE_IMAGE
    wxImage img;
    if (!img.LoadFile(cursor_file, type))
        return;

    if (!img.HasOption(wxIMAGE_OPTION_CUR_HOTSPOT_X))
        img.SetOption(wxIMAGE_OPTION_CUR_HOTSPOT_X, hotSpotX);
    if (!img.HasOption(wxIMAGE_OPTION_CUR_HOTSPOT_Y))
        img.SetOption(wxIMAGE_OPTION_CUR_HOTSPOT_Y, hotSpotY);

    InitFromImage(img);
#endif // wxUSE_IMAGE
}

#if wxUSE_IMAGE
wxCursor::wxCursor(const wxImage& img)
{
    InitFromImage(img);
}

wxCursor::wxCursor(const char* const* xpmData)
{
    InitFromImage(wxImage(xpmData));
}
#endif // wxUSE_IMAGE

// ----------------------------------------------------------------------------
// Accessors
// ----------------------------------------------------------------------------

wxPoint wxCursor::GetHotSpot() const
{
    return m_refData ? static_cast<wxCursorRefData*>(m_refData)->m_hotSpot : wxDefaultPosition;
}

BCursor* wxCursor::GetHandle() const
{
    // wxNullCursor is a valid wxCursor with no reference data.  Native callers
    // use a null handle to leave the window cursor unchanged.
    if (!m_refData)
        return nullptr;

    return static_cast<wxCursorRefData*>(m_refData)->m_cursor;
}

// ----------------------------------------------------------------------------
// Initialization helpers
// ----------------------------------------------------------------------------

void wxCursor::InitFromStock(wxStockCursor cursorId)
{
    AllocExclusive();

    if (cursorId == wxCURSOR_RIGHT_ARROW)
    {
        // Haiku has no stock reverse arrow; Scintilla uses it in the margin.
        static const unsigned char rightArrow[] = {
            0x10, 0x01, 0x00, 0x0f, 0x00, 0x01, 0x00, 0x03, 0x00, 0x05, 0x00, 0x09,
            0x00, 0x11, 0x00, 0x21, 0x00, 0x41, 0x00, 0x81, 0x01, 0xc1, 0x00, 0x25,
            0x00, 0x2b, 0x00, 0x51, 0x00, 0x60, 0x00, 0xa0, 0x00, 0xc0, 0x00, 0x00,
            0x00, 0x01, 0x00, 0x03, 0x00, 0x07, 0x00, 0x0f, 0x00, 0x1f, 0x00, 0x3f,
            0x00, 0x7f, 0x00, 0xff, 0x01, 0xff, 0x00, 0x3f, 0x00, 0x3b, 0x00, 0x71,
            0x00, 0x60, 0x00, 0xe0, 0x00, 0xc0, 0x00, 0x00,
        };
        auto* data = static_cast<wxCursorRefData*>(m_refData);
        delete data->m_cursor;
        data->m_cursor = new BCursor(rightArrow);
        data->m_hotSpot = wxPoint(15, 0);
        return;
    }

    BCursorID haikuCursor;
    switch (cursorId)
    {
        case wxCURSOR_NONE:
        case wxCURSOR_BLANK:
            haikuCursor = B_CURSOR_ID_NO_CURSOR;
            break;
        case wxCURSOR_DEFAULT:      // wxCURSOR_ARROW is same value - REMOVED duplicate
      //  case wxCURSOR_ARROW:
            haikuCursor = B_CURSOR_ID_SYSTEM_DEFAULT;
            break;
        case wxCURSOR_HAND:
            haikuCursor = B_CURSOR_ID_FOLLOW_LINK;
            break;
        // Drawing tools use the native precision cursor when no exact
        // Haiku stock artwork exists.
        case wxCURSOR_BULLSEYE:
        case wxCURSOR_PAINT_BRUSH:
        case wxCURSOR_PENCIL:
        case wxCURSOR_SPRAYCAN:
        case wxCURSOR_CROSS:
            haikuCursor = B_CURSOR_ID_CROSS_HAIR;
            break;
        case wxCURSOR_SIZEWE:
            haikuCursor = B_CURSOR_ID_RESIZE_EAST_WEST;
            break;
        case wxCURSOR_SIZENS:
            haikuCursor = B_CURSOR_ID_RESIZE_NORTH_SOUTH;
            break;
        case wxCURSOR_ARROWWAIT:
        case wxCURSOR_WAIT:
        case wxCURSOR_WATCH:
            haikuCursor = B_CURSOR_ID_PROGRESS;
            break;
        case wxCURSOR_SIZING:
            haikuCursor = B_CURSOR_ID_MOVE;
            break;
        case wxCURSOR_CHAR:
        case wxCURSOR_IBEAM:
            haikuCursor = B_CURSOR_ID_I_BEAM;
            break;
        case wxCURSOR_NO_ENTRY:
            haikuCursor = B_CURSOR_ID_NOT_ALLOWED;
            break;
        case wxCURSOR_SIZENWSE:
            haikuCursor = B_CURSOR_ID_RESIZE_NORTH_WEST_SOUTH_EAST;
            break;
        case wxCURSOR_SIZENESW:
            haikuCursor = B_CURSOR_ID_RESIZE_NORTH_EAST_SOUTH_WEST;
            break;
        case wxCURSOR_QUESTION_ARROW:
            haikuCursor = B_CURSOR_ID_HELP;
            break;
        case wxCURSOR_MAGNIFIER:
            haikuCursor = B_CURSOR_ID_ZOOM_IN;
            break;
        case wxCURSOR_POINT_LEFT:
            haikuCursor = B_CURSOR_ID_RESIZE_WEST;
            break;
        case wxCURSOR_POINT_RIGHT:
            haikuCursor = B_CURSOR_ID_RESIZE_EAST;
            break;
        case wxCURSOR_LEFT_BUTTON:
        case wxCURSOR_MIDDLE_BUTTON:
        case wxCURSOR_RIGHT_BUTTON:
            // Haiku has no mouse-button instruction stock cursors.
            haikuCursor = B_CURSOR_ID_SYSTEM_DEFAULT;
            break;
        default:
            wxFAIL_MSG(wxT("invalid stock cursor type"));
            haikuCursor = B_CURSOR_ID_SYSTEM_DEFAULT;
            break;
    }

    auto* data = static_cast<wxCursorRefData*>(m_refData);
    delete data->m_cursor;
    data->m_cursor = new BCursor(haikuCursor);
#if wxUSE_IMAGE
    data->m_image = wxImage();
#endif
    data->m_hotSpot = wxDefaultPosition;
}

wxBitmap wxCursor::GetBitmap() const
{
#if wxUSE_IMAGE
    auto* data = static_cast<wxCursorRefData*>(m_refData);
    if (data && data->m_image.IsOk()) return wxBitmap(data->m_image);
#endif
    // BCursor provides no public bitmap query for system stock cursors.
    return wxBitmap();
}

void wxCursor::InitFromBitmap(const wxBitmap& bmp, int hotSpotX, int hotSpotY)
{
    AllocExclusive();

#if wxUSE_IMAGE
    wxImage img = bmp.ConvertToImage();
    if (img.IsOk())
    {
        if (!img.HasOption(wxIMAGE_OPTION_CUR_HOTSPOT_X))
            img.SetOption(wxIMAGE_OPTION_CUR_HOTSPOT_X, hotSpotX);
        if (!img.HasOption(wxIMAGE_OPTION_CUR_HOTSPOT_Y))
            img.SetOption(wxIMAGE_OPTION_CUR_HOTSPOT_Y, hotSpotY);
        
        InitFromImage(img);
        return;
    }
#endif // wxUSE_IMAGE

    InitFromStock(wxCURSOR_DEFAULT);
}

#if wxUSE_IMAGE
void wxCursor::InitFromImage(const wxImage& image)
{
    AllocExclusive();

    if (!image.IsOk())
    {
        InitFromStock(wxCURSOR_DEFAULT);
        return;
    }

    // FIX: Convert wxImage â†’ wxBitmap first, then use WxBitmapToBBitmap
    wxBitmap bmp(image);
    BBitmap* bitmap = HaikuWxConverter::WxBitmapToBBitmap(bmp);
    
    if (bitmap)
    {
        int hotSpotX = image.HasOption(wxIMAGE_OPTION_CUR_HOTSPOT_X) 
                     ? image.GetOptionInt(wxIMAGE_OPTION_CUR_HOTSPOT_X) : 0;
        int hotSpotY = image.HasOption(wxIMAGE_OPTION_CUR_HOTSPOT_Y) 
                     ? image.GetOptionInt(wxIMAGE_OPTION_CUR_HOTSPOT_Y) : 0;
        
        BPoint hotSpot(hotSpotX, hotSpotY);
        auto* data = static_cast<wxCursorRefData*>(m_refData);
        delete data->m_cursor;
        data->m_cursor = new BCursor(bitmap, hotSpot);
        data->m_hotSpot = wxPoint(hotSpotX, hotSpotY);
        data->m_image = image.Copy();
        
        delete bitmap;
    }
    else
    {
        InitFromStock(wxCURSOR_DEFAULT);
    }
}
#endif // wxUSE_IMAGE

// ----------------------------------------------------------------------------
// RefData management
// ----------------------------------------------------------------------------

wxGDIRefData* wxCursor::CreateGDIRefData() const
{
    return new wxCursorRefData;
}

wxGDIRefData* wxCursor::CloneGDIRefData(const wxGDIRefData* data) const
{
    return new wxCursorRefData(*static_cast<const wxCursorRefData*>(data));
}
