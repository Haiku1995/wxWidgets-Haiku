/////////////////////////////////////////////////////////////////////////////
// Name:        aui.h
// Purpose:     wxAuiManager class for Haiku OS
// Author:      [Your Name or Original Author]
// Copyright:   (c) 2025 wxWidgets dev team
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_AUI_H_
#define _WX_HAIKU_AUI_H_

#include "wx/defs.h"
#include "wx/aui/aui.h"

class WXDLLIMPEXP_AUI wxAuiManager : public wxAuiManager
{
public:
    wxAuiManager(wxWindow* managedWnd = nullptr, unsigned int flags = wxAUI_MGR_DEFAULT);
    virtual ~wxAuiManager();
};

#endif // _WX_HAIKU_AUI_H_
