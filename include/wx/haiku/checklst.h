#ifndef _WX_HAIKU_CHECKLST_H_
#define _WX_HAIKU_CHECKLST_H_


class BListView;
#include <map>

class WXDLLIMPEXP_CORE wxCheckListBox : public wxCheckListBoxBase
{
public:
    wxCheckListBox() = default;

    wxCheckListBox(wxWindow *parent, wxWindowID id,
            const wxPoint& pos = wxDefaultPosition,
            const wxSize& size = wxDefaultSize,
            int nStrings = 0,
            const wxString *choices = nullptr,
            long style = 0,
            const wxValidator& validator = wxDefaultValidator,
            const wxString& name = wxASCII_STR(wxListBoxNameStr));
    wxCheckListBox(wxWindow *parent, wxWindowID id,
            const wxPoint& pos,
            const wxSize& size,
            const wxArrayString& choices,
            long style = 0,
            const wxValidator& validator = wxDefaultValidator,
            const wxString& name = wxASCII_STR(wxListBoxNameStr));


    virtual ~wxCheckListBox();

    bool Create(wxWindow *parent, wxWindowID id,
                  const wxPoint& pos = wxDefaultPosition,
                  const wxSize& size = wxDefaultSize,
                  int n = 0, const wxString choices[] = nullptr,
                  long style = 0,
                  const wxValidator& validator = wxDefaultValidator,
                  const wxString& name = wxASCII_STR(wxListBoxNameStr));
    bool Create(wxWindow *parent, wxWindowID id,
                  const wxPoint& pos,
                  const wxSize& size,
                  const wxArrayString& choices,
                  long style = 0,
                  const wxValidator& validator = wxDefaultValidator,
                  const wxString& name = wxASCII_STR(wxListBoxNameStr));

    // Check list box specific methods
    virtual bool IsChecked(unsigned int item) const override;
    virtual void Check(unsigned int item, bool check = true) override;
  // #if wxUSE_DRAG_AND_DROP
  //  virtual void SetDropTarget(wxDropTargetBase* dropTarget) override;
//#endif
  //  virtual bool DoPopupMenu(wxMenu* menu, int x, int y) override;

    // Haiku-specific event handling
    bool HaikuHandleSelection(BView* view, BMessage* msg);

    BListView* GetBListView() const;
private:
    BListView* m_nativeListView = nullptr;
    std::map<unsigned int, bool> m_checked;

    wxDECLARE_DYNAMIC_CLASS(wxCheckListBox);
};

#endif // _WX_HAIKU_CHECKLST_H_
