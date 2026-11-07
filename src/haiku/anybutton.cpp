/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/anybutton.cpp
// Purpose:     wxAnyButton class for Haiku
// Author:      wxWidgets Team (Haiku port)
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"
#include "control_lock.h"

#ifdef wxHAS_ANY_BUTTON

#ifndef WX_PRECOMP
    #include "wx/anybutton.h"
    #include "wx/app.h"
    #include "wx/bitmap.h"
    #include "wx/window.h"
#endif

#include <Button.h>
#include <View.h>
#include <Message.h>

#include "wx/haiku/private.h"
#include "wx/haiku/window.h"
#include "wx/haiku/bitmap.h"

// ============================================================================
// wxHaikuButtonImpl: thin wrapper around Haiku BButton
// ============================================================================
class wxHaikuButtonImpl : public BButton
{
public:
    wxHaikuButtonImpl(wxAnyButton* handler,
                      const char* name = "wxButton")
        : BButton(BRect(0, 0, 80, 25),
                  name,
                  "",
                  new BMessage('wxbt'),
                  B_FOLLOW_LEFT | B_FOLLOW_TOP,
                  B_WILL_DRAW | B_NAVIGABLE),
          m_handler(handler)
    {
        SetTarget(this);
    }

    void AttachedToWindow() override
    {
        BButton::AttachedToWindow();
        SetTarget(this);
    }

    void SetLabelText(const wxString& label)
    {
        SetLabel(label.utf8_str());
    }

    wxString GetLabelText() const
    {
        return wxString::FromUTF8(Label());
    }

    void SetEnabledState(bool enable)
    {
        SetEnabled(enable);
    }

    void MessageReceived(BMessage* msg) override
    {
        if (msg && msg->what == 'wxbt' && m_handler)
        {
            wxCommandEvent event(m_handler->HaikuGetEventType(),
                                 m_handler->GetId());
            event.SetEventObject(m_handler);
            m_handler->GetEventHandler()->ProcessEvent(event);
            return;
        }

        BButton::MessageReceived(msg);
    }

private:
    wxAnyButton* m_handler;
};

// ============================================================================
// wxAnyButton implementation
// ============================================================================
wxAnyButton::wxAnyButton()
    : m_impl(nullptr)
{
}

wxAnyButton::~wxAnyButton()
{
    // Native lifetime should be managed through wx/Haiku adopted-view ownership.
    m_impl = nullptr;
}

void wxAnyButton::HaikuCreate(wxWindowHaiku* parent)
{
    wxCHECK_RET(!m_impl, "wxAnyButton already created");
    wxCHECK_RET(parent, "Parent window must not be null");

    m_impl = new wxHaikuButtonImpl(this);

    // Same pattern as wxButton: create native view first, adopt it,
    // let wx/window glue handle the actual parenting/ownership.
    HaikuAdoptView(m_impl, nullptr, m_impl);
}

void wxAnyButton::SetLabel(const wxString& label)
{
    wxHaikuControlLock lock(m_impl);
    if (!lock.IsOk())
        return;

    if (m_impl)
        m_impl->SetLabelText(label);

    wxAnyButtonBase::SetLabel(label);
}

wxString wxAnyButton::GetLabel() const
{
    wxHaikuControlLock lock(m_impl);
    if (!lock.IsOk())
        return wxString();

    return m_impl ? m_impl->GetLabelText()
                  : wxAnyButtonBase::GetLabel();
}

bool wxAnyButton::Enable(bool enable)
{
    wxHaikuControlLock lock(m_impl);
    if (!lock.IsOk())
        return false;

    if (m_impl)
        m_impl->SetEnabledState(enable);

    return wxWindow::Enable(enable);
}

BButton* wxAnyButton::GetBButton() const
{
    return m_impl;
}

// ============================================================================
// Bitmaps (Haiku limitation: only normal state supported)
// ============================================================================
wxBitmap wxAnyButton::DoGetBitmap(State state) const
{
    return (state < State_Max)
           ? m_bitmaps[state].GetBitmap(wxDefaultSize)
           : wxNullBitmap;
}

void wxAnyButton::DoSetBitmap(const wxBitmapBundle& bitmap, State which)
{
    wxCHECK_RET(which < State_Max, "Invalid state");

    m_bitmaps[which] = bitmap;

    if (which == State_Normal && bitmap.IsOk())
        HaikuUpdateBitmap();
}

void wxAnyButton::HaikuUpdateBitmap()
{
    wxHaikuControlLock lock(m_impl);
    if (!lock.IsOk())
        return;

    if (!m_impl)
        return;

    const wxBitmapBundle& bundle = m_bitmaps[State_Normal];
    if (!bundle.IsOk())
        return;

    wxBitmap bmp = bundle.GetBitmap(wxDefaultSize);
    if (!bmp.IsOk())
        return;

    BBitmap* nativeBmp = bmp.GetBBitmap();
    if (nativeBmp)
    {
        m_impl->SetIcon(nativeBmp);
        InvalidateBestSize();
    }
}

#endif // wxHAS_ANY_BUTTON
