/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/frame.cpp
// Purpose:     wxFrame implementation for Haiku
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"
#include "wx/haiku/private/dispatch.h"
#include <Messenger.h>
#include <cstdio>

#ifndef WX_PRECOMP
    #include "wx/menu.h"
    #include "wx/toolbar.h"
    #include "wx/statusbr.h"
    #include "wx/log.h"
#endif


#include "wx/frame.h"
#include "wx/app.h"
#include "wx/textctrl.h"
#include "wx/accel.h"
#include "wx/haiku/private/utils.h"
#include "wx/haiku/private/framewin.h"
#include "wx/haiku/private/windowview.h"

#include <Application.h>
#include <InterfaceKit.h>
#include <Window.h>
#include <View.h>
#include <MenuBar.h>
#include <Menu.h>
#include <MenuItem.h>
#include <stdio.h>

#if defined(WX_DEBUG_HAIKU) && WX_DEBUG_HAIKU
    #define HAIKU_LOG(msg, ...) \
        printf("[HAIKU-FRAME:%s:%d] " msg "\n", __func__, __LINE__, ##__VA_ARGS__)
#else
    #define HAIKU_LOG(msg, ...) ((void)0)
#endif

static const uint32 kWxMenuInvoke = 'wMnu';
static const uint32 kWxMenuHighlight = 'wMhi';

// Haiku keymaps may assign the physical Ctrl key to Command. Match either
// native shortcut modifier to wx Ctrl, using each menu item's declared binding.
static int HaikuFindMenuAccelerator(wxMenu* menu, int key, int flags)
{
    for (auto node = menu->GetMenuItems().GetFirst(); node; node = node->GetNext())
    {
        wxMenuItem* item = node->GetData();
        if (!item->IsEnabled())
            continue;
        if (item->GetSubMenu())
        {
            const int found = HaikuFindMenuAccelerator(item->GetSubMenu(), key, flags);
            if (found != wxID_NONE)
                return found;
        }
        std::unique_ptr<wxAcceleratorEntry> accel(item->GetAccel());
        if (accel && accel->GetKeyCode() == key && accel->GetFlags() == flags)
            return item->GetId();
    }
    return wxID_NONE;
}


// ----------------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------------

#if defined(WX_DEBUG_HAIKU) && WX_DEBUG_HAIKU

static void HaikuLogViewGeometry(const char* name, BView* v)
{
    if (!v)
    {
        printf("%s: view=(nil)\n", name);
        return;
    }

    const BRect f = v->Frame();
    const BRect b = v->Bounds();

    printf("%s: ptr=%p name='%s' hidden=%d parent=%p window=%p "
           "frame=(%.1f,%.1f,%.1f,%.1f) size=(%.1f x %.1f) "
           "bounds=(%.1f,%.1f,%.1f,%.1f) boundsSize=(%.1f x %.1f)\n",
           name,
           v,
           v->Name() ? v->Name() : "",
           v->IsHidden(),
           v->Parent(),
           v->Window(),
           f.left, f.top, f.right, f.bottom,
           f.Width() + 1.0f, f.Height() + 1.0f,
           b.left, b.top, b.right, b.bottom,
           b.Width() + 1.0f, b.Height() + 1.0f);
}

static void HaikuLogWindowGeometry(const char* name, BWindow* w)
{
    if (!w)
    {
        printf("%s: window=(nil)\n", name);
        return;
    }

    const BRect f = w->Frame();
    const BRect b = w->Bounds();

    printf("%s: ptr=%p title='%s' hidden=%d active=%d childCount=%ld "
           "frame=(%.1f,%.1f,%.1f,%.1f) size=(%.1f x %.1f) "
           "bounds=(%.1f,%.1f,%.1f,%.1f) boundsSize=(%.1f x %.1f)\n",
           name,
           w,
           w->Title() ? w->Title() : "",
           w->IsHidden(),
           w->IsActive(),
           (long)w->CountChildren(),
           f.left, f.top, f.right, f.bottom,
           f.Width() + 1.0f, f.Height() + 1.0f,
           b.left, b.top, b.right, b.bottom,
           b.Width() + 1.0f, b.Height() + 1.0f);
}

static void HaikuLogMenuBar(const char* name, BMenuBar* bar)
{
    if (!bar)
    {
        printf("%s: menubar=(nil)\n", name);
        return;
    }

    const BRect f = bar->Frame();

    printf("%s: ptr=%p name='%s' hidden=%d parent=%p window=%p count=%ld "
           "frame=(%.1f,%.1f,%.1f,%.1f) size=(%.1f x %.1f)\n",
           name,
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
        printf("  [%ld] item=%p label='%s' submenu=%p enabled=%d marked=%d\n",
               (long)i,
               item,
               item && item->Label() ? item->Label() : "",
               item ? item->Submenu() : nullptr,
               item ? item->IsEnabled() : 0,
               item ? item->IsMarked() : 0);
    }
}

#else
static inline void HaikuLogViewGeometry(const char*, BView*) {}
static inline void HaikuLogWindowGeometry(const char*, BWindow*) {}
static inline void HaikuLogMenuBar(const char*, BMenuBar*) {}
#endif

static void HaikuSizeMenuBarToPreferred(BMenuBar* bar, float winWidth)
{
    HAIKU_LOG("HaikuSizeMenuBarToPreferred ENTER bar=%p winWidth=%.1f",
              bar, winWidth);

    if (!bar)
        return;

    bar->ResizeToPreferred();

    float prefW = 0.0f, prefH = 0.0f;
    bar->GetPreferredSize(&prefW, &prefH);

    if (prefH <= 0.0f)
        prefH = 20.0f;

    bar->MoveTo(0.0f, 0.0f);
    bar->ResizeTo(winWidth, prefH);
    bar->Show();
    bar->Invalidate();

    HaikuLogMenuBar("HaikuSizeMenuBarToPreferred after", bar);
}







// ----------------------------------------------------------------------------
// Client view
// ----------------------------------------------------------------------------

class wxHaikuFrameClientView : public BView
{
public:
    wxHaikuFrameClientView(BRect rect, wxFrame* handler)
        : BView(rect, "wx_client", B_FOLLOW_ALL_SIDES, B_WILL_DRAW),
          m_handler(handler)
    {
        HAIKU_LOG("ClientView ctor this=%p handler=%p rect=(%.1f,%.1f,%.1f,%.1f)",
                  this, handler, rect.left, rect.top, rect.right, rect.bottom);
        SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
        HaikuLogViewGeometry("ClientView ctor", this);
    }

private:
    wxFrame* m_handler = nullptr;
};

// ----------------------------------------------------------------------------
// Native frame window
// ----------------------------------------------------------------------------

class wxHaikuFrameWindow : public BWindow
{
public:
    wxHaikuFrameWindow(BRect frame,
                       const char* title,
                       window_look look,
                       window_feel feel,
                       uint32 flags,
                       wxFrame* handler)
        : BWindow(frame, title, look, feel, flags),
          m_handler(handler)
    {
        HAIKU_LOG("Native frame ctor this=%p handler=%p title='%s' "
                  "frame=(%.1f,%.1f,%.1f,%.1f) look=%ld feel=%ld flags=0x%08lx",
                  this, handler, title ? title : "",
                  frame.left, frame.top, frame.right, frame.bottom,
                  (long)look, (long)feel, (unsigned long)flags);

        m_clientView = new wxHaikuWindowView(Bounds(), "wx_client",
                                            B_FOLLOW_ALL_SIDES,
                                            B_WILL_DRAW | B_FRAME_EVENTS);
        AddChild(m_clientView);

        HaikuLogWindowGeometry("Native frame ctor window", this);
        HaikuLogViewGeometry("Native frame ctor client", m_clientView);

        LayoutChrome();
    }
    
    void SetHandler(wxFrame* handler)
     {
    m_handler = handler;
     }
    
    
    BView* GetClientView() const { return m_clientView; }

    void LayoutChrome()
    {
        HAIKU_LOG("LayoutChrome ENTER this=%p handler=%p", this, m_handler);
        HaikuLogWindowGeometry("LayoutChrome window", this);

        float top = 0.0f;
        float bottom = 0.0f;

        BMenuBar* mb = KeyMenuBar();
        BView* tb = FindView("toolbar");
        if (tb && tb->Parent()) tb = nullptr;
        BView* sb = FindView("statusbar");

        HaikuLogMenuBar("LayoutChrome menubar before", mb);
        HaikuLogViewGeometry("LayoutChrome toolbar before", tb);
        HaikuLogViewGeometry("LayoutChrome statusbar before", sb);
        HaikuLogViewGeometry("LayoutChrome client before", m_clientView);

        if (mb)
        {
            mb->MoveTo(0.0f, 0.0f);
            mb->ResizeTo(Bounds().Width(), mb->Frame().Height());
            top += mb->Frame().Height() + 1.0f;
        }

        if (tb)
        {
            const BRect f = tb->Frame();
            const bool vertical = m_handler && m_handler->GetToolBar() && m_handler->GetToolBar()->HasFlag(wxTB_VERTICAL);

            HAIKU_LOG("LayoutChrome toolbar vertical=%d", vertical);

            if (!vertical)
            {
                const float h = f.Height();
                tb->MoveTo(0.0f, top);
                tb->ResizeTo(Bounds().Width(), h);
                top += h + 1.0f;
            }
            else
            {
                tb->ResizeTo(f.Width(), Bounds().Height() - top - bottom);
                tb->MoveTo(f.left, top);
            }
        }

        if (sb)
        {
            const float h = sb->Frame().Height();
            sb->MoveTo(0.0f, Bounds().Height() - h);
            sb->ResizeTo(Bounds().Width(), h);
            bottom += h + 1.0f;
        }

        BRect client = Bounds();

        if (tb)
        {
            const BRect tf = tb->Frame();
            const bool vertical = m_handler && m_handler->GetToolBar() && m_handler->GetToolBar()->HasFlag(wxTB_VERTICAL);

            if (vertical)
            {
                if (tf.left <= 0.0f)
                    client.left += tf.Width() + 1.0f;
                else
                    client.right -= tf.Width() + 1.0f;
            }
        }

        client.top += top;
        client.bottom -= bottom;

        if (client.bottom < client.top)
            client.bottom = client.top;
        if (client.right < client.left)
            client.right = client.left;

        m_clientView->MoveTo(client.LeftTop());
        m_clientView->ResizeTo(client.Width(), client.Height());

        HaikuLogMenuBar("LayoutChrome menubar after", mb);
        HaikuLogViewGeometry("LayoutChrome toolbar after", tb);
        HaikuLogViewGeometry("LayoutChrome statusbar after", sb);
        HaikuLogViewGeometry("LayoutChrome client after", m_clientView);

        if (mb)
            mb->Invalidate();

        if (tb)
            tb->Invalidate();

        if (sb)
            sb->Invalidate();

        m_clientView->Invalidate();

        HAIKU_LOG("LayoutChrome EXIT top=%.1f bottom=%.1f client=(%.1f,%.1f,%.1f,%.1f)",
                  top, bottom,
                  client.left, client.top, client.right, client.bottom);
    }

    void DispatchMessage(BMessage* message, BHandler* target) override
    {
        // Native Quit() can terminate a looper thread without unwinding C++
        // guards. Protect wx close handling in QuitRequested(), not Quit().
        if (message && message->what == B_QUIT_REQUESTED)
        {
            BWindow::DispatchMessage(message, target);
            return;
        }
        BMessenger destination(target);
        wxHaikuGuiDispatchLock guard(this);
        // IsValid() checks the port, not the handler token. The port may
        // remain valid after a timer/view handler was removed during the wait.
        BHandler* resolved = target ? destination.Target(nullptr) : nullptr;
        if (target && (!resolved || resolved->Looper() != this))
            return;
        if (message && message->what == 'wxUp' && resolved == this)
        {
            UpdateIfNeeded();
            message->SendReply(B_REPLY);
            return;
        }
        // Layout can change while the native looper is released to acquire
        // the GUI lock. A queued press can then refer to a view which no
        // longer covers the pointer. Recheck these presses against the current
        // hierarchy, but preserve capture (including drags outside a view).
        if (message && message->what == B_MOUSE_DOWN && !wxWindow::GetCapture())
        {
            BView* view = dynamic_cast<BView*>(resolved);
            BPoint screen;
            if (view && message->FindPoint("screen_where", &screen) == B_OK &&
                !view->Bounds().Contains(view->ConvertFromScreen(screen)))
            {
                if (BView* hit = FindView(ConvertFromScreen(screen)))
                {
                    const BPoint local = hit->ConvertFromScreen(screen);
                    message->RemoveName("where");
                    message->AddPoint("where", local);
                    message->RemoveName("be:view_where");
                    message->AddPoint("be:view_where", local);
                    resolved = hit;
                }
            }
        }
        // A retargeted press must be paired with its release even when the
        // native queue still names the old recipient.
        if (message && message->what == B_MOUSE_UP)
        {
            wxWindow* capture = wxWindow::GetCapture();
            BView* captured = capture ? static_cast<BView*>(capture->GetHandle()) : nullptr;
            BPoint screen;
            if (captured && captured->Window() == this &&
                message->FindPoint("screen_where", &screen) == B_OK)
            {
                const BPoint local = captured->ConvertFromScreen(screen);
                message->RemoveName("where");
                message->AddPoint("where", local);
                message->RemoveName("be:view_where");
                message->AddPoint("be:view_where", local);
                resolved = captured;
            }
        }
        // Deliver retargeted generic wx mouse events with the corrected message
        // directly, avoiding another native callback lookup of CurrentMessage().
        if (message && (message->what == B_MOUSE_DOWN || message->what == B_MOUSE_UP) && resolved != target)
        {
            BView* view = dynamic_cast<wxHaikuWindowView*>(resolved);
            wxWindowHaiku* owner = view ? wxWindowHaiku::HaikuRetrieveWindowPointer(view) : nullptr;
            if (owner)
            {
                if (message->what == B_MOUSE_DOWN)
                    owner->HaikuHandleMouseDown(view, message);
                else
                    owner->HaikuHandleMouseUp(view, message);
                return;
            }
        }
        if (message && message->what == B_KEY_DOWN && m_handler && m_handler->GetMenuBar())
        {
            int32 raw = 0, mods = 0;
            message->FindInt32("raw_char", &raw);
            message->FindInt32("modifiers", &mods);
            if (mods & (B_COMMAND_KEY | B_CONTROL_KEY))
            {
                int flags = wxACCEL_CTRL;
                if (mods & B_SHIFT_KEY) flags |= wxACCEL_SHIFT;
                if (mods & B_OPTION_KEY) flags |= wxACCEL_ALT;
                int key = (raw >= 'a' && raw <= 'z') ? raw - 'a' + 'A' : raw;
                wxMenuBar* bar = m_handler->GetMenuBar();
                for (size_t i = 0; i < bar->GetMenuCount(); ++i)
                {
                    if (!bar->IsEnabledTop(i)) continue;
                    const int id = HaikuFindMenuAccelerator(bar->GetMenu(i), key, flags);
                    if (id != wxID_NONE)
                    {
                        m_handler->CallAfter([frame = m_handler, id]()
                        {
                            frame->ProcessCommand(id);
                        
                        });
                        return;
                    }
                }
            }
        }
#if wxUSE_TEXTCTRL
        // Native text views don't use wxHaikuWindowView's key forwarding.
        // Give validators and generic spin-control handlers the same key/char
        // events before falling back to BTextView's normal editing behavior.
        if (message && (message->what == B_KEY_DOWN || message->what == B_KEY_UP))
        {
            for (BView* view = dynamic_cast<BView*>(resolved); view; view = view->Parent())
            {
                if (auto* owner = wxWindowHaiku::HaikuRetrieveWindowPointer(view))
                {
                    if (dynamic_cast<wxTextCtrl*>(owner))
                    {
                        const bool handled = message->what == B_KEY_DOWN
                            ? owner->HaikuHandleKeyDown(view, message)
                            : owner->HaikuHandleKeyUp(view, message);
                        if (handled) return;
                    }
                    break;
                }
            }
        }
#endif
        BWindow::DispatchMessage(message, resolved);
        wxHaikuSyncTextFocus(this);
    }

    void FrameResized(float newWidth, float newHeight) override
    {
        BWindow::FrameResized(newWidth, newHeight);
        LayoutChrome();

        if (m_handler)
        {
            wxSizeEvent ev(wxSize((int)(newWidth + 1), (int)(newHeight + 1)));
            ev.SetEventObject(m_handler);
            m_handler->GetEventHandler()->ProcessEvent(ev);
        }

        HaikuLogWindowGeometry("FrameResized after", this);
    }

    void MessageReceived(BMessage* msg) override
    {
        HAIKU_LOG("MessageReceived this=%p msg=%p what='%4.4s'/0x%08lx",
                  this,
                  msg,
                  msg ? (const char*)&msg->what : "null",
                  msg ? (unsigned long)msg->what : 0UL);

        if (msg && msg->what == kWxMenuInvoke)
        {
            int32 id;
            if (msg->FindInt32("wx_id", &id) == B_OK && m_handler)
            {
                HAIKU_LOG("MessageReceived menu invoke wx_id=%ld handler=%p",
                          (long)id, m_handler);

                // Native menu messages run on a BWindow thread. Queue command
                // handlers to the main wx thread (required by wxExecute).
                m_handler->CallAfter([frame = m_handler, id]()
                {
                    // Dispatch through the owning menu before the frame.
                    // KiCad ACTION_MENU handlers are attached to the menu.
                    frame->ProcessCommand((int)id);
                });
                if (wxTheApp)
                    wxTheApp->WakeUpIdle();
                return;
            }
        }

        if (msg && msg->what == kWxMenuHighlight && m_handler)
        {
            int32 id = wxID_NONE;
            msg->FindInt32("wx_id", &id);

            wxMenuEvent evt(wxEVT_MENU_HIGHLIGHT, (int)id);
            evt.SetEventObject(m_handler);
            m_handler->GetEventHandler()->ProcessEvent(evt);
            return;
        }

        BWindow::MessageReceived(msg);
    }

    bool QuitRequested() override
    {
        wxHaikuGuiDispatchLock guard(this);
        HAIKU_LOG("QuitRequested ENTER this=%p handler=%p", this, m_handler);
        if (!m_handler)
        {
            HAIKU_LOG("QuitRequested no handler -> allow");
            return true;
        }

        const bool closed = m_handler->Close();
        HAIKU_LOG("QuitRequested Close=%d", closed);
        if (closed && wxTheApp)
            wxTheApp->WakeUpIdle();

        // wxWidgets now owns deferred wrapper destruction. The destructor
        // clears the native handler and posts B_QUIT_REQUESTED safely.
        return false;
    }

private:
    wxFrame* m_handler = nullptr;
    BView* m_clientView = nullptr;
};

BWindow* wxHaikuCreateFrameWindow(const BRect& frame,
                                  const char* title,
                                  window_look look,
                                  window_feel feel,
                                  uint32 flags,
                                  wxFrame* handler)
{
    HAIKU_LOG("CreateFrameWindow ENTER handler=%p title='%s' "
              "frame=(%.1f,%.1f,%.1f,%.1f)",
              handler, title ? title : "",
              frame.left, frame.top, frame.right, frame.bottom);

    BWindow* window = new wxHaikuFrameWindow(frame, title, look, feel, flags, handler);

    HAIKU_LOG("CreateFrameWindow EXIT handler=%p native=%p", handler, window);
    HaikuLogWindowGeometry("CreateFrameWindow native", window);

    return window;
}

BView* wxHaikuFrameGetClientView(BWindow* window)
{
    HAIKU_LOG("GetClientView window=%p", window);

    auto* frameWin = dynamic_cast<wxHaikuFrameWindow*>(window);
    BView* view = frameWin ? frameWin->GetClientView() : nullptr;

    HaikuLogViewGeometry("GetClientView result", view);
    return view;
}

void wxHaikuFrameLayoutChrome(BWindow* window)
{
    HAIKU_LOG("FrameLayoutChrome window=%p", window);

    auto* frameWin = dynamic_cast<wxHaikuFrameWindow*>(window);
    if (frameWin)
        frameWin->LayoutChrome();
}


static void wxHaikuClearNativeFrameHandler(BWindow* win)
{
    if ( !win )
        return;

    BWindow* native = win;

    if ( wxHaikuFrameWindow* frameWin =
            dynamic_cast<wxHaikuFrameWindow*>(native) )
    {
        if ( native->LockLooper() )
        {
            frameWin->SetHandler(nullptr);
            native->UnlockLooper();
        }
        else
        {
            frameWin->SetHandler(nullptr);
        }
    }

    // Keep the native window alive while wxFrameBase destroys its bars and
    // wxTopLevelWindowHaiku destroys its children.
}


// ----------------------------------------------------------------------------
// wxFrame
// ----------------------------------------------------------------------------

wxFrame::~wxFrame()
{
    HAIKU_LOG("~wxFrame ENTER this=%p native=%p menubar=%p toolbar=%p statusbar=%p",
              (void*)this,
              m_haikuWindow,
              m_frameMenuBar,
              m_frameToolBar,
              m_frameStatusBar);

    wxHaikuClearNativeFrameHandler(m_haikuWindow);

    HAIKU_LOG("~wxFrame EXIT this=%p native=%p",
              (void*)this, m_haikuWindow);
}

bool wxFrame::Create(wxWindow* parent,
                     wxWindowID id,
                     const wxString& title,
                     const wxPoint& pos,
                     const wxSize& size,
                     long style,
                     const wxString& name)
{
    HAIKU_LOG("wxFrame::Create ENTER this=%p parent=%p id=%d  "
              "pos=(%d,%d) size=(%d,%d) style=0x%lx ",
              this, parent, (int)id, 
              pos.x, pos.y, size.x, size.y,
              (unsigned long)style );

    if (!wxFrameBase::Create(parent, id, title, pos, size, style, name))
    {
        HAIKU_LOG("wxFrame::Create FAILED in base");
        return false;
    }

    SetWindowStyleFlag(style);
    Layout();

    HAIKU_LOG("wxFrame::Create EXIT this=%p native=%p", this, m_haikuWindow);
    HaikuLogWindowGeometry("wxFrame::Create native", m_haikuWindow);

    return true;
}

// ----------------------------------------------------------------------------
// client geometry
// ----------------------------------------------------------------------------

void wxFrame::DoGetClientSize(int* width, int* height) const
{
    if (!m_haikuWindow)
    {
        wxFrameBase::DoGetClientSize(width, height);
        return;
    }

    int w = 0;
    int h = 0;

    if (m_haikuWindow->LockLooper())
    {
        if (BView* client = wxHaikuFrameGetClientView(m_haikuWindow))
        {
            const BRect bounds = client->Bounds();
            w = (int)(bounds.Width() + 1.0f);
            h = (int)(bounds.Height() + 1.0f);
        }
        else
        {
            const BRect bounds = m_haikuWindow->Bounds();
            w = (int)(bounds.Width() + 1.0f);
            h = (int)(bounds.Height() + 1.0f);
        }
        m_haikuWindow->UnlockLooper();
    }
    else
    {
        wxFrameBase::DoGetClientSize(&w, &h);
    }

    if (width)
        *width = wxMax(0, w);
    if (height)
        *height = wxMax(0, h);
}

void wxFrame::DoSetClientSize(int width, int height)
{
    HAIKU_LOG("DoSetClientSize ENTER this=%p native=%p width=%d height=%d",
              this, m_haikuWindow, width, height);

    if (!m_haikuWindow)
    {
        wxFrameBase::DoSetClientSize(width, height);
        return;
    }

    if (!m_haikuWindow->LockLooper())
    {
        HAIKU_LOG("DoSetClientSize unable to lock looper");
        return;
    }

    float extraW = 0.0f;
    float extraH = 0.0f;

    if (BMenuBar* mb = m_haikuWindow->KeyMenuBar())
        extraH += mb->Frame().Height() + 1.0f;

    if (BView* tb = m_haikuWindow->FindView("toolbar"); tb && !tb->Parent())
    {
        const BRect tf = tb->Frame();
        const bool vertical = GetToolBar() && GetToolBar()->HasFlag(wxTB_VERTICAL);

        if (vertical)
            extraW += tf.Width() + 1.0f;
        else
            extraH += tf.Height() + 1.0f;
    }

    if (BView* sb = m_haikuWindow->FindView("statusbar"))
        extraH += sb->Frame().Height() + 1.0f;

    const BRect frame = m_haikuWindow->Frame();

    HAIKU_LOG("DoSetClientSize extras extraW=%.1f extraH=%.1f", extraW, extraH);

    m_haikuWindow->MoveTo(frame.left, frame.top);
    m_haikuWindow->ResizeTo((float)width + extraW - 1.0f,
                            (float)height + extraH - 1.0f);

    wxHaikuFrameLayoutChrome(m_haikuWindow);
    HaikuLogWindowGeometry("DoSetClientSize after resize", m_haikuWindow);

    m_haikuWindow->UnlockLooper();

    HAIKU_LOG("DoSetClientSize EXIT");
}

// ----------------------------------------------------------------------------
// menu bar
// ----------------------------------------------------------------------------

void wxFrame::SetMenuBar(wxMenuBar* menuBar)
{
    if (menuBar == GetMenuBar())
        return;

    HAIKU_LOG("SetMenuBar ENTER this=%p native=%p old=%p new=%p",
              this, m_haikuWindow, m_frameMenuBar, menuBar);

    if (m_haikuWindow)
    {
        bool locked = false;
        if (!m_haikuWindow->IsLocked())
            locked = m_haikuWindow->LockLooper();

        if (locked || m_haikuWindow->IsLocked())
        {
            if (BMenuBar* oldBar = m_haikuWindow->KeyMenuBar())
            {
                oldBar->RemoveSelf();
                m_haikuWindow->SetKeyMenuBar(nullptr);
            }

            wxHaikuFrameLayoutChrome(m_haikuWindow);
            m_haikuWindow->UpdateIfNeeded();
        }

        if (locked)
            m_haikuWindow->UnlockLooper();
    }

    wxFrameBase::SetMenuBar(menuBar);

    if (m_haikuWindow)
    {
        bool locked = false;
        if (!m_haikuWindow->IsLocked())
            locked = m_haikuWindow->LockLooper();

        if (locked || m_haikuWindow->IsLocked())
        {
            wxHaikuFrameLayoutChrome(m_haikuWindow);
            m_haikuWindow->UpdateIfNeeded();
        }

        if (locked)
            m_haikuWindow->UnlockLooper();
    }

    HAIKU_LOG("SetMenuBar EXIT frameMenuBar=%p", m_frameMenuBar);
}

void wxFrame::DetachMenuBar()
{
    HAIKU_LOG("DetachMenuBar ENTER this=%p native=%p frameMenuBar=%p",
              this, m_haikuWindow, m_frameMenuBar);

    if (m_haikuWindow && m_haikuWindow->LockLooper())
    {
        if (BMenuBar* oldBar = m_haikuWindow->KeyMenuBar())
        {
            HAIKU_LOG("DetachMenuBar removing native bar=%p", oldBar);
            HaikuLogMenuBar("DetachMenuBar native before remove", oldBar);

            oldBar->RemoveSelf();
            m_haikuWindow->SetKeyMenuBar(nullptr);
        }

        wxHaikuFrameLayoutChrome(m_haikuWindow);
        m_haikuWindow->UpdateIfNeeded();
        m_haikuWindow->UnlockLooper();
    }

    wxFrameBase::DetachMenuBar();

    HAIKU_LOG("DetachMenuBar EXIT");
}

#if wxUSE_MENUS_NATIVE
void wxFrame::AttachMenuBar(wxMenuBar* menuBar)
{
    HAIKU_LOG("AttachMenuBar ENTER this=%p menuBar=%p nativeWin=%p",
              this, menuBar, m_haikuWindow);

    wxFrameBase::AttachMenuBar(menuBar);

    if (!m_haikuWindow || !menuBar)
    {
        HAIKU_LOG("AttachMenuBar EXIT early native=%p menuBar=%p",
                  m_haikuWindow, menuBar);
        return;
    }

    BMenuBar* nativeBar = menuBar->GetBMenuBar();
    if (!nativeBar)
    {
        HAIKU_LOG("AttachMenuBar EXIT no native menubar");
        return;
    }

    if (!m_haikuWindow->LockLooper())
    {
        HAIKU_LOG("AttachMenuBar EXIT unable to lock looper");
        return;
    }

    HaikuLogWindowGeometry("AttachMenuBar window before", m_haikuWindow);
    HaikuLogMenuBar("AttachMenuBar native before attach", nativeBar);

    if (nativeBar->Parent() || nativeBar->Window())
    {
        HAIKU_LOG("AttachMenuBar removing native bar from old parent=%p window=%p",
                  nativeBar->Parent(), nativeBar->Window());
        nativeBar->RemoveSelf();
    }

    BView* client = wxHaikuFrameGetClientView(m_haikuWindow);
    HaikuLogViewGeometry("AttachMenuBar client view", client);

    if (client)
        m_haikuWindow->AddChild(nativeBar, client);
    else
        m_haikuWindow->AddChild(nativeBar);

    m_haikuWindow->SetKeyMenuBar(nativeBar);
    nativeBar->SetTargetForItems(m_haikuWindow);

    for (int32 i = 0; i < nativeBar->CountItems(); ++i)
    {
        if (BMenuItem* topItem = nativeBar->ItemAt(i))
        {
            if (BMenu* sub = topItem->Submenu())
            {
                HAIKU_LOG("AttachMenuBar set submenu target i=%ld submenu=%p label='%s'",
                          (long)i, sub, topItem->Label() ? topItem->Label() : "");
                sub->SetTargetForItems(m_haikuWindow);
            }
        }
    }

    nativeBar->MoveTo(0.0f, 0.0f);
    HaikuSizeMenuBarToPreferred(nativeBar, m_haikuWindow->Bounds().Width());
    nativeBar->Invalidate();

    HaikuLogMenuBar("AttachMenuBar native after direct attach", nativeBar);

    wxHaikuFrameLayoutChrome(m_haikuWindow);
    m_haikuWindow->UpdateIfNeeded();

    HaikuLogMenuBar("AttachMenuBar native after layout", nativeBar);
    HaikuLogWindowGeometry("AttachMenuBar window after", m_haikuWindow);

    m_haikuWindow->UnlockLooper();

    HAIKU_LOG("AttachMenuBar EXIT");
}
#endif // wxUSE_MENUS_NATIVE

// ----------------------------------------------------------------------------
// toolbar / statusbar
// ----------------------------------------------------------------------------

#if wxUSE_TOOLBAR
void wxFrame::SetToolBar(wxToolBar* toolbar)
{
    HAIKU_LOG("SetToolBar ENTER this=%p native=%p toolbar=%p",
              this, m_haikuWindow, toolbar);

    if (m_haikuWindow && m_haikuWindow->LockLooper())
    {
        if (BView* old = m_frameToolBar ? m_frameToolBar->GetHandle() : nullptr)
        {
            HAIKU_LOG("SetToolBar removing old toolbar=%p", old);
            HaikuLogViewGeometry("SetToolBar old before remove", old);
            old->RemoveSelf();
        }

        if (toolbar)
        {
            BView* nativeTool = toolbar->GetHandle();
            HAIKU_LOG("SetToolBar nativeTool=%p vertical=%d right=%d",
                      nativeTool,
                      toolbar->IsVertical(),
                      toolbar->HasFlag(wxTB_RIGHT));

            if (nativeTool)
            {
                nativeTool->SetName("toolbar");
                nativeTool->ResizeToPreferred();

                if (nativeTool->Parent() || nativeTool->Window())
                {
                    HAIKU_LOG("SetToolBar removing tool from old parent=%p window=%p",
                              nativeTool->Parent(), nativeTool->Window());
                    nativeTool->RemoveSelf();
                }

                const bool rightSide = toolbar->HasFlag(wxTB_RIGHT);

                if (toolbar->IsVertical())
                {
                    const float w = nativeTool->Bounds().Width() + 1.0f;
                    const float x = rightSide ? (m_haikuWindow->Bounds().Width() - w) : 0.0f;
                    nativeTool->MoveTo(x, nativeTool->Frame().top);
                    m_haikuWindow->AddChild(nativeTool);
                }
                else
                {
                    BView* client = wxHaikuFrameGetClientView(m_haikuWindow);
                    if (client)
                        m_haikuWindow->AddChild(nativeTool, client);
                    else
                        m_haikuWindow->AddChild(nativeTool);
                }

                HaikuLogViewGeometry("SetToolBar native after attach", nativeTool);
            }
        }

        wxHaikuFrameLayoutChrome(m_haikuWindow);
        m_haikuWindow->UpdateIfNeeded();
        m_haikuWindow->UnlockLooper();
    }

    wxFrameBase::SetToolBar(toolbar);

    HAIKU_LOG("SetToolBar EXIT");
}
#endif // wxUSE_TOOLBAR

#if wxUSE_STATUSBAR
void wxFrame::SetStatusBar(wxStatusBar* statusBar)
{
    HAIKU_LOG("SetStatusBar ENTER this=%p arg=%p old=%p get=%p",
              this, statusBar, m_frameStatusBar, GetStatusBar());

    wxFrameBase::SetStatusBar(statusBar);

    HAIKU_LOG("SetStatusBar AFTER BASE this=%p arg=%p frame=%p get=%p",
              this, statusBar, m_frameStatusBar, GetStatusBar());

    if (m_haikuWindow && m_haikuWindow->LockLooper())
    {
        if (BView* old = m_haikuWindow->FindView("statusbar"))
            old->RemoveSelf();

        if (statusBar)
        {
            if (BView* nativeStatus = statusBar->GetHandle())
            {
                nativeStatus->SetName("statusbar");
                if (nativeStatus->Parent() || nativeStatus->Window())
                    nativeStatus->RemoveSelf();
                m_haikuWindow->AddChild(nativeStatus);
            }
        }

        wxHaikuFrameLayoutChrome(m_haikuWindow);
        m_haikuWindow->UpdateIfNeeded();
        m_haikuWindow->UnlockLooper();
    }

    HAIKU_LOG("SetStatusBar EXIT this=%p frame=%p get=%p",
              this, m_frameStatusBar, GetStatusBar());
}
#endif // wxUSE_STATUSBAR

// ----------------------------------------------------------------------------
// style / children / client origin
// ----------------------------------------------------------------------------

void wxFrame::SetWindowStyleFlag(long style)
{
    HAIKU_LOG("SetWindowStyleFlag ENTER this=%p native=%p style=0x%lx",
              this, m_haikuWindow, (unsigned long)style);

    wxWindow::SetWindowStyleFlag(style);

    if (!m_haikuWindow)
        return;

    if (m_haikuWindow->LockLooper())
    {
        m_haikuWindow->SetLook(HasFlag(wxNO_BORDER)
                               ? B_NO_BORDER_WINDOW_LOOK
                               : B_TITLED_WINDOW_LOOK);

        m_haikuWindow->SetFeel(HasFlag(wxSTAY_ON_TOP)
                               ? B_FLOATING_APP_WINDOW_FEEL
                               : B_NORMAL_WINDOW_FEEL);

        HaikuLogWindowGeometry("SetWindowStyleFlag after", m_haikuWindow);
        m_haikuWindow->UnlockLooper();
    }

    HAIKU_LOG("SetWindowStyleFlag EXIT");
}

wxPoint wxFrame::GetClientAreaOrigin() const
{
    HAIKU_LOG("GetClientAreaOrigin ENTER this=%p native=%p", this, m_haikuWindow);

    if (!m_haikuWindow)
        return wxWindow::GetClientAreaOrigin();

    int x = 0;
    int y = 0;

    if (m_haikuWindow->LockLooper())
    {
        if (BMenuBar* mb = m_haikuWindow->KeyMenuBar())
            y += (int)(mb->Frame().Height() + 1.0f);

        if (BView* tb = m_haikuWindow->FindView("toolbar"); tb && !tb->Parent())
        {
            const BRect tf = tb->Frame();
            const bool vertical = GetToolBar() && GetToolBar()->HasFlag(wxTB_VERTICAL);

            if (vertical)
            {
                if (tf.left <= 0.0f)
                    x += (int)(tf.Width() + 1.0f);
            }
            else
            {
                y += (int)(tf.Height() + 1.0f);
            }
        }

        m_haikuWindow->UnlockLooper();
    }

    HAIKU_LOG("GetClientAreaOrigin EXIT x=%d y=%d", x, y);
    return wxPoint(x, y);
}

void wxFrame::AddChild(wxWindowBase* child)
{
    HAIKU_LOG("AddChild this=%p child=%p", this, child);
    wxFrameBase::AddChild(child);
}

void wxFrame::RemoveChild(wxWindowBase* child)
{
    HAIKU_LOG("RemoveChild this=%p child=%p", this, child);
    wxFrameBase::RemoveChild(child);
}

bool wxFrame::Show(bool show)
{
    HAIKU_LOG("Show ENTER this=%p native=%p show=%d",
              this, m_haikuWindow, show);

    if (!wxFrameBase::Show(show))
    {
        HAIKU_LOG("Show base returned false");
        return false;
    }

    if (!m_haikuWindow)
        return true;

    HaikuLogWindowGeometry("Show before", m_haikuWindow);
    const bool wasHidden = m_haikuWindow->IsHidden();
    bool shownNow = false;

    if (m_haikuWindow->LockLooper())
    {
        if (show)
        {
            if (m_haikuWindow->IsHidden())
            {
                HAIKU_LOG("Show calling native Show()");
                m_haikuWindow->Show();
                shownNow = wasHidden;
            }
        }
        else
        {
            if (!m_haikuWindow->IsHidden())
            {
                HAIKU_LOG("Show calling native Hide()");
                m_haikuWindow->Hide();
            }
        }

        if (show)
        {
            wxHaikuFrameLayoutChrome(m_haikuWindow);
            m_haikuWindow->Layout(true);

            if (BView* client = wxHaikuFrameGetClientView(m_haikuWindow))
                client->Invalidate();
        }

        HaikuLogWindowGeometry("Show after", m_haikuWindow);
        m_haikuWindow->UnlockLooper();
    }

    if (show && shownNow)
    {
        SendSizeEvent();
        Layout();
        Refresh();
    }
    else if (show)
    {
        Refresh();
    }

    HAIKU_LOG("Show EXIT show=%d", show);
    return true;
}
