
///////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/textctrl.cpp
// Purpose:     wxTextCtrl implementation for Haiku
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"
#include "control_lock.h"

#if wxUSE_TEXTCTRL

#include "wx/textctrl.h"
#include "wx/tokenzr.h"
#include "wx/haiku/private.h"

#include <TextControl.h>
#include <TextView.h>
#include <ScrollView.h>
#include <Window.h>
#include <Clipboard.h>

//wxIMPLEMENT_DYNAMIC_CLASS(wxTextCtrl, wxTextCtrlBase);

class wxHaikuTextLock
{
public:
    explicit wxHaikuTextLock(BView* view)
        : m_looper(view ? view->Looper() : nullptr),
          m_locked(m_looper && m_looper->Lock())
    {
    }

    ~wxHaikuTextLock()
    {
        if ( m_locked )
            m_looper->Unlock();
    }

    bool IsOk() const
    {
        return m_locked || !m_looper;
    }

private:
    BLooper* m_looper;
    bool     m_locked;
};


// wx positions count characters; BTextView positions count UTF-8 bytes.
static int32 wxHaikuTextByteOffset(BTextView* view, long position)
{
    const wxString value = wxString::FromUTF8(view->Text());
    const size_t count = position < 0 ? value.length()
                                     : wxMin(static_cast<size_t>(position), value.length());
    return static_cast<int32>(value.substr(0, count).utf8_str().length());
}

static long wxHaikuTextCharacterOffset(BTextView* view, int32 position)
{
    const char* const text = view->Text();
    int32 offset = wxMax(0, wxMin(position, view->TextLength()));
    while (offset > 0 && (static_cast<unsigned char>(text[offset]) & 0xc0) == 0x80)
        --offset;
    return static_cast<long>(wxString::FromUTF8(text, offset).length());
}

static wxString BStringToWx(const BString& bstr)
{
    return bstr.IsEmpty() ? wxString() : wxString::FromUTF8(bstr.String());
}

static BRect wxToBRect(const wxPoint& pos, const wxSize& size,
                       float defW, float defH)
{
    const wxPoint p = (pos == wxDefaultPosition) ? wxPoint(0, 0) : pos;

    if (size == wxDefaultSize)
    {
        return BRect((float)p.x, (float)p.y,
                     (float)(p.x + defW - 1),
                     (float)(p.y + defH - 1));
    }

    return BRect((float)p.x, (float)p.y,
                 (float)(p.x + size.x - 1),
                 (float)(p.y + size.y - 1));
}

// ---------------------------------------------------------------------------
// Single-line BTextControl wrapper
// ---------------------------------------------------------------------------

class wxHaikuTextControl : public BTextControl
{
public:
    wxHaikuTextControl(BRect frame, wxTextCtrl* wxpeer, long style)
        : BTextControl(frame, "wxTextCtrl", nullptr, "",
                       new BMessage('TEXT'),
                       B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP),
          m_wxpeer(wxpeer)
    {
        SetFont(be_plain_font);
        TextView()->SetDoesUndo(true);

        alignment textAlign = B_ALIGN_LEFT;
        if (style & wxTE_RIGHT)
            textAlign = B_ALIGN_RIGHT;
        else if (style & wxTE_CENTRE)
            textAlign = B_ALIGN_CENTER;

        SetAlignment(B_ALIGN_LEFT, textAlign);

        if (style & wxTE_PASSWORD)
            TextView()->HideTyping(true);

        if (style & wxTE_READONLY)
        {
            TextView()->MakeEditable(false);

        }

        SetModificationMessage(new BMessage('TEXT'));
        SetTarget(this);
    }

    void AttachedToWindow() override
    {
        BTextControl::AttachedToWindow();
        SetTarget(this);
        TextView()->MakeEditable(IsEnabled() && m_wxpeer->IsEditable());
    }

    void SetText(const char* text) override
    {
        BTextControl::SetText(text);
        ResetLengthSnapshot();
    }
    void ResetLengthSnapshot() { m_previousValue = wxString::FromUTF8(Text()); }

    void SetEnabled(bool enabled) override
    {
        BTextControl::SetEnabled(enabled);
        // BTextControl changes its inner view's editable state when enabled.
        // Preserve the wx read-only policy across Disable()/Enable().
        TextView()->MakeEditable(enabled && m_wxpeer->IsEditable());
    }
    void MessageReceived(BMessage* msg) override
    {
        if (msg && msg->what == 'TEXT' && m_wxpeer)
        {
            wxString value = wxString::FromUTF8(Text());
            if (m_maxLength && value.length() > m_maxLength && value.length() > m_previousValue.length())
            {
                size_t prefix = 0, suffix = 0;
                while (prefix < value.length() && prefix < m_previousValue.length() &&
                       value[prefix] == m_previousValue[prefix]) ++prefix;
                while (suffix < value.length()-prefix && suffix < m_previousValue.length()-prefix &&
                       value[value.length()-suffix-1] == m_previousValue[m_previousValue.length()-suffix-1]) ++suffix;
                size_t retained = prefix + suffix;
                size_t room = m_maxLength > retained ? m_maxLength-retained : 0;
                value = value.substr(0,prefix) + value.substr(prefix,room) + value.substr(value.length()-suffix);
                SetModificationMessage(nullptr);
                BTextControl::SetText(value.utf8_str());
                const int32 caret = static_cast<int32>(value.substr(0,prefix+room).utf8_str().length());
                TextView()->Select(caret,caret);
                SetModificationMessage(new BMessage('TEXT'));
                wxCommandEvent maximum(wxEVT_TEXT_MAXLEN,m_wxpeer->GetId());
                maximum.SetEventObject(m_wxpeer);maximum.SetString(value);
                m_wxpeer->HandleWindowEvent(maximum);
            }
            m_previousValue = value;
            m_modified = true;
            wxCommandEvent evt(wxEVT_TEXT, m_wxpeer->GetId());
            evt.SetEventObject(m_wxpeer);
            evt.SetString(BStringToWx(Text()));
            m_wxpeer->HandleWindowEvent(evt);
            return;
        }

        BTextControl::MessageReceived(msg);
    }

    bool m_modified = false;

    void SetMaxLength(unsigned long len)
    {
        m_maxLength = len;
        ResetLengthSnapshot();
    }

protected:
    void KeyDown(const char* bytes, int32 numBytes) override
    {
        BTextControl::KeyDown(bytes, numBytes);
    }

private:
    wxString m_previousValue;
    wxTextCtrl* m_wxpeer;
    unsigned long m_maxLength = 0;
};

// ---------------------------------------------------------------------------
// Multi-line BTextView
// ---------------------------------------------------------------------------

class wxHaikuTextView : public BTextView
{
public:
    wxHaikuTextView(BRect frame, wxTextCtrl* wxpeer, long style)
        : BTextView(frame, "wxTextView",
                    BRect(4, 4, frame.Width() - 8, frame.Height() - 8),
                    B_FOLLOW_ALL_SIDES, B_WILL_DRAW | B_NAVIGABLE),
          m_wxpeer(wxpeer)
    {
        SetFontAndColor(be_plain_font);
        MakeEditable(true);
        MakeSelectable(true);
        SetStylable(false);
        SetDoesUndo(true);

        SetWordWrap(!(style & wxTE_DONTWRAP));

        if (style & wxTE_READONLY)
            MakeEditable(false);
    }

    void KeyDown(const char* bytes, int32 numBytes) override
    {
        BTextView::KeyDown(bytes, numBytes);
    }

    void InsertText(const char* text, int32 length, int32 offset,
                    const text_run_array* runs) override
    {
        wxCharBuffer limited;
        bool exceeded = false;
        if (!m_suppressNotifications && m_maxLength)
        {
            const size_t current = wxString::FromUTF8(Text()).length();
            const size_t room = m_maxLength > current ? m_maxLength-current : 0;
            const wxString incoming = wxString::FromUTF8(text,length);
            if (incoming.length() > room)
            {
                limited = incoming.substr(0,room).utf8_str();
                text = limited.data();length = static_cast<int32>(limited.length());
                exceeded = true;
            }
        }
        if (length) BTextView::InsertText(text, length, offset, runs);
        if (exceeded)
        {
            wxCommandEvent maximum(wxEVT_TEXT_MAXLEN,m_wxpeer->GetId());
            maximum.SetEventObject(m_wxpeer);maximum.SetString(wxString::FromUTF8(Text()));
            m_wxpeer->HandleWindowEvent(maximum);
        }
        if (!length) return;

        if (m_wxpeer && !m_suppressNotifications)
        {
            m_modified = true;
            m_wxpeer->InvalidateBestSize();
            wxCommandEvent evt(wxEVT_TEXT, m_wxpeer->GetId());
            evt.SetEventObject(m_wxpeer);
            evt.SetString(BStringToWx(BString(Text())));
            m_wxpeer->HandleWindowEvent(evt);
        }
    }

    void DeleteText(int32 fromOffset, int32 toOffset) override
    {
        BTextView::DeleteText(fromOffset, toOffset);

        if (m_wxpeer && !m_suppressNotifications)
        {
            m_modified = true;
            m_wxpeer->InvalidateBestSize();
            wxCommandEvent evt(wxEVT_TEXT, m_wxpeer->GetId());
            evt.SetEventObject(m_wxpeer);
            evt.SetString(BStringToWx(BString(Text())));
            m_wxpeer->HandleWindowEvent(evt);
        }
    }

    void SetMaxLength(unsigned long len)
    {
        m_maxLength = len;
    }

    bool m_modified = false;
    bool m_suppressNotifications = false;

    int32 GetLayoutLineCount() const
    {
        // Haiku's CountLines() omits the empty line after a final newline.
        return LineAt(TextLength()) + 1;
    }

    int32 GetLayoutLineEnd(int32 line) const
    {
        int32 end = OffsetAt(line + 1);
        if (end > OffsetAt(line) && Text()[end - 1] == '\n')
            --end;
        return end;
    }

private:
    wxTextCtrl* m_wxpeer;
    unsigned long m_maxLength = 0;
};

// ---------------------------------------------------------------------------
// Multi-line BScrollView wrapper
// ---------------------------------------------------------------------------

class wxHaikuMultiLineText : public BScrollView
{
public:
    wxHaikuMultiLineText(BRect frame, wxTextCtrl* wxpeer, long style)
        : BScrollView("wxScrollView", nullptr,
                      B_FOLLOW_ALL_SIDES, 0, true, true,
                      (style & wxBORDER_MASK) == wxBORDER_NONE ? B_NO_BORDER
                                                               : B_FANCY_BORDER),
          m_wxpeer(wxpeer)
    {
        MoveTo(frame.LeftTop());
        ResizeTo(frame.Width(), frame.Height());

        m_textView = new wxHaikuTextView(Bounds(), wxpeer, style);
        AddChild(m_textView);
    }

    wxHaikuTextView* GetTextView() const { return m_textView; }

    void SetMaxLength(unsigned long len)
    {
        m_textView->SetMaxLength(len);
    }

private:
    wxHaikuTextView* m_textView = nullptr;
    wxTextCtrl* m_wxpeer = nullptr;
};

// ---------------------------------------------------------------------------
// wxTextCtrl
// ---------------------------------------------------------------------------

wxTextCtrl::wxTextCtrl(wxWindow *parent,
                       wxWindowID id,
                       const wxString &value,
                       const wxPoint &pos,
                       const wxSize &size,
                       long style,
                       const wxValidator& validator,
                       const wxString &name)
{
    Create(parent, id, value, pos, size, style, validator, name);
}

wxTextCtrl::~wxTextCtrl()
{
}

bool wxTextCtrl::Create(wxWindow* parent,
                        wxWindowID id,
                        const wxString& value,
                        const wxPoint& pos,
                        const wxSize& size,
                        long style,
                        const wxValidator& validator,
                        const wxString& name)
{
    wxCHECK_MSG(parent, false, "wxTextCtrl requires a parent");

    if (style & wxTE_MULTILINE)
    {
        m_haikuMulti = new wxHaikuMultiLineText(
            wxToBRect(pos, size, 200, 120), this, style);

        HaikuAdoptView(m_haikuMulti, nullptr, m_haikuMulti);
    }
    else
    {
        m_haikuSingle = new wxHaikuTextControl(
            wxToBRect(pos, size, 150, 25), this, style);

        HaikuAdoptView(m_haikuSingle, nullptr, m_haikuSingle);
    }

    if (!wxControl::Create(parent, id, pos, size, style, validator, name))
        return false;

    if (!value.empty())
        ChangeValue(value);

    return true;
}

wxString wxTextCtrl::DoGetValue() const
{
    wxHaikuControlLock lock(m_haikuSingle ? static_cast<BView*>(m_haikuSingle) : static_cast<BView*>(m_haikuMulti));
    if (!lock.IsOk())
        return wxString();

    if (m_haikuSingle)
        return BStringToWx(m_haikuSingle->Text());

    if (m_haikuMulti)
        return BStringToWx(BString(m_haikuMulti->GetTextView()->Text()));

    return wxEmptyString;
}

// Generic hints occupy native storage, but are not the control value.
wxString wxTextCtrl::GetValue() const
{
    return wxTextEntryBase::GetValue();
}

void wxTextCtrl::DoSetValue(const wxString& value, int flags)
{
    const bool sendEvent = (flags & SetValue_SendEvent) != 0;

    // Setting the same value still notifies, but must preserve the selection.
    if (value == DoGetValue())
    {
        if (sendEvent)
            SendTextUpdatedEventIfAllowed();
        return;
    }

    if (m_haikuSingle)
    {
        wxHaikuTextLock lock(m_haikuSingle);
        if ( !lock.IsOk() )
            return;

        m_haikuSingle->SetModificationMessage(nullptr);

        m_haikuSingle->SetText(value.utf8_str());

        m_haikuSingle->m_modified = false;
        m_haikuSingle->SetModificationMessage(new BMessage('TEXT'));
    }
    else if (m_haikuMulti)
    {
        wxHaikuTextView* tv = m_haikuMulti->GetTextView();
        wxHaikuTextLock lock(tv);
        if ( !lock.IsOk() )
            return;

        tv->m_suppressNotifications = true;
        tv->SetText(value.utf8_str());
        tv->m_suppressNotifications = false;
        tv->m_modified = false;
        InvalidateBestSize();
    }
    EmptyUndoBuffer();
    if (sendEvent)
        SendTextUpdatedEventIfAllowed();
}

wxTextCtrlHitTestResult wxTextCtrl::HitTest(const wxPoint& pt, long *pos) const
{
    BTextView* view = m_haikuSingle ? m_haikuSingle->TextView()
                     : m_haikuMulti ? m_haikuMulti->GetTextView() : nullptr;
    wxHaikuTextLock lock(view);
    if (!view || !lock.IsOk())
        return wxTE_HT_UNKNOWN;

    BPoint point(pt.x, pt.y);
    GetHandle()->ConvertToScreen(&point);
    view->ConvertFromScreen(&point);

    const int32 textEnd = view->TextLength();
    float lastHeight;
    const BPoint last = view->PointAt(textEnd, &lastHeight);
    int32 offset;
    wxTextCtrlHitTestResult result;
    if (point.y < view->PointAt(0).y)
    {
        offset = 0;
        result = wxTE_HT_BEFORE;
    }
    else if (point.y >= last.y + lastHeight)
    {
        offset = textEnd;
        result = wxTE_HT_BELOW;
    }
    else
    {
        const int32 line = view->LineAt(point);
        const int32 start = view->OffsetAt(line);
        const BPoint origin = view->PointAt(start);
        if (point.x < origin.x)
        {
            offset = start;
            result = wxTE_HT_BEFORE;
        }
        else if (line >= view->CountLines() ||
                 point.x >= origin.x + view->LineWidth(line))
        {
            offset = line + 1 < view->CountLines() ? view->OffsetAt(line + 1)
                                                   : textEnd;
            if (offset > start && view->Text()[offset - 1] == '\n')
                --offset;
            result = wxTE_HT_BEYOND;
        }
        else
        {
            offset = view->OffsetAt(point);
            // OffsetAt finds the nearest caret. HitTest reports the glyph
            // under the point, including the right half of a UTF-8 character.
            if (offset > start &&
                (view->LineAt(offset) != line || view->PointAt(offset).x > point.x))
                --offset;
            result = wxTE_HT_ON_TEXT;
        }
    }

    if (pos)
        *pos = wxHaikuTextCharacterOffset(view, offset);
    return result;
}

wxTextCtrlHitTestResult wxTextCtrl::HitTest(const wxPoint& pt,
                                            wxTextCoord *col,
                                            wxTextCoord *row) const
{
    long pos;
    const wxTextCtrlHitTestResult result = HitTest(pt, &pos);
    if (result != wxTE_HT_UNKNOWN && (col || row))
    {
        long x, y;
        if (PositionToXY(pos, &x, &y))
        {
            if (col)
                *col = x;
            if (row)
                *row = y;
        }
    }
    return result;
}

void wxTextCtrl::WriteText(const wxString& text)
{
    BTextView* tv = nullptr;

    if (m_haikuSingle)
        tv = m_haikuSingle->TextView();
    else if (m_haikuMulti)
        tv = m_haikuMulti->GetTextView();

    if (!tv)
        return;

    wxHaikuTextLock lock(tv);
    if ( !lock.IsOk() )
        return;

    // Treat replacing the selection as one edit and send one wx event.
    if (m_haikuSingle)
        m_haikuSingle->SetModificationMessage(nullptr);
    else
        m_haikuMulti->GetTextView()->m_suppressNotifications = true;

    int32 from = 0, to = 0;
    tv->GetSelection(&from, &to);
    if (from != to)
        tv->Delete(from, to);
    const wxCharBuffer utf8 = text.utf8_str();
    tv->Insert(from, utf8.data(), static_cast<int32>(utf8.length()));
    const int32 insertion = from + static_cast<int32>(utf8.length());
    tv->Select(insertion, insertion);
    if (!m_defaultStyle.IsDefault())
    {
        wxTextAttr nativeStyle = m_defaultStyle;
        nativeStyle.SetFlags(nativeStyle.GetFlags() & (wxTEXT_ATTR_FONT | wxTEXT_ATTR_TEXT_COLOUR));
        SetStyle(wxHaikuTextCharacterOffset(tv, from),
                 wxHaikuTextCharacterOffset(tv, insertion), nativeStyle);
    }

    if (m_haikuSingle)
    {
        m_haikuSingle->ResetLengthSnapshot();
        m_haikuSingle->SetModificationMessage(new BMessage('TEXT'));
    }
    else
        m_haikuMulti->GetTextView()->m_suppressNotifications = false;

    MarkDirty();
    if (m_haikuMulti)
        InvalidateBestSize();
    // Replace() suppresses the intermediate removal event.
    SendTextUpdatedEventIfAllowed();
}
bool wxTextCtrl::IsModified() const
{
    wxHaikuControlLock lock(m_haikuSingle ? static_cast<BView*>(m_haikuSingle) : static_cast<BView*>(m_haikuMulti));
    if (!lock.IsOk())
        return false;

    return m_haikuSingle ? m_haikuSingle->m_modified
         : m_haikuMulti ? m_haikuMulti->GetTextView()->m_modified : false;
}

void wxTextCtrl::MarkDirty()
{
    wxHaikuControlLock lock(m_haikuSingle ? static_cast<BView*>(m_haikuSingle) : static_cast<BView*>(m_haikuMulti));
    if (!lock.IsOk())
        return;

    if (m_haikuSingle) m_haikuSingle->m_modified = true;
    if (m_haikuMulti) m_haikuMulti->GetTextView()->m_modified = true;
}

void wxTextCtrl::DiscardEdits()
{
    wxHaikuControlLock lock(m_haikuSingle ? static_cast<BView*>(m_haikuSingle) : static_cast<BView*>(m_haikuMulti));
    if (!lock.IsOk())
        return;

    if (m_haikuSingle) m_haikuSingle->m_modified = false;
    if (m_haikuMulti) m_haikuMulti->GetTextView()->m_modified = false;
}

long wxTextCtrl::XYToPosition(long x, long y) const
{
    if (x < 0 || y < 0)
        return -1;

    if (m_haikuSingle)
        return y == 0 && x <= GetLastPosition() ? x : -1;

    wxHaikuTextView* view = m_haikuMulti ? m_haikuMulti->GetTextView() : nullptr;
    wxHaikuTextLock lock(view);
    if (!view || !lock.IsOk() || y >= view->GetLayoutLineCount())
        return -1;

    const long first = wxHaikuTextCharacterOffset(view, view->OffsetAt(y));
    const long last = wxHaikuTextCharacterOffset(view, view->GetLayoutLineEnd(y));
    return x <= last - first ? first + x : -1;
}

bool wxTextCtrl::PositionToXY(long pos, long* x, long* y) const
{
    if (!x || !y || pos < 0)
        return false;

    if (m_haikuSingle)
    {
        if (pos > GetLastPosition())
            return false;
        *x = pos;
        *y = 0;
        return true;
    }

    wxHaikuTextView* view = m_haikuMulti ? m_haikuMulti->GetTextView() : nullptr;
    wxHaikuTextLock lock(view);
    if (!view || !lock.IsOk() || pos > wxHaikuTextCharacterOffset(view, view->TextLength()))
        return false;

    const int32 line = view->LineAt(wxHaikuTextByteOffset(view, pos));
    *y = line;
    *x = pos - wxHaikuTextCharacterOffset(view, view->OffsetAt(line));
    return true;
}

long wxTextCtrl::GetInsertionPoint() const
{
    int32 s = 0, e = 0;

    if (m_haikuSingle)
    {
        BTextView* tv = m_haikuSingle->TextView();
        wxHaikuTextLock lock(tv);
        if ( !lock.IsOk() )
            return 0;

        tv->GetSelection(&s, &e);
        return wxHaikuTextCharacterOffset(tv, s);
    }

    if (m_haikuMulti)
    {
        wxHaikuTextView* tv = m_haikuMulti->GetTextView();
        wxHaikuTextLock lock(tv);
        if ( !lock.IsOk() )
            return 0;

        tv->GetSelection(&s, &e);
        return wxHaikuTextCharacterOffset(tv, s);
    }

    return 0;
}

// BTextView supports font and foreground colour runs, not paragraph or
// per-run background attributes. Reject unsupported requests explicitly.
static bool wxHaikuCanApplyTextStyle(const wxTextAttr& style)
{
    const long supported = wxTEXT_ATTR_FONT | wxTEXT_ATTR_TEXT_COLOUR;
    return (style.GetFlags() & ~supported) == 0;
}

bool wxTextCtrl::SetStyle(long start, long end, const wxTextAttr& style)
{
    if (!wxHaikuCanApplyTextStyle(style) || start < 0 || end < start ||
        end > GetLastPosition())
        return false;
    BTextView* view = m_haikuSingle ? m_haikuSingle->TextView()
                                  : m_haikuMulti ? m_haikuMulti->GetTextView() : nullptr;
    if (!view) return false;
    wxHaikuTextLock lock(view);
    if (!lock.IsOk()) return false;
    if (style.IsDefault()) return true;
    const int32 first = wxHaikuTextByteOffset(view, start);
    const int32 last = wxHaikuTextByteOffset(view, end);
    BFont font; rgb_color colour;
    view->GetFontAndColor(first, &font, &colour);
    wxTextAttr effective = wxTextAttr::Combine(style, wxTextAttr(wxColour(colour.red, colour.green, colour.blue, colour.alpha), wxNullColour, wxFont(font)), this);
    if (effective.HasFont()) font = effective.GetFont().GetHandle();
    if (style.HasTextColour())
    {
        const wxColour& c = style.GetTextColour();
        colour = rgb_color{c.Red(), c.Green(), c.Blue(), c.Alpha()};
    }
    view->SetStylable(true);
    view->SetFontAndColor(first, last, &font, B_FONT_ALL, &colour);
    return true;
}

bool wxTextCtrl::GetStyle(long position, wxTextAttr& style)
{
    if (position < 0 || position > GetLastPosition()) return false;
    BTextView* view = m_haikuSingle ? m_haikuSingle->TextView()
                                  : m_haikuMulti ? m_haikuMulti->GetTextView() : nullptr;
    if (!view) return false;
    wxHaikuTextLock lock(view);
    if (!lock.IsOk()) return false;
    BFont font; rgb_color colour;
    view->GetFontAndColor(wxHaikuTextByteOffset(view, position), &font, &colour);
    style = wxTextAttr(wxColour(colour.red, colour.green, colour.blue, colour.alpha),
                       wxNullColour, wxFont(font));
    return true;
}

bool wxTextCtrl::SetDefaultStyle(const wxTextAttr& style)
{
    if (!wxHaikuCanApplyTextStyle(style)) return false;
    if (!wxTextCtrlBase::SetDefaultStyle(style)) return false;
    wxTextAttr nativeStyle = m_defaultStyle;
    nativeStyle.SetFlags(nativeStyle.GetFlags() & (wxTEXT_ATTR_FONT | wxTEXT_ATTR_TEXT_COLOUR));
    const long position = GetInsertionPoint();
    return position < 0 || SetStyle(position, position, nativeStyle);
}

void wxTextCtrl::SetInsertionPoint(long pos)
{
    if (pos == -1)
        pos = GetValue().length();

    if (m_haikuSingle)
    {
        BTextView* tv = m_haikuSingle->TextView();
        wxHaikuTextLock lock(tv);
        if ( lock.IsOk() )
        {
            const int32 offset = wxHaikuTextByteOffset(tv, pos);
            tv->Select(offset, offset);
            tv->ScrollToOffset(offset);
        }
    }
    else if (m_haikuMulti)
    {
        wxHaikuTextView* tv = m_haikuMulti->GetTextView();
        wxHaikuTextLock lock(tv);
        if ( lock.IsOk() )
        {
            const int32 offset = wxHaikuTextByteOffset(tv, pos);
            tv->Select(offset, offset);
            tv->ScrollToOffset(offset);
        }
    }
}

void wxTextCtrl::SetSelection(long from, long to)
{
    if (from == -1)
        from = 0;
    if (to == -1)
        to = GetValue().length();

    if (m_haikuSingle)
    {
        BTextView* tv = m_haikuSingle->TextView();
        wxHaikuTextLock lock(tv);
        if ( lock.IsOk() )
            tv->Select(wxHaikuTextByteOffset(tv, from), wxHaikuTextByteOffset(tv, to));
    }
    else if (m_haikuMulti)
    {
        wxHaikuTextView* tv = m_haikuMulti->GetTextView();
        wxHaikuTextLock lock(tv);
        if ( lock.IsOk() )
            tv->Select(wxHaikuTextByteOffset(tv, from), wxHaikuTextByteOffset(tv, to));
    }
}

void wxTextCtrl::GetSelection(long* from, long* to) const
{
    int32 s = 0, e = 0;

    if (m_haikuSingle)
    {
        BTextView* tv = m_haikuSingle->TextView();
        wxHaikuTextLock lock(tv);
        if ( lock.IsOk() )
        {
            tv->GetSelection(&s, &e);
            s = wxHaikuTextCharacterOffset(tv, s);
            e = wxHaikuTextCharacterOffset(tv, e);
        }
    }
    else if (m_haikuMulti)
    {
        wxHaikuTextView* tv = m_haikuMulti->GetTextView();
        wxHaikuTextLock lock(tv);
        if ( lock.IsOk() )
        {
            tv->GetSelection(&s, &e);
            s = wxHaikuTextCharacterOffset(tv, s);
            e = wxHaikuTextCharacterOffset(tv, e);
        }
    }

    if (from)
        *from = s;
    if (to)
        *to = e;
}

bool wxTextCtrl::DoLoadFile(const wxString& file, int fileType)
{
    if (fileType != wxTEXT_TYPE_ANY && fileType != wxTEXT_TYPE_PLAIN)
        return false;
    return wxTextAreaBase::DoLoadFile(file, fileType);
}

bool wxTextCtrl::DoSaveFile(const wxString& file, int fileType)
{
    // Reject unsupported formats before the shared writer opens the file.
    if (fileType != wxTEXT_TYPE_ANY && fileType != wxTEXT_TYPE_PLAIN)
        return false;
    return wxTextAreaBase::DoSaveFile(file, fileType);
}

void wxTextCtrl::Copy()
{
    HandleClipboardCommand(B_COPY);
}

void wxTextCtrl::Cut()
{
    HandleClipboardCommand(B_CUT);
}

void wxTextCtrl::Paste()
{
    HandleClipboardCommand(B_PASTE);
}

void wxTextCtrl::Undo()
{
    BTextView* tv = m_haikuSingle ? m_haikuSingle->TextView()
                  : m_haikuMulti ? m_haikuMulti->GetTextView() : nullptr;
    wxHaikuTextLock lock(tv);
    if (tv && lock.IsOk() && CanUndo() && tv->IsEditable())
        tv->Undo(be_clipboard);
}

void wxTextCtrl::Redo()
{
    BTextView* tv = m_haikuSingle ? m_haikuSingle->TextView()
                  : m_haikuMulti ? m_haikuMulti->GetTextView() : nullptr;
    wxHaikuTextLock lock(tv);
    if (tv && lock.IsOk() && CanRedo() && tv->IsEditable())
        tv->Undo(be_clipboard);
}

void wxTextCtrl::EmptyUndoBuffer()
{
    BTextView* tv = m_haikuSingle ? m_haikuSingle->TextView()
                  : m_haikuMulti ? m_haikuMulti->GetTextView() : nullptr;
    wxHaikuTextLock lock(tv);
    if (tv && lock.IsOk())
    {
        tv->SetDoesUndo(false);
        tv->SetDoesUndo(true);
    }
}

wxPoint wxTextAreaBase::DoPositionToCoords(long pos) const
{
    // Keep the existing virtual slot so clients built with older headers
    // can still construct wxTextCtrl with the correct secondary vtables.
    if (const auto* text = dynamic_cast<const wxTextCtrl*>(this))
        return text->HaikuPositionToCoords(pos);

    return wxDefaultPosition;
}

wxPoint wxTextCtrl::HaikuPositionToCoords(long pos) const
{
    BTextView* view = m_haikuSingle ? m_haikuSingle->TextView()
                     : m_haikuMulti ? m_haikuMulti->GetTextView() : nullptr;
    wxHaikuTextLock lock(view);
    if (!view || !lock.IsOk())
        return wxDefaultPosition;

    BPoint point = view->PointAt(wxHaikuTextByteOffset(view, pos));
    view->ConvertToScreen(&point);
    GetHandle()->ConvertFromScreen(&point);
    return wxPoint(wxRound(point.x), wxRound(point.y));
}

void wxTextCtrl::ShowPosition(long pos)
{
    BTextView* tv = m_haikuSingle ? m_haikuSingle->TextView()
                  : m_haikuMulti ? m_haikuMulti->GetTextView() : nullptr;
    wxHaikuTextLock lock(tv);
    if (tv && lock.IsOk())
        tv->ScrollToOffset(wxHaikuTextByteOffset(tv, pos));
}

void wxTextCtrl::HandleClipboardCommand(uint32 what)
{
    BTextView* view = m_haikuSingle ? m_haikuSingle->TextView()
                     : m_haikuMulti ? m_haikuMulti->GetTextView() : nullptr;
    wxHaikuTextLock lock(view);
    if (!view || !lock.IsOk())
        return;

    // Preserve the native password and editability checks. Complete the
    // operation before the caller changes the selection or reads the value.
    BMessage message(what);
    view->MessageReceived(&message);
}

bool wxTextCtrl::CanUndo() const
{
    BTextView* tv = m_haikuSingle ? m_haikuSingle->TextView()
                  : m_haikuMulti ? m_haikuMulti->GetTextView() : nullptr;
    wxHaikuTextLock lock(tv);
    bool redo = false;
    return tv && lock.IsOk() && tv->UndoState(&redo) != B_UNDO_UNAVAILABLE && !redo;
}

bool wxTextCtrl::CanRedo() const
{
    BTextView* tv = m_haikuSingle ? m_haikuSingle->TextView()
                  : m_haikuMulti ? m_haikuMulti->GetTextView() : nullptr;
    wxHaikuTextLock lock(tv);
    bool redo = false;
    return tv && lock.IsOk() && tv->UndoState(&redo) != B_UNDO_UNAVAILABLE && redo;
}

int wxTextCtrl::GetNumberOfLines() const
{
    wxHaikuControlLock lock(m_haikuSingle ? static_cast<BView*>(m_haikuSingle) : static_cast<BView*>(m_haikuMulti));
    if (!lock.IsOk())
        return 0;

    if (m_haikuSingle)
        return 1;

    return m_haikuMulti ? m_haikuMulti->GetTextView()->GetLayoutLineCount() : 1;
}

wxTextSearchResult wxTextCtrl::SearchText(const wxTextSearch& search) const
{
    BTextView* textView = nullptr;

    if (m_haikuSingle)
        textView = m_haikuSingle->TextView();
    else if (m_haikuMulti)
        textView = m_haikuMulti->GetTextView();

    if (!textView)
        return wxTextSearchResult();

    wxHaikuTextLock lock(textView);
    if ( !lock.IsOk() )
        return wxTextSearchResult();

    const char* rawText = textView->Text();
    if (!rawText)
        return wxTextSearchResult();

    wxString haystack = wxString::FromUTF8(rawText);
    wxString needle = search.m_searchValue;

    if (needle.empty())
        return wxTextSearchResult();

    const bool down = search.m_direction == wxTextSearch::Direction::Down;
    const size_t length = haystack.length();
    const long start = search.m_startingPosition;
    if (start < -1 || (start >= 0 && static_cast<size_t>(start) > length))
        return wxTextSearchResult();

    if (!search.m_matchCase)
    {
        haystack.MakeLower();
        needle.MakeLower();
    }

    const auto isWordCharacter = [](wxUniChar ch)
    {
        return wxIsalnum(ch) || ch == '_';
    };

    const size_t limit = start == -1 ? (down ? 0 : length)
                                     : static_cast<size_t>(start);
    if (!down && limit < needle.length())
        return wxTextSearchResult();
    // A backward match must finish at or before the starting position.
    size_t cursor = down ? limit : limit - needle.length();
    size_t foundPos, endPos;
    for (;;)
    {
        foundPos = down ? haystack.find(needle, cursor)
                        : haystack.rfind(needle, cursor);
        if (foundPos == wxString::npos)
            return wxTextSearchResult();

        endPos = foundPos + needle.length();
        if (!search.m_wholeWord ||
            ((foundPos == 0 || !isWordCharacter(haystack[foundPos - 1])) &&
             (endPos == length || !isWordCharacter(haystack[endPos]))))
            break;

        if (!down && foundPos == 0)
            return wxTextSearchResult();
        cursor = down ? foundPos + 1 : foundPos - 1;
    }

    textView->Select(wxHaikuTextByteOffset(textView, foundPos), wxHaikuTextByteOffset(textView, endPos));
    textView->ScrollToSelection();

    return wxTextSearchResult(foundPos, endPos);
}

int wxTextCtrl::GetLineLength(long lineNo) const
{
    if (lineNo < 0 || lineNo >= GetNumberOfLines())
        return -1;
    return GetLineText(lineNo).length();
}

wxString wxTextCtrl::GetLineText(long lineNo) const
{
    if (m_haikuSingle)
        return lineNo == 0 ? DoGetValue() : wxString();

    wxHaikuTextView* view = m_haikuMulti ? m_haikuMulti->GetTextView() : nullptr;
    wxHaikuTextLock lock(view);
    if (!view || !lock.IsOk() || lineNo < 0 || lineNo >= view->GetLayoutLineCount())
        return wxString();

    const int32 start = view->OffsetAt(lineNo);
    const int32 end = view->GetLayoutLineEnd(lineNo);
    return wxString::FromUTF8(view->Text() + start, end - start);
}

void wxTextCtrl::SetMaxLength(unsigned long len)
{
    wxHaikuControlLock lock(m_haikuSingle ? static_cast<BView*>(m_haikuSingle) : static_cast<BView*>(m_haikuMulti));
    if (!lock.IsOk())
        return;

    if (m_haikuSingle)
        m_haikuSingle->SetMaxLength(len);

    if (m_haikuMulti)
        m_haikuMulti->SetMaxLength(len);
}

bool wxTextCtrl::IsEditable() const
{
    return !HasFlag(wxTE_READONLY);
}

void wxTextCtrl::SetEditable(bool editable)
{
    long style = GetWindowStyle();
    if (editable)
        style &= ~wxTE_READONLY;
    else
        style |= wxTE_READONLY;

    SetWindowStyle(style);

    if (m_haikuSingle)
    {
        wxHaikuTextLock lock(m_haikuSingle);
        if ( lock.IsOk() )
        {
            m_haikuSingle->TextView()->MakeEditable(editable && IsEnabled());

        }
    }

    if (m_haikuMulti)
    {
        wxHaikuTextView* tv = m_haikuMulti->GetTextView();
        wxHaikuTextLock lock(tv);
        if ( lock.IsOk() )
            tv->MakeEditable(editable && IsEnabled());
    }
}

wxSize wxTextCtrl::DoGetBestSize() const
{
    if (!IsMultiLine())
        return wxSize(150, GetCharHeight() + 12);

    const int lines = wxMax(1, wxMin(GetNumberOfLines(), 10));
    return wxSize(200, lines * GetCharHeight() + 24);
}

BControl* wxTextCtrl::BeControl() const
{
    return m_haikuSingle ? static_cast<BControl*>(m_haikuSingle) : nullptr;
}

#endif // wxUSE_TEXTCTRL
