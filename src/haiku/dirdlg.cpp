
/////////////////////////////////////////////////////////////////////////////
/// Name:        src/haiku/dirdlg.cpp
// Purpose:     wxDirDialog implementation for Haiku
// Author:      Based on wxFileDialog
// Created:     [Date]
// Copyright:   (c) 2025 wxWidgets team
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////
/*
#include "wx/haiku/dirdlg.h"
#include "wx/haiku/private.h"

wxIMPLEMENT_DYNAMIC_CLASS(wxDirDialog, wxDirDialogBase);

bool wxDirDialog::Create(wxWindow* parent,
                         const wxString& message,
                         const wxString& defaultPath,
                         long style,
                         const wxPoint& pos,
                         const wxSize& size,
                         const wxString& name)
{
    long fdStyle = wxFD_OPEN | wxFD_DIR_MUST_EXIST;
    if (style & wxDD_CHANGE_DIR)
        fdStyle |= wxFD_CHANGE_DIR;

    return m_fileDialog.Create(parent, message, defaultPath, wxEmptyString,
                               wxEmptyString, fdStyle, pos, size, name);
}

int wxDirDialog::ShowModal()
{
    int result = m_fileDialog.ShowModal();
    if (result == wxID_OK)
    {
        m_path = m_fileDialog.GetPath();
    }
    return result;
}

wxString wxDirDialog::GetPath() const
{
    return m_path;
}

void wxDirDialog::SetPath(const wxString& path)
{
    m_path = path;
    m_fileDialog.SetPath(path);
}
*/
