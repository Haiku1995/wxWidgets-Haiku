/////////////////////////////////////////////////////////////////////////////
// Name:        include/haiku/window.h
// Purpose:     wxWindow implementation for Haiku
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_WINDOW_H_
#define _WX_HAIKU_WINDOW_H_

#include <memory>
#include <SupportDefs.h>
#include "wx/region.h"

class BPicture;
class BView;
class BScrollView;
class BScrollBar;
class BWindow;
class BMessage;
class BRect;
class BPoint;
class wxHaikuShortcutHandler;

class WXDLLIMPEXP_CORE wxWindowHaiku : public wxWindowBase
{
public:
    wxWindowHaiku();
    ~wxWindowHaiku();

    // IMPORTANT: parent is wxWindow*, not wxWindowHaiku*
    wxWindowHaiku(wxWindow* parent,
                  wxWindowID id,
                  const wxPoint& pos = wxDefaultPosition,
                  const wxSize& size = wxDefaultSize,
                  long style = 0,
                  const wxString& name = wxASCII_STR(wxPanelNameStr));

    // IMPORTANT: parent is wxWindow*, not wxWindowHaiku*
    bool Create(wxWindow* parent,
                wxWindowID id,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = 0,
                const wxString& name = wxASCII_STR(wxPanelNameStr));

    // Derived classes can call PostCreation() if they bypass Create().
    void PostCreation(bool generic = true);

    void AddChild(wxWindowBase* child) override;

    BView*   GetBView() const { return m_haikuView; }
    BWindow* GetBWindow() const;

    // Virtual factory methods (used by derived classes like wxFrame in other files)
    virtual BView*   CreateBView(const BRect& frame);
    virtual BWindow* CreateBWindow(const BRect& frame, const char* title, uint32 flags);

    bool Show(bool show = true) override;

    void SetLabel(const wxString& label) override;
    wxString GetLabel() const override;

    void DoEnable(bool enable) override;
    void SetFocus() override;

    // Parent/Child:
    static void HaikuReparent(BView* child, BView* parent);
    bool Reparent(wxWindowBase* newParent) override;

    // Z-order:
    void Raise() override;
    void Lower() override;

    void WarpPointer(int x, int y) override;

    void Update() override;
    void Refresh(bool eraseBackground = true, const wxRect* rect = nullptr) override;

    bool SetCursor(const wxCursor& cursor) override;
    bool SetFont(const wxFont& font) override;

    int GetCharHeight() const override;
    int GetCharWidth() const override;

    double GetContentScaleFactor() const override;

    wxSize GetDPI() const override;
    double GetDPIScaleFactor() const override;

    void SetScrollbar(int orient, int pos, int thumbvisible, int range, bool refresh = true) override;
    void SetScrollPos(int orient, int pos, bool refresh = true) override;
    int GetScrollPos(int orient) const override;
    int GetScrollThumb(int orient) const override;
    int GetScrollRange(int orient) const override;

    wxSize GetWindowBorderSize() const override;

    void ScrollWindow(int dx, int dy, const wxRect* rect = nullptr) override;

    // Styles
    void SetWindowStyleFlag(long style) override;
    void SetExtraStyle(long exStyle) override;

    bool SetBackgroundStyle(wxBackgroundStyle style) override;
    bool IsTransparentBackgroundSupported(wxString* reason = nullptr) const override;
    bool SetTransparent(wxByte alpha) override;
    bool CanSetTransparent() override { return true; }

    bool SetBackgroundColour(const wxColour& colour) override;
    bool SetForegroundColour(const wxColour& colour) override;

    void SetDoubleBuffered(bool on) override;
    bool IsDoubleBuffered() const override;

    // Native handle for this window
    BView* GetHandle() const override;
   
#if wxUSE_DRAG_AND_DROP
    void SetDropTarget(wxDropTarget* dropTarget) override;
#endif

#if wxUSE_ACCEL
    void SetAcceleratorTable(const wxAcceleratorTable& accel) override;
#endif

    bool EnableTouchEvents(int eventsMask) override;

    // Haiku-specific internals:

    // Takes ownership of picture - window will delete it
    void HaikuSetPicture(BPicture* pict);

    BView* HaikuGetDrawView();
    virtual bool HaikuCanPaintWithoutActiveView() const;

    virtual bool HaikuHandleDrawMessage      (BView* handler, BRect updateRect);
    virtual bool HaikuHandleFrameResized     (BView* handler, float width, float height);
    virtual bool HaikuHandleMouseWheelChanged(BView* handler, BMessage* message);
    virtual bool HaikuHandleKeyDown          (BView* handler, BMessage* message);
    virtual bool HaikuHandleKeyUp            (BView* handler, BMessage* message);
    virtual bool HaikuHandleMouseDown        (BView* handler, BMessage* message);
    virtual bool HaikuHandleMouseUp          (BView* handler, BMessage* message);
    virtual bool HaikuHandleMouseMoved       (BView* handler, BMessage* message);
    virtual bool HaikuHandleFrameMoved       (BView* handler, BPoint newPosition);
    virtual bool HaikuHandleWindowActivated  (BView* handler, bool active);
    virtual bool HaikuHandleQuitRequested    (BView* handler);
    virtual bool HaikuHandleMessageReceived  (BView* handler, BMessage* message);

    static void HaikuStoreWindowPointer(BView* view, const wxWindowHaiku* window);
    static wxWindowHaiku* HaikuRetrieveWindowPointer(const BView* view);
    static void HaikuSendSetCursorEvent(wxWindowHaiku* win, const wxPoint& posClient);

#if wxUSE_DRAG_AND_DROP
    

    bool HaikuHandleDragOver(BView* handler, BMessage* message, const BPoint& where);
    bool HaikuHandleDropMessage(BView* handler, BMessage* message);
    void HaikuHandleDragLeave(BView* handler);
#endif


BView* GetHaikuParentViewForChildren() const { return HaikuGetParentView(); }
#if wxUSE_ACCEL
    virtual void HaikuHandleShortcut(int command);
#endif

#if wxUSE_TOOLTIPS
    virtual void HaikuApplyToolTip(const wxString& text);
#endif
void HaikuAttachNativeChild();
protected:
    void DoGetTextExtent(const wxString& string,
                         int* x, int* y,
                         int* descent = nullptr,
                         int* externalLeading = nullptr,
                         const wxFont* font = nullptr) const override;

    void DoClientToScreen(int* x, int* y) const override;
    void DoScreenToClient(int* x, int* y) const override;

    void DoCaptureMouse() override;
    void DoReleaseMouse() override;

    void DoFreeze() override;
    void DoThaw() override;

    void DoGetPosition(int* x, int* y) const override;

    void DoSetSize(int x, int y, int width, int height, int sizeFlags = wxSIZE_AUTO) override;
    void DoGetSize(int* width, int* height) const override;

    void DoSetClientSize(int width, int height) override;
    void DoGetClientSize(int* width, int* height) const override;

    void DoMoveWindow(int x, int y, int width, int height) override;

#if wxUSE_TOOLTIPS
    void DoSetToolTip(wxToolTip* tip) override;
#endif

#if wxUSE_MENUS
    bool DoPopupMenu(wxMenu* menu, int x, int y) override;
#endif

    void HaikuReleaseMouseAndNotify();

    // Return the native BView that should be used as the parent for our children.
    // Default: this window's own handle.
    virtual BView* HaikuGetParentView() const { return GetHandle(); }

  
 void HaikuAdoptView(BView* view, BScrollView* scrollView = nullptr, BView* drawView = nullptr)
    {
        m_haikuView = view;
        m_haikuScrollView = scrollView;
        m_haikuDrawView = drawView ? drawView : view;
    }


      BView* HaikuGetNativeViewToAttach() const;
   BView*       m_haikuView = nullptr;
    BScrollView* m_haikuScrollView = nullptr;
private:
    void Init();

    // Return the target view of m_haikuScrollView if used, otherwise m_haikuView.
    BView* HaikuGetClientView() const;

    BScrollBar* HaikuGetScrollBar(int orientation) const;

    bool HaikuSetBackgroundStyle();

    std::unique_ptr<BPicture> m_haikuPicture; // owned by this window
    BView* m_haikuDrawView = nullptr;          // view used for drawing
    wxRegion m_updateRegion;
    bool m_mouseInside = false;
    int32 m_haikuLastMouseButtons = 0;  // tracks pressed-button state for MouseUp detection

    bool m_pendingSize = false;
    wxSize m_pendingClientSize;

#if wxUSE_ACCEL
    wxAcceleratorTable m_accelTable;
    std::unique_ptr<wxHaikuShortcutHandler> m_haikuShortcutHandler;
    bool m_processingShortcut = false;
#endif

    wxDECLARE_DYNAMIC_CLASS_NO_COPY(wxWindowHaiku);
};

#endif // _WX_HAIKU_WINDOW_H_
