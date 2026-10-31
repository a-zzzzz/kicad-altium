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

#include "dialog_active_bom.h"

#include <algorithm>
#include <map>
#include <set>

#include <lib_symbol.h>
#include <sch_edit_frame.h>
#include <sch_reference_list.h>
#include <sch_sheet_path.h>
#include <sch_symbol.h>
#include <schematic.h>
#include <schematic_settings.h>
#include <string_utils.h>
#include <tool/tool_manager.h>
#include <tools/sch_actions.h>
#include <tools/sch_editor_control.h>

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/grid.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/srchctrl.h>
#include <wx/stattext.h>


namespace
{

enum COLUMNS
{
    COL_LINE,
    COL_NAME,
    COL_DESCRIPTION,
    COL_DESIGNATOR,
    COL_QUANTITY,
    COL_FOOTPRINT,
    COL_MANUFACTURER,
    COL_MPN,
    COL_SUPPLIER,
    COL_SPN,
    COL_STATUS,
    COL_COUNT
};


const std::vector<wxString> MANUFACTURER_FIELDS = {
    wxS( "Manufacturer" ), wxS( "Manufacturer Name" ), wxS( "Manufacturer_Name" ), wxS( "MFR" ),
    wxS( "Mfr" ), wxS( "Mfg" ), wxS( "Manufacturer 1" )
};

const std::vector<wxString> MPN_FIELDS = {
    wxS( "MPN" ), wxS( "Manufacturer Part Number" ), wxS( "Manufacturer_Part_Number" ),
    wxS( "Manufacturer PN" ), wxS( "MFR PN" ), wxS( "MFR_PN" ), wxS( "MfrPN" ), wxS( "Mfr. No." ),
    wxS( "MFN" ), wxS( "Part Number" ), wxS( "PartNumber" ), wxS( "Manufacturer Part Number 1" )
};

const std::vector<wxString> SUPPLIER_FIELDS = {
    wxS( "Supplier" ), wxS( "Distributor" ), wxS( "Vendor" ), wxS( "Supplier 1" )
};

/// Supplier part number fields.  The ones named after a distributor also name the supplier.
const std::vector<wxString> SPN_FIELDS = {
    wxS( "Supplier Part Number" ), wxS( "Supplier PN" ), wxS( "SPN" ),
    wxS( "Distributor Part Number" ), wxS( "Supplier Part Number 1" ), wxS( "LCSC" ),
    wxS( "LCSC Part" ), wxS( "LCSC Part #" ), wxS( "DigiKey" ), wxS( "Digikey" ),
    wxS( "DigiKey_PN" ), wxS( "Digi-Key_PN" ), wxS( "Digi-Key Part Number" ), wxS( "Mouser" ),
    wxS( "Mouser Part Number" ), wxS( "Farnell" ), wxS( "Newark" ), wxS( "RS" )
};

const std::vector<wxString> DISTRIBUTOR_NAMES = {
    wxS( "LCSC" ), wxS( "DigiKey" ), wxS( "Digikey" ), wxS( "Digi-Key" ), wxS( "Mouser" ),
    wxS( "Farnell" ), wxS( "Newark" ), wxS( "RS" )
};


/// First non-empty field among aNames (case-insensitive).  aFoundName gets the field's name.
wxString findField( const SCH_SYMBOL* aSymbol, const SCH_SHEET_PATH& aPath,
                    const wxString& aVariant, const std::vector<wxString>& aNames,
                    wxString* aFoundName = nullptr )
{
    for( const wxString& name : aNames )
    {
        for( const SCH_FIELD& field : aSymbol->GetFields() )
        {
            if( field.GetName().CmpNoCase( name ) != 0 )
                continue;

            wxString text = field.GetShownText( &aPath, false, 0, aVariant );
            text.Trim().Trim( false );

            if( !text.IsEmpty() && text != wxS( "~" ) )
            {
                if( aFoundName )
                    *aFoundName = field.GetName();

                return text;
            }
        }
    }

    return wxEmptyString;
}


wxString fieldText( const SCH_SYMBOL* aSymbol, const SCH_SHEET_PATH& aPath,
                    const wxString& aVariant, const wxString& aName )
{
    if( aName == wxS( "Value" ) )
        return aSymbol->GetValue( true, &aPath, false, aVariant );

    if( aName == wxS( "Footprint" ) )
        return aSymbol->GetFootprintFieldText( true, &aPath, false, aVariant );

    if( aName == wxS( "${DNP}" ) )
        return aSymbol->GetDNP( &aPath, aVariant ) ? wxString( wxS( "DNP" ) ) : wxString();

    if( aName == wxS( "${EXCLUDE_FROM_BOM}" ) )
        return aSymbol->GetExcludedFromBOM( &aPath, aVariant ) ? wxString( wxS( "X" ) ) : wxString();

    if( aName == wxS( "${EXCLUDE_FROM_BOARD}" ) )
        return aSymbol->GetExcludedFromBoard( &aPath, aVariant ) ? wxString( wxS( "X" ) ) : wxString();

    if( aName == wxS( "${EXCLUDE_FROM_SIM}" ) )
        return aSymbol->GetExcludedFromSim( &aPath, aVariant ) ? wxString( wxS( "X" ) ) : wxString();

    if( aName.StartsWith( wxS( "${" ) ) )
        return wxEmptyString;   // computed columns (${QUANTITY}, ${ITEM_NUMBER}, ...)

    if( const SCH_FIELD* field = aSymbol->GetField( aName ) )
        return field->GetShownText( &aPath, false, 0, aVariant );

    return wxEmptyString;
}


wxColour blend( const wxColour& aBase, const wxColour& aTint, int aPercent )
{
    auto mix = [&]( int a, int b ) { return ( a * ( 100 - aPercent ) + b * aPercent ) / 100; };
    return wxColour( mix( aBase.Red(), aTint.Red() ), mix( aBase.Green(), aTint.Green() ),
                     mix( aBase.Blue(), aTint.Blue() ) );
}

} // namespace


DIALOG_ACTIVE_BOM::DIALOG_ACTIVE_BOM( SCH_EDIT_FRAME* aParent ) :
        DIALOG_SHIM( aParent, wxID_ANY, _( "ActiveBOM" ), wxDefaultPosition, wxDefaultSize,
                     wxDEFAULT_FRAME_STYLE | wxRESIZE_BORDER, wxS( "DIALOG_ACTIVE_BOM" ) ),
        m_frame( aParent ),
        m_filling( false )
{
    wxBoxSizer* main = new wxBoxSizer( wxVERTICAL );

    // Top bar: variant, BOM preset (grouping), filter, refresh
    wxBoxSizer* top = new wxBoxSizer( wxHORIZONTAL );

    top->Add( new wxStaticText( this, wxID_ANY, _( "Variant:" ) ), 0,
              wxALIGN_CENTER_VERTICAL | wxRIGHT, 4 );
    m_variantChoice = new wxChoice( this, wxID_ANY );
    m_variantChoice->SetMinSize( FromDIP( wxSize( 160, -1 ) ) );
    top->Add( m_variantChoice, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12 );

    top->Add( new wxStaticText( this, wxID_ANY, _( "BOM view:" ) ), 0,
              wxALIGN_CENTER_VERTICAL | wxRIGHT, 4 );
    m_presetChoice = new wxChoice( this, wxID_ANY );
    m_presetChoice->SetToolTip( _( "KiCad BOM preset: its group-by fields and DNP / excluded "
                                   "settings define the line items" ) );
    top->Add( m_presetChoice, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12 );

    m_filter = new wxSearchCtrl( this, wxID_ANY );
    m_filter->ShowCancelButton( true );
    m_filter->SetDescriptiveText( _( "Filter" ) );
    m_filter->SetMinSize( FromDIP( wxSize( 160, -1 ) ) );
    top->Add( m_filter, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12 );

    top->AddStretchSpacer();

    wxButton* refresh = new wxButton( this, wxID_REFRESH, _( "Refresh" ) );
    top->Add( refresh, 0, wxALIGN_CENTER_VERTICAL );

    main->Add( top, 0, wxEXPAND | wxALL, 8 );

    // BOM grid
    m_grid = new wxGrid( this, wxID_ANY );
    m_grid->CreateGrid( 0, COL_COUNT );
    m_grid->SetRowLabelSize( 0 );
    m_grid->EnableEditing( false );
    m_grid->SetSelectionMode( wxGrid::wxGridSelectRows );
    m_grid->SetCellHighlightPenWidth( 0 );
    m_grid->EnableDragRowSize( false );

    const std::pair<int, wxString> labels[] = {
        { COL_LINE, _( "#" ) },
        { COL_NAME, _( "Name" ) },
        { COL_DESCRIPTION, _( "Description" ) },
        { COL_DESIGNATOR, _( "Designator" ) },
        { COL_QUANTITY, _( "Quantity" ) },
        { COL_FOOTPRINT, _( "Footprint" ) },
        { COL_MANUFACTURER, _( "Manufacturer 1" ) },
        { COL_MPN, _( "Manufacturer Part Number 1" ) },
        { COL_SUPPLIER, _( "Supplier 1" ) },
        { COL_SPN, _( "Supplier Part Number 1" ) },
        { COL_STATUS, _( "Status" ) },
    };

    for( const auto& [col, label] : labels )
        m_grid->SetColLabelValue( col, label );

    const std::pair<int, int> widths[] = {
        { COL_LINE, 36 },      { COL_NAME, 120 },  { COL_DESCRIPTION, 200 }, { COL_DESIGNATOR, 140 },
        { COL_QUANTITY, 64 },  { COL_FOOTPRINT, 200 }, { COL_MANUFACTURER, 120 }, { COL_MPN, 160 },
        { COL_SUPPLIER, 90 },  { COL_SPN, 140 },   { COL_STATUS, 220 },
    };

    for( const auto& [col, width] : widths )
        m_grid->SetColSize( col, FromDIP( width ) );

    main->Add( m_grid, 1, wxEXPAND | wxLEFT | wxRIGHT, 8 );

    // Bottom: summary and buttons
    wxBoxSizer* bottom = new wxBoxSizer( wxHORIZONTAL );
    m_summary = new wxStaticText( this, wxID_ANY, wxEmptyString );
    bottom->Add( m_summary, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8 );

    wxButton* editFields = new wxButton( this, wxID_ANY, _( "Edit Fields..." ) );
    editFields->SetToolTip( _( "Edit symbol fields in KiCad's Symbol Fields Table" ) );
    bottom->Add( editFields, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4 );

    wxButton* exportBtn = new wxButton( this, wxID_ANY, _( "Export..." ) );
    exportBtn->SetToolTip( _( "Export the bill of materials with KiCad's BOM export" ) );
    bottom->Add( exportBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4 );

    wxButton* close = new wxButton( this, wxID_CLOSE, _( "Close" ) );
    bottom->Add( close, 0, wxALIGN_CENTER_VERTICAL );

    main->Add( bottom, 0, wxEXPAND | wxALL, 8 );

    SetSizer( main );

    // Presets: the project's BOM settings, then KiCad's built-in ones
    BOM_PRESET project = m_frame->Schematic().Settings().m_BomSettings;
    m_presets.push_back( project );
    m_presetChoice->Append( project.name.IsEmpty()
                                    ? _( "Project BOM settings" )
                                    : wxString::Format( _( "Project BOM settings (%s)" ),
                                                        wxGetTranslation( project.name ) ) );

    int groupedIdx = 0;

    for( const BOM_PRESET& preset : BOM_PRESET::BuiltInPresets() )
    {
        if( preset.name == BOM_PRESET::GroupedByValueFootprint().name )
            groupedIdx = (int) m_presets.size();

        m_presets.push_back( preset );
        m_presetChoice->Append( wxGetTranslation( preset.name ) );
    }

    // A BOM groups parts; fall back to "Grouped By Value and Footprint" when the project's
    // settings don't group.
    m_presetChoice->SetSelection( project.groupSymbols ? 0 : groupedIdx );

    fillVariants();

    m_variantChoice->Bind( wxEVT_CHOICE, [this]( wxCommandEvent& ) { Rebuild(); } );
    m_presetChoice->Bind( wxEVT_CHOICE, [this]( wxCommandEvent& ) { Rebuild(); } );
    m_filter->Bind( wxEVT_TEXT, [this]( wxCommandEvent& ) { fillGrid(); } );
    m_filter->Bind( wxEVT_SEARCHCTRL_CANCEL_BTN,
                    [this]( wxCommandEvent& )
                    {
                        m_filter->ChangeValue( wxEmptyString );
                        fillGrid();
                    } );
    refresh->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { Rebuild(); } );
    close->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { Show( false ); } );

    editFields->Bind( wxEVT_BUTTON,
                      [this]( wxCommandEvent& )
                      {
                          m_frame->GetToolManager()->RunAction( SCH_ACTIONS::editSymbolFields );
                      } );
    exportBtn->Bind( wxEVT_BUTTON,
                     [this]( wxCommandEvent& )
                     {
                         m_frame->GetToolManager()->RunAction( SCH_ACTIONS::generateBOM );
                     } );

    m_grid->Bind( wxEVT_GRID_SELECT_CELL, &DIALOG_ACTIVE_BOM::onGridSelect, this );
    m_grid->Bind( wxEVT_GRID_CELL_LEFT_DCLICK,
                  [this]( wxGridEvent& aEvent )
                  {
                      crossProbe( aEvent.GetRow() );
                  } );

    Bind( wxEVT_ACTIVATE, &DIALOG_ACTIVE_BOM::onActivate, this );
    Bind( wxEVT_CLOSE_WINDOW, [this]( wxCloseEvent& ) { Show( false ); } );

    // finishDialogSettings() shrinks the dialog to its sizer's minimum: give the grid a usable
    // minimum (an empty grid's own is a sliver) and size the window afterwards
    m_grid->SetMinSize( FromDIP( wxSize( 600, 300 ) ) );
    finishDialogSettings();
    SetSize( FromDIP( wxSize( 1200, 640 ) ) );
    Centre();

    Rebuild();
}


void DIALOG_ACTIVE_BOM::fillVariants()
{
    wxString previous = currentVariant();

    if( m_variantNames.empty() )
        previous = m_frame->Schematic().GetCurrentVariant();

    m_variantChoice->Clear();
    m_variantNames.Clear();

    m_variantChoice->Append( _( "[No Variations]" ) );
    m_variantNames.Add( wxEmptyString );

    std::vector<wxString> names( m_frame->Schematic().GetVariantNames().begin(),
                                 m_frame->Schematic().GetVariantNames().end() );

    std::sort( names.begin(), names.end(),
               []( const wxString& a, const wxString& b ) { return StrNumCmp( a, b, true ) < 0; } );

    for( const wxString& name : names )
    {
        m_variantChoice->Append( name );
        m_variantNames.Add( name );
    }

    int idx = m_variantNames.Index( previous );
    m_variantChoice->SetSelection( idx == wxNOT_FOUND ? 0 : idx );
}


wxString DIALOG_ACTIVE_BOM::currentVariant() const
{
    int sel = m_variantChoice->GetSelection();

    if( sel < 0 || sel >= (int) m_variantNames.size() )
        return wxEmptyString;

    return m_variantNames[sel];
}


BOM_PRESET DIALOG_ACTIVE_BOM::currentPreset() const
{
    int sel = m_presetChoice->GetSelection();

    if( sel < 0 || sel >= (int) m_presets.size() )
        return BOM_PRESET::GroupedByValueFootprint();

    // Keep the project entry current with the project's settings
    if( sel == 0 )
        return m_frame->Schematic().Settings().m_BomSettings;

    return m_presets[sel];
}


void DIALOG_ACTIVE_BOM::Rebuild()
{
    if( !m_frame->Schematic().IsValid() )
        return;

    fillVariants();

    const wxString   variant = currentVariant();
    const BOM_PRESET preset = currentPreset();

    // Group-by fields of the preset (Value + Footprint when it doesn't group)
    std::vector<wxString> groupBy;
    bool                  group = preset.groupSymbols;

    for( const BOM_FIELD& field : preset.fieldsOrdered )
    {
        if( !field.groupBy )
            continue;

        if( field.name == wxS( "Reference" ) )
            group = false;
        else
            groupBy.push_back( field.name );
    }

    if( group && groupBy.empty() )
        groupBy = { wxS( "Value" ), wxS( "Footprint" ) };

    // One entry per designator: units of a multi-unit part are one component
    SCH_REFERENCE_LIST refs;
    m_frame->Schematic().Hierarchy().GetSymbols( refs, SYMBOL_FILTER_NON_POWER );

    struct COMPONENT
    {
        SCH_SYMBOL*    symbol;
        SCH_SHEET_PATH path;
        wxString       fullPath;
    };

    std::map<wxString, COMPONENT, std::function<bool( const wxString&, const wxString& )>>
            components( []( const wxString& a, const wxString& b )
                        {
                            return StrNumCmp( a, b, true ) < 0;
                        } );

    for( size_t i = 0; i < refs.GetCount(); ++i )
    {
        SCH_REFERENCE& ref = refs[(int) i];
        SCH_SYMBOL*    symbol = ref.GetSymbol();

        if( !symbol || ref.GetRef().StartsWith( wxS( "#" ) ) )
            continue;

        if( symbol->GetLibSymbolRef() && symbol->GetLibSymbolRef()->IsPower() )
            continue;

        wxString designator = symbol->GetRef( &ref.GetSheetPath(), false );

        if( !components.contains( designator ) )
            components.emplace( designator, COMPONENT{ symbol, ref.GetSheetPath(), ref.GetFullPath() } );
    }

    std::map<wxString, size_t> lineOf;   // group key -> index into m_lines
    m_lines.clear();

    for( auto& [designator, comp] : components )
    {
        const SCH_SYMBOL*     sym = comp.symbol;
        const SCH_SHEET_PATH& path = comp.path;

        bool excluded = sym->GetExcludedFromBOM( &path, variant );
        bool dnp = sym->GetDNP( &path, variant );

        if( excluded && !preset.includeExcludedFromBOM )
            continue;

        if( dnp && preset.excludeDNP )
            continue;

        LINE item;
        item.name = sym->GetValue( true, &path, false, variant );
        item.footprint = sym->GetFootprintFieldText( true, &path, false, variant );

        if( const SCH_FIELD* desc = sym->GetField( FIELD_T::DESCRIPTION ) )
            item.description = desc->GetShownText( &path, false, 0, variant );

        item.manufacturer = findField( sym, path, variant, MANUFACTURER_FIELDS );
        item.mpn = findField( sym, path, variant, MPN_FIELDS );
        item.supplier = findField( sym, path, variant, SUPPLIER_FIELDS );

        wxString spnField;
        item.spn = findField( sym, path, variant, SPN_FIELDS, &spnField );

        if( item.supplier.IsEmpty() && !item.spn.IsEmpty() )
        {
            for( const wxString& dist : DISTRIBUTOR_NAMES )
            {
                if( spnField.Lower().StartsWith( dist.Lower() ) )
                {
                    item.supplier = dist;
                    break;
                }
            }
        }

        item.notFitted = dnp;

        wxString key;

        if( group )
        {
            for( const wxString& name : groupBy )
                key << fieldText( sym, path, variant, name ) << wxS( "\x1f" );

            key << ( dnp ? wxS( "DNP" ) : wxS( "FIT" ) );
        }
        else
        {
            key = designator;
        }

        auto it = lineOf.find( key );

        if( it == lineOf.end() )
        {
            lineOf[key] = m_lines.size();
            m_lines.push_back( item );
            it = lineOf.find( key );
        }

        LINE& line = m_lines[it->second];
        line.designators.push_back( designator );
        line.symbolPaths.push_back( comp.fullPath );
        line.quantity++;

        // Part data shared by a line: keep the first non-empty value
        if( line.manufacturer.IsEmpty() )
            line.manufacturer = item.manufacturer;

        if( line.mpn.IsEmpty() )
            line.mpn = item.mpn;

        if( line.supplier.IsEmpty() )
            line.supplier = item.supplier;

        if( line.spn.IsEmpty() )
            line.spn = item.spn;

        if( line.footprint.IsEmpty() )
            line.footprint = item.footprint;
    }

    // Status
    for( LINE& line : m_lines )
    {
        wxArrayString issues;

        if( line.notFitted )
        {
            line.severity = 1;
            line.status = _( "Not Fitted" );
            continue;
        }

        if( line.footprint.IsEmpty() )
        {
            issues.Add( _( "Missing footprint" ) );
            line.severity = std::max( line.severity, 3 );
        }

        if( line.name.IsEmpty() || line.name == wxS( "~" ) )
        {
            issues.Add( _( "Missing value" ) );
            line.severity = std::max( line.severity, 3 );
        }

        if( line.mpn.IsEmpty() )
        {
            issues.Add( _( "Missing part data (MPN)" ) );
            line.severity = std::max( line.severity, 2 );
        }

        line.status = issues.IsEmpty() ? _( "OK" ) : wxJoin( issues, ',', 0 ).Trim();
        line.status.Replace( wxS( "," ), wxS( ", " ) );
    }

    fillGrid();
}


void DIALOG_ACTIVE_BOM::fillGrid()
{
    wxString filter = m_filter->GetValue().Lower();

    // Keep the selected line selected across refreshes
    wxString selectedKey;
    int      cursor = m_grid->GetGridCursorRow();

    if( cursor >= 0 && cursor < m_grid->GetNumberRows() )
        selectedKey = m_grid->GetCellValue( cursor, COL_DESIGNATOR );

    m_filling = true;

    m_shown.clear();

    for( int i = 0; i < (int) m_lines.size(); ++i )
    {
        const LINE& line = m_lines[i];

        if( !filter.IsEmpty() )
        {
            wxString all = line.name + wxS( " " ) + line.description + wxS( " " )
                           + wxJoin( wxArrayString( line.designators.size(), line.designators.data() ), ' ' )
                           + wxS( " " ) + line.footprint + wxS( " " ) + line.manufacturer
                           + wxS( " " ) + line.mpn + wxS( " " ) + line.supplier + wxS( " " )
                           + line.spn + wxS( " " ) + line.status;

            if( !all.Lower().Contains( filter ) )
                continue;
        }

        m_shown.push_back( i );
    }

    m_grid->Freeze();

    if( m_grid->GetNumberRows() > 0 )
        m_grid->DeleteRows( 0, m_grid->GetNumberRows() );

    m_grid->AppendRows( (int) m_shown.size() );

    wxColour bg = wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOW );
    wxColour fg = wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOWTEXT );
    bool     dark = bg.GetLuminance() < 0.5;
    wxColour okColour = dark ? wxColour( 110, 200, 110 ) : wxColour( 0, 128, 0 );
    wxColour infoColour = blend( fg, bg, 45 );
    wxColour warnColour = dark ? wxColour( 240, 170, 60 ) : wxColour( 190, 110, 0 );
    wxColour errColour = dark ? wxColour( 240, 100, 100 ) : wxColour( 200, 0, 0 );

    int components = 0;
    int notFitted = 0;
    int attention = 0;

    for( int row = 0; row < (int) m_shown.size(); ++row )
    {
        const LINE& line = m_lines[m_shown[row]];

        wxArrayString designators( line.designators.size(), line.designators.data() );

        m_grid->SetCellValue( row, COL_LINE, wxString::Format( wxS( "%d" ), row + 1 ) );
        m_grid->SetCellValue( row, COL_NAME, line.name );
        m_grid->SetCellValue( row, COL_DESCRIPTION, line.description );
        m_grid->SetCellValue( row, COL_DESIGNATOR, wxJoin( designators, ',', 0 ) );
        m_grid->SetCellValue( row, COL_QUANTITY, wxString::Format( wxS( "%d" ), line.quantity ) );
        m_grid->SetCellValue( row, COL_FOOTPRINT, line.footprint );
        m_grid->SetCellValue( row, COL_MANUFACTURER, line.manufacturer );
        m_grid->SetCellValue( row, COL_MPN, line.mpn );
        m_grid->SetCellValue( row, COL_SUPPLIER, line.supplier );
        m_grid->SetCellValue( row, COL_SPN, line.spn );
        m_grid->SetCellValue( row, COL_STATUS, line.status );

        m_grid->SetCellAlignment( row, COL_LINE, wxALIGN_RIGHT, wxALIGN_CENTRE );
        m_grid->SetCellAlignment( row, COL_QUANTITY, wxALIGN_RIGHT, wxALIGN_CENTRE );

        wxColour statusColour = line.severity == 0 ? okColour
                              : line.severity == 1 ? infoColour
                              : line.severity == 2 ? warnColour
                                                   : errColour;

        m_grid->SetCellTextColour( row, COL_STATUS, statusColour );

        if( line.notFitted )
        {
            for( int col = 0; col < COL_STATUS; ++col )
                m_grid->SetCellTextColour( row, col, infoColour );
        }

        components += line.quantity;

        if( line.notFitted )
            notFitted += line.quantity;
        else if( line.severity >= 2 )
            attention++;
    }

    // Designators get commas without spaces from wxJoin; tidy them
    for( int row = 0; row < m_grid->GetNumberRows(); ++row )
    {
        wxString d = m_grid->GetCellValue( row, COL_DESIGNATOR );
        d.Replace( wxS( "," ), wxS( ", " ) );
        m_grid->SetCellValue( row, COL_DESIGNATOR, d );

        if( !selectedKey.IsEmpty() && d == selectedKey )
        {
            m_grid->SetGridCursor( row, COL_NAME );
            m_grid->SelectRow( row );
            selectedKey.Clear();
        }
    }

    m_grid->Thaw();
    m_grid->ForceRefresh();
    m_filling = false;

    m_summary->SetLabel( wxString::Format( _( "%d line item(s), %d component(s), %d not fitted.  "
                                              "%d line item(s) need attention." ),
                                           (int) m_shown.size(), components, notFitted,
                                           attention ) );
}


void DIALOG_ACTIVE_BOM::crossProbe( int aRow )
{
    if( aRow < 0 || aRow >= (int) m_shown.size() )
        return;

    const LINE& line = m_lines[m_shown[aRow]];

    if( line.symbolPaths.empty() )
        return;

    if( SCH_EDITOR_CONTROL* editor = m_frame->GetToolManager()->GetTool<SCH_EDITOR_CONTROL>() )
    {
        wxString path = line.symbolPaths.front();
        editor->FindSymbolAndItem( &path, nullptr, true, HIGHLIGHT_SYMBOL, wxEmptyString );
    }
}


void DIALOG_ACTIVE_BOM::onGridSelect( wxGridEvent& aEvent )
{
    if( !m_filling && IsActive() )
        crossProbe( aEvent.GetRow() );

    aEvent.Skip();
}


void DIALOG_ACTIVE_BOM::onActivate( wxActivateEvent& aEvent )
{
    // Like Altium's ActiveBOM, the view follows the design: refresh when coming back to it
    if( aEvent.GetActive() && IsShown() )
        CallAfter( [this]() { Rebuild(); } );

    aEvent.Skip();
}
