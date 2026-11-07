///////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/toplevel.h
// Purpose:     declares wxTopLevelWindowHaiku class
// Author:      Alan Touran (based on wx/qt/toplevel.h)
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_TOPLEVEL_H_
#define _WX_HAIKU_TOPLEVEL_H_

#include "wx/toplevel.h"
#include "wx/iconbndl.h"
#include <InterfaceKit.h>
//class BWindow;
//class BView;

class WXDLLIMPEXP_CORE wxTopLevelWindowHaiku : public wxTopLevelWindowBase
{
public:
    wxTopLevelWindowHaiku() = default;

    wxTopLevelWindowHaiku(wxWindow* parent,
                          wxWindowID winid,
                          const wxString& title,
                          const wxPoint& pos = wxDefaultPosition,
                          const wxSize& size = wxDefaultSize,
                          long style = wxDEFAULT_FRAME_STYLE,
                          const wxString& name = wxASCII_STR(wxFrameNameStr));

    bool Create(wxWindow* parent,
                wxWindowID id,
                const wxString& title,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = wxDEFAULT_FRAME_STYLE,
                const wxString& name = wxASCII_STR(wxFrameNameStr));
 virtual ~wxTopLevelWindowHaiku();
    bool Show(bool show = true) override;
    void Maximize(bool maximize = true) override;
    void Restore() override;
    void Iconize(bool iconize = true) override;
    bool IsMaximized() const override;
    bool IsIconized() const override;

    bool ShowFullScreen(bool show, long style = wxFULLSCREEN_ALL) override;
    bool IsFullScreen() const override;
    void SetTitle(const wxString& title) override;
    wxString GetTitle() const override;
    void SetIcons(const wxIconBundle& icons) override;

    void SetWindowStyleFlag(long style) override;
    long GetWindowStyleFlag() const override;
    void HaikuLayoutRootView(); 
    // IMPORTANT: used by wxWindowHaiku::Create(wxWindow* parent,...) to attach children.
    BView* HaikuGetParentView() const override;

protected:
    void HaikuSetSizeIncrement(int width, int height);
    void DoSetSizeHints(int minW, int minH,
                        int maxW, int maxH,
                        int incW, int incH) override;


    BWindow* GetHaikuWindow() const;

    BWindow* m_haikuWindow = nullptr;
    BView*   m_rootView = nullptr;
    bool     m_isFullScreen = false;

    BRect        m_savedFrame{};
    window_look  m_savedLook{};


};


#endif // _WX_HAIKU_TOPLEVEL_H_
