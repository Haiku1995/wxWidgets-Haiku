/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/anybutton.h
// Purpose:     wxAnyButton class for Haiku
// Author:      wxWidgets Team (Haiku port)
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_ANYBUTTON_H_
#define _WX_HAIKU_ANYBUTTON_H_

#include "wx/anybutton.h"

class BButton;
class wxHaikuButtonImpl;
class wxWindowHaiku;

// ============================================================================
// wxAnyButton for Haiku
// ============================================================================
class WXDLLIMPEXP_CORE wxAnyButton : public wxAnyButtonBase
{
public:
    wxAnyButton();
    virtual ~wxAnyButton();

    // Label
    void SetLabel(const wxString& label) override;
    wxString GetLabel() const override;

    // Enable/disable
    bool Enable(bool enable = true) override;

    // Subclasses (wxButton, wxBitmapButton, etc.) must define event type
    virtual int HaikuGetEventType() const = 0;

protected:
    // Bitmap handling
    wxBitmap DoGetBitmap(State state) const override;
    void DoSetBitmap(const wxBitmapBundle& bitmap, State which) override;

    // Create native control
    void HaikuCreate(wxWindowHaiku* parent);
    void HaikuUpdateBitmap();

    BButton* GetBButton() const;

private:
    wxHaikuButtonImpl* m_impl;                  // backend impl (wraps BButton)
    wxBitmapBundle     m_bitmaps[State_Max];    // cached per-state bitmaps

    wxDECLARE_NO_COPY_CLASS(wxAnyButton);
    friend class wxHaikuButtonImpl;
};

#endif // _WX_HAIKU_ANYBUTTON_H_
