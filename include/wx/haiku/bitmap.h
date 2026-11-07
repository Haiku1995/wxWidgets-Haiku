// include/wx/haiku/bitmap.h

#ifndef _WX_HAIKU_BITMAP_H_
#define _WX_HAIKU_BITMAP_H_

//#include "wx/bitmap.h"
#include <Bitmap.h>

class wxCursor;

class WXDLLIMPEXP_CORE wxBitmapRefData : public wxGDIRefData
{
public:
    wxBitmapRefData(int width = 0, int height = 0, int depth = -1);
    wxBitmapRefData(const wxBitmapRefData& data);
    virtual ~wxBitmapRefData();

    BBitmap* m_bitmap;
    wxMask*  m_mask;
#if wxUSE_PALETTE
    wxPalette* m_palette;
#endif
};

class WXDLLIMPEXP_CORE wxBitmap : public wxBitmapBase
{
public:
    wxBitmap() { }
    wxBitmap(const wxBitmap& bmp) : wxBitmapBase(bmp) { }
    wxBitmap& operator=(const wxBitmap&) = default;
    wxBitmap(const char bits[], int width, int height, int depth = 1);
    wxBitmap(int width, int height, int depth = wxBITMAP_SCREEN_DEPTH);
    wxBitmap(int width, int height, const wxDC& dc)
        { Create(width, height, dc); }
    wxBitmap(const wxSize& sz, int depth = wxBITMAP_SCREEN_DEPTH)
        { Create(sz.GetWidth(), sz.GetHeight(), depth); }
    wxBitmap(const char* const* bits);
    explicit wxBitmap(const wxCursor& cursor);
    wxBitmap(const wxString& filename, wxBitmapType type = wxBITMAP_TYPE_ANY);
    wxBitmap(const wxImage& image, int depth = wxBITMAP_SCREEN_DEPTH, double scale = 1.0);
    // wxBitmap(const wxImage& image, const wxDC& dc); // optional, not standard
    wxBitmap(const wxImage& image, const wxDC& dc);  // 

    static void InitStandardHandlers();

    bool Create(int width, int height, int depth = wxBITMAP_SCREEN_DEPTH) override;
    bool Create(const wxSize& sz, int depth = wxBITMAP_SCREEN_DEPTH) override
        { return Create(sz.GetWidth(), sz.GetHeight(), depth); }
    // Remove Create(int, int, const wxDC&) unless needed
    bool Create(int width, int height, const wxDC& dc) ;
    int GetHeight() const override;
    int GetWidth() const override;
    int GetDepth() const override;
    bool HasAlpha() const override;
    bool UseAlpha(bool use = true) override;

    wxImage ConvertToImage() const override;

    wxMask* GetMask() const override;
    void SetMask(wxMask* mask) override;

    wxBitmap GetSubBitmap(const wxRect& rect) const override;

    bool SaveFile(const wxString& name, wxBitmapType type, const wxPalette* palette = nullptr) const override;
    bool LoadFile(const wxString& name, wxBitmapType type = wxBITMAP_TYPE_ANY) override;

    void* GetRawData(wxPixelDataBase& data, int bpp) ;
    void UngetRawData(wxPixelDataBase& data) ;

    BBitmap* GetBBitmap() const;

bool IsOk() const override
{
    wxBitmapRefData* ref = static_cast<wxBitmapRefData*>(GetRefData());
    return ref && ref->m_bitmap != nullptr;
}

#if wxUSE_PALETTE
    wxPalette* GetPalette() const override;
    void SetPalette(const wxPalette& palette) override;
#endif

protected:
    wxGDIRefData* CreateGDIRefData() const override;
    wxGDIRefData* CloneGDIRefData(const wxGDIRefData* data) const override;

    wxDECLARE_DYNAMIC_CLASS(wxBitmap);
};

// wxMask declaration (simplified)
class WXDLLIMPEXP_CORE wxMask : public wxMaskBase
{
public:
    wxMask();
    wxMask(const wxMask& mask);
    wxMask(const wxBitmap& bitmap, const wxColour& colour);
#if wxUSE_PALETTE
    wxMask(const wxBitmap& bitmap, int paletteIndex);
#endif
    wxMask(const wxBitmap& bitmap);
    wxMask(BBitmap* haikuBitmap);
    virtual ~wxMask();

    wxBitmap GetBitmap() const;
    bool InitFromColour(const wxBitmap& bitmap, const wxColour& colour) override;
    bool InitFromMonoBitmap(const wxBitmap& bitmap) override;

protected:
    void FreeData() override;
    BBitmap* m_bitmap;

    wxDECLARE_DYNAMIC_CLASS(wxMask);
};

#endif // _WX_HAIKU_BITMAP_H_
