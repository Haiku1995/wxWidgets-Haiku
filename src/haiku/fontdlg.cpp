/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/fontdlg.cpp
// Purpose:     Haiku-native font dialog using real BFont API
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#if wxUSE_FONTDLG

#include "wx/fontdlg.h"
#include "wx/font.h"
#include "wx/log.h"

#include <Application.h>
#include <Window.h>
#include <View.h>
#include <MenuField.h>
#include <PopUpMenu.h>
#include <MenuItem.h>
#include <TextControl.h>
#include <StringView.h>
#include <Button.h>
#include <GroupLayout.h>
#include <Font.h>
#include <stdlib.h>
#include <string.h>

// ----------------------------------------------------------------------------
// Private implementation
// ----------------------------------------------------------------------------

class wxHaikuFontDialogImpl : public BWindow
{
public:
    wxHaikuFontDialogImpl(wxFontDialog* owner, const wxFontData& data)
        : BWindow(BRect(100, 100, 500, 360),
                  "Choose Font",
                  B_TITLED_WINDOW_LOOK,
                  B_MODAL_APP_WINDOW_FEEL,
                  B_ASYNCHRONOUS_CONTROLS |
                  B_NOT_ZOOMABLE |
                  B_AUTO_UPDATE_SIZE_LIMITS |
                  B_CLOSE_ON_ESCAPE),
          m_owner(owner),
          m_initialFont(data.GetInitialFont())
    {
        BuildUI();
        PopulateFamilies();
        SelectInitialFont();
    }

    bool QuitRequested() override
{
    if ( m_owner )
        m_owner->SetReturnCode(wxID_CANCEL);

    Hide();
    return false;
}

    void MessageReceived(BMessage* msg) override
    {
        switch ( msg ? msg->what : 0 )
        {
            case 'famc':
            {
                const char* family = nullptr;
                if ( msg->FindString("family", &family) == B_OK )
                {
                    m_selectedFamily = family;
                    OnFamilyChanged();
                }
                break;
            }

            case 'styl':
            {
                const char* style = nullptr;
                if ( msg->FindString("style", &style) == B_OK )
                {
                    m_selectedStyle = style;
                    UpdatePreview();
                }
                break;
            }

            case 'size':
                UpdatePreview();
                break;

            case 'ok__':
              ApplySelection();
              if (m_owner)
              m_owner->SetReturnCode(wxID_OK);
              Hide();
              break;

             case 'cncl':
             if (m_owner)
             m_owner->SetReturnCode(wxID_CANCEL);
             Hide();
             break;

            default:
                BWindow::MessageReceived(msg);
                break;
        }
    }

private:
    void BuildUI()
    {
        BView* root = new BView(Bounds(), "root", B_FOLLOW_ALL, B_WILL_DRAW);
        root->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
        AddChild(root);

        BGroupLayout* mainLayout = new BGroupLayout(B_VERTICAL, 8.0f);
        root->SetLayout(mainLayout);

        // Family
        {
            BView* row = new BView("famView", B_WILL_DRAW);
            row->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

            BGroupLayout* layout = new BGroupLayout(B_HORIZONTAL, 6.0f);
            row->SetLayout(layout);

            layout->AddView(new BStringView("famLbl", "Family:"));

            m_familyMenu = new BPopUpMenu("Families");
            m_familyField = new BMenuField("family", "", m_familyMenu);
            layout->AddView(m_familyField, 1.0f);

            mainLayout->AddView(row);
        }

        // Style
        {
            BView* row = new BView("styleView", B_WILL_DRAW);
            row->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

            BGroupLayout* layout = new BGroupLayout(B_HORIZONTAL, 6.0f);
            row->SetLayout(layout);

            layout->AddView(new BStringView("styleLbl", "Style:"));

            m_styleMenu = new BPopUpMenu("Styles");
            m_styleField = new BMenuField("style", "", m_styleMenu);
            layout->AddView(m_styleField, 1.0f);

            mainLayout->AddView(row);
        }

        // Size
        {
            BView* row = new BView("sizeView", B_WILL_DRAW);
            row->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

            BGroupLayout* layout = new BGroupLayout(B_HORIZONTAL, 6.0f);
            row->SetLayout(layout);

            layout->AddView(new BStringView("sizeLbl", "Size:"));
            m_sizeCtrl = new BTextControl("size", "", "12", new BMessage('size'));
            layout->AddView(m_sizeCtrl, 1.0f);

            mainLayout->AddView(row);
        }

        // Preview
        m_preview = new BStringView("preview", "AaBbYyZz 0123");
        m_preview->SetExplicitMinSize(BSize(0, 48));
        mainLayout->AddView(m_preview);

        // Buttons
        {
            BView* row = new BView("btnView", B_WILL_DRAW);
            row->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

            BGroupLayout* layout = new BGroupLayout(B_HORIZONTAL, 6.0f);
            row->SetLayout(layout);

            BView* spacer = new BView("spacer", B_WILL_DRAW);
            spacer->SetExplicitMinSize(BSize(1, 1));
            layout->AddView(spacer, 1.0f);

            BButton* ok = new BButton("ok", "OK", new BMessage('ok__'));
            BButton* cancel = new BButton("cancel", "Cancel", new BMessage('cncl'));
            layout->AddView(ok);
            layout->AddView(cancel);

            if ( ok )
                ok->MakeDefault(true);

            mainLayout->AddView(row);
        }
    }

    void PopulateFamilies()
    {
        m_familyMenu->RemoveItems(0, m_familyMenu->CountItems(), true);

        const int32 count = count_font_families();
        for ( int32 i = 0; i < count; ++i )
        {
            font_family name;
            if ( get_font_family(i, &name) == B_OK )
            {
                BMessage* msg = new BMessage('famc');
                msg->AddString("family", name);
                m_familyMenu->AddItem(new BMenuItem(name, msg));
            }
        }
    }

    void PopulateStyles(const char* family)
    {
        char mutableFamily[B_FONT_FAMILY_LENGTH];
        strlcpy(mutableFamily, family, sizeof(mutableFamily));

        m_styleMenu->RemoveItems(0, m_styleMenu->CountItems(), true);

        const int32 count = count_font_styles(mutableFamily);
        for ( int32 i = 0; i < count; ++i )
        {
            font_style name;
            if ( get_font_style(mutableFamily, i, &name) == B_OK )
            {
                BMessage* msg = new BMessage('styl');
                msg->AddString("style", name);
                m_styleMenu->AddItem(new BMenuItem(name, msg));
            }
        }

        if ( count > 0 && m_styleMenu->ItemAt(0) )
        {
            m_styleMenu->ItemAt(0)->SetMarked(true);
            m_selectedStyle = m_styleMenu->ItemAt(0)->Label();
        }
        else
        {
            m_selectedStyle.clear();
        }
    }

    void SelectInitialFont()
    {
        BFont initial = m_initialFont.IsOk() ? m_initialFont.GetHandle() : *be_plain_font;

        font_family family;
        font_style style;
        initial.GetFamilyAndStyle(&family, &style);

        m_selectedFamily = family;
        m_selectedStyle = style;

        for ( int32 i = 0; i < m_familyMenu->CountItems(); ++i )
        {
            BMenuItem* item = m_familyMenu->ItemAt(i);
            if ( item && strcmp(item->Label(), family) == 0 )
            {
                item->SetMarked(true);
                break;
            }
        }

        PopulateStyles(family);

        for ( int32 i = 0; i < m_styleMenu->CountItems(); ++i )
        {
            BMenuItem* item = m_styleMenu->ItemAt(i);
            if ( item && strcmp(item->Label(), style) == 0 )
            {
                item->SetMarked(true);
                break;
            }
        }

        if ( m_sizeCtrl )
            m_sizeCtrl->SetText(wxString::Format("%.1f", initial.Size()).mb_str());

        UpdatePreview();
    }

    void OnFamilyChanged()
    {
        PopulateStyles(m_selectedFamily.c_str());
        UpdatePreview();
    }

    void UpdatePreview()
    {
        BFont font;
        if ( font.SetFamilyAndStyle(m_selectedFamily.c_str(), m_selectedStyle.c_str()) == B_OK )
        {
            const float size = (float)atof(m_sizeCtrl ? m_sizeCtrl->Text() : "12");
            font.SetSize(size > 0.0f ? size : 12.0f);

            if ( m_preview )
            {
                m_preview->SetFont(&font);
                m_preview->Invalidate();
            }
        }
    }

    void ApplySelection()
    {
        BFont nativeFont;
        if ( nativeFont.SetFamilyAndStyle(m_selectedFamily.c_str(), m_selectedStyle.c_str()) == B_OK )
        {
            const float size = (float)atof(m_sizeCtrl ? m_sizeCtrl->Text() : "12");
            nativeFont.SetSize(size > 0.0f ? size : 12.0f);

            wxFont wxfont(nativeFont);
            m_owner->m_fontData.SetChosenFont(wxfont);
        }
    }

private:
    wxFontDialog*  m_owner;
    wxFont         m_initialFont;
    std::string    m_selectedFamily;
    std::string    m_selectedStyle;

    BMenuField*    m_familyField = nullptr;
    BPopUpMenu*    m_familyMenu = nullptr;
    BMenuField*    m_styleField = nullptr;
    BPopUpMenu*    m_styleMenu = nullptr;
    BTextControl*  m_sizeCtrl = nullptr;
    BStringView*   m_preview = nullptr;
};

// ----------------------------------------------------------------------------
// wxFontDialog public interface
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(wxFontDialog, wxFontDialogBase);

bool wxFontDialog::Create(wxWindow* WXUNUSED(parent), const wxFontData& data)
{
    m_fontData = data;
    return true;
}

int wxFontDialog::ShowModal()
{
    if ( !m_impl )
        m_impl = new wxHaikuFontDialogImpl(this, m_fontData);

    SetReturnCode(wxID_CANCEL);

    m_impl->Show();

    while ( m_impl && !m_impl->IsHidden() )
        snooze(10000);

    const int rc = GetReturnCode();

    // Do not delete a BWindow directly here.
    if ( m_impl )
    {
        m_impl->PostMessage(B_QUIT_REQUESTED);
        m_impl = nullptr;
    }

    return rc;
}

#endif // wxUSE_FONTDLG
