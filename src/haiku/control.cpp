/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/control.cpp
// Purpose:     wxControl implementation for Haiku
// Author:      Based on Qt port; adapted for Haiku
// Created:     [Date]
// Copyright:   (c) 2025 wxWidgets team
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/control.h"
#include "wx/haiku/private.h"
#include "wx/window.h"
#include "wx/log.h"
#include <Control.h>
#include <StringView.h>
#include <Looper.h>
#include <cmath>

// ----------------------------------------------------------------------------
// wxControl
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(wxControl, wxWindow);

wxControl::wxControl(wxWindow* parent,
                     wxWindowID id,
                     const wxPoint& pos,
                     const wxSize& size,
                     long style,
                     const wxValidator& validator,
                     const wxString& name)
{
    Create(parent, id, pos, size, style, validator, name);
}

bool wxControl::Create(wxWindow* parent,
                       wxWindowID id,
                       const wxPoint& pos,
                       const wxSize& size,
                       long style,
                       const wxValidator& validator,
                       const wxString& name)
{
    return wxControlBase::Create(parent, id, pos, size, style, validator, name);
}

wxSize wxControl::DoGetBestSize() const
{
    // A control's current bounds can be stretched by its parent or sizer.
    // Query its native preferred dimensions instead of feeding those bounds
    // back into the next layout as an ever-growing minimum.
    BView* control = dynamic_cast<BControl*>(GetHandle());
    // BStringView is a BView, not a BControl, but also has an intrinsic
    // preferred size. Its stretched bounds are not a text measurement.
    if (!control)
        control = dynamic_cast<BStringView*>(GetHandle());
    if (control)
    {
        BLooper* looper = control->Looper();
        if (!looper || looper->Lock())
        {
            float width = 0, height = 0;
            control->GetPreferredSize(&width, &height);
            if (looper)
                looper->Unlock();
            if (std::isfinite(width) && std::isfinite(height) && width >= 0 && height >= 0)
                return wxSize(static_cast<int>(std::ceil(width + 1)),
                              static_cast<int>(std::ceil(height + 1)));
        }
    }
    return GetSize();
}
