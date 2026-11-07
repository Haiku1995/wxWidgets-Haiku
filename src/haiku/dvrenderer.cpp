/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/dvrenderer.cpp
// Purpose:     Stub wxDataViewRenderer implementation for Haiku
// Author:      wxWidgets team
// Copyright:   (c) 2025 wxWidgets team
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#if wxUSE_DATAVIEWCTRL

#include "wx/dataview.h"

#ifndef wxHAS_GENERIC_DATAVIEWCTRL

wxDataViewRenderer::wxDataViewRenderer(const wxString& variantType,
                                       wxDataViewCellMode mode,
                                       int align)
    : wxDataViewRendererBase(variantType, mode, align)
{
}

void wxDataViewRenderer::SetMode(wxDataViewCellMode mode)
{
    m_mode = mode;
}

wxDataViewCellMode wxDataViewRenderer::GetMode() const
{
    return m_mode;
}

void wxDataViewRenderer::SetAlignment(int align)
{
    m_alignment = align;
}

int wxDataViewRenderer::GetAlignment() const
{
    return m_alignment;
}

void wxDataViewRenderer::EnableEllipsize(wxEllipsizeMode mode)
{
    m_ellipsizeMode = mode;
}

wxEllipsizeMode wxDataViewRenderer::GetEllipsizeMode() const
{
    return m_ellipsizeMode;
}

#endif // !wxHAS_GENERIC_DATAVIEWCTRL

#endif // wxUSE_DATAVIEWCTRL
