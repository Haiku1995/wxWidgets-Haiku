/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/msgdlg.cpp
// Purpose:     wxMessageDialog implementation for Haiku
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"
#include "wx/msgdlg.h"
#include "wx/control.h"
#include "wx/modalhook.h"
#include "wx/haiku/private.h"
#include <Alert.h>
#include <Button.h>

wxIMPLEMENT_CLASS(wxMessageDialog, wxDialog);

wxMessageDialog::wxMessageDialog(wxWindow* parent, const wxString& message,
                                const wxString& caption, long style,
                                const wxPoint& WXUNUSED(pos))
    : wxMessageDialogBase(parent, message, caption, style)
{
}

int wxMessageDialog::ShowModal()
{
    WX_HOOK_MODAL_DIALOG();
    const long style = GetMessageDialogStyle();
    wxString labels[3];
    int count = 0;
    int defaultIndex = 0;
    int escapeIndex = -1;
    if (style & wxYES_NO)
    {
        labels[count++] = GetYesLabel();
        labels[count++] = GetNoLabel();
        if (style & wxNO_DEFAULT)
            defaultIndex = 1;
    }
    else if (style & wxOK)
    {
        labels[count++] = GetOKLabel();
        if (!(style & wxCANCEL))
            escapeIndex = 0;
    }
    if (style & wxCANCEL)
    {
        escapeIndex = count;
        labels[count++] = GetCancelLabel();
        if (style & wxCANCEL_DEFAULT)
            defaultIndex = escapeIndex;
    }
    if (!count)
        return wxID_CANCEL;

    // Retain UTF-8 storage until BAlert copies all labels. Native buttons
    // do not parse wx mnemonic markers; GetLabelText preserves escaped &&.
    for (int i = 0; i < count; ++i)
        labels[i] = wxControl::GetLabelText(labels[i]);
    const wxScopedCharBuffer first = labels[0].utf8_str();
    const wxScopedCharBuffer second = labels[1].utf8_str();
    const wxScopedCharBuffer third = labels[2].utf8_str();
    wxString message = GetMessage();
    if (!GetExtendedMessage().empty())
        message += "\n\n" + GetExtendedMessage();

    BAlert* alert = new BAlert(GetTitle().utf8_str(), message.utf8_str(),
                              first.data(), count > 1 ? second.data() : nullptr,
                              count > 2 ? third.data() : nullptr,
                              B_WIDTH_AS_USUAL, GetAlertType(style));
    // Indices follow constructor order, independently of the default button.
    alert->SetDefaultButton(alert->ButtonAt(defaultIndex));
    if (escapeIndex >= 0)
        alert->SetShortcut(escapeIndex, B_ESCAPE);
    const int result = MapButtonIndex(alert->Go(), style);
    SetReturnCode(result);
    return result;
}

alert_type wxMessageDialog::GetAlertType(long style)
{
    if (style & wxICON_NONE) return B_EMPTY_ALERT;
    if (style & wxICON_EXCLAMATION) return B_WARNING_ALERT;
    if (style & wxICON_ERROR) return B_STOP_ALERT;
    if (style & wxICON_QUESTION) return B_IDEA_ALERT;
    if (style & wxICON_INFORMATION) return B_INFO_ALERT;
    return (style & wxYES_NO) ? B_IDEA_ALERT : B_INFO_ALERT;
}

int wxMessageDialog::MapButtonIndex(int32 buttonIndex, long style)
{
    if (style & wxYES_NO)
    {
        if (buttonIndex == 0) return wxID_YES;
        if (buttonIndex == 1) return wxID_NO;
        if (buttonIndex == 2 && (style & wxCANCEL)) return wxID_CANCEL;
    }
    else if (style & wxOK)
    {
        if (buttonIndex == 0) return wxID_OK;
        if (buttonIndex == 1 && (style & wxCANCEL)) return wxID_CANCEL;
    }
    return wxID_CANCEL;
}

