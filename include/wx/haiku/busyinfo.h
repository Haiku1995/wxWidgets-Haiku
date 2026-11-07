#ifndef WX_HAIKU_BUSYINFO_H
#define WX_HAIKU_BUSYINFO_H

#include "wx/busyinfo.h"

class BWindow;

class wxBusyInfoHaiku : public wxBusyInfoBase
{
public:
    wxBusyInfoHaiku(const wxBusyInfoFlags& flags);
    virtual ~wxBusyInfoHaiku();

    virtual bool Create(const wxBusyInfoFlags& flags);
    virtual void Show(bool show) override;

private:
    BWindow* m_busyWindow;

    wxDECLARE_DYNAMIC_CLASS(wxBusyInfoHaiku);
};

#endif // WX_HAIKU_BUSYINFO_H