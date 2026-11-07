// BEnhancedOutlineListView.h
// Enhanced BOutlineListView with editing, headers, and column support

#ifndef _B_ENHANCED_OUTLINE_LIST_VIEW_H_
#define _B_ENHANCED_OUTLINE_LIST_VIEW_H_

#include <OutlineListView.h>
#include <ListView.h>
#include <View.h>
#include <String.h>
#include <List.h>
#include <TextControl.h>
#include <Window.h>
#include <ScrollView.h>
#include <Message.h>
#include <Font.h>
#include <InterfaceKit.h>
#include <SupportKit.h>
#include <TranslationKit.h>
#include <ObjectList.h>  // Add this for BObjectList

#include <new>

class BEnhancedListItem;
class BEnhancedOutlineListView;

// Message constants for editing
const uint32 B_EDIT_STARTED = 'EDST';
const uint32 B_EDIT_ENDED   = 'EDEN';

// Enhanced list item with multi-column support
class BEnhancedListItem : public BListItem
{
public:
    BEnhancedListItem(const char* text = nullptr, uint32 outlineLevel = 0, bool expanded = false);
    virtual ~BEnhancedListItem();

    void SetText(int32 column, const char* text);
    const char* Text(int32 column) const;

    int32 ColumnCount() const;

    // Custom drawing
    virtual void DrawItem(BView* owner, BRect frame, bool complete = false);

    // User data
    void SetUserData(void* data) { fUserData = data; }
    void* GetUserData() const { return fUserData; }

private:
    BObjectList<BString> fColumnData;
    void* fUserData;
};

// Enhanced outline list view with headers, editing, and columns
class BEnhancedOutlineListView : public BOutlineListView
{
public:
    // Constructors
    BEnhancedOutlineListView(BRect frame,
                             const char* name,
                             list_view_type type = B_SINGLE_SELECTION_LIST,
                             uint32 resizingMode = B_FOLLOW_ALL_SIDES,
                             uint32 flags = 0);

    BEnhancedOutlineListView(const char* name,
                             list_view_type type = B_SINGLE_SELECTION_LIST,
                             uint32 flags = 0);

    BEnhancedOutlineListView(BMessage* archive);

    virtual ~BEnhancedOutlineListView();

    // Column management
    void SetColumnCount(int32 count);
    int32 ColumnCount() const { return fColumnCount; }

    void SetColumnWidth(int32 column, float width);
    float ColumnWidth(int32 column) const;

    void SetHeaderLabels(const char* labels[], int32 count);
    void SetHeaderLabel(int32 column, const char* label);

    // Item text access
    void SetItemText(BListItem* item, int32 column, const char* text);
    const char* ItemText(BListItem* item, int32 column) const;

    // Editing
    void EditItem(BListItem* item, int32 column);
    void EndEditItem(bool discardChanges = false);
    BTextControl* GetEditControl() const;

    // Header view
    BView* HeaderView() const { return fHeaderView; }

    // Override drawing
    virtual void Draw(BRect updateRect);
    virtual void DrawItem(BListItem* item, BRect itemRect, bool complete = false);
    virtual void DrawLatch(BRect itemRect, int32 level, bool collapsed, bool highlighted, bool misTracked);

    // Events
    virtual void MouseDown(BPoint where);
    virtual void KeyDown(const char* bytes, int32 numBytes);
    virtual void FrameResized(float newWidth, float newHeight);
    virtual void MessageReceived(BMessage* message);

    // Helpers
    BRect LatchRect(BRect itemRect, int32 level) const;
    void UpdateEditControlPosition(BListItem* item, int32 column);
    int32 ColumnAt(BPoint point) const;
    void LayoutHeaderView();

private:
    int32 fColumnCount;
    float* fColumnWidths;
    BObjectList<BString> fHeaderLabels;

    BView* fHeaderView;
    BTextControl* fEditControl;
    BListItem* fEditingItem;
    int32 fEditingColumn;
};

// Implementation section - this should be in a separate .cpp file
// But including here for completeness

// Constructor implementation
inline BEnhancedListItem::BEnhancedListItem(const char* text, uint32 outlineLevel, bool expanded)
    : BListItem(outlineLevel, expanded),
      fUserData(nullptr)
{
    // Initialize with the first column text if provided
    if (text) {
        BString* str = new BString(text);
        fColumnData.AddItem(str);
    }
}

// Helper method for drawing header (this should be part of BEnhancedOutlineListView implementation)
inline void BEnhancedOutlineListView::LayoutHeaderView()
{
    if (!fHeaderView) return;
    
    float xOffset = 0;
    for (int32 i = 0; i < fColumnCount; i++) {
        if (i < fHeaderLabels.CountItems()) {
            BString* label = fHeaderLabels.ItemAt(i);
            if (label) {
                fHeaderView->DrawString(label->String(), BPoint(xOffset + 2, 15));
            }
        }
        xOffset += fColumnWidths[i];
    }
}

inline void BEnhancedOutlineListView::UpdateEditControlPosition(BListItem* item, int32 column)
{
    if (!fEditControl || !item) return;

    BRect itemRect = ItemFrame(FullListIndexOf(item));
    float xOffset = 0;
    for (int32 i = 0; i < column; i++) {
        xOffset += fColumnWidths[i];
    }

    BRect editRect(itemRect.left + xOffset + 2,
                   itemRect.top,
                   itemRect.left + xOffset + fColumnWidths[column] - 2,
                   itemRect.bottom);

    fEditControl->MoveTo(editRect.left, editRect.top);
    fEditControl->ResizeTo(editRect.Width(), editRect.Height());
}

inline int32 BEnhancedOutlineListView::ColumnAt(BPoint point) const
{
    float xOffset = 0;
    for (int32 i = 0; i < fColumnCount; i++) {
        xOffset += fColumnWidths[i];
        if (point.x < xOffset)
            return i;
    }
    return fColumnCount - 1;
}

#endif // _B_ENHANCED_OUTLINE_LIST_VIEW_H_
