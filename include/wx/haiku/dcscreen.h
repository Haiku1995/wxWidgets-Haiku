/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/dcscreen.h
// Purpose:     wxScreenDCImpl for Haiku
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_DCSCREEN_H_
#define _WX_HAIKU_DCSCREEN_H_

#include "wx/dcscreen.h"
#include "wx/haiku/dcclient.h"

class BBitmap;
class BView;

class WXDLLIMPEXP_CORE wxScreenDCImpl : public wxWindowDCImpl
{
public:
    explicit wxScreenDCImpl(wxScreenDC* owner);
    ~wxScreenDCImpl() override;

    void DoGetSize(int* width, int* height) const override;
    wxBitmap DoGetAsBitmap(const wxRect* subrect) const override;

protected:
    BBitmap* GetScreenBitmap();

private:
    BBitmap* m_screenBitmap = nullptr;
    BView*   m_screenView = nullptr;

    wxDECLARE_CLASS(wxScreenDCImpl);
    wxDECLARE_NO_COPY_CLASS(wxScreenDCImpl);
};

#endif // _WX_HAIKU_DCSCREEN_H_
