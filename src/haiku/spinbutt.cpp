/////////////////////////////////////////////////////////////////////////////
// Native Haiku spin arrows used by wxSpinButton and generic spin controls.
// Licence: wxWindows licence
/////////////////////////////////////////////////////////////////////////////
#include "wx/wxprec.h"
#if wxUSE_SPINBTN
#include "wx/spinbutt.h"
#include "wx/haiku/bspin.h"
#include <Autolock.h>
#include <Window.h>
#include <memory>

class wxHaikuSpinLock
{
public:
    explicit wxHaikuSpinLock(SpinButton* view) : m_view(view),
        m_lock(view && view->Looper() ? new BAutolock(view->Looper()) : nullptr) {}
    bool IsOk() const { return m_view && (!m_lock || m_lock->IsLocked()); }
private:
    SpinButton* m_view;
    std::unique_ptr<BAutolock> m_lock;
};

class wxHaikuSpinnerImpl : public SpinButton
{
public:
    wxHaikuSpinnerImpl(BRect frame, const char* name, wxSpinButton* handler)
        : SpinButton(frame,name), m_handler(handler) {}
    status_t Invoke(BMessage* = nullptr) override
    {
        wxSpinEvent event(wxEVT_SPIN,m_handler->GetId());
        event.SetEventObject(m_handler);
        event.SetPosition(Value());
        m_handler->HandleWindowEvent(event);
        return B_OK;
    }
protected:
    bool NotifyValueChanging(int32 value, bool up) override
    {
        wxSpinEvent event(up ? wxEVT_SPIN_UP : wxEVT_SPIN_DOWN,m_handler->GetId());
        event.SetEventObject(m_handler);
        event.SetPosition(value);
        m_handler->HandleWindowEvent(event);
        return event.IsAllowed();
    }
private:
    wxSpinButton* m_handler;
};

wxSpinButton::wxSpinButton(wxWindow* parent,wxWindowID id,const wxPoint& pos,
                         const wxSize& size,long style,const wxString& name)
{
    Create(parent,id,pos,size,style,name);
}
bool wxSpinButton::Create(wxWindow* parent,wxWindowID id,const wxPoint& pos,
                         const wxSize& size,long style,const wxString& name)
{
    const wxPoint p(pos.x == wxDefaultCoord ? 0 : pos.x,
                    pos.y == wxDefaultCoord ? 0 : pos.y);
    const wxSize s(size.x == wxDefaultCoord ? 20 : size.x,
                   size.y == wxDefaultCoord ? 24 : size.y);
    m_bSpinner=new wxHaikuSpinnerImpl(BRect(p.x,p.y,p.x+s.x-1,p.y+s.y-1),
                                      name.utf8_str(),this);
    m_bSpinner->SetWrap((style & wxSP_WRAP) != 0);
    HaikuAdoptView(m_bSpinner,nullptr,m_bSpinner);
    return wxSpinButtonBase::Create(parent,id,pos,size,style,wxDefaultValidator,name);
}
int wxSpinButton::GetValue() const
{
    wxHaikuSpinLock lock(m_bSpinner);
    return lock.IsOk() ? m_bSpinner->Value() : 0;
}
void wxSpinButton::SetValue(int value)
{
    wxHaikuSpinLock lock(m_bSpinner);
    if (lock.IsOk()) m_bSpinner->SetValue(value);
}
void wxSpinButton::SetRange(int min,int max)
{
    wxCHECK_RET(min <= max,"invalid spin range");
    wxHaikuSpinLock lock(m_bSpinner);
    if (!lock.IsOk()) return;
    wxSpinButtonBase::SetRange(min,max);
    m_bSpinner->SetRange(min,max);
}
void wxSpinButton::SetIncrement(int value)
{
    wxCHECK_RET(value > 0,"spin increment must be positive");
    wxHaikuSpinLock lock(m_bSpinner);
    if (!lock.IsOk()) return;
    m_increment=value;
    m_bSpinner->SetStep(value);
}
int wxSpinButton::GetIncrement() const { return m_increment; }
SpinButton* wxSpinButton::GetBSpinner() const { return m_bSpinner; }
void* wxSpinButton::GetHandle1() const { return m_bSpinner; }
#endif
