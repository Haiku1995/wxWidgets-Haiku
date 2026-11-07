///////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/textentry.cpp
// Purpose:     wxTextEntry for Haiku
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#include "wx/textentry.h"
#include "wx/textcompleter.h"
#include "wx/window.h"
#include "wx/event.h"

wxTextEntry::wxTextEntry()
{
}

void wxTextEntry::WriteText(const wxString& text)
{
    wxString value = GetValue();
    const long ip = GetInsertionPoint();

    value.insert(ip, text);
    SetValue(value);
    SetInsertionPoint(ip + text.length());
}

void wxTextEntry::Remove(long from, long to)
{
    wxString value = GetValue();

    if (from < 0)
        from = 0;
    if (to < 0 || to > (long)value.length())
        to = value.length();
    if (from >= to)
        return;

    value.erase(from, to - from);
    SetValue(value);
    SetInsertionPoint(from);
}

void wxTextEntry::Copy()
{
}

void wxTextEntry::Cut()
{
}

void wxTextEntry::Paste()
{
}

void wxTextEntry::Undo()
{
}

void wxTextEntry::Redo()
{
}

bool wxTextEntry::CanUndo() const
{
    return false;
}

bool wxTextEntry::CanRedo() const
{
    return false;
}

void wxTextEntry::SetInsertionPoint(long WXUNUSED(pos))
{
}

long wxTextEntry::GetInsertionPoint() const
{
    return 0;
}

long wxTextEntry::GetLastPosition() const
{
    return GetValue().length();
}

void wxTextEntry::SetSelection(long WXUNUSED(from), long WXUNUSED(to))
{
    wxFAIL_MSG("wxTextEntry::SetSelection must be implemented by the control");
}

void wxTextEntry::GetSelection(long* from, long* to) const
{
    const long pos = GetInsertionPoint();
    *from = pos;
    *to = pos;
}

bool wxTextEntry::IsEditable() const
{
    return true;
}

void wxTextEntry::SetEditable(bool WXUNUSED(editable))
{
}

wxString wxTextEntry::DoGetValue() const
{
    return wxString();
}

void wxTextEntry::DoSetValue(const wxString& value, int flags)
{
    wxTextEntryBase::DoSetValue(value, flags);
}

wxWindow* wxTextEntry::GetEditableWindow()
{
    return nullptr;
}

void wxTextEntry::EnableTextChangedEvents(bool WXUNUSED(enable))
{
}

bool wxTextEntry::DoAutoCompleteFileNames(int WXUNUSED(flags))
{
    return false;
}

bool wxTextEntry::DoAutoCompleteStrings(const wxArrayString& WXUNUSED(choices))
{
    return false;
}

bool wxTextEntry::DoAutoCompleteCustom(wxTextCompleter* WXUNUSED(completer))
{
    return false;
}
