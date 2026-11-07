/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/bmpbuttn.cpp
// Purpose:     wxBitmapButton implementation for Haiku
// Author:      Based on Qt; adapted for Haiku
// Created:     [Date]
// Copyright:   (c) 2025 wxWidgets team
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////
#include "wx/wxprec.h"
#include "wx/bmpbuttn.h"
#include "wx/haiku/private.h"
#include "wx/log.h"

#include <Button.h>
#include <Message.h>

// ----------------------------------------------------------------------------
// wxBitmapButton
// ----------------------------------------------------------------------------


// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_BMPBUTTON

#include "wx/bmpbuttn.h"

wxBitmapButton::wxBitmapButton(wxWindow *parent,
               wxWindowID id,
               const wxBitmapBundle& bitmap,
               const wxPoint& pos,
               const wxSize& size,
               long style,
               const wxValidator& validator,
               const wxString& name )
{
    Create( parent, id, bitmap, pos, size, style, validator, name );
}


bool wxBitmapButton::Create(wxWindow *parent,
            wxWindowID id,
            const wxBitmapBundle& bitmap,
            const wxPoint& pos,
            const wxSize& size,
            long style,
            const wxValidator& validator,
            const wxString& name )
{
    if ( !wxBitmapButtonBase::Create( parent, id, pos, size, style, validator, name ))
        return false;
    // Show the initial bitmap and resize accordingly:
    if ( bitmap.IsOk() )
    {
        wxBitmapButtonBase::SetBitmapLabel(bitmap);

        // we need to adjust the size after setting the bitmap as it may be too
        // big for the default button size
        SetInitialSize(size);
    }
    return true;
}

#endif // wxUSE_BMPBUTTON
