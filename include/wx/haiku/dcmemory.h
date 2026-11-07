/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/dcmemory.h
// Purpose:     wxMemoryDCImpl for Haiku
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_DCMEMORY_H_
#define _WX_HAIKU_DCMEMORY_H_

#include "wx/dcmemory.h"
#include "wx/haiku/dc.h"

class WXDLLIMPEXP_CORE wxMemoryDCImpl : public wxHaikuDCImpl
{
public:
    explicit wxMemoryDCImpl(wxMemoryDC* owner);
    wxMemoryDCImpl(wxMemoryDC* owner, wxBitmap& bitmap);
    wxMemoryDCImpl(wxMemoryDC* owner, wxDC* dc);
    ~wxMemoryDCImpl() override;

    void DoGetSize(int* width, int* height) const override;
    void DoSelect(const wxBitmap& bitmap) override;
    wxBitmap DoGetAsBitmap(const wxRect* subrect) const override;

    const wxBitmap& GetSelectedBitmap() const override;
    wxBitmap& GetSelectedBitmap() override;

    void SetLayoutDirection(wxLayoutDirection dir) override;

private:
    // Synchronize drawing and release temporary native resources.
    void ReleaseDrawingBitmap();

    wxBitmap m_selected;

    // View-capable bitmap used only while this memory DC is drawing.
    // The original selected wxBitmap remains lightweight.
    BBitmap* m_drawingBitmap = nullptr;

    wxDECLARE_CLASS(wxMemoryDCImpl);
    wxDECLARE_NO_COPY_CLASS(wxMemoryDCImpl);
};

#endif // _WX_HAIKU_DCMEMORY_H_
