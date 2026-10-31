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

#include "dialog_variant_manager.h"

#include <algorithm>
#include <set>

#include <confirm.h>
#include <lib_symbol.h>
#include <sch_commit.h>
#include <sch_edit_frame.h>
#include <sch_field.h>
#include <sch_reference_list.h>
#include <sch_screen.h>
#include <sch_symbol.h>
#include <schematic.h>
#include <string_utils.h>

#include <wx/button.h>
#include <wx/grid.h>
#include <wx/menu.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textdlg.h>


namespace
{

enum FIXED_COLUMNS
{
    COL_DESIGNATOR,
    COL_COMMENT,
    COL_FOOTPRINT,
    COL_DESCRIPTION,
    FIXED_COLS
};


wxString stateFitted()     { return _( "Fitted" ); }
wxString stateNotFitted()  { return _( "Not Fitted" ); }
wxString stateAlternate()  { return _( "Alternate" ); }


wxColour tint( const wxColour& aBase, const wxColour& aTint, int aPercent )
{
    auto mix = [&]( int a, int b ) { return ( a * ( 100 - aPercent ) + b * aPercent ) / 100; };
    return wxColour( mix( aBase.Red(), aTint.Red() ), mix( aBase.Green(), aTint.Green() ),
                     mix( aBase.Blue(), aTint.Blue() ) );
}


/// Parameter variations of one component in one variant: field name, base value, variant value.
class DIALOG_PARAMETER_VARIATIONS : public DIALOG_SHIM
{
public:
    DIALOG_PARAMETER_VARIATIONS( wxWindow* aParent, const wxString& aTitle,
                                 const std::vector<std::pair<wxString, wxString>>& aBase,
                                 const std::map<wxString, wxString>& aOverrides ) :
            DIALOG_SHIM( aParent, wxID_ANY, aTitle, wxDefaultPosition, wxDefaultSize,
                         wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER,
                         wxS( "DIALOG_PARAMETER_VARIATIONS" ) ),
            m_base( aBase )
    {
        wxBoxSizer* main = new wxBoxSizer( wxVERTICAL );

        wxStaticText* hint = new wxStaticText( this, wxID_ANY,
                _( "Values that differ from the base design are this variant's alternate part "
                   "data.  Clear a value back to the base value to remove the variation." ) );
        hint->Wrap( FromDIP( 520 ) );
        main->Add( hint, 0, wxEXPAND | wxALL, 8 );

        m_grid = new wxGrid( this, wxID_ANY );
        m_grid->CreateGrid( (int) aBase.size(), 3 );
        m_grid->SetRowLabelSize( 0 );
        m_grid->SetColLabelValue( 0, _( "Parameter" ) );
        m_grid->SetColLabelValue( 1, _( "Base Value" ) );
        m_grid->SetColLabelValue( 2, _( "Variant Value" ) );
        m_grid->SetColSize( 0, FromDIP( 140 ) );
        m_grid->SetColSize( 1, FromDIP( 200 ) );
        m_grid->SetColSize( 2, FromDIP( 200 ) );

        wxColour bg = wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOW );
        wxColour hl = tint( bg, wxColour( 60, 120, 230 ), 25 );

        for( int row = 0; row < (int) aBase.size(); ++row )
        {
            const auto& [name, value] = aBase[row];
            auto        it = aOverrides.find( name );

            m_grid->SetCellValue( row, 0, name );
            m_grid->SetCellValue( row, 1, value );
            m_grid->SetCellValue( row, 2, it != aOverrides.end() ? it->second : value );
            m_grid->SetReadOnly( row, 0 );
            m_grid->SetReadOnly( row, 1 );

            if( it != aOverrides.end() )
                m_grid->SetCellBackgroundColour( row, 2, hl );
        }

        main->Add( m_grid, 1, wxEXPAND | wxLEFT | wxRIGHT, 8 );
        main->Add( CreateStdDialogButtonSizer( wxOK | wxCANCEL ), 0, wxEXPAND | wxALL, 8 );
        SetSizer( main );
        SetupStandardButtons();
        m_grid->SetMinSize( FromDIP( wxSize( 400, 200 ) ) );
        finishDialogSettings();
        SetSize( FromDIP( wxSize( 580, 420 ) ) );
    }

    std::map<wxString, wxString> GetOverrides()
    {
        m_grid->SaveEditControlValue();

        std::map<wxString, wxString> result;

        for( int row = 0; row < (int) m_base.size(); ++row )
        {
            wxString value = m_grid->GetCellValue( row, 2 );

            if( value != m_base[row].second )
                result[m_base[row].first] = value;
        }

        return result;
    }

private:
    std::vector<std::pair<wxString, wxString>> m_base;
    wxGrid*                                    m_grid;
};

} // namespace


DIALOG_VARIANT_MANAGER::DIALOG_VARIANT_MANAGER( SCH_EDIT_FRAME* aParent ) :
        DIALOG_SHIM( aParent, wxID_ANY, _( "Variant Management" ), wxDefaultPosition,
                     wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER,
                     wxS( "DIALOG_VARIANT_MANAGER" ) ),
        m_frame( aParent )
{
    wxBoxSizer* main = new wxBoxSizer( wxVERTICAL );

    m_info = new wxStaticText( this, wxID_ANY, wxEmptyString );
    main->Add( m_info, 0, wxEXPAND | wxALL, 8 );

    m_grid = new wxGrid( this, wxID_ANY );
    m_grid->CreateGrid( 0, FIXED_COLS );
    m_grid->SetRowLabelSize( 0 );
    m_grid->SetColLabelValue( COL_DESIGNATOR, _( "Designator" ) );
    m_grid->SetColLabelValue( COL_COMMENT, _( "Comment" ) );
    m_grid->SetColLabelValue( COL_FOOTPRINT, _( "Footprint" ) );
    m_grid->SetColLabelValue( COL_DESCRIPTION, _( "Description" ) );
    m_grid->SetColSize( COL_DESIGNATOR, FromDIP( 80 ) );
    m_grid->SetColSize( COL_COMMENT, FromDIP( 110 ) );
    m_grid->SetColSize( COL_FOOTPRINT, FromDIP( 170 ) );
    m_grid->SetColSize( COL_DESCRIPTION, FromDIP( 170 ) );
    m_grid->SetColLabelSize( FromDIP( 40 ) );
    m_grid->EnableDragRowSize( false );
    m_grid->SetColMinimalAcceptableWidth( FromDIP( 40 ) );
    main->Add( m_grid, 1, wxEXPAND | wxLEFT | wxRIGHT, 8 );

    wxBoxSizer* buttons = new wxBoxSizer( wxHORIZONTAL );

    auto addButton =
            [&]( const wxString& aLabel, const wxString& aTip, std::function<void()> aAction )
            {
                wxButton* btn = new wxButton( this, wxID_ANY, aLabel );
                btn->SetToolTip( aTip );
                btn->Bind( wxEVT_BUTTON, [aAction]( wxCommandEvent& ) { aAction(); } );
                buttons->Add( btn, 0, wxRIGHT, 4 );
            };

    auto currentVariant =
            [this]() -> int
            {
                return variantCol( m_grid->GetGridCursorCol() );
            };

    addButton( _( "Add Variant..." ), _( "Add a new variant" ), [this]() { addVariant(); } );
    addButton( _( "Rename Variant..." ), _( "Rename the variant of the current column" ),
               [this, currentVariant]() { renameVariant( currentVariant() ); } );
    addButton( _( "Remove Variant" ), _( "Remove the variant of the current column" ),
               [this, currentVariant]() { removeVariant( currentVariant() ); } );
    addButton( _( "Edit Description..." ), _( "Edit the description of the current variant" ),
               [this, currentVariant]() { describeVariant( currentVariant() ); } );
    buttons->AddSpacer( FromDIP( 16 ) );
    addButton( _( "Edit Component Parameter Variations..." ),
               _( "Alternate part data for the current component in the current variant" ),
               [this, currentVariant]()
               {
                   int row = m_grid->GetGridCursorRow();
                   int var = currentVariant();

                   if( row >= 0 && var >= 0 )
                       editParameters( row, var );
                   else
                       wxBell();
               } );

    main->Add( buttons, 0, wxEXPAND | wxALL, 8 );
    main->Add( CreateStdDialogButtonSizer( wxOK | wxCANCEL ), 0, wxEXPAND | wxALL, 8 );

    SetSizer( main );
    SetupStandardButtons();

    m_grid->Bind( wxEVT_GRID_CELL_CHANGED, &DIALOG_VARIANT_MANAGER::onCellChanged, this );
    m_grid->Bind( wxEVT_GRID_CELL_LEFT_DCLICK, &DIALOG_VARIANT_MANAGER::onCellDClick, this );
    m_grid->Bind( wxEVT_GRID_CELL_RIGHT_CLICK, &DIALOG_VARIANT_MANAGER::onCellRightClick, this );
    m_grid->Bind( wxEVT_GRID_LABEL_RIGHT_CLICK, &DIALOG_VARIANT_MANAGER::onLabelRightClick, this );

    loadData();
    fillGrid();

    // finishDialogSettings() sizes the dialog to its sizer's minimum, which for a grid is all of
    // its rows and columns: cap the grid's minimum and size the window afterwards
    m_grid->SetMinSize( FromDIP( wxSize( 600, 300 ) ) );
    finishDialogSettings();
    SetSize( FromDIP( wxSize( 1000, 640 ) ) );
    Centre();
    fillGrid();     // re-wraps the info text at the final width
}


void DIALOG_VARIANT_MANAGER::loadData()
{
    SCHEMATIC& sch = m_frame->Schematic();

    std::vector<wxString> names( sch.GetVariantNames().begin(), sch.GetVariantNames().end() );
    std::sort( names.begin(), names.end(),
               []( const wxString& a, const wxString& b ) { return StrNumCmp( a, b, true ) < 0; } );

    for( const wxString& name : names )
    {
        wxString desc = sch.GetVariantDescription( name );
        m_variants.push_back( { name, name, desc, desc } );
    }

    SCH_REFERENCE_LIST refs;
    sch.Hierarchy().GetSymbols( refs, SYMBOL_FILTER_NON_POWER );

    std::map<wxString, size_t> indexOf;

    for( size_t i = 0; i < refs.GetCount(); ++i )
    {
        SCH_REFERENCE& ref = refs[(int) i];
        SCH_SYMBOL*    symbol = ref.GetSymbol();

        if( !symbol || ref.GetRef().StartsWith( wxS( "#" ) ) )
            continue;

        if( symbol->GetLibSymbolRef() && symbol->GetLibSymbolRef()->IsPower() )
            continue;

        const SCH_SHEET_PATH& path = ref.GetSheetPath();
        wxString              designator = symbol->GetRef( &path, false );
        auto                  it = indexOf.find( designator );

        if( it == indexOf.end() )
        {
            COMPONENT comp;
            comp.designator = designator;
            comp.comment = symbol->GetValue( true, &path, false );
            comp.footprint = symbol->GetFootprintFieldText( true, &path, false );

            if( const SCH_FIELD* desc = symbol->GetField( FIELD_T::DESCRIPTION ) )
                comp.description = desc->GetShownText( &path, false );

            indexOf[designator] = m_components.size();
            m_components.push_back( comp );
            it = indexOf.find( designator );
        }

        m_components[it->second].units.push_back( { symbol, path } );
    }

    std::sort( m_components.begin(), m_components.end(),
               []( const COMPONENT& a, const COMPONENT& b )
               {
                   return StrNumCmp( a.designator, b.designator, true ) < 0;
               } );

    m_cells.assign( m_components.size(), std::vector<CELL>( m_variants.size() ) );

    for( size_t i = 0; i < m_components.size(); ++i )
    {
        const UNIT& unit = m_components[i].units.front();

        for( size_t v = 0; v < m_variants.size(); ++v )
        {
            CELL& cell = m_cells[i][v];
            cell.dnp = unit.symbol->GetDNP( &unit.path, m_variants[v].name );

            if( std::optional<SCH_SYMBOL_VARIANT> variant =
                        unit.symbol->GetVariant( unit.path, m_variants[v].name ) )
            {
                for( const auto& [field, value] : variant->m_Fields )
                {
                    if( field != wxS( "Reference" ) )
                        cell.fields[field] = value;
                }
            }

            cell.origDnp = cell.dnp;
            cell.origFields = cell.fields;
        }
    }
}


int DIALOG_VARIANT_MANAGER::variantCol( int aGridCol ) const
{
    int idx = aGridCol - FIXED_COLS;
    return ( idx >= 0 && idx < (int) m_variants.size() ) ? idx : -1;
}


void DIALOG_VARIANT_MANAGER::fillGrid()
{
    m_grid->Freeze();

    if( m_grid->GetNumberRows() > 0 )
        m_grid->DeleteRows( 0, m_grid->GetNumberRows() );

    int wantCols = FIXED_COLS + (int) m_variants.size();

    if( m_grid->GetNumberCols() > wantCols )
        m_grid->DeleteCols( wantCols, m_grid->GetNumberCols() - wantCols );
    else if( m_grid->GetNumberCols() < wantCols )
        m_grid->AppendCols( wantCols - m_grid->GetNumberCols() );

    wxArrayString choices;
    choices.Add( stateFitted() );
    choices.Add( stateNotFitted() );
    choices.Add( stateAlternate() );

    for( int col = 0; col < FIXED_COLS; ++col )
    {
        wxGridCellAttr* attr = new wxGridCellAttr;
        attr->SetReadOnly( true );
        m_grid->SetColAttr( col, attr );
    }

    for( size_t v = 0; v < m_variants.size(); ++v )
    {
        int             col = FIXED_COLS + (int) v;
        wxGridCellAttr* attr = new wxGridCellAttr;
        attr->SetEditor( new wxGridCellChoiceEditor( choices ) );
        attr->SetAlignment( wxALIGN_CENTRE, wxALIGN_CENTRE );
        m_grid->SetColAttr( col, attr );

        wxString label = m_variants[v].name;

        if( !m_variants[v].description.IsEmpty() )
            label << wxS( "\n" ) << m_variants[v].description;

        m_grid->SetColLabelValue( col, label );
        m_grid->SetColSize( col, FromDIP( 110 ) );
    }

    m_grid->AppendRows( (int) m_components.size() );

    for( int row = 0; row < (int) m_components.size(); ++row )
        fillRow( row );

    m_grid->Thaw();
    m_grid->ForceRefresh();

    if( m_variants.empty() )
    {
        m_info->SetLabel( _( "No variants are defined.  Use Add Variant to create one; each "
                             "component is then Fitted, Not Fitted or an Alternate part in it." ) );
    }
    else
    {
        m_info->SetLabel( _( "Each cell is Fitted, Not Fitted (DNP in that variant) or Alternate "
                             "(parameter variations).  Changes reach the board with Design > "
                             "Update PCB Document." ) );
    }

    // Wrap to the dialog's width rather than widening the dialog to one long line
    m_info->Wrap( std::max( FromDIP( 300 ), GetClientSize().x - FromDIP( 16 ) ) );
    Layout();
}


void DIALOG_VARIANT_MANAGER::fillRow( int aRow )
{
    const COMPONENT& comp = m_components[aRow];

    m_grid->SetCellValue( aRow, COL_DESIGNATOR, comp.designator );
    m_grid->SetCellValue( aRow, COL_COMMENT, comp.comment );
    m_grid->SetCellValue( aRow, COL_FOOTPRINT, comp.footprint );
    m_grid->SetCellValue( aRow, COL_DESCRIPTION, comp.description );

    wxColour bg = wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOW );
    wxColour notFittedBg = tint( bg, wxColour( 220, 40, 40 ), 22 );
    wxColour alternateBg = tint( bg, wxColour( 60, 120, 230 ), 22 );

    for( size_t v = 0; v < m_variants.size(); ++v )
    {
        int         col = FIXED_COLS + (int) v;
        const CELL& cell = m_cells[aRow][v];

        if( cell.dnp )
        {
            m_grid->SetCellValue( aRow, col, stateNotFitted() );
            m_grid->SetCellBackgroundColour( aRow, col, notFittedBg );
        }
        else if( !cell.fields.empty() )
        {
            m_grid->SetCellValue( aRow, col, stateAlternate() );
            m_grid->SetCellBackgroundColour( aRow, col, alternateBg );
        }
        else
        {
            m_grid->SetCellValue( aRow, col, stateFitted() );
            m_grid->SetCellBackgroundColour( aRow, col, bg );
        }

        wxFont font = m_grid->GetDefaultCellFont();

        if( cell.dnp != cell.origDnp || cell.fields != cell.origFields )
            font = font.Bold();

        m_grid->SetCellFont( aRow, col, font );
    }
}


void DIALOG_VARIANT_MANAGER::setCell( int aRow, int aVariant, const wxString& aState )
{
    if( aRow < 0 || aRow >= (int) m_components.size() || aVariant < 0
            || aVariant >= (int) m_variants.size() )
    {
        return;
    }

    CELL& cell = m_cells[aRow][aVariant];

    if( aState == stateFitted() )
    {
        cell.dnp = false;
        cell.fields.clear();
    }
    else if( aState == stateNotFitted() )
    {
        cell.dnp = true;
    }
    else if( aState == stateAlternate() )
    {
        cell.dnp = false;

        if( cell.fields.empty() )
            editParameters( aRow, aVariant );
    }

    fillRow( aRow );
}


bool DIALOG_VARIANT_MANAGER::editParameters( int aRow, int aVariant )
{
    if( aRow < 0 || aRow >= (int) m_components.size() || aVariant < 0
            || aVariant >= (int) m_variants.size() )
    {
        return false;
    }

    const COMPONENT& comp = m_components[aRow];
    const UNIT&      unit = comp.units.front();

    std::vector<std::pair<wxString, wxString>> base;

    for( const SCH_FIELD& field : unit.symbol->GetFields() )
    {
        if( field.GetId() == FIELD_T::REFERENCE )
            continue;

        wxString name = field.GetName();
        base.emplace_back( name, unit.symbol->GetFieldText( name, &unit.path ) );
    }

    CELL& cell = m_cells[aRow][aVariant];

    DIALOG_PARAMETER_VARIATIONS dlg( this,
                                     wxString::Format( _( "Parameter Variations - %s in %s" ),
                                                       comp.designator, m_variants[aVariant].name ),
                                     base, cell.fields );

    if( dlg.ShowModal() != wxID_OK )
    {
        fillRow( aRow );
        return false;
    }

    cell.fields = dlg.GetOverrides();

    if( !cell.fields.empty() )
        cell.dnp = false;

    fillRow( aRow );
    return true;
}


bool DIALOG_VARIANT_MANAGER::askVariantName( const wxString& aTitle, wxString& aName, int aIgnore )
{
    while( true )
    {
        wxTextEntryDialog dlg( this, _( "Variant name:" ), aTitle, aName );

        if( dlg.ShowModal() != wxID_OK )
            return false;

        wxString name = dlg.GetValue().Trim().Trim( false );

        if( name.IsEmpty() )
            continue;

        bool clash = name.CmpNoCase( GetDefaultVariantName() ) == 0;

        for( int v = 0; v < (int) m_variants.size(); ++v )
        {
            if( v != aIgnore && m_variants[v].name.CmpNoCase( name ) == 0 )
                clash = true;
        }

        if( clash )
        {
            DisplayError( this, wxString::Format( _( "A variant named '%s' already exists or the "
                                                     "name is reserved." ),
                                                  name ) );
            aName = name;
            continue;
        }

        aName = name;
        return true;
    }
}


void DIALOG_VARIANT_MANAGER::addVariant()
{
    m_grid->SaveEditControlValue();

    wxString name = wxString::Format( _( "Variant %d" ), (int) m_variants.size() + 1 );

    if( !askVariantName( _( "Add Variant" ), name, -1 ) )
        return;

    m_variants.push_back( { name, wxEmptyString, wxEmptyString, wxEmptyString } );

    // A new variant starts as the base design: every component as it is fitted today
    for( size_t i = 0; i < m_components.size(); ++i )
    {
        const UNIT& unit = m_components[i].units.front();
        CELL        cell;
        cell.dnp = cell.origDnp = unit.symbol->GetDNP( &unit.path );
        m_cells[i].push_back( cell );
    }

    fillGrid();
    m_grid->SetGridCursor( 0, FIXED_COLS + (int) m_variants.size() - 1 );
}


void DIALOG_VARIANT_MANAGER::renameVariant( int aVariant )
{
    if( aVariant < 0 )
    {
        wxBell();
        return;
    }

    m_grid->SaveEditControlValue();

    wxString name = m_variants[aVariant].name;

    if( askVariantName( _( "Rename Variant" ), name, aVariant ) )
    {
        m_variants[aVariant].name = name;
        fillGrid();
    }
}


void DIALOG_VARIANT_MANAGER::removeVariant( int aVariant )
{
    if( aVariant < 0 )
    {
        wxBell();
        return;
    }

    m_grid->SaveEditControlValue();

    if( !IsOK( this, wxString::Format( _( "Remove variant '%s' and all its fitting and parameter "
                                          "variations?" ),
                                       m_variants[aVariant].name ) ) )
    {
        return;
    }

    if( !m_variants[aVariant].origName.IsEmpty() )
        m_removed.push_back( m_variants[aVariant].origName );

    m_variants.erase( m_variants.begin() + aVariant );

    for( std::vector<CELL>& row : m_cells )
        row.erase( row.begin() + aVariant );

    fillGrid();
}


void DIALOG_VARIANT_MANAGER::describeVariant( int aVariant )
{
    if( aVariant < 0 )
    {
        wxBell();
        return;
    }

    wxTextEntryDialog dlg( this, _( "Description:" ),
                           wxString::Format( _( "Variant '%s'" ), m_variants[aVariant].name ),
                           m_variants[aVariant].description );

    if( dlg.ShowModal() == wxID_OK )
    {
        m_variants[aVariant].description = dlg.GetValue().Trim().Trim( false );
        fillGrid();
    }
}


void DIALOG_VARIANT_MANAGER::onCellChanged( wxGridEvent& aEvent )
{
    int row = aEvent.GetRow();
    int var = variantCol( aEvent.GetCol() );

    if( var < 0 )
        return;

    wxString state = m_grid->GetCellValue( row, aEvent.GetCol() );

    // Opening a dialog from inside the cell-changed handler upsets the grid; defer it
    CallAfter( [this, row, var, state]() { setCell( row, var, state ); } );
}


void DIALOG_VARIANT_MANAGER::onCellDClick( wxGridEvent& aEvent )
{
    int var = variantCol( aEvent.GetCol() );

    if( var >= 0 && m_cells[aEvent.GetRow()][var].fields.size() > 0 )
    {
        int row = aEvent.GetRow();
        CallAfter( [this, row, var]() { editParameters( row, var ); } );
        return;
    }

    aEvent.Skip();
}


void DIALOG_VARIANT_MANAGER::onCellRightClick( wxGridEvent& aEvent )
{
    int row = aEvent.GetRow();
    int col = aEvent.GetCol();
    int var = variantCol( col );

    if( var < 0 )
        return;

    // Cells the menu applies to: the selection when the clicked cell is in it
    std::vector<std::pair<int, int>> cells;

    if( m_grid->IsInSelection( row, col ) )
    {
        for( const wxGridBlockCoords& block : m_grid->GetSelectedBlocks() )
        {
            for( int r = block.GetTopRow(); r <= block.GetBottomRow(); ++r )
            {
                for( int c = block.GetLeftCol(); c <= block.GetRightCol(); ++c )
                {
                    if( variantCol( c ) >= 0 )
                        cells.emplace_back( r, variantCol( c ) );
                }
            }
        }
    }

    if( cells.empty() )
    {
        m_grid->SetGridCursor( row, col );
        cells.emplace_back( row, var );
    }

    enum { ID_FITTED = wxID_HIGHEST + 1, ID_NOT_FITTED, ID_PARAMS, ID_RENAME, ID_REMOVE };

    wxMenu menu;
    menu.Append( ID_FITTED, stateFitted() );
    menu.Append( ID_NOT_FITTED, stateNotFitted() );
    menu.AppendSeparator();
    menu.Append( ID_PARAMS, _( "Edit Component Parameter Variations..." ) )
            ->Enable( cells.size() == 1 );

    switch( GetPopupMenuSelectionFromUser( menu ) )
    {
    case ID_FITTED:
        for( const auto& [r, v] : cells )
            setCell( r, v, stateFitted() );

        break;

    case ID_NOT_FITTED:
        for( const auto& [r, v] : cells )
            setCell( r, v, stateNotFitted() );

        break;

    case ID_PARAMS:
        editParameters( cells.front().first, cells.front().second );
        break;

    default:
        break;
    }
}


void DIALOG_VARIANT_MANAGER::onLabelRightClick( wxGridEvent& aEvent )
{
    int var = variantCol( aEvent.GetCol() );

    if( aEvent.GetRow() != -1 )
    {
        aEvent.Skip();
        return;
    }

    enum { ID_ADD = wxID_HIGHEST + 1, ID_RENAME, ID_REMOVE, ID_DESCRIBE, ID_ALL_FITTED,
           ID_ALL_NOT_FITTED };

    wxMenu menu;
    menu.Append( ID_ADD, _( "Add Variant..." ) );

    if( var >= 0 )
    {
        menu.Append( ID_RENAME, _( "Rename Variant..." ) );
        menu.Append( ID_DESCRIBE, _( "Edit Description..." ) );
        menu.Append( ID_REMOVE, _( "Remove Variant" ) );
        menu.AppendSeparator();
        menu.Append( ID_ALL_FITTED, _( "Set All Fitted" ) );
        menu.Append( ID_ALL_NOT_FITTED, _( "Set All Not Fitted" ) );
    }

    int sel = GetPopupMenuSelectionFromUser( menu );

    switch( sel )
    {
    case ID_ADD:      addVariant();          break;
    case ID_RENAME:   renameVariant( var );  break;
    case ID_DESCRIBE: describeVariant( var ); break;
    case ID_REMOVE:   removeVariant( var );  break;

    case ID_ALL_FITTED:
    case ID_ALL_NOT_FITTED:
    {
        bool notFitted = sel == ID_ALL_NOT_FITTED;

        for( int row = 0; row < (int) m_components.size(); ++row )
        {
            CELL& cell = m_cells[row][var];
            cell.dnp = notFitted;
            fillRow( row );
        }

        break;
    }

    default:
        break;
    }
}


bool DIALOG_VARIANT_MANAGER::TransferDataFromWindow()
{
    m_grid->SaveEditControlValue();

    SCHEMATIC& sch = m_frame->Schematic();
    SCH_COMMIT commit( m_frame );
    bool       variantsChanged = false;

    for( const wxString& name : m_removed )
    {
        sch.DeleteVariant( name, &commit );
        variantsChanged = true;
    }

    for( const VARIANT_COL& v : m_variants )
    {
        if( !v.origName.IsEmpty() && v.origName != v.name )
        {
            sch.RenameVariant( v.origName, v.name, &commit );
            variantsChanged = true;
        }
    }

    for( const VARIANT_COL& v : m_variants )
    {
        if( v.origName.IsEmpty() )
        {
            sch.AddVariant( v.name );
            variantsChanged = true;
        }

        if( v.origName.IsEmpty() || v.description != v.origDescription )
        {
            sch.SetVariantDescription( v.name, v.description );
            variantsChanged = true;
        }
    }

    for( size_t i = 0; i < m_components.size(); ++i )
    {
        for( size_t v = 0; v < m_variants.size(); ++v )
        {
            const CELL&     cell = m_cells[i][v];
            const wxString& variant = m_variants[v].name;

            if( cell.dnp == cell.origDnp && cell.fields == cell.origFields
                    && !m_variants[v].origName.IsEmpty() )
            {
                continue;
            }

            std::set<wxString> names;

            for( const auto& [name, value] : cell.origFields )
                names.insert( name );

            for( const auto& [name, value] : cell.fields )
                names.insert( name );

            for( const UNIT& unit : m_components[i].units )
            {
                commit.Modify( unit.symbol, unit.path.LastScreen() );

                unit.symbol->SetDNP( cell.dnp, &unit.path, variant );

                for( const wxString& name : names )
                {
                    if( !unit.symbol->GetField( name ) )
                        continue;

                    auto     it = cell.fields.find( name );
                    wxString value = it != cell.fields.end()
                                             ? it->second
                                             : unit.symbol->GetFieldText( name, &unit.path );

                    unit.symbol->SetFieldText( name, value, &unit.path, variant );
                }
            }
        }
    }

    if( !commit.Empty() )
        commit.Push( _( "Variant Management" ) );
    else if( variantsChanged )
        m_frame->OnModify();

    if( variantsChanged )
    {
        wxString current = sch.GetCurrentVariant();

        if( !current.IsEmpty() && !sch.GetVariantNames().contains( current ) )
            current = wxEmptyString;

        m_frame->UpdateVariantSelectionCtrl( sch.GetVariantNamesForUI() );
        m_frame->SetCurrentVariant( current );
    }

    m_frame->HardRedraw();
    return true;
}
