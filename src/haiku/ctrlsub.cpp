/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/ctrlsub.cpp
// Author:      Peter Most
// Copyright:   (c) Peter Most
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#include "wx/ctrlsub.h"
#include <wx/log.h>
#include <wx/sizer.h>
wxControlWithItems::wxControlWithItems()
{
    m_itemList = nullptr;
}

bool wxControlWithItems::Create(wxWindow *parent, wxWindowID id,
                                const wxPoint& pos, const wxSize& size,
                                long style, const wxValidator& validator,
                                const wxString& name)
{
    if (!wxControlWithItemsBase::Create(parent, id, pos, size, style, validator, name))
        return false;

    // Concrete item controls (wxListBox, wxChoice, wxComboBox) create and
    // adopt their native Haiku view before reaching this common base. Creating
    // another BListView here made those controls have two native children,
    // while wxWidgets only has one control in its window hierarchy.
    if (GetHandle())
    {
        m_itemList = dynamic_cast<BListView*>(GetHandle());
        return true;
    }

    // Fallback for a future simple BListView-based control which doesn't
    // supply its own native view.
    m_itemList = new BListView(BRect(pos.x, pos.y, pos.x + size.x, pos.y + size.y), 
                               name.c_str(), 
                               B_SINGLE_SELECTION_LIST, // Specify list view type
                               B_FOLLOW_LEFT | B_FOLLOW_TOP, // Resizing mode
                               B_WILL_DRAW); // Drawing flags
    
    if (parent)
    {
        BView* parentView = static_cast<BView*>(parent->GetHandle());
        if (parentView)
        {
            parentView->AddChild(m_itemList);
        }
        else
        {
            wxLogError("Failed to get BView from parent window");
            delete m_itemList;
            m_itemList = nullptr;
            return false;
        }
    }
    else
    {
        wxLogError("No parent window provided");
        delete m_itemList;
        m_itemList = nullptr;
        return false;
    }

    return true;
}

wxSize wxControlWithItems::DoGetBestSize() const
{
    return wxSize(200, 150); // Adjust as needed
}

void wxControlWithItems::AddItem(const wxString& item)
{
    BStringItem* newItem = new BStringItem(item.c_str());
    m_itemList->AddItem(newItem);
    m_itemClientData.push_back(nullptr);
}

void wxControlWithItems::RemoveItem(unsigned int index)
{
    m_itemList->RemoveItem(index);
    if (index < m_itemClientData.size())
        m_itemClientData.erase(m_itemClientData.begin() + index);
}

void wxControlWithItems::ClearItems()
{
    m_itemList->MakeEmpty();
    m_itemClientData.clear();
}

unsigned int wxControlWithItems::GetItemCount() const
{
    return m_itemList->CountItems();
}

wxString wxControlWithItems::GetString(unsigned int n) const
{
    BStringItem* item = static_cast<BStringItem*>(m_itemList->ItemAt(n));
    return item ? wxString(item->Text()) : wxString();
}

void wxControlWithItems::SetString(unsigned int n, const wxString& s)
{
    BStringItem* item = static_cast<BStringItem*>(m_itemList->ItemAt(n));
    if (item) {
        item->SetText(s.c_str());
        m_itemList->InvalidateItem(n);
    }
}

void wxControlWithItems::SetSelection(int n)
{
    m_itemList->Select(n);
}

int wxControlWithItems::GetSelection() const
{
    return m_itemList->CurrentSelection();
}

int wxControlWithItems::DoInsertItems(const wxArrayStringsAdapter& items, 
                                     unsigned int pos, 
                                     void **clientData, 
                                     wxClientDataType type)
{
    wxUnusedVar(type);
    for (unsigned int i = 0; i < items.GetCount(); ++i) {
        BStringItem* newItem = new BStringItem(items[i].c_str());
        m_itemList->AddItem(newItem, pos + i);
        m_itemClientData.insert(m_itemClientData.begin() + pos + i,
                                clientData ? clientData[i] : nullptr);
        if (clientData) {
            DoSetItemClientData(pos + i, clientData[i]);
        }
    }
    return pos + items.GetCount() - 1; // Return index of last inserted item
}

void wxControlWithItems::DoSetItemClientData(unsigned int n, void *clientData)
{
    if (n < m_itemClientData.size())
        m_itemClientData[n] = clientData;
}

void* wxControlWithItems::DoGetItemClientData(unsigned int n) const
{
    return n < m_itemClientData.size() ? m_itemClientData[n] : nullptr;
}
