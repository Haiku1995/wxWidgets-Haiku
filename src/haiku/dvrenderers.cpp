/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/dvrenderers.cpp
// Purpose:     Stub wxDataView renderer implementations for Haiku
// Author:      wxWidgets team (based on Qt port)
// Copyright:   (c) 2025 wxWidgets team
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#if wxUSE_DATAVIEWCTRL

#include "wx/dataview.h"
//
#ifndef wxHAS_GENERIC_DATAVIEWCTRL

// ----------------------------------------------------------------------------
// wxDataViewTextRenderer
// ----------------------------------------------------------------------------

wxDataViewTextRenderer::wxDataViewTextRenderer(const wxString& varianttype,
                                               wxDataViewCellMode mode,
                                               int align)
    : wxDataViewRenderer(varianttype, mode, align)
{
}

bool wxDataViewTextRenderer::SetValue(const wxVariant& value)
{
    return false;
}

bool wxDataViewTextRenderer::GetValue(wxVariant& value) const
{
    return false;
}

void wxDataViewTextRenderer::SetAlignment(int align)
{
    wxDataViewRenderer::SetAlignment(align);
}

// ----------------------------------------------------------------------------
// wxDataViewBitmapRenderer
// ----------------------------------------------------------------------------

wxDataViewBitmapRenderer::wxDataViewBitmapRenderer(const wxString& varianttype,
                                                   wxDataViewCellMode mode,
                                                   int align)
    : wxDataViewRenderer(varianttype, mode, align)
{
}

bool wxDataViewBitmapRenderer::SetValue(const wxVariant& value)
{
    return false;
}

bool wxDataViewBitmapRenderer::GetValue(wxVariant& value) const
{
    return false;
}

bool wxDataViewBitmapRenderer::IsCompatibleVariantType(const wxString& variantType) const
{
    return variantType == wxS("wxBitmap") || variantType == wxS("wxIcon");
}

// ----------------------------------------------------------------------------
// wxDataViewToggleRenderer
// ----------------------------------------------------------------------------

wxDataViewToggleRenderer::wxDataViewToggleRenderer(const wxString& varianttype,
                                                   wxDataViewCellMode mode,
                                                   int align)
    : wxDataViewRenderer(varianttype, mode, align)
{
}

bool wxDataViewToggleRenderer::SetValue(const wxVariant& value)
{
    return false;
}

bool wxDataViewToggleRenderer::GetValue(wxVariant& value) const
{
    return false;
}

// ----------------------------------------------------------------------------
// wxDataViewProgressRenderer
// ----------------------------------------------------------------------------

wxDataViewProgressRenderer::wxDataViewProgressRenderer(const wxString& label,
                                                       const wxString& varianttype,
                                                       wxDataViewCellMode mode,
                                                       int align)
    : wxDataViewCustomRenderer(varianttype, mode, align)
{
    wxUnusedVar(label);
}

bool wxDataViewProgressRenderer::SetValue(const wxVariant& value)
{
    return false;
}

bool wxDataViewProgressRenderer::GetValue(wxVariant& value) const
{
    return false;
}

bool wxDataViewProgressRenderer::Render(wxRect cell, wxDC* dc, int state)
{
    return false;
}

wxSize wxDataViewProgressRenderer::GetSize() const
{
    return wxSize();
}

// ----------------------------------------------------------------------------
// wxDataViewIconTextRenderer
// ----------------------------------------------------------------------------

wxDataViewIconTextRenderer::wxDataViewIconTextRenderer(const wxString& varianttype,
                                                       wxDataViewCellMode mode,
                                                       int align)
    : wxDataViewCustomRenderer(varianttype, mode, align)
{
}

bool wxDataViewIconTextRenderer::SetValue(const wxVariant& value)
{
    return false;
}

bool wxDataViewIconTextRenderer::GetValue(wxVariant& value) const
{
    return false;
}

bool wxDataViewIconTextRenderer::Render(wxRect cell, wxDC* dc, int state)
{
    return false;
}

wxSize wxDataViewIconTextRenderer::GetSize() const
{
    return wxSize();
}

wxControl* wxDataViewIconTextRenderer::CreateEditorCtrl(wxWindow* parent,
                                                        wxRect labelRect,
                                                        const wxVariant& value)
{
    return nullptr;
}

bool wxDataViewIconTextRenderer::GetValueFromEditorCtrl(wxControl* editor,
                                                        wxVariant& value)
{
    return false;
}

// ----------------------------------------------------------------------------
// wxDataViewDateRenderer
// ----------------------------------------------------------------------------

wxDataViewDateRenderer::wxDataViewDateRenderer(const wxString& varianttype,
                                               wxDataViewCellMode mode,
                                               int align)
    : wxDataViewCustomRenderer(varianttype, mode, align)
{
}

bool wxDataViewDateRenderer::SetValue(const wxVariant& value)
{
    return false;
}

bool wxDataViewDateRenderer::GetValue(wxVariant& value) const
{
    return false;
}

bool wxDataViewDateRenderer::Render(wxRect cell, wxDC* dc, int state)
{
    return false;
}

wxSize wxDataViewDateRenderer::GetSize() const
{
    return wxSize();
}

bool wxDataViewDateRenderer::Activate(wxRect cell,
                                      wxDataViewModel* model,
                                      const wxDataViewItem& item,
                                      unsigned int col)
{
    return false;
}

// ----------------------------------------------------------------------------
// wxDataViewChoiceRenderer
// ----------------------------------------------------------------------------

wxDataViewChoiceRenderer::wxDataViewChoiceRenderer(const wxArrayString& choices,
                                                   wxDataViewCellMode mode,
                                                   int alignment)
    : wxDataViewCustomRenderer(wxT("string"), mode, alignment),
      m_choices(choices)
{
}

bool wxDataViewChoiceRenderer::Render(wxRect rect, wxDC* dc, int state)
{
    return false;
}

wxSize wxDataViewChoiceRenderer::GetSize() const
{
    return wxSize();
}

bool wxDataViewChoiceRenderer::SetValue(const wxVariant& value)
{
    return false;
}

bool wxDataViewChoiceRenderer::GetValue(wxVariant& value) const
{
    return false;
}

void wxDataViewChoiceRenderer::SetAlignment(int align)
{
    wxDataViewRenderer::SetAlignment(align);
}

#endif // !wxHAS_GENERIC_DATAVIEWCTRL

#endif // wxUSE_DATAVIEWCTRL
