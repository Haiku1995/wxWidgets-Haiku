#ifndef WX_HAIKU_BANNERWINDOW_H
#define WX_HAIKU_BANNERWINDOW_H

#include "wx/bannerwindow.h"
#include "wx/haiku/window.h"

class BView;

class wxBannerWindowHaiku : public wxBannerWindowBase
{
public:
    wxBannerWindowHaiku();
    wxBannerWindowHaiku(wxWindow* parent, wxDirection dir = wxLEFT);
    virtual ~wxBannerWindowHaiku();

    virtual bool Create(wxWindow* parent, wxDirection dir = wxLEFT) override;
    virtual void SetText(const wxString& title, const wxString& message) override;

private:
    BView* m_bannerView;

    wxDECLARE_DYNAMIC_CLASS(wxBannerWindowHaiku);
};

#endif // WX_HAIKU_BANNERWINDOW_H