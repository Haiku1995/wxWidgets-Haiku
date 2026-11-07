/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/glcanvas.cpp
// Purpose:     wxGLCanvas implementation for Haiku using BGLView
// Author:      Based on Qt port; adapted for Haiku
// Created:     [Date]
// Copyright:   (c) 2025 wxWidgets team
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/glcanvas.h"
#include "wx/haiku/private.h"
#include "wx/haiku/window.h"
#include "wx/log.h"
#include "wx/haiku/private/dispatch.h"
#include <Messenger.h>
#include <Message.h>

#include <GLView.h>
#include <Window.h>
#include <View.h>


namespace
{

thread_local BGLView* gs_currentGLView = nullptr;

void ReleaseCurrentGL()
{
    if (gs_currentGLView)
    {
        BGLView* view = gs_currentGLView;
        // Complete writes to the drawable before a different looper can
        // resize or rebind it. Mesa can otherwise still own raster work.
        glFinish();
        gs_currentGLView = nullptr;
        view->UnlockGL();
    }
}

// The GL view itself must be the wx native child, not a second view over a
// generic placeholder. Preserve BGLView's framebuffer handling and forward
// native events through the same wx handlers as ordinary Haiku windows.
class wxHaikuGLView final : public BGLView
{
    wxWindowHaiku* Owner() const
        { return wxWindowHaiku::HaikuRetrieveWindowPointer(const_cast<wxHaikuGLView*>(this)); }
public:
    wxHaikuGLView(BRect frame, const char* name, uint32 options)
        : BGLView(frame, name, B_FOLLOW_NONE,
                  B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE, options) {}
    void Draw(BRect rect) override
    {
        BGLView::Draw(rect);
        if (auto* owner = Owner()) owner->HaikuHandleDrawMessage(this, rect);
    }
    void FrameResized(float width, float height) override
    {
        ReleaseCurrentGL();
        BGLView::FrameResized(width, height);
        if (auto* owner = Owner()) owner->HaikuHandleFrameResized(this, width, height);
    }
    void KeyDown(const char* bytes, int32 count) override
    {
        if (auto* owner = Owner()) owner->HaikuHandleKeyDown(this, Window()->CurrentMessage());
        else BGLView::KeyDown(bytes, count);
    }
    void KeyUp(const char* bytes, int32 count) override
    {
        if (auto* owner = Owner()) owner->HaikuHandleKeyUp(this, Window()->CurrentMessage());
        else BGLView::KeyUp(bytes, count);
    }
    void MouseDown(BPoint point) override
    {
        if (auto* owner = Owner())
        {
            MakeFocus();
            if (owner->HaikuHandleMouseDown(this, Window()->CurrentMessage())) return;
        }
        BGLView::MouseDown(point);
    }
    void MouseUp(BPoint point) override
    {
        if (auto* owner = Owner())
            if (owner->HaikuHandleMouseUp(this, Window()->CurrentMessage())) return;
        BGLView::MouseUp(point);
    }
    void MouseMoved(BPoint point, uint32 transit, const BMessage* drag) override
    {
        if (auto* owner = Owner()) owner->HaikuHandleMouseMoved(this, Window()->CurrentMessage());
        else BGLView::MouseMoved(point, transit, drag);
    }
    void WindowActivated(bool active) override
    {
        BGLView::WindowActivated(active);
        if (auto* owner = Owner()) owner->HaikuHandleWindowActivated(this, active);
    }
    void MakeFocus(bool focus = true) override
    {
        const bool wasFocused = IsFocus();
        BGLView::MakeFocus(focus);
        if (wasFocused != IsFocus())
            if (auto* owner = Owner())
            {
                wxFocusEvent event(IsFocus() ? wxEVT_SET_FOCUS : wxEVT_KILL_FOCUS, owner->GetId());
                event.SetEventObject(owner); owner->ProcessWindowEvent(event);
            }
    }
    void MessageReceived(BMessage* message) override
    {
        if (message && message->what == B_MOUSE_WHEEL_CHANGED)
            if (auto* owner = Owner())
                if (owner->HaikuHandleMouseWheelChanged(this, message)) return;
        BGLView::MessageReceived(message);
    }
};


// Convert wx display/context attributes into Haiku BGLView flags.
// This is conservative: unsupported requests return false.
static bool wxConvertWXAttrsToHaikuGL(const wxGLAttributes& dispAttrs,
                                      const wxGLContextAttrs& ctxAttrs,
                                      uint32& mode,
                                      uint32& options)
{
    mode = BGL_RGB;
    options = 0;

    const int* attrs = dispAttrs.GetGLAttrs();
    if ( !attrs )
        return false;

    for ( int i = 0; attrs[i] != 0; ++i )
    {
        const int attr = attrs[i];
        bool isBoolAttr = true;
        int value = 0;

        switch ( attr )
        {
            case WX_GL_RGBA:
                mode |= BGL_RGB;
                break;

            case WX_GL_DOUBLEBUFFER:
                mode |= BGL_DOUBLE;
                break;

            case WX_GL_STEREO:
#ifdef BGL_STEREO
                mode |= BGL_STEREO;
#else
                return false;
#endif
                break;

            case WX_GL_FRAMEBUFFER_SRGB:
                return false;

            case WX_GL_DEPTH_SIZE:
                isBoolAttr = false;
                value = attrs[++i];
                if (value > 24) return false;
                if ( value > 0 )
                    mode |= BGL_DEPTH;
                break;

            case WX_GL_STENCIL_SIZE:
                isBoolAttr = false;
                value = attrs[++i];
                if (value > 8) return false;
#ifdef BGL_STENCIL
                if ( value > 0 )
                    mode |= BGL_STENCIL;
#else
                if ( value > 0 )
                    return false;
#endif
                break;

            case WX_GL_MIN_ALPHA:
                isBoolAttr = false;
                value = attrs[++i];
                if (value > 8) return false;
                if ( value > 0 )
                    mode |= BGL_ALPHA;
                break;

            case WX_GL_MIN_RED:
            case WX_GL_MIN_GREEN:
            case WX_GL_MIN_BLUE:
                value = attrs[++i];
                // BGLView's RGB visual uses 8-bit channels.
                if (value > 8) return false;
                break;
            case WX_GL_BUFFER_SIZE:
                value = attrs[++i];
                if (value > 32) return false;
                break;
            case WX_GL_LEVEL:
            case WX_GL_AUX_BUFFERS:
            case WX_GL_MIN_ACCUM_RED:
            case WX_GL_MIN_ACCUM_GREEN:
            case WX_GL_MIN_ACCUM_BLUE:
            case WX_GL_MIN_ACCUM_ALPHA:
            case WX_GL_SAMPLE_BUFFERS:
            case WX_GL_SAMPLES:
                // Haiku BGLView does not expose a clean 1:1 chooser API
                // for these in the same way as GLX/WGL/Qt surface formats.
                // Be conservative and reject requests that explicitly ask
                // for things we cannot guarantee.
                isBoolAttr = false;
                value = attrs[++i];
                if ( value != 0 )
                    return false;
                break;

            default:
                wxLogDebug("Unsupported Haiku OpenGL display attribute %d", attr);
                return false;
        }
    }

    // Context attrs: Haiku/BGLView has much less explicit control than Qt.
    // Accept only requests we can safely ignore or satisfy implicitly.
    const int* ctx = ctxAttrs.GetGLAttrs();
    if ( ctx )
    {
        for ( int i = 0; ctx[i] != 0; ++i )
        {
            const int attr = ctx[i];
            bool hasValue = false;
            int value = 0;

            switch ( attr )
            {
                case WX_GL_MAJOR_VERSION:
                case WX_GL_MINOR_VERSION:
                    hasValue = true;
                    value = ctx[++i];
                    // Haiku backend usually cannot guarantee an exact version
                    // here through BGLView creation alone.
                    // Accept 1.x/2.x conservatively, reject stricter requests.
                    if ( attr == WX_GL_MAJOR_VERSION && value > 2 )
                        return false;
                    break;

                case WX_GL_CORE_PROFILE:
                case WX_GL_COMPAT_PROFILE:
                case WX_GL_FORWARD_COMPAT:
                case WX_GL_ES2:
                case WX_GL_DEBUG:
                case WX_GL_ROBUST_ACCESS:
                case WX_GL_NO_RESET_NOTIFY:
                case WX_GL_LOSE_ON_RESET:
                case WX_GL_RESET_ISOLATION:
                case WX_GL_RELEASE_FLUSH:
                    // No direct Haiku/BGLView support for these.
                    return false;

                default:
                    wxLogDebug("Unsupported Haiku OpenGL context attribute %d", attr);
                    return false;
            }

            wxUnusedVar(hasValue);
        }
    }

    return true;
}

} 


// ----------------------------------------------------------------------------
// wxGLContext
// ----------------------------------------------------------------------------
wxIMPLEMENT_CLASS(wxGLContext, wxObject);
wxGLContext::wxGLContext(wxGLCanvas* win,
                         const wxGLContext* other,
                         const wxGLContextAttrs* ctxAttrs)
    : wxGLContextBase()
{
    m_isOk = false;
    // BGLView owns its rendering context and exposes no API for choosing a
    // share group. Never claim to share resources when we cannot do so.
    if (!win || other)
        return;

    wxGLAttributes display;
    display.Defaults().EndList();
    uint32 mode = 0, options = 0;
    const wxGLContextAttrs& requested = ctxAttrs ? *ctxAttrs : win->GetGLCTXAttrs();
    if (!wxConvertWXAttrsToHaikuGL(display, requested, mode, options))
        return;

    m_glView = win->GetBGLView();
    m_isOk = m_glView != nullptr;
}

wxGLContext::~wxGLContext()
{
    if (gs_currentGLView == m_glView)
        ReleaseCurrentGL();
}

void wxGLContextBase::ClearCurrent()
{
    ReleaseCurrentGL();
}

wxGLExtFunction wxGLContextBase::GetProcAddress(const wxString& name)
{
    if (!gs_currentGLView || name.empty())
        return nullptr;
    return reinterpret_cast<wxGLExtFunction>(
        gs_currentGLView->GetGLProcAddress(name.utf8_str()));
}

bool wxGLContext::SetCurrent(const wxGLCanvas& win) const
{
    BGLView* view = win.GetBGLView();
    if ( !view )
        return false;

    if (!view->Window() || !m_isOk || view != m_glView)
        return false;
    if (gs_currentGLView != view)
    {
        ReleaseCurrentGL();
        view->LockGL();
        gs_currentGLView = view;
    }
    // Haiku contexts need an explicit unlock before dispatch moves to another
    // native looper thread. Keep the context current through SwapBuffers(),
    // then release once the outer serialized wx callback finishes.
    wxHaikuSetGLContextRelease(ReleaseCurrentGL);
    return glGetString(GL_VERSION) != nullptr;
}



// ----------------------------------------------------------------------------
// wxGLCanvas
// ----------------------------------------------------------------------------

//wxIMPLEMENT_DYNAMIC_CLASS(wxGLCanvas, wxGLCanvasBase);
//wxIMPLEMENT_CLASS(wxGLCanvas, wxGLCanvasBase);
wxIMPLEMENT_CLASS(wxGLCanvas, wxWindow);
wxGLCanvas::wxGLCanvas(wxWindow* parent,
                       const wxGLAttributes& dispAttrs,
                       wxWindowID id,
                       const wxPoint& pos,
                       const wxSize& size,
                       long style,
                       const wxString& name,
                       const wxPalette& palette)
{
    Create(parent, dispAttrs, id, pos, size, style, name, palette);
}

wxGLCanvas::wxGLCanvas(wxWindow* parent,
                       wxWindowID id,
                       const int* attribList,
                       const wxPoint& pos,
                       const wxSize& size,
                       long style,
                       const wxString& name,
                       const wxPalette& palette)
{
    wxGLAttributes attrs;
    if (!ParseAttribList(attribList, attrs, &m_GLCTXAttrs))
        return;

    Create(parent, attrs, id, pos, size, style, name, palette);
}

bool wxGLCanvas::Create(wxWindow* parent,
                        const wxGLAttributes& dispAttrs,
                        wxWindowID id,
                        const wxPoint& pos,
                        const wxSize& size,
                        long style,
                        const wxString& name,
                        const wxPalette& palette)
{
    wxUnusedVar(palette);

    uint32 mode = 0;
    uint32 options = 0;
    wxGLContextAttrs ctxAttrs;
    if ( !wxConvertWXAttrsToHaikuGL(dispAttrs, ctxAttrs, mode, options) )
        return false;

    wxPoint p = (pos == wxDefaultPosition) ? wxPoint(0, 0) : pos;

    wxSize initialSize = size;
    if ( initialSize == wxDefaultSize )
    {
        if ( parent )
        {
            initialSize = parent->GetClientSize();

            if ( p.x > 0 )
                initialSize.x -= p.x;
            if ( p.y > 0 )
                initialSize.y -= p.y;
        }
        else
        {
            initialSize = wxSize(320, 240);
        }
    }

    if ( initialSize.x <= 0 )
        initialSize.x = 1;
    if ( initialSize.y <= 0 )
        initialSize.y = 1;

    const BRect frame((float)p.x, (float)p.y,
                      (float)(p.x + initialSize.x - 1),
                      (float)(p.y + initialSize.y - 1));

    m_glView = new wxHaikuGLView(frame, wxToBStr(name), mode);
    HaikuAdoptView(m_glView, nullptr, m_glView);
    // Adopt before base creation: it registers and attaches this view without
    // allocating an overlapping generic wxHaikuWindowView.
    if (!wxGLCanvasBase::Create(parent, id, pos, initialSize, style, name))
    {
        delete m_glView;
        m_glView = nullptr;
        HaikuAdoptView(nullptr);
        return false;
    }

    return true;
}

bool wxGLCanvas::Create(wxWindow* parent,
                        wxWindowID id,
                        const wxPoint& pos,
                        const wxSize& size,
                        long style,
                        const wxString& name,
                        const int* attribList,
                        const wxPalette& palette)
{
    wxGLAttributes attrs;
    if (!ParseAttribList(attribList, attrs, &m_GLCTXAttrs))
        return false;

    return Create(parent, attrs, id, pos, size, style, name, palette);
}

wxGLCanvas::~wxGLCanvas()
{
    if (gs_currentGLView == m_glView)
        ReleaseCurrentGL();
    if (!m_glView)
        return;
    SendDestroyEvent();
    DestroyChildren();
    BWindow* window = m_glView->Window();
    if (!window || window->Lock())
    {
        HaikuStoreWindowPointer(m_glView, nullptr);
        m_glView->RemoveSelf();
        delete m_glView;
        m_glView = nullptr;
        HaikuAdoptView(nullptr);
        if (window) window->Unlock();
    }
}

bool wxGLCanvas::SwapBuffers()
{
    if (!m_glView || gs_currentGLView != m_glView)
        return false;
    m_glView->SwapBuffers();
    return true;
}


BWindow* wxGLCanvas::GetBWindow() const
{
    if (m_glView && m_glView->Window())
        return m_glView->Window();
    return nullptr;
}


bool wxGLCanvasBase::IsDisplaySupported(const wxGLAttributes& dispAttrs)
{
    const int* attrsList = dispAttrs.GetGLAttrs();

    wxCHECK_MSG(attrsList, false, "wxGLAttributes object is empty.");

    return IsDisplaySupported(attrsList);
}

/* static */
bool wxGLCanvasBase::IsDisplaySupported(const int* attribList)
{

    // Same pattern as Qt: split display and context attributes first.
    wxGLAttributes dispAttrs;
    wxGLContextAttrs ctxAttrs;
    if ( !ParseAttribList(attribList, dispAttrs, &ctxAttrs) )
        return false;

    uint32 mode = 0;
    uint32 options = 0;
    if ( !wxConvertWXAttrsToHaikuGL(dispAttrs, ctxAttrs, mode, options) )
        return false;

    // Haiku does not provide a rich "pixel format chooser" API like Qt/GLX.
    // If we successfully translated the request into BGLView flags, treat it
    // as supported.
    //
    // Later, when wxGLCanvas::Create() is implemented, actual creation can
    // still fail and should set m_isOk=false appropriately.
    wxUnusedVar(mode);
    wxUnusedVar(options);
    return true;
}


// ----------------------------------------------------------------------------
// wxGLContextAttrs: OpenGL rendering context attributes
// ----------------------------------------------------------------------------
// GLX specific values

wxGLContextAttrs& wxGLContextAttrs::CoreProfile()
{
    AddAttribute(WX_GL_CORE_PROFILE);
    SetNeedsARB();
    return *this;
}


wxGLContextAttrs& wxGLContextAttrs::MajorVersion(int val)
{
    if ( val > 0 )
    {
        AddAttribute(WX_GL_MAJOR_VERSION);
        AddAttribute(val);

        if ( val >= 3 )
            SetNeedsARB();
    }
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::MinorVersion(int val)
{
    if ( val >= 0 )
    {
        AddAttribute(WX_GL_MINOR_VERSION);
        AddAttribute(val);
    }
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::CompatibilityProfile()
{
    AddAttribute(WX_GL_COMPAT_PROFILE);
    SetNeedsARB();
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::ForwardCompatible()
{
    AddAttribute(WX_GL_FORWARD_COMPAT);
    SetNeedsARB();
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::ES2()
{
    AddAttribute(WX_GL_ES2);
    SetNeedsARB();
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::DebugCtx()
{
    AddAttribute(WX_GL_DEBUG);
    SetNeedsARB();
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::Robust()
{
    AddAttribute(WX_GL_ROBUST_ACCESS);
    SetNeedsARB();
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::NoResetNotify()
{
    AddAttribute(WX_GL_NO_RESET_NOTIFY);
    SetNeedsARB();
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::LoseOnReset()
{
    AddAttribute(WX_GL_LOSE_ON_RESET);
    SetNeedsARB();
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::ResetIsolation()
{
    AddAttribute(WX_GL_RESET_ISOLATION);
    SetNeedsARB();
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::ReleaseFlush(int val)
{
    AddAttribute(WX_GL_RELEASE_FLUSH);
    AddAttribute(val);
    SetNeedsARB();
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::PlatformDefaults()
{
    return *this;
}

void wxGLContextAttrs::EndList()
{
    AddAttribute(0);
}

// ----------------------------------------------------------------------------
// wxGLAttributes: Visual/FBconfig attributes
// ----------------------------------------------------------------------------
// GLX specific values

//   Different versions of GLX API use rather different attributes lists, see
//   the following URLs:
//
//   - <= 1.2: http://www.opengl.org/sdk/docs/man/xhtml/glXChooseVisual.xml
//   - >= 1.3: http://www.opengl.org/sdk/docs/man/xhtml/glXChooseFBConfig.xml
//
//   Notice in particular that
//   - GLX_RGBA is boolean attribute in the old version of the API but a
//     value of GLX_RENDER_TYPE in the new one
//   - Boolean attributes such as GLX_DOUBLEBUFFER don't take values in the
//     old version but must be followed by True or False in the new one.

wxGLAttributes& wxGLAttributes::RGBA()
{
    AddAttribute(WX_GL_RGBA);
    return *this;
}

wxGLAttributes& wxGLAttributes::BufferSize(int val)
{
    if ( val >= 0 )
    {
        AddAttribute(WX_GL_BUFFER_SIZE);
        AddAttribute(val);
    }
    return *this;
}

wxGLAttributes& wxGLAttributes::Level(int val)
{
    AddAttribute(WX_GL_LEVEL);
    AddAttribute(val);
    return *this;
}

wxGLAttributes& wxGLAttributes::DoubleBuffer()
{
    AddAttribute(WX_GL_DOUBLEBUFFER);
    return *this;
}

wxGLAttributes& wxGLAttributes::Stereo()
{
    AddAttribute(WX_GL_STEREO);
    return *this;
}

wxGLAttributes& wxGLAttributes::AuxBuffers(int val)
{
    if ( val >= 0 )
    {
        AddAttribute(WX_GL_AUX_BUFFERS);
        AddAttribute(val);
    }
    return *this;
}

wxGLAttributes& wxGLAttributes::MinRGBA(int mRed, int mGreen, int mBlue, int mAlpha)
{
    if ( mRed >= 0)
    {
        AddAttribute(WX_GL_MIN_RED);
        AddAttribute(mRed);
    }
    if ( mGreen >= 0)
    {
        AddAttribute(WX_GL_MIN_GREEN);
        AddAttribute(mGreen);
    }
    if ( mBlue >= 0)
    {
        AddAttribute(WX_GL_MIN_BLUE);
        AddAttribute(mBlue);
    }
    if ( mAlpha >= 0)
    {
        AddAttribute(WX_GL_MIN_ALPHA);
        AddAttribute(mAlpha);
    }
    return *this;
}

wxGLAttributes& wxGLAttributes::Depth(int val)
{
    if ( val >= 0 )
    {
        AddAttribute(WX_GL_DEPTH_SIZE);
        AddAttribute(val);
    }
    return *this;
}

wxGLAttributes& wxGLAttributes::Stencil(int val)
{
    if ( val >= 0 )
    {
        AddAttribute(WX_GL_STENCIL_SIZE);
        AddAttribute(val);
    }
    return *this;
}

wxGLAttributes& wxGLAttributes::MinAcumRGBA(int mRed, int mGreen, int mBlue, int mAlpha)
{
    if ( mRed >= 0)
    {
        AddAttribute(WX_GL_MIN_ACCUM_RED);
        AddAttribute(mRed);
    }
    if ( mGreen >= 0)
    {
        AddAttribute(WX_GL_MIN_ACCUM_GREEN);
        AddAttribute(mGreen);
    }
    if ( mBlue >= 0)
    {
        AddAttribute(WX_GL_MIN_ACCUM_BLUE);
        AddAttribute(mBlue);
    }
    if ( mAlpha >= 0)
    {
        AddAttribute(WX_GL_MIN_ACCUM_ALPHA);
        AddAttribute(mAlpha);
    }
    return *this;
}

wxGLAttributes& wxGLAttributes::SampleBuffers(int val)
{
    if ( val >= 0 )
    {
        AddAttribute(WX_GL_SAMPLE_BUFFERS);
        AddAttribute(val);
    }
    return *this;
}

wxGLAttributes& wxGLAttributes::Samplers(int val)
{
    if ( val >= 0 )
    {
        AddAttribute(WX_GL_SAMPLES);
        AddAttribute(val);
    }
    return *this;
}

wxGLAttributes& wxGLAttributes::FrameBuffersRGB()
{
    AddAttribute(WX_GL_FRAMEBUFFER_SRGB);
    return *this;
}

void wxGLAttributes::EndList()
{
    AddAttribute(0);
}

wxGLAttributes& wxGLAttributes::PlatformDefaults()
{
    
    return *this;
}
