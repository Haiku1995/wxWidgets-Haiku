/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/dialog.cpp
// Purpose:     wxDialog class for Haiku
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"
#include "wx/haiku/private/dispatch.h"

#include "wx/dialog.h"
#include "wx/evtloop.h"
#include "wx/modalhook.h"
#include "wx/log.h"
#include "wx/app.h"
#include "wx/utils.h"
#include "wx/toplevel.h"

#include <Window.h>
#include <View.h>
#include <Alert.h>

wxDialog::wxDialog(wxWindow* parent,
                   wxWindowID id,
                   const wxString& title,
                   const wxPoint& pos,
                   const wxSize& size,
                   long style,
                   const wxString& name)
{
    Create(parent, id, title, pos, size, style, name);
}

wxDialog::~wxDialog()
{
    if ( m_isShowingModal )
        EndModal(wxID_CANCEL);
}

bool wxDialog::Create(wxWindow* parent,
                      wxWindowID id,
                      const wxString& title,
                      const wxPoint& pos,
                      const wxSize& size,
                      long style,
                      const wxString& name)
{
    SetExtraStyle(GetExtraStyle() | wxTOPLEVEL_EX_DIALOG);

    // All dialogs should have tab traversal enabled.
    style |= wxTAB_TRAVERSAL;

    // IMPORTANT:
    // wxTopLevelWindow::Create() already creates the native Haiku top-level
    // window. Do not create a second BWindow here.
    if ( !wxTopLevelWindow::Create(parent, id, title, pos, size, style, name) )
        return false;

    m_isShowingModal = false;
    m_returnCode = wxID_NONE;

    return true;
}

int wxDialog::ShowModal()
{
    WX_HOOK_MODAL_DIALOG();

    wxCHECK_MSG( GetHandle() != nullptr || IsTopLevel(), wxID_CANCEL, "Invalid dialog" );
    wxASSERT_MSG( !m_isShowingModal, "ShowModal() can't be called twice" );

    m_isShowingModal = true;
    m_returnCode = wxID_NONE;

    // Modal dialogs can run during OnInit(), before the main loop exists.
    // Publish their nested loop so idle handlers can create event-loop sources.
    wxGUIEventLoop modalLoop;
    wxEventLoopActivator activate(&modalLoop);

    Show(true);

    // FIX: Process events instead of sleeping the thread.
    // This allows buttons to be clicked and the 'm_isShowingModal' 
    // flag to be updated by EndModal().
    while ( m_isShowingModal )
    {
        // Now that evtloop.cpp is fixed, this won't crash!
        if ( wxTheApp )
        {
            wxHaikuGuiDispatchLock guard;
            wxHaikuServiceModalWindow();
            wxTheApp->ProcessPendingEvents();
            wxTheApp->ProcessIdle();
        }
        
        {
            // Let other window loopers deliver modal input while we wait.
            wxHaikuGuiDispatchPause pause;
            snooze(10000);
        }
    }
    {
        wxHaikuGuiDispatchLock guard;
        wxHaikuServiceModalWindow();
    }
    return m_returnCode;
}


void wxDialog::EndModal(int retCode)
{
    if ( !m_isShowingModal )
    {
        wxFAIL_MSG("either wxDialog::EndModal() called twice or ShowModal() wasn't called");
        return;
    }

    m_returnCode = retCode;
    m_isShowingModal = false;

    // Hide through wx/top-level path now that there is only one native window.
    wxTopLevelWindow::Show(false);
}

bool wxDialog::IsModal() const
{
    return m_isShowingModal;
}

bool wxDialog::Show(bool show)
{
    if ( show == IsShown() )
        return false;

    if ( !show && IsModal() )
    {
        EndModal(wxID_CANCEL);
        return true;
    }

    if ( show )
    {
        // Perform any layout adaptation before computing final size.
        if ( CanDoLayoutAdaptation() )
            DoLayoutAdaptation();

        // If the dialog uses sizers, make sure its size is derived from them
        // before showing the native window.
        if ( GetSizer() )
        {
            Layout();
            GetSizer()->SetSizeHints(this);
            Fit();
        }
    }


    const bool topRet = wxTopLevelWindow::Show(show);

    if ( show )
        InitDialog();

    return topRet;
}
