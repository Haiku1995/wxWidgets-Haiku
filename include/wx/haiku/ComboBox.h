#ifndef WX_HAIKU_BCOMBOBOX_H
#define WX_HAIKU_BCOMBOBOX_H

#include <interface/TextControl.h>
#include <interface/PopUpMenu.h>
#include <interface/Button.h>

class BComboBox : public BTextControl {
public:
    BComboBox(BRect frame, const char* name, const char* text, BMessage* message);
    virtual ~BComboBox();

    void AddItem(const char* item);
    const char* Text() const;
    void SetText(const char* text);
    int32 CurrentSelection() const;

    // Added methods for wxComboBox compatibility
    void MakeEditable(bool editable);
    bool IsEditable() const;
    void ShowPopup();
    void HidePopup();

protected:
    virtual void AttachedToWindow() override;
    virtual void MessageReceived(BMessage* message) override;

private:
    BPopUpMenu* m_menu;
    BButton* m_dropButton;
    bool m_editable;
};

#endif // WX_HAIKU_BCOMBOBOX_H
