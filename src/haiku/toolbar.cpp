/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/toolbar.cpp
// Purpose:     wxToolBar implementation for Haiku (native BPrivate::BToolBar)
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"
#include "control_lock.h"

#if wxUSE_TOOLBAR

#include "wx/toolbar.h"

#include "wx/window.h"
#include "wx/bitmap.h"
#include "wx/image.h"
#include "wx/menu.h"

#include "wx/haiku/private.h"
#include "wx/haiku/private/utils.h"
#include "wx/haiku/private/converter.h"

#include <Window.h>
#include <View.h>
#include <Message.h>
#include <Bitmap.h>
#include <Handler.h>
#include <Button.h>
#include <OS.h>
#include <Autolock.h>
#include <ScrollView.h>
#include "wx/control.h"

#include </boot/system/develop/headers/private/shared/ToolBar.h> // BPrivate::BToolBar

#define HAIKU_LOG(msg, ...) wxLogDebug("[HAIKU:%s:%d] " msg, __func__, __LINE__, ##__VA_ARGS__)
    #define HAIKU_LOG_PRINTF(msg, ...) printf("[HAIKU:%s:%d] " msg "\n", __func__, __LINE__, ##__VA_ARGS__); fflush(stdout)
// ----------------------------------------------------------------------------
// Helper: wxBitmap -> BBitmap (B_RGBA32 stored as BGRA in memory)
// ----------------------------------------------------------------------------
static BBitmap* wxBitmapToBBitmap(const wxBitmap& wxBmp)
{
    if (!wxBmp.IsOk())
        return nullptr;

    wxImage img = wxBmp.ConvertToImage();
    const int w = img.GetWidth();
    const int h = img.GetHeight();

    BBitmap* bbmp = new BBitmap(BRect(0, 0, w - 1, h - 1), B_RGBA32);
    if (!bbmp->IsValid())
    {
        delete bbmp;
        return nullptr;
    }

    uint8* dst = (uint8*)bbmp->Bits();
    const int bpr = bbmp->BytesPerRow();
    const uint8* rgb = img.GetData();
    const uint8* alpha = img.HasAlpha() ? img.GetAlpha() : nullptr;

    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const int src = (y * w + x) * 3;
            const int off = y * bpr + x * 4;

            dst[off + 0] = rgb[src + 2]; // B
            dst[off + 1] = rgb[src + 1]; // G
            dst[off + 2] = rgb[src + 0]; // R
            dst[off + 3] = alpha ? alpha[y * w + x] : 255; // A
        }
    }

    return bbmp;
}


// ----------------------------------------------------------------------------
// Tool representation
// ----------------------------------------------------------------------------
static BView* wxHaikuToolView(wxControl* control)
{
    BView* view=control->GetHandle();
    // Multiline text controls attach a scroll view around their draw view.
    if (view)
        if (auto* scroll=dynamic_cast<BScrollView*>(view->Parent()))
            if (scroll->Target()==view) return scroll;
    return view;
}

class wxToolBarTool : public wxToolBarToolBase
{
public:
    wxToolBarTool(wxToolBar* tbar,
                  int id,
                  const wxString& label,
                  const wxBitmapBundle& bmpNormal,
                  const wxBitmapBundle& bmpDisabled,
                  wxItemKind kind,
                  wxObject* clientData,
                  const wxString& shortHelp,
                  const wxString& longHelp)
        : wxToolBarToolBase(tbar, id, label, bmpNormal, bmpDisabled,
                            kind, clientData, shortHelp, longHelp)
    {}

    wxToolBarTool(wxToolBar* tbar,wxControl* control,const wxString& label)
        : wxToolBarToolBase(tbar,control,label) {}
    BButton* button = nullptr;
    uint32   command = 0; // mapped 1:1 with wx id
};


// ----------------------------------------------------------------------------
// Haiku target handler: receives BToolBar action messages and forwards to wx
// ----------------------------------------------------------------------------
class wxToolBar::HaikuTarget : public BHandler
{
public:
    explicit HaikuTarget(wxToolBar* tb) : m_tb(tb) {}

    void MessageReceived(BMessage* msg) override
    {
        if (!m_tb || !msg)
        {
            BHandler::MessageReceived(msg);
            return;
        }

        const int id = (int)msg->what;

        // BToolBar callbacks arrive on a BWindow looper thread. Commands
        // such as Build can launch processes and must run on the wx main thread.
        wxToolBar* const toolbar = m_tb;
        toolbar->CallAfter([toolbar, id] { toolbar->HandleNativeAction(id); });
    }

private:
    wxToolBar* m_tb = nullptr;
};

wxIMPLEMENT_DYNAMIC_CLASS(wxToolBar, wxToolBarBase);

void wxToolBar::HandleNativeAction(int id)
{
    wxToolBarToolBase* const toolBase = FindById(id);
    if (!toolBase)
        return;

    wxToolBarTool* const tool = static_cast<wxToolBarTool*>(toolBase);
    bool toggled = false;
    if (tool->CanBeToggled())
    {
        toggled = tool->button && tool->button->Value() == B_CONTROL_ON;
        tool->Toggle(toggled);

        if (!toggled && tool->GetKind() == wxITEM_RADIO)
            return;

        if (tool->GetKind() == wxITEM_RADIO)
            UnToggleRadioGroup(tool);
    }

    // wxToolBarBase::OnLeftClick() creates and routes wxEVT_TOOL.
    // Sending one here as well delivered every native click twice.
    OnLeftClick(id, toggled);
}


// ----------------------------------------------------------------------------
// wxToolBar
// ----------------------------------------------------------------------------
wxToolBar::~wxToolBar()
{
    // Delete wx controls before the native toolbar deletes its child views.
    ClearTools();
    DestroyChildren();
    if (m_toolbar)
    {
        if (BWindow* win = m_toolbar->Window())
        {
            if (win->LockLooper())
            {
                m_toolbar->RemoveSelf();
                win->UnlockLooper();
            }
        }
        delete m_toolbar;
        m_toolbar = nullptr;
        HaikuAdoptView(nullptr);
    }

    if (m_target)
    {
        if (BLooper* looper = m_target->Looper())
        {
            if (BWindow* win = dynamic_cast<BWindow*>(looper))
            {
                if (win->LockLooper())
                {
                    win->RemoveHandler(m_target);
                    win->UnlockLooper();
                }
            }
            else
            {
                looper->RemoveHandler(m_target);
            }
        }
        delete m_target;
        m_target = nullptr;
    }
}


bool wxToolBar::Create(wxWindow* parent,wxWindowID id,const wxPoint& pos,
                       const wxSize& size,long style,const wxString& name)
{
    const orientation orient=(style & wxTB_VERTICAL) ? B_VERTICAL : B_HORIZONTAL;
    m_toolbar=new BPrivate::BToolBar(orient);
    m_toolbar->SetName("toolbar");
    HaikuAdoptView(m_toolbar,nullptr,m_toolbar);
    if (!wxToolBarBase::Create(parent,id,pos,size,style,wxDefaultValidator,name))
        return false;
    m_target=new HaikuTarget(this);
    BAutolock lock(m_toolbar->Looper());
    if (!lock.IsLocked()) return false;
    m_toolbar->Looper()->AddHandler(m_target);
    return true;
}


BView* wxToolBar::GetHandle() const
{
    return m_toolbar;
}


wxToolBarToolBase* wxToolBar::FindToolForPosition(wxCoord x, wxCoord y) const
{
    for (wxToolBarToolsList::const_iterator it = m_tools.begin();
         it != m_tools.end(); ++it)
    {
        wxToolBarTool* const tool = static_cast<wxToolBarTool*>(*it);
        if (tool && tool->button &&
            tool->button->Frame().Contains(BPoint((float)x, (float)y)))
        {
            return tool;
        }
    }

    return nullptr;
}


void wxToolBar::SetWindowStyleFlag(long style)
{
    wxToolBarBase::SetWindowStyleFlag(style);
    // Optional: rebuild on orientation changes
}


void wxToolBar::SetToolShortHelp(int id, const wxString& help)
{
    wxToolBarTool* tool = static_cast<wxToolBarTool*>(FindById(id));
    if (tool)
    {
        tool->SetShortHelp(help);
        if (m_toolbar)
            Realize();
    }
}


void wxToolBar::SetToolNormalBitmap(int id, const wxBitmapBundle& bitmap)
{
    wxToolBarTool* tool = static_cast<wxToolBarTool*>(FindById(id));
    if (!tool || !tool->IsButton())
        return;

    tool->SetNormalBitmap(bitmap);
    if (m_toolbar)
        Realize();
}


void wxToolBar::SetToolDisabledBitmap(int id, const wxBitmapBundle& bitmap)
{
    wxToolBarTool* tool = static_cast<wxToolBarTool*>(FindById(id));
    if (!tool || !tool->IsButton())
        return;

    tool->SetDisabledBitmap(bitmap);
    if (m_toolbar)
        Realize();
}


wxToolBarToolBase* wxToolBar::CreateTool(int toolid,
                                         const wxString& label,
                                         const wxBitmapBundle& bmpNormal,
                                         const wxBitmapBundle& bmpDisabled,
                                         wxItemKind kind,
                                         wxObject* clientData,
                                         const wxString& shortHelp,
                                         const wxString& longHelp)
{
       HAIKU_LOG("create tool toolbar called");
   
    wxToolBarTool* t = new wxToolBarTool(this, toolid, label, bmpNormal, bmpDisabled,
                                         kind, clientData, shortHelp, longHelp);
    t->command = (uint32)toolid;
    return t;
}


wxToolBarToolBase* wxToolBar::CreateTool(wxControl* control, const wxString& label)
{
    return new wxToolBarTool(this,control,label);
}


bool wxToolBar::Realize()
{

HAIKU_LOG("tool toolbar realize=====called");
    if (!wxToolBarBase::Realize())
        return false;

    if (!m_toolbar)
        return false;

    BWindow* win = m_toolbar->Window();

    bool lockedHere = false;
    if (win && win->LockingThread() != find_thread(nullptr))
    {
        if (!win->LockLooper())
            return false;
        lockedHere = true;
    }

    if (win && m_target && m_target->Looper() == nullptr)
        win->AddHandler(m_target);

    // Clear existing children (BToolBar has no Clear())
    while (m_toolbar->CountChildren() > 0)
    {
        BView* child = m_toolbar->ChildAt(0);
        bool ownedByControl=false;
        for (auto node=m_tools.GetFirst();node;node=node->GetNext())
        {
            wxToolBarToolBase* tool=node->GetData();
            if (tool->IsControl() && wxHaikuToolView(tool->GetControl())==child)
                ownedByControl=true;
        }
        m_toolbar->RemoveChild(child);
        if (!ownedByControl) delete child;
    }

    for (wxToolBarToolsList::const_iterator it = m_tools.begin(); it != m_tools.end(); ++it)
    {
        wxToolBarTool* tool = static_cast<wxToolBarTool*>(*it);
        if (!tool)
            continue;

        if (tool->IsSeparator())
        {
            if (tool->IsStretchable())
                m_toolbar->AddGlue();
            else
                m_toolbar->AddSeparator();
            continue;
        }

        if (tool->IsControl())
        {
            wxControl* control=tool->GetControl();
            BView* view=wxHaikuToolView(control);
            if (view)
            {
                if (view->Parent()) view->RemoveSelf();
                const wxSize size=control->GetSize();
                view->SetExplicitMinSize(BSize(size.x-1,size.y-1));
                view->SetExplicitMaxSize(BSize(size.x-1,size.y-1));
                m_toolbar->AddView(view);
            }
            continue;
        }
        if (tool->IsButton())
        {
            // Select the bundle matching the current enabled state.
            wxBitmap bmp = tool->GetBitmap(m_bitmapSize);

            if (bmp.IsOk() &&
                (bmp.GetWidth() != m_bitmapSize.x || bmp.GetHeight() != m_bitmapSize.y))
            {
                wxImage img = bmp.ConvertToImage();
                img.Rescale(m_bitmapSize.x, m_bitmapSize.y, wxIMAGE_QUALITY_HIGH);
                bmp = wxBitmap(img);
            }

            BBitmap* icon = wxBitmapToBBitmap(bmp);

            m_toolbar->AddAction(tool->command,
                                 m_target,
                                 icon,
                                 tool->GetShortHelp().utf8_str(),
                                 tool->GetLabel().utf8_str(),
                                 tool->CanBeToggled());

            delete icon;

            tool->button = m_toolbar->FindButton(tool->command);

            m_toolbar->SetActionEnabled(tool->command, tool->IsEnabled());
            if (tool->CanBeToggled())
                m_toolbar->SetActionPressed(tool->command, tool->IsToggled());
        }
    }

    if (lockedHere)
        win->UnlockLooper();

    InvalidateBestSize();
    const wxSize best = GetBestSize();
    SetSize(wxDefaultCoord, wxDefaultCoord,
            HasFlag(wxTB_VERTICAL) ? best.x : GetSize().x,
            HasFlag(wxTB_VERTICAL) ? GetSize().y : best.y);
    if (win)
    {
        extern void wxHaikuFrameLayoutChrome(BWindow*);
        BAutolock layoutLock(win);
        if (layoutLock.IsLocked())
            wxHaikuFrameLayoutChrome(win);
    }
    return true;
}


bool wxToolBar::DoInsertTool(size_t, wxToolBarToolBase*)
{
    InvalidateBestSize();
    return true;
}

bool wxToolBar::DoDeleteTool(size_t,wxToolBarToolBase* base)
{
    BAutolock lock(m_toolbar ? m_toolbar->Looper() : nullptr);
    if (m_toolbar && m_toolbar->Looper() && !lock.IsLocked()) return false;
    wxToolBarTool* tool=static_cast<wxToolBarTool*>(base);
    BView* view=tool->IsControl() ? wxHaikuToolView(tool->GetControl())
                                : static_cast<BView*>(tool->button);
    if (view && view->Parent()==m_toolbar)
    {
        view->RemoveSelf();
        if (!tool->IsControl()) delete view;
    }
    tool->button=nullptr;
    InvalidateBestSize();
    return true;
}

void wxToolBar::DoEnableTool(wxToolBarToolBase* toolBase, bool enable)
{
    if (!m_toolbar || !toolBase)
        return;

    if (toolBase->IsControl())
    {
        toolBase->GetControl()->Enable(enable);
        return;
    }
    wxToolBarTool* tool = static_cast<wxToolBarTool*>(toolBase);
    const uint32 cmd = tool->command;

    wxHaikuControlLock lock(m_toolbar);
    if (!lock.IsOk())
        return;

    m_toolbar->SetActionEnabled(cmd, enable);
}

void wxToolBar::DoToggleTool(wxToolBarToolBase* toolBase, bool toggle)
{
    if (!m_toolbar || !toolBase)
        return;

    wxToolBarTool* tool = static_cast<wxToolBarTool*>(toolBase);
    const uint32 cmd = tool->command;

    wxHaikuControlLock lock(m_toolbar);
    if (!lock.IsOk())
        return;

    m_toolbar->SetActionPressed(cmd, toggle);
}

void wxToolBar::DoSetToggle(wxToolBarToolBase* toolBase, bool toggle)
{
    DoToggleTool(toolBase, toggle);
}

void wxToolBar::DoLayout()
{
    // BToolBar handles layout
}

wxSize wxToolBar::DoGetBestSize() const
{
    BAutolock lock(m_toolbar ? m_toolbar->Looper() : nullptr);
    if (m_toolbar && (!m_toolbar->Looper() || lock.IsLocked()))
    {
        BSize size=m_toolbar->PreferredSize();
        return wxSize(wxMax(1,(int)size.width+1),wxMax(1,(int)size.height+1));
    }
    return wxSize(200,m_bitmapSize.y+12);
}

#endif // wxUSE_TOOLBAR
