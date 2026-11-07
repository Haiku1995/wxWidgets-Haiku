/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/menuitem.cpp
// Purpose:     wxMenuItem implementation for Haiku
// Author:      Based on Qt; adapted for Haiku
// Created:     [Date]
// Copyright:   (c) 2025 wxWidgets team
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/haiku/private.h"
#include "wx/menuitem.h"
#include "wx/menu.h"
#include "wx/log.h"

#include <MenuItem.h>
#include <Message.h>
#include <Messenger.h>
#include <PopUpMenu.h>
#define HAIKU_LOG(msg, ...) wxLogDebug("[HAIKU:%s:%d] " msg, __func__, __LINE__, ##__VA_ARGS__)
#define HAIKU_LOG_PRINTF(msg, ...) printf("[HAIKU:%s:%d] " msg "\n", __func__, __LINE__, ##__VA_ARGS__); fflush(stdout)

//wxIMPLEMENT_DYNAMIC_CLASS(wxMenuItem, wxMenuItemBase);
static const uint32 kWxMenuInvoke = 'wMnu';
static const uint32 kWxMenuHighlight = 'wMhi';

// BMenuItem receives the native selection changes while the menu is tracked.
// Forward them to the owning wx frame so its standard wxFrameBase handler can
// display the item's help string in the status bar.
class wxHaikuMenuItem : public BMenuItem
{
public:
    wxHaikuMenuItem(const char* label, BMessage* message, int32 wxId)
        : BMenuItem(label, message),
          m_wxId(wxId)
    {
    }

protected:
    void Highlight(bool highlighted) override
    {
        BMenuItem::Highlight(highlighted);

        BMenu* const menu = Menu();
        BWindow* const window = menu ? menu->Window() : nullptr;
        if (!window)
            return;

        BMessage message(kWxMenuHighlight);
        message.AddInt32("wx_id", highlighted ? m_wxId : wxID_NONE);
        BMessenger(window).SendMessage(&message);
    }

private:
    int32 m_wxId;
};
wxMenuItem* wxMenuItemBase::New(wxMenu* parentMenu, int id,
                                const wxString& name, const wxString& help,
                                wxItemKind kind, wxMenu* subMenu)
{
HAIKU_LOG("=====wxMenuItem* wxMenuItemBase::New Function ==========");
    return new wxMenuItem(parentMenu, id, name, help, kind, subMenu);
}

WXDLLIMPEXP_CORE wxMenuItem::wxMenuItem(wxMenu* parentMenu,
                                       int id,
                                       const wxString& text,
                                       const wxString& help,
                                       wxItemKind kind,
                                       wxMenu* subMenu)
    : wxMenuItemBase(parentMenu, id, text, help, kind, subMenu),
      m_bItem(nullptr)
{
    HAIKU_LOG("===========WXDLLIMPEXP_CORE wxMenuItem::wxMenuItem(wxMenu* parentMenu =============");

    if (kind == wxITEM_SEPARATOR)
    {
        m_bItem = new BSeparatorItem();
        return;
    }

    // The base class resolves empty stock labels before native creation.
    const wxString plain = GetItemLabelText();

    if (subMenu)
    {
        BMenu* bSub = subMenu->GetBMenu();
        bSub->SetName(wxToBStr(plain).String());

        BMessage* msg = new BMessage(kWxMenuInvoke);
        msg->AddInt32("wx_id", (int32)id);

        m_bItem = new BMenuItem(bSub, msg);
    }
    else
    {
        BMessage* msg = new BMessage(kWxMenuInvoke);
        msg->AddInt32("wx_id", (int32)id);

        m_bItem = new wxHaikuMenuItem(wxToBStr(plain).String(), msg, (int32)id);
    }

    if (kind == wxITEM_CHECK || kind == wxITEM_RADIO)
        m_bItem->SetMarked(false);
}

WXDLLIMPEXP_CORE wxMenuItem::~wxMenuItem()
{
    // The native item owns the native submenu. Destroy wx metadata first,
    // while that handle still exists; prevent the base from deleting it again.
    wxMenu* submenu=GetSubMenu();
    SetSubMenu(nullptr);
    delete submenu;
    // FIX: Only delete m_bItem if it is not already owned by a BMenu.
    // Once AddItem() has been called, the BMenu owns the BMenuItem and will
    // delete it. Deleting it ourselves causes a double-free crash.
    // BMenuItem::Menu() returns the owning BMenu (nullptr if not attached).
    if (m_bItem && !m_bItem->Menu())
        delete m_bItem;
}

WXDLLIMPEXP_CORE void wxMenuItem::SetItemLabel(const wxString& label)
{
    wxMenuItemBase::SetItemLabel(label);
    HAIKU_LOG("====WXDLLIMPEXP_CORE void wxMenuItem::SetItemLabel(const wxString====");

    if (!m_bItem)
        return;

    // Use the resolved label, including the stock fallback for empty input.
    const wxString plain = GetItemLabelText();

    BMenu* menu = m_bItem->Menu();
    BWindow* win = menu ? menu->Window() : nullptr;
    if (win && win->LockLooper())
    {
        m_bItem->SetLabel(plain.utf8_str());
        win->UnlockLooper();
    }
    else
    {
        m_bItem->SetLabel(plain.utf8_str());
    }
}

WXDLLIMPEXP_CORE void wxMenuItem::SetCheckable(bool checkable)
{
    wxMenuItemBase::SetCheckable(checkable);
    // Haiku does not have an explicit "checkable" API separate from
    // SetMarked(); the visual state is controlled via Check()/IsChecked().
}

WXDLLIMPEXP_CORE void wxMenuItem::Enable(bool enable)
{
    wxMenuItemBase::Enable(enable);

    if (!m_bItem)
        return;

    // FIX: Guard with LockLooper.
    BMenu* menu = m_bItem->Menu();
    BWindow* win = menu ? menu->Window() : nullptr;
    if (win && win->LockLooper())
    {
        m_bItem->SetEnabled(enable);
        win->UnlockLooper();
    }
    else
    {
        m_bItem->SetEnabled(enable);
    }
}

WXDLLIMPEXP_CORE bool wxMenuItem::IsEnabled() const
{
    return m_bItem ? m_bItem->IsEnabled() : false;
}

WXDLLIMPEXP_CORE void wxMenuItem::Check(bool checked)
{
    // Separators cannot be checked.
    if (IsSeparator())
        return;

    wxMenuItemBase::Check(checked);

    if (!m_bItem)
        return;

    // FIX: Guard with LockLooper.
    BMenu* menu = m_bItem->Menu();
    BWindow* win = menu ? menu->Window() : nullptr;
    if (win && win->LockLooper())
    {
        m_bItem->SetMarked(checked);
        win->UnlockLooper();
    }
    else
    {
        m_bItem->SetMarked(checked);
    }
}

WXDLLIMPEXP_CORE bool wxMenuItem::IsChecked() const
{
    return m_bItem ? m_bItem->IsMarked() : false;
}

WXDLLIMPEXP_CORE void wxMenuItem::SetFont(const wxFont& WXUNUSED(font))
{
    // Per-item fonts are not supported on Haiku.
}

WXDLLIMPEXP_CORE BMenuItem* wxMenuItem::GetBMenuItem() const
{
    return m_bItem;
}
