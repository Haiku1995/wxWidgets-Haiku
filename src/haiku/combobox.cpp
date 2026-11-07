#include "wx/combobox.h"
#include "wx/haiku/private.h"
#include "wx/wxprec.h"
#include "control_sort.h"
#include <Clipboard.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Message.h>
#include <PopUpMenu.h>
#include <TextControl.h>
#include <TextView.h>
#include <Window.h>

namespace {
class ComboLock {
public:
  explicit ComboLock(BView *view)
      : m_looper(view ? view->Looper() : nullptr),
        m_locked(m_looper && m_looper->Lock()) {}
  ~ComboLock() {
    if (m_locked)
      m_looper->Unlock();
  }
  bool IsOk() const { return !m_looper || m_locked; }

private:
  BLooper *m_looper;
  bool m_locked;
};
int32 ByteOffset(BTextView *view, long pos) {
  const wxString text = wxString::FromUTF8(view->Text());
  const size_t n = pos < 0 ? text.length() : wxMin(size_t(pos), text.length());
  return int32(text.substr(0, n).utf8_str().length());
}
long CharOffset(BTextView *view, int32 pos) {
  return wxString::FromUTF8(view->Text(),
                            wxMax(0, wxMin(pos, view->TextLength())))
      .length();
}
} // namespace

class wxHaikuComboBoxMenuField : public BMenuField {
public:
  wxHaikuComboBoxMenuField(wxComboBox *peer, BPopUpMenu *menu, bool editable)
      : BMenuField(BRect(0, 0, 149, 25), "wxComboMenu", nullptr, menu),
        m_peer(peer) {
    SetDivider(0);
    if (editable) {
      editor = new BTextControl(BRect(0, 0, 119, 25), "wxComboText", nullptr,
                                "", nullptr);
      editor->SetDivider(0);
      editor->SetModificationMessage(new BMessage('CTXT'));
      editor->TextView()->SetDoesUndo(true);
    }
  }
  void SetDisplay(const wxString &value) {
    if (editor) {
      editor->SetModificationMessage(nullptr);
      editor->SetText(value.utf8_str());
      editor->SetModificationMessage(new BMessage('CTXT'));
    } else if (MenuItem()) {
      MenuItem()->SetLabel(value.utf8_str());
    }
  }
  void ClearMarks() {
    for (int32 i = 0; i < Menu()->CountItems(); ++i)
      Menu()->ItemAt(i)->SetMarked(false);
  }
  void MessageReceived(BMessage *message) override {
    if (!m_peer->IsEnabled())
      return;
    if (message->what == 'CTXT' && editor) {
      m_peer->m_value = wxString::FromUTF8(editor->Text());
      ClearMarks();
      auto *event = new wxCommandEvent(wxEVT_TEXT, m_peer->GetId());
      event->SetEventObject(m_peer);
      event->SetString(m_peer->m_value);
      m_peer->QueueEvent(event);
      return;
    }
    if (message->what == 'CMBX') {
      int64 id;
      if (message->FindInt64("wxItemId", &id) != B_OK)
        return;
      // Resolve a stable item identity, not an insertion-time index.
      for (int32 i = 0; i < Menu()->CountItems(); ++i) {
        BMenuItem *item = Menu()->ItemAt(i);
        int64 itemId;
        if (!item->Message() ||
            item->Message()->FindInt64("wxItemId", &itemId) != B_OK ||
            itemId != id)
          continue;
        m_peer->SetSelection(i);
        auto *event = new wxCommandEvent(wxEVT_COMBOBOX, m_peer->GetId());
        event->SetEventObject(m_peer);
        event->SetInt(i);
        event->SetString(m_peer->GetValue());
        if (m_peer->HasClientObjectData())
          event->SetClientObject(m_peer->wxItemContainer::GetClientObject(i));
        else if (m_peer->HasClientUntypedData())
          event->SetClientData(m_peer->wxItemContainer::GetClientData(i));
        auto *textEvent = new wxCommandEvent(wxEVT_TEXT, m_peer->GetId());
        textEvent->SetEventObject(m_peer);
        textEvent->SetString(m_peer->GetValue());
        m_peer->QueueEvent(event);
        m_peer->QueueEvent(textEvent);
        return;
      }
      return; // removed item: ignore queued selection
    }
    BMenuField::MessageReceived(message);
  }
  BTextControl *editor = nullptr; // owned by the containing native view
  int64 nextItemId = 0;

private:
  wxComboBox *m_peer;
};

class wxHaikuComboBoxView : public BView {
public:
  wxHaikuComboBoxView(wxHaikuComboBoxMenuField *field, BRect frame)
      : BView(frame, "wxComboBox", B_FOLLOW_NONE,
              B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE),
        m_field(field) {
    SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
    if (field->editor)
      AddChild(field->editor);
    AddChild(field);
    LayoutChildren();
  }
  void AllAttached() override {
    BView::AllAttached();
    m_field->Menu()->SetTargetForItems(m_field);
    if (m_field->editor)
      m_field->editor->SetTarget(m_field);
    LayoutChildren();
  }
  void FrameResized(float w, float h) override {
    BView::FrameResized(w, h);
    LayoutChildren();
  }
  void MakeFocus(bool focus = true) override {
    if (m_field->editor)
      m_field->editor->MakeFocus(focus);
    else
      m_field->MakeFocus(focus);
  }
  void GetPreferredSize(float *width, float *height) override {
    m_field->GetPreferredSize(width, height);
    if (width)
      *width = wxMax(*width, 149.0f);
    if (height)
      *height = wxMax(*height, 25.0f);
  }

private:
  void LayoutChildren() {
    const BRect bounds = Bounds();
    if (m_field->editor) {
      const float editorWidth = wxMax(0.0f, bounds.Width() - 28.0f);
      m_field->editor->MoveTo(0, 0);
      m_field->editor->ResizeTo(editorWidth, bounds.Height());
      m_field->MoveTo(editorWidth + 2, 0);
      m_field->ResizeTo(26, bounds.Height());
    } else {
      m_field->MoveTo(0, 0);
      m_field->ResizeTo(bounds.Width(), bounds.Height());
    }
  }
  wxHaikuComboBoxMenuField *m_field;
};

static wxHaikuComboBoxMenuField *Native(BMenuField *field) {
  return static_cast<wxHaikuComboBoxMenuField *>(field);
}
static BTextView *Editor(BMenuField *field) {
  return field && Native(field)->editor ? Native(field)->editor->TextView()
                                        : nullptr;
}

wxComboBox::wxComboBox() = default;
wxComboBox::~wxComboBox() {
  if (m_bMenu)
    wxItemContainer::Clear();
}
wxComboBox::wxComboBox(wxWindow *parent, wxWindowID id, const wxString &value,
                       const wxPoint &pos, const wxSize &size, int n,
                       const wxString choices[], long style,
                       const wxValidator &validator, const wxString &name) {
  Create(parent, id, value, pos, size, n, choices, style, validator, name);
}
wxComboBox::wxComboBox(wxWindow *parent, wxWindowID id, const wxString &value,
                       const wxPoint &pos, const wxSize &size,
                       const wxArrayString &choices, long style,
                       const wxValidator &validator, const wxString &name) {
  Create(parent, id, value, pos, size, choices, style, validator, name);
}
bool wxComboBox::Create(wxWindow *parent, wxWindowID id, const wxString &value,
                        const wxPoint &pos, const wxSize &size,
                        const wxArrayString &choices, long style,
                        const wxValidator &validator, const wxString &name) {
  return Create(parent, id, value, pos, size, choices.size(),
                choices.empty() ? nullptr : &choices[0], style, validator,
                name);
}
bool wxComboBox::Create(wxWindow *parent, wxWindowID id, const wxString &value,
                        const wxPoint &pos, const wxSize &size, int n,
                        const wxString choices[], long style,
                        const wxValidator &validator, const wxString &name) {
  wxCHECK_MSG(parent && n >= 0, false, "invalid combo box arguments");
  const bool editable = (style & wxCB_READONLY) == 0;
  m_bMenu = new BPopUpMenu("", true, !editable);
  m_bMenuField = new wxHaikuComboBoxMenuField(this, m_bMenu, editable);
  const int x = pos.x == wxDefaultCoord ? 0 : pos.x,
            y = pos.y == wxDefaultCoord ? 0 : pos.y;
  const int w = size.x == wxDefaultCoord ? 150 : wxMax(1, size.x),
            h = size.y == wxDefaultCoord ? 26 : wxMax(1, size.y);
  auto *host = new wxHaikuComboBoxView(Native(m_bMenuField),
                                       BRect(x, y, x + w - 1, y + h - 1));
  HaikuAdoptView(host);
  if (!wxControl::Create(parent, id, pos, size, style, validator, name))
    return false;
  for (int i = 0; i < n; ++i)
    Append(choices[i]);
  ChangeValue(value);
  return true;
}
unsigned int wxComboBox::GetCount() const {
  ComboLock lock(m_bMenuField);
  return lock.IsOk() && m_bMenu ? m_bMenu->CountItems() : 0;
}
wxString wxComboBox::GetString(unsigned int n) const {
  ComboLock lock(m_bMenuField);
  wxCHECK_MSG(lock.IsOk() && m_bMenu && n < GetCount(), wxString(),
              "invalid combo index");
  return wxString::FromUTF8(m_bMenu->ItemAt(n)->Label());
}
void wxComboBox::SetString(unsigned int n, const wxString &value) {
  ComboLock lock(m_bMenuField);
  wxCHECK_RET(lock.IsOk() && m_bMenu && n < GetCount(), "invalid combo index");
  const bool selected = GetSelection() == int(n);
  m_bMenu->ItemAt(n)->SetLabel(value.utf8_str());
  if (selected)
    SetSelection(n);
  InvalidateBestSize();
}
void wxComboBox::SetSelection(int n) {
  ComboLock lock(m_bMenuField);
  if (!lock.IsOk() || !m_bMenuField)
    return;
  wxCHECK_RET(n == wxNOT_FOUND || (n >= 0 && n < int(GetCount())),
              "invalid combo selection");
  Native(m_bMenuField)->ClearMarks();
  m_value = n == wxNOT_FOUND ? wxString() : GetString(n);
  if (n != wxNOT_FOUND)
    m_bMenu->ItemAt(n)->SetMarked(true);
  Native(m_bMenuField)->SetDisplay(m_value);
}
int wxComboBox::GetSelection() const {
  ComboLock lock(m_bMenuField);
  if (!lock.IsOk() || !m_bMenu)
    return wxNOT_FOUND;
  for (int32 i = 0; i < m_bMenu->CountItems(); ++i)
    if (m_bMenu->ItemAt(i)->IsMarked())
      return i;
  return wxNOT_FOUND;
}
wxString wxComboBox::GetStringSelection() const {
  const int n = GetSelection();
  return n == wxNOT_FOUND ? wxString() : GetString(n);
}
bool wxComboBox::IsSorted() const { return HasFlag(wxCB_SORT); }
wxString wxComboBox::DoGetValue() const {
  ComboLock lock(m_bMenuField);
  if (!lock.IsOk())
    return wxString();
  if (BTextView *editor = Editor(m_bMenuField))
    return wxString::FromUTF8(editor->Text());
  return m_value;
}
void wxComboBox::ChangeValue(const wxString &value) {
  wxTextEntryBase::ChangeValue(value);
}
void wxComboBox::SetValue(const wxString &value) {
  DoSetValue(value, SetValue_SendEvent);
}
void wxComboBox::DoSetValue(const wxString &value, int flags) {
  {
    ComboLock lock(m_bMenuField);
    if (!lock.IsOk() || !m_bMenuField)
      return;
    const bool readOnly = !Editor(m_bMenuField);
    const int index = FindString(value, !readOnly);
    if (readOnly && index == wxNOT_FOUND && !value.empty())
      return;
    // Preserve the caret and selection for an unchanged native value.
    const wxString displayValue = readOnly && index != wxNOT_FOUND ? GetString(index) : value;
    if (displayValue != DoGetValue()) {
      Native(m_bMenuField)->ClearMarks();
      if (index != wxNOT_FOUND)
        m_bMenu->ItemAt(index)->SetMarked(true);
      m_value = displayValue;
      Native(m_bMenuField)->SetDisplay(displayValue);
    }
  }
  if (flags & SetValue_SendEvent)
    SendTextUpdatedEventIfAllowed();
}
void wxComboBox::AppendText(const wxString &value) {
  SetInsertionPoint(-1);
  WriteText(value);
}
void wxComboBox::Replace(long from, long to, const wxString &value) {
  SetSelection(from, to);
  WriteText(value);
}
void wxComboBox::WriteText(const wxString &value) {
  ComboLock lock(m_bMenuField);
  BTextView *editor = Editor(m_bMenuField);
  if (!lock.IsOk() || !editor)
    return;
  auto *native = Native(m_bMenuField);
  native->editor->SetModificationMessage(nullptr);
  int32 from, to;
  editor->GetSelection(&from, &to);
  if (from != to)
    editor->Delete(from, to);
  const wxCharBuffer bytes = value.utf8_str();
  editor->Insert(from, bytes.data(), int32(bytes.length()));
  editor->Select(from + bytes.length(), from + bytes.length());
  native->editor->SetModificationMessage(new BMessage('CTXT'));
  native->ClearMarks();
  m_value = wxString::FromUTF8(editor->Text());
  SendTextUpdatedEventIfAllowed();
}
void wxComboBox::SetInsertionPoint(long pos) {
  SetSelection(pos == -1 ? GetLastPosition() : pos,
               pos == -1 ? GetLastPosition() : pos);
}
long wxComboBox::GetInsertionPoint() const {
  long from = 0, to = 0;
  GetSelection(&from, &to);
  return from;
}
void wxComboBox::GetSelection(long *from, long *to) const {
  ComboLock lock(m_bMenuField);
  int32 a = 0, b = 0;
  if (lock.IsOk())
    if (BTextView *editor = Editor(m_bMenuField)) {
      editor->GetSelection(&a, &b);
      a = CharOffset(editor, a);
      b = CharOffset(editor, b);
    }
  if (from)
    *from = a;
  if (to)
    *to = b;
}
void wxComboBox::SetSelection(long from, long to) {
  ComboLock lock(m_bMenuField);
  if (lock.IsOk())
    if (BTextView *editor = Editor(m_bMenuField)) {
      const int32 start = ByteOffset(editor, from == -1 ? 0 : from),
                  end = ByteOffset(editor, to);
      editor->Select(end, end);
      editor->Select(start, end);
    }
}
bool wxComboBox::IsEditable() const {
  ComboLock lock(m_bMenuField);
  auto *editor = Editor(m_bMenuField);
  return lock.IsOk() && editor && editor->IsEditable();
}
void wxComboBox::SetEditable(bool editable) {
  ComboLock lock(m_bMenuField);
  if (lock.IsOk())
    if (auto *editor = Editor(m_bMenuField))
      editor->MakeEditable(editable);
}
const wxTextEntry *wxComboBox::WXGetTextEntry() const { return this; }
wxWindow *wxComboBox::GetEditableWindow() { return this; }
void wxComboBox::Clear() {
  wxItemContainer::Clear();
  SetValue(wxString());
}
void wxComboBox::DoSetItemClientData(unsigned int n, void *data) {
  if (n < m_clientData.size())
    m_clientData[n] = data;
}
void *wxComboBox::DoGetItemClientData(unsigned int n) const {
  return n < m_clientData.size() ? m_clientData[n] : nullptr;
}
int wxComboBox::DoInsertOneItem(const wxString &value, unsigned int pos) {
  ComboLock lock(m_bMenuField);
  if (!lock.IsOk() || !m_bMenu)
    return wxNOT_FOUND;
  if (IsSorted()) {
    pos = 0;
    while (pos < GetCount() && wxHaikuCompareItemLabels(GetString(pos), value) <= 0)
      ++pos;
  }
  wxCHECK_MSG(pos <= GetCount(), wxNOT_FOUND, "invalid combo insertion index");
  auto *message = new BMessage('CMBX');
  message->AddInt64("wxItemId", ++Native(m_bMenuField)->nextItemId);
  auto *item = new BMenuItem(value.utf8_str(), message);
  if (!m_bMenu->AddItem(item, pos)) {
    delete item;
    return wxNOT_FOUND;
  }
  item->SetTarget(m_bMenuField);
  m_clientData.insert(m_clientData.begin() + pos, nullptr);
  InvalidateBestSize();
  return pos;
}
void wxComboBox::DoDeleteOneItem(unsigned int pos) {
  ComboLock lock(m_bMenuField);
  if (!lock.IsOk() || !m_bMenu || pos >= GetCount())
    return;
  const bool selected = GetSelection() == int(pos);
  delete m_bMenu->RemoveItem(int32(pos));
  m_clientData.erase(m_clientData.begin() + pos);
  if (selected)
    SetSelection(wxNOT_FOUND);
  InvalidateBestSize();
}
void wxComboBox::DoClear() {
  ComboLock lock(m_bMenuField);
  if (!lock.IsOk())
    return;
  if (m_bMenu)
    m_bMenu->RemoveItems(0, m_bMenu->CountItems(), true);
  m_clientData.clear();
  m_value.clear();
  if (m_bMenuField)
    Native(m_bMenuField)->SetDisplay(m_value);
  InvalidateBestSize();
}
int wxComboBox::DoInsertItems(const wxArrayStringsAdapter &items,
                              unsigned int pos, void **data,
                              wxClientDataType type) {
  return DoInsertItemsInLoop(items, pos, data, type);
}
void wxComboBox::DoEnable(bool enable) {
  wxControl::DoEnable(enable);
  ComboLock lock(m_bMenuField);
  if (!lock.IsOk() || !m_bMenuField)
    return;
  m_bMenuField->SetEnabled(enable);
  if (Native(m_bMenuField)->editor)
    Native(m_bMenuField)->editor->SetEnabled(enable);
}
wxSize wxComboBox::DoGetBestSize() const {
  ComboLock lock(m_bMenuField);
  float w = 149, h = 25;
  if (lock.IsOk() && GetHandle())
    GetHandle()->GetPreferredSize(&w, &h);
  return wxSize(int(w) + 1, int(h) + 1);
}
void wxComboBox::Popup() {
  BPoint where;
  {
    ComboLock lock(m_bMenuField);
    if (!lock.IsOk() || !m_bMenuField || !IsEnabled())
      return;
    where = m_bMenuField->Bounds().LeftBottom();
    m_bMenuField->ConvertToScreen(&where);
  }
  m_bMenu->Go(where, true, false, true);
}
void wxComboBox::Dismiss() {
  if (m_bMenu && m_bMenu->LockLooper()) {
    const char escape = B_ESCAPE;
    m_bMenu->KeyDown(&escape, 1);
    m_bMenu->UnlockLooper();
  }
}
void wxComboBox::Copy() {
  ComboLock lock(m_bMenuField);
  if (lock.IsOk())
    if (auto *editor = Editor(m_bMenuField))
      editor->Copy(be_clipboard);
}
void wxComboBox::Cut() {
  ComboLock lock(m_bMenuField);
  if (lock.IsOk() && IsEditable())
    if (auto *editor = Editor(m_bMenuField))
      editor->Cut(be_clipboard);
}
void wxComboBox::Paste() {
  ComboLock lock(m_bMenuField);
  if (lock.IsOk() && IsEditable())
    if (auto *editor = Editor(m_bMenuField))
      editor->Paste(be_clipboard);
}
bool wxComboBox::CanUndo() const {
  ComboLock lock(m_bMenuField);
  bool redo = false;
  auto *editor = Editor(m_bMenuField);
  return lock.IsOk() && editor &&
         editor->UndoState(&redo) != B_UNDO_UNAVAILABLE && !redo;
}
bool wxComboBox::CanRedo() const {
  ComboLock lock(m_bMenuField);
  bool redo = false;
  auto *editor = Editor(m_bMenuField);
  return lock.IsOk() && editor &&
         editor->UndoState(&redo) != B_UNDO_UNAVAILABLE && redo;
}
void wxComboBox::Undo() {
  ComboLock lock(m_bMenuField);
  if (lock.IsOk() && CanUndo() && IsEditable())
    Editor(m_bMenuField)->Undo(be_clipboard);
}
void wxComboBox::Redo() {
  ComboLock lock(m_bMenuField);
  if (lock.IsOk() && CanRedo() && IsEditable())
    Editor(m_bMenuField)->Undo(be_clipboard);
}
