/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/colordlg.cpp
// Purpose:     wxColourDialog implementation for Haiku using BColorControl
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#if wxUSE_COLOURDLG

#include "wx/colordlg.h"
#include "wx/button.h"
#include "wx/panel.h"
#include "wx/sizer.h"
#include "wx/settings.h"

#include <Window.h>
#include <View.h>
#include <ColorControl.h>
#include <Message.h>

wxIMPLEMENT_DYNAMIC_CLASS(wxColourDialog, wxDialog);

enum
{
    ID_COLOR_CHANGED = 1001
};

// ----------------------------------------------------------------------------
// Native control wrapper
// ----------------------------------------------------------------------------

class wxHaikuColourControl : public BColorControl
{
public:
    wxHaikuColourControl(BPoint pt, wxColourDialog* owner)
        : BColorControl(pt,
                        B_CELLS_32x8,
                        7.0f,
                        "color_control",
                        new BMessage(ID_COLOR_CHANGED)),
          m_owner(owner)
    {
    }

    void MessageReceived(BMessage* msg) override
    {
        if ( msg && msg->what == ID_COLOR_CHANGED )
        {
            if ( m_owner )
                m_owner->OnColorChanged();
            return;
        }

        BColorControl::MessageReceived(msg);
    }

private:
    wxColourDialog* m_owner;
};

// ----------------------------------------------------------------------------
// Events
// ----------------------------------------------------------------------------

wxBEGIN_EVENT_TABLE(wxColourDialog, wxDialog)
    EVT_BUTTON(wxID_OK,     wxColourDialog::OnOK)
    EVT_BUTTON(wxID_CANCEL, wxColourDialog::OnCancel)
    EVT_INIT_DIALOG(        wxColourDialog::OnInitDialog)
wxEND_EVENT_TABLE()

// ----------------------------------------------------------------------------
// Local helpers
// ----------------------------------------------------------------------------

namespace
{

wxColour GetSafeDefaultColour()
{
    wxColour col = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
    if ( !col.IsOk() )
        col = wxColour(0, 0, 0);
    return col;
}

} // namespace

// ----------------------------------------------------------------------------
// wxColourDialog
// ----------------------------------------------------------------------------

bool wxColourDialog::Create(wxWindow* parent, const wxColourData* data)
{
    // IMPORTANT:
    // Do not use:
    //   m_data = *data;
    //   m_data = wxColourData();
    // Both can hit wxColourData::operator= on Haiku and currently crash.

    m_data.SetChooseFull(false);
    m_data.SetColour(GetSafeDefaultColour());

    if ( data )
    {
        m_data.SetChooseFull(data->GetChooseFull());

        const wxColour src = data->GetColour();
        if ( src.IsOk() )
        {
            // Rebuild from channels instead of copying wxColour internals.
            m_data.SetColour(wxColour(src.Red(),
                                      src.Green(),
                                      src.Blue(),
                                      src.Alpha()));
        }

        // TEMPORARY:
        // Do not copy custom colours here. On the current Haiku build this
        // can still trigger wxColour refcount/assignment crashes.
    }

    if ( !wxDialog::Create(parent,
                           wxID_ANY,
                           "Choose Color",
                           wxDefaultPosition,
                           wxDefaultSize,
                           wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER,
                           "colourDialog") )
    {
        return false;
    }

    m_bColorControl = nullptr;
    m_previewHost = nullptr;
    m_btnOK = nullptr;
    m_btnCancel = nullptr;

    wxBoxSizer* const topSizer = new wxBoxSizer(wxVERTICAL);

    m_previewHost = new wxPanel(this, wxID_ANY);
    m_previewHost->SetMinSize(wxSize(340, 250));
    m_previewHost->SetBackgroundColour(
        wxSystemSettings::GetColour(wxSYS_COLOUR_3DFACE));
    topSizer->Add(m_previewHost, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);

    wxBoxSizer* const buttonRow = new wxBoxSizer(wxHORIZONTAL);

    m_btnOK = new wxButton(this, wxID_OK, "OK");
    m_btnCancel = new wxButton(this, wxID_CANCEL, "Cancel");

    buttonRow->AddStretchSpacer(1);
    buttonRow->Add(m_btnOK, 0, wxRIGHT, 8);
    buttonRow->Add(m_btnCancel, 0, wxRIGHT, 0);

    topSizer->Add(buttonRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, 12);

    SetSizer(topSizer);

    SetEscapeId(wxID_CANCEL);
    if ( m_btnOK )
        SetDefaultItem(m_btnOK);

    BView* parentView = static_cast<BView*>(m_previewHost->GetHandle());
    if ( !parentView )
        return false;

    BWindow* win = parentView->Window();
    if ( !win || !win->LockLooper() )
        return false;

    m_bColorControl = new wxHaikuColourControl(BPoint(0.0f, 0.0f), this);

    float prefW = 240.0f;
    float prefH = 180.0f;

    if ( m_bColorControl )
    {
        parentView->AddChild(m_bColorControl);
        // BColorControl invokes its message target, not necessarily itself.
        // Targeting the wrapper preserves wxColourData when the user picks a
        // colour.
        m_bColorControl->SetTarget(m_bColorControl);

        m_bColorControl->GetPreferredSize(&prefW, &prefH);

        if ( prefW <= 0.0f ) prefW = 240.0f;
        if ( prefH <= 0.0f ) prefH = 180.0f;

        m_bColorControl->ResizeTo(prefW, prefH);

        BRect hostBounds = parentView->Bounds();
        const float x = std::max(8.0f, (hostBounds.Width()  - prefW) / 2.0f);
        const float y = std::max(8.0f, (hostBounds.Height() - prefH) / 2.0f);

        m_bColorControl->MoveTo(x, y);
        m_bColorControl->Show();

        SyncNativeColorFromData();
    }

    win->UnlockLooper();

    if ( !m_bColorControl )
        return false;

    const int buttonH =
        std::max(m_btnOK ? m_btnOK->GetBestSize().y : 30,
                 m_btnCancel ? m_btnCancel->GetBestSize().y : 30);

    const int buttonW =
        (m_btnOK ? m_btnOK->GetBestSize().x : 80) +
        (m_btnCancel ? m_btnCancel->GetBestSize().x : 80) + 40;

    const int clientW = std::max(440, std::max((int)prefW + 40, buttonW + 24));
    const int clientH = std::max(390, (int)prefH + buttonH + 80);

    // wxDialog::Show() calls Fit(), which uses the sizer's best/minimum size
    // and can otherwise shrink the panel to its initial native size.  Put the
    // palette requirement on the top-level sizer itself, not only on its
    // child panel.
    topSizer->SetMinSize(clientW, clientH);
    m_previewHost->SetMinSize(wxSize((int)prefW + 20, (int)prefH + 20));
    SetClientSize(clientW, clientH);
    SetMinClientSize(wxSize(clientW, clientH));
    SetMinSize(wxSize(clientW, clientH));

    Layout();
    SendSizeEvent();

    // The panel has its final native bounds only after wx's sizers have run.
    // The previous implementation attached the BColorControl while the host
    // was still at its initial tiny bounds and never positioned it again.
    if ( m_bColorControl )
    {
        BView* const host = static_cast<BView*>(m_previewHost->GetHandle());
        BWindow* const hostWindow = host ? host->Window() : nullptr;
        if ( hostWindow && hostWindow->LockLooper() )
        {
            const BRect bounds = host->Bounds();
            const float width = std::min(prefW, std::max(1.0f, bounds.Width() - 16.0f));
            const float height = std::min(prefH, std::max(1.0f, bounds.Height() - 16.0f));
            m_bColorControl->ResizeTo(width, height);
            m_bColorControl->MoveTo(
                std::max(8.0f, (bounds.Width() - width) / 2.0f),
                std::max(8.0f, (bounds.Height() - height) / 2.0f));
            m_bColorControl->Invalidate();
            host->Invalidate();
            hostWindow->UnlockLooper();
        }
    }

    CentreOnParent();

    return true;
}
/*
wxColourDialog::~wxColourDialog()
{
    if ( m_bColorControl )
    {
        BView* parent = m_bColorControl->Parent();
        BWindow* win = parent ? parent->Window() : nullptr;

        if ( win && win->LockLooper() )
        {
            if ( parent )
                parent->RemoveChild(m_bColorControl);
            win->UnlockLooper();
        }

        delete m_bColorControl;
        m_bColorControl = nullptr;
    }
}
*/

wxColourDialog::~wxColourDialog()
{
    if ( m_bColorControl )
    {
        BColorControl* const control = m_bColorControl;
        if ( BLooper* looper = m_bColorControl->Looper() )
        {
            if ( looper->Lock() )
            {
                control->RemoveSelf();
                looper->Unlock();
            }
        }

        // RemoveSelf() only detaches a BView; it does not destroy it.
        delete control;
        m_bColorControl = nullptr;
    }
}

int wxColourDialog::ShowModal()
{
    return wxDialog::ShowModal();
}

wxColourData& wxColourDialog::GetColourData()
{
    SyncDataFromNative();
    return m_data;
}

void wxColourDialog::OnColorChanged()
{
    SyncDataFromNative();
}

void wxColourDialog::OnOK(wxCommandEvent& WXUNUSED(event))
{
    SyncDataFromNative();
    EndModal(wxID_OK);
}

void wxColourDialog::OnCancel(wxCommandEvent& WXUNUSED(event))
{
    EndModal(wxID_CANCEL);
}

void wxColourDialog::OnInitDialog(wxInitDialogEvent& event)
{
    event.Skip();

    SyncNativeColorFromData();

    if ( m_btnOK )
        m_btnOK->SetFocus();
}

void wxColourDialog::SyncNativeColorFromData()
{
    if ( !m_bColorControl )
        return;

    BView* parent = m_bColorControl->Parent();
    BWindow* win = parent ? parent->Window() : nullptr;
    if ( !win || !win->LockLooper() )
        return;

    wxColour col = m_data.GetColour();
    if ( !col.IsOk() )
    {
        col = GetSafeDefaultColour();
        m_data.SetColour(col);
    }

    const rgb_color hc =
    {
        (uint8)col.Red(),
        (uint8)col.Green(),
        (uint8)col.Blue(),
        255
    };

    m_bColorControl->SetValue(hc);
    win->UnlockLooper();
}

/*
void wxColourDialog::SyncDataFromNative()
{
    if ( !m_bColorControl )
        return;

    BView* parent = m_bColorControl->Parent();
    BWindow* win = parent ? parent->Window() : nullptr;
    if ( !win || !win->LockLooper() )
        return;

    const rgb_color col = m_bColorControl->ValueAsColor();
    win->UnlockLooper();

    m_data.SetColour(wxColour(col.red, col.green, col.blue));
}
*/
void wxColourDialog::SyncDataFromNative()
{
    if ( !m_bColorControl )
        return;

    // Use the control's own looper directly for the lock
    if ( BLooper* looper = m_bColorControl->Looper() )
    {
        if ( looper->Lock() )
        {
            const rgb_color col = m_bColorControl->ValueAsColor();
            looper->Unlock();
            m_data.SetColour(wxColour(col.red, col.green, col.blue));
        }
    }
}


#endif // wxUSE_COLOURDLG
