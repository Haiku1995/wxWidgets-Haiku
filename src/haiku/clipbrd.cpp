/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/clipbrd.cpp
// Purpose:     wxClipboard implementation for Haiku using BClipboard
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#include "wx/clipbrd.h"
#include "wx/log.h"
#include "wx/scopedptr.h"

#include "wx/haiku/private.h"
#include "wx/haiku/dataobj.h"   // IMPORTANT: Haiku wxDataObject with HaikuAddDataTo/HaikuSetDataFrom

// Haiku headers
#include <Clipboard.h>
#include <Message.h>
#include <TypeConstants.h>

// Global system clipboard
extern BClipboard* be_clipboard;

// ----------------------------------------------------------------------------
// wxClipboard
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(wxClipboard, wxObject);

wxClipboard::wxClipboard()
    : m_isOpened(false)
{
}

wxClipboard::~wxClipboard()
{
    if ( m_isOpened )
        Close();
}

bool wxClipboard::Open()
{
    if ( m_isOpened )
        return true;

    if ( !be_clipboard )
        return false;

    if ( be_clipboard->Lock() )
    {
        m_isOpened = true;
        return true;
    }

    wxLogError("Failed to lock system clipboard");
    return false;
}

void wxClipboard::Close()
{
    if ( m_isOpened && be_clipboard )
    {
        be_clipboard->Unlock();
        m_isOpened = false;
    }
}

bool wxClipboard::IsOpened() const
{
    return m_isOpened;
}

static bool wxHaikuClipboardCommit()
{
    return be_clipboard && be_clipboard->Commit() == B_OK;
}

bool wxClipboard::SetData(wxDataObject* data)
{
    // The clipboard owns the object even when writing its data fails.
    wxScopedPtr<wxDataObject> ownedData(data);
    if ( !data )
        return false;

    // wxWidgets convention: SetData requires clipboard to be open in most ports.
    // But we can be forgiving: lock temporarily if needed.
    bool tempLock = false;
    if ( !m_isOpened )
    {
        if ( !be_clipboard || !be_clipboard->Lock() )
            return false;
        tempLock = true;
    }

    bool ok = false;

    BMessage* msg = be_clipboard->Data();
    if ( msg )
    {
        msg->MakeEmpty();

        // Use Haiku-native helper to add *all* supported formats as raw bytes.
        data->HaikuAddDataTo(*msg);

        ok = wxHaikuClipboardCommit();
    }

    if ( tempLock )
        be_clipboard->Unlock();

    return ok;
}

bool wxClipboard::AddData(wxDataObject* data)
{
    wxScopedPtr<wxDataObject> ownedData(data);
    if (!data || !be_clipboard) return false;
    const bool tempLock = !m_isOpened;
    if (tempLock && !be_clipboard->Lock()) return false;
    bool ok = false;
    if (BMessage* message = be_clipboard->Data())
    {
        BMessage additions;
        data->HaikuAddDataTo(additions);
        char* name;
        type_code type;
        int32 count;
        for (int32 i = 0; additions.GetInfo(B_ANY_TYPE, i, &name, &type, &count) == B_OK; ++i)
            message->RemoveName(name);
        ok = message->Append(additions) == B_OK && wxHaikuClipboardCommit();
    }
    if (tempLock) be_clipboard->Unlock();
    return ok;
}

bool wxClipboard::GetData(wxDataObject& data)
{
    // Like SetData(), be forgiving and lock temporarily.
    bool tempLock = false;
    if ( !m_isOpened )
    {
        if ( !be_clipboard || !be_clipboard->Lock() )
            return false;
        tempLock = true;
    }

    bool ok = false;

    BMessage* msg = be_clipboard->Data();
    if ( msg )
    {
        // Prefer generic path: find first supported format and SetData() for it.
        ok = data.HaikuSetDataFrom(*msg);

        // Fallback: some foreign apps put text as B_STRING_TYPE under "text/plain".
        // Our HaikuSetDataFrom() checks only B_RAW_TYPE, so handle this here.
        if ( !ok )
        {
           wxTextDataObject* textObj = dynamic_cast<wxTextDataObject*>(&data);

            if ( textObj )
            {
                const char* s = nullptr;
                if ( msg->FindString("text/plain", &s) == B_OK && s )
                {
                    textObj->SetText(wxString::FromUTF8(s));
                    ok = true;
                }
            }
        }
    }

    if ( tempLock )
        be_clipboard->Unlock();

    return ok;
}

void wxClipboard::Clear()
{
    bool tempLock = false;
    if ( !m_isOpened )
    {
        if ( !be_clipboard || !be_clipboard->Lock() )
            return;
        tempLock = true;
    }

    if ( BMessage* msg = be_clipboard->Data() )
    {
        msg->MakeEmpty();
        wxHaikuClipboardCommit();
    }

    if ( tempLock )
        be_clipboard->Unlock();
}

bool wxClipboard::IsSupported(const wxDataFormat& format)
{
    // Don’t require the user to Open() just to query.
    bool tempLock = false;
    if ( !m_isOpened )
    {
        if ( !be_clipboard || !be_clipboard->Lock() )
            return false;
        tempLock = true;
    }

    bool supported = false;

    BMessage* msg = be_clipboard->Data();
    if ( msg )
    {
        if (format == wxDF_BITMAP && msg->HasMessage("image/bitmap")) supported = true;
        if (format == wxDF_FILENAME && msg->HasRef("refs")) supported = true;
        const wxString& mime = format.GetMimeType();

        if ( !mime.empty() )
        {
            // Our Haiku dataobj stores bytes as B_RAW_TYPE under mime key.
            if ( msg->HasData(mime.utf8_str(), B_ANY_TYPE) )
                supported = true;

            // Also accept common foreign text storage.
            if ( !supported && (format == wxDF_TEXT || format == wxDF_UNICODETEXT) )
            {
                if ( msg->HasData("text/plain", B_STRING_TYPE) )
                    supported = true;
            }
        }
    }

    if ( tempLock )
        be_clipboard->Unlock();

    return supported;
}

bool wxClipboard::IsSupportedAsync(wxEvtHandler* sink)
{
    wxUnusedVar(sink);
    return false; // No async clipboard notifications in this port currently.
}
