/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/webview.cpp
// Purpose:     wxWebView implementation for Haiku using BWebView
// Created:     [Date]
// Copyright:   (c) 2025 wxWidgets team
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/haiku/webview.h"
#include "wx/haiku/private.h"
#include "wx/webview.h"
#include "wx/log.h"

#include <WebView.h>
#include <WebPage.h>
#include <WebSettings.h>
#include <View.h>
#include <TranslationUtils.h>
#include <Messenger.h>
#include <Message.h>

// Message constants
const uint32 B_WEBVIEW_LOAD_FINISHED = 'wvld';
const uint32 B_WEBVIEW_TITLE_CHANGED = 'wvtc';
const uint32 B_WEBVIEW_URL_CHANGED = 'wvuc';

// ----------------------------------------------------------------------------
// wxWebViewHaiku
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(wxWebViewHaiku, wxWebViewGeneric);

wxWebViewHaiku::wxWebViewHaiku()
    : m_bWebView(nullptr)
    , m_bWebPage(nullptr)
    , m_settings(nullptr)
    , m_zoomType(wxWEBVIEW_ZOOM_TYPE_LAYOUT)
{
}

wxWebViewHaiku::~wxWebViewHaiku()
{
    if (m_bWebView)
    {
        m_bWebView->RemoveSelf();
        delete m_bWebView;
    }
}

bool wxWebViewHaiku::Create(wxWindow* parent,
                            wxWindowID id,
                            const wxString& url,
                            const wxPoint& pos,
                            const wxSize& size,
                            long style,
                            const wxString& name)
{
    BRect frame = wxToBRect(wxRect(pos, size));
    m_bWebView = new BWebView("webview", frame, B_FOLLOW_ALL_SIDES, B_WILL_DRAW);
    m_bWebPage = m_bWebView->Page();
    m_settings = m_bWebPage->Settings();

    // Configure settings
    m_settings->SetLoadsImagesAutomatically(true);
    m_settings->SetJavaScriptEnabled(true);
    m_settings->SetPluginsEnabled(true);

    wxWindow* win = wxCreateWindowFromBView(parent, m_bWebView, id);
    if (!win)
        return false;

    InjectMessageBridge();

    if (!url.IsEmpty())
        LoadURL(url);

    return wxWebViewGeneric::Create(parent, id, url, pos, size, style, name);
}

// ----------------------------------------------------------------------------
// Navigation
// ----------------------------------------------------------------------------

void wxWebViewHaiku::LoadURL(const wxString& url)
{
    if (!m_bWebView)
        return;

    m_bWebView->LoadURL(wxToUTF8(url));
    PostEvent(wxEVT_WEBVIEW_NAVIGATED, url);
}

void wxWebViewHaiku::GoBack()
{
    if (m_bWebView && CanGoBack())
        m_bWebView->GoBack();
}

void wxWebViewHaiku::GoForward()
{
    if (m_bWebView && CanGoForward())
        m_bWebView->GoForward();
}

void wxWebViewHaiku::Reload()
{
    if (m_bWebView)
        m_bWebView->Reload();
}

void wxWebViewHaiku::Stop()
{
    if (m_bWebView)
        m_bWebView->StopLoading();
}

bool wxWebViewHaiku::CanGoBack() const
{
    return m_bWebView && m_bWebView->CanGoBack();
}

bool wxWebViewHaiku::CanGoForward() const
{
    return m_bWebView && m_bWebView->CanGoForward();
}

bool wxWebViewHaiku::IsBusy() const
{
    return m_bWebView && m_bWebView->IsLoading();
}

wxString wxWebViewHaiku::GetCurrentURL() const
{
    if (!m_bWebView)
        return wxEmptyString;

    BString url = m_bWebView->MainWebFrame()->URL();
    return wxString::FromUTF8(url.String());
}

wxString wxWebViewHaiku::GetPageTitle() const
{
    if (!m_bWebView)
        return wxEmptyString;

    BString title = m_bWebView->MainWebFrame()->Title();
    return wxString::FromUTF8(title.String());
}

// ----------------------------------------------------------------------------
// Zoom
// ----------------------------------------------------------------------------

void wxWebViewHaiku::SetZoomType(wxWebViewZoomType type)
{
    if (m_zoomType == type)
        return;

    m_zoomType = type;
}

wxWebViewZoomType wxWebViewHaiku::GetZoomType() const
{
    return m_zoomType;
}

void wxWebViewHaiku::SetZoomFactor(double factor)
{
    if (!m_bWebView)
        return;

    m_bWebView->SetTextZoomFactor(factor);
    m_bWebView->SetPageZoomFactor(factor);
}

double wxWebViewHaiku::GetZoomFactor() const
{
    if (!m_bWebView)
        return 1.0;

    return m_bWebView->PageZoomFactor();
}

// ----------------------------------------------------------------------------
// Printing
// ----------------------------------------------------------------------------

bool wxWebViewHaiku::Print()
{
    if (!m_bWebView)
        return false;

    m_bWebView->Print();
    return true;
}

// ----------------------------------------------------------------------------
// Clipboard
// ----------------------------------------------------------------------------

void wxWebViewHaiku::Cut()
{
    if (m_bWebView)
        m_bWebView->Cut();
}

void wxWebViewHaiku::Copy()
{
    if (m_bWebView)
        m_bWebView->Copy();
}

void wxWebViewHaiku::Paste()
{
    if (m_bWebView)
        m_bWebView->Paste();
}

bool wxWebViewHaiku::CanCut() const
{
    return m_bWebView && m_bWebView->CanCut();
}

bool wxWebViewHaiku::CanCopy() const
{
    return m_bWebView && m_bWebView->CanCopy();
}

bool wxWebViewHaiku::CanPaste() const
{
    return m_bWebView && m_bWebView->CanPaste();
}

// ----------------------------------------------------------------------------
// Selection
// ----------------------------------------------------------------------------

void wxWebViewHaiku::SelectAll()
{
    if (m_bWebView)
        m_bWebView->SelectAll();
}

void wxWebViewHaiku::ClearSelection()
{
    if (m_bWebView)
        m_bWebView->Unselect();
}

bool wxWebViewHaiku::HasSelection() const
{
    if (!m_bWebView)
        return false;

    BString selected = m_bWebView->SelectedString();
    return !selected.IsEmpty();
}

// ----------------------------------------------------------------------------
// Source
// ----------------------------------------------------------------------------

void wxWebViewHaiku::SetPage(const wxString& html, const wxString& baseUrl)
{
    if (!m_bWebView)
        return;

    BString bhtml(wxToUTF8(html));
    BString bbase(wxToUTF8(baseUrl));
    m_bWebView->MainWebFrame()->LoadHTMLString(bhtml, bbase);
}

wxString wxWebViewHaiku::GetPageSource() const
{
    // Not directly exposed — could use JS to get document.documentElement.outerHTML
    return wxEmptyString;
}

// ----------------------------------------------------------------------------
// JavaScript
// ----------------------------------------------------------------------------

void wxWebViewHaiku::RunScript(const wxString& javascript, wxString* output)
{
    ExecuteScript(javascript, output);
}

bool wxWebViewHaiku::ExecuteScript(const wxString& javascript, wxString* output)
{
    if (!m_bWebView)
        return false;

    BString result;
    bool success = m_bWebView->EvaluateJavaScript(wxToUTF8(javascript), &result);
    if (output)
        *output = wxString::FromUTF8(result.String());
    return success;
}

bool wxWebViewHaiku::AddScriptMessageHandler(const wxString& name)
{
    if (name.empty() || !m_bWebView)
        return false;

    m_scriptMessageHandlers.insert(name);
    return true;
}

bool wxWebViewHaiku::RemoveScriptMessageHandler(const wxString& name)
{
    auto it = m_scriptMessageHandlers.find(name);
    if (it == m_scriptMessageHandlers.end())
        return false;

    m_scriptMessageHandlers.erase(it);

    wxString js = wxString::Format("delete window.%s;", name);
    m_bWebView->EvaluateJavaScript(wxToUTF8(js));

    return true;
}

// ----------------------------------------------------------------------------
// User Agent
// ----------------------------------------------------------------------------

void wxWebViewHaiku::SetUserAgent(const wxString& userAgent)
{
    if (m_settings)
        m_settings->SetUserAgent(wxToUTF8(userAgent));
}

// ----------------------------------------------------------------------------
// Message Bridge
// ----------------------------------------------------------------------------

void wxWebViewHaiku::InjectMessageBridge()
{
    wxString js =
        "class ChromeWebView {\n"
        "  constructor() {\n"
        "    this.messageHandlers = new Set();\n"
        "  }\n"
        "  postMessage(name, data) {\n"
        "    if (this.messageHandlers.has(name)) {\n"
        "      const msg = JSON.stringify({name: name, data: data});\n"
        "      window.location = 'wxevent://' + encodeURIComponent(msg);\n"
        "    }\n"
        "  }\n"
        "  addMessageHandler(name) {\n"
        "    this.messageHandlers.add(name);\n"
        "  }\n"
        "}\n"
        "window.chrome = { webview: new ChromeWebView() };\n"
        "window.addEventListener('message', (event) => {\n"
        "  if (event.data && event.data.name) {\n"
        "    window.chrome.webview.postMessage(event.data.name, event.data.data);\n"
        "  }\n"
        "});";

    m_bWebView->EvaluateJavaScript(wxToUTF8(js));
}

void wxWebViewHaiku::OnScriptMessageReceived(const wxString& message, const wxString& handlerName)
{
    if (m_scriptMessageHandlers.find(handlerName) == m_scriptMessageHandlers.end())
        return;

    wxWebViewEvent event(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, GetId());
    event.SetMessageHandler(handlerName);
    event.SetString(message);
    event.SetEventObject(this);
    ProcessEvent(event);
}

// ----------------------------------------------------------------------------
// Context Menu
// ----------------------------------------------------------------------------

void wxWebViewHaiku::DoEnableContextMenu(bool WXUNUSED(enable))
{
    // Not directly controllable
}

// ----------------------------------------------------------------------------
// Implementation
// ----------------------------------------------------------------------------

BWebView* wxWebViewHaiku::GetBWebView() const
{
    return m_bWebView;
}

BWebPage* wxWebViewHaiku::GetBWebPage() const
{
    return m_bWebPage;
}

void wxWebViewHaiku::PostEvent(wxEventType type, const wxString& url)
{
    wxWebViewEvent event(type, GetId());
    event.SetURL(url);
    event.SetEventObject(this);
    ProcessEvent(event);
}

// ... (after wxWebViewHaiku implementation)

// ----------------------------------------------------------------------------
// wxWebViewFactoryHaiku
// ----------------------------------------------------------------------------

class wxWebViewFactoryHaiku : public wxWebViewFactory
{
public:
    virtual wxWebView* Create() override
    {
        return new wxWebViewHaiku();
    }

    virtual wxWebView* Create(wxWindow* parent,
                              wxWindowID id,
                              const wxString& url = wxASCII_STR(wxWebViewDefaultURLStr),
                              const wxPoint& pos = wxDefaultPosition,
                              const wxSize& size = wxDefaultSize,
                              long style = 0,
                              const wxString& name = wxASCII_STR(wxWebViewNameStr)) override
    {
        wxWebViewHaiku* web = new wxWebViewHaiku();
        if (!web->Create(parent, id, url, pos, size, style, name))
        {
            wxLogError("Failed to create wxWebViewHaiku");
            delete web;
            return nullptr;
        }
        return web;
    }

    virtual bool IsAvailable() override
    {
        return true;
    }
};

// ----------------------------------------------------------------------------
// Module initialization
// ----------------------------------------------------------------------------

static struct wxWebViewHaikuModule
{
    wxWebViewHaikuModule()
    {
        wxWebView::RegisterFactory(wxASCII_STR(wxWebViewBackendDefault),
                                   wxSharedPtr<wxWebViewFactory>(new wxWebViewFactoryHaiku()));
    }
} gs_haikuWebViewModule;
