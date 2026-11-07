#include "wx/checklst.h"
/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/listbox.cpp
// Purpose:     wxListBox implementation for Haiku
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"
#include "control_sort.h"

#if wxUSE_LISTBOX

#include "wx/listbox.h"
#include "wx/haiku/private/winevent.h"

#include <ListView.h>
#include <ListItem.h>
#include <ScrollView.h>
#include <String.h>
#include <Window.h>
#include <Looper.h>
#include <vector>
#include <algorithm>

#define HAIKU_LOG(msg, ...) wxLogDebug("[HAIKU-LISTBOX:%s:%d] " msg, __func__, __LINE__, ##__VA_ARGS__)

// ----------------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------------

static inline BPoint wxToHaikuPoint(const wxPoint& pt)
{
    return BPoint((float)pt.x, (float)pt.y);
}

static BRect wxToBRect(const wxPoint& pos, const wxSize& size)
{
    const wxPoint p = (pos == wxDefaultPosition) ? wxPoint(0, 0) : pos;

    if (size == wxDefaultSize)
        return BRect((float)p.x, (float)p.y,
                     (float)(p.x + 120 - 1), (float)(p.y + 80 - 1));

    return BRect((float)p.x, (float)p.y,
                 (float)(p.x + size.GetWidth() - 1),
                 (float)(p.y + size.GetHeight() - 1));
}

// ----------------------------------------------------------------------------
// Native item
// ----------------------------------------------------------------------------

class wxHaikuListItem : public BStringItem
{
public:
    explicit wxHaikuListItem(const char* text,
                             uint32 level = 0,
                             bool expanded = true)
        : BStringItem(text, level, expanded),
          m_checked(false),
          m_clientData(nullptr)
    {
    }

    void DrawItem(BView* owner, BRect frame, bool complete = false) override;

    void SetChecked(bool checked) { m_checked = checked; }
    bool IsChecked() const { return m_checked; }

    void SetClientData(void* data) { m_clientData = data; }
    void* GetClientData() const { return m_clientData; }

private:
    bool  m_checked;
    void* m_clientData;
};

// ----------------------------------------------------------------------------
// Native list view
// ----------------------------------------------------------------------------

class wxHaikuListView : public BListView
{
public:
    wxHaikuListView(BRect frame,
                    const char* name,
                    wxListBox* handler,
                    list_view_type type = B_SINGLE_SELECTION_LIST,
                    uint32 resizingMode = B_FOLLOW_ALL_SIDES,
                    uint32 flags = B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE)
        : BListView(frame, name, type, resizingMode, flags),
          m_handler(handler),
          m_ignoreSelectionChange(false)
    {
    }

    void SelectionChanged() override
    {
        std::vector<BListItem*> selected;
        for (int32 ordinal = 0; ; ++ordinal)
        {
            const int32 index = CurrentSelection(ordinal);
            if (index < 0) break;
            selected.push_back(ItemAt(index));
        }
        if (!m_ignoreSelectionChange && m_handler && m_handler->IsEnabled())
        {
            for (int32 index = 0; index < CountItems(); ++index)
            {
                BListItem* item = ItemAt(index);
                const bool was = std::find(m_selected.begin(), m_selected.end(), item) != m_selected.end();
                const bool now = item->IsSelected();
                if (was != now && (now || ListType() == B_MULTIPLE_SELECTION_LIST))
                    m_handler->HaikuSendEvent(wxEVT_LISTBOX, index, now);
            }
        }
        m_selected.swap(selected);
    }
    void MouseDown(BPoint point) override
    {
#if wxUSE_CHECKLISTBOX
        if (auto* checklist = dynamic_cast<wxCheckListBox*>(m_handler))
        {
            const int32 index = IndexOf(point);
            if (index >= 0 && point.x < 20 && checklist->IsEnabled())
            {
                checklist->Check(index, !checklist->IsChecked(index));
                wxCommandEvent event(wxEVT_CHECKLISTBOX, checklist->GetId());
                event.SetEventObject(checklist);
                event.SetInt(index);
                event.SetString(checklist->GetString(index));
                wxPostEvent(checklist, event);
            }
        }
#endif
        BListView::MouseDown(point);

        BMessage* msg = Window() ? Window()->CurrentMessage() : nullptr;
        int32 clicks = 0;
        if (msg && msg->FindInt32("clicks", &clicks) == B_OK && clicks == 2)
        {
            const int32 index = CurrentSelection();
            if (index >= 0 && m_handler)
                m_handler->HaikuSendEvent(wxEVT_LISTBOX_DCLICK, (int)index, true);
        }
    }

    void KeyDown(const char* bytes, int32 count) override
    {
#if wxUSE_CHECKLISTBOX
        if (count == 1 && bytes[0] == ' ')
        {
            if (auto* checklist = dynamic_cast<wxCheckListBox*>(m_handler))
            {
                const int32 index = CurrentSelection();
                if (index >= 0 && checklist->IsEnabled())
                {
                    checklist->Check(index, !checklist->IsChecked(index));
                    wxCommandEvent event(wxEVT_CHECKLISTBOX, checklist->GetId());
                    event.SetEventObject(checklist);
                    event.SetInt(index);
                    event.SetString(checklist->GetString(index));
                    wxPostEvent(checklist, event);
                }
                return;
            }
        }
#endif
        BListView::KeyDown(bytes, count);
    }

    void SetIgnoreSelectionChange(bool ignore)
    {
        // Refresh the identity snapshot while still suppressing notifications.
        if (!ignore) SelectionChanged();
        m_ignoreSelectionChange = ignore;
    }
    wxListBox* GetHandler() const { return m_handler; }

private:
    wxListBox* m_handler;
    bool       m_ignoreSelectionChange;
    std::vector<BListItem*> m_selected;
};

void wxHaikuListItem::DrawItem(BView* owner, BRect frame, bool complete)
{
#if wxUSE_CHECKLISTBOX
    auto* list = dynamic_cast<wxHaikuListView*>(owner);
    if (list && dynamic_cast<wxCheckListBox*>(list->GetHandler()))
    {
        owner->PushState();
        owner->SetHighColor(ui_color(IsSelected() ? B_LIST_SELECTED_BACKGROUND_COLOR : B_LIST_BACKGROUND_COLOR));
        owner->FillRect(frame);
        BRect textFrame(frame); textFrame.left += 20;
        BStringItem::DrawItem(owner, textFrame, complete);
        const float top = frame.top + (frame.Height() - 11) / 2;
        BRect box(frame.left + 3, top, frame.left + 14, top + 11);
        owner->SetHighColor(ui_color(IsSelected() ? B_LIST_SELECTED_ITEM_TEXT_COLOR : B_LIST_ITEM_TEXT_COLOR));
        owner->StrokeRect(box);
        if (m_checked)
        {
            owner->StrokeLine(BPoint(box.left+2,box.top+5), BPoint(box.left+5,box.bottom-2));
            owner->StrokeLine(BPoint(box.left+5,box.bottom-2), BPoint(box.right-2,box.top+2));
        }
        owner->PopState();
        return;
    }
#endif
    BStringItem::DrawItem(owner, frame, complete);
}

bool wxHaikuListItemIsChecked(BListItem* item)
{
    auto* native = dynamic_cast<wxHaikuListItem*>(item);
    return native && native->IsChecked();
}
void wxHaikuListItemSetChecked(BListItem* item, bool checked)
{
    if (auto* native = dynamic_cast<wxHaikuListItem*>(item))
        native->SetChecked(checked);
}

class ListLock
{
public:
    explicit ListLock(BView* view) : m_looper(view ? view->Looper() : nullptr),
        m_locked(m_looper && m_looper->Lock()) {}
    ~ListLock() { if (m_locked) m_looper->Unlock(); }
    bool IsOk() const { return !m_looper || m_locked; }
private:
    BLooper* m_looper;
    bool m_locked;
};
// ----------------------------------------------------------------------------
// wxListBox
// ----------------------------------------------------------------------------

wxListBox::wxListBox(wxWindow* parent, wxWindowID id,
                     const wxPoint& pos,
                     const wxSize& size,
                     int n, const wxString choices[],
                     long style,
                     const wxValidator& validator,
                     const wxString& name)
{
    Create(parent, id, pos, size, n, choices, style, validator, name);
}

wxListBox::wxListBox(wxWindow* parent, wxWindowID id,
                     const wxPoint& pos,
                     const wxSize& size,
                     const wxArrayString& choices,
                     long style,
                     const wxValidator& validator,
                     const wxString& name)
{
    Create(parent, id, pos, size, choices, style, validator, name);
}

wxListBox::~wxListBox()
{
    if (m_bListView) Clear();
}

bool wxListBox::Create(wxWindow* parent, wxWindowID id,
                       const wxPoint& pos,
                       const wxSize& size,
                       int n, const wxString choices[],
                       long style,
                       const wxValidator& validator,
                       const wxString& name)
{
    wxCHECK_MSG(parent && n >= 0 && (!n || choices), false, "invalid listbox arguments");
    DoCreate(parent, style, pos, size);
    const long scrollStyle = ((style & wxLB_NO_SB) ? 0 : wxVSCROLL)
                           | ((style & wxLB_HSCROLL) ? wxHSCROLL : 0);
    if (!wxListBoxBase::Create(parent, id, pos, size,
                              style | scrollStyle, validator, name))
        return false;
    for (int i = 0; i < n; ++i)
        Append(choices[i]);
    return true;
}
bool wxListBox::Create(wxWindow* parent, wxWindowID id,
                       const wxPoint& pos,
                       const wxSize& size,
                       const wxArrayString& choices,
                       long style,
                       const wxValidator& validator,
                       const wxString& name)
{
    return Create(parent, id, pos, size, choices.size(),
                  choices.empty() ? nullptr : &choices[0], style, validator, name);
}
void wxListBox::DoCreate(wxWindow* WXUNUSED(parent), long style,
                         const wxPoint& pos, const wxSize& size)
{
    const BRect frame = wxToBRect(pos, size);

    list_view_type selType = B_SINGLE_SELECTION_LIST;
    if (style & (wxLB_MULTIPLE | wxLB_EXTENDED))
        selType = B_MULTIPLE_SELECTION_LIST;

    wxHaikuListView* listView = new wxHaikuListView(frame, "listbox", this, selType);

    BScrollView* scrollView = new BScrollView("listbox_scroll",
                                              listView,
                                              B_FOLLOW_ALL_SIDES,
                                              B_WILL_DRAW | B_FRAME_EVENTS,
                                              (style & wxLB_HSCROLL) != 0,
                                              (style & wxLB_NO_SB) == 0,
                                              (style & wxBORDER_MASK) == wxBORDER_NONE
                                                  ? B_NO_BORDER : B_FANCY_BORDER);

    m_bListView = listView;
    m_bScrollView = scrollView;

    // Integrate with wxWindowHaiku generic handling:
    // - outer geometry/parenting is the scroll view
    // - inner drawing/content is the list view
   HaikuAdoptView(m_bListView, m_bScrollView, m_bListView);



    HAIKU_LOG("DoCreate listView=%p scrollView=%p", m_bListView, m_bScrollView);
}

wxHaikuListView* wxListBox::GetHaikuListView() const
{
    return m_bListView;
}

bool wxListBox::IsSelected(int n) const
{
    ListLock lock(m_bListView);
    return lock.IsOk() && m_bListView && m_bListView->IsItemSelected(n);
}

int wxListBox::GetSelections(wxArrayInt& aSelections) const
{
    aSelections.clear();
    ListLock lock(m_bListView);
    if (!lock.IsOk() || !m_bListView) return 0;

    for (int32 ordinal = 0; ; ++ordinal)
    {
        const int32 index = GetHaikuListView()->CurrentSelection(ordinal);
        if (index < 0)
            break;
        aSelections.push_back((int)index);
    }

    return (int)aSelections.size();
}

unsigned wxListBox::GetCount() const
{
    ListLock lock(m_bListView);
    return lock.IsOk() && m_bListView ? unsigned(m_bListView->CountItems()) : 0;
}

wxString wxListBox::GetString(unsigned int n) const
{
    ListLock lock(m_bListView);
    wxCHECK_MSG(lock.IsOk() && m_bListView, wxString(), "listbox is not created");
    wxHaikuListItem* item = static_cast<wxHaikuListItem*>(GetHaikuListView()->ItemAt((int32)n));
    wxCHECK_MSG(item != nullptr, wxString(), "wrong listbox index");
    return wxString::FromUTF8(item->Text());
}

void wxListBox::SetString(unsigned int n, const wxString& s)
{
    ListLock lock(m_bListView);
    wxCHECK_RET(lock.IsOk() && m_bListView, "listbox is not created");
    wxHaikuListItem* item = static_cast<wxHaikuListItem*>(GetHaikuListView()->ItemAt((int32)n));
    wxCHECK_RET(item != nullptr, "wrong listbox index");

    item->SetText(s.utf8_str());
    GetHaikuListView()->InvalidateItem((int32)n);
}

int wxListBox::GetSelection() const
{
    ListLock lock(m_bListView);
    return lock.IsOk() && m_bListView ? int(m_bListView->CurrentSelection()) : wxNOT_FOUND;
}

void wxListBox::EnsureVisible(int n)
{
    ListLock lock(m_bListView);
    if (!lock.IsOk() || !m_bListView)
        return;

    wxCHECK_RET(n >= 0 && n < (int)GetCount(),
                "invalid index in wxListBox::EnsureVisible");

    BRect itemFrame = GetHaikuListView()->ItemFrame((int32)n);
    GetHaikuListView()->ScrollTo(itemFrame.LeftTop());
}

int wxListBox::GetTopItem() const
{
    ListLock lock(m_bListView);
    if (!lock.IsOk() || !m_bListView)
        return wxNOT_FOUND;

    const int32 index = GetHaikuListView()->IndexOf(BPoint(2.0f, 2.0f));
    return index >= 0 ? (int)index : -1;
}

int wxListBox::GetCountPerPage() const
{
    ListLock lock(m_bListView);
    if (!lock.IsOk() || !m_bListView)
        return 0;

    wxCHECK_MSG(GetCount() > 0, 0,
                "wxListBox needs at least one item to calculate the count per page");

    const BRect itemRect = GetHaikuListView()->ItemFrame(0);
    const float itemHeight = itemRect.Height();

    if (itemHeight > 0.0f)
        return (int)(GetHaikuListView()->Bounds().Height() / itemHeight);

    return 0;
}

void wxListBox::DoSetFirstItem(int n)
{
    ListLock lock(m_bListView);
    if (!lock.IsOk() || !m_bListView)
        return;

    wxCHECK_RET(n >= 0 && n < (int)GetCount(),
                "invalid index in wxListBox::DoSetFirstItem");

    BRect itemFrame = GetHaikuListView()->ItemFrame((int32)n);
    GetHaikuListView()->ScrollTo(itemFrame.LeftTop());
}

void wxListBox::DoSetSelection(int n, bool select)
{
    ListLock lock(m_bListView);
    wxCHECK_RET(lock.IsOk() && m_bListView, "listbox is not created");
    wxCHECK_RET(n == wxNOT_FOUND || (n >= 0 && n < int(GetCount())), "invalid selection");
    m_bListView->SetIgnoreSelectionChange(true);
    if (n == wxNOT_FOUND)
        m_bListView->DeselectAll();
    else if (select)
        m_bListView->Select(n, HasMultipleSelection());
    else
        m_bListView->Deselect(n);
    m_bListView->SetIgnoreSelectionChange(false);
    UpdateOldSelections();
}
int wxListBox::DoInsertItems(const wxArrayStringsAdapter& items,
                             unsigned int pos,
                             void** clientData,
                             wxClientDataType type)
{
    InvalidateBestSize();
    const int n = DoInsertItemsInLoop(items, pos, clientData, type);
    UpdateOldSelections();
    return n;
}

int wxListBox::DoInsertOneItem(const wxString& text, unsigned int pos)
{
    ListLock lock(m_bListView);
    wxCHECK_MSG(lock.IsOk() && m_bListView, wxNOT_FOUND, "listbox is not created");
    if (IsSorted())
    {
        pos = 0;
        while (pos < GetCount() && wxHaikuCompareItemLabels(GetString(pos), text) <= 0) ++pos;
    }
    wxCHECK_MSG(pos <= GetCount(), wxNOT_FOUND, "invalid insertion position");
    auto* item = new wxHaikuListItem(text.utf8_str());
    m_bListView->SetIgnoreSelectionChange(true);
    const bool added = m_bListView->AddItem(item, pos);
    m_bListView->SetIgnoreSelectionChange(false);
    if (!added) { delete item; return wxNOT_FOUND; }
    return pos;
}
void wxListBox::DoSetItemClientData(unsigned int n, void* clientData)
{
    ListLock lock(m_bListView);
    wxCHECK_RET(lock.IsOk() && m_bListView, "listbox is not created");
    wxHaikuListItem* item = static_cast<wxHaikuListItem*>(GetHaikuListView()->ItemAt((int32)n));
    if (item)
        item->SetClientData(clientData);
}

void* wxListBox::DoGetItemClientData(unsigned int n) const
{
    ListLock lock(m_bListView);
    wxCHECK_MSG(lock.IsOk() && m_bListView, nullptr, "listbox is not created");
    wxHaikuListItem* item = static_cast<wxHaikuListItem*>(GetHaikuListView()->ItemAt((int32)n));
    return item ? item->GetClientData() : nullptr;
}

void wxListBox::DoClear()
{
    ListLock lock(m_bListView);
    if (!lock.IsOk() || !m_bListView) return;
    m_bListView->SetIgnoreSelectionChange(true);
    while (BListItem* item = m_bListView->RemoveItem(int32(0))) delete item;
    m_bListView->SetIgnoreSelectionChange(false);
    UpdateOldSelections();
    InvalidateBestSize();
}

void wxListBox::DoDeleteOneItem(unsigned int pos)
{
    ListLock lock(m_bListView);
    wxCHECK_RET(lock.IsOk() && m_bListView && pos < GetCount(), "invalid listbox index");
    m_bListView->SetIgnoreSelectionChange(true);
    delete m_bListView->RemoveItem(int32(pos));
    m_bListView->SetIgnoreSelectionChange(false);
    UpdateOldSelections();
    InvalidateBestSize();
}
int wxListBox::DoListHitTest(const wxPoint& point) const
{
    ListLock lock(m_bListView);
    if (!lock.IsOk() || !m_bListView)
        return wxNOT_FOUND;

    const int32 index = GetHaikuListView()->IndexOf(wxToHaikuPoint(point));
    return index >= 0 ? (int)index : wxNOT_FOUND;
}

void wxListBox::SendEvent(wxEventType evtType, int n, bool selected)
{
    wxCHECK_RET(n >= 0 && n < int(GetCount()), "invalid listbox event index");
    auto* event = new wxCommandEvent(evtType, GetId());
    event->SetInt(n);
    event->SetString(GetString(n));
    event->SetExtraLong(selected);
    InitCommandEventWithItems(*event, n);
    GetEventHandler()->QueueEvent(event);
}
void wxListBox::HaikuSendEvent(wxEventType evtType, int rowIndex, bool selected)
{
    SendEvent(evtType, rowIndex, selected);
}

#endif // wxUSE_LISTBOX
