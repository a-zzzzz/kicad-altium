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

#include <dialogs/dialog_find_similar_objects.h>

#include <eda_draw_frame.h>
#include <eda_item.h>
#include <properties/property.h>
#include <properties/property_mgr.h>
#include <properties/property_value_text.h>

#include <algorithm>
#include <set>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/grid.h>
#include <wx/sizer.h>
#include <wx/stattext.h>


namespace
{
// Match column choices, in display order
enum MATCH
{
    MATCH_ANY = 0,
    MATCH_SAME,
    MATCH_DIFFERENT
};

const int COL_PROPERTY = 0;
const int COL_VALUE = 1;
const int COL_MATCH = 2;

wxArrayString matchChoices()
{
    wxArrayString choices;
    choices.Add( _( "Any" ) );
    choices.Add( _( "Same" ) );
    choices.Add( _( "Different" ) );
    return choices;
}
}


DIALOG_FIND_SIMILAR_OBJECTS::DIALOG_FIND_SIMILAR_OBJECTS( EDA_DRAW_FRAME* aFrame,
                                                          EDA_ITEM* aReference,
                                                          const std::vector<EDA_ITEM*>& aCandidates,
                                                          FORMATTER aFormatter ) :
        DIALOG_SHIM( aFrame, wxID_ANY, _( "Find Similar Objects" ), wxDefaultPosition,
                     wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER ),
        m_frame( aFrame ),
        m_reference( aReference )
{
    if( !aFormatter )
    {
        aFormatter = [aFrame]( PROPERTY_BASE* aProperty, const wxVariant& aValue )
                     {
                         return PROPERTY_VALUE_TEXT::Format( aFrame, aProperty, aValue );
                     };
    }

    // Only objects of the same kind are compared
    for( EDA_ITEM* candidate : aCandidates )
    {
        if( candidate && candidate->Type() == aReference->Type() )
            m_candidates.push_back( candidate );
    }

    // Rows: every property the Properties panel would show for this object, in its groups
    PROPERTY_MANAGER&  propMgr = PROPERTY_MANAGER::Instance();
    std::set<wxString> seen;

    for( PROPERTY_BASE* property : propMgr.GetProperties( TYPE_HASH( *aReference ) ) )
    {
        wxVariant value;

        // Base and derived classes can both register a property; list each name once
        if( property->IsHiddenFromPropertiesManager() || !seen.insert( property->Name() ).second )
            continue;

        if( PROPERTY_VALUE_TEXT::GetValue( aReference, property, value ) )
            m_rows.push_back( { property, value } );
    }

    std::stable_sort( m_rows.begin(), m_rows.end(),
                      []( const ROW& a, const ROW& b )
                      {
                          return a.property->Group() < b.property->Group();
                      } );

    wxBoxSizer* mainSizer = new wxBoxSizer( wxVERTICAL );

    mainSizer->Add( new wxStaticText( this, wxID_ANY,
                                      wxString::Format( _( "Select objects like: %s" ),
                                                        aReference->GetItemDescription( aFrame, true ) ) ),
                    0, wxALL, FromDIP( 8 ) );

    // Grid rows: a header row per property group (the ungrouped ones under "Object Specific"),
    // then the group's properties
    for( int ii = 0; ii < (int) m_rows.size(); ++ii )
    {
        if( ii == 0 || m_rows[ii].property->Group() != m_rows[ii - 1].property->Group() )
            m_gridRows.push_back( -1 );

        m_gridRows.push_back( ii );
    }

    m_grid = new wxGrid( this, wxID_ANY );
    m_grid->CreateGrid( (int) m_gridRows.size(), 3 );
    m_grid->SetColLabelValue( COL_PROPERTY, _( "Property" ) );
    m_grid->SetColLabelValue( COL_VALUE, _( "Value" ) );
    m_grid->SetColLabelValue( COL_MATCH, _( "Match" ) );
    m_grid->HideRowLabels();
    m_grid->EnableDragRowSize( false );

    wxFont   headerFont = m_grid->GetDefaultCellFont().Bold();
    wxColour headerBg = m_grid->GetLabelBackgroundColour();

    for( int row = 0; row < (int) m_gridRows.size(); ++row )
    {
        m_grid->SetReadOnly( row, COL_PROPERTY );
        m_grid->SetReadOnly( row, COL_VALUE );

        if( m_gridRows[row] < 0 )
        {
            const ROW& next = m_rows[m_gridRows[row + 1]];
            wxString   group = next.property->Group();

            m_grid->SetCellValue( row, COL_PROPERTY,
                                  group.IsEmpty() ? _( "Object Specific" ) : wxGetTranslation( group ) );
            m_grid->SetCellSize( row, COL_PROPERTY, 1, 3 );
            m_grid->SetCellFont( row, COL_PROPERTY, headerFont );
            m_grid->SetCellBackgroundColour( row, COL_PROPERTY, headerBg );
            m_grid->SetReadOnly( row, COL_MATCH );
            continue;
        }

        const ROW& r = m_rows[m_gridRows[row]];

        m_grid->SetCellValue( row, COL_PROPERTY, wxS( "    " ) + wxGetTranslation( r.property->Name() ) );
        m_grid->SetCellValue( row, COL_VALUE, aFormatter( r.property, r.value ) );
        m_grid->SetCellEditor( row, COL_MATCH, new wxGridCellChoiceEditor( matchChoices() ) );
        m_grid->SetCellValue( row, COL_MATCH, matchChoices()[MATCH_ANY] );
    }

    m_grid->AutoSizeColumns();

    // The Value column takes the spare width, so the table fills the dialog
    m_grid->Bind( wxEVT_SIZE,
                  [this]( wxSizeEvent& aEvent )
                  {
                      aEvent.Skip();
                      int spare = m_grid->GetClientSize().x - m_grid->GetColSize( COL_PROPERTY )
                                  - m_grid->GetColSize( COL_MATCH ) - 1;

                      if( spare > FromDIP( 80 ) )
                          m_grid->SetColSize( COL_VALUE, spare );
                  } );
    m_grid->SetMinSize( FromDIP( wxSize( 520, 360 ) ) );
    mainSizer->Add( m_grid, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP( 8 ) );

    m_clearExisting = new wxCheckBox( this, wxID_ANY, _( "Clear existing selection" ) );
    m_clearExisting->SetValue( true );
    m_zoomToMatches = new wxCheckBox( this, wxID_ANY, _( "Zoom to matching" ) );

    wxBoxSizer* optionsSizer = new wxBoxSizer( wxHORIZONTAL );
    optionsSizer->Add( m_clearExisting, 0, wxRIGHT, FromDIP( 16 ) );
    optionsSizer->Add( m_zoomToMatches, 0 );
    mainSizer->Add( optionsSizer, 0, wxALL, FromDIP( 8 ) );

    wxStdDialogButtonSizer* buttons = new wxStdDialogButtonSizer();
    buttons->AddButton( new wxButton( this, wxID_OK ) );
    buttons->AddButton( new wxButton( this, wxID_CANCEL ) );
    buttons->Realize();
    mainSizer->Add( buttons, 0, wxEXPAND | wxALL, FromDIP( 8 ) );

    SetSizer( mainSizer );
    SetupStandardButtons();
    finishDialogSettings();
}


bool DIALOG_FIND_SIMILAR_OBJECTS::TransferDataFromWindow()
{
    m_grid->SaveEditControlValue();

    std::vector<std::pair<const ROW*, MATCH>> rules;
    wxArrayString                             choices = matchChoices();

    for( int row = 0; row < (int) m_gridRows.size(); ++row )
    {
        if( m_gridRows[row] < 0 )
            continue;

        int match = choices.Index( m_grid->GetCellValue( row, COL_MATCH ) );

        if( match == MATCH_SAME || match == MATCH_DIFFERENT )
            rules.emplace_back( &m_rows[m_gridRows[row]], static_cast<MATCH>( match ) );
    }

    m_matches.clear();

    for( EDA_ITEM* candidate : m_candidates )
    {
        bool ok = true;

        for( const auto& [rule, match] : rules )
        {
            wxVariant value;
            bool      same = PROPERTY_VALUE_TEXT::GetValue( candidate, rule->property, value )
                             && value == rule->value;

            if( same != ( match == MATCH_SAME ) )
            {
                ok = false;
                break;
            }
        }

        if( ok )
            m_matches.push_back( candidate );
    }

    return true;
}


bool DIALOG_FIND_SIMILAR_OBJECTS::ClearExistingSelection() const
{
    return m_clearExisting->GetValue();
}


bool DIALOG_FIND_SIMILAR_OBJECTS::ZoomToMatches() const
{
    return m_zoomToMatches->GetValue();
}
