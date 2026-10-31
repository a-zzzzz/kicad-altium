/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <dialogs/dialog_altium_eco.h>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/filedlg.h>
#include <wx/hyperlink.h>
#include <wx/listctrl.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textfile.h>

#include <algorithm>


namespace
{
enum COLS { COL_ENABLE = 0, COL_ACTION, COL_OBJECT, COL_IN, COL_DOCUMENT, COL_CHECK, COL_DONE,
            COL_MESSAGE };

const wxString TICK = wxS( "✔" );
const wxString CROSS = wxS( "✖" );
}


DIALOG_ALTIUM_ECO::DIALOG_ALTIUM_ECO( wxWindow* aParent, std::vector<ITEM> aItems,
                                      HANDLER aExecute, HANDLER aValidate ) :
        DIALOG_SHIM( aParent, wxID_ANY, _( "Engineering Change Order" ), wxDefaultPosition,
                     wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxMAXIMIZE_BOX ),
        m_items( std::move( aItems ) ),
        m_execute( std::move( aExecute ) ),
        m_validate( std::move( aValidate ) ),
        m_executed( false ),
        m_filling( false )
{
    for( const ITEM& item : m_items )
    {
        if( std::find( m_groups.begin(), m_groups.end(), item.group ) == m_groups.end() )
            m_groups.push_back( item.group );
    }

    wxBoxSizer* main = new wxBoxSizer( wxVERTICAL );

    wxBoxSizer* titles = new wxBoxSizer( wxHORIZONTAL );
    titles->Add( new wxStaticText( this, wxID_ANY, _( "Modifications" ) ), 1 );
    titles->Add( new wxStaticText( this, wxID_ANY, _( "Status" ) ), 0, wxRIGHT,
                 FromDIP( 320 ) );
    main->Add( titles, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8 );

    m_list = new wxListCtrl( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT );
    m_list->EnableCheckBoxes( true );

    m_list->AppendColumn( _( "Enable" ), wxLIST_FORMAT_LEFT, FromDIP( 64 ) );
    m_list->AppendColumn( _( "Action" ), wxLIST_FORMAT_LEFT, FromDIP( 200 ) );
    m_list->AppendColumn( _( "Affected Object" ), wxLIST_FORMAT_LEFT, FromDIP( 220 ) );
    m_list->AppendColumn( wxEmptyString, wxLIST_FORMAT_LEFT, FromDIP( 40 ) );
    m_list->AppendColumn( _( "Affected Document" ), wxLIST_FORMAT_LEFT, FromDIP( 200 ) );
    m_list->AppendColumn( _( "Check" ), wxLIST_FORMAT_CENTER, FromDIP( 56 ) );
    m_list->AppendColumn( _( "Done" ), wxLIST_FORMAT_CENTER, FromDIP( 56 ) );
    m_list->AppendColumn( _( "Message" ), wxLIST_FORMAT_LEFT, FromDIP( 220 ) );
    m_list->SetMinSize( FromDIP( wxSize( 900, 360 ) ) );
    main->Add( m_list, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8 );

    wxBoxSizer* warning = new wxBoxSizer( wxHORIZONTAL );
    m_warning = new wxStaticText( this, wxID_ANY, wxEmptyString );
    m_warning->SetForegroundColour( wxColour( 230, 100, 90 ) );
    m_link = new wxHyperlinkCtrl( this, wxID_ANY, wxS( " " ), wxEmptyString );
    warning->Add( m_warning, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6 );
    warning->Add( m_link, 0, wxALIGN_CENTER_VERTICAL );
    main->Add( warning, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8 );
    m_warning->Hide();
    m_link->Hide();

    wxBoxSizer* buttons = new wxBoxSizer( wxHORIZONTAL );
    wxButton*   validate = new wxButton( this, wxID_ANY, _( "Validate Changes" ) );
    wxButton*   execute = new wxButton( this, wxID_ANY, _( "Execute Changes" ) );
    wxButton*   report = new wxButton( this, wxID_ANY, _( "Report Changes..." ) );
    m_onlyErrors = new wxCheckBox( this, wxID_ANY, _( "Only Show Errors" ) );

    buttons->Add( validate, 0, wxRIGHT, 4 );
    buttons->Add( execute, 0, wxRIGHT, 4 );
    buttons->Add( report, 0, wxRIGHT, 8 );
    buttons->Add( m_onlyErrors, 0, wxALIGN_CENTER_VERTICAL );
    buttons->AddStretchSpacer();
    buttons->Add( new wxButton( this, wxID_CANCEL, _( "Close" ) ), 0 );
    main->Add( buttons, 0, wxEXPAND | wxALL, 8 );

    SetSizer( main );

    validate->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { this->validate(); } );
    execute->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { this->execute(); } );
    report->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { this->report(); } );
    m_onlyErrors->Bind( wxEVT_CHECKBOX, [this]( wxCommandEvent& ) { fill(); } );
    m_link->Bind( wxEVT_HYPERLINK,
                  [this]( wxHyperlinkEvent& )
                  {
                      if( m_onLink )
                          m_onLink();
                  } );

    auto onCheck =
            [this]( wxListEvent& aEvent, bool aEnable )
            {
                if( m_filling )
                    return;

                long row = aEvent.GetIndex();

                if( row >= 0 && row < (long) m_rowItem.size() && !m_executed )
                {
                    int ref = m_rowItem[row];

                    if( ref < 0 )
                    {
                        // A heading enables or disables its group
                        const wxString& group = m_groups[-ref - 1];

                        for( ITEM& item : m_items )
                        {
                            if( item.group == group )
                                item.enabled = aEnable;
                        }
                    }
                    else
                    {
                        m_items[ref].enabled = aEnable;
                    }
                }

                // Rebuilt after the event, as the list must not change during it
                CallAfter( [this]() { fill(); } );
            };

    m_list->Bind( wxEVT_LIST_ITEM_CHECKED, [onCheck]( wxListEvent& e ) { onCheck( e, true ); } );
    m_list->Bind( wxEVT_LIST_ITEM_UNCHECKED, [onCheck]( wxListEvent& e ) { onCheck( e, false ); } );

    fill();

    execute->SetDefault();
    finishDialogSettings();
    SetSize( FromDIP( wxSize( 1080, 520 ) ) );
    Centre();
}


void DIALOG_ALTIUM_ECO::SetWarning( const wxString& aText, const wxString& aLinkText,
                                    std::function<void()> aOnLink )
{
    m_warning->SetLabel( aText );
    m_warning->Show( !aText.IsEmpty() );
    m_link->SetLabel( aLinkText );
    m_link->Show( !aLinkText.IsEmpty() );
    m_onLink = std::move( aOnLink );
    Layout();
}


void DIALOG_ALTIUM_ECO::fill()
{
    m_filling = true;
    m_list->Freeze();
    m_list->DeleteAllItems();
    m_rowItem.clear();

    bool onlyErrors = m_onlyErrors->GetValue();

    for( size_t gg = 0; gg < m_groups.size(); ++gg )
    {
        const wxString& group = m_groups[gg];
        int             count = 0;
        bool            allEnabled = true;

        for( const ITEM& item : m_items )
        {
            if( item.group == group )
            {
                count++;
                allEnabled &= item.enabled;
            }
        }

        long head = m_list->InsertItem( m_list->GetItemCount(), wxS( "◢" ) );
        m_list->SetItem( head, COL_ACTION, wxString::Format( wxS( "%s(%d)" ), group, count ) );
        m_list->CheckItem( head, allEnabled );
        m_list->SetItemBackgroundColour( head, m_list->GetBackgroundColour().ChangeLightness( 112 ) );
        m_rowItem.push_back( -(int) gg - 1 );

        for( size_t ii = 0; ii < m_items.size(); ++ii )
        {
            const ITEM& item = m_items[ii];

            if( item.group != group || ( onlyErrors && !item.failed ) )
                continue;

            long row = m_list->InsertItem( m_list->GetItemCount(), wxEmptyString );
            m_list->SetItem( row, COL_ACTION, wxS( "    " ) + item.action );
            m_list->SetItem( row, COL_OBJECT, item.object );
            m_list->SetItem( row, COL_IN, item.preposition.IsEmpty() ? _( "In" ) : item.preposition );
            m_list->SetItem( row, COL_DOCUMENT, item.document );
            m_list->SetItem( row, COL_CHECK, item.failed ? CROSS : item.checked ? TICK : wxString() );
            m_list->SetItem( row, COL_DONE, item.done ? TICK : wxString() );
            m_list->SetItem( row, COL_MESSAGE, item.message );
            m_list->CheckItem( row, item.enabled );

            if( item.failed )
                m_list->SetItemTextColour( row, wxColour( 255, 105, 97 ) );
            else if( item.done )
                m_list->SetItemTextColour( row, wxColour( 120, 210, 120 ) );

            m_rowItem.push_back( (int) ii );
        }
    }

    m_list->Thaw();
    m_filling = false;
}


void DIALOG_ALTIUM_ECO::validate()
{
    std::vector<int> enabled;

    for( size_t ii = 0; ii < m_items.size(); ++ii )
    {
        ITEM& item = m_items[ii];

        item.checked = false;

        if( !item.done )
        {
            item.failed = false;
            item.message.clear();
        }

        if( item.enabled )
            enabled.push_back( (int) ii );
    }

    if( m_validate )
        m_validate( m_items, enabled );

    for( int ii : enabled )
        m_items[ii].checked = !m_items[ii].failed;

    fill();
}


void DIALOG_ALTIUM_ECO::execute()
{
    if( m_executed )
        return;

    validate();

    std::vector<int> toApply;

    for( size_t ii = 0; ii < m_items.size(); ++ii )
    {
        if( m_items[ii].enabled && !m_items[ii].failed )
            toApply.push_back( (int) ii );
    }

    if( !toApply.empty() && m_execute )
        m_execute( m_items, toApply );

    for( int ii : toApply )
    {
        if( !m_items[ii].failed )
            m_items[ii].done = true;
    }

    m_executed = true;
    fill();
}


void DIALOG_ALTIUM_ECO::report()
{
    wxFileDialog dlg( this, _( "Report Changes" ), wxEmptyString, wxS( "ECO Report.txt" ),
                      _( "Text files" ) + wxS( " (*.txt)|*.txt" ),
                      wxFD_SAVE | wxFD_OVERWRITE_PROMPT );

    if( dlg.ShowModal() != wxID_OK )
        return;

    wxTextFile file( dlg.GetPath() );

    if( file.Exists() )
        file.Open();
    else
        file.Create();

    file.Clear();
    file.AddLine( _( "Engineering Change Order" ) );

    for( const wxString& group : m_groups )
    {
        file.AddLine( wxEmptyString );
        file.AddLine( group );

        for( const ITEM& item : m_items )
        {
            if( item.group != group )
                continue;

            file.AddLine( wxString::Format( wxS( "\t%s\t%s\t%s\t%s\t%s\t%s\t%s" ),
                                            item.enabled ? _( "Enabled" ) : _( "Disabled" ),
                                            item.action, item.object,
                                            item.preposition.IsEmpty() ? _( "In" ) : item.preposition,
                                            item.document,
                                            item.done ? _( "Done" ) : item.failed ? _( "Failed" ) : wxString(),
                                            item.message ) );
        }
    }

    file.Write();
    file.Close();
}
