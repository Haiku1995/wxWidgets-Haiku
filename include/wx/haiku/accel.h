/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/accel.h
// Purpose:     wxAcceleratorTable for Haiku
// Author:      [Your Name]
// Copyright:   (c) 2025 wxWidgets dev team
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_ACCEL_H_
#define _WX_HAIKU_ACCEL_H_

#include "wx/defs.h"
//#include "wx/object.h"


// Forward declare Haiku types to avoid including heavy headers in header
class BWindow;
class BHandler;

class WXDLLIMPEXP_CORE wxAcceleratorTable : public wxObject
{
public:
    wxAcceleratorTable();
    wxAcceleratorTable(int n, const wxAcceleratorEntry entries[]);

    // Apply all accelerators in this table to the given Haiku window.
    // This is the Haiku equivalent of Qt's ConvertShortcutTable().
    void ApplyToWindow(BWindow* window, BHandler* target = nullptr) const;
    void RemoveFromWindow(BWindow* window) const;

    bool Ok() const { return IsOk(); }
    bool IsOk() const;

protected:
    virtual wxObjectRefData* CreateRefData() const override;
    wxNODISCARD virtual wxObjectRefData* CloneRefData(const wxObjectRefData* data) const override;

private:
    wxDECLARE_DYNAMIC_CLASS(wxAcceleratorTable);
};

#endif // _WX_HAIKU_ACCEL_H_
