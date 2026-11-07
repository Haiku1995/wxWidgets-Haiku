#include "control_lock.h"
/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/notebook.cpp
// Purpose:     wxNotebook implementation for Haiku using BTabView
// Author:      Adapted for Haiku
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/notebook.h"
#include "wx/haiku/private.h"
#include "wx/image.h"
#include "wx/imaglist.h"
#include "wx/log.h"
#include <support/Autolock.h>
#include <TabView.h>
#include <View.h>
#include <Bitmap.h>
#include <TranslationUtils.h>
#include <GroupLayout.h>
#include <Looper.h>

#include <stdio.h>

// This file contains detailed native-view diagnostics useful while bringing
// up the port. They must never write to an application's stdout in a normal
// build, where wxWidgets diagnostics are opt-in (via wxLogTrace).
#ifndef WX_DEBUG_HAIKU
    #define printf(...) ((void)0)
#endif

namespace
{

class wxHaikuNotebookTabView : public BTabView
{
public:
    explicit wxHaikuNotebookTabView(wxNotebook* owner)
        : BTabView("notebook", B_WIDTH_FROM_LABEL), m_owner(owner) {}
    void Select(int32 index) override
    {
        if (m_internal || !m_owner || index < 0 ||
            static_cast<size_t>(index) >= m_owner->GetPageCount())
            BTabView::Select(index);
        else
            m_owner->SetSelection(index);
    }
    bool m_internal = false;
private:
    wxNotebook* m_owner;
};

class wxHaikuNotebookNativeUpdate
{
public:
    explicit wxHaikuNotebookNativeUpdate(BTabView* view)
        : m_view(static_cast<wxHaikuNotebookTabView*>(view)), m_old(m_view->m_internal)
        { m_view->m_internal = true; }
    ~wxHaikuNotebookNativeUpdate() { m_view->m_internal = m_old; }
private:
    wxHaikuNotebookTabView* m_view;
    bool m_old;
};

class wxHaikuAutoLockLooper
{
public:
    explicit wxHaikuAutoLockLooper(BLooper* looper, const char* where)
        : m_looper(looper),
          m_locked(false),
          m_where(where ? where : "?")
    {
        printf("[wxNotebook::%s] lock attempt looper=%p\n", m_where, m_looper);

        if (m_looper)
            m_locked = m_looper->Lock();

        printf("[wxNotebook::%s] lock result=%d\n", m_where, m_locked ? 1 : 0);
    }

    ~wxHaikuAutoLockLooper()
    {
        if (m_locked && m_looper)
        {
            printf("[wxNotebook::%s] unlock looper=%p\n", m_where, m_looper);
            m_looper->Unlock();
        }
    }

    bool IsLocked() const { return m_locked; }

private:
    BLooper* m_looper;
    bool m_locked;
    const char* m_where;
};

#include <support/Autolock.h>

static void wxNotebookTraceTabHierarchy(const char* where, BTabView* tabView)
{
    wxUnusedVar(where);
    if (!tabView)
    {
        printf("[wxNotebook::%s] tabView=(nil)\n", where ? where : "?");
        return;
    }

    // Attempt to lock the window looper if the view is attached
    BAutolock lock(tabView->Looper());
    
    // Note: if tabView is not attached to a window yet, Looper() is NULL, 
    // and lock.IsLocked() will be false. In Haiku, you can query 
    // unattached views without a lock.
    bool isAttached = (tabView->Looper() != nullptr);
    if (isAttached && !lock.IsLocked())
    {
        printf("[wxNotebook::%s] SKIPPING TRACE: Could not lock looper\n", where);
        return;
    }

    printf("[wxNotebook::%s] tabView=%p parent=%p window=%p childCount=%ld container=%p\n",
           where ? where : "?",
           tabView,
           tabView->Parent(),
           tabView->Window(),
           (long)tabView->CountChildren(),
           tabView->ContainerView());

    for (int32 i = 0; i < tabView->CountChildren(); ++i)
    {
        BView* child = tabView->ChildAt(i);
        if (!child)
        {
            printf("[wxNotebook::%s] child[%ld]=(nil)\n",
                   where ? where : "?", (long)i);
            continue;
        }

        const BRect f = child->Frame();
        const BRect b = child->Bounds();
        wxUnusedVar(f);
        wxUnusedVar(b);

        printf("[wxNotebook::%s] child[%ld]=%p parent=%p window=%p hidden=%d "
               "frame=(%.1f,%.1f,%.1f,%.1f) size=(%.1f x %.1f) "
               "bounds=(%.1f,%.1f,%.1f,%.1f) boundsSize=(%.1f x %.1f)\n",
               where ? where : "?",
               (long)i,
               child,
               child->Parent(),
               child->Window(),
               child->IsHidden() ? 1 : 0,
               f.left, f.top, f.right, f.bottom,
               f.Width() + 1.0f, f.Height() + 1.0f,
               b.left, b.top, b.right, b.bottom,
               b.Width() + 1.0f, b.Height() + 1.0f);
    }
}


static BLooper* wxNotebookGetLooper(const BView* view)
{
    return view ? view->Looper() : nullptr;
}

static void wxNotebookTraceView(const char* where, const char* label, BView* view)
{
    wxUnusedVar(where);
    wxUnusedVar(label);
    if (!view)
    {
        printf("[wxNotebook::%s] %s=(nil)\n", where ? where : "?", label ? label : "view");
        return;
    }

    // FIX: Accessing Frame() or Bounds() on an attached view requires a lock
    BAutolock lock(view->Looper());
    
    // If the view is attached to a window but we failed to lock it, skip
    if (view->Window() && !lock.IsLocked())
    {
        printf("[wxNotebook::%s] %s [LOCKED - SKIPPING]\n", where, label);
        return;
    }

    const BRect f = view->Frame();
    const BRect b = view->Bounds();
    wxUnusedVar(f);
    wxUnusedVar(b);

    printf("[wxNotebook::%s] %s=%p parent=%p window=%p hidden=%d "
           "frame=(%.1f,%.1f,%.1f,%.1f) size=(%.1f x %.1f) "
           "bounds=(%.1f,%.1f,%.1f,%.1f) boundsSize=(%.1f x %.1f)\n",
           where ? where : "?",
           label ? label : "view",
           view,
           view->Parent(),
           view->Window(),
           view->IsHidden() ? 1 : 0,
           f.left, f.top, f.right, f.bottom,
           f.Width() + 1.0f, f.Height() + 1.0f,
           b.left, b.top, b.right, b.bottom,
           b.Width() + 1.0f, b.Height() + 1.0f);
}

} // namespace

// ----------------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------------

BView* wxNotebook::GetTabView() const
{
    return m_tabView ? m_tabView->ContainerView() : nullptr;
}

// Important: notebook children must attach to the *container* view, not the
// BTabView itself, otherwise the pages paint over the tabs or appear as direct
// children of the tab view.
//
// If your notebook class declaration does not already have this virtual in the
// header, add:
//     virtual BView* HaikuGetNativeParentViewForChildren() const override;
BView* wxNotebook::HaikuGetNativeParentViewForChildren() const
{
    BView* container = GetTabView();
    if (container)
    {
        printf("[wxNotebook::HaikuGetNativeParentViewForChildren] returning container=%p\n",
               container);
        return container;
    }

    printf("[wxNotebook::HaikuGetNativeParentViewForChildren] falling back to tabView=%p\n",
           m_tabView);

    return m_tabView;
}

wxWindow* wxNotebook::GetBestSizeSource() const
{
    wxWindow* win = const_cast<wxNotebook*>(this);
    wxWindow* best = win;

    while (win)
    {
        const wxSize sz = win->GetSize();
        const wxSize bestSz = best->GetSize();

        if (sz.x > bestSz.x && sz.y > bestSz.y)
            best = win;

        win = win->GetParent();
    }

    return best;
}

// ----------------------------------------------------------------------------
// wxNotebook Implementation
// ----------------------------------------------------------------------------

wxNotebook::wxNotebook()
    : m_tabView(nullptr),
      m_selection(wxNOT_FOUND)
{
    printf("[wxNotebook::ctor] this=%p m_tabView=%p\n", this, m_tabView);
}

wxNotebook::wxNotebook(wxWindow* parent,
                       wxWindowID id,
                       const wxPoint& pos,
                       const wxSize& size,
                       long style,
                       const wxString& name)
    : m_tabView(nullptr),
      m_selection(wxNOT_FOUND)
{
    printf("[wxNotebook::ctor(args)] this=%p parent=%p\n", this, parent);
    Create(parent, id, pos, size, style, name);
}

wxNotebook::~wxNotebook()
{
    printf("[wxNotebook::~wxNotebook] ENTER this=%p m_tabView=%p pageCount=%zu\n",
           this, m_tabView, m_pages.size());

    if (m_tabView)
        SendDestroyEvent();
    DeleteAllPages();
    // RemovePage() keeps pages alive as children of this notebook.
    DestroyChildren();

    if (m_tabView)
    {
        BLooper* looper = wxNotebookGetLooper(m_tabView);
        wxHaikuAutoLockLooper lock(looper, "~wxNotebook");

        if (looper && !lock.IsLocked())
        {
            printf("[wxNotebook::~wxNotebook] failed to lock looper for m_tabView=%p\n",
                   m_tabView);
        }
        else
        {
            if (m_tabView->Parent())
            {
                printf("[wxNotebook::~wxNotebook] RemoveSelf m_tabView=%p parent=%p\n",
                       m_tabView, m_tabView->Parent());
                m_tabView->RemoveSelf();
            }
        }

        HaikuStoreWindowPointer(m_tabView, nullptr);
        HaikuAdoptView(nullptr);
        delete m_tabView;
        m_tabView = nullptr;
    }

    printf("[wxNotebook::~wxNotebook] EXIT this=%p\n", this);
}

void wxNotebook::UpdateNativeGeometry(const char* fromWhere)
{
    printf("[wxNotebook::%s] ENTER UpdateNativeGeometry this=%p m_tabView=%p\n",
           fromWhere ? fromWhere : "?", this, m_tabView);

    wxNotebookTraceTabHierarchy(fromWhere ? fromWhere : "UpdateNativeGeometry", m_tabView);

    if (!m_tabView)
        return;

    wxPoint pos = GetPosition();
    wxSize size = GetSize();

    const bool badSize = size.x <= 1 || size.y <= 1 || size.x <= 20 || size.y <= 20;

    printf("[wxNotebook::%s] initial wx pos=(%d,%d) size=(%d x %d) badSize=%d\n",
           fromWhere ? fromWhere : "?",
           pos.x, pos.y, size.x, size.y, badSize ? 1 : 0);

    if (badSize)
    {
        wxWindow* source = GetBestSizeSource();
        printf("[wxNotebook::%s] best size source=%p\n",
               fromWhere ? fromWhere : "?", source);

        if (source)
        {
            const wxSize srcSize = source->GetClientSize();
            printf("[wxNotebook::%s] best source client=(%d x %d)\n",
                   fromWhere ? fromWhere : "?", srcSize.x, srcSize.y);

            if (srcSize.x > 1 && srcSize.y > 1)
            {
                if (pos.x < 0)
                    pos.x = 0;
                if (pos.y < 0)
                    pos.y = 0;

                size = srcSize;
            }
        }
    }

    BLooper* looper = wxNotebookGetLooper(m_tabView);
    wxHaikuAutoLockLooper lock(looper, fromWhere ? fromWhere : "UpdateNativeGeometry");

    if (looper && !lock.IsLocked())
    {
        printf("[wxNotebook::%s] failed to lock looper for m_tabView=%p\n",
               fromWhere ? fromWhere : "?", m_tabView);
        return;
    }

    m_tabView->MoveTo(wxToBPoint(pos));
    m_tabView->ResizeTo((float)size.x, (float)size.y);

    wxNotebookTraceView(fromWhere, "tabView after Move/Resize", m_tabView);

    BView* container = GetTabView();
    wxNotebookTraceView(fromWhere, "container before manual resize", container);

    if (container)
    {
        const BRect tabBounds = m_tabView->Bounds();

        // Do not let the page container cover the tab strip.
        float tabHeight = 24.0f;
#if defined(__HAIKU__)
        const float nativeTabHeight = m_tabView->TabHeight();
        if (nativeTabHeight > 0.0f)
            tabHeight = nativeTabHeight;
#endif

        float containerTop = tabBounds.top + tabHeight + 1.0f;
        if (containerTop > tabBounds.bottom)
            containerTop = tabBounds.top;

        const float containerWidth = tabBounds.Width();
        float containerHeight = tabBounds.bottom - containerTop;
        if (containerHeight < 0.0f)
            containerHeight = 0.0f;

        printf("[wxNotebook::%s] manual container resize container=%p "
               "tabBounds=(%.1f,%.1f,%.1f,%.1f) tabHeight=%.1f "
               "target=(%.1f,%.1f %.1f x %.1f)\n",
               fromWhere ? fromWhere : "?",
               container,
               tabBounds.left, tabBounds.top, tabBounds.right, tabBounds.bottom,
               tabHeight,
               tabBounds.left, containerTop, containerWidth + 1.0f, containerHeight + 1.0f);

        container->MoveTo(BPoint(tabBounds.left, containerTop));
        container->ResizeTo(containerWidth, containerHeight);

        wxNotebookTraceView(fromWhere, "container after manual resize", container);
    }

    wxNotebookTraceTabHierarchy(fromWhere ? fromWhere : "UpdateNativeGeometry-after", m_tabView);

    printf("[wxNotebook::%s] EXIT UpdateNativeGeometry\n",
           fromWhere ? fromWhere : "?");
    printf("--------------------------------------------------\n");
}

void wxNotebook::UpdatePageSize(wxWindow* page)
{
    printf("[wxNotebook::UpdatePageSize] ENTER this=%p page=%p m_tabView=%p\n",
           this, page, m_tabView);

    if (!page || !m_tabView)
        return;

    BRect r;
    bool usedContainer = false;

    {
        BView* container = GetTabView();
        BLooper* looper = wxNotebookGetLooper(m_tabView);
        wxHaikuAutoLockLooper lock(looper, "UpdatePageSize");

        if (looper && !lock.IsLocked())
        {
            printf("[wxNotebook::UpdatePageSize] failed to lock looper for m_tabView=%p\n",
                   m_tabView);
            return;
        }

        wxNotebookTraceTabHierarchy("UpdatePageSize", m_tabView);
        wxNotebookTraceView("UpdatePageSize", "tabView", m_tabView);
        wxNotebookTraceView("UpdatePageSize", "container", container);

        if (container)
        {
            const BRect cb = container->Bounds();
            if (cb.Width() >= 0 && cb.Height() >= 0)
            {
                r = cb;
                usedContainer = true;
            }
        }

        if (!usedContainer)
            r = m_tabView->Bounds();
    }

    if (!usedContainer)
    {
        const float tabHeight = 24.0f;
        r.top += tabHeight;
    }

    const int w = static_cast<int>(r.Width()) + 1;
    const int h = static_cast<int>(r.Height()) + 1;

    printf("[wxNotebook::UpdatePageSize] usedContainer=%d adjusted size=(%d x %d)\n",
           usedContainer ? 1 : 0, w, h);

    if (w > 0 && h > 0)
        page->SetSize(0, 0, w, h);

    printf("[wxNotebook::UpdatePageSize] page=%p size=(%d x %d)\n",
           page, page->GetSize().x, page->GetSize().y);
}

void wxNotebook::UpdateAllPagesSize()
{
    printf("[wxNotebook::UpdateAllPagesSize] ENTER this=%p pageCount=%zu\n",
           this, m_pages.size());

    for (size_t i = 0; i < m_pages.size(); ++i)
    {
        printf("[wxNotebook::UpdateAllPagesSize] sizing page[%zu]=%p\n", i, m_pages[i]);
        UpdatePageSize(m_pages[i]);
    }

    printf("[wxNotebook::UpdateAllPagesSize] EXIT\n");
}

bool wxNotebook::Create(wxWindow* parent,
                        wxWindowID id,
                        const wxPoint& pos,
                        const wxSize& size,
                        long style,
                        const wxString& name)
{
    printf("[wxNotebook::Create] ENTER this=%p parent=%p id=%d pos=(%d,%d) size=(%d x %d) style=0x%lx name='%s'\n",
           this, parent, (int)id, pos.x, pos.y, size.x, size.y,
           (unsigned long)style, name.utf8_str().data());

    m_selection = wxNOT_FOUND;
    m_pageImages.clear();

    m_tabView = new wxHaikuNotebookTabView(this);
    if (!m_tabView)
    {
        printf("[wxNotebook::Create] BTabView allocation failed\n");
        return false;
    }

    wxNotebookTraceTabHierarchy("Create-new", m_tabView);

    printf("[wxNotebook::Create] created m_tabView=%p\n", m_tabView);

    m_tabView->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

    BView* container = GetTabView();
    if (container)
    {
        container->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
        printf("[wxNotebook::Create] container=%p view color set only\n", container);
    }

    // Make the notebook native handle be the real BTabView.
    HaikuAdoptView(m_tabView, nullptr, m_tabView);

    if (!wxControl::Create(parent, id, pos, size, style, wxDefaultValidator, name))
    {
        printf("[wxNotebook::Create] wxControl::Create failed\n");
        return false;
    }

    wxNotebookTraceView("Create", "tabView after wxControl::Create", m_tabView);
    wxNotebookTraceView("Create", "container after wxControl::Create", GetTabView());

    BView* parentHandle = GetParent() ? GetParent()->GetHandle() : nullptr;
    if (!parentHandle)
    {
        printf("[wxNotebook::Create] missing parent handle\n");
        return false;
    }

    if (!m_tabView->Parent())
    {
        BLooper* looper = wxNotebookGetLooper(parentHandle);
        wxHaikuAutoLockLooper lock(looper, "Create-AddChild");

        if (looper && !lock.IsLocked())
        {
            printf("[wxNotebook::Create] failed to lock parent looper parentHandle=%p\n",
                   parentHandle);
            return false;
        }

        parentHandle->AddChild(m_tabView);
        printf("[wxNotebook::Create] AddChild parentHandle=%p m_tabView=%p newParent=%p\n",
               parentHandle, m_tabView, m_tabView->Parent());
    }

    wxNotebookTraceView("Create", "tabView after attach", m_tabView);
    wxNotebookTraceView("Create", "container after attach", GetTabView());

    InvalidateBestSize();
    UpdateNativeGeometry("Create");

    CallAfter([this]()
    {
        printf("[wxNotebook::CallAfter-Create] ENTER this=%p m_tabView=%p\n", this, m_tabView);

        if (!this || !m_tabView)
            return;

        wxNotebookTraceView("CallAfter-Create", "tabView", m_tabView);
        wxNotebookTraceView("CallAfter-Create", "container", GetTabView());

        UpdateNativeGeometry("CallAfter-Create");
        UpdateAllPagesSize();
    });

    printf("[wxNotebook::Create] EXIT success\n");
    return true;
}

void wxNotebook::DoMoveWindow(int x, int y, int width, int height)
{
    printf("[wxNotebook::DoMoveWindow] ENTER this=%p x=%d y=%d w=%d h=%d\n",
           this, x, y, width, height);

    wxNotebookBase::DoMoveWindow(x, y, width, height);

    UpdateNativeGeometry("DoMoveWindow");
    UpdateAllPagesSize();

    printf("[wxNotebook::DoMoveWindow] EXIT\n");
}

void wxNotebook::SetPadding(const wxSize& WXUNUSED(padding))
{
}

void wxNotebook::SetTabSize(const wxSize& WXUNUSED(sz))
{
}

int wxNotebook::GetSelection() const
{
    if (!m_tabView)
        return wxNOT_FOUND;

    if (m_selection < 0 || m_selection >= static_cast<int>(m_pages.size()))
        return wxNOT_FOUND;

    return m_selection;
}

void wxNotebook::UpdateSelectionAfterInsert(size_t n, bool select)
{
    if (m_selection == wxNOT_FOUND)
    {
        ChangeSelection(n);
        return;
    }
    if (m_selection >= static_cast<int>(n))
        ++m_selection;
    if (select)
        SetSelection(n);
}

// BTab owns its content view; wx owns the page. Keep these lifetimes separate.
class wxHaikuNotebookPageContainer : public BView
{
public:
    explicit wxHaikuNotebookPageContainer(BRect bounds)
        : BView(bounds, "wxNotebookPageContainer", B_FOLLOW_ALL_SIDES, B_WILL_DRAW)
    {
        SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
    }
    ~wxHaikuNotebookPageContainer() override
    {
        // RemovePage() returns a live wx page, so never delete it with the tab.
        while (BView* child = ChildAt(0))
            child->RemoveSelf();
    }
};
bool wxNotebook::InsertPage(size_t n, wxWindow* page, const wxString& text,
                            bool bSelect, int imageId)
{
    wxCHECK_MSG(m_tabView, false, "invalid notebook");
    wxCHECK_MSG(page, false, "null page");
    wxCHECK_MSG(page->GetParent() == this, false,
                "Can't add a page whose parent is not the notebook!");

    const size_t pageCount = GetPageCount();
    wxCHECK_MSG(n <= pageCount, false, "invalid page index");

    BView* childView = page->GetHandle();
    if (!childView)
        return false;

    printf("[wxNotebook::InsertPage] ENTER n=%zu pageCount=%zu page=%p childView=%p parent=%p text='%s' bSelect=%d imageId=%d\n",
           n, pageCount, page, childView, childView->Parent(),
           text.utf8_str().data(), bSelect ? 1 : 0, imageId);

    wxNotebookTraceTabHierarchy("InsertPage", m_tabView);
    wxNotebookTraceView("InsertPage", "tabView before lock", m_tabView);
    wxNotebookTraceView("InsertPage", "container before lock", GetTabView());
    wxNotebookTraceView("InsertPage", "child before lock", childView);

    {
        BLooper* looper = wxNotebookGetLooper(m_tabView);
        wxHaikuAutoLockLooper lock(looper, "InsertPage");

        if (looper && !lock.IsLocked())
        {
            printf("[wxNotebook::InsertPage] failed to lock looper for m_tabView=%p\n",
                   m_tabView);
            return false;
        }

        wxHaikuNotebookNativeUpdate nativeUpdate(m_tabView);
        if (childView->Parent())
        {
            printf("[wxNotebook::InsertPage] RemoveSelf childView=%p oldParent=%p\n",
                   childView, childView->Parent());
            childView->RemoveSelf();
        }

        // BTabView appends only. Temporarily detach the trailing tabs,
        // append the new page, then restore the same tab/view objects.
        std::vector<BTab*> trailing;
        for (size_t i = n; i < pageCount; ++i)
            trailing.push_back(m_tabView->RemoveTab(static_cast<int32>(n)));
        BView* content = new wxHaikuNotebookPageContainer(GetTabView()->Bounds());
        content->AddChild(childView);
        childView->MoveTo(0, 0);
        m_tabView->AddTab(content);
        for (BTab* existing : trailing)
            m_tabView->AddTab(existing->View(), existing);

        printf("[wxNotebook::InsertPage] after AddTab child parent=%p tabCount=%ld\n",
               childView->Parent(), (long)m_tabView->CountTabs());

        wxNotebookTraceView("InsertPage", "tabView after AddTab", m_tabView);
        wxNotebookTraceView("InsertPage", "container after AddTab", GetTabView());
        wxNotebookTraceView("InsertPage", "child after AddTab", childView);

        BTab* tab = m_tabView->TabAt(static_cast<int32>(n));
        if (!tab)
        {
            printf("[wxNotebook::InsertPage] TabAt failed for index=%zu\n", pageCount);
            return false;
        }

        tab->SetLabel(wxToBStr(text));
    }

    m_pages.insert(m_pages.begin() + n, page);
    m_pageImages.insert(m_pageImages.begin() + n, imageId);

    printf("[wxNotebook::InsertPage] vectors updated m_pages.size()=%zu m_pageImages.size()=%zu\n",
           m_pages.size(), m_pageImages.size());

    UpdateNativeGeometry("InsertPage");
    UpdateAllPagesSize();

    // Inserting before the current page changes its index, not its identity.
    UpdateSelectionAfterInsert(n, bSelect);
    {
        BLooper* looper = wxNotebookGetLooper(m_tabView);
        wxHaikuAutoLockLooper lock(looper, "InsertPage-restore-selection");
        if ((!looper || lock.IsLocked()) && m_selection != wxNOT_FOUND)
            m_tabView->BTabView::Select(m_selection);
    }

    UpdateNativeGeometry("InsertPage-after-selection");
    UpdateAllPagesSize();

    CallAfter([this]()
    {
        printf("[wxNotebook::CallAfter-InsertPage] ENTER this=%p m_tabView=%p\n", this, m_tabView);

        if (!this || !m_tabView)
            return;

        wxNotebookTraceView("CallAfter-InsertPage", "tabView", m_tabView);
        wxNotebookTraceView("CallAfter-InsertPage", "container", GetTabView());

        UpdateNativeGeometry("CallAfter-InsertPage");
        UpdateAllPagesSize();
    });

    printf("[wxNotebook::InsertPage] EXIT success\n");
    return true;
}

wxSize wxNotebook::CalcSizeFromPage(const wxSize& sizePage) const
{
    const int tabHeight = 30;
    const int border = 4;
    return wxSize(sizePage.x + border, sizePage.y + tabHeight + border);
}

bool wxNotebook::DeleteAllPages()
{
    printf("[wxNotebook::DeleteAllPages] ENTER this=%p m_tabView=%p\n", this, m_tabView);

    if (!m_tabView)
        return true;

    while (!m_pages.empty())
    {
        if (!DeletePage(m_pages.size() - 1))
            return false;
    }
    m_selection = wxNOT_FOUND;
    return true;
}

wxWindow* wxNotebook::DoRemovePage(size_t page)
{
    printf("[wxNotebook::DoRemovePage] ENTER this=%p page=%zu pageCount=%zu\n",
           this, page, m_pages.size());

    if (!m_tabView || page >= m_pages.size())
        return nullptr;

    BTab* tab = nullptr;

    {
        BLooper* looper = wxNotebookGetLooper(m_tabView);
        wxHaikuAutoLockLooper lock(looper, "DoRemovePage");

        if (looper && !lock.IsLocked())
        {
            printf("[wxNotebook::DoRemovePage] failed to lock looper\n");
            return nullptr;
        }

        wxHaikuNotebookNativeUpdate nativeUpdate(m_tabView);
        tab = m_tabView->RemoveTab(static_cast<int32>(page));
        if (!tab)
        {
            printf("[wxNotebook::DoRemovePage] RemoveTab failed index=%zu\n", page);
            return nullptr;
        }
    }

    const int oldSelection = m_selection;
    wxWindow* win = m_pages[page];
    printf("[wxNotebook::DoRemovePage] removed tab=%p win=%p\n", tab, win);

    delete tab;

    wxNotebookBase::DoRemovePage(page);
    m_pageImages.erase(m_pageImages.begin() + page);

    UpdateSelectionAfterRemove(page);
    if (m_selection != wxNOT_FOUND)
    {
        wxHaikuAutoLockLooper lock(wxNotebookGetLooper(m_tabView), "RemoveSelection");
        if (!m_tabView->Looper() || lock.IsLocked())
            m_tabView->BTabView::Select(m_selection);
    }
    if (!IsBeingDeleted() && oldSelection >= static_cast<int>(page) && m_selection != wxNOT_FOUND)
    {
        wxNotebookEvent changed(wxEVT_NOTEBOOK_PAGE_CHANGED, GetId());
        changed.SetSelection(m_selection);
        changed.SetOldSelection(oldSelection);
        changed.SetEventObject(this);
        ProcessWindowEvent(changed);
    }
    UpdateNativeGeometry("DoRemovePage");
    UpdateAllPagesSize();

    printf("[wxNotebook::DoRemovePage] EXIT\n");
    return win;
}

void wxNotebook::UpdateSelectionAfterRemove(size_t removedPage)
{
    printf("[wxNotebook::UpdateSelectionAfterRemove] removedPage=%zu current=%d pageCount=%zu\n",
           removedPage, m_selection, m_pages.size());

    if (m_pages.empty())
    {
        m_selection = wxNOT_FOUND;
        return;
    }

    if (m_selection == wxNOT_FOUND)
        return;

    if (m_selection > static_cast<int>(removedPage))
    {
        --m_selection;
    }
    else if (m_selection == static_cast<int>(removedPage))
    {
        if (removedPage < m_pages.size())
            m_selection = static_cast<int>(removedPage);
        else
            m_selection = static_cast<int>(m_pages.size() - 1);
    }

    if (m_selection < 0 || m_selection >= static_cast<int>(m_pages.size()))
        m_selection = wxNOT_FOUND;

    printf("[wxNotebook::UpdateSelectionAfterRemove] new selection=%d\n", m_selection);
}

wxString wxNotebook::GetPageText(size_t n) const
{
    if (!m_tabView)
        return wxEmptyString;

    BLooper* looper = wxNotebookGetLooper(m_tabView);
    wxHaikuAutoLockLooper lock(looper, "GetPageText");

    if (looper && !lock.IsLocked())
    {
        printf("[wxNotebook::GetPageText] failed to lock looper\n");
        return wxEmptyString;
    }

    if (n >= static_cast<size_t>(m_tabView->CountTabs()))
        return wxEmptyString;

    BTab* tab = m_tabView->TabAt(static_cast<int32>(n));
    if (!tab)
        return wxEmptyString;

    return wxString::FromUTF8(tab->Label());
}

bool wxNotebook::SetPageText(size_t n, const wxString& text)
{
    printf("[wxNotebook::SetPageText] ENTER n=%zu text='%s'\n",
           n, text.utf8_str().data());

    if (!m_tabView)
        return false;

    BLooper* looper = wxNotebookGetLooper(m_tabView);
    wxHaikuAutoLockLooper lock(looper, "SetPageText");

    if (looper && !lock.IsLocked())
    {
        printf("[wxNotebook::SetPageText] failed to lock looper\n");
        return false;
    }

    if (n >= static_cast<size_t>(m_tabView->CountTabs()))
        return false;

    BTab* tab = m_tabView->TabAt(static_cast<int32>(n));
    if (!tab)
        return false;

    tab->SetLabel(wxToBStr(text));
    return true;
}

int wxNotebook::GetPageImage(size_t n) const
{
    if (n >= m_pageImages.size())
        return wxNOT_FOUND;

    return m_pageImages[n];
}

bool wxNotebook::SetPageImage(size_t n, int imageId)
{
    wxHaikuControlLock lock(m_tabView);
    if (!lock.IsOk())
        return false;

    if (!m_tabView || n >= static_cast<size_t>(m_tabView->CountTabs()))
        return false;

    if (n >= m_pageImages.size())
        return false;

    m_pageImages[n] = imageId;
    return true;
}

void wxNotebook::OnImagesChanged()
{
}

int wxNotebook::SetSelection(size_t n)
{
    wxCHECK_MSG(m_tabView && n < m_pages.size(), wxNOT_FOUND, "invalid notebook index");
    const int old = GetSelection();
    if (old == static_cast<int>(n))
        return old;
    if (!IsBeingDeleted())
    {
        wxNotebookEvent changing(wxEVT_NOTEBOOK_PAGE_CHANGING, GetId());
        changing.SetSelection(n);
        changing.SetOldSelection(old);
        changing.SetEventObject(this);
        ProcessWindowEvent(changing);
        if (!changing.IsAllowed())
            return old;
    }
    ChangeSelection(n);
    if (!IsBeingDeleted())
    {
        wxNotebookEvent changed(wxEVT_NOTEBOOK_PAGE_CHANGED, GetId());
        changed.SetSelection(n);
        changed.SetOldSelection(old);
        changed.SetEventObject(this);
        ProcessWindowEvent(changed);
    }
    return old;
}

int wxNotebook::ChangeSelection(size_t n)
{
    wxCHECK_MSG(m_tabView && n < m_pages.size(), wxNOT_FOUND, "invalid notebook index");
    const int old = GetSelection();
    wxHaikuAutoLockLooper lock(wxNotebookGetLooper(m_tabView), "ChangeSelection");
    if (m_tabView->Looper() && !lock.IsLocked())
        return wxNOT_FOUND;
    m_tabView->BTabView::Select(n);
    m_selection = n;
    UpdateNativeGeometry("ChangeSelection");
    UpdateAllPagesSize();
    return old;
}

void wxNotebook::MessageReceived(BMessage* message)
{
    printf("[wxNotebook::MessageReceived] ENTER this=%p message=%p\n", this, message);

    if (!message)
        return;

    if (message->what == B_CURRENT_PAGE_CHANGED)
    {
        int32 selectedIndex = -1;
        if (message->FindInt32("be:tab", &selectedIndex) == B_OK)
        {
            printf("[wxNotebook::MessageReceived] B_CURRENT_PAGE_CHANGED selectedIndex=%ld current=%d\n",
                   (long)selectedIndex, m_selection);

            if (selectedIndex >= 0 &&
                selectedIndex < static_cast<int32>(m_pages.size()) &&
                selectedIndex != m_selection)
            {
                const int oldSel = m_selection;
                m_selection = static_cast<int>(selectedIndex);

                UpdateNativeGeometry("MessageReceived");
                UpdateAllPagesSize();

                wxNotebookEvent event(wxEVT_NOTEBOOK_PAGE_CHANGED, GetId());
                event.SetSelection(static_cast<int>(selectedIndex));
                event.SetOldSelection(oldSel);
                event.SetEventObject(this);
                ProcessWindowEvent(event);
            }
        }
    }
}
