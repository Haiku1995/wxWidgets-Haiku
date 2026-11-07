///////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/caret.h
// Purpose:     Haiku OS implementation of wxCaret
// Author:      Ported by User
// Created:     2026-01-26
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_CARET_H_
#define _WX_HAIKU_CARET_H_



class WXDLLIMPEXP_CORE wxCaret : public wxCaretBase
{
public:
    wxCaret() { Init(); }

    wxCaret(wxWindow *window, int width, int height)
    {
        Init();
        (void)Create(window, width, height);
    }

    wxCaret(wxWindowBase *window, const wxSize& size)
    {
        Init();
        (void)Create(window, size);
    }

    virtual void OnSetFocus() override;
    virtual void OnKillFocus() override;

protected:
    virtual void DoMove() override;
    virtual void DoShow() override;
    virtual void DoHide() override;
    virtual void DoSize() override;

private:
    void Init()
    {
        m_visible = false;
        m_window = nullptr;
    }

    bool m_visible;
    wxWindow* m_window;

    wxDECLARE_NO_COPY_CLASS(wxCaret);
};

#endif // _WX_HAIKU_CARET_H_
