/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/choice.h
// Purpose:     wxChoice class for Haiku
// Author:      Based on Qt; adapted for Haiku
// Created:     [Date]
// Copyright:   (c) 2025 wxWidgets team
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_CHOICE_H_
#define _WX_HAIKU_CHOICE_H_

//#include "wx/choice.h"
//#include "wx/haiku/private.h"  // For BMenuField

class BMenuField;
class BPopUpMenu;

class WXDLLIMPEXP_CORE wxChoice : public wxChoiceBase
{
public:
    wxChoice() = default;
    ~wxChoice() override;

    wxChoice(wxWindow* parent,
             wxWindowID id,
             const wxPoint& pos = wxDefaultPosition,
             const wxSize& size = wxDefaultSize,
             int n = 0,
             const wxString choices[] = nullptr,
             long style = 0,
             const wxValidator& validator = wxDefaultValidator,
             const wxString& name = wxASCII_STR(wxChoiceNameStr));

    wxChoice(wxWindow* parent,
             wxWindowID id,
             const wxPoint& pos,
             const wxSize& size,
             const wxArrayString& choices,
             long style = 0,
             const wxValidator& validator = wxDefaultValidator,
             const wxString& name = wxASCII_STR(wxChoiceNameStr));

    bool Create(wxWindow* parent,
                wxWindowID id,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                int n = 0,
                const wxString choices[] = nullptr,
                long style = 0,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxChoiceNameStr)) ;

    bool Create(wxWindow* parent,
                wxWindowID id,
                const wxPoint& pos,
                const wxSize& size,
                const wxArrayString& choices,
                long style = 0,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxChoiceNameStr)) ;

    virtual wxSize DoGetBestSize() const override;

    virtual unsigned int GetCount() const override;
    virtual wxString GetString(unsigned int n) const override;
    virtual void SetString(unsigned int n, const wxString& s) override;

    virtual void SetSelection(int n) override;
    virtual int GetSelection() const override;

    BMenuField* GetBMenuField() const;

protected:
    virtual int DoInsertItems(const wxArrayStringsAdapter& items,
                              unsigned int pos,
                              void** clientData,
                              wxClientDataType type) override;
    virtual int DoInsertOneItem(const wxString& item, unsigned int pos) override;

    virtual void DoSetItemClientData(unsigned int n, void* clientData) override;
    virtual void* DoGetItemClientData(unsigned int n) const override;

    virtual void DoClear() override;
    virtual void DoDeleteOneItem(unsigned int pos) override;

private:
    
    BPopUpMenu* m_bMenu = nullptr;
    BMenuField* m_bMenuField = nullptr;
    std::vector<void*> m_clientData;

    wxDECLARE_DYNAMIC_CLASS(wxChoice);
};

#endif // _WX_HAIKU_CHOICE_H_
