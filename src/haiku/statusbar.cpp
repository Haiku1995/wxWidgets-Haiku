#include "wx/wxprec.h"

#if wxUSE_STATUSBAR

// ----------------------------------------------------------------------------
// Haiku Debug Logging Macro
// ----------------------------------------------------------------------------
#if defined(WX_DEBUG_HAIKU) && WX_DEBUG_HAIKU
    #define HAIKU_LOG(msg, ...) \
        wxLogDebug("[HAIKU-STATUSBAR:%s:%d] " msg, __func__, __LINE__, ##__VA_ARGS__)
    #define HAIKU_LOG_RAW(msg, ...) \
        do { printf("[HAIKU-STATUSBAR:%s:%d] " msg "\n", __func__, __LINE__, ##__VA_ARGS__); fflush(stdout); } while(0)
#else
    #define HAIKU_LOG(msg, ...) ((void)0)
    #define HAIKU_LOG_RAW(msg, ...) ((void)0)
#endif

#include "wx/statusbr.h"
#include "wx/haiku/private.h"

#include "wx/haiku/private/utils.h"
#include <View.h>
#include <StringView.h>
#include <Rect.h>
#include <InterfaceDefs.h>
#include <InterfaceKit.h>

// ----------------------------------------------------------------------------
// Helper: Get native parent view
// ----------------------------------------------------------------------------
static BView* HaikuGetNativeParentView(wxWindow* parent)
{
    if (!parent)
        return nullptr;

    if (wxWindowHaiku* wh = wxDynamicCast(parent, wxWindowHaiku))
        return wh->GetHaikuParentViewForChildren();

#if defined(__WXHAIKU__)
    if (wxTopLevelWindowHaiku* tlw = wxDynamicCast(parent, wxTopLevelWindowHaiku))
        return tlw->HaikuGetParentView();
#endif

    return nullptr;
}

// ----------------------------------------------------------------------------
// wxHaikuStatusBarView
// ----------------------------------------------------------------------------
class wxHaikuStatusBarView : public BView
{
public:
    wxHaikuStatusBarView(BRect frame, wxStatusBar* owner)
        : BView(frame, "wx_status_bar", B_FOLLOW_LEFT_RIGHT | B_FOLLOW_BOTTOM, B_WILL_DRAW)
        , m_owner(owner)
    {
        HAIKU_LOG_RAW("wxHaikuStatusBarView ctor: frame=(%.1f,%.1f,%.1f,%.1f)",
                      frame.left, frame.top, frame.right, frame.bottom);
        SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
    }

    void Draw(BRect updateRect) override
    {
        HAIKU_LOG("Draw: updateRect=(%.1f,%.1f,%.1f,%.1f)",
                  updateRect.left, updateRect.top, updateRect.right, updateRect.bottom);
        SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR), B_DARKEN_1_TINT));
        StrokeLine(BPoint(0, 0), BPoint(Bounds().Width(), 0));

        if (!m_owner)
            return;

        font_height metrics;
        GetFontHeight(&metrics);
        const float baseline = Bounds().Height() - metrics.descent - 3.0f;
        SetHighColor(ui_color(B_PANEL_TEXT_COLOR));
        DrawString(m_owner->GetStatusText(0).utf8_str(), BPoint(4.0f, baseline));
    }

    void FrameResized(float width, float height) override
    {
        HAIKU_LOG("FrameResized: %.1fx%.1f", width, height);
        BView::FrameResized(width, height);
    }

private:
    wxStatusBar* m_owner;
};

// ----------------------------------------------------------------------------
// wxStatusBar implementation
// ----------------------------------------------------------------------------
wxStatusBar::wxStatusBar(wxWindow *parent, wxWindowID winid,
                         long style, const wxString& name)
{
    HAIKU_LOG_RAW("wxStatusBar ctor: parent=%p id=%d", parent, (int)winid);
    Create(parent, winid, style, name);
}

bool wxStatusBar::Create(wxWindow *parent, wxWindowID id,
                         long style, const wxString& name)
{
    HAIKU_LOG_RAW("wxStatusBar::Create ENTRY: parent=%p id=%d", parent, (int)id);
    
    if (!wxStatusBarBase::Create(parent, id, wxDefaultPosition, wxDefaultSize,
                                 style, wxDefaultValidator, name))
    {
        HAIKU_LOG_RAW("Create FAILED: wxStatusBarBase::Create returned false");
        return false;
    }
    HAIKU_LOG("wxStatusBarBase::Create succeeded");

    // Create native view
    BRect frame(0, 0, 400, 20);
    BView* view = new wxHaikuStatusBarView(frame, this);
    HAIKU_LOG("Native view created: %p", view);

    // Base creation attaches a generic placeholder to the frame's client.
    // Replacing only m_haikuView leaves that full-client-sized view alive: it
    // wins native hit testing and absorbs clicks intended for client controls.
    BView* placeholder = m_haikuView;
    BWindow* placeholderWindow = placeholder ? placeholder->Window() : nullptr;
    if (placeholderWindow && !placeholderWindow->LockLooper())
    {
        delete view;
        return false;
    }
    if (placeholder)
    {
        HaikuStoreWindowPointer(placeholder, nullptr);
        if (placeholder->Parent() || placeholder->Window())
            placeholder->RemoveSelf();
    }
    // Also replace the drawing target, which otherwise points at the removed
    // placeholder. wxFrame::SetStatusBar() attaches the new native view.
    HaikuAdoptView(view);
    delete placeholder;
    if (placeholderWindow)
        placeholderWindow->UnlockLooper();
    HaikuStoreWindowPointer(view, this);
    HAIKU_LOG("Stored native view: m_haikuView=%p", m_haikuView);

    // ❌ REMOVE: Do NOT attach to parent here!
    // The attachment happens in wxFrame::SetStatusBar()
    /*
    if (parent)
    {
        BView* parentView = HaikuGetNativeParentView(parent);
        if (parentView)
        {
            BWindow* win = parentView->Window();
            if (win && win->LockLooper())
            {
                win->AddChild(view);  // ← REMOVE THIS!
                win->UnlockLooper();
            }
        }
    }
    */
    HAIKU_LOG("Native view created (will be attached by SetStatusBar)");

    SetFieldsCount(1);
    HAIKU_LOG("SetFieldsCount(1)");

    HAIKU_LOG_RAW("wxStatusBar::Create EXIT: returning true");
    return true;
}

void wxStatusBar::SetStatusWidths(int number, const int widths[])
{
    HAIKU_LOG("SetStatusWidths: number=%d", number);
    if (number != (int)m_panes.GetCount())
        return;
    wxStatusBarBase::SetStatusWidths(number, widths);
}

bool wxStatusBar::GetFieldRect(int i, wxRect& rect) const
{
    HAIKU_LOG("GetFieldRect: i=%d", i);
    wxCHECK_MSG((i >= 0) && ((size_t)i < m_panes.GetCount()), false,
                "invalid statusbar field index");
    const_cast<wxStatusBar*>(this)->CreateFieldsIfNeeded();
    rect = wxRect(0, 0, 100, 20);
    return true;
}

void wxStatusBar::SetMinHeight(int height)
{
    HAIKU_LOG("SetMinHeight: %d", height);
    BView* view = GetBView();
    if (view && view->LockLooper())
    {
        BRect frame = view->Frame();
        frame.bottom = frame.top + height - 1;
        view->ResizeTo(frame.Width(), frame.Height());
        view->UnlockLooper();
    }
}

int wxStatusBar::GetBorderX() const { return 2; }
int wxStatusBar::GetBorderY() const { return 2; }

void wxStatusBar::DoUpdateStatusText(int number)
{
    HAIKU_LOG("DoUpdateStatusText: number=%d", number);
    CreateFieldsIfNeeded();
    if (number != 0)
        return;

    if (BView* view = GetBView())
    {
        if (BWindow* window = view->Window())
        {
            if (window->Lock())
            {
                view->Invalidate();
                window->Unlock();
            }
        }
        else
        {
            view->Invalidate();
        }
    }
}

void wxStatusBar::CreateFieldsIfNeeded()
{
    HAIKU_LOG("CreateFieldsIfNeeded");
}

// ✅ NO destructor needed - compiler generates default

#endif // wxUSE_STATUSBAR
