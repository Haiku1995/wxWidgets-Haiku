#ifndef _WX_HAIKU_COMBOBOX_H_
#define _WX_HAIKU_COMBOBOX_H_

#include "wx/control.h"
#include "wx/textentry.h"


class BMenuField;
class BPopUpMenu;

class WXDLLIMPEXP_CORE wxComboBox : public wxControl,
                                   public wxItemContainer,
                                   public wxTextEntry
{
public:
    wxComboBox();
    wxComboBox(wxWindow* parent,
               wxWindowID id,
               const wxString& value = wxEmptyString,
               const wxPoint& pos = wxDefaultPosition,
               const wxSize& size = wxDefaultSize,
               int n = 0, const wxString choices[] = nullptr,
               long style = 0,
               const wxValidator& validator = wxDefaultValidator,
               const wxString& name = wxASCII_STR(wxComboBoxNameStr));

    wxComboBox(wxWindow* parent,
               wxWindowID id,
               const wxString& value,
               const wxPoint& pos,
               const wxSize& size,
               const wxArrayString& choices,
               long style = 0,
               const wxValidator& validator = wxDefaultValidator,
               const wxString& name = wxASCII_STR(wxComboBoxNameStr));

    bool Create(wxWindow* parent,
                wxWindowID id,
                const wxString& value = wxEmptyString,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                int n = 0, const wxString choices[] = nullptr,
                long style = 0,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxComboBoxNameStr));

    bool Create(wxWindow* parent,
                wxWindowID id,
                const wxString& value,
                const wxPoint& pos,
                const wxSize& size,
                const wxArrayString& choices,
                long style = 0,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxComboBoxNameStr));

    virtual ~wxComboBox();

    // Resolve the window-level and item-level client-data overloads.
    using wxControl::GetClientData;
    using wxControl::SetClientData;
    using wxControl::GetClientObject;
    using wxControl::SetClientObject;
    using wxItemContainer::GetClientData;
    using wxItemContainer::SetClientData;
    using wxItemContainer::GetClientObject;
    using wxItemContainer::SetClientObject;

    // wxItemContainer
    unsigned int GetCount() const override;
    wxString GetString(unsigned int n) const override;
    void SetString(unsigned int n, const wxString& s) override;
    void SetSelection(int n) override;
    int GetSelection() const override;
    wxString GetStringSelection() const override;
    bool IsListEmpty() const { return wxItemContainer::IsEmpty(); }
    bool IsTextEmpty() const { return wxTextEntry::IsEmpty(); }
    bool IsSorted() const override;

    // wxTextEntry
    void SetValue(const wxString& value) override;
    void ChangeValue(const wxString& value) override;
    void AppendText(const wxString& value) override;
    void Replace(long from, long to, const wxString& value) override;
    void WriteText(const wxString& value) override;
    void SetInsertionPoint(long pos) override;
    long GetInsertionPoint() const override;
    void GetSelection(long* from, long* to) const override;
    void SetSelection(long from, long to) override;
    bool IsEditable() const override;
    void SetEditable(bool editable) override;
    void Clear() override;
    void Copy() override;
    void Cut() override;
    void Paste() override;
    void Undo() override;
    void Redo() override;
    bool CanUndo() const override;
    bool CanRedo() const override;
    const wxTextEntry* WXGetTextEntry() const override;
    wxWindow* GetEditableWindow() override;

    void Popup();
    void Dismiss();

protected:
    void DoEnable(bool enable) override;
    wxSize DoGetBestSize() const override;
    // wxItemContainer hooks
    void DoSetItemClientData(unsigned int n, void* clientData) override;
    void* DoGetItemClientData(unsigned int n) const override;
    int DoInsertOneItem(const wxString& item, unsigned int pos) override;
    void DoClear() override;
    void DoDeleteOneItem(unsigned int pos) override;
    int DoInsertItems(const wxArrayStringsAdapter& items,
                      unsigned int pos,
                      void** clientData,
                      wxClientDataType type) override;

    wxString DoGetValue() const override;
    void DoSetValue(const wxString& value, int flags) override;

private:
    BMenuField* m_bMenuField { nullptr };
    BPopUpMenu* m_bMenu { nullptr };
    wxString m_value;
    std::vector<void*> m_clientData;

    friend class wxHaikuComboBoxMenuField;

    wxDECLARE_DYNAMIC_CLASS(wxComboBox);
};

#endif // _WX_HAIKU_COMBOBOX_H_
