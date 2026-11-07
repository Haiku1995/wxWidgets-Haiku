#include "wx/wxprec.h"
#include "wx/private/overlay.h"
#include "wx/dc.h"
#include "wx/window.h"
#include "wx/haiku/private.h"

#include <View.h>
#include <Window.h>

// ----------------------------------------------------------------------------
// wxHaikuViewLocker
// ----------------------------------------------------------------------------
namespace
{
class wxHaikuViewLocker
{
public:
    explicit wxHaikuViewLocker(BView* view)
        : m_window(view ? view->Window() : nullptr),
          m_locked(m_window && m_window->LockLooper())
    {
    }

    ~wxHaikuViewLocker()
    {
        if ( m_locked )
            m_window->UnlockLooper();
    }

    bool IsOk() const { return m_locked; }

private:
    BWindow* const m_window;
    const bool m_locked;
};

static BView* wxGetOverlayTargetView(wxWindow* win)
{
    if ( !win ) return nullptr;
    return static_cast<BView*>(win->GetHandle());
}
} // anonymous namespace

// ----------------------------------------------------------------------------
// wxOverlayImpl
// ----------------------------------------------------------------------------
class wxOverlayImpl : public wxOverlay::Impl
{
public:
    wxOverlayImpl() = default;
    virtual ~wxOverlayImpl() { Reset(); }

    virtual bool IsOk() override { return true; }
    
    virtual void Init(wxDC* dc, int x, int y, int width, int height) override;
    virtual void BeginDrawing(wxDC* dc) override;
    virtual void EndDrawing(wxDC* dc) override;
    virtual void Clear(wxDC* dc) override;
    virtual void Reset() override;

private:
    wxWindow* m_target = nullptr;
    BView*    m_targetView = nullptr;
    BView*    m_overlayView = nullptr;
    BRect     m_rect;
};

void wxOverlayImpl::Init(wxDC* dc, int x, int y, int width, int height)
{
    Reset();
    if ( !dc ) return;

    m_target = dc->GetWindow();
    if ( !m_target ) return;

    m_targetView = wxGetOverlayTargetView(m_target);
    if ( !m_targetView ) return;

    m_rect = BRect(x, y, x + width - 1, y + height - 1);

    wxHaikuViewLocker lock(m_targetView);
    if ( !lock.IsOk() ) return;

    m_overlayView = new BView(m_rect, "wxOverlayView", B_FOLLOW_NONE, B_WILL_DRAW);
    m_overlayView->SetViewColor(B_TRANSPARENT_COLOR);
    m_overlayView->Hide();
    
    m_targetView->AddChild(m_overlayView);
}

void wxOverlayImpl::BeginDrawing(wxDC* dc)
{
    if ( !m_overlayView ) 
    {
        Init(dc, 0, 0, 0, 0);
        if ( !m_overlayView ) return;
    }

    wxHaikuViewLocker lock(m_overlayView);
    if ( lock.IsOk() && m_overlayView->IsHidden() )
        m_overlayView->Show();
}

void wxOverlayImpl::EndDrawing(wxDC* WXUNUSED(dc))
{
    if ( m_overlayView )
        m_overlayView->Invalidate();
}

void wxOverlayImpl::Clear(wxDC* WXUNUSED(dc))
{
    if ( !m_overlayView ) return;

    wxHaikuViewLocker lock(m_overlayView);
    if ( lock.IsOk() )
    {
        m_overlayView->Hide();
        if ( m_targetView )
            m_targetView->Invalidate(m_rect);
    }
}

void wxOverlayImpl::Reset()
{
    if ( m_overlayView )
    {
        BView* parent = m_overlayView->Parent();
        if ( parent )
        {
            wxHaikuViewLocker lock(parent);
            if ( lock.IsOk() )
                parent->RemoveChild(m_overlayView);
        }
        delete m_overlayView;
        m_overlayView = nullptr;
    }
    m_target = nullptr;
    m_targetView = nullptr;
}

// ----------------------------------------------------------------------------
// The Factory
// ----------------------------------------------------------------------------
wxOverlay::Impl* wxOverlay::Create()
{
    return new wxOverlayImpl();
}
