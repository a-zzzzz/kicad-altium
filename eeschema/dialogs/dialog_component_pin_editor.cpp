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

#include "dialog_component_pin_editor.h"

#include <lib_symbol.h>
#include <sch_commit.h>
#include <sch_pin.h>
#include <string_utils.h>
#include <symbol_edit_frame.h>
#include <symbol_editor_settings.h>
#include <tool/actions.h>
#include <tool/tool_manager.h>
#include <widgets/altium_sub_tabs.h>
#include <widgets/wx_grid.h>

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/dcbuffer.h>
#include <wx/grid.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/simplebook.h>
#include <wx/spinctrl.h>
#include <wx/srchctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <algorithm>


namespace
{
enum COLS
{
    COL_DESIGNATOR = 0,
    COL_NAME,
    COL_DESC,
    COL_TYPE,
    COL_OWNER,
    COL_SHOW,
    COL_NUMBER,
    COL_SHOW_NAME,
    COL_PKG_LENGTH,
    COL_DELAY,
    COL_COUNT
};

// Altium's rotations (the pin extends from its body end in this direction)
const std::vector<int> ROTATIONS = { 0, 90, 180, 270 };

const wxString CHECKED = wxS( "1" );


PIN_ORIENTATION orientationFor( int aRotation )
{
    switch( aRotation )
    {
    case 90:  return PIN_ORIENTATION::PIN_DOWN;    // extends up: the connection is at the top
    case 180: return PIN_ORIENTATION::PIN_RIGHT;   // extends left
    case 270: return PIN_ORIENTATION::PIN_UP;      // extends down
    default:  return PIN_ORIENTATION::PIN_LEFT;    // extends right
    }
}


VECTOR2I direction( int aRotation )
{
    // KiCad's symbol coordinates grow downwards
    switch( aRotation )
    {
    case 90:  return VECTOR2I( 0, -1 );
    case 180: return VECTOR2I( -1, 0 );
    case 270: return VECTOR2I( 0, 1 );
    default:  return VECTOR2I( 1, 0 );
    }
}
} // namespace


DIALOG_COMPONENT_PIN_EDITOR::DIALOG_COMPONENT_PIN_EDITOR( SYMBOL_EDIT_FRAME* aFrame,
                                                          LIB_SYMBOL* aSymbol ) :
        DIALOG_SHIM( aFrame, wxID_ANY, _( "Component Pin Editor" ), wxDefaultPosition,
                     wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER ),
        m_frame( aFrame ),
        m_symbol( aSymbol ),
        m_showNumbers( aSymbol->GetShowPinNumbers() ),
        m_showNames( aSymbol->GetShowPinNames() )
{
    // Work on copies: OK puts them in the symbol, Cancel drops them
    for( SCH_PIN* pin : aSymbol->GetGraphicalPins( 0, 0 ) )
        m_pins.push_back( new SCH_PIN( *pin ) );

    std::sort( m_pins.begin(), m_pins.end(),
               []( SCH_PIN* a, SCH_PIN* b )
               {
                   return StrNumCmp( a->GetNumber(), b->GetNumber(), true ) < 0;
               } );

    wxBoxSizer* main = new wxBoxSizer( wxVERTICAL );
    wxBoxSizer* columns = new wxBoxSizer( wxHORIZONTAL );

    // ---- left: the pins
    wxBoxSizer* left = new wxBoxSizer( wxVERTICAL );
    m_grid = new WX_GRID( this, wxID_ANY );
    m_grid->CreateGrid( 0, COL_COUNT );
    m_grid->SetRowLabelSize( 0 );
    m_grid->SetSelectionMode( wxGrid::wxGridSelectRows );
    m_grid->EnableDragGridSize( false );

    const wxString titles[COL_COUNT] = { _( "Designator" ), _( "Name" ), _( "Desc" ), _( "Type" ),
                                         _( "Owner" ), _( "Show" ), _( "Number" ), _( "Name" ),
                                         _( "Pin/Pkg Length" ), _( "Propagation Delay" ) };
    const int widths[COL_COUNT] = { 75, 90, 70, 95, 55, 50, 60, 50, 100, 125 };

    for( int col = 0; col < COL_COUNT; ++col )
    {
        m_grid->SetColLabelValue( col, titles[col] );
        m_grid->SetColSize( col, FromDIP( widths[col] ) );

        wxGridCellAttr* attr = new wxGridCellAttr;

        if( col == COL_SHOW || col == COL_NUMBER || col == COL_SHOW_NAME )
        {
            attr->SetRenderer( new wxGridCellBoolRenderer );
            attr->SetReadOnly( true );     // toggled by a click (see below)
            attr->SetAlignment( wxALIGN_CENTER, wxALIGN_CENTER );
        }
        else if( col == COL_TYPE )
        {
            wxArrayString types;

            for( int ii = 0; ii < ELECTRICAL_PINTYPES_TOTAL; ++ii )
                types.Add( ElectricalPinTypeGetText( static_cast<ELECTRICAL_PINTYPE>( ii ) ) );

            attr->SetEditor( new wxGridCellChoiceEditor( types ) );
        }
        else if( col == COL_DESC || col == COL_PKG_LENGTH || col == COL_DELAY || col == COL_OWNER )
        {
            // KiCad keeps no pin description, package length or delay; the owner is set at the
            // right (Part Number)
            attr->SetReadOnly( true );
        }

        m_grid->SetColAttr( col, attr );
    }

    m_grid->SetMinSize( FromDIP( wxSize( 760, 620 ) ) );
    left->Add( m_grid, 1, wxEXPAND );

    wxBoxSizer* leftButtons = new wxBoxSizer( wxHORIZONTAL );
    wxButton*   add = new wxButton( this, wxID_ANY, _( "&Add..." ) );
    wxButton*   remove = new wxButton( this, wxID_ANY, _( "&Remove..." ) );
    leftButtons->Add( add, 0, wxRIGHT, FromDIP( 6 ) );
    leftButtons->Add( remove, 0 );
    left->Add( leftButtons, 0, wxTOP, FromDIP( 8 ) );

    columns->Add( left, 3, wxEXPAND | wxALL, FromDIP( 8 ) );

    // ---- right: the picked pin
    wxBoxSizer* right = new wxBoxSizer( wxVERTICAL );
    m_search = new wxSearchCtrl( this, wxID_ANY );
    m_search->SetDescriptiveText( _( "Search" ) );
    right->Add( m_search, 0, wxEXPAND | wxBOTTOM, FromDIP( 6 ) );

    m_tabs = new ALTIUM_SUB_TABS( this );
    m_book = new wxSimplebook( this );
    m_form = new wxPanel( m_book );
    wxPanel* params = new wxPanel( m_book );
    m_book->AddPage( m_form, _( "General" ) );
    m_book->AddPage( params, _( "Parameters" ) );
    m_tabs->AttachBook( m_book );
    right->Add( m_tabs, 0, wxEXPAND );
    right->Add( m_book, 1, wxEXPAND );

    wxBoxSizer* paramSizer = new wxBoxSizer( wxVERTICAL );
    paramSizer->Add( new wxStaticText( params, wxID_ANY, _( "No Parameters" ) ), 0,
                     wxALIGN_CENTER | wxALL, FromDIP( 20 ) );
    params->SetSizer( paramSizer );

    wxFlexGridSizer* grid = new wxFlexGridSizer( 2, FromDIP( 6 ), FromDIP( 8 ) );
    grid->AddGrowableCol( 1 );

    auto label =
            [&]( const wxString& aText )
            {
                grid->Add( new wxStaticText( m_form, wxID_ANY, aText ), 0,
                           wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL );
            };

    auto heading =
            [&]( const wxString& aText )
            {
                wxStaticText* text = new wxStaticText( m_form, wxID_ANY, aText );
                text->SetFont( text->GetFont().Bold() );
                grid->Add( text, 0, wxTOP, FromDIP( 6 ) );
                grid->AddSpacer( 0 );
            };

    auto textBox =
            [&]()
            {
                return new wxTextCtrl( m_form, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                       wxDefaultSize, wxTE_PROCESS_ENTER );
            };

    heading( _( "Location" ) );
    label( wxS( "(X/Y)" ) );
    wxBoxSizer* xy = new wxBoxSizer( wxHORIZONTAL );
    m_x = textBox();
    m_y = textBox();
    xy->Add( m_x, 1, wxRIGHT, FromDIP( 4 ) );
    xy->Add( m_y, 1 );
    grid->Add( xy, 1, wxEXPAND );

    label( _( "Rotation" ) );
    m_rotation = new wxChoice( m_form, wxID_ANY );

    for( int rotation : ROTATIONS )
        m_rotation->Append( wxString::Format( _( "%d Degrees" ), rotation ) );

    grid->Add( m_rotation, 1, wxEXPAND );

    heading( _( "Properties" ) );
    label( _( "Designator" ) );
    m_designator = textBox();
    grid->Add( m_designator, 1, wxEXPAND );

    label( _( "Name" ) );
    m_name = textBox();
    grid->Add( m_name, 1, wxEXPAND );

    label( _( "Functions" ) );
    m_functions = new wxTextCtrl( m_form, wxID_ANY );
    m_functions->SetEditable( false );
    m_functions->SetToolTip( _( "Alternate pin functions (edited in the pin's properties)" ) );
    grid->Add( m_functions, 1, wxEXPAND );

    label( _( "Electrical Type" ) );
    m_type = new wxChoice( m_form, wxID_ANY );

    for( int ii = 0; ii < ELECTRICAL_PINTYPES_TOTAL; ++ii )
        m_type->Append( ElectricalPinTypeGetText( static_cast<ELECTRICAL_PINTYPE>( ii ) ) );

    grid->Add( m_type, 1, wxEXPAND );

    label( _( "Description" ) );
    wxTextCtrl* desc = new wxTextCtrl( m_form, wxID_ANY );
    desc->Enable( false );
    desc->SetToolTip( _( "KiCad keeps no pin descriptions" ) );
    grid->Add( desc, 1, wxEXPAND );

    label( _( "Pin Length" ) );
    m_length = textBox();
    grid->Add( m_length, 1, wxEXPAND );

    label( _( "Pin Package Length" ) );
    wxTextCtrl* pkg = new wxTextCtrl( m_form, wxID_ANY, m_frame->StringFromValue( 0, true ) );
    pkg->Enable( false );
    grid->Add( pkg, 1, wxEXPAND );

    label( _( "Propagation Delay" ) );
    wxTextCtrl* delay = new wxTextCtrl( m_form, wxID_ANY, wxS( "0ps" ) );
    delay->Enable( false );
    grid->Add( delay, 1, wxEXPAND );

    label( _( "Part Number" ) );
    m_part = new wxSpinCtrl( m_form, wxID_ANY );
    m_part->SetRange( 1, std::max( 1, aSymbol->GetUnitCount() ) );
    m_part->Enable( aSymbol->GetUnitCount() > 1 );
    grid->Add( m_part, 1, wxEXPAND );

    wxBoxSizer* formSizer = new wxBoxSizer( wxVERTICAL );
    formSizer->Add( grid, 0, wxEXPAND | wxALL, FromDIP( 8 ) );

    m_preview = new wxWindow( m_form, wxID_ANY, wxDefaultPosition, FromDIP( wxSize( 300, 150 ) ) );
    m_preview->SetBackgroundStyle( wxBG_STYLE_PAINT );
    m_preview->Bind( wxEVT_PAINT, [this]( wxPaintEvent& ) { paintPreview(); } );
    formSizer->Add( m_preview, 1, wxEXPAND | wxALL, FromDIP( 8 ) );
    m_form->SetSizer( formSizer );

    m_status = new wxStaticText( this, wxID_ANY, wxEmptyString );
    right->Add( m_status, 0, wxTOP, FromDIP( 6 ) );

    columns->Add( right, 2, wxEXPAND | wxALL, FromDIP( 8 ) );
    main->Add( columns, 1, wxEXPAND );

    wxStdDialogButtonSizer* buttons = new wxStdDialogButtonSizer();
    buttons->AddButton( new wxButton( this, wxID_OK ) );
    buttons->AddButton( new wxButton( this, wxID_CANCEL ) );
    buttons->Realize();
    main->Add( buttons, 0, wxEXPAND | wxALL, FromDIP( 8 ) );

    SetSizer( main );

    // ---- events
    add->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { onAdd(); } );
    remove->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { onRemove(); } );
    m_grid->Bind( wxEVT_GRID_CELL_CHANGED, &DIALOG_COMPONENT_PIN_EDITOR::onCellChanged, this );
    m_grid->Bind( wxEVT_GRID_SELECT_CELL, &DIALOG_COMPONENT_PIN_EDITOR::onSelect, this );
    m_grid->Bind( wxEVT_GRID_CELL_LEFT_CLICK,
                  [this]( wxGridEvent& aEvent )
                  {
                      // Check boxes toggle with one click
                      int row = aEvent.GetRow();
                      int col = aEvent.GetCol();

                      if( ( col == COL_SHOW || col == COL_NUMBER || col == COL_SHOW_NAME )
                          && row >= 0 && row < (int) m_shown.size() )
                      {
                          bool on = m_grid->GetCellValue( row, col ) == CHECKED;
                          m_grid->SetCellValue( row, col, on ? wxString() : CHECKED );

                          wxGridEvent changed( m_grid->GetId(), wxEVT_GRID_CELL_CHANGED, m_grid,
                                               row, col );
                          onCellChanged( changed );
                      }

                      aEvent.Skip();
                  } );

    auto applyNow = [this]( wxCommandEvent& ) { applyForm(); };

    for( wxTextCtrl* ctrl : { m_x, m_y, m_designator, m_name, m_length } )
    {
        ctrl->Bind( wxEVT_TEXT_ENTER, applyNow );
        ctrl->Bind( wxEVT_KILL_FOCUS,
                    [this]( wxFocusEvent& aEvent )
                    {
                        applyForm();
                        aEvent.Skip();
                    } );
    }

    m_rotation->Bind( wxEVT_CHOICE, applyNow );
    m_type->Bind( wxEVT_CHOICE, applyNow );
    m_part->Bind( wxEVT_SPINCTRL, [this]( wxSpinEvent& ) { applyForm(); } );
    m_search->Bind( wxEVT_TEXT,
                    [this]( wxCommandEvent& )
                    {
                        applyForm();
                        buildTable();
                        showPin( m_shown.empty() ? -1 : 0 );
                    } );

    buildTable();

    if( !m_shown.empty() )
    {
        m_loading = true;
        m_grid->SetGridCursor( 0, COL_DESIGNATOR );
        m_grid->SelectRow( 0 );
        m_loading = false;
        showPin( 0 );
    }
    else
    {
        showPin( -1 );
    }

    finishDialogSettings();
    SetSize( FromDIP( wxSize( 1250, 900 ) ) );
    Centre();
}


DIALOG_COMPONENT_PIN_EDITOR::~DIALOG_COMPONENT_PIN_EDITOR()
{
    for( SCH_PIN* pin : m_pins )
        delete pin;
}


VECTOR2I DIALOG_COMPONENT_PIN_EDITOR::bodyEnd( const SCH_PIN* aPin ) const
{
    // KiCad keeps the connection end; Altium the end at the body
    VECTOR2I dir = direction( altiumRotation( aPin ) );
    return aPin->GetPosition() - dir * aPin->GetLength();
}


int DIALOG_COMPONENT_PIN_EDITOR::altiumRotation( const SCH_PIN* aPin ) const
{
    switch( aPin->GetOrientation() )
    {
    case PIN_ORIENTATION::PIN_DOWN:  return 90;
    case PIN_ORIENTATION::PIN_RIGHT: return 180;
    case PIN_ORIENTATION::PIN_UP:    return 270;
    default:                         return 0;
    }
}


void DIALOG_COMPONENT_PIN_EDITOR::placePin( SCH_PIN* aPin, const VECTOR2I& aBodyEnd,
                                            int aRotation ) const
{
    aPin->SetOrientation( orientationFor( aRotation ) );
    aPin->SetPosition( aBodyEnd + direction( aRotation ) * aPin->GetLength() );
}


void DIALOG_COMPONENT_PIN_EDITOR::buildTable()
{
    m_loading = true;
    m_formPin = nullptr;

    wxString filter = m_search->GetValue().Lower();
    m_shown.clear();

    for( int ii = 0; ii < (int) m_pins.size(); ++ii )
    {
        SCH_PIN* pin = m_pins[ii];

        if( filter.IsEmpty() || pin->GetNumber().Lower().Contains( filter )
            || pin->GetName().Lower().Contains( filter ) )
        {
            m_shown.push_back( ii );
        }
    }

    if( m_grid->GetNumberRows() > 0 )
        m_grid->DeleteRows( 0, m_grid->GetNumberRows() );

    if( !m_shown.empty() )
        m_grid->AppendRows( (int) m_shown.size() );

    for( int row = 0; row < (int) m_shown.size(); ++row )
        fillRow( row );

    m_loading = false;
}


void DIALOG_COMPONENT_PIN_EDITOR::fillRow( int aRow )
{
    SCH_PIN* pin = m_pins[m_shown[aRow]];

    m_grid->SetCellValue( aRow, COL_DESIGNATOR, pin->GetNumber() );
    m_grid->SetCellValue( aRow, COL_NAME, pin->GetName() );
    m_grid->SetCellValue( aRow, COL_DESC, wxEmptyString );
    m_grid->SetCellValue( aRow, COL_TYPE, ElectricalPinTypeGetText( pin->GetType() ) );
    m_grid->SetCellValue( aRow, COL_OWNER, wxString::Format( wxS( "%d" ), std::max( 1, pin->GetUnit() ) ) );
    m_grid->SetCellValue( aRow, COL_SHOW, pin->IsVisible() ? CHECKED : wxString() );
    m_grid->SetCellValue( aRow, COL_NUMBER, m_showNumbers ? CHECKED : wxString() );
    m_grid->SetCellValue( aRow, COL_SHOW_NAME, m_showNames ? CHECKED : wxString() );
    m_grid->SetCellValue( aRow, COL_PKG_LENGTH, m_frame->StringFromValue( 0, true ) );
    m_grid->SetCellValue( aRow, COL_DELAY, wxS( "0ps" ) );
}


int DIALOG_COMPONENT_PIN_EDITOR::pickedRow() const
{
    int row = m_grid->GetGridCursorRow();
    return row >= 0 && row < (int) m_shown.size() ? row : -1;
}


void DIALOG_COMPONENT_PIN_EDITOR::showPin( int aRow )
{
    m_loading = true;

    bool any = aRow >= 0 && aRow < (int) m_shown.size();
    m_form->Enable( any );
    m_formPin = any ? m_pins[m_shown[aRow]] : nullptr;

    if( any )
    {
        SCH_PIN* pin = m_pins[m_shown[aRow]];
        VECTOR2I body = bodyEnd( pin );

        // Altium's Y grows upwards
        m_x->ChangeValue( m_frame->StringFromValue( body.x, true ) );
        m_y->ChangeValue( m_frame->StringFromValue( -body.y, true ) );

        auto it = std::find( ROTATIONS.begin(), ROTATIONS.end(), altiumRotation( pin ) );
        m_rotation->SetSelection( (int) ( it - ROTATIONS.begin() ) );

        m_designator->ChangeValue( pin->GetNumber() );
        m_name->ChangeValue( pin->GetName() );

        wxString functions;

        for( const auto& [altName, alt] : pin->GetAlternates() )
        {
            if( !functions.IsEmpty() )
                functions << wxS( ", " );

            functions << altName;
        }

        m_functions->ChangeValue( functions );
        m_type->SetSelection( static_cast<int>( pin->GetType() ) );
        m_length->ChangeValue( m_frame->StringFromValue( pin->GetLength(), true ) );
        m_part->SetValue( std::max( 1, pin->GetUnit() ) );
        m_status->SetLabel( _( "1 object is selected" ) );
    }
    else
    {
        m_status->SetLabel( _( "Nothing selected" ) );
    }

    m_preview->Refresh();
    m_loading = false;
}


void DIALOG_COMPONENT_PIN_EDITOR::applyForm()
{
    if( m_loading || !m_formPin )
        return;

    SCH_PIN* pin = m_formPin;
    int      row = -1;

    for( int ii = 0; ii < (int) m_shown.size(); ++ii )
    {
        if( m_pins[m_shown[ii]] == pin )
            row = ii;
    }

    int rotation = m_rotation->GetSelection() >= 0 ? ROTATIONS[m_rotation->GetSelection()]
                                                   : altiumRotation( pin );
    VECTOR2I body( m_frame->ValueFromString( m_x->GetValue() ),
                   -m_frame->ValueFromString( m_y->GetValue() ) );

    int length = m_frame->ValueFromString( m_length->GetValue() );

    if( length > 0 )
        pin->SetLength( length );

    placePin( pin, body, rotation );
    pin->SetNumber( m_designator->GetValue() );
    pin->SetName( m_name->GetValue() );

    if( m_type->GetSelection() >= 0 )
        pin->SetType( static_cast<ELECTRICAL_PINTYPE>( m_type->GetSelection() ) );

    if( m_symbol->GetUnitCount() > 1 )
        pin->SetUnit( m_part->GetValue() );

    if( row >= 0 )
    {
        m_loading = true;
        fillRow( row );
        m_loading = false;
    }

    m_preview->Refresh();
}


void DIALOG_COMPONENT_PIN_EDITOR::onSelect( wxGridEvent& aEvent )
{
    aEvent.Skip();

    if( m_loading )
        return;

    // Keep what was typed for the pin being left
    applyForm();

    int row = aEvent.GetRow();
    CallAfter( [this, row]() { showPin( row ); } );
}


void DIALOG_COMPONENT_PIN_EDITOR::onCellChanged( wxGridEvent& aEvent )
{
    if( m_loading )
        return;

    int row = aEvent.GetRow();

    if( row < 0 || row >= (int) m_shown.size() )
        return;

    SCH_PIN* pin = m_pins[m_shown[row]];
    wxString value = m_grid->GetCellValue( row, aEvent.GetCol() );

    switch( aEvent.GetCol() )
    {
    case COL_DESIGNATOR: pin->SetNumber( value );               break;
    case COL_NAME:       pin->SetName( value );                 break;
    case COL_SHOW:       pin->SetVisible( value == CHECKED );   break;

    case COL_TYPE:
        for( int ii = 0; ii < ELECTRICAL_PINTYPES_TOTAL; ++ii )
        {
            if( ElectricalPinTypeGetText( static_cast<ELECTRICAL_PINTYPE>( ii ) ) == value )
                pin->SetType( static_cast<ELECTRICAL_PINTYPE>( ii ) );
        }

        break;

    case COL_NUMBER:
        // KiCad shows pin numbers (and names) for the whole symbol
        m_showNumbers = value == CHECKED;
        break;

    case COL_SHOW_NAME:
        m_showNames = value == CHECKED;
        break;

    default:
        break;
    }

    m_loading = true;

    for( int ii = 0; ii < (int) m_shown.size(); ++ii )
        fillRow( ii );

    m_loading = false;

    if( row == pickedRow() )
        showPin( row );
}


void DIALOG_COMPONENT_PIN_EDITOR::onAdd()
{
    applyForm();
    m_formPin = nullptr;

    SYMBOL_EDITOR_SETTINGS* cfg = m_frame->GetSettings();
    SCH_PIN*                pin = new SCH_PIN( m_symbol );

    // The next free numeric designator
    long next = 1;

    for( SCH_PIN* existing : m_pins )
    {
        long number = 0;

        if( existing->GetNumber().ToLong( &number ) )
            next = std::max( next, number + 1 );
    }

    pin->SetNumber( wxString::Format( wxS( "%ld" ), next ) );
    pin->SetType( ELECTRICAL_PINTYPE::PT_PASSIVE );
    pin->SetLength( schIUScale.MilsToIU( cfg ? cfg->m_Defaults.pin_length : 100 ) );

    if( cfg )
    {
        pin->SetNumberTextSize( schIUScale.MilsToIU( cfg->m_Defaults.pin_num_size ) );
        pin->SetNameTextSize( schIUScale.MilsToIU( cfg->m_Defaults.pin_name_size ) );
    }

    // Below the last pin, as Altium stacks new pins
    VECTOR2I body( 0, 0 );

    if( !m_pins.empty() )
        body = bodyEnd( m_pins.back() ) + VECTOR2I( 0, schIUScale.MilsToIU( 100 ) );

    placePin( pin, body, 0 );
    pin->SetUnit( m_symbol->GetUnitCount() > 1 ? 1 : 0 );
    m_pins.push_back( pin );

    m_search->ChangeValue( wxEmptyString );
    buildTable();

    int row = (int) m_shown.size() - 1;
    m_loading = true;
    m_grid->SetGridCursor( row, COL_DESIGNATOR );
    m_grid->SelectRow( row );
    m_grid->MakeCellVisible( row, COL_DESIGNATOR );
    m_loading = false;
    showPin( row );
}


void DIALOG_COMPONENT_PIN_EDITOR::onRemove()
{
    wxArrayInt rows = m_grid->GetSelectedRows();

    if( rows.IsEmpty() && pickedRow() >= 0 )
        rows.Add( pickedRow() );

    if( rows.IsEmpty() )
        return;

    if( wxMessageBox( wxString::Format( _( "Remove %d pin(s)?" ), (int) rows.size() ),
                      _( "Remove Pins" ), wxYES_NO | wxICON_QUESTION, this ) != wxYES )
    {
        return;
    }

    std::vector<int> indices;

    for( int row : rows )
        indices.push_back( m_shown[row] );

    std::sort( indices.rbegin(), indices.rend() );
    m_formPin = nullptr;

    for( int idx : indices )
    {
        delete m_pins[idx];
        m_pins.erase( m_pins.begin() + idx );
    }

    buildTable();

    if( !m_shown.empty() )
    {
        m_loading = true;
        m_grid->SetGridCursor( 0, COL_DESIGNATOR );
        m_grid->SelectRow( 0 );
        m_loading = false;
    }

    showPin( m_shown.empty() ? -1 : 0 );
}


void DIALOG_COMPONENT_PIN_EDITOR::paintPreview()
{
    wxAutoBufferedPaintDC dc( m_preview );
    wxSize                size = m_preview->GetClientSize();

    // A checkered backdrop with the pin drawn on it, as Altium previews it
    wxColour light( 0x55, 0x55, 0x55 ), dark( 0x4A, 0x4A, 0x4A );
    int      cell = m_preview->FromDIP( 8 );

    dc.SetPen( *wxTRANSPARENT_PEN );

    for( int y = 0; y < size.y; y += cell )
    {
        for( int x = 0; x < size.x; x += cell )
        {
            dc.SetBrush( wxBrush( ( ( x / cell + y / cell ) % 2 ) ? light : dark ) );
            dc.DrawRectangle( x, y, cell, cell );
        }
    }

    int row = pickedRow();

    if( row < 0 )
        return;

    SCH_PIN* pin = m_pins[m_shown[row]];

    // A body square in the middle and the pin leaving it in its direction
    int      body = m_preview->FromDIP( 50 );
    int      len = m_preview->FromDIP( 60 );
    wxPoint  centre( size.x / 2, size.y / 2 );
    wxRect   box( centre.x - body / 2, centre.y - body / 2, body, body );
    VECTOR2I dir = direction( altiumRotation( pin ) );

    dc.SetPen( wxPen( wxColour( 0x99, 0x99, 0x99 ) ) );
    dc.SetBrush( wxBrush( wxColour( 0x77, 0x77, 0x77 ) ) );
    dc.DrawRectangle( box );

    wxPoint start( centre.x + dir.x * body / 2, centre.y + dir.y * body / 2 );
    wxPoint end( start.x + dir.x * len, start.y + dir.y * len );

    dc.SetPen( wxPen( *wxWHITE, m_preview->FromDIP( 2 ) ) );
    dc.DrawLine( start, end );

    dc.SetTextForeground( *wxWHITE );
    dc.SetFont( m_preview->GetFont() );

    wxString number = pin->GetNumber();
    wxString name = pin->GetName();
    wxSize   numberExt = dc.GetTextExtent( number );
    wxSize   nameExt = dc.GetTextExtent( name );
    wxPoint  mid( ( start.x + end.x ) / 2, ( start.y + end.y ) / 2 );
    int      gap = m_preview->FromDIP( 4 );

    if( dir.y == 0 )
    {
        dc.DrawText( number, mid.x - numberExt.x / 2, mid.y - numberExt.y - gap / 2 );
        int nameX = dir.x > 0 ? start.x - nameExt.x - gap : start.x + gap;
        dc.DrawText( name, nameX, start.y - nameExt.y / 2 );
    }
    else
    {
        dc.DrawText( number, mid.x + gap, mid.y - numberExt.y / 2 );
        int nameY = dir.y > 0 ? start.y - nameExt.y - gap / 2 : start.y + gap / 2;
        dc.DrawText( name, start.x - nameExt.x / 2, nameY );
    }
}


bool DIALOG_COMPONENT_PIN_EDITOR::TransferDataFromWindow()
{
    if( !m_grid->CommitPendingChanges() )
        return false;

    applyForm();

    for( SCH_PIN* pin : m_symbol->GetGraphicalPins( 0, 0 ) )
        m_symbol->RemoveDrawItem( pin );

    for( SCH_PIN* pin : m_pins )
    {
        pin->SetParent( m_symbol );
        m_symbol->AddDrawItem( pin );
    }

    m_pins.clear();

    m_symbol->SetShowPinNumbers( m_showNumbers );
    m_symbol->SetShowPinNames( m_showNames );
    return true;
}


void DIALOG_COMPONENT_PIN_EDITOR::Run( SYMBOL_EDIT_FRAME* aFrame )
{
    LIB_SYMBOL* symbol = aFrame->GetCurSymbol();

    if( !symbol )
        return;

    // Selected pins would dangle once the pins are replaced
    aFrame->GetToolManager()->RunAction( ACTIONS::selectionClear );

    SCH_COMMIT commit( aFrame );
    commit.Modify( symbol, aFrame->GetScreen() );

    DIALOG_COMPONENT_PIN_EDITOR dlg( aFrame, symbol );

    if( dlg.ShowModal() != wxID_OK )
        return;

    commit.Push( _( "Edit Pins" ) );
    aFrame->RebuildView();
}
