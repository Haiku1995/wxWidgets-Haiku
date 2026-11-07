// Name:        wx/haiku/BCompatTreeCtrl.h
// Author:      [Your Name]
// Copyright:   (c) 2024 [Your Copyright]
// License:     MIT License
/////////////////////////////////////////////////////////////////////////////

#ifndef _HAIKU_BCOMPAT_TREECTRL_H_
#define _HAIKU_BCOMPAT_TREECTRL_H_

#include <InterfaceKit.h>
#include <SupportKit.h>
#include <AppKit.h>

// Forward Declarations
class BBitmap;
class BHandler;

class BCompatTreeCtrl : public BView {
public:
    // Data structure for a tree item
    struct TreeItemData {
        BString label;
        BBitmap* icon;         // Normal icon
        int32 imageIndex;      // Index into the normal image list (or NO_IMAGE)
        BBitmap* stateIcon;    // State icon (e.g., checkbox)
        int32 stateImageIndex; // Index into the state image list (or NO_IMAGE)
        BList children;        // List of TreeItemData*
        bool expanded;
        TreeItemData* parent;  // Pointer to the parent item (nullptr for root)
        bool enabled;          // Item Enabled

        TreeItemData(const char* text) 
            : label(text), icon(nullptr), imageIndex(NO_IMAGE), 
              stateIcon(nullptr), stateImageIndex(NO_IMAGE), 
              expanded(false), parent(nullptr), enabled(true) {}
        ~TreeItemData();
    };

    // Constants
    static const int32 NO_IMAGE = -1; // Constant for default image
    enum ImageType {
        IMAGE_NORMAL = 0,    // Normal image
        IMAGE_STATE          // State image (e.g., selected or checkbox)
    };

    BCompatTreeCtrl(BRect frame, const char* name, uint32 resizingMode = B_FOLLOW_ALL, uint32 flags = B_WILL_DRAW | B_NAVIGABLE);
    virtual ~BCompatTreeCtrl();
float GetIndentSize() const { return kIndentSize; }
    // Item Management
    TreeItemData* AddRoot(const char* label, int32 image = NO_IMAGE, int32 stateImage = NO_IMAGE);
    TreeItemData* AppendItem(TreeItemData* parent, const char* label, int32 image = NO_IMAGE, int32 stateImage = NO_IMAGE);
    void DeleteItem(TreeItemData* item);
    void DeleteAllItems();

    // Expanding/Collapsing
    void Expand(TreeItemData* item);
    void Collapse(TreeItemData* item);
    bool IsExpanded(TreeItemData* item) const;

    // Selection
    void SelectItem(TreeItemData* item);
    TreeItemData* GetSelection() const;

    // Images
    void SetImageList(BList* imageList);           // List of normal images (BBitmap*)
    void SetStateImageList(BList* stateImageList); // List of state images (BBitmap*)
    void SetItemImage(TreeItemData* item, int32 image, int32 which = IMAGE_NORMAL); // IMAGE_NORMAL or IMAGE_STATE
    int32 GetItemImage(TreeItemData* item, int32 which = IMAGE_NORMAL) const;

    // Enable/Disable Item
    void EnableItem(TreeItemData* item, bool enable = true);
    bool IsItemEnabled(TreeItemData* item) const;

    // Events (send BMessages to the target)
    void SetTarget(BHandler* target);

    // Styles
    enum TreeCtrlStyle {
        TR_EDIT_LABELS   = 0x0001, // Allow label editing
        TR_NO_BUTTONS    = 0x0002, // No expand/collapse buttons
        TR_HAS_BUTTONS   = 0x0004, // Show expand/collapse buttons
        TR_NO_LINES      = 0x0008, // No connecting lines
        TR_LINES_AT_ROOT = 0x0010, // Lines at root nodes
        TR_HIDE_ROOT     = 0x0020, // Hide the root node
        TR_SINGLE        = 0x0040, // Single selection (default)
        TR_MULTIPLE      = 0x0080  // Multiple selection (not implemented)
    };
    void SetStyle(uint32 style);
    uint32 GetStyle() const;

protected:
    virtual void Draw(BRect updateRect) override;
    virtual void MouseDown(BPoint point) override;
    virtual void KeyDown(const char* bytes, int32 numBytes) override;
    virtual void MessageReceived(BMessage* message) override; // For label editing

private:
    BList fRootItems;            // List of root TreeItemData*
    TreeItemData* fSelectedItem; // Current selected item
    BHandler* fTarget;           // Target for events
    BList* fImageList;           // List of normal images (BBitmap*)
    BList* fStateImageList;      // List of state images (BBitmap*)
    uint32 fStyle;               // Control styles

    // Helper methods
    void _DrawTree(BView* view, TreeItemData* item, BPoint position, float indent);
    BRect _GetItemRect(TreeItemData* item) const;
    TreeItemData* _HitTest(BPoint point) const;
    void _SendEvent(uint32 eventType, TreeItemData* item); // Send tree events

    // Constants for label edit messages
    static const uint32 kMsgBeginLabelEdit;
    static const uint32 kMsgEndLabelEdit;

    // Constants for drawing
    static const float kIndentSize;
    static const float kItemHeight;
};

#endif // _HAIKU_BCOMPAT_TREECTRL_H_
