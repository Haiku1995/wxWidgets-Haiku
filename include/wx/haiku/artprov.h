/////////////////////////////////////////////////////////////////////////////
// Name:        artprov.h
// Purpose:     wxArtProvider class for Haiku OS
// Author:      [Your Name or Original Author]
// Copyright:   (c) 2025 wxWidgets dev team
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_ARTPROV_H_
#define _WX_HAIKU_ARTPROV_H_

#include "wx/defs.h"
#include "wx/artprov.h"

class WXDLLIMPEXP_CORE wxArtProvider : public wxArtProvider
{
public:
    wxArtProvider() {}
    virtual ~wxArtProvider() {}

protected:
    virtual wxBitmap CreateBitmap(const wxArtID& id, const wxArtClient& client, const wxSize& size) override;
};

#endif // _WX_HAIKU_ARTPROV_H_
