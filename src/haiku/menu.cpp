/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/menu.cpp
// Purpose:     wxMenu and wxMenuBar implementation for Haiku
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#if wxUSE_MENUS

#include "wx/haiku/private.h"
#include "wx/menu.h"
#include "wx/frame.h"
#include "wx/log.h"

#include <Menu.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <Window.h>
#include <View.h>

#if defined(WX_DEBUG_HAIKU) && WX_DEBUG_HAIKU
    #define HAIKU_LOG(msg, ...) \
        wxLogDebug("[HAIKU-MENU:%s:%d] " msg, __func__, __LINE__, ##__VA_ARGS__)
#else
    #define HAIKU_LOG(msg, ...) ((void)0)
#endif

// Submenus may have no Window() until opened. Lock the attached ancestor.
class HaikuMenuLock
{
public:
    explicit HaikuMenuLock(BMenu* menu) : m_window(nullptr), m_locked(true)
    {
        for (BMenu* current = menu; current; current = current->Supermenu())
        {
            if ((m_window = current->Window()))
            {
                m_locked = m_window->LockLooper();
                break;
            }
        }
    }
    ~HaikuMenuLock() { if (m_window && m_locked) m_window->UnlockLooper(); }
    bool IsLocked() const { return m_locked; }
private:
    BWindow* m_window;
    bool m_locked;
};

static void HaikuDumpMenu(const char* label, BMenu* menu)
{
#if defined(WX_DEBUG_HAIKU) && WX_DEBUG_HAIKU
    if (!menu)
    {
        wxLogDebug("%s: menu=(nil)", label);
        return;
    }

    wxLogDebug("%s: ptr=%p title='%s' superitem=%p window=%p items=%ld radio=%d",
               label,
               menu,
               menu->Name() ? menu->Name() : "",
               menu->Superitem(),
               menu->Window(),
               (long)menu->CountItems(),
               menu->IsRadioMode());

    for (int32 i = 0; i < menu->CountItems(); ++i)
    {
        BMenuItem* item = menu->ItemAt(i);
        wxLogDebug("  [%ld] item=%p label='%s' submenu=%p enabled=%d marked=%d message=%p",
                   (long)i,
                   item,
                   item && item->Label() ? item->Label() : "",
                   item ? item->Submenu() : nullptr,
                   item ? item->IsEnabled() : 0,
                   item ? item->IsMarked() : 0,
                   item ? item->Message() : nullptr);
    }
#else
    wxUnusedVar(label);
    wxUnusedVar(menu);
#endif
}

static void HaikuDumpMenuBar(const char* label, BMenuBar* bar)
{
#if defined(WX_DEBUG_HAIKU) && WX_DEBUG_HAIKU
    if (!bar)
    {
        wxLogDebug("%s: menubar=(nil)", label);
        return;
    }

    const BRect f = bar->Frame();

    wxLogDebug("%s: ptr=%p name='%s' hidden=%d parent=%p window=%p count=%ld "
               "frame=(%.1f,%.1f,%.1f,%.1f) size=(%.1f x %.1f)",
               label,
               bar,
               bar->Name() ? bar->Name() : "",
               bar->IsHidden(),
               bar->Parent(),
               bar->Window(),
               (long)bar->CountItems(),
               f.left, f.top, f.right, f.bottom,
               f.Width() + 1.0f, f.Height() + 1.0f);

    for (int32 i = 0; i < bar->CountItems(); ++i)
    {
        BMenuItem* item = bar->ItemAt(i);
        wxLogDebug("  [%ld] item=%p label='%s' submenu=%p enabled=%d marked=%d",
                   (long)i,
                   item,
                   item && item->Label() ? item->Label() : "",
                   item ? item->Submenu() : nullptr,
                   item ? item->IsEnabled() : 0,
                   item ? item->IsMarked() : 0);
    }
#else
    wxUnusedVar(label);
    wxUnusedVar(bar);
#endif
}

static void HaikuDumpWindow(const char* label, BWindow* win)
{
#if defined(WX_DEBUG_HAIKU) && WX_DEBUG_HAIKU
    if (!win)
    {
        wxLogDebug("%s: window=(nil)", label);
        return;
    }

    const BRect f = win->Frame();
    const BRect b = win->Bounds();

    wxLogDebug("%s: ptr=%p title='%s' hidden=%d active=%d childCount=%ld "
               "frame=(%.1f,%.1f,%.1f,%.1f) size=(%.1f x %.1f) "
               "bounds=(%.1f,%.1f,%.1f,%.1f) boundsSize=(%.1f x %.1f)",
               label,
               win,
               win->Title() ? win->Title() : "",
               win->IsHidden(),
               win->IsActive(),
               (long)win->CountChildren(),
               f.left, f.top, f.right, f.bottom,
               f.Width() + 1.0f, f.Height() + 1.0f,
               b.left, b.top, b.right, b.bottom,
               b.Width() + 1.0f, b.Height() + 1.0f);
#else
    wxUnusedVar(label);
    wxUnusedVar(win);
#endif
}

static void HaikuSizeMenuBarToPreferred(BMenuBar* bar, float winWidth)
{
    HAIKU_LOG("SizeMenuBarToPreferred ENTER bar=%p winWidth=%.1f", bar, winWidth);

    if (!bar)
        return;

    bar->ResizeToPreferred();

    float prefW = 0.0f, prefH = 0.0f;
    bar->GetPreferredSize(&prefW, &prefH);

    if (prefH < 16.0f)
        prefH = 20.0f;

    HAIKU_LOG("SizeMenuBarToPreferred preferred=(%.1f x %.1f)", prefW, prefH);

    bar->MoveTo(0.0f, 0.0f);
    bar->ResizeTo(winWidth, prefH);
    bar->Invalidate();

    HaikuDumpMenuBar("SizeMenuBarToPreferred after", bar);
}

// ----------------------------------------------------------------------------
// wxMenu
// ----------------------------------------------------------------------------

wxMenu::wxMenu(long style)
    : wxMenuBase(style),
      m_bMenu(new BMenu(""))
{
    HAIKU_LOG("wxMenu ctor this=%p style=0x%lx native=%p",
              this, (unsigned long)style, m_bMenu);
    HaikuDumpMenu("wxMenu ctor", m_bMenu);
    (void)style;
}

wxMenu::wxMenu(const wxString& title, long style)
    : wxMenuBase(title, style),
      m_bMenu(new BMenu(wxMenuItem::GetLabelText(title).utf8_str()))
{
    HAIKU_LOG("wxMenu titled ctor this=%p title='%s' style=0x%lx native=%p",
              this, title, (unsigned long)style, m_bMenu);
    HaikuDumpMenu("wxMenu titled ctor", m_bMenu);
    (void)style;
}

wxMenu::~wxMenu()
{
    HAIKU_LOG("~wxMenu this=%p native=%p superitem=%p window=%p",
              this,
              m_bMenu,
              m_bMenu ? m_bMenu->Superitem() : nullptr,
              m_bMenu ? m_bMenu->Window() : nullptr);

    HaikuDumpMenu("~wxMenu native before delete", m_bMenu);

    // Destroy wx metadata while its native items and submenu handles are alive.
    wxClearList(m_items);

    // Only delete unattached menus. If the menu belongs to a menubar,
    // Haiku ownership is handled through the menubar/window side.
    if (m_bMenu && !m_bMenu->Superitem())
        delete m_bMenu;
}

wxMenuItem* wxMenu::DoAppend(wxMenuItem* item)
{
    HaikuMenuLock lock(m_bMenu);
    if (!lock.IsLocked())
        return nullptr;

    HAIKU_LOG("DoAppend ENTER this=%p item=%p nativeMenu=%p nativeItem=%p",
              this, item, m_bMenu, item ? item->GetBMenuItem() : nullptr);

    if (!wxMenuBase::DoAppend(item))
    {
        HAIKU_LOG("DoAppend base returned false");
        return nullptr;
    }

    if (BMenuItem* bitem = item->GetBMenuItem())
        m_bMenu->AddItem(bitem);

    HaikuDumpMenu("DoAppend after", m_bMenu);
    return item;
}

wxMenuItem* wxMenu::DoInsert(size_t pos, wxMenuItem* item)
{
    HaikuMenuLock lock(m_bMenu);
    if (!lock.IsLocked())
        return nullptr;

    HAIKU_LOG("DoInsert ENTER this=%p pos=%lu item=%p nativeItem=%p",
              this, (unsigned long)pos, item, item ? item->GetBMenuItem() : nullptr);

    if (!wxMenuBase::DoInsert(pos, item))
    {
        HAIKU_LOG("DoInsert base returned false");
        return nullptr;
    }

    if (BMenuItem* bitem = item->GetBMenuItem())
        m_bMenu->AddItem(bitem, (int32)pos);

    HaikuDumpMenu("DoInsert after", m_bMenu);
    return item;
}

wxMenuItem* wxMenu::DoRemove(wxMenuItem* item)
{
    HaikuMenuLock lock(m_bMenu);
    if (!lock.IsLocked())
        return nullptr;

    HAIKU_LOG("DoRemove ENTER this=%p item=%p nativeItem=%p",
              this, item, item ? item->GetBMenuItem() : nullptr);

    if (!wxMenuBase::DoRemove(item))
    {
        HAIKU_LOG("DoRemove base returned false");
        return nullptr;
    }

    if (BMenuItem* bitem = item->GetBMenuItem())
        m_bMenu->RemoveItem(bitem);

    HaikuDumpMenu("DoRemove after", m_bMenu);
    return item;
}

void wxMenu::SetTitle(const wxString& title)
{
    HaikuMenuLock lock(m_bMenu);
    if (!lock.IsLocked())
        return;

    HAIKU_LOG("SetTitle ENTER this=%p title='%s' native=%p",
              this, title, m_bMenu);

    wxMenuBase::SetTitle(title);

    const wxString plain = wxMenuItem::GetLabelText(title);
    HAIKU_LOG("SetTitle plain='%s' superitem=%p", plain, m_bMenu->Superitem());

    if (m_bMenu->Superitem())
        m_bMenu->Superitem()->SetLabel(plain.utf8_str());
    else
        m_bMenu->SetName(plain.utf8_str());

    HaikuDumpMenu("SetTitle after", m_bMenu);
}

wxString wxMenu::GetTitle() const
{
    HAIKU_LOG("GetTitle this=%p native=%p superitem=%p",
              this, m_bMenu, m_bMenu ? m_bMenu->Superitem() : nullptr);

    if (m_bMenu->Superitem())
        return wxString::FromUTF8(m_bMenu->Superitem()->Label());

    return wxString::FromUTF8(m_bMenu->Name());
}

BMenu* wxMenu::GetBMenu() const
{
    return m_bMenu;
}

// ----------------------------------------------------------------------------
// wxMenuBar
// ----------------------------------------------------------------------------

wxMenuBar::wxMenuBar()
    : m_bMenuBar(new BMenuBar(BRect(0, 0, 10, 10),
                              "menubar",
                              B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP,
                              B_ITEMS_IN_ROW))
{
    HAIKU_LOG("wxMenuBar ctor this=%p native=%p", this, m_bMenuBar);
    wxMenuBarBase::Create(nullptr, wxID_ANY);
    HaikuDumpMenuBar("wxMenuBar ctor", m_bMenuBar);
}

wxMenuBar::wxMenuBar(long style)
    : m_bMenuBar(new BMenuBar(BRect(0, 0, 10, 10),
                              "menubar",
                              B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP,
                              B_ITEMS_IN_ROW))
{
    HAIKU_LOG("wxMenuBar styled ctor this=%p style=0x%lx native=%p",
              this, (unsigned long)style, m_bMenuBar);
    wxMenuBarBase::Create(nullptr, wxID_ANY, wxDefaultPosition, wxDefaultSize, style);
    HaikuDumpMenuBar("wxMenuBar styled ctor", m_bMenuBar);
}

wxMenuBar::wxMenuBar(size_t count, wxMenu* menus[], const wxString titles[], long style)
    : m_bMenuBar(new BMenuBar(BRect(0, 0, 10, 10),
                              "menubar",
                              B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP,
                              B_ITEMS_IN_ROW))
{
    HAIKU_LOG("wxMenuBar array ctor this=%p count=%lu style=0x%lx native=%p",
              this, (unsigned long)count, (unsigned long)style, m_bMenuBar);

    wxMenuBarBase::Create(nullptr, wxID_ANY, wxDefaultPosition, wxDefaultSize, style);

    for (size_t i = 0; i < count; ++i)
        Append(menus[i], titles[i]);

    HaikuDumpMenuBar("wxMenuBar array ctor after append", m_bMenuBar);
}

wxMenuBar::~wxMenuBar()
{
    HAIKU_LOG("~wxMenuBar this=%p native=%p window=%p parent=%p",
              this,
              m_bMenuBar,
              m_bMenuBar ? m_bMenuBar->Window() : nullptr,
              m_bMenuBar ? m_bMenuBar->Parent() : nullptr);

    HaikuDumpMenuBar("~wxMenuBar native before delete", m_bMenuBar);

    wxClearList(m_menus);

    if (m_bMenuBar && !m_bMenuBar->Window())
        delete m_bMenuBar;
}

bool wxMenuBar::Append(wxMenu* menu, const wxString& title)
{
    HaikuMenuLock lock(m_bMenuBar);
    if (!lock.IsLocked())
        return false;

    HAIKU_LOG("Append ENTER this=%p menu=%p title='%s' nativeBar=%p",
              this, menu, title, m_bMenuBar);

    if (!wxMenuBarBase::Append(menu, title))
    {
        HAIKU_LOG("Append base returned false");
        return false;
    }

    menu->SetTitle(title);

    BMenu* bmenu = menu->GetBMenu();
    HAIKU_LOG("Append native BMenu=%p", bmenu);
    HaikuDumpMenu("Append menu before AddItem", bmenu);

    m_bMenuBar->AddItem(bmenu);

    // Haiku: derived menus such as KiCad ACTION_MENU override
    // SetTitle() without updating the native BMenu name.
    // Explicitly set the top-level BMenuItem label.
    if (BMenuItem* nativeItem = bmenu->Superitem())
    {
        const wxString plain = wxMenuItem::GetLabelText(title);
        nativeItem->SetLabel(plain.utf8_str());
    }

    HaikuDumpMenuBar("Append after AddItem", m_bMenuBar);

    if (m_bMenuBar->Window())
    {
        BWindow* win = m_bMenuBar->Window();
        HaikuDumpWindow("Append window before resize", win);

        if (win->LockLooper())
        {
            HAIKU_LOG("Append menubar already attached to window title='%s'",
                      win->Title() ? win->Title() : "");
            HAIKU_LOG("Append resizing menubar to window width=%.1f",
                      win->Bounds().Width());

            HaikuSizeMenuBarToPreferred(m_bMenuBar, win->Bounds().Width());

            BRect f = m_bMenuBar->Frame();
            HAIKU_LOG("Append new frame=(%.1f,%.1f,%.1f,%.1f)",
                      f.left, f.top, f.right, f.bottom);

            win->UnlockLooper();
        }
    }

    HAIKU_LOG("Append EXIT count=%ld", (long)m_bMenuBar->CountItems());
    return true;
}

bool wxMenuBar::Insert(size_t pos, wxMenu* menu, const wxString& title)
{
    HaikuMenuLock lock(m_bMenuBar);
    if (!lock.IsLocked())
        return false;

    HAIKU_LOG("Insert ENTER this=%p pos=%lu menu=%p title='%s' nativeBar=%p",
              this, (unsigned long)pos, menu, title, m_bMenuBar);

    if (!wxMenuBarBase::Insert(pos, menu, title))
    {
        HAIKU_LOG("Insert base returned false");
        return false;
    }

    menu->SetTitle(title);
    BMenu* bmenu = menu->GetBMenu();
    m_bMenuBar->AddItem(bmenu, (int32)pos);

    // Keep native top-level labels correct for derived wxMenu classes.
    if (BMenuItem* nativeItem = bmenu->Superitem())
    {
        const wxString plain = wxMenuItem::GetLabelText(title);
        nativeItem->SetLabel(plain.utf8_str());
    }

    HaikuDumpMenuBar("Insert after AddItem", m_bMenuBar);

    if (m_bMenuBar->Window())
    {
        BWindow* win = m_bMenuBar->Window();
        if (win->LockLooper())
        {
            HaikuSizeMenuBarToPreferred(m_bMenuBar, win->Bounds().Width());
            win->UnlockLooper();
        }
    }

    HAIKU_LOG("Insert EXIT count=%ld", (long)m_bMenuBar->CountItems());
    return true;
}

wxMenu* wxMenuBar::Remove(size_t pos)
{
    HaikuMenuLock lock(m_bMenuBar);
    if (!lock.IsLocked())
        return nullptr;

    HAIKU_LOG("Remove ENTER this=%p pos=%lu nativeBar=%p",
              this, (unsigned long)pos, m_bMenuBar);

    wxMenu* menu = wxMenuBarBase::Remove(pos);
    if (!menu)
    {
        HAIKU_LOG("Remove base returned nullptr");
        return nullptr;
    }

    HAIKU_LOG("Remove native menu=%p", menu->GetBMenu());
    m_bMenuBar->RemoveItem(menu->GetBMenu());

    HaikuDumpMenuBar("Remove after RemoveItem", m_bMenuBar);

    if (m_bMenuBar->Window())
    {
        BWindow* win = m_bMenuBar->Window();
        if (win->LockLooper())
        {
            HaikuSizeMenuBarToPreferred(m_bMenuBar, win->Bounds().Width());
            win->UnlockLooper();
        }
    }

    return menu;
}

void wxMenuBar::EnableTop(size_t pos, bool enable)
{
    HaikuMenuLock lock(m_bMenuBar);
    if (!lock.IsLocked())
        return;

    HAIKU_LOG("EnableTop this=%p pos=%lu enable=%d",
              this, (unsigned long)pos, enable);

    if (BMenuItem* item = m_bMenuBar->ItemAt((int32)pos))
    {
        item->SetEnabled(enable);
        HAIKU_LOG("EnableTop item=%p label='%s' nowEnabled=%d",
                  item,
                  item->Label() ? item->Label() : "",
                  item->IsEnabled());
    }

    HaikuDumpMenuBar("EnableTop after", m_bMenuBar);
}

bool wxMenuBar::IsEnabledTop(size_t pos) const
{
    HAIKU_LOG("IsEnabledTop this=%p pos=%lu", this, (unsigned long)pos);

    if (BMenuItem* item = m_bMenuBar->ItemAt((int32)pos))
        return item->IsEnabled();

    return false;
}

void wxMenuBar::SetMenuLabel(size_t pos, const wxString& label)
{
    HaikuMenuLock lock(m_bMenuBar);
    if (!lock.IsLocked())
        return;

    HAIKU_LOG("SetMenuLabel this=%p pos=%lu label='%s'",
              this, (unsigned long)pos, label);

    if (BMenuItem* item = m_bMenuBar->ItemAt((int32)pos))
    {
        const wxString plain = wxMenuItem::GetLabelText(label);
        item->SetLabel(plain.utf8_str());
        if (BWindow* window = m_bMenuBar->Window())
            HaikuSizeMenuBarToPreferred(m_bMenuBar, window->Bounds().Width());
        HAIKU_LOG("SetMenuLabel item=%p nowLabel='%s'",
                  item, item->Label() ? item->Label() : "");
    }

    HaikuDumpMenuBar("SetMenuLabel after", m_bMenuBar);
}

wxString wxMenuBar::GetMenuLabel(size_t pos) const
{
    HAIKU_LOG("GetMenuLabel this=%p pos=%lu", this, (unsigned long)pos);

    if (BMenuItem* item = m_bMenuBar->ItemAt((int32)pos))
        return wxString::FromUTF8(item->Label());

    return wxString();
}

void wxMenuBar::Attach(wxFrame* frame)
{
    HAIKU_LOG("Attach ENTER this=%p frame=%p nativeBar=%p",
              this, frame, m_bMenuBar);

    wxCHECK_RET(frame, "wxMenuBar::Attach requires a frame");
    wxCHECK_RET(m_bMenuBar, "Invalid Haiku menu bar");

    HaikuDumpMenuBar("Attach before base", m_bMenuBar);

    wxMenuBarBase::Attach(frame);

    HaikuDumpMenuBar("Attach after base", m_bMenuBar);
}

void wxMenuBar::Detach()
{
    HAIKU_LOG("Detach ENTER this=%p nativeBar=%p", this, m_bMenuBar);

    HaikuDumpMenuBar("Detach before base", m_bMenuBar);
    wxMenuBarBase::Detach();
    HaikuDumpMenuBar("Detach after base", m_bMenuBar);
}

BMenuBar* wxMenuBar::GetBMenuBar() const
{
    return m_bMenuBar;
}

#endif // wxUSE_MENUS
