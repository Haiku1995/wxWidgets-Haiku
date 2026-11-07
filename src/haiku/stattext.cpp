#include "wx/wxprec.h"

#if wxUSE_STATTEXT

#ifndef WX_PRECOMP
    #include "wx/stattext.h"
#endif

#include "wx/stattext.h"
#include "wx/haiku/private.h"

#include <StringView.h>
#include <Rect.h>
#include <Alignment.h>
#include <InterfaceDefs.h>
#include <View.h>
#include <Looper.h>
#include <String.h>
#include <StringList.h>
#include <cmath>

//wxIMPLEMENT_DYNAMIC_CLASS(wxStaticText, wxStaticTextBase);

static BRect wxToBRect(const wxPoint& pos, const wxSize& size)
{
    const wxPoint p = (pos == wxDefaultPosition) ? wxPoint(0, 0) : pos;

    if (size == wxDefaultSize)
    {
        return BRect((float)p.x, (float)p.y,
                     (float)(p.x + 100 - 1),
                     (float)(p.y + 20 - 1));
    }

    return BRect((float)p.x, (float)p.y,
                 (float)(p.x + size.x - 1),
                 (float)(p.y + size.y - 1));
}

static alignment wxGetHaikuAlignment(long style)
{
    if (style & wxALIGN_CENTER_HORIZONTAL)
        return B_ALIGN_CENTER;

    if ((style & wxALIGN_MASK) == wxALIGN_RIGHT)
        return B_ALIGN_RIGHT;

    return B_ALIGN_LEFT;
}

class wxHaikuStaticText : public BStringView
{
public:
    wxHaikuStaticText(BRect frame,
                      const char* name,
                      const char* label,
                      long style)
        : BStringView(frame, name, label),
          m_style(style)
    {
        SetAlignment(wxGetHaikuAlignment(style));
        SetTruncation(B_NO_TRUNCATION); // wx handles explicit ellipsize styles.
    }

    void AttachedToWindow() override
    {
        BStringView::AttachedToWindow();
        SetAlignment(wxGetHaikuAlignment(m_style));
    }

    void SetWxLabel(const wxString& label)
    {
        m_visibleLabel = label;
        wxString text;
        m_mnemonic = wxControl::FindAccelIndex(label, &text);
        m_text = text;
        SetText(text.utf8_str());
        Invalidate(); // Also needed when only the mnemonic position changed.
    }

    wxString GetWxLabel() const { return m_visibleLabel; }

    void Draw(BRect updateRect) override
    {
        BStringView::Draw(updateRect);
        if (m_mnemonic == wxNOT_FOUND)
            return;

        const wxString before = m_text.Left(m_mnemonic);
        const size_t lineNumber = before.Freq('\n');
        const wxString linePrefix = before.AfterLast('\n');
        const wxString character = m_text.Mid(m_mnemonic, 1);
        if (character == "\n")
            return;
        BStringList lines;
        BString(Text()).Split("\n", false, lines);
        if (lineNumber >= static_cast<size_t>(lines.CountStrings()))
            return;
        const float width = StringWidth(lines.StringAt(lineNumber).String());
        float x = 0;
        if (Alignment() == B_ALIGN_CENTER)
            x = (Bounds().Width() - width) / 2;
        else if (Alignment() == B_ALIGN_RIGHT)
            x = Bounds().Width() - width;
        font_height height;
        GetFontHeight(&height);
        const float y = Bounds().top + Bounds().bottom - std::ceil(height.descent)
            - std::ceil(height.ascent + height.descent + height.leading)
                * (lines.CountStrings() - lineNumber - 1) + 1;
        const float start = x + StringWidth(linePrefix.utf8_str());
        const float end = x + StringWidth((linePrefix + character).utf8_str());
        PushState();
        SetPenSize(1);
        StrokeLine(BPoint(start, y), BPoint(end - 1, y));
        PopState();
    }

private:
    long m_style;
    wxString m_visibleLabel;
    wxString m_text;
    int m_mnemonic = wxNOT_FOUND;
};

wxStaticText::wxStaticText(wxWindow *parent,
                           wxWindowID id,
                           const wxString &label,
                           const wxPoint &pos,
                           const wxSize &size,
                           long style,
                           const wxString &name)
{
    Create(parent, id, label, pos, size, style, name);
}

wxStaticText::~wxStaticText()
{
}

bool wxStaticText::Create(wxWindow *parent,
                          wxWindowID id,
                          const wxString &label,
                          const wxPoint &pos,
                          const wxSize &size,
                          long style,
                          const wxString &name)
{
    wxCHECK_MSG(parent, false, "wxStaticText requires a parent");

    BStringView* stringView = new wxHaikuStaticText(
        wxToBRect(pos, size),
        name.utf8_str(),
        "",
        style);

    HaikuAdoptView(stringView, nullptr, stringView);

    if (!wxStaticTextBase::Create(parent, id, pos, size, style,
                                  wxDefaultValidator, name))
    {
        delete stringView;
        return false;
    }

    UpdateLabelOrig(label);
    WXSetVisibleLabel(GetEllipsizedLabel());
    return true;
}

void wxStaticText::SetLabel(const wxString& label)
{
    if (!UpdateLabelOrig(label))
        return;

    WXSetVisibleLabel(GetEllipsizedLabel());
    InvalidateBestSize();
}

#include <Looper.h>

void wxStaticText::WXSetVisibleLabel(const wxString& str)
{
    BStringView* view = GetBStringView();
    if (!view)
        return;

    BLooper* looper = view->Looper();
    if (looper && !looper->Lock())
        return;

    static_cast<wxHaikuStaticText*>(view)->SetWxLabel(str);

    if (looper)
        looper->Unlock();
}

wxString wxStaticText::WXGetVisibleLabel() const
{
    BStringView* view = GetBStringView();
    if (!view)
        return wxString();
    BLooper* looper = view->Looper();
    if (looper && !looper->Lock())
        return wxString();
    const wxString label = static_cast<wxHaikuStaticText*>(view)->GetWxLabel();
    if (looper)
        looper->Unlock();
    return label;
}

#endif // wxUSE_STATTEXT
