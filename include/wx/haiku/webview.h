#ifndef WX_HAIKU_WEBVIEW_H
#define WX_HAIKU_WEBVIEW_H

#include "wx/webview.h"

class BWebView;
class BWindow;

class wxWebViewHaiku : public wxWebView
{
public:
    wxWebViewHaiku() : m_webView(nullptr) {}
    wxWebViewHaiku(wxWindow* parent, wxWindowID id, const wxString& url = wxWebViewDefaultURLStr,
                   const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize,
                   long style = 0, const wxString& name = wxWebViewNameStr)
        : m_webView(nullptr)
    {
        Create(parent, id, url, pos, size, style, name);
    }
    virtual ~wxWebViewHaiku();

    virtual bool Create(wxWindow* parent, wxWindowID id, const wxString& url,
                       const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize,
                       long style = 0, const wxString& name = wxWebViewNameStr) override;

    virtual void LoadURL(const wxString& url) override;
    virtual void Stop() override;
    virtual bool CanGoBack() const override;
    virtual bool CanGoForward() const override;
    virtual void GoBack() override;
    virtual void GoForward() override;
    virtual void Reload(wxWebViewReloadFlags flags = wxWEBVIEW_RELOAD_DEFAULT) override;
    virtual wxString GetCurrentURL() const override;
    virtual wxString GetCurrentTitle() const override;
    virtual bool RunScript(const wxString& javascript, wxString* output = nullptr) override;

    virtual void SetZoomType(wxWebViewZoomType type) override;
    virtual wxWebViewZoomType GetZoomType() const override;
    virtual bool CanSetZoomType(wxWebViewZoomType type) const override;
    virtual void SetZoom(float zoom) override;
    virtual float GetZoom() const override;

    virtual bool IsBusy() const override;
    virtual bool IsEditable() const override;
    virtual void SetEditable(bool enable = true) override;

    bool HaikuHandleMessage(BMessage* msg);

private:
    BWebView* m_webView;
    BWindow* m_parentWindow;
    wxString m_currentURL;
    wxWebViewZoomType m_zoomType;

    wxDECLARE_DYNAMIC_CLASS(wxWebViewHaiku);
};

#endif // WX_HAIKU_WEBVIEW_H