#ifndef _WX_HAIKU_CTRLSUB_H_
#define _WX_HAIKU_CTRLSUB_H_

#include "wx/control.h"
#include <vector>
#include <View.h>
#include <ListView.h>

class WXDLLIMPEXP_CORE wxControlWithItems : public wxControlWithItemsBase
{
public:
    wxControlWithItems();

    bool Create(wxWindow *parent, wxWindowID id,
                const wxPoint& pos, const wxSize& size,
                long style, const wxValidator& validator,
                const wxString& name);

    void AddItem(const wxString& item);
    void RemoveItem(unsigned int index);
    void ClearItems();
    unsigned int GetItemCount() const;

    // wxItemContainer pure virtual methods
    virtual unsigned int GetCount() const override { return GetItemCount(); }
    virtual wxString GetString(unsigned int n) const override;
    virtual void SetString(unsigned int n, const wxString& s) override;
    virtual void SetSelection(int n) override;
    virtual int GetSelection() const override;

protected:
    virtual wxSize DoGetBestSize() const override;
    virtual int DoInsertItems(const wxArrayStringsAdapter& items, unsigned int pos, 
                             void **clientData, wxClientDataType type) override;
    virtual void DoSetItemClientData(unsigned int n, void *clientData) override;
    virtual void* DoGetItemClientData(unsigned int n) const override;
    virtual void DoClear() override { ClearItems(); }
    virtual void DoDeleteOneItem(unsigned int pos) override { RemoveItem(pos); }

    BListView* m_itemList;
    std::vector<void*> m_itemClientData;

private:
    wxDECLARE_ABSTRACT_CLASS(wxControlWithItems);
};

#endif // _WX_HAIKU_CTRLSUB_H_
