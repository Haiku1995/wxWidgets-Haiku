/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/window.cpp
// Purpose:     wxWindow implementation for Haiku
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"
#include "wx/frame.h"
#include "wx/haiku/private/framewin.h"
#include "wx/haiku/private/dispatch.h"
#include <Messenger.h>

#ifdef __BORLANDC__
    #pragma hdrstop
#endif
   #include "wx/dnd.h"
#ifndef WX_PRECOMP
    #include "wx/window.h"
    #include "wx/log.h"
    #include "wx/utils.h"
    #include "wx/settings.h"
 
    #include "wx/menu.h"
    #include "wx/toplevel.h"
#endif


#include "wx/haiku/private/utils.h"
#include "wx/tooltip.h"

#include "wx/statbox.h"
#include "wx/textctrl.h"
#include "wx/weakref.h"
#include <vector>
#include <PopUpMenu.h>
#include <MenuItem.h>
#include "wx/menu.h"
#include <Application.h>
#include <View.h>
#include <Control.h>
#include <MenuField.h>
#include <TextView.h>
#include <MessageFilter.h>
#include <Window.h>
#include <WindowScreen.h>
#include <ScrollView.h>
#include <ScrollBar.h>
#include <Message.h>
#include <Picture.h>
#include <Font.h>
#include <OS.h>
#include <stdio.h>
#include "wx/haiku/private/viewmap.h"
#include "wx/haiku/private/windowview.h"
#include "wx/haiku/private/converter.h"

#include <unordered_map>
#include <mutex>

// ----------------------------------------------------------------------------
// Haiku Debug Logging Macro
// ----------------------------------------------------------------------------
#if defined(WX_DEBUG_HAIKU) && WX_DEBUG_HAIKU
    
#define HAIKU_LOG(msg, ...) \
    printf("[HAIKU-WINDOW:%s:%d] " msg "\n", __func__, __LINE__, ##__VA_ARGS__)
#else
    #define HAIKU_LOG(msg, ...) ((void)0)
#endif

inline const char* wxToUTF8(const wxString& str) { return str.utf8_str(); }
static void HaikuDumpViewHierarchy(const char* where, BView* v)
{
#if defined(WX_DEBUG_HAIKU) && WX_DEBUG_HAIKU
    if (!v)
    {
        printf("[HAIKU-WINDOW:%s] view=(nil)\n", where ? where : "?");
        return;
    }

    const BRect f = v->Frame();
    const BRect b = v->Bounds();

    printf("[HAIKU-WINDOW:%s] view=%p parent=%p window=%p hidden=%d childCount=%ld "
           "frame=(%.1f,%.1f,%.1f,%.1f) size=(%.1f x %.1f) "
           "bounds=(%.1f,%.1f,%.1f,%.1f) boundsSize=(%.1f x %.1f)\n",
           where ? where : "?",
           v,
           v->Parent(),
           v->Window(),
           v->IsHidden() ? 1 : 0,
           (long)v->CountChildren(),
           f.left, f.top, f.right, f.bottom,
           f.Width() + 1.0f, f.Height() + 1.0f,
           b.left, b.top, b.right, b.bottom,
           b.Width() + 1.0f, b.Height() + 1.0f);

    for (int32 i = 0; i < v->CountChildren(); ++i)
    {
        BView* child = v->ChildAt(i);
        if (!child)
        {
            printf("[HAIKU-WINDOW:%s]   child[%ld]=(nil)\n",
                   where ? where : "?", (long)i);
            continue;
        }

        const BRect cf = child->Frame();
        const BRect cb = child->Bounds();

        printf("[HAIKU-WINDOW:%s]   child[%ld]=%p parent=%p window=%p hidden=%d "
               "frame=(%.1f,%.1f,%.1f,%.1f) size=(%.1f x %.1f) "
               "bounds=(%.1f,%.1f,%.1f,%.1f) boundsSize=(%.1f x %.1f)\n",
               where ? where : "?",
               (long)i,
               child,
               child->Parent(),
               child->Window(),
               child->IsHidden() ? 1 : 0,
               cf.left, cf.top, cf.right, cf.bottom,
               cf.Width() + 1.0f, cf.Height() + 1.0f,
               cb.left, cb.top, cb.right, cb.bottom,
               cb.Width() + 1.0f, cb.Height() + 1.0f);
    }
#else
    wxUnusedVar(where);
    wxUnusedVar(v);
#endif
}
static void HaikuDumpView(const char* label, BView* v)
{

    wxUnusedVar(label);
    wxUnusedVar(v);
}

static void HaikuDumpWindow(const char* label, BWindow* w)
{
    wxUnusedVar(label);
    wxUnusedVar(w);

}

namespace
{
    class wxHaikuViewLock
    {
    public:
        explicit wxHaikuViewLock(BView* view)
            : m_window(view ? view->Window() : nullptr),
              m_locked(false)
        {
            if (m_window && m_window->LockingThread() != find_thread(nullptr))
                m_locked = m_window->LockLooper();
        }

        ~wxHaikuViewLock()
        {
            if (m_locked && m_window)
                m_window->UnlockLooper();
        }

        bool ok() const
        {
            return !m_window || m_locked ||
                   m_window->LockingThread() == find_thread(nullptr);
        }

    private:
        BWindow* m_window;
        bool m_locked;
    };
}

class wxHaikuAutoLock
{
public:
    explicit wxHaikuAutoLock(BLooper* looper)
        : m_looper(looper), m_locked(false)
    {
        if (m_looper && m_looper->LockingThread() != find_thread(nullptr))
            m_locked = m_looper->Lock();
    }

    ~wxHaikuAutoLock()
    {
        if (m_locked)
            m_looper->Unlock();
    }

    bool ok() const
    {
        return !m_looper || m_locked ||
               m_looper->LockingThread() == find_thread(nullptr);
    }

private:
    BLooper* m_looper;
    bool m_locked;
};

// ----------------------------------------------------------------------------
// Capture tracking
// ----------------------------------------------------------------------------
static wxWindowHaiku* s_capturedWindow = nullptr;

// A modal callback can delay its parent's deactivation message. Keep a
// token-checked reference to the window whose activation arrived most recently.
static BMessenger s_activeNativeWindow;

// ----------------------------------------------------------------------------
// Parent native view resolver
// ----------------------------------------------------------------------------
// Haiku hit-tests siblings in list order. Decorative static boxes must follow
// interactive siblings, including controls using the older shared-parent form
// of wxStaticBoxSizer. Native children of a static box remain inside that box.
// Block native input even for composites which do not inherit BControl.
// Native target views are resolved to their closest wx wrapper so disabled
// ancestors, including modal owners, are respected without greying the TLW.
class wxHaikuEnabledFilter : public BMessageFilter
{
public:
    wxHaikuEnabledFilter() : BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE) {}
    filter_result Filter(BMessage* message, BHandler** target) override
    {
        switch (message->what)
        {
            case B_MOUSE_DOWN: case B_MOUSE_UP: case B_MOUSE_MOVED:
            case B_MOUSE_WHEEL_CHANGED: case B_KEY_DOWN: case B_KEY_UP:
            case B_UNMAPPED_KEY_DOWN: case B_UNMAPPED_KEY_UP:
                break;
            default:
#if wxUSE_DRAG_AND_DROP
                if (message->WasDropped()) break;
#endif
                return B_DISPATCH_MESSAGE;
        }
        BMessenger destination(*target);
        wxHaikuGuiDispatchLock guard(BLooper::LooperForThread(find_thread(nullptr)));
        if (*target)
        {
            BHandler* const resolved = destination.Target(nullptr);
            if (!resolved || resolved->Looper() != BLooper::LooperForThread(find_thread(nullptr)))
                return B_SKIP_MESSAGE;
            *target = resolved;
        }
        BView* view = dynamic_cast<BView*>(*target);
        if (!view)
            if (BWindow* window = dynamic_cast<BWindow*>(*target))
                view = window->ChildAt(0);
        for (; view; view = view->Parent())
            if (wxWindowHaiku* owner = wxWindowHaiku::HaikuRetrieveWindowPointer(view))
            {
                if (!owner->IsEnabled()) return B_SKIP_MESSAGE;
#if wxUSE_DRAG_AND_DROP
                if (owner->GetDropTarget())
                {
                    BView* dropView = owner->GetHandle();
                    if (message->WasDropped())
                    {
                        owner->HaikuHandleDropMessage(dropView, message);
                        return B_SKIP_MESSAGE;
                    }
                    if (message->what == B_MOUSE_MOVED)
                    {
                        int32 transit = B_INSIDE_VIEW;
                        message->FindInt32("be:transit", &transit);
                        if (transit == B_EXITED_VIEW || transit == B_OUTSIDE_VIEW)
                            owner->HaikuHandleDragLeave(dropView);
                        else
                        {
                            BMessage drag;
                            if (message->FindMessage("be:drag_message", &drag) != B_OK)
                                return B_DISPATCH_MESSAGE;
                            BPoint point;
                            message->FindPoint("be:view_where", &point);
                            if (view != dropView)
                                point = dropView->ConvertFromScreen(view->ConvertToScreen(point));
                            owner->HaikuHandleDragOver(dropView, &drag, point);
                        }
                        if (message->HasMessage("be:drag_message")) return B_SKIP_MESSAGE;
                    }
                }
#endif
                return B_DISPATCH_MESSAGE;
            }
        return B_DISPATCH_MESSAGE;
    }
};

static void wxHaikuEnsureEnabledFilter(BWindow* window)
{
    if (!window)
        return;
    BList* filters = window->CommonFilterList();
    if (filters)
        for (int32 i = 0; i < filters->CountItems(); ++i)
            if (dynamic_cast<wxHaikuEnabledFilter*>(static_cast<BMessageFilter*>(filters->ItemAt(i))))
                return;
    window->AddCommonFilter(new wxHaikuEnabledFilter);
}
// BView Show/Hide maintain a counter, not a boolean. Inspect only this
// view's own state: a hidden ancestor must not cause an extra Show/Hide.
static void wxHaikuSetNativeShown(BView* view, bool shown)
{
    const bool hidden = view->IsHidden(view);
    if (shown && hidden)
        view->Show();
    else if (!shown && !hidden)
        view->Hide();
}
static void wxHaikuAddNativeChild(BView* parent, BView* child)
{
    BView* before = nullptr;
#if wxUSE_STATBOX
    if (!dynamic_cast<wxStaticBox*>(wxWindowHaiku::HaikuRetrieveWindowPointer(child)))
    {
        for (BView* sibling = parent->ChildAt(0); sibling; sibling = sibling->NextSibling())
        {
            if (dynamic_cast<wxStaticBox*>(wxWindowHaiku::HaikuRetrieveWindowPointer(sibling)))
            {
                before = sibling;
                break;
            }
        }
    }
#endif
    parent->AddChild(child, before);
#if wxUSE_STATBOX
    // AddChild(before) updates the client-side sibling list, but Haiku's
    // AS_VIEW_CREATE message carries no sibling position. The server appends
    // the new view, leaving an older static box in front for mouse hit tests.
    // Reattach the trailing decorative views in their client-side order so
    // the app_server sees the same order as BWindow::FindView().
    if (before && parent->Window())
    {
        BView* last = before;
        while (last->NextSibling())
            last = last->NextSibling();
        for (BView* sibling = before; sibling; )
        {
            BView* next = sibling == last ? nullptr : sibling->NextSibling();
            sibling->RemoveSelf();
            parent->AddChild(sibling);
            sibling = next;
        }
    }
#endif
    wxHaikuEnsureEnabledFilter(parent->Window());
}
BView* HaikuGetNativeParentViewForChildren(wxWindow* parent)
{
    if (!parent)
        return nullptr;

    if (wxTopLevelWindowHaiku* tlw = wxDynamicCast(parent, wxTopLevelWindowHaiku))
        return tlw->HaikuGetParentView();

    if (wxWindowHaiku* wh = wxDynamicCast(parent, wxWindowHaiku))
        return wh->GetHaikuParentViewForChildren();

    return parent->GetHandle();
}

// ----------------------------------------------------------------------------
// wxHaikuShortcutHandler
// ----------------------------------------------------------------------------
#if wxUSE_ACCEL
#include <Handler.h>

class wxHaikuShortcutHandler : public BHandler
{
public:
    explicit wxHaikuShortcutHandler(wxWindowHaiku* handler)
        : BHandler("wxHaikuShortcutHandler"),
          m_handler(handler)
    {
    //    HAIKU_LOG("ShortcutHandler ctor this=%p handler=%p", this, handler);
    }

    void MessageReceived(BMessage* msg) override
    {
     //   HAIKU_LOG("ShortcutHandler MessageReceived this=%p msg=%p", this, msg);

        if (!msg || !m_handler)
            return;

        int32 command = -1;
        if (msg->FindInt32("wxHaiku_Command", &command) == B_OK && command != -1)
        {
         //   HAIKU_LOG("ShortcutHandler dispatch command=%ld handler=%p",
              //        (long)command, m_handler);
            m_handler->HaikuHandleShortcut((int)command);
        }
        else
        {
            BHandler::MessageReceived(msg);
        }
    }

private:
    wxWindowHaiku* const m_handler;
};
#endif // wxUSE_ACCEL

// ============================================================================
// wxWindowHaiku
// ============================================================================

void wxWindowHaiku::Init()
{
    m_haikuView = nullptr;
    m_haikuScrollView = nullptr;
    m_haikuDrawView = nullptr;
    m_mouseInside = false;
    m_pendingSize = false;
    m_pendingClientSize = wxDefaultSize;

#if wxUSE_ACCEL
    m_processingShortcut = false;
    m_haikuShortcutHandler.reset(new wxHaikuShortcutHandler(this));
#endif

 //   HAIKU_LOG("Init this=%p members initialized", (void*)this);
}

wxWindowHaiku::wxWindowHaiku()
{
 //   HAIKU_LOG("Default ctor ENTER");// this=%p", (void*)this);
    Init();
 //   HAIKU_LOG("Default ctor EXIT");// this=%p", (void*)this);
}

wxWindowHaiku::wxWindowHaiku(wxWindow* parent,
                             wxWindowID id,
                             const wxPoint& pos,
                             const wxSize& size,
                             long style,
                             const wxString& name)
{
    const wxScopedCharBuffer nameUtf8 = name.utf8_str();

  //  HAIKU_LOG("Param ctor ENTER this=%p parent=%p id=%d name='%s' style=0x%lx "
   //           "pos=(%d,%d) size=(%d,%d)",
   //           this, parent, (int)id,
    //         nameUtf8.data() ? nameUtf8.data() : "",
     //         (unsigned long)style,
      //        pos.x, pos.y, size.x, size.y);

    Init();
    bool result = Create(parent, id, pos, size, style, name);

   // HAIKU_LOG("Param ctor EXIT this=%p Create()=%d native=%p scroll=%p",
    //          (void*)this, result, m_haikuView, m_haikuScrollView);
}

wxWindowHaiku::~wxWindowHaiku()
{
 //   HAIKU_LOG("Dtor ENTER this=%p native=%p scroll=%p draw=%p parent=%p",
  //            this, m_haikuView, m_haikuScrollView, m_haikuDrawView, GetParent());

    HaikuDumpView("Dtor native view", m_haikuView);
    HaikuDumpView("Dtor scroll view", m_haikuScrollView);
    HaikuDumpView("Dtor draw view", m_haikuDrawView);

    if (!m_haikuView)
        return;

    if (s_capturedWindow == this)
        s_capturedWindow = nullptr;

    SendDestroyEvent();

#if wxUSE_DRAG_AND_DROP
    SetDropTarget(nullptr);
#endif

    DestroyChildren();

    HaikuStoreWindowPointer(m_haikuView, nullptr);
    // Adopted native controls, like generic views, belong to this wrapper.
    // Leaving them attached lets destroyed controls intercept later input.
    // Specialized owners clear the adopted view when they dispose it themselves.
    if (m_haikuView)
    {
        BView* native = HaikuGetNativeViewToAttach();
        BLooper* looper = native ? native->Looper() : nullptr;
        if (!looper || looper->Lock())
        {
            if (native)
            {
                native->RemoveSelf();
                delete native;
            }
            m_haikuView = nullptr;
            m_haikuDrawView = nullptr;
            m_haikuScrollView = nullptr;
            if (looper)
                looper->Unlock();
        }
    }

  //  HAIKU_LOG("Dtor EXIT this=%p", this);
}

bool wxWindowHaiku::Create(wxWindow* parent,
                           wxWindowID id,
                           const wxPoint& pos,
                           const wxSize& size,
                           long style,
                           const wxString& name)
{
    const wxScopedCharBuffer nameUtf8 = name.utf8_str();

  //  HAIKU_LOG("Create ENTER this=%p parent=%p id=%d name='%s' style=0x%lx "
   //           "pos=(%d,%d) size=(%d,%d)",
     //         (void*)this, (void*)parent, (int)id,
     //         nameUtf8.data() ? nameUtf8.data() : "",
     //        (unsigned long)style,
     //         pos.x, pos.y, size.x, size.y);

    bool isGeneric = false;

    wxPoint p = (pos == wxDefaultPosition) ? wxPoint(0, 0) : pos;

    // Use a SAFE temporary size for native creation.
    // Do not guess the final client size here.
    wxSize nativeSize = size;
    if (nativeSize == wxDefaultSize)
    {
        if (parent)
            nativeSize = wxSize(1, 1);
        else
            nativeSize = wxSize(320, 240);
    }

    if (nativeSize.x <= 0)
        nativeSize.x = 1;
    if (nativeSize.y <= 0)
        nativeSize.y = 1;

    //HAIKU_LOG("Create initial nativeSize=(%d,%d) handle=%p attach=%p parentNative=%p",
      //        nativeSize.x, nativeSize.y,
      //        (void*)GetHandle(),
      //        (void*)HaikuGetNativeViewToAttach(),
       //       (void*)(parent ? HaikuGetNativeParentViewForChildren(parent) : nullptr));

    HaikuDumpViewHierarchy("Create ENTRY native", m_haikuView);
    HaikuDumpViewHierarchy("Create ENTRY attach", HaikuGetNativeViewToAttach());

    if (GetHandle() == nullptr)
    {
        isGeneric = true;

        const BRect r((float)p.x, (float)p.y,
                      (float)(p.x + nativeSize.x - 1),
                      (float)(p.y + nativeSize.y - 1));

     //   HAIKU_LOG("Create generic native rect=(%.1f,%.1f,%.1f,%.1f) "
       //           "hscroll=%d vscroll=%d",
               //   r.left, r.top, r.right, r.bottom,
            //      (style & wxHSCROLL) != 0,
                //  (style & wxVSCROLL) != 0);

        if (style & (wxHSCROLL | wxVSCROLL))
        {
            m_haikuView = new wxHaikuWindowView(
                r, nameUtf8.data(),
                B_FOLLOW_ALL_SIDES,
                B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE);

            m_haikuScrollView = new BScrollView(
                nameUtf8.data(),
                m_haikuView,
                B_FOLLOW_ALL_SIDES,
                B_WILL_DRAW | B_FRAME_EVENTS,
                (style & wxHSCROLL) != 0,
                (style & wxVSCROLL) != 0);

            m_haikuDrawView = m_haikuView;
        }
        else
        {
            m_haikuView = new wxHaikuWindowView(
                r, nameUtf8.data(),
                B_FOLLOW_ALL_SIDES,
                B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE);

            m_haikuScrollView = nullptr;
            m_haikuDrawView = m_haikuView;
        }

  //      HAIKU_LOG("Create generic allocated isGeneric=%d native=%p scroll=%p draw=%p",
   //               isGeneric, (void*)m_haikuView, (void*)m_haikuScrollView, (void*)m_haikuDrawView);

        HaikuDumpViewHierarchy("Create generic native view", m_haikuView);
        HaikuDumpViewHierarchy("Create generic scroll view", m_haikuScrollView);
        HaikuDumpViewHierarchy("Create generic attach view", HaikuGetNativeViewToAttach());
    }
    else
    {
    //    HAIKU_LOG("Create using pre-existing native handle=%p scroll=%p draw=%p",
          //        (void*)m_haikuView, (void*)m_haikuScrollView, (void*)m_haikuDrawView);

        HaikuDumpViewHierarchy("Create pre-existing native BEFORE CreateBase", m_haikuView);
        HaikuDumpViewHierarchy("Create pre-existing attach BEFORE CreateBase", HaikuGetNativeViewToAttach());
    }

    HAIKU_LOG("Create BEFORE CreateBase this=%p native=%p attach=%p parent=%p parentNative=%p",
              (void*)this,
              (void*)m_haikuView,
              (void*)HaikuGetNativeViewToAttach(),
              (void*)parent,
              (void*)(parent ? HaikuGetNativeParentViewForChildren(parent) : nullptr));

    HaikuDumpViewHierarchy("Create BEFORE CreateBase native", m_haikuView);
    HaikuDumpViewHierarchy("Create BEFORE CreateBase attach", HaikuGetNativeViewToAttach());

    if (!wxWindowBase::CreateBase(parent, id, pos, size, style,
                                  wxDefaultValidator, name))
    {
        HAIKU_LOG("Create FAILED in CreateBase");
        return false;
    }

    // wxControlBase::Create() delegates to wxWindow::Create(), expecting the
    // platform implementation to register the new control in its wx parent.
    // CreateBase() only stores the parent pointer; without AddChild(), the
    // native view is visible but wxWidgets cannot see sibling relationships
    // (breaking, among other things, dialog button tab ordering).
    if (parent)
        parent->AddChild(this);

    HAIKU_LOG("Create AFTER CreateBase this=%p native=%p attach=%p parent=%p parentNative=%p",
              (void*)this,
              (void*)m_haikuView,
              (void*)HaikuGetNativeViewToAttach(),
              (void*)GetParent(),
              (void*)(GetParent() ? HaikuGetNativeParentViewForChildren(GetParent()) : nullptr));

    HaikuDumpViewHierarchy("Create AFTER CreateBase native", m_haikuView);
    HaikuDumpViewHierarchy("Create AFTER CreateBase attach", HaikuGetNativeViewToAttach());

  //  HAIKU_LOG("Create BEFORE StoreWindowPointer native=%p this=%p",
    //          (void*)m_haikuView, (void*)this);

    HaikuDumpViewHierarchy("Create BEFORE StoreWindowPointer native", m_haikuView);

    HaikuStoreWindowPointer(m_haikuView, this);

  //  HAIKU_LOG("Create AFTER StoreWindowPointer native=%p this=%p",
    //          (void*)m_haikuView, (void*)this);

    HaikuDumpViewHierarchy("Create AFTER StoreWindowPointer native", m_haikuView);
    HaikuDumpViewHierarchy("Create AFTER StoreWindowPointer attach", HaikuGetNativeViewToAttach());

    SetLayoutDirection(wxLayout_Default);

   // HAIKU_LOG("Create before PostCreation this=%p isGeneric=%d native=%p attach=%p parentNative=%p",
       //       (void*)this, isGeneric, (void*)m_haikuView,
       //       (void*)HaikuGetNativeViewToAttach(),
       //       (void*)(GetParent() ? HaikuGetNativeParentViewForChildren(GetParent()) : nullptr));

    HaikuDumpViewHierarchy("Create BEFORE PostCreation native", m_haikuView);
    HaikuDumpViewHierarchy("Create BEFORE PostCreation attach", HaikuGetNativeViewToAttach());

    PostCreation(isGeneric);

  //  HAIKU_LOG("Create AFTER PostCreation this=%p native=%p attach=%p scroll=%p draw=%p",
   //           (void*)this,
     //         (void*)m_haikuView,
          //   (void*)HaikuGetNativeViewToAttach(),
         //     (void*)m_haikuScrollView,
       //       (void*)m_haikuDrawView);

    HaikuDumpViewHierarchy("Create AFTER PostCreation native", m_haikuView);
    HaikuDumpViewHierarchy("Create AFTER PostCreation attach", HaikuGetNativeViewToAttach());

    // Now that the native child is attached, compute the REAL size.
    wxSize finalSize = size;
    if (finalSize == wxDefaultSize)
    {
        if (parent)
        {
            finalSize = parent->GetClientSize();

            if (p.x > 0)
                finalSize.x -= p.x;
            if (p.y > 0)
                finalSize.y -= p.y;
        }
        else
        {
            finalSize = nativeSize;
        }
    }

    if (finalSize.x <= 0)
        finalSize.x = 1;
    if (finalSize.y <= 0)
        finalSize.y = 1;

  //  HAIKU_LOG("Create before DoMoveWindow finalSize=(%d,%d) pos=(%d,%d)",
         //     finalSize.x, finalSize.y, p.x, p.y);

    HaikuDumpViewHierarchy("Create BEFORE DoMoveWindow native", m_haikuView);
    HaikuDumpViewHierarchy("Create BEFORE DoMoveWindow attach", HaikuGetNativeViewToAttach());

    // Composite controls create their children after this base creation.
    // Initialize native geometry without calling their layout override yet.
    wxWindowHaiku::DoMoveWindow(p.x,p.y,finalSize.x,finalSize.y);

    HaikuDumpViewHierarchy("Create AFTER DoMoveWindow native", m_haikuView);
    HaikuDumpViewHierarchy("Create AFTER DoMoveWindow attach", HaikuGetNativeViewToAttach());

  //  HAIKU_LOG("Create EXIT this=%p native=%p scroll=%p draw=%p final=(%d,%d)",
      //        (void*)this, (void*)m_haikuView, (void*)m_haikuScrollView,
       //       (void*)m_haikuDrawView, finalSize.x, finalSize.y);
    return true;
}
void wxWindowHaiku::HaikuAttachNativeChild()
{
    HAIKU_LOG("AttachNativeChild this=%p native=%p parent=%p",
              this, m_haikuView, GetParent());

    BView* childNative = HaikuGetNativeViewToAttach();
    BView* parentNative = HaikuGetNativeParentViewForChildren(GetParent());

    HAIKU_LOG("AttachNativeChild childNative=%p parentNative=%p",
              childNative, parentNative);

    if (!childNative || !parentNative)
        return;

    BWindow* win = parentNative->Window();

    if (win && win->LockLooper())
    {
     
     
        HaikuDumpViewHierarchy("AttachNativeChild BEFORE child", childNative);
HaikuDumpViewHierarchy("AttachNativeChild BEFORE parent", parentNative);

if (childNative->Parent() == parentNative)
{
}
else
{
    if (childNative->Parent())
    {
        childNative->RemoveSelf();
        HaikuDumpViewHierarchy("AttachNativeChild AFTER RemoveSelf child", childNative);
    }

    wxHaikuAddNativeChild(parentNative, childNative);
}

HaikuDumpViewHierarchy("AttachNativeChild AFTER AddChild child", childNative);
HaikuDumpViewHierarchy("AttachNativeChild AFTER AddChild parent", parentNative);

        wxHaikuSetNativeShown(childNative, IsShown());

        parentNative->Invalidate();
        childNative->Invalidate();
        win->UnlockLooper();
    }
    else
    {
        if (childNative->Parent())
        {
         //   HAIKU_LOG("AttachNativeChild removing from old parent=%p (no lock)", childNative->Parent());
            childNative->RemoveSelf();
        }

        wxHaikuAddNativeChild(parentNative, childNative);

        wxHaikuSetNativeShown(childNative, IsShown());

        parentNative->Invalidate();
        childNative->Invalidate();
    }

    HaikuDumpView("Attach child after", childNative);
    HaikuDumpView("Attach parent after", parentNative);
}

void wxWindowHaiku::PostCreation(bool generic)
{
 //   HAIKU_LOG("PostCreation ENTER this=%p generic=%d", this, generic);

    HaikuAttachNativeChild();

    // wx sizers own child geometry. Native follow rules would resize children
    // again before wx lays them out, corrupting fixed/minimum dimensions.
    if (GetParent() && !IsTopLevel())
    {
        BView* attached = HaikuGetNativeViewToAttach();
        wxHaikuViewLock lock(attached);
        if (lock.ok())
            attached->SetResizingMode(B_FOLLOW_NONE);
    }

    if (generic)
        HaikuSetBackgroundStyle();
    else if (m_backgroundStyle != wxBG_STYLE_TRANSPARENT)
        SetBackgroundStyle(wxBG_STYLE_SYSTEM);

    wxWindowBase::SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
    wxWindowBase::SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));

    if (BView* view = GetHandle())
    {
        // Attached views must hold their looper lock during font updates.
        wxHaikuViewLock lock(view);
        if (lock.ok())
            view->SetFont(be_plain_font);
    }

    if (!IsThisEnabled())
        DoEnable(false);

    Show(m_isShown);

#if wxUSE_DRAG_AND_DROP
    if ( m_dropTarget )
    {
        if (BView* view = HaikuGetDrawView())
            m_dropTarget->ConnectTo(view);
    }
#endif

    wxWindowCreateEvent event(this);
    HandleWindowEvent(event);

//    HAIKU_LOG("PostCreation EXIT this=%p native=%p shown=%d enabled=%d",
        //      this, GetHandle(), IsShown(), IsEnabled());
}

void wxWindowHaiku::AddChild(wxWindowBase* child)
{
  //  HAIKU_LOG("AddChild ENTER this=%p child=%p", this, child);

    wxWindow* childWin = static_cast<wxWindow*>(child);
    wxWindowHaiku* childHaiku = wxDynamicCast(childWin, wxWindowHaiku);

    BView* childNative = childHaiku
                           ? childHaiku->HaikuGetNativeViewToAttach()
                           : (childWin ? childWin->GetHandle() : nullptr);

    BView* parentNative = HaikuGetNativeParentViewForChildren(this);

  //  HAIKU_LOG("AddChild native child=%p parentNative=%p", childNative, parentNative);

    if (childNative && parentNative && childNative->Parent() != parentNative)
    {
        BWindow* win = parentNative->Window();
        const bool childShown = childWin ? childWin->IsShown() : true;

        // Detach under the source looper lock, then attach under the
        // destination lock. Holding only the destination lock is insufficient
        // for panes moved between AUI floating frames.
        HaikuReparent(childNative, parentNative);

        if (!win || win->LockLooper())
        {
            wxHaikuSetNativeShown(childNative, childShown);
            parentNative->Invalidate();
            childNative->Invalidate();
            if (win)
                win->UnlockLooper();
        }

        HaikuDumpView("AddChild child after attach", childNative);
        HaikuDumpView("AddChild parent after attach", parentNative);
    }

    wxWindowBase::AddChild(child);

    HAIKU_LOG("AddChild EXIT this=%p child=%p", this, child);
}

bool wxWindowHaiku::Show(bool show)
{
    HAIKU_LOG("Show ENTER this=%p show=%d native=%p attach=%p",
              this, show, GetHandle(), HaikuGetNativeViewToAttach());

    if (!wxWindowBase::Show(show))
        return false;

    if (BView* view = HaikuGetNativeViewToAttach())
    {
        HaikuDumpView("Show before", view);

        BView* parent = view->Parent();

        if (BWindow* win = view->Window())
        {
            if (win->LockLooper())
            {
                wxHaikuSetNativeShown(view, show);

                if (parent)
                    parent->Invalidate();

                view->Invalidate();
                win->UnlockLooper();
            }
        }
        else
        {
            wxHaikuSetNativeShown(view, show);

            if (parent)
                parent->Invalidate();

            view->Invalidate();
        }

        HaikuDumpView("Show after", view);
    }

    wxShowEvent event(GetId(), show);
    event.SetEventObject(this);
    HandleWindowEvent(event);
    return true;
}
BWindow* wxWindowHaiku::GetBWindow() const
{
    if (BView* v = HaikuGetNativeViewToAttach())
        return v->Window();

    return nullptr;
}

BView* wxWindowHaiku::CreateBView(const BRect& WXUNUSED(frame))
{
//   HAIKU_LOG("CreateBView default stub");
    return nullptr;
}

BWindow* wxWindowHaiku::CreateBWindow(const BRect& WXUNUSED(frame),
                                      const char* WXUNUSED(title),
                                      uint32 WXUNUSED(flags))
{
 //   HAIKU_LOG("CreateBWindow default stub");
    return nullptr;
}

void wxWindowHaiku::SetLabel(const wxString& label)
{
    if (!m_haikuView)
        return;

    wxHaikuViewLock lock(m_haikuView);
    if (!lock.ok())
        return;

    if (IsTopLevel())
    {
        if (BWindow* window = m_haikuView->Window())
            window->SetTitle(label.utf8_str());
    }
    else if (auto* view = dynamic_cast<wxHaikuWindowView*>(m_haikuView))
    {
        view->SetWxLabel(label);
    }
}

wxString wxWindowHaiku::GetLabel() const
{
    if (!m_haikuView)
        return wxEmptyString;

    wxHaikuViewLock lock(m_haikuView);
    if (!lock.ok())
        return wxEmptyString;

    if (IsTopLevel())
    {
        if (BWindow* window = m_haikuView->Window())
            return wxString::FromUTF8(window->Title());
    }
    else if (auto* view = dynamic_cast<wxHaikuWindowView*>(m_haikuView))
    {
        return view->GetWxLabel();
    }
    return wxEmptyString;
}

void wxWindowHaiku::DoEnable(bool enable)
{
   // HAIKU_LOG("DoEnable this=%p enable=%d", this, enable);
    wxWindowBase::DoEnable(enable);
    BView* view = GetHandle();
    BLooper* looper = view ? view->Looper() : nullptr;
    if (view && (!looper || looper->Lock()))
    {
        const bool effective = enable && IsEnabled();
        if (BControl* control = dynamic_cast<BControl*>(view))
            control->SetEnabled(effective);
        if (BMenuField* field = dynamic_cast<BMenuField*>(view))
            field->SetEnabled(effective);
        // A multiline text control owns its native text view below a scroll
        // container. Do not alter unrelated child wx controls here.
#if wxUSE_TEXTCTRL
        if (dynamic_cast<wxTextCtrl*>(this))
        {
            std::vector<BView*> pending(1, view);
            while (!pending.empty())
            {
                BView* current = pending.back(); pending.pop_back();
                if (BTextView* text = dynamic_cast<BTextView*>(current))
                {
                    text->MakeEditable(effective && !HasFlag(wxTE_READONLY));
                    text->MakeSelectable(effective);
                }
                for (BView* child = current->ChildAt(0); child; child = child->NextSibling())
                    pending.push_back(child);
            }
        }
#endif
        wxHaikuEnsureEnabledFilter(view->Window());
        if (looper)
            looper->Unlock();
    }    Refresh();
}

// Inner BTextViews are owned by native controls and don't inherit our
// generic view's MakeFocus override. Synchronize their wx focus events after
// native focus changes, with weak references to allow reentrant destruction.
void wxHaikuSyncTextFocus(BWindow* window)
{
#if wxUSE_TEXTCTRL
    if (!window || !window->IsActive()) return;
    wxHaikuGuiDispatchLock guard(window);
    static wxWeakRef<wxWindow> previous;
    wxWindowHaiku* next = nullptr;
    for (BView* view = window->CurrentFocus(); view && !next; view = view->Parent())
        next = wxWindowHaiku::HaikuRetrieveWindowPointer(view);
    if (previous.get() == next) return;
    wxWeakRef<wxWindow> old = previous;
    previous = next;
    if (old && dynamic_cast<wxTextCtrl*>(old.get()))
    {
        wxFocusEvent event(wxEVT_KILL_FOCUS, old->GetId());
        event.SetEventObject(old.get()); event.SetWindow(previous.get());
        old->ProcessWindowEvent(event);
    }
    if (previous.get() == next && next && dynamic_cast<wxTextCtrl*>(next))
    {
        wxFocusEvent event(wxEVT_SET_FOCUS, next->GetId());
        event.SetEventObject(next); event.SetWindow(old.get());
        next->ProcessWindowEvent(event);
    }
#else
    wxUnusedVar(window);
#endif
}

void wxWindowHaiku::SetFocus()
{
//   HAIKU_LOG("SetFocus this=%p native=%p", this, m_haikuView);

    if (!m_haikuView)
        return;

    if (BWindow* win = m_haikuView->Window())
    {
        if (win->LockLooper())
        {
            m_haikuView->MakeFocus(true);
            wxHaikuSyncTextFocus(win);
            win->UnlockLooper();
        }
    }

    HaikuDumpView("SetFocus after", m_haikuView);
}

void wxWindowHaiku::HaikuReparent(BView* child, BView* parent)
{
  //  HAIKU_LOG("HaikuReparent child=%p parent=%p", child, parent);

    if (!child || !parent)
        return;

    if (child->Parent() == parent)
        return;

    if (BWindow* source = child->Window())
    {
        if (!source->LockLooper())
            return;
        child->RemoveSelf();
        source->UnlockLooper();
    }
    else if (child->Parent())
    {
        child->RemoveSelf();
    }

    BWindow* destination = parent->Window();
    if (destination && !destination->LockLooper())
        return;
    wxHaikuAddNativeChild(parent, child);
    if (destination)
        destination->UnlockLooper();

    HaikuDumpView("HaikuReparent child after", child);
    HaikuDumpView("HaikuReparent parent after", parent);
}

bool wxWindowHaiku::Reparent(wxWindowBase* newParent)
{
 //   HAIKU_LOG("Reparent ENTER this=%p newParent=%p", this, newParent);

    if (!wxWindowBase::Reparent(newParent))
    {
        HAIKU_LOG("Reparent base returned false");
        return false;
    }

    wxWindowHaiku* hp = wxDynamicCast(newParent, wxWindowHaiku);
    wxCHECK_MSG(hp != nullptr, false, "New parent must be wxWindowHaiku");

    if (!m_haikuView)
        return true;

    BView* parentView = hp->HaikuGetParentView();
    wxCHECK_MSG(parentView != nullptr, false, "New parent has no native BView");

    HaikuDumpView("Reparent child before", m_haikuView);
    HaikuDumpView("Reparent new parent", parentView);

    // wxWindowBase::Reparent already calls the destination AddChild(), which
    // transfers the native view. Avoid a second detach/attach of scroll views.

 //   HAIKU_LOG("Reparent EXIT this=%p newParent=%p", this, newParent);
    return true;
}

void wxWindowHaiku::Raise()
{
  //  HAIKU_LOG("Raise this=%p nativeWindow=%p", this, GetBWindow());

    if (BWindow* win = GetBWindow())
        win->Activate(true);
}

void wxWindowHaiku::Lower()
{
   // HAIKU_LOG("Lower this=%p (no-op)", this);
}

void wxWindowHaiku::WarpPointer(int x, int y)
{
    wxPoint point = ClientToScreen(wxPoint(x, y));
    set_mouse_position(point.x, point.y);
}

void wxWindowHaiku::Update()
{
    BView* view = HaikuGetNativeViewToAttach();
    if (!view)
        return;

    BMessenger destination;
    BWindow* window = nullptr;
    {
        wxHaikuViewLock lock(view);
        if (!lock.ok() || !view->Window())
            return;
        window = view->Window();
        const thread_id windowThread = window->Thread();
        // A native window that has never been shown has no running looper.
        if (windowThread < B_OK)
            return;

        if (find_thread(nullptr) == windowThread)
        {
            window->UpdateIfNeeded();
            return;
        }
        destination = BMessenger(window);
    }

    // UpdateIfNeeded() only works on the native window thread. Release wx
    // dispatch while it paints, and wait for the paint to finish before return.
    // A focus/layout callback can own this window's native lock even on
    // the application thread. The dispatch pause releases its own looper,
    // so also release this target's locks before handing work to its thread.
    const int nativeLocks = window->LockingThread() == find_thread(nullptr)
                                ? window->CountLocks() : 0;
    for (int i = 0; i < nativeLocks; ++i)
        window->Unlock();
    {
        BMessage request('wxUp'), reply;
        wxHaikuGuiDispatchPause pause;
        destination.SendMessage(&request, &reply, 5000000, 5000000);
    }
    // Restore the original recursion depth after wx dispatch is reacquired.
    for (int i = 0; i < nativeLocks; ++i)
        window->Lock();
}

void wxWindowHaiku::Refresh(bool eraseBackground, const wxRect* rect)
{
    BView* view = HaikuGetDrawView();
    if (!view)
        return;

    wxHaikuViewLock lock(view);
    if (!lock.ok() || !view->Window())
        return;

    // A Haiku view invalidates only its own drawing. Pass the intersection
    // to each visible wx child in that child's local coordinates.
    for (wxWindow* child : GetChildren())
    {
        if (!child->IsShown() || child->IsTopLevel())
            continue;

        if (!rect)
        {
            child->Refresh(eraseBackground);
            continue;
        }

        const wxRect bounds = child->GetRect();
        wxRect damage = *rect;
        damage.Intersect(bounds);
        if (!damage.IsEmpty())
        {
            damage.Offset(-bounds.x, -bounds.y);
            child->Refresh(eraseBackground, &damage);
        }
    }

    if (!rect)
        view->Invalidate();
    else if (!rect->IsEmpty())
        view->Invalidate(BRect(rect->x, rect->y, rect->GetRight(), rect->GetBottom()));
}

bool wxWindowHaiku::SetCursor(const wxCursor& cursor)
{
  //  HAIKU_LOG("SetCursor this=%p native=%p", this, m_haikuView);

    if (!wxWindowBase::SetCursor(cursor))
        return false;

    if (!m_haikuView)
        return true;

    const BCursor* hc = cursor.GetHandle();
    if (!hc)
        return true;

    if (BWindow* win = m_haikuView->Window())
    {
        if (win->LockLooper())
        {
            m_haikuView->SetViewCursor(hc, true);
            win->UnlockLooper();
        }
    }

    HaikuDumpView("SetCursor after", m_haikuView);
    return true;
}

bool wxWindowHaiku::SetFont(const wxFont& font)
{
 //   HAIKU_LOG("SetFont this=%p fontOk=%d", this, font.IsOk());

    if (!wxWindowBase::SetFont(font))
        return false;

    Refresh();
    return true;
}

int wxWindowHaiku::GetCharHeight() const
{
    BFont font = *be_plain_font;
    if (m_haikuView)
    {
        wxHaikuViewLock lock(m_haikuView);
        if (lock.ok())
            m_haikuView->GetFont(&font);
    }

    // Match the line height used by wxHaikuDCImpl, not the nominal font size.
    font_height height;
    font.GetHeight(&height);
    return static_cast<int>(height.ascent + height.descent + height.leading);
}

int wxWindowHaiku::GetCharWidth() const
{
  //  HAIKU_LOG("GetCharWidth this=%p native=%p", this, m_haikuView);

    BFont f;
    if (m_haikuView && m_haikuView->Window())
    {
        if (m_haikuView->Window()->LockLooper())
        {
            m_haikuView->GetFont(&f);
            m_haikuView->Window()->UnlockLooper();
            return int(f.StringWidth("M") + 0.5f);
        }
    }
    return 0;
}

double wxWindowHaiku::GetContentScaleFactor() const
{
    return wxWindowBase::GetContentScaleFactor();
}

wxSize wxWindowHaiku::GetDPI() const
{
    return wxWindowBase::GetDPI();
}

double wxWindowHaiku::GetDPIScaleFactor() const
{
 //   HAIKU_LOG("GetDPIScaleFactor this=%p native=%p", this, m_haikuView);

    if (!m_haikuView || !m_haikuView->Window())
        return 1.0;

    return wxWindowBase::GetDPIScaleFactor();
}

void wxWindowHaiku::SetScrollbar(int orient, int pos, int thumbvisible, int range, bool refresh)
{
  //  HAIKU_LOG("SetScrollbar this=%p orient=%d pos=%d thumb=%d range=%d refresh=%d",
    //          this, orient, pos, thumbvisible, range, refresh);

    BScrollBar* bar = HaikuGetScrollBar(orient);
    if (!bar)
        return;

    if (BWindow* win = bar->Window())
    {
        if (win->LockLooper())
        {
            auto* target = dynamic_cast<wxHaikuWindowView*>(bar->Target());
            range = wxMax(0, range);
            thumbvisible = wxMax(0, wxMin(thumbvisible, range));
            const int maximum = range - thumbvisible;
            if (target)
            {
                target->RecordScrollbar(orient, range, thumbvisible);
                target->SetProgrammaticScroll(true);
            }
            bar->SetRange(0, maximum);
            bar->SetProportion(range ? float(thumbvisible) / range : 1.0f);
            bar->SetSteps(1, thumbvisible > 0 ? thumbvisible : 1);
            bar->SetValue(wxMax(0, wxMin(pos, maximum)));
            if (target)
                target->SetProgrammaticScroll(false);
            win->UnlockLooper();
        }
    }

    if (refresh)
        Refresh();
}

void wxWindowHaiku::SetScrollPos(int orient, int pos, bool refresh)
{
  //  HAIKU_LOG("SetScrollPos this=%p orient=%d pos=%d refresh=%d",
   //           this, orient, pos, refresh);

    BScrollBar* bar = HaikuGetScrollBar(orient);
    if (!bar)
        return;

    if (BWindow* win = bar->Window())
    {
        if (win->LockLooper())
        {
            auto* target = dynamic_cast<wxHaikuWindowView*>(bar->Target());
            if (target)
                target->SetProgrammaticScroll(true);
            bar->SetValue(pos);
            if (target)
                target->SetProgrammaticScroll(false);
            win->UnlockLooper();
        }
    }

    if (refresh)
        Refresh();
}

int wxWindowHaiku::GetScrollPos(int orient) const
{
  //  HAIKU_LOG("GetScrollPos this=%p orient=%d", this, orient);

    BScrollBar* bar = HaikuGetScrollBar(orient);
    if (!bar)
        return 0;

    int value = 0;
    if (BWindow* win = bar->Window())
    {
        if (win->LockLooper())
        {
            value = int(bar->Value() + 0.5f);
            win->UnlockLooper();
        }
    }
    return value;
}

int wxWindowHaiku::GetScrollThumb(int orient) const
{
 //   HAIKU_LOG("GetScrollThumb this=%p orient=%d", this, orient);

    BScrollBar* bar = HaikuGetScrollBar(orient);
    if (!bar)
        return 0;

    if (auto* target = dynamic_cast<wxHaikuWindowView*>(bar->Target()))
        return target->GetWxScrollThumb(orient);

    int thumb = 0;
    if (BWindow* win = bar->Window())
    {
        if (win->LockLooper())
        {
            float minV, maxV;
            bar->GetRange(&minV, &maxV);
            const float range = maxV - minV;
            thumb = range > 0 ? int(bar->Proportion() * range + 0.5f) : 0;
            win->UnlockLooper();
        }
    }
    return thumb;
}

int wxWindowHaiku::GetScrollRange(int orient) const
{
  //  HAIKU_LOG("GetScrollRange this=%p orient=%d", this, orient);

    BScrollBar* bar = HaikuGetScrollBar(orient);
    if (!bar)
        return 0;

    if (auto* target = dynamic_cast<wxHaikuWindowView*>(bar->Target()))
        return target->GetWxScrollRange(orient);

    int r = 0;
    if (BWindow* win = bar->Window())
    {
        if (win->LockLooper())
        {
            float minV, maxV;
            bar->GetRange(&minV, &maxV);
            r = int(maxV - minV + 1.0f + 0.5f);
            win->UnlockLooper();
        }
    }
    return r;
}

wxSize wxWindowHaiku::GetWindowBorderSize() const
{
    return wxWindowBase::GetWindowBorderSize();
}

void wxWindowHaiku::ScrollWindow(int dx, int dy, const wxRect* rect)
{
    if (!dx && !dy) return;
    for (wxWindow* child : GetChildren())
    {
        if (!rect || rect->Intersects(child->GetRect()))
        {
            wxPoint pos = child->GetPosition();
            child->Move(pos.x + dx, pos.y + dy);
        }
    }
    Refresh(true, rect);
}

void wxWindowHaiku::SetWindowStyleFlag(long style)
{
 //   HAIKU_LOG("SetWindowStyleFlag this=%p style=0x%lx", this, (unsigned long)style);
    wxWindowBase::SetWindowStyleFlag(style);
}

void wxWindowHaiku::SetExtraStyle(long exStyle)
{
 //   HAIKU_LOG("SetExtraStyle" );
    wxWindowBase::SetExtraStyle(exStyle);
}

bool wxWindowHaiku::SetBackgroundStyle(wxBackgroundStyle style)
{
  //  HAIKU_LOG("SetBackgroundStyle this=%p style=%d", this, (int)style);

    if (!wxWindowBase::SetBackgroundStyle(style))
        return false;
    return HaikuSetBackgroundStyle();
}

bool wxWindowHaiku::HaikuSetBackgroundStyle()
{
  //  HAIKU_LOG("HaikuSetBackgroundStyle this=%p", this);
    return true;
}

bool wxWindowHaiku::IsTransparentBackgroundSupported(wxString* reason) const
{
    //HAIKU_LOG("IsTransparentBackgroundSupported this=%p", this);
    (void)reason;
    return true;
}

bool wxWindowHaiku::SetTransparent(wxByte WXUNUSED(alpha))
{
  //  HAIKU_LOG("SetTransparent not supported this=%p", this);
    return false;
}

bool wxWindowHaiku::SetBackgroundColour(const wxColour& colour)
{
  //  HAIKU_LOG("SetBackgroundColour this=%p rgb=(%d,%d,%d,%d)",
   //           this, colour.Red(), colour.Green(), colour.Blue(), colour.Alpha());

    if (!wxWindowBase::SetBackgroundColour(colour))
        return false;
    Refresh();
    return true;
}

bool wxWindowHaiku::SetForegroundColour(const wxColour& colour)
{
    //HAIKU_LOG("SetForegroundColour this=%p rgb=(%d,%d,%d,%d)",
      //        this, colour.Red(), colour.Green(), colour.Blue(), colour.Alpha());

    if (!wxWindowBase::SetForegroundColour(colour))
        return false;
    Refresh();
    return true;
}

void wxWindowHaiku::SetDoubleBuffered(bool WXUNUSED(on))
{
   // HAIKU_LOG("SetDoubleBuffered no-op this=%p", this);
}

bool wxWindowHaiku::IsDoubleBuffered() const
{
    return true;
}

BView* wxWindowHaiku::GetHandle() const
{
    return m_haikuView;
}

wxWindowHaiku* wxWindowBase::DoFindFocus()
{
    if (!be_app) return nullptr;
    BWindow* preferred = dynamic_cast<BWindow*>(s_activeNativeWindow.Target(nullptr));
    if (preferred && !preferred->IsHidden() && preferred->IsActive())
    {
        if (preferred->LockWithTimeout(0) != B_OK)
            return nullptr;
        wxWindowHaiku* result = nullptr;
        for (BView* view = preferred->CurrentFocus(); view && !result; view = view->Parent())
            result = wxHaiku::GetWxFromView(view);
        preferred->Unlock();
        return result;
    }
    for (int32 i = 0; BWindow* win = be_app->WindowAt(i); ++i)
    {
        // Hidden windows may still be locked by their constructing thread.
        // A focus query from a paint callback must not wait on another looper.
        if (win->IsHidden() || !win->IsActive() || win->LockWithTimeout(0) != B_OK)
            continue;
        wxWindowHaiku* result = nullptr;
        if (win->IsActive())
            for (BView* view = win->CurrentFocus(); view && !result; view = view->Parent())
                result = wxHaiku::GetWxFromView(view);
        win->Unlock();
        if (result) return result;
    }
    return nullptr;
}

wxWindowHaiku* wxWindowBase::GetCapture()
{
    return s_capturedWindow;
}

#if wxUSE_DRAG_AND_DROP
void wxWindowHaiku::SetDropTarget(wxDropTarget* dropTarget)
{
    if ( m_dropTarget == dropTarget )
        return;

    if ( m_dropTarget )
    {
        m_dropTarget->Disconnect();
        delete m_dropTarget;
    }

    m_dropTarget = dropTarget;

    if ( m_dropTarget )
        m_dropTarget->ConnectTo(HaikuGetDrawView());
}
#endif
#if wxUSE_ACCEL
void wxWindowHaiku::SetAcceleratorTable(const wxAcceleratorTable& accel)
{
    // Remove the old native registrations before replacing the wx table: Haiku
    // keeps shortcuts until explicitly removed.
    BWindow* const window = m_haikuView ? m_haikuView->Window() : nullptr;
    if (window)
    {
        wxHaikuAutoLock lock(window);
        if (lock.ok())
            m_accelTable.RemoveFromWindow(window);
    }

    wxWindowBase::SetAcceleratorTable(accel);
    m_accelTable = accel;

    if (!window || !m_haikuShortcutHandler)
        return;

    wxHaikuAutoLock lock(window);
    if (!lock.ok())
        return;

    if (!m_haikuShortcutHandler->Looper())
        window->AddHandler(m_haikuShortcutHandler.get());

    m_accelTable.ApplyToWindow(window, m_haikuShortcutHandler.get());
}

void wxWindowHaiku::HaikuHandleShortcut(int command)
{
   // HAIKU_LOG("HaikuHandleShortcut this=%p command=%d", this, command);

    wxCommandEvent menuEvent(wxEVT_COMMAND_MENU_SELECTED, command);
    menuEvent.SetEventObject(this);
    if (!ProcessWindowEvent(menuEvent))
    {
        wxCommandEvent buttonEvent(wxEVT_COMMAND_BUTTON_CLICKED, command);
        buttonEvent.SetEventObject(this);
        ProcessWindowEvent(buttonEvent);
    }
}
#endif

bool wxWindowHaiku::EnableTouchEvents(int WXUNUSED(eventsMask))
{
//    HAIKU_LOG("EnableTouchEvents not supported this=%p", (void*)this);
    return false;
}

void wxWindowHaiku::HaikuSetPicture(BPicture* pict)
{
    HAIKU_LOG("HaikuSetPicture this=%p pict=%p", this, pict);
    m_haikuPicture.reset(pict);
}

BView* wxWindowHaiku::HaikuGetDrawView()
{
    return m_haikuDrawView ? m_haikuDrawView : m_haikuView;
}

bool wxWindowHaiku::HaikuCanPaintWithoutActiveView() const
{
    return false;
}

#include "wx/region.h"

bool wxWindowHaiku::HaikuHandleDrawMessage(BView* handler, BRect updateRect)
{
   // HAIKU_LOG("DRAW handler=%p updateRect=(%.1f,%.1f,%.1f,%.1f)",
     //         handler, updateRect.left, updateRect.top, updateRect.right, updateRect.bottom);

    if (!handler)
    {
   //     HAIKU_LOG("DRAW ERROR null handler");
        return false;
    }

    if (m_haikuPicture)
    {
     //   HAIKU_LOG("DRAW using cached picture");
        handler->DrawPicture(m_haikuPicture.get());
        m_haikuPicture.reset();
        return true;
    }

    const int x = (int)updateRect.left;
    const int y = (int)updateRect.top;
    const int w = (int)(updateRect.Width() + 1.0f);
    const int h = (int)(updateRect.Height() + 1.0f);

    // Use the base window region queried by GetUpdateRegion()/IsExposed().
    // The legacy Haiku member with the same name is not used by those APIs.
    GetUpdateRegion() = wxRegion(x, y, w, h);

    wxPaintEvent ev;
    ev.SetEventObject(this);
    // Native update clipping belongs to the current Draw callback. Do not
    // leave a wx paint handler's transform or user clip on the persistent view.
    handler->SetTransform(BAffineTransform());
    handler->ConstrainClippingRegion(nullptr);
    handler->PushState();
    const bool handled = ProcessWindowEvent(ev);
    handler->PopState();
    handler->Flush();

    HAIKU_LOG("DRAW paint handled");

    GetUpdateRegion().Clear();
    return handled;
}

bool wxWindowHaiku::HaikuHandleFrameResized(BView* WXUNUSED(handler), float width, float height)
{
  //  HAIKU_LOG("HaikuHandleFrameResized ");//this=%p size=(%.1f x %.1f)",
              //(void*)this, width, height);

    wxSizeEvent ev(wxSize(int(width + 1), int(height + 1)));
    ev.SetEventObject(this);
    return ProcessWindowEvent(ev);
}
bool wxWindowHaiku::HaikuHandleMouseWheelChanged(BView* handler, BMessage* message)
{
    HAIKU_LOG("HaikuHandleMouseWheelChanged ");//this=%p handler=%p message=%p",
       //       (void*)this, handler, message);

    if (!handler || !message)
        return false;

    float dx = 0, dy = 0;
    message->FindFloat("be:wheel_delta_x", &dx);
    message->FindFloat("be:wheel_delta_y", &dy);

    int32 modifiers = 0;
    message->FindInt32("modifiers", &modifiers);

    int32 buttons = 0;
    message->FindInt32("buttons", &buttons);

    // Use horizontal axis when only horizontal delta is present, otherwise vertical
    const bool isHorizontal = (dx != 0.0f && dy == 0.0f);
    wxMouseEvent ev(wxEVT_MOUSEWHEEL);
    ev.m_wheelAxis      = isHorizontal ? wxMOUSE_WHEEL_HORIZONTAL : wxMOUSE_WHEEL_VERTICAL;
    ev.m_wheelDelta     = 120;
    ev.m_linesPerAction = 3;
    ev.m_wheelRotation  = isHorizontal ? int(-dx * 120.0f) : int(-dy * 120.0f);

    // Coordinates are already view-local â€” no ConvertFromScreen.
    BPoint where;
    if (message->FindPoint("where", &where) == B_OK)
    {
        ev.m_x = int(where.x);
        ev.m_y = int(where.y);
    }

    m_haikuLastMouseButtons = buttons;

    ev.m_leftDown   = (buttons & B_PRIMARY_MOUSE_BUTTON)   != 0;
    ev.m_middleDown = (buttons & B_TERTIARY_MOUSE_BUTTON)  != 0;
    ev.m_rightDown  = (buttons & B_SECONDARY_MOUSE_BUTTON) != 0;

    ev.m_shiftDown   = (modifiers & B_SHIFT_KEY)   != 0;
    ev.m_controlDown = (modifiers & B_CONTROL_KEY) != 0;
    ev.m_altDown     = (modifiers & B_OPTION_KEY)  != 0;
    ev.m_metaDown    = (modifiers & B_COMMAND_KEY) != 0;

  //  HAIKU_LOG("MouseWheel dx=%.2f dy=%.2f pos=(%d,%d) rotation=%d axis=%d",
         //     dx, dy, ev.m_x, ev.m_y, ev.m_wheelRotation, (int)ev.m_wheelAxis);

    ev.SetEventObject(this);
    return ProcessWindowEvent(ev);
}

static wxKeyCode wxHaikuGetMessageKeyCode(const BMessage& message, int32 rawChar)
{
    int32 key;
    if (message.FindInt32("wx:modifier_key", &key) == B_OK)
        return static_cast<wxKeyCode>(key);

    if (rawChar == B_FUNCTION_KEY &&
        message.FindInt32("key", &key) == B_OK &&
        key >= B_F1_KEY && key <= B_F12_KEY)
        return static_cast<wxKeyCode>(WXK_F1 + key - B_F1_KEY);

    wxKeyCode code = wxHaikuConvertKeyCode(static_cast<uint32>(rawChar));
    if (code >= 'a' && code <= 'z')
        code = static_cast<wxKeyCode>(code - 'a' + 'A');
    return code;
}

bool wxWindowHaiku::HaikuHandleKeyDown(BView* WXUNUSED(handler), BMessage* message)
{
 //   HAIKU_LOG("HaikuHandleKeyDown");// this=%p message=%p", (void*)this, message);

    if (!message)
        return false;

    // raw_char: the key's base character, pre-modifier (e.g. B_UP_ARROW, 'a', B_ENTER)
    int32 rawChar = 0;
    message->FindInt32("raw_char", &rawChar);

    uint32 modifiers = 0;
    message->FindInt32("modifiers", (int32*)&modifiers);

  //  HAIKU_LOG("KeyDown rawChar=%ld modifiers=0x%lx", (long)rawChar, (unsigned long)modifiers);

    // Map raw_char through the Haikuâ†’wx key code table (handles arrows, F-keys, Return, etc.)
    const wxKeyCode wxCode = wxHaikuGetMessageKeyCode(*message, rawChar);

    wxKeyEvent ev(wxEVT_KEY_DOWN);
    ev.m_keyCode = wxCode;
    ev.m_uniChar = wxCode >= WXK_START ? WXK_NONE : (wxCode ? wxCode : rawChar);
    ev.m_rawCode = static_cast<wxUint32>(rawChar);
    wxHaikuFillKeyboardModifiers(modifiers, &ev);
    ev.SetEventObject(this);

    const bool handled = ProcessWindowEvent(ev);
    bool charHandled = false;


    // wxEVT_CHAR carries the actual produced character (with modifiers applied).
    // Read the "bytes" field which Haiku fills with the translated UTF-8 character.
    const char* bytes = nullptr;
    int32 charCode = rawChar;
    if (message->FindString("bytes", &bytes) == B_OK && bytes && *bytes)
    {
        wxString charStr(bytes, wxConvUTF8);
        if (!charStr.IsEmpty())
            charCode = static_cast<int32>(charStr[0].GetValue());
    }

    if (!handled && charCode != 0)
    {
        // For special keys, carry the same WXK_xxx code; for printable chars, use the
        // actual character value (e.g. 'A' when Shift+a is pressed).
        wxKeyCode charKey;
        if (wxCode >= WXK_START && charCode == rawChar)
            charKey = wxCode;
        else if ((modifiers & B_CONTROL_KEY) && wxCode >= 'A' && wxCode <= 'Z' &&
                 charCode == wxCode - 'A' + 1)
            charKey = static_cast<wxKeyCode>(charCode);
        else
            charKey = wxHaikuConvertKeyCode(static_cast<uint32>(charCode));

        wxKeyEvent ch(wxEVT_CHAR);
        ch.m_keyCode = charKey;
        ch.m_uniChar = charKey >= WXK_START ? WXK_NONE
                                            : (charKey ? charKey : charCode);
        ch.m_rawCode = static_cast<wxUint32>(rawChar);
        wxHaikuFillKeyboardModifiers(modifiers, &ch);
        ch.SetEventObject(this);
        charHandled = ProcessWindowEvent(ch);
    
    }

    return handled || charHandled;
}

bool wxWindowHaiku::HaikuHandleKeyUp(BView* WXUNUSED(handler), BMessage* message)
{
  //  HAIKU_LOG("HaikuHandleKeyUp this");//=%p message=%p", (void*)this, message);

    if (!message)
        return false;

    int32 rawChar = 0;
    message->FindInt32("raw_char", &rawChar);

    uint32 modifiers = 0;
    message->FindInt32("modifiers", (int32*)&modifiers);

 //   HAIKU_LOG("KeyUp rawChar=%ld modifiers=0x%lx", (long)rawChar, (unsigned long)modifiers);

    const wxKeyCode wxCode = wxHaikuGetMessageKeyCode(*message, rawChar);

    wxKeyEvent ev(wxEVT_KEY_UP);
    ev.m_keyCode = wxCode;
    ev.m_uniChar = wxCode >= WXK_START ? WXK_NONE : (wxCode ? wxCode : rawChar);
    ev.m_rawCode = static_cast<wxUint32>(rawChar);
    wxHaikuFillKeyboardModifiers(modifiers, &ev);
    ev.SetEventObject(this);

    return ProcessWindowEvent(ev);
}

bool wxWindowHaiku::HaikuHandleMouseDown(BView* handler, BMessage* message)
{
  //  HAIKU_LOG("HaikuHandleMouseDown this");//=%p handler=%p message=%p",
          //    (void*)this, (void*)handler, (void*)message);

    if (!handler || !message)
        return false;

    BPoint where;
    if (message->FindPoint("where", &where) != B_OK)
        return false;

    // Coordinates are already view-local â€” no ConvertFromScreen.

    int32 buttons = 0;
    message->FindInt32("buttons", &buttons);

    int32 modifiers = 0;
    message->FindInt32("modifiers", &modifiers);

    // "buttons" after B_MOUSE_DOWN includes all currently held buttons.
    // XOR with the previous state isolates the button that was just pressed.
    // This correctly handles chording (pressing a second button while one is held).
    int32 newlyPressed = buttons & ~m_haikuLastMouseButtons;
    m_haikuLastMouseButtons = buttons;

    wxEventType type = wxEVT_LEFT_DOWN;
    if (newlyPressed & B_SECONDARY_MOUSE_BUTTON)     type = wxEVT_RIGHT_DOWN;
    else if (newlyPressed & B_TERTIARY_MOUSE_BUTTON) type = wxEVT_MIDDLE_DOWN;

 //   HAIKU_LOG("MouseDown pos=(%.1f,%.1f) buttons=0x%lx newlyPressed=0x%lx mods=0x%lx type=%d",
       //       where.x, where.y, (unsigned long)buttons,
            //  (unsigned long)newlyPressed, (unsigned long)modifiers, (int)type);

    int32 clicks = 1;
    message->FindInt32("clicks", &clicks);
    if (clicks == 2)
    {
        if (type == wxEVT_LEFT_DOWN) type = wxEVT_LEFT_DCLICK;
        else if (type == wxEVT_RIGHT_DOWN) type = wxEVT_RIGHT_DCLICK;
        else if (type == wxEVT_MIDDLE_DOWN) type = wxEVT_MIDDLE_DCLICK;
    }
    wxMouseEvent ev(type);
    ev.m_clickCount = clicks;
    ev.m_x = (int)where.x;
    ev.m_y = (int)where.y;

    ev.m_leftDown   = (buttons & B_PRIMARY_MOUSE_BUTTON)   != 0;
    ev.m_middleDown = (buttons & B_TERTIARY_MOUSE_BUTTON)  != 0;
    ev.m_rightDown  = (buttons & B_SECONDARY_MOUSE_BUTTON) != 0;

    ev.m_shiftDown   = (modifiers & B_SHIFT_KEY)   != 0;
    ev.m_controlDown = (modifiers & B_CONTROL_KEY) != 0;
    ev.m_altDown     = (modifiers & B_OPTION_KEY)  != 0;
    ev.m_metaDown    = (modifiers & B_COMMAND_KEY) != 0;

    ev.SetEventObject(this);
    const bool processed = ProcessWindowEvent(ev);

    return processed;
}

bool wxWindowHaiku::HaikuHandleMouseUp(BView* handler, BMessage* message)
{
  //  HAIKU_LOG("HaikuHandleMouseUp this");//=%p handler=%p message=%p",
            //  (void*)this, (void*)handler, (void*)message);

    if (!handler || !message)
        return false;

    BPoint where;
    if (message->FindPoint("where", &where) != B_OK)
        return false;

    // Coordinates are already view-local â€” no ConvertFromScreen.

    int32 buttons = 0;
    message->FindInt32("buttons", &buttons);

    int32 modifiers = 0;
    message->FindInt32("modifiers", &modifiers);

    // In B_MOUSE_UP, "buttons" is the state AFTER the release â€” the released
    // button is already cleared. Compare with m_haikuLastMouseButtons to find
    // which button was just released. This is the only reliable way.
    int32 released = m_haikuLastMouseButtons & ~buttons;
    m_haikuLastMouseButtons = buttons;

    wxEventType type = wxEVT_LEFT_UP;
    if (released & B_SECONDARY_MOUSE_BUTTON)     type = wxEVT_RIGHT_UP;
    else if (released & B_TERTIARY_MOUSE_BUTTON) type = wxEVT_MIDDLE_UP;

   // HAIKU_LOG("MouseUp pos=(%.1f,%.1f) buttons=0x%lx released=0x%lx mods=0x%lx type=%d",
        //      where.x, where.y, (unsigned long)buttons,
           //   (unsigned long)released, (unsigned long)modifiers, (int)type);

    wxMouseEvent ev(type);
    ev.m_x = (int)where.x;
    ev.m_y = (int)where.y;

    // Reflect buttons still held after this release
    ev.m_leftDown   = (buttons & B_PRIMARY_MOUSE_BUTTON)   != 0;
    ev.m_middleDown = (buttons & B_TERTIARY_MOUSE_BUTTON)  != 0;
    ev.m_rightDown  = (buttons & B_SECONDARY_MOUSE_BUTTON) != 0;

    ev.m_shiftDown   = (modifiers & B_SHIFT_KEY)   != 0;
    ev.m_controlDown = (modifiers & B_CONTROL_KEY) != 0;
    ev.m_altDown     = (modifiers & B_OPTION_KEY)  != 0;
    ev.m_metaDown    = (modifiers & B_COMMAND_KEY) != 0;

    ev.SetEventObject(this);
    const bool processed = ProcessWindowEvent(ev);

    return processed;
}

bool wxWindowHaiku::HaikuHandleMouseMoved(BView* handler, BMessage* message)
{
 //   HAIKU_LOG("HaikuHandleMouseMoved this");//=%p handler=%p message=%p",
            //  (void*)this, (void*)handler, (void*)message);

    if (!handler || !message)
        return false;

    BPoint where;
    // B_MOUSE_MOVED carries window coordinates in "where" and the
    // receiving view's local coordinates in "be:view_where".
    if (message->FindPoint("be:view_where", &where) != B_OK)
    {
        if (message->FindPoint("where", &where) != B_OK)
            return false;
        if (handler->Window())
            where = handler->ConvertFromScreen(handler->Window()->ConvertToScreen(where));
    }

    int32 buttons = 0;
    message->FindInt32("buttons", &buttons);

    int32 modifiers = 0;
    message->FindInt32("modifiers", &modifiers);

    // Keep tracking state in sync during drags (button state can change mid-move)
    m_haikuLastMouseButtons = buttons;

    wxMouseEvent ev(wxEVT_MOTION);
    ev.m_x = (int)where.x;
    ev.m_y = (int)where.y;

    ev.m_leftDown   = (buttons & B_PRIMARY_MOUSE_BUTTON)   != 0;
    ev.m_middleDown = (buttons & B_TERTIARY_MOUSE_BUTTON)  != 0;
    ev.m_rightDown  = (buttons & B_SECONDARY_MOUSE_BUTTON) != 0;

    ev.m_shiftDown   = (modifiers & B_SHIFT_KEY)   != 0;
    ev.m_controlDown = (modifiers & B_CONTROL_KEY) != 0;
    ev.m_altDown     = (modifiers & B_OPTION_KEY)  != 0;
    ev.m_metaDown    = (modifiers & B_COMMAND_KEY) != 0;

    ev.SetEventObject(this);

    HAIKU_LOG("MouseMoved pos=(%d,%d)", ev.m_x, ev.m_y);

    HaikuSendSetCursorEvent(this, wxPoint(ev.m_x, ev.m_y));
    return ProcessWindowEvent(ev);
}

bool wxWindowHaiku::HaikuHandleFrameMoved(BView* WXUNUSED(handler), BPoint newPosition)
{
    HAIKU_LOG("HaikuHandleFrameMoved this");//=%p pos=(%.1f,%.1f)",
              //(void*)this, newPosition.x, newPosition.y);

    wxMoveEvent ev(wxPoint(int(newPosition.x), int(newPosition.y)));
    ev.SetEventObject(this);
    return ProcessWindowEvent(ev);
}

bool wxWindowHaiku::HaikuHandleWindowActivated(BView* WXUNUSED(handler), bool active)
{
    if (BWindow* native = GetBWindow())
    {
        if (active)
            s_activeNativeWindow = BMessenger(native);
        else if (s_activeNativeWindow.Target(nullptr) == native)
            s_activeNativeWindow = BMessenger();
    }
  //  HAIKU_LOG("HaikuHandleWindowActivated this");//=%p active=%d", (void*)this, active);

    wxActivateEvent ev(wxEVT_ACTIVATE, active);
    ev.SetEventObject(this);
    return ProcessWindowEvent(ev);
}

bool wxWindowHaiku::HaikuHandleQuitRequested(BView* handler)
{
   // HAIKU_LOG("HaikuHandleQuitRequested ENTER this=%p handler=%p GetHandle()=%p enabled=%d shown=%d",
   //           this, handler, GetHandle(), IsEnabled(), IsShown());

    const bool rc = Close();

   // HAIKU_LOG("HaikuHandleQuitRequested EXIT this=%p Close()=%d", (void*)this, rc);

    return rc;
}

bool wxWindowHaiku::HaikuHandleMessageReceived(BView* handler, BMessage* message)
{
   // HAIKU_LOG("HaikuHandleMessageReceived");// this=%p handler=%p message=%p what=0x%lx",
          //    (void*)this, (void*)handler, (void*)message,
            //  message ? (unsigned long)message->what : 0UL);

#if wxUSE_DRAG_AND_DROP
    if ( message && GetDropTarget() )
    {
        switch ( message->what )
        {
            case B_SIMPLE_DATA:
            case B_REFS_RECEIVED:
            case B_COPY_TARGET:
            case B_MOVE_TARGET:
            case B_LINK_TARGET:
            case 'WxDn':
              //  HAIKU_LOG("HaikuHandleMessageReceived forwarding drop message what=0x%lx",
                     //     (unsigned long)message->what);
                return HaikuHandleDropMessage(handler, message);

            default:
                break;
        }
    }
#endif

    return false;
}




#if wxUSE_DRAG_AND_DROP
bool wxWindowHaiku::HaikuHandleDragOver(BView* handler,
                                        BMessage* message,
                                        const BPoint& where)
{
   // HAIKU_LOG("HaikuHandleDragOver this=");//%p handler=%p message=%p where=(%.1f,%.1f)",
              //(void*)this, (void*)handler, (void*)message, where.x, where.y);

    if ( !handler || !message )
        return false;

    wxDropTarget* dropTarget = GetDropTarget();
    if ( !dropTarget )
        return false;

    return dropTarget->HandleDragOver(handler, message, where);
}

bool wxWindowHaiku::HaikuHandleDropMessage(BView* handler, BMessage* message)
{
  //  HAIKU_LOG("HaikuHandleDropMessage this");//=%p handler=%p message=%p",
      //        (void*)this, (void*)handler, (void*)message);

    if ( !handler || !message )
        return false;

    wxDropTarget* dropTarget = GetDropTarget();
    if ( !dropTarget )
        return false;

    return dropTarget->HandleDrop(handler, message);
}

void wxWindowHaiku::HaikuHandleDragLeave(BView* handler)
{
 //   HAIKU_LOG("HaikuHandleDragLeave this");//=%p handler=%p",
//              (void*)this, (void*)handler);

    wxUnusedVar(handler);

    wxDropTarget* dropTarget = GetDropTarget();
    if ( dropTarget )
        dropTarget->HandleLeave();
}
#endif



// ----------------------------------------------------------------------------
// Pointer association
// ----------------------------------------------------------------------------
void wxWindowHaiku::HaikuStoreWindowPointer(BView* view, const wxWindowHaiku* window)
{
   // HAIKU_LOG("StoreWindowPointer view=%p window=%p", view, window);

    if (!view)
        return;
    wxHaiku::SetWxForView(view, const_cast<wxWindowHaiku*>(window));
}

wxWindowHaiku* wxWindowHaiku::HaikuRetrieveWindowPointer(const BView* view)
{
   // HAIKU_LOG("RetrieveWindowPointer view=%p", view);
    return view ? wxHaiku::GetWxFromView(view) : nullptr;
}

void wxWindowHaiku::HaikuSendSetCursorEvent(wxWindowHaiku* win, const wxPoint& posClient)
{
   // HAIKU_LOG("SendSetCursorEvent win=%p pos=(%d,%d)", win, posClient.x, posClient.y);

    wxSetCursorEvent ev(posClient.x, posClient.y);
    ev.SetEventObject(win);
    if (win->ProcessWindowEvent(ev) && ev.HasCursor())
        win->SetCursor(ev.GetCursor());
}

// ----------------------------------------------------------------------------
// Tooltips
// ----------------------------------------------------------------------------
#if wxUSE_TOOLTIPS
void wxWindowHaiku::DoSetToolTip(wxToolTip* tip)
{
   // HAIKU_LOG("DoSetToolTip this=%p tip=%p", (void*)this, tip);

    wxWindowBase::DoSetToolTip(tip);

    if (!m_haikuView)
        return;

    wxScopedCharBuffer textBuf;
    const char* text = nullptr;

    if (tip)
    {
        textBuf = tip->GetTip().utf8_str();
        text = textBuf.data();
    }

   // HAIKU_LOG("DoSetToolTip text='%s'", text ? text : "");

    if (BWindow* w = m_haikuView->Window())
    {
        if (w->LockLooper())
        {
            m_haikuView->SetToolTip(text);
            w->UnlockLooper();
        }
        return;
    }

    m_haikuView->SetToolTip(text);
}

void wxWindowHaiku::HaikuApplyToolTip(const wxString& text)
{
 //   HAIKU_LOG("HaikuApplyToolTip this=%p text='%s'", this, text);

    if (!m_haikuView)
        return;

    wxScopedCharBuffer textBuf;
    const char* ctext = nullptr;

    if (!text.empty())
    {
        textBuf = text.utf8_str();
        ctext = textBuf.data();
    }

    m_haikuView->SetToolTip(ctext);
}
#endif // wxUSE_TOOLTIPS

// ----------------------------------------------------------------------------
// Text extent
// ----------------------------------------------------------------------------
void wxWindowHaiku::DoGetTextExtent(const wxString& string,
                                    int* x, int* y,
                                    int* descent,
                                    int* externalLeading,
                                    const wxFont* font) const
{
    if (string.empty())
    {
        if (x) *x = 0; if (y) *y = 0;
        if (descent) *descent = 0; if (externalLeading) *externalLeading = 0;
        return;
    }

 //   HAIKU_LOG("DoGetTextExtent this=%p string='%s' font=%p",
   //           this, string, font);

    BFont f;
    if (font && font->IsOk())
    {
        f = font->GetHandle();
    }
    else if (m_haikuView)
    {
        wxHaikuViewLock lock(m_haikuView);
        if (lock.ok())
            m_haikuView->GetFont(&f);
        else
            f = *be_plain_font;
    }
    else
    {
        f = *be_plain_font;
    }

    // wx text extents use advance width and full font line height, not
    // the ink bounds of these particular glyphs. Sizers need room for every row.
    wxScopedCharBuffer utf8 = string.utf8_str();
    font_height metrics;
    f.GetHeight(&metrics);
    if (x) *x = wxRound(f.StringWidth(utf8.data()));
    if (y) *y = wxRound(metrics.ascent + metrics.descent + metrics.leading);
    if (descent) *descent = wxRound(metrics.descent);
    if (externalLeading) *externalLeading = wxRound(metrics.leading);

   // HAIKU_LOG("DoGetTextExtent result x=%d y=%d",
       //       x ? *x : -1, y ? *y : -1);
}

// ----------------------------------------------------------------------------
// Coordinate transforms
// ----------------------------------------------------------------------------
void wxWindowHaiku::DoClientToScreen(int* x, int* y) const
{
  //  HAIKU_LOG("DoClientToScreen this=%p in=(%d,%d)", this,
    //          x ? *x : -1, y ? *y : -1);

    if (!m_haikuView)
        return;

    BWindow* win = m_haikuView->Window();
    if (!win)
        return;

    wxHaikuAutoLock lock(win);

    BPoint pt(x ? *x : 0, y ? *y : 0);
    pt = m_haikuView->ConvertToScreen(pt);
    if (x)
        *x = int(pt.x);
    if (y)
        *y = int(pt.y);

   // HAIKU_LOG("DoClientToScreen out=(%d,%d)", *x, *y);
}

void wxWindowHaiku::DoScreenToClient(int* x, int* y) const
{
 //   HAIKU_LOG("DoScreenToClient this=%p in=(%d,%d)", this,
   //           x ? *x : -1, y ? *y : -1);

    if (!m_haikuView)
        return;

    BWindow* win = m_haikuView->Window();
    if (!win)
        return;

    wxHaikuAutoLock lock(win);

    BPoint pt(x ? *x : 0, y ? *y : 0);
    pt = m_haikuView->ConvertFromScreen(pt);
    if (x)
        *x = int(pt.x);
    if (y)
        *y = int(pt.y);

   // HAIKU_LOG("DoScreenToClient out=(%d,%d)", *x, *y);
}

// ----------------------------------------------------------------------------
// Mouse capture
// ----------------------------------------------------------------------------
void wxWindowHaiku::DoCaptureMouse()
{
  //  HAIKU_LOG("DoCaptureMouse this=%p native=%p", this, GetHandle());

    wxCHECK_RET(GetHandle() != nullptr, "invalid window");
    s_capturedWindow = this;

    if (m_haikuView && m_haikuView->LockLooper())
    {
        m_haikuView->SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
        m_haikuView->UnlockLooper();
    }
}

void wxWindowHaiku::DoReleaseMouse()
{
  // HAIKU_LOG("DoReleaseMouse this=%p native=%p", this, GetHandle());

    wxCHECK_RET(GetHandle() != nullptr, "invalid window");

    if (m_haikuView && m_haikuView->LockLooper())
    {
        m_haikuView->SetMouseEventMask(0);
        m_haikuView->UnlockLooper();
    }

    s_capturedWindow = nullptr;
}

void wxWindowHaiku::HaikuReleaseMouseAndNotify()
{
  //  HAIKU_LOG("HaikuReleaseMouseAndNotify this=%p", this);
    DoReleaseMouse();
    NotifyCaptureLost();
}

// ----------------------------------------------------------------------------
// Freeze/thaw
// ----------------------------------------------------------------------------
void wxWindowHaiku::DoFreeze()
{
 //   HAIKU_LOG("DoFreeze this=%p", this);
}

void wxWindowHaiku::DoThaw()
{
 //   HAIKU_LOG("DoThaw this=%p", this);
    Refresh();
}

// ----------------------------------------------------------------------------
// Position/size
// ----------------------------------------------------------------------------
void wxWindowHaiku::DoGetPosition(int* x, int* y) const
{
  //  HAIKU_LOG("DoGetPosition this=%p", this);

    BView* v = m_haikuScrollView ? static_cast<BView*>(m_haikuScrollView) : m_haikuView;
    if (!v)
    {
        if (x) *x = 0;
        if (y) *y = 0;
        return;
    }

    wxHaikuAutoLock lock(v->Looper());

    BRect f = IsTopLevel() && v->Window() ? v->Window()->Frame() : v->Frame();
    if (x) *x = (int)f.left;
    if (y) *y = (int)f.top;

  //  HAIKU_LOG("DoGetPosition out=(%d,%d)", x ? *x : -1, y ? *y : -1);
}

void wxWindowHaiku::DoSetSize(int x, int y, int width, int height, int sizeFlags)
{
    int oldX, oldY, oldW, oldH;
    GetPosition(&oldX, &oldY);
    GetSize(&oldW, &oldH);
    if (!(sizeFlags & wxSIZE_ALLOW_MINUS_ONE))
    {
        if (x == wxDefaultCoord) x = oldX;
        if (y == wxDefaultCoord) y = oldY;
    }
    const wxSize best = (sizeFlags & wxSIZE_AUTO) ? GetBestSize() : wxSize(oldW, oldH);
    if (width == wxDefaultCoord) width = (sizeFlags & wxSIZE_AUTO_WIDTH) ? best.x : oldW;
    if (height == wxDefaultCoord) height = (sizeFlags & wxSIZE_AUTO_HEIGHT) ? best.y : oldH;
    DoMoveWindow(x, y, width, height);
}

void wxWindowHaiku::DoGetSize(int* w, int* h) const
{
 //   HAIKU_LOG("DoGetSize this=%p", this);

    BView* v = m_haikuScrollView ? static_cast<BView*>(m_haikuScrollView) : m_haikuView;
    if (!v)
    {
        if (w) *w = 0;
        if (h) *h = 0;
        return;
    }

    wxHaikuAutoLock lock(v->Looper());

    BRect f = IsTopLevel() && v->Window() ? v->Window()->Frame() : v->Frame();
    if (w) *w = (int)(f.Width() + 1);
    if (h) *h = (int)(f.Height() + 1);

//    HAIKU_LOG("DoGetSize out=(%d,%d)", w ? *w : -1, h ? *h : -1);
}

void wxWindowHaiku::DoSetClientSize(int width, int height)
{
    const wxSize oldSize = GetClientSize();
    BView* view = HaikuGetNativeViewToAttach();
   // HAIKU_LOG("DoSetClientSize this=%p w=%d h=%d native=%p",
     //         this, width, height, view);

    if (!view)
        return;

    const float w = width > 0 ? (float)(width - 1) : 0.0f;
    const float h = height > 0 ? (float)(height - 1) : 0.0f;

    wxHaikuViewLock lock(view);
    if (!lock.ok())
        return;

    if (IsTopLevel() && view->Window())
    {
        // Client dimensions exclude frame menus, toolbars and status bars.
        const wxSize outerSize = GetSize();
        view->Window()->ResizeTo(w + outerSize.x - oldSize.x,
                                 h + outerSize.y - oldSize.y);
    }
    view->ResizeTo(w, h);
    view->Invalidate();

    if (GetClientSize() != oldSize)
    {
        wxSizeEvent event(GetSize(), GetId());
        event.SetEventObject(this);
        HandleWindowEvent(event);
    }

  //  HaikuDumpView("DoSetClientSize after", view);
}

void wxWindowHaiku::DoGetClientSize(int* width, int* height) const
{
  //  HAIKU_LOG("DoGetClientSize enter");//=%p native=%p", (void*)this, m_haikuView);

    if (!m_haikuView)
    {
        if (width) *width = 0;
        if (height) *height = 0;
        return;
    }

    wxHaikuViewLock lock(m_haikuView);
    if (!lock.ok())
    {
        if (width) *width = 0;
        if (height) *height = 0;
        return;
    }

    BRect b = m_haikuView->Bounds();
    if (width) *width = (int)(b.Width() + 1);
    if (height) *height = (int)(b.Height() + 1);

  //  HAIKU_LOG("DoGetClientSize out");//=(%d,%d)",
           //   width ? *width : -1, height ? *height : -1);
}

void wxWindowHaiku::DoMoveWindow(int x, int y, int width, int height)
{
    BView* v = HaikuGetNativeViewToAttach();
    if (!v)
        return;

    const wxSize oldSize = GetSize();

    const float nativeW = width  > 0 ? (float)width  - 1.0f : 0.0f;
    const float nativeH = height > 0 ? (float)height - 1.0f : 0.0f;

    BWindow* win = v->Window();
    bool locked = false;
    if (win && !win->IsLocked())
    {
        locked = win->LockLooper();
        if (!locked)
            return;
    }

    if (IsTopLevel() && win)
    {
        // A top-level position is a screen position of the BWindow. Moving
        // its root BView instead displaces and clips all dialog controls.
        win->MoveTo((float)x, (float)y);
        win->ResizeTo(nativeW, nativeH);
        if (wxDynamicCast(this, wxFrame))
            wxHaikuFrameLayoutChrome(win);
        else if (!v->Parent())
        {
            v->MoveTo(0, 0);
            v->ResizeTo(win->Bounds().Width(), win->Bounds().Height());
        }
    }
    else
    {
        v->MoveTo((float)x, (float)y);
        v->ResizeTo(nativeW, nativeH);
    }
    v->Invalidate();

    if (locked)
        win->UnlockLooper();

    if (GetSize() != oldSize)
    {
        wxSizeEvent event(GetSize(), GetId());
        event.SetEventObject(this);
        HandleWindowEvent(event);
    }
}

// ----------------------------------------------------------------------------
// Menus
// ----------------------------------------------------------------------------
#if wxUSE_MENUS
// Copy menu presentation into a temporary native popup. The original native
// menu may be owned by a menubar: never transfer its items or submenus.
static void wxHaikuPopulatePopup(BMenu* native,wxMenu* menu)
{
    for (auto node=menu->GetMenuItems().GetFirst(); node; node=node->GetNext())
    {
        wxMenuItem* item=node->GetData();
        if (item->IsSeparator()) { native->AddSeparatorItem(); continue; }
        BMenuItem* entry;
        if (item->GetSubMenu())
        {
            BMenu* sub=new BMenu(item->GetItemLabelText().utf8_str());
            wxHaikuPopulatePopup(sub,item->GetSubMenu());
            entry=new BMenuItem(sub);
        }
        else
        {
            BMessage* command=new BMessage('wxPM');
            command->AddInt32("wxid",item->GetId());
            entry=new BMenuItem(item->GetItemLabelText().utf8_str(),command);
        }
        entry->SetEnabled(item->IsEnabled());
        if (item->IsCheckable()) entry->SetMarked(item->IsChecked());
        native->AddItem(entry);
    }
}
bool wxWindowHaiku::DoPopupMenu(wxMenu* menu,int x,int y)
{
    wxPoint screen=(x == wxDefaultCoord && y == wxDefaultCoord)
        ? wxGetMousePosition() : ClientToScreen(wxPoint(x,y));
    BPopUpMenu popup("wxPopup",false,false);
    wxHaikuPopulatePopup(&popup,menu);
    wxMenuEvent opened(wxEVT_MENU_OPEN,0,menu);
    wxMenu::ProcessMenuEvent(menu,opened,menu->GetWindow());
    BMenuItem* selected=popup.Go(BPoint(screen.x,screen.y),false,false,false);
    int32 id=wxID_NONE;
    if (selected && selected->Message()) selected->Message()->FindInt32("wxid",&id);
    wxMenuEvent closed(wxEVT_MENU_CLOSE,0,menu);
    wxMenu::ProcessMenuEvent(menu,closed,menu->GetWindow());
    if (id != wxID_NONE)
    {
        wxMenuItem* item=menu->FindItem(id);
        int checked=-1;
        if (item && item->IsCheckable())
        {
            if (item->GetKind()==wxITEM_RADIO)
            {
                // Uncheck the contiguous radio group in the owning menu.
                wxMenu* owner=item->GetMenu();
                auto current=owner->GetMenuItems().Find(item);
                auto first=current;
                while (first->GetPrevious() && first->GetPrevious()->GetData()->GetKind()==wxITEM_RADIO)
                    first=first->GetPrevious();
                for (auto n=first;n && n->GetData()->GetKind()==wxITEM_RADIO;n=n->GetNext())
                    n->GetData()->Check(n->GetData()==item);
            }
            else item->Check(!item->IsChecked());
            checked=item->IsChecked();
        }
        menu->SendEvent(id,checked);
    }
    return true;
}
#endif

// ----------------------------------------------------------------------------
// Private helpers
// ----------------------------------------------------------------------------
BView* wxWindowHaiku::HaikuGetClientView() const
{
    return m_haikuView;
}

BView* wxWindowHaiku::HaikuGetNativeViewToAttach() const
{
    return m_haikuScrollView
             ? static_cast<BView*>(m_haikuScrollView)
             : m_haikuView;
}

namespace
{
    inline orientation HaikuOrientation(int orient)
    {
        return (orient == wxHORIZONTAL) ? B_HORIZONTAL : B_VERTICAL;
    }
}

BScrollBar* wxWindowHaiku::HaikuGetScrollBar(int orientationWx) const
{


    if (!m_haikuScrollView)
        return nullptr;

    return m_haikuScrollView->ScrollBar(HaikuOrientation(orientationWx));
}

