/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/mdi.cpp
// Purpose:     MDI implementation for Haiku
// Author:      Based on Qt; adapted for Haiku
// Created:     [Date]
// Copyright:   (c) 2025 wxWidgets team
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_MDI

#include "wx/mdi.h"

#ifndef WX_PRECOMP
    #include "wx/log.h"
    #include "wx/menu.h"
    #include "wx/menuitem.h"
    #include "wx/stockitem.h"
    #include "wx/frame.h"
#endif

#include "wx/haiku/private/winevent.h"

#include <Window.h>
#include <Screen.h>

// ----------------------------------------------------------------------------
// wxMDIParentFrame
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(wxMDIParentFrame, wxFrame);
//wxIMPLEMENT_DYNAMIC_CLASS(wxMDIChildFrame, wxFrame);
//wxIMPLEMENT_DYNAMIC_CLASS(wxMDIClientWindow, wxWindow);
//wxMDIParentFrame::wxMDIParentFrame()
//{
//}

wxMDIParentFrame::wxMDIParentFrame(wxWindow* parent,
                                   wxWindowID id,
                                   const wxString& title,
                                   const wxPoint& pos,
                                   const wxSize& size,
                                   long style,
                                   const wxString& name)
{
    Create(parent, id, title, pos, size, style, name);
}

bool wxMDIParentFrame::Create(wxWindow* parent,
                              wxWindowID id,
                              const wxString& title,
                              const wxPoint& pos,
                              const wxSize& size,
                              long style,
                              const wxString& name)
{
    if (!wxFrame::Create(parent, id, title, pos, size, style, name))
        return false;

    // Create the MDI client window
    wxMDIClientWindow* client = OnCreateClient();
    m_clientWindow = client;

    if (!m_clientWindow || !m_clientWindow->CreateClient(this, GetWindowStyleFlag()))
        return false;

    return true;
}

wxMDIParentFrame::~wxMDIParentFrame()
{
#if wxUSE_MENUS
    // The parent menubar owns the Window menu.
    // Prevent wxMDIParentFrameBase from deleting it again.
    if (m_windowMenu && m_windowMenu->GetMenuBar())
        m_windowMenu = nullptr;
#endif
}


BWindow* wxMDIParentFrame::GetHaikuWindow() const
{
    BView* view = GetHandle();
    return view ? view->Window() : nullptr;
}

void wxMDIParentFrame::AddMDIChild(wxMDIChildFrame* child)
{
    m_children.AddItem(child);

#if wxUSE_MENUS
    switch (GetChildFramesCount())
    {
        case 1:
            // First MDI child was just added
            AddWindowMenu();
            UpdateWindowMenu(false);
            break;

        case 2:
            // Second MDI child was added, enable menu items
            UpdateWindowMenu(true);
            break;
    }
#endif // wxUSE_MENUS
}

void wxMDIParentFrame::RemoveMDIChild(wxMDIChildFrame* child)
{
    // Prevent stale active-child pointer during destruction.
    if (GetActiveChild() == child)
        SetActiveChild(nullptr);

    m_children.RemoveItem(child);

#if wxUSE_MENUS
    switch (GetChildFramesCount())
    {
        case 0:
            // Last MDI child is being removed
            RemoveWindowMenu();
            break;

        case 1:
            // Only one child remaining, disable multi-window commands
            UpdateWindowMenu(false);
            break;
    }
#endif // wxUSE_MENUS
}

void wxMDIParentFrame::Cascade()
{
    // Haiku doesn't have native MDI cascading
    // Could implement manually if needed
}

void wxMDIParentFrame::Tile(wxOrientation WXUNUSED(orient))
{
    // Haiku doesn't have native MDI tiling
    // Could implement manually if needed
}

void wxMDIParentFrame::ActivateNext()
{
    int count = GetChildCount();
    if (count < 2)
        return;

    wxMDIChildFrame* active = GetActiveChild();
    if (!active)
    {
        if (count > 0)
        {
            wxMDIChildFrame* first = GetChildAt(0);
            if (first)
                first->Activate();
        }
        return;
    }

    // Find current active child and activate next
    for (int i = 0; i < count; i++)
    {
        if (GetChildAt(i) == active)
        {
            int next = (i + 1) % count;
            wxMDIChildFrame* nextChild = GetChildAt(next);
            if (nextChild)
                nextChild->Activate();
            break;
        }
    }
}

void wxMDIParentFrame::ActivatePrevious()
{
    int count = GetChildCount();
    if (count < 2)
        return;

    wxMDIChildFrame* active = GetActiveChild();
    if (!active)
    {
        if (count > 0)
        {
            wxMDIChildFrame* last = GetChildAt(count - 1);
            if (last)
                last->Activate();
        }
        return;
    }

    // Find current active child and activate previous
    for (int i = 0; i < count; i++)
    {
        if (GetChildAt(i) == active)
        {
            int prev = (i - 1 + count) % count;
            wxMDIChildFrame* prevChild = GetChildAt(prev);
            if (prevChild)
                prevChild->Activate();
            break;
        }
    }
}

void wxMDIParentFrame::DeactivateOtherChildren(wxMDIChildFrame* activeChild)
{
    for (int i = 0; i < GetChildCount(); i++)
    {
        wxMDIChildFrame* child = GetChildAt(i);
        if (child && child != activeChild)
        {
            BWindow* childWindow = child->GetHaikuWindow();
            if (childWindow && !childWindow->IsHidden())
            {
                // Send deactivate event
                wxActivateEvent event(wxEVT_ACTIVATE, false, child->GetId());
                event.SetEventObject(child);
                child->HandleWindowEvent(event);
            }
        }
    }
}

#if wxUSE_MENUS
void wxMDIParentFrame::OnMDICommand(wxCommandEvent& event)
{
    switch (event.GetId())
    {
        case wxID_MDI_WINDOW_CASCADE:
            Cascade();
            break;

        case wxID_MDI_WINDOW_TILE_HORZ:
        case wxID_MDI_WINDOW_TILE_VERT:
            Tile(event.GetId() == wxID_MDI_WINDOW_TILE_HORZ ? wxHORIZONTAL : wxVERTICAL);
            break;

        case wxID_MDI_WINDOW_NEXT:
            ActivateNext();
            break;

        case wxID_MDI_WINDOW_PREV:
            ActivatePrevious();
            break;

        default:
            event.Skip();
            break;
    }
}

int wxMDIParentFrame::GetChildFramesCount() const
{
    const wxWindow* parent = GetClientWindow();
    if (!parent)
        return 0;

    int count = 0;
    for (wxWindowList::const_iterator i = parent->GetChildren().begin();
         i != parent->GetChildren().end();
         ++i)
    {
        if (wxDynamicCast(*i, wxMDIChildFrame))
            count++;
    }

    return count;
}
/*
void wxMDIParentFrame::AddMDIChild(wxMDIChildFrame* child)
{
    m_children.AddItem(child);

    switch (GetChildFramesCount())
    {
        case 1:
            // First MDI child was just added
            AddWindowMenu();
            UpdateWindowMenu(false);
            break;

        case 2:
            // Second MDI child was added, enable menu items
            UpdateWindowMenu(true);
            break;
    }
}

void wxMDIParentFrame::RemoveMDIChild(wxMDIChildFrame* child)
{
    m_children.RemoveItem(child);

    switch (GetChildFramesCount())
    {
        case 0:
            // Last MDI child is being removed
            RemoveWindowMenu();
            break;

        case 1:
            // Only one child remaining, disable multi-window commands
            UpdateWindowMenu(false);
            break;
    }
}
*/
void wxMDIParentFrame::AddWindowMenu()
{
    if (!(GetWindowStyleFlag() & wxFRAME_NO_WINDOW_MENU) && !m_windowMenu)
    {
        m_windowMenu = new wxMenu;

        m_windowMenu->Append(wxID_MDI_WINDOW_CASCADE, _("&Cascade"));
        m_windowMenu->Append(wxID_MDI_WINDOW_TILE_HORZ, _("Tile &Horizontally"));
        m_windowMenu->Append(wxID_MDI_WINDOW_TILE_VERT, _("Tile &Vertically"));
        m_windowMenu->AppendSeparator();
        m_windowMenu->Append(wxID_MDI_WINDOW_NEXT, _("&Next"));
        m_windowMenu->Append(wxID_MDI_WINDOW_PREV, _("&Previous"));

        wxMenuBar* menuBar = GetMenuBar();
        if (menuBar)
        {
            // Insert before Help menu if it exists
            int helpPos = menuBar->FindMenu(wxGetStockLabel(wxID_HELP));
            if (helpPos != wxNOT_FOUND)
                menuBar->Insert(helpPos, m_windowMenu, _("&Window"));
            else
                menuBar->Append(m_windowMenu, _("&Window"));
        }

        Bind(wxEVT_MENU, &wxMDIParentFrame::OnMDICommand, this,
             wxID_MDI_WINDOW_FIRST, wxID_MDI_WINDOW_LAST);
    }
}

void wxMDIParentFrame::RemoveWindowMenu()
{
    if (!m_windowMenu)
        return;

    // Find the actual menu bar containing the Window menu.
    wxMenuBar* menuBar = m_windowMenu->GetMenuBar();

    if (menuBar)
    {
        bool removed = false;

        for (size_t i = 0; i < menuBar->GetMenuCount(); ++i)
        {
            if (menuBar->GetMenu(i) == m_windowMenu)
            {
                // Remove() detaches the menu but does not delete it.
                if (menuBar->Remove(i) == m_windowMenu)
                    removed = true;

                break;
            }
        }

        // Never delete a menu still owned by a menu bar.
        if (!removed)
            return;
    }

    delete m_windowMenu;
    m_windowMenu = nullptr;
}

void wxMDIParentFrame::UpdateWindowMenu(bool enable)
{
    if (m_windowMenu)
    {
        m_windowMenu->Enable(wxID_MDI_WINDOW_CASCADE, enable);
        m_windowMenu->Enable(wxID_MDI_WINDOW_TILE_HORZ, enable);
        m_windowMenu->Enable(wxID_MDI_WINDOW_TILE_VERT, enable);
        m_windowMenu->Enable(wxID_MDI_WINDOW_NEXT, enable);
        m_windowMenu->Enable(wxID_MDI_WINDOW_PREV, enable);
    }
}
#endif // wxUSE_MENUS

// ----------------------------------------------------------------------------
// wxMDIChildFrame
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(wxMDIChildFrame, wxMDIChildFrameBase);

//wxMDIChildFrame::wxMDIChildFrame()
//{
//}

wxMDIChildFrame::wxMDIChildFrame(wxMDIParentFrame* parent,
                                 wxWindowID id,
                                 const wxString& title,
                                 const wxPoint& pos,
                                 const wxSize& size,
                                 long style,
                                 const wxString& name)
{
    Create(parent, id, title, pos, size, style, name);
}

bool wxMDIChildFrame::Create(wxMDIParentFrame* parent,
                             wxWindowID id,
                             const wxString& title,
                             const wxPoint& pos,
                             const wxSize& size,
                             long style,
                             const wxString& name)
{
    if (!wxFrame::Create(parent->GetClientWindow(), id, title, pos, size, style, name))
        return false;

    m_mdiParent = parent;

#if wxUSE_MENUS
    m_mdiParent->AddMDIChild(this);
#endif

    return true;
}

wxMDIChildFrame::~wxMDIChildFrame()
{
    
    m_mdiParent->RemoveMDIChild(this);

  //  delete m_menuBar;
}


BWindow* wxMDIChildFrame::GetHaikuWindow() const
{
    BView* view = GetHandle();
    return view ? view->Window() : nullptr;
}

void wxMDIChildFrame::Activate()
{
    BWindow* window = GetHaikuWindow();
    if (window)
    {
        if (window->IsHidden())
            Show();
        
        window->Activate();
        
        // Deactivate other children
        if (m_mdiParent)
        {
            m_mdiParent->SetActiveChild(this);
            m_mdiParent->DeactivateOtherChildren(this);
        }

        // Send activate event
        wxActivateEvent event(wxEVT_ACTIVATE, true, GetId());
        event.SetEventObject(this);
        HandleWindowEvent(event);
    }
}

#if wxUSE_MENUS
void wxMDIChildFrame::SetMenuBar(wxMenuBar* menuBar)
{
    wxMenuBar* oldMenuBar = GetMenuBar();
    
    if (oldMenuBar && m_mdiParent)
    {
        // Restore parent's menu bar if this is the active child
        if (m_mdiParent->GetActiveChild() == this)
        {
            InternalSetMenuBar();
        }
    }

    // Call base class to set m_menuBar
    wxFrame::SetMenuBar(menuBar);

    // If this is the active child, attach the new menu bar
    if (m_mdiParent && m_mdiParent->GetActiveChild() == this && menuBar)
    {
        InternalSetMenuBar();
    }
}

void wxMDIChildFrame::InternalSetMenuBar()
{
    wxCHECK_RET(m_mdiParent, "Invalid MDI parent window");

    // Each native frame owns its menu bar. Only a shared native frame
    // can display a child's menu bar on the parent.
    if (GetHaikuWindow() != m_mdiParent->GetHaikuWindow())
        return;

    wxMenuBar* childMenuBar = GetMenuBar();
    if (!childMenuBar)
    {
        // This child doesn't have a menu bar, restore parent's
        return;
    }

    wxMenuBar* parentMenuBar = m_mdiParent->GetMenuBar();

    if (childMenuBar != parentMenuBar)
    {
        m_mdiParent->SetMenuBar(childMenuBar);

        // Handle window menu
        AttachWindowMenuTo(childMenuBar, parentMenuBar);

        if (childMenuBar)
            childMenuBar->Show();
        if (parentMenuBar)
            parentMenuBar->Hide();
    }
}

void wxMDIChildFrame::AttachWindowMenuTo(wxMenuBar* attachedMenuBar,
                                         wxMenuBar* detachedMenuBar)
{
    const wxString windowMenuTitle = _("&Window");

    if (detachedMenuBar)
    {
        int pos = detachedMenuBar->FindMenu(windowMenuTitle);
        if (pos != wxNOT_FOUND)
        {
            detachedMenuBar->Remove(pos);
        }
    }

    wxMenu* windowMenu = m_mdiParent->GetWindowMenu();

    if (windowMenu && attachedMenuBar)
    {
        int helpPos = attachedMenuBar->FindMenu(wxGetStockLabel(wxID_HELP));
        if (helpPos != wxNOT_FOUND)
        {
            attachedMenuBar->Insert(helpPos, windowMenu, windowMenuTitle);
        }
        else
        {
            attachedMenuBar->Append(windowMenu, windowMenuTitle);
        }
    }
}
#endif // wxUSE_MENUS

// ----------------------------------------------------------------------------
// wxMDIClientWindow
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(wxMDIClientWindow, wxWindow);

bool wxMDIClientWindow::CreateClient(wxMDIParentFrame* parent, long style)
{
    if ( !wxWindow::Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, style) )
        return false;

    return true;
}

#endif // wxUSE_MDI
