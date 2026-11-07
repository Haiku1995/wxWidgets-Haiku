/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/choice.cpp
// Purpose:     wxChoice implementation for Haiku
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"
#include "control_sort.h"

#if wxUSE_CHOICE

#ifndef WX_PRECOMP
    #include "wx/choice.h"
#endif

#include "wx/choice.h"
#include "wx/haiku/private.h"
#include "wx/log.h"

#include <MenuField.h>
#include <PopUpMenu.h>
#include <MenuItem.h>
#include <Message.h>
#include <View.h>
#include <InterfaceDefs.h>
#include <Looper.h>

//wxIMPLEMENT_DYNAMIC_CLASS(wxChoice, wxChoiceBase);

static BRect wxToBRect(const wxPoint& pos, const wxSize& size)
{
    const wxPoint p = (pos == wxDefaultPosition) ? wxPoint(0, 0) : pos;

    if (size == wxDefaultSize)
    {
        return BRect((float)p.x, (float)p.y,
                     (float)(p.x + 100 - 1),
                     (float)(p.y + 24 - 1));
    }

    return BRect((float)p.x, (float)p.y,
                 (float)(p.x + size.x - 1),
                 (float)(p.y + size.y - 1));
}

namespace
{
class ChoiceLock
{
public:
    explicit ChoiceLock(BView* view)
        : m_looper(view ? view->Looper() : nullptr),
          m_locked(m_looper && m_looper->Lock()) {}
    ~ChoiceLock() { if (m_locked) m_looper->Unlock(); }
    bool IsOk() const { return !m_looper || m_locked; }
private:
    BLooper* m_looper;
    bool m_locked;
};
}

class wxHaikuChoiceMenuField : public BMenuField
{
public:
    wxHaikuChoiceMenuField(BRect frame, wxChoice* handler, const char* label, BMenu* menu)
        : BMenuField(frame, "choice", label, menu), m_handler(handler)
    {
        SetDivider(0);
        SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
    }

    void AllAttached() override
    {
        BMenuField::AllAttached();
        Menu()->SetTargetForItems(this);
    }

    void MessageReceived(BMessage* message) override
    {
        if (message->what != 'CHCE')
        {
            BMenuField::MessageReceived(message);
            return;
        }
        if (!m_handler->IsEnabled())
            return;
        int64 id;
        if (message->FindInt64("wxItemId", &id) != B_OK)
            return;
        for (int32 n = 0; n < Menu()->CountItems(); ++n)
        {
            BMessage* itemMessage = Menu()->ItemAt(n)->Message();
            int64 itemId;
            if (!itemMessage || itemMessage->FindInt64("wxItemId", &itemId) != B_OK || itemId != id)
                continue;
            m_handler->SetSelection(n);
            auto* event = new wxCommandEvent(wxEVT_CHOICE, m_handler->GetId());
            event->SetInt(n);
            event->SetString(m_handler->GetString(n));
            event->SetEventObject(m_handler);
            if (m_handler->HasClientObjectData())
                event->SetClientObject(m_handler->GetClientObject(n));
            else if (m_handler->HasClientUntypedData())
                event->SetClientData(m_handler->GetClientData(n));
            m_handler->GetEventHandler()->QueueEvent(event);
            return;
        }
        // A queued notification for an item already removed is obsolete.
    }

    int64 nextItemId = 0;
private:
    wxChoice* m_handler;
};

wxChoice::~wxChoice()
{
    Clear();
}

wxChoice::wxChoice(wxWindow* parent,
                   wxWindowID id,
                   const wxPoint& pos,
                   const wxSize& size,
                   int n,
                   const wxString choices[],
                   long style,
                   const wxValidator& validator,
                   const wxString& name)
{
    Create(parent, id, pos, size, n, choices, style, validator, name);
}

wxChoice::wxChoice(wxWindow* parent,
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

bool wxChoice::Create(wxWindow* parent,
                      wxWindowID id,
                      const wxPoint& pos,
                      const wxSize& size,
                      const wxArrayString& choices,
                      long style,
                      const wxValidator& validator,
                      const wxString& name)
{
    return Create(parent, id, pos, size,
                  (int)choices.size(),
                  choices.empty() ? nullptr : &choices[0],
                  style, validator, name);
}

bool wxChoice::Create(wxWindow* parent,
                      wxWindowID id,
                      const wxPoint& pos,
                      const wxSize& size,
                      int n,
                      const wxString choices[],
                      long style,
                      const wxValidator& validator,
                      const wxString& name)
{
    wxCHECK_MSG(parent && n >= 0 && (!n || choices), false, "invalid choice arguments");
    m_bMenu = new BPopUpMenu("", true, true);
    m_bMenuField = new wxHaikuChoiceMenuField(wxToBRect(pos, size), this, "", m_bMenu);
    HaikuAdoptView(m_bMenuField, nullptr, m_bMenuField);
    if (!wxChoiceBase::Create(parent, id, pos, size, style, validator, name))
        return false;
    for (int i = 0; i < n; ++i)
        Append(choices[i]);
    SetSelection(wxNOT_FOUND);
    return true;
}

BMenuField* wxChoice::GetBMenuField() const
{
    return m_bMenuField;
}

wxSize wxChoice::DoGetBestSize() const
{
    ChoiceLock lock(m_bMenuField);
    if (lock.IsOk() && m_bMenuField)
    {
        float w = 0.0f, h = 0.0f;
        m_bMenuField->GetPreferredSize(&w, &h);
        return wxSize((int)(w + 1), (int)(h + 1));
    }

    wxSize size = wxChoiceBase::DoGetBestSize();
    size.IncTo(wxSize(80, 24));
    return size;
}

unsigned int wxChoice::GetCount() const
{
    ChoiceLock lock(m_bMenuField);
    return lock.IsOk() && m_bMenuField ? (unsigned int)m_bMenuField->Menu()->CountItems() : 0;
}

wxString wxChoice::GetString(unsigned int n) const
{
    ChoiceLock lock(m_bMenuField);
    if (!lock.IsOk() || !m_bMenuField)
        return wxString();

    wxCHECK_MSG(n < GetCount(), wxString(), "invalid choice index");
    BMenuItem* item = m_bMenuField->Menu()->ItemAt((int32)n);
    return item ? wxString::FromUTF8(item->Label()) : wxString();
}

void wxChoice::SetString(unsigned int n, const wxString& s)
{
    ChoiceLock lock(m_bMenuField);
    if (!lock.IsOk() || !m_bMenuField)
        return;

    wxCHECK_RET(n < GetCount(), "invalid choice index");
    BMenuItem* item = m_bMenuField->Menu()->ItemAt((int32)n);
    if (item)
    {
        item->SetLabel(s.utf8_str());
        if (GetSelection() == int(n)) SetSelection(n);
        InvalidateBestSize();
    }
}

void wxChoice::SetSelection(int n)
{
    ChoiceLock lock(m_bMenuField);
    if (!lock.IsOk() || !m_bMenuField)
        return;

    if (n == wxNOT_FOUND)
    {
        BMenu* const menu = m_bMenuField->Menu();
        for (int32 i = 0; i < menu->CountItems(); ++i)
            menu->ItemAt(i)->SetMarked(false);
        m_bMenuField->MenuItem()->SetLabel("");
        return;
    }

    wxCHECK_RET(n >= 0 && n < (int)GetCount(), "invalid choice selection");

    BMenuItem* item = m_bMenuField->Menu()->ItemAt((int32)n);
    if (item)
    {
        item->SetMarked(true);
        m_bMenuField->MenuItem()->SetLabel(item->Label());
    }
}

int wxChoice::GetSelection() const
{
    ChoiceLock lock(m_bMenuField);
    if (!lock.IsOk() || !m_bMenuField)
        return wxNOT_FOUND;

    BMenu* menu = m_bMenuField->Menu();
    if (!menu)
        return wxNOT_FOUND;

    for (int32 i = 0; i < menu->CountItems(); ++i)
    {
        BMenuItem* item = menu->ItemAt(i);
        if (item && item->IsMarked())
            return (int)i;
    }

    return wxNOT_FOUND;
}

int wxChoice::DoInsertOneItem(const wxString& item, unsigned int pos)
{
    ChoiceLock lock(m_bMenuField);
    if (!lock.IsOk() || !m_bMenuField)
        return wxNOT_FOUND;
    if (IsSorted())
    {
        pos = 0;
        while (pos < GetCount() && wxHaikuCompareItemLabels(GetString(pos), item) <= 0)
            ++pos;
    }
    wxCHECK_MSG(pos <= GetCount(), wxNOT_FOUND, "invalid insertion position");
    auto* msg = new BMessage('CHCE');
    msg->AddInt64("wxItemId", ++static_cast<wxHaikuChoiceMenuField*>(m_bMenuField)->nextItemId);
    auto* menuItem = new BMenuItem(item.utf8_str(), msg);
    if (!m_bMenuField->Menu()->AddItem(menuItem, pos))
    {
        delete menuItem;
        return wxNOT_FOUND;
    }
    menuItem->SetTarget(m_bMenuField);
    m_clientData.insert(m_clientData.begin() + pos, nullptr);
    InvalidateBestSize();
    return pos;
}
void wxChoice::DoSetItemClientData(unsigned int n, void* clientData)
{
    if (n < m_clientData.size())
        m_clientData[n] = clientData;
}

void* wxChoice::DoGetItemClientData(unsigned int n) const
{
    return n < m_clientData.size() ? m_clientData[n] : nullptr;
}

void wxChoice::DoClear()
{
    ChoiceLock lock(m_bMenuField);
    if (!lock.IsOk())
        return;
    if (m_bMenuField)
    {
        m_bMenuField->Menu()->RemoveItems(0, m_bMenuField->Menu()->CountItems(), true);
        m_bMenuField->MenuItem()->SetLabel("");
    }
    m_clientData.clear();
    InvalidateBestSize();
}

void wxChoice::DoDeleteOneItem(unsigned int pos)
{
    ChoiceLock lock(m_bMenuField);
    wxCHECK_RET(lock.IsOk() && m_bMenuField && pos < GetCount(), "invalid choice index");
    const bool selected = GetSelection() == int(pos);
    delete m_bMenuField->Menu()->RemoveItem(int32(pos));
    m_clientData.erase(m_clientData.begin() + pos);
    if (selected)
        SetSelection(wxNOT_FOUND);
    InvalidateBestSize();
}

int wxChoice::DoInsertItems(const wxArrayStringsAdapter& items,
                           unsigned int pos, void** clientData,
                           wxClientDataType type)
{
    return DoInsertItemsInLoop(items, pos, clientData, type);
}

#endif // wxUSE_CHOICE