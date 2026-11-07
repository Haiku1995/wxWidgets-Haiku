#include "wx/wxprec.h"

#if wxUSE_TASKBARICON

#include "wx/taskbar.h"
#include "wx/menu.h"
#include "wx/bitmap.h"
#include "wx/graphics.h"
#include "wx/dcbuffer.h"

#include <Bitmap.h>
#include <TranslationUtils.h>
#include <InterfaceDefs.h>
#include <Message.h>
#include <Deskbar.h>
#include <PopUpMenu.h>
#include <MenuItem.h>
#include <Window.h>
wxIMPLEMENT_DYNAMIC_CLASS(wxTaskBarIcon, wxEvtHandler);
// ============================================================================
// wxHaikuTaskBarReplicant Implementation
// ============================================================================

wxHaikuTaskBarReplicant::wxHaikuTaskBarReplicant(BRect frame, const char* name, const BBitmap* icon)
    : BView(frame, name, B_FOLLOW_NONE, B_WILL_DRAW | B_NAVIGABLE),
      m_icon(nullptr),
      m_wxHandler(nullptr)
{
    if (icon) {
        m_icon = new BBitmap(icon); // Clone the icon
    }
}

wxHaikuTaskBarReplicant::wxHaikuTaskBarReplicant(BMessage* archive)
    : BView(archive),
      m_icon(nullptr),
      m_wxHandler(nullptr)
{
    // Custom archiving not implemented for icon; Haiku handles view state
}

wxHaikuTaskBarReplicant::~wxHaikuTaskBarReplicant()
{
    delete m_icon;
}

BArchivable*
wxHaikuTaskBarReplicant::Instantiate(BMessage* data)
{
    if (validate_instantiation(data, "wxHaikuTaskBarReplicant"))
        return new wxHaikuTaskBarReplicant(data);
    return nullptr;
}

status_t
wxHaikuTaskBarReplicant::Archive(BMessage* data, bool deep) const
{
    status_t result = BView::Archive(data, deep);
    if (result == B_OK) {
        data->AddString("class", "wxHaikuTaskBarReplicant");
    }
    return result;
}

void
wxHaikuTaskBarReplicant::Draw(BRect /*updateRect*/)
{
    SetHighColor(ui_color(B_PANEL_BACKGROUND_COLOR));
    FillRect(Bounds());

    if (m_icon) {
        DrawBitmap(m_icon, BPoint(0, 0));
    } else {
        SetHighColor(0, 0, 0);
        StrokeRect(Bounds());
    }
}

void
wxHaikuTaskBarReplicant::MouseDown(BPoint point)
{
    BMessage* msg = Window()->CurrentMessage();
    if (!msg) return;

    int32 buttons = 0;
    if (msg->FindInt32("buttons", &buttons) != B_OK)
        return;

    if (buttons & B_PRIMARY_MOUSE_BUTTON) {
        if (m_wxHandler) {
            m_wxHandler->OnReplicantClicked();
        }
    } else if (buttons & B_SECONDARY_MOUSE_BUTTON) {
        if (m_wxHandler) {
            wxMenu* menu = m_wxHandler->GetCurrentMenu();
            if (menu) {
                BPoint screenPoint = point;
                ConvertToScreen(&screenPoint);
                m_wxHandler->ShowPopupMenu(screenPoint, menu);
            }
        }
    }
}

// ============================================================================
// wxTaskBarIcon Implementation
// ============================================================================

bool wxTaskBarIconBase::IsAvailable()
{
    BDeskbar deskbar;
    return deskbar.IsRunning();
}



wxTaskBarIcon::wxTaskBarIcon(wxTaskBarIconType WXUNUSED(iconType))
    : m_deskbar(new BDeskbar()),
      m_replicantId(-1),
      m_replicantView(nullptr),
      m_currentMenu(nullptr)
{
}

wxTaskBarIcon::~wxTaskBarIcon()
{
    RemoveIcon();
    delete m_deskbar;
    // Do NOT delete m_replicantView — owned by Deskbar after AddItem()
}


// Helper: convert wxBitmap to BBitmap (16x16 RGBA)
static BBitmap* wxBitmapToBBitmap(const wxBitmap& wxBmp)
{
    if (!wxBmp.IsOk())
        return nullptr;

    // Step 1: Get a valid 16x16 bitmap (scale if needed)
    wxBitmap bmp16;
    if (wxBmp.GetWidth() == 16 && wxBmp.GetHeight() == 16 &&
        wxBmp.GetScaleFactor() == 1.0) // already perfect
    {
        bmp16 = wxBmp;
    }
    else
    {
        // Convert to image and rescale to exactly 16x16 (high quality)
        wxImage img = wxBmp.ConvertToImage();
        if (!img.IsOk()) return nullptr;

        img.Rescale(16, 16, wxIMAGE_QUALITY_HIGH);
        bmp16 = wxBitmap(img);
    }

    if (!bmp16.IsOk() || bmp16.GetWidth() != 16 || bmp16.GetHeight() != 16)
        return nullptr;

    // Step 2: Create Haiku BBitmap (B_RGBA32 is endian-safe on Haiku)
    BRect rect(0, 0, 15, 15);
    BBitmap* hbmp = new BBitmap(rect, B_RGBA32);
    if (!hbmp || !hbmp->IsValid())
    {
        delete hbmp;
        return nullptr;
    }

    // Step 3: Copy pixel data (wxImage is always RGBA with separate alpha)
    wxImage img = bmp16.ConvertToImage();  // guaranteed 16x16 now
    const unsigned char* src = img.GetData();
    const unsigned char* alpha = img.HasAlpha() ? img.GetAlpha() : nullptr;

    uint32* dst = (uint32*)hbmp->Bits();
    int32 bpr = hbmp->BytesPerRow() / 4;

    for (int y = 0; y < 16; ++y)
    {
        for (int x = 0; x < 16; ++x)
        {
            int idx = (y * 16 + x) * 3;
            unsigned char r = src[idx + 0];
            unsigned char g = src[idx + 1];
            unsigned char b = src[idx + 2];
            unsigned char a = alpha ? alpha[y * 16 + x] : 255;

            // Haiku B_RGBA32: ARGB32 (big-endian) or ABGR32 (little-endian) — but B_RGBA32 handles it
            dst[y * bpr + x] = (uint32(a) << 24) |
                               (uint32(r) << 16) |
                               (uint32(g) <<  8) |
                               uint32(b);
        }
    }

    return hbmp;
}

bool wxTaskBarIcon::SetIcon(const wxBitmapBundle& icon, const wxString& tooltip)
{
    (void)tooltip; // TODO: Tooltip not supported in Deskbar replicants

    wxBitmap wxBmp = icon.GetBitmap(wxDefaultSize); // or GetBitmapForSize(16,16)
    if (!wxBmp.IsOk()) {
        wxBmp = wxBitmap(16, 16);
        wxMemoryDC dc(wxBmp);
        dc.SetBrush(*wxBLUE_BRUSH);
        dc.DrawRectangle(0, 0, 16, 16);
        dc.SelectObject(wxNullBitmap);
    }

    BBitmap* haikuIcon = wxBitmapToBBitmap(wxBmp);
    if (!haikuIcon) {
        return false;
    }

    BRect frame(0, 0, 15, 15);
    m_replicantView = new wxHaikuTaskBarReplicant(frame, "wx_taskbar_icon", haikuIcon);
    m_replicantView->SetWxHandler(this);

    status_t result = m_deskbar->AddItem(m_replicantView, &m_replicantId);
    if (result != B_OK) {
        delete m_replicantView;
        m_replicantView = nullptr;
        m_replicantId = -1;
        delete haikuIcon;
        return false;
    }

    // Haiku now owns haikuIcon and m_replicantView — do not delete manually
    return true;
}

bool wxTaskBarIcon::RemoveIcon()
{
    if (m_replicantId < 0)
        return true;

    status_t result = m_deskbar->RemoveItem(m_replicantId);
    if (result == B_OK) {
        m_replicantId = -1;
        m_replicantView = nullptr; // now owned/deleted by Deskbar
        return true;
    }
    return false;
}

void wxTaskBarIcon::ShowPopupMenu(BPoint screenPoint, wxMenu* menu)
{
    if (!menu) return;

    BPopUpMenu* haikuMenu = new BPopUpMenu("popup", false, false);

    // Simple conversion: only top-level items, no submenus or icons
    size_t count = menu->GetMenuItemCount();
    for (size_t i = 0; i < count; ++i) {
        wxMenuItem* item = menu->GetMenuItems()[i];
        if (!item->IsSeparator()) {
            BMenuItem* hItem = new BMenuItem(
                wxString(item->GetItemLabel()).utf8_str(),
                new BMessage('menu')
            );
            // You'd normally map ID to message, but for demo:
            hItem->SetEnabled(item->IsEnabled());
            haikuMenu->AddItem(hItem);
        } else {
            haikuMenu->AddSeparatorItem();
        }
    }

    // Show and auto-delete
    haikuMenu->Go(screenPoint, true, true, BRect(), true);
    // Note: Full wx event integration would require mapping IDs → wx events
}

bool wxTaskBarIcon::PopupMenu(wxMenu* menu)
{
    if (!menu) return false;

    // Store for MouseDown handler
    m_currentMenu = menu;

    // Trigger via right-click only — no programmatic screen position
    // So this method is effectively a no-op unless called from replicant
    return true;
}

void wxTaskBarIcon::OnReplicantClicked()
{
    wxTaskBarIconEvent event(wxEVT_TASKBAR_LEFT_DOWN, this);
    event.SetEventObject(this);
    ProcessEvent(event);
}

#endif // wxUSE_TASKBARICON
