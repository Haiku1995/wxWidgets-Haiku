/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/dcclient.h
// Purpose:     wxWindowDCImpl, wxClientDCImpl, wxPaintDCImpl for Haiku
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_DCCLIENT_H_
#define _WX_HAIKU_DCCLIENT_H_

#include "wx/haiku/dc.h"
#include "wx/window.h"

class WXDLLIMPEXP_CORE wxWindowDCImpl : public wxHaikuDCImpl
{
public:
    explicit wxWindowDCImpl(wxDC* owner);
    wxWindowDCImpl(wxDC* owner, wxWindow* win);
    ~wxWindowDCImpl() override;

    void DoGetSize(int* width, int* height) const override;

protected:
    wxWindow* m_window = nullptr;

private:
    wxDECLARE_CLASS(wxWindowDCImpl);
    wxDECLARE_NO_COPY_CLASS(wxWindowDCImpl);
};

class WXDLLIMPEXP_CORE wxClientDCImpl : public wxWindowDCImpl
{
public:
    explicit wxClientDCImpl(wxDC* owner);
    wxClientDCImpl(wxDC* owner, wxWindow* win);

    void DoGetSize(int* width, int* height) const override;

    static bool CanBeUsedForDrawing(const wxWindow* WXUNUSED(window))
    {
        return true;
    }

private:
    wxDECLARE_CLASS(wxClientDCImpl);
    wxDECLARE_NO_COPY_CLASS(wxClientDCImpl);
};

class WXDLLIMPEXP_CORE wxPaintDCImpl : public wxWindowDCImpl
{
public:
    explicit wxPaintDCImpl(wxDC* owner);
    wxPaintDCImpl(wxDC* owner, wxWindow* win);

private:
    wxDECLARE_CLASS(wxPaintDCImpl);
    wxDECLARE_NO_COPY_CLASS(wxPaintDCImpl);
};

#endif // _WX_HAIKU_DCCLIENT_H_
