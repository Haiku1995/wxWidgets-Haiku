/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/checklst.cpp
// Purpose:     wxCheckListBox implementation for Haiku
// Author:      Based on Qt; adapted for Haiku
// Created:     [Date]
// Copyright:   (c) 2025 wxWidgets team
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////


#include "wx/haiku/private.h"
#include "wx/checklst.h"
#include "wx/log.h"

#include <ListView.h>
#include <Autolock.h>
#include <ListItem.h>
#include <Message.h>

// ----------------------------------------------------------------------------
// wxCheckListBox
// ----------------------------------------------------------------------------



// The base listbox owns the single native view and stable item identities.
extern bool wxHaikuListItemIsChecked(BListItem* item);
extern void wxHaikuListItemSetChecked(BListItem* item, bool checked);

wxCheckListBox::wxCheckListBox(wxWindow* parent,
                               wxWindowID id,
                               const wxPoint& pos,
                               const wxSize& size,
                               int nStrings,
                               const wxString* choices,
                               long style,
                               const wxValidator& validator,
                               const wxString& name)
{
    Create(parent, id, pos, size, nStrings, choices, style, validator, name);
}

wxCheckListBox::wxCheckListBox(wxWindow* parent,
                               wxWindowID id,
                               const wxPoint& pos,
                               const wxSize& size,
                               const wxArrayString& choices,
                               long style,
                               const wxValidator& validator,
                               const wxString& name)
{
    Create(parent, id, pos, size, choices, style, validator, name);
}

wxCheckListBox::~wxCheckListBox()
{
    // Items deleted by BListView
}

bool wxCheckListBox::Create(wxWindow* parent,
                            wxWindowID id,
                            const wxPoint& pos,
                            const wxSize& size,
                            int n,
                            const wxString choices[],
                            long style,
                            const wxValidator& validator,
                            const wxString& name)
{
    if (!wxListBox::Create(parent, id, pos, size, n, choices, style, validator, name))
        return false;
    m_nativeListView = dynamic_cast<BListView*>(GetHandle());
    return m_nativeListView != nullptr;
}

bool wxCheckListBox::Create(wxWindow* parent,
                            wxWindowID id,
                            const wxPoint& pos,
                            const wxSize& size,
                            const wxArrayString& choices,
                            long style,
                            const wxValidator& validator,
                            const wxString& name)
{
    return Create(parent, id, pos, size, static_cast<int>(choices.size()),
                  choices.empty() ? nullptr : &choices[0],
                  style, validator, name);
}

bool wxCheckListBox::IsChecked(unsigned int item) const
{
    if (!m_nativeListView) return false;
    BAutolock lock(m_nativeListView->Looper());
    if (m_nativeListView->Looper() && !lock.IsLocked()) return false;
    if (item >= static_cast<unsigned int>(m_nativeListView->CountItems())) return false;
    return wxHaikuListItemIsChecked(m_nativeListView->ItemAt(item));
}

void wxCheckListBox::Check(unsigned int item, bool checked)
{
    if (!m_nativeListView) return;
    BAutolock lock(m_nativeListView->Looper());
    if (m_nativeListView->Looper() && !lock.IsLocked()) return;
    if (item >= static_cast<unsigned int>(m_nativeListView->CountItems())) return;
    wxHaikuListItemSetChecked(m_nativeListView->ItemAt(item), checked);
    m_nativeListView->InvalidateItem(item);
}

BListView* wxCheckListBox::GetBListView() const
{
    return m_nativeListView;
}
