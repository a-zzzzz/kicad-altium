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

#include <widgets/property_list_panel.h>

#include <eda_draw_frame.h>
#include <eda_item.h>
#include <properties/property.h>
#include <properties/property_mgr.h>
#include <properties/property_value_text.h>
#include <tool/actions.h>
#include <tool/tool_manager.h>

#include <wx/choice.h>
#include <wx/grid.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <algorithm>
#include <set>


PROPERTY_LIST_PANEL::PROPERTY_LIST_PANEL( wxWindow* aParent, EDA_DRAW_FRAME* aFrame,
                                          const wxString& aListName ) :
        wxPanel( aParent ),
        m_frame( aFrame ),
        m_listName( aListName ),
        m_updating( false ),
        m_rebuildPending( false )
{
    // Header reads like Altium's: "View [selected objects] include [all types of objects]"
    wxBoxSizer* header = new wxBoxSizer( wxHORIZONTAL );

    m_viewChoice = new wxChoice( this, wxID_ANY );
    m_viewChoice->Append( _( "selected objects" ) );
    m_viewChoice->Append( _( "all objects" ) );
    m_viewChoice->SetSelection( 0 );

    m_kindChoice = new wxChoice( this, wxID_ANY );
    m_kindChoice->SetMinSize( FromDIP( wxSize( 180, -1 ) ) );

    header->Add( new wxStaticText( this, wxID_ANY, _( "View" ) ), 0,
                 wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP( 4 ) );
    header->Add( m_viewChoice, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP( 8 ) );
    header->Add( new wxStaticText( this, wxID_ANY, _( "include" ) ), 0,
                 wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP( 4 ) );
    header->Add( m_kindChoice, 0, wxALIGN_CENTER_VERTICAL );

    m_grid = new wxGrid( this, wxID_ANY );
    m_grid->CreateGrid( 0, 0 );
    m_grid->HideRowLabels();
    m_grid->SetSelectionMode( wxGrid::wxGridSelectRows );

    // A wheel notch scrolls whole rows (3 by default, like the other panels), not wxGrid's
    // 15 px scroll lines
    m_grid->SetScrollLineY( m_grid->GetDefaultRowSize() );

    wxBoxSizer* sizer = new wxBoxSizer( wxVERTICAL );
    sizer->Add( header, 0, wxALL, FromDIP( 4 ) );
    sizer->Add( m_grid, 1, wxEXPAND );
    SetSizer( sizer );

    m_viewChoice->Bind( wxEVT_CHOICE, [this]( wxCommandEvent& ) { Rebuild(); } );
    m_kindChoice->Bind( wxEVT_CHOICE, [this]( wxCommandEvent& ) { Rebuild(); } );
    m_grid->Bind( wxEVT_GRID_CELL_CHANGED, &PROPERTY_LIST_PANEL::onCellChanged, this );
    m_grid->Bind( wxEVT_GRID_RANGE_SELECTED, &PROPERTY_LIST_PANEL::onRangeSelected, this );
}


wxString PROPERTY_LIST_PANEL::formatValue( PROPERTY_BASE* aProperty, const wxVariant& aValue ) const
{
    return PROPERTY_VALUE_TEXT::Format( m_frame, aProperty, aValue );
}


bool PROPERTY_LIST_PANEL::parseValue( PROPERTY_BASE* aProperty, const wxVariant& aCurrent,
                                      const wxString& aText, wxVariant& aValue ) const
{
    return PROPERTY_VALUE_TEXT::Parse( m_frame, aProperty, aCurrent, aText, aValue );
}


bool PROPERTY_LIST_PANEL::isEditable( EDA_ITEM* aItem, PROPERTY_BASE* aProperty,
                                      const wxVariant& aCurrent ) const
{
    return aProperty->Writeable( aItem ) && PROPERTY_VALUE_TEXT::IsEditableAsText( aProperty, aCurrent );
}


bool PROPERTY_LIST_PANEL::showingSelection() const
{
    return m_viewChoice->GetSelection() == 0;
}


void PROPERTY_LIST_PANEL::OnSelectionChanged()
{
    if( IsShownOnScreen() && showingSelection() )
        Rebuild();
}


void PROPERTY_LIST_PANEL::ScheduleRebuild()
{
    if( m_rebuildPending || !IsShownOnScreen() )
        return;

    m_rebuildPending = true;

    CallAfter(
            [this]()
            {
                m_rebuildPending = false;
                Rebuild();
            } );
}


void PROPERTY_LIST_PANEL::Rebuild()
{
    if( !IsShownOnScreen() || !hasModel() )
        return;

    // Source objects
    std::vector<EDA_ITEM*> source = showingSelection() ? selectedItems() : allItems();

    // Kind filter: keep the choice if that kind is still present
    wxString           keepKind = m_kindChoice->GetStringSelection();
    std::set<wxString> kinds;

    for( EDA_ITEM* item : source )
        kinds.insert( item->GetFriendlyName() );

    m_kindChoice->Clear();
    m_kindChoice->Append( _( "all types of objects" ) );

    for( const wxString& kind : kinds )
        m_kindChoice->Append( kind );

    if( !keepKind.IsEmpty() && m_kindChoice->FindString( keepKind ) != wxNOT_FOUND )
        m_kindChoice->SetStringSelection( keepKind );
    else
        m_kindChoice->SetSelection( 0 );

    wxString onlyKind = m_kindChoice->GetSelection() > 0 ? m_kindChoice->GetStringSelection()
                                                         : wxString();

    m_rows.clear();

    for( EDA_ITEM* item : source )
    {
        if( onlyKind.IsEmpty() || item->GetFriendlyName() == onlyKind )
            m_rows.push_back( item );
    }

    // Columns: object kind, then every visible property of the listed objects
    PROPERTY_MANAGER&  propMgr = PROPERTY_MANAGER::Instance();
    std::set<wxString> seen;

    // The property manager's order isn't stable between runs; sort by group, then name, so
    // columns stay put
    std::vector<std::pair<wxString, wxString>> columns;   // (group, name)

    for( EDA_ITEM* item : m_rows )
    {
        for( PROPERTY_BASE* property : propMgr.GetProperties( TYPE_HASH( *item ) ) )
        {
            if( !property->IsHiddenFromPropertiesManager() && property->Available( item )
                && seen.insert( property->Name() ).second )
            {
                columns.emplace_back( property->Group(), property->Name() );
            }
        }
    }

    std::sort( columns.begin(), columns.end() );

    m_columns.assign( 1, wxString() );

    for( const auto& [group, name] : columns )
        m_columns.push_back( name );

    // Fill the grid
    m_updating = true;
    m_grid->Freeze();
    m_grid->ClearGrid();

    if( m_grid->GetNumberRows() )
        m_grid->DeleteRows( 0, m_grid->GetNumberRows() );

    if( m_grid->GetNumberCols() )
        m_grid->DeleteCols( 0, m_grid->GetNumberCols() );

    m_grid->AppendCols( (int) m_columns.size() );
    m_grid->AppendRows( (int) m_rows.size() );
    m_grid->SetColLabelValue( 0, _( "Object Kind" ) );

    for( int col = 1; col < (int) m_columns.size(); ++col )
        m_grid->SetColLabelValue( col, m_columns[col] );

    for( int row = 0; row < (int) m_rows.size(); ++row )
    {
        EDA_ITEM* item = m_rows[row];

        m_grid->SetCellValue( row, 0, item->GetFriendlyName() );
        m_grid->SetReadOnly( row, 0 );

        for( int col = 1; col < (int) m_columns.size(); ++col )
        {
            PROPERTY_BASE* property = propMgr.GetProperty( TYPE_HASH( *item ), m_columns[col] );
            wxVariant      value;

            if( property && PROPERTY_VALUE_TEXT::GetValue( item, property, value ) )
            {
                m_grid->SetCellValue( row, col, formatValue( property, value ) );
                m_grid->SetReadOnly( row, col, !isEditable( item, property, value ) );
            }
            else
            {
                m_grid->SetReadOnly( row, col );
            }
        }
    }

    m_grid->AutoSizeColumns( false );
    m_grid->Thaw();
    m_updating = false;
}


std::vector<int> PROPERTY_LIST_PANEL::selectedRows() const
{
    wxArrayInt       rows = m_grid->GetSelectedRows();
    std::vector<int> result( rows.begin(), rows.end() );

    std::sort( result.begin(), result.end() );
    return result;
}


void PROPERTY_LIST_PANEL::onCellChanged( wxGridEvent& aEvent )
{
    if( m_updating )
        return;

    int row = aEvent.GetRow();
    int col = aEvent.GetCol();

    if( row < 0 || row >= (int) m_rows.size() || col < 1 || col >= (int) m_columns.size() )
        return;

    // Altium-style bulk edit: the value goes to every selected row containing this one
    std::vector<int> targets = selectedRows();

    if( std::find( targets.begin(), targets.end(), row ) == targets.end() )
        targets = { row };

    wxString          text = m_grid->GetCellValue( row, col );
    PROPERTY_MANAGER& propMgr = PROPERTY_MANAGER::Instance();
    int               changed = 0;

    beginEdit();

    for( int target : targets )
    {
        EDA_ITEM*      item = m_rows[target];
        PROPERTY_BASE* property = propMgr.GetProperty( TYPE_HASH( *item ), m_columns[col] );
        wxVariant      current;
        wxVariant      value;

        if( !property || !PROPERTY_VALUE_TEXT::GetValue( item, property, current )
            || !isEditable( item, property, current )
            || !parseValue( property, current, text, value ) )
        {
            continue;
        }

        if( setValue( item, property, value ) )
            changed++;
    }

    endEdit( wxString::Format( _( "Edit %s in %s" ), m_columns[col], m_listName ) );

    if( !changed )
    {
        m_frame->ShowInfoBarError( wxString::Format( _( "'%s' is not a valid %s." ), text,
                                                     m_columns[col] ) );
    }

    // Show the values the objects actually have now
    CallAfter( [this]() { Rebuild(); } );
}


void PROPERTY_LIST_PANEL::onRangeSelected( wxGridRangeSelectEvent& aEvent )
{
    aEvent.Skip();

    // Picking rows selects those objects on the canvas (in "selected objects" mode the rows
    // already are the selection)
    if( m_updating || showingSelection() || !aEvent.Selecting() )
        return;

    EDA_ITEMS items;

    for( int row : selectedRows() )
        items.push_back( m_rows[row] );

    TOOL_MANAGER* toolMgr = m_frame->GetToolManager();
    toolMgr->RunAction( ACTIONS::selectionClear );

    if( !items.empty() )
        toolMgr->RunAction<EDA_ITEMS*>( ACTIONS::selectItems, &items );
}
