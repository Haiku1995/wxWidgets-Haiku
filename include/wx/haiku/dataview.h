/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/dataview.h
// Purpose:     wxDataViewCtrl declaration for Haiku
// Author:      wxWidgets team
// Copyright:   (c) 2025 wxWidgets team
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_DATAVIEW_H_
#define _WX_HAIKU_DATAVIEW_H_

//#include "wx/dataview.h"

// ============================================================================
// wxDataViewColumn
// ============================================================================

class WXDLLIMPEXP_ADV wxDataViewColumn : public wxDataViewColumnBase
{
public:
    wxDataViewColumn(const wxString& title,
                     wxDataViewRenderer* renderer,
                     unsigned int model_column,
                     int width = wxDVC_DEFAULT_WIDTH,
                     wxAlignment align = wxALIGN_CENTER,
                     int flags = wxDATAVIEW_COL_RESIZABLE);

    wxDataViewColumn(const wxBitmapBundle& bitmap,
                     wxDataViewRenderer* renderer,
                     unsigned int model_column,
                     int width = wxDVC_DEFAULT_WIDTH,
                     wxAlignment align = wxALIGN_CENTER,
                     int flags = wxDATAVIEW_COL_RESIZABLE);

    // Setters
    virtual void SetTitle(const wxString& title) override;
    virtual void SetBitmap(const wxBitmapBundle& bitmap) override;
    virtual void SetOwner(wxDataViewCtrl* owner) override;
    virtual void SetAlignment(wxAlignment align) override;
    virtual void SetSortable(bool sortable) override;
    virtual void SetSortOrder(bool ascending) override;
    virtual void SetAsSortKey(bool sort = true) override;
    virtual void SetResizeable(bool resizeable) override;
    virtual void SetHidden(bool hidden) override;
    virtual void SetMinWidth(int minWidth) override;
    virtual void SetWidth(int width) override;
    virtual void SetReorderable(bool reorderable) override;
    virtual void SetFlags(int flags) override;

    // Getters
    virtual wxString GetTitle() const override;
    virtual wxAlignment GetAlignment() const override;
    virtual bool IsSortable() const override;
    virtual bool IsSortOrderAscending() const override;
    virtual bool IsSortKey() const override;
    virtual bool IsResizeable() const override;
    virtual bool IsHidden() const override;
    virtual int GetWidth() const override;
    virtual int GetMinWidth() const override;
    virtual bool IsReorderable() const override;
    virtual int GetFlags() const override;
};

// ============================================================================
// wxDataViewCtrl
// ============================================================================

class WXDLLIMPEXP_ADV wxDataViewCtrl : public wxDataViewCtrlBase
{
public:
    wxDataViewCtrl() = default;

    wxDataViewCtrl(wxWindow* parent,
                   wxWindowID id,
                   const wxPoint& pos = wxDefaultPosition,
                   const wxSize& size = wxDefaultSize,
                   long style = 0,
                   const wxValidator& validator = wxDefaultValidator);

    virtual ~wxDataViewCtrl();

    bool Create(wxWindow* parent,
                wxWindowID id,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = 0,
                const wxValidator& validator = wxDefaultValidator);

    // Model & columns
    virtual bool AssociateModel(wxDataViewModel* model) override;
    virtual bool PrependColumn(wxDataViewColumn* col) override;
    virtual bool AppendColumn(wxDataViewColumn* col) override;
    virtual bool InsertColumn(unsigned int pos, wxDataViewColumn* col) override;
    virtual unsigned int GetColumnCount() const override;
    virtual wxDataViewColumn* GetColumn(unsigned int pos) const override;
    virtual bool DeleteColumn(wxDataViewColumn* column) override;
    virtual bool ClearColumns() override;
    virtual int GetColumnPosition(const wxDataViewColumn* column) const override;
    virtual wxDataViewColumn* GetSortingColumn() const override;

    // Selection
    virtual wxDataViewItem GetSelection() const override;
    virtual int GetSelections(wxDataViewItemArray& sel) const override;
    virtual void SetSelections(const wxDataViewItemArray& sel) override;
    virtual void Select(const wxDataViewItem& item) override;
    virtual void Unselect(const wxDataViewItem& item) override;
    virtual bool IsSelected(const wxDataViewItem& item) const override;
    virtual void SelectAll() override;
    virtual void UnselectAll() override;

    // Visibility & hit testing
    virtual void EnsureVisible(const wxDataViewItem& item,
                               const wxDataViewColumn* column = nullptr) override;
    virtual void HitTest(const wxPoint& point,
                         wxDataViewItem& item,
                         wxDataViewColumn*& column) const override;
    virtual wxRect GetItemRect(const wxDataViewItem& item,
                               const wxDataViewColumn* column = nullptr) const override;

    // Expansion
    virtual void Collapse(const wxDataViewItem& item) override;
    virtual bool IsExpanded(const wxDataViewItem& item) const override;

    // Drag-and-drop
    virtual bool EnableDragSource(const wxDataFormat& format) override;
    virtual bool DoEnableDropTarget(const wxVector<wxDataFormat>& formats) override;

    static wxVisualAttributes
    GetClassDefaultAttributes(wxWindowVariant variant = wxWINDOW_VARIANT_NORMAL);

    wxWindow* GetMainWindow() { return this; }

    virtual void OnInternalIdle() override;

protected:
    virtual void DoSetExpanderColumn() override;
    virtual void DoSetIndent() override;
    virtual void DoExpand(const wxDataViewItem& item, bool expandChildren) override;

private:
    virtual wxDataViewItem DoGetCurrentItem() const override;
    virtual void DoSetCurrentItem(const wxDataViewItem& item) override;
};

#endif // _WX_HAIKU_DATAVIEW_H_
