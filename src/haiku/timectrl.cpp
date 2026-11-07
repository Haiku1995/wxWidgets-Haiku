/////////////////////////////////////////////////////////////////////////////
// Haiku time editor. Licence: wxWindows licence.
/////////////////////////////////////////////////////////////////////////////
#include "wx/wxprec.h"
#if wxUSE_TIMEPICKCTRL
#include "wx/timectrl.h"
#include "wx/dateevt.h"
#include <TextControl.h>
#include <Message.h>
#include <Autolock.h>
#include <memory>

wxIMPLEMENT_DYNAMIC_CLASS(wxTimePickerCtrl, wxControl);
class wxHaikuTimeText : public BTextControl
{
public:
    wxHaikuTimeText(wxTimePickerCtrl* handler,const char* name)
        : BTextControl(name,"","",new BMessage('TMCH')),m_handler(handler) {}
    void AttachedToWindow() override
    {
        BTextControl::AttachedToWindow();
        SetTarget(this);
    }
    void MessageReceived(BMessage* message) override
    {
        if (message->what == 'TMCH') m_handler->OnTextChanged();
        else BTextControl::MessageReceived(message);
    }
    void MakeFocus(bool focused) override
    {
        BTextControl::MakeFocus(focused);
        if (!focused) m_handler->OnTextChanged();
    }
private:
    wxTimePickerCtrl* m_handler;
};
class wxHaikuTimeLock
{
public:
    explicit wxHaikuTimeLock(BTextControl* view) : m_view(view),
        m_lock(view && view->Looper() ? new BAutolock(view->Looper()) : nullptr) {}
    bool IsOk() const { return m_view && (!m_lock || m_lock->IsLocked()); }
private:
    BTextControl* m_view;
    std::unique_ptr<BAutolock> m_lock;
};
bool wxTimePickerCtrl::Create(wxWindow* parent,wxWindowID id,const wxDateTime& dt,
                             const wxPoint& pos,const wxSize& size,long style,
                             const wxValidator& validator,const wxString& name)
{
    m_bText=new wxHaikuTimeText(this,name.utf8_str());
    HaikuAdoptView(m_bText,nullptr,m_bText);
    SetValue(dt.IsValid() ? dt : wxDateTime::Now());
    return wxControl::Create(parent,id,pos,size,style,validator,name);
}
void wxTimePickerCtrl::SetValue(const wxDateTime& dt)
{
    wxCHECK_RET(dt.IsValid(),"time picker requires a valid time");
    wxHaikuTimeLock lock(m_bText);
    if (!lock.IsOk()) return;
    m_value=dt;
    m_bText->SetText(dt.Format("%H:%M:%S").utf8_str());
}
wxDateTime wxTimePickerCtrl::GetValue() const
{
    wxHaikuTimeLock lock(m_bText);
    if (!lock.IsOk()) return m_value;
    wxDateTime parsed=m_value;
    if (parsed.ParseTime(wxString::FromUTF8(m_bText->Text()))) return parsed;
    return m_value;
}
void wxTimePickerCtrl::OnTextChanged()
{
    wxHaikuTimeLock lock(m_bText);
    if (!lock.IsOk()) return;
    wxDateTime value=m_value;
    if (!value.ParseTime(wxString::FromUTF8(m_bText->Text())))
    {
        m_bText->SetText(m_value.Format("%H:%M:%S").utf8_str());
        return;
    }
    if (value.GetHour()==m_value.GetHour() && value.GetMinute()==m_value.GetMinute()
        && value.GetSecond()==m_value.GetSecond()) return;
    m_value=value;
    wxDateEvent event(this,value,wxEVT_TIME_CHANGED);
    HandleWindowEvent(event);
}
#endif
