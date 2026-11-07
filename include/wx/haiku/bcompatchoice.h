#ifndef _HAIKU_BCOMPATCHOICE_H_
#define _HAIKU_BCOMPATCHOICE_H_

#include <InterfaceKit.h>
#include <SupportKit.h>
#include <AppKit.h>

class BCompatChoice : public BControl
{
public:
    BCompatChoice(BRect frame, const char* name, const char* label, BMessage* message,
                  int32 resizingMode = B_FOLLOW_LEFT | B_FOLLOW_TOP,
                  uint32 flags = B_WILL_DRAW | B_NAVIGABLE);

    BCompatChoice(const char* name, const char* label, BMessage* message,
                  uint32 flags = B_WILL_DRAW | B_NAVIGABLE);

    virtual ~BCompatChoice();

    virtual void Draw(BRect updateRect) override;
    virtual void AttachedToWindow() override;
    virtual void MessageReceived(BMessage* message) override;
    virtual void FrameMoved(BPoint new_position) override;
    virtual void FrameResized(float new_width, float new_height) override;
    virtual BSize MinSize() override;
    virtual BSize MaxSize() override;
    virtual BSize PreferredSize() override;

    // Item management
    int32 CountItems() const;
    void AddItem(const char* label);
    void AddItem(const BString& label);
    void InsertItem(int32 index, const char* label);
    void InsertItem(int32 index, const BString& label);
    void RemoveItem(int32 index);
    void RemoveAllItems();
    const char* ItemAt(int32 index) const;
    BString ItemAtString(int32 index) const;
    int32 FindItem(const char* label) const;
    int32 FindItem(const BString& label) const;

    // Selection
    void Select(int32 index);
    int32 Selection() const;

    // Enable/Disable an item
    void EnableItem(int32 index, bool enable = true);
    void DisableItem(int32 index);
    bool IsItemEnabled(int32 index) const;

private:
    BPopUpMenu* fPopUpMenu;
    BMenuField* fMenuField;
    BStringList fItemList;
    int32 fSelectedIndex;
    bool fAttached;
    BList fItemEnabled; // Stores enabled item indices

    void _BuildMenu();
    void _SelectItem(int32 index);
    void _SetEnabledState(int32 index, bool enabled);

    static const uint32 kMenuSelected = 'selc'; // Custom message for menu selection
};

#endif // _HAIKU_BCOMPATCHOICE_H_
