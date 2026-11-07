#ifndef _WX_HAIKU_BRUSH_H_
#define _WX_HAIKU_BRUSH_H_

#include <interface/View.h>
#include <interface/Bitmap.h>
#include <support/SupportDefs.h>

class WXDLLIMPEXP_CORE wxBrush : public wxBrushBase
{
public:
    wxBrush();
    wxBrush(const wxColour& col, wxBrushStyle style = wxBRUSHSTYLE_SOLID);

    wxDEPRECATED_MSG("use wxBRUSHSTYLE_XXX constants")
    wxBrush(const wxColour& col, int style);

    wxBrush(const wxBitmap& stipple);

    virtual ~wxBrush();

    virtual void SetColour(const wxColour& col) override;
    virtual void SetColour(unsigned char r, unsigned char g, unsigned char b) override;
    virtual void SetStyle(wxBrushStyle style) override;
    virtual void SetStipple(const wxBitmap& stipple) override;

    bool operator==(const wxBrush& brush) const;
    bool operator!=(const wxBrush& brush) const { return !(*this == brush); }

    virtual wxColour GetColour() const override;
    virtual wxBrushStyle GetStyle() const override;
    virtual wxBitmap *GetStipple() const override;

    wxDEPRECATED_MSG("use wxBRUSHSTYLE_XXX constants")
    void SetStyle(int style) { SetStyle((wxBrushStyle)style); }

    // Apply brush settings to a BView for drawing
    void ApplyToView(BView* view) const;

    // Haiku-specific accessors for pattern and stipple
    pattern GetHaikuPattern() const;
    BBitmap* GetHaikuStipple() const;

protected:
    virtual wxGDIRefData *CreateGDIRefData() const override;
    virtual wxGDIRefData *CloneGDIRefData(const wxGDIRefData *data) const override;

private:
    wxDECLARE_DYNAMIC_CLASS(wxBrush);
};

#endif // _WX_HAIKU_BRUSH_H_
