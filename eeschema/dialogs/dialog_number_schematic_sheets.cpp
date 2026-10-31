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

#include <dialogs/dialog_number_schematic_sheets.h>

#include <project.h>
#include <sch_commit.h>
#include <sch_edit_frame.h>
#include <sch_screen.h>
#include <sch_sheet.h>
#include <schematic.h>
#include <title_block.h>

#include <algorithm>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/filename.h>
#include <wx/grid.h>
#include <wx/menu.h>
#include <wx/sizer.h>


namespace
{
enum COLS { COL_DOCUMENT = 0, COL_SHEET, COL_DOCNUMBER, COL_TOTAL, COL_COUNT };

/// Title block comment holding the document number (Comment 9)
const int DOC_NUMBER_COMMENT = 8;
}


DIALOG_NUMBER_SCHEMATIC_SHEETS::DIALOG_NUMBER_SCHEMATIC_SHEETS( SCH_EDIT_FRAME* aParent ) :
        DIALOG_SHIM( aParent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                     wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER ),
        m_frame( aParent )
{
    for( const SCH_SHEET_PATH& path : m_frame->Schematic().Hierarchy() )
    {
        SHEET sheet;
        sheet.path = path;
        sheet.document = wxFileName( path.LastScreen()->GetFileName() ).GetFullName();
        sheet.number = path.GetPageNumber();
        sheet.docNumber = path.LastScreen()->GetTitleBlock().GetComment( DOC_NUMBER_COMMENT );
        m_sheets.push_back( sheet );
    }

    wxString project = m_frame->Prj().GetProjectName() + wxS( ".kicad_pro" );
    SetTitle( wxString::Format( m_sheets.size() == 1
                                        ? _( "Sheet Numbering For Project: %s  - %d Schematic Sheet" )
                                        : _( "Sheet Numbering For Project: %s  - %d Schematic Sheets" ),
                                project, (int) m_sheets.size() ) );

    wxBoxSizer* main = new wxBoxSizer( wxVERTICAL );

    m_grid = new wxGrid( this, wxID_ANY );
    m_grid->CreateGrid( 0, COL_COUNT );
    m_grid->SetRowLabelSize( 0 );
    m_grid->SetColLabelSize( FromDIP( 22 ) );
    m_grid->SetDefaultRowSize( FromDIP( 22 ) );
    m_grid->EnableDragRowSize( false );
    m_grid->SetCellHighlightPenWidth( 0 );
    m_grid->SetSelectionMode( wxGrid::wxGridSelectRows );
    m_grid->SetColLabelAlignment( wxALIGN_LEFT, wxALIGN_CENTER );
    m_grid->SetLabelFont( m_grid->GetFont() );
    m_grid->SetColLabelValue( COL_DOCUMENT, _( "Schematic Document" ) );
    m_grid->SetColLabelValue( COL_SHEET, _( "SheetNumber" ) );
    m_grid->SetColLabelValue( COL_DOCNUMBER, _( "DocumentNumber" ) );
    m_grid->SetColLabelValue( COL_TOTAL, _( "SheetTotal" ) );
    m_grid->SetColSize( COL_DOCUMENT, FromDIP( 260 ) );
    m_grid->SetColSize( COL_SHEET, FromDIP( 180 ) );
    m_grid->SetColSize( COL_DOCNUMBER, FromDIP( 180 ) );
    m_grid->SetColSize( COL_TOTAL, FromDIP( 180 ) );
    m_grid->SetMinSize( FromDIP( wxSize( 820, 380 ) ) );
    main->Add( m_grid, 1, wxEXPAND | wxALL, 8 );

    m_automatic = new wxCheckBox( this, wxID_ANY, _( "Automatic Sheet Numbering (Project Option)" ) );
    m_automatic->SetValue( true );
    main->Add( m_automatic, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8 );

    wxBoxSizer* buttons = new wxBoxSizer( wxHORIZONTAL );

    auto splitButton =
            [this, buttons]( const wxString& aLabel, wxButton*& aMore ) -> wxButton*
            {
                wxButton* button = new wxButton( this, wxID_ANY, aLabel );
                aMore = new wxButton( this, wxID_ANY, wxS( "\u25BE" ), wxDefaultPosition,
                                      wxDefaultSize, wxBU_EXACTFIT );
                buttons->Add( button, 0 );
                buttons->Add( aMore, 0, wxRIGHT, 8 );
                return button;
            };

    wxButton* autoDocMore = nullptr;
    m_autoSheet = splitButton( _( "Auto &Sheet Number" ), m_autoSheetMore );
    wxButton* autoDoc = splitButton( _( "Auto Document &Number" ), autoDocMore );
    wxButton* updateCount = new wxButton( this, wxID_ANY, _( "Update Sheet &Count" ) );
    m_up = new wxButton( this, wxID_ANY, _( "Move &Up" ) );
    m_down = new wxButton( this, wxID_ANY, _( "Move &Down" ) );

    buttons->Add( updateCount, 0, wxRIGHT, 8 );
    buttons->Add( m_up, 0, wxRIGHT, 4 );
    buttons->Add( m_down, 0 );
    buttons->AddStretchSpacer();
    buttons->Add( new wxButton( this, wxID_OK ), 0, wxRIGHT, 4 );
    buttons->Add( new wxButton( this, wxID_CANCEL ), 0 );
    main->Add( buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8 );

    SetSizer( main );

    m_autoSheet->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { autoNumber( false ); } );
    m_autoSheetMore->Bind( wxEVT_BUTTON,
                           [this]( wxCommandEvent& ) { showSchemeMenu( m_autoSheetMore, false ); } );
    autoDoc->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { autoNumber( true ); } );
    autoDocMore->Bind( wxEVT_BUTTON,
                       [this, autoDocMore]( wxCommandEvent& ) { showSchemeMenu( autoDocMore, true ); } );
    updateCount->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { fill(); } );
    m_up->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { move( -1 ); } );
    m_down->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { move( 1 ); } );
    m_automatic->Bind( wxEVT_CHECKBOX,
                       [this]( wxCommandEvent& )
                       {
                           if( m_automatic->GetValue() )
                               autoNumber( false );

                           updateControls();
                       } );
    m_grid->Bind( wxEVT_GRID_SELECT_CELL,
                  [this]( wxGridEvent& aEvent )
                  {
                      aEvent.Skip();
                      CallAfter( [this]() { updateControls(); } );
                  } );
    m_grid->Bind( wxEVT_GRID_CELL_CHANGED,
                  [this]( wxGridEvent& aEvent )
                  {
                      int row = aEvent.GetRow();

                      if( row < 0 || row >= (int) m_sheets.size() )
                          return;

                      wxString value = m_grid->GetCellValue( row, aEvent.GetCol() );
                      value.Trim().Trim( false );

                      if( aEvent.GetCol() == COL_SHEET )
                          m_sheets[row].number = value;
                      else if( aEvent.GetCol() == COL_DOCNUMBER )
                          m_sheets[row].docNumber = value == wxS( "*" ) ? wxString() : value;
                  } );

    fill();
    updateControls();
    SetupStandardButtons();
    finishDialogSettings();
    Centre();
}


void DIALOG_NUMBER_SCHEMATIC_SHEETS::fill()
{
    if( m_grid->GetNumberRows() != (int) m_sheets.size() )
    {
        if( m_grid->GetNumberRows() )
            m_grid->DeleteRows( 0, m_grid->GetNumberRows() );

        m_grid->AppendRows( (int) m_sheets.size() );
    }

    wxString total = wxString::Format( wxS( "%d" ), (int) m_sheets.size() );

    for( size_t ii = 0; ii < m_sheets.size(); ++ii )
    {
        m_grid->SetCellValue( ii, COL_DOCUMENT, m_sheets[ii].document );
        m_grid->SetCellValue( ii, COL_SHEET, m_sheets[ii].number );
        m_grid->SetCellValue( ii, COL_DOCNUMBER, m_sheets[ii].docNumber.IsEmpty()
                                                         ? wxString( wxS( "*" ) )
                                                         : m_sheets[ii].docNumber );
        m_grid->SetCellValue( ii, COL_TOTAL, total );
    }
}


void DIALOG_NUMBER_SCHEMATIC_SHEETS::updateControls()
{
    bool automatic = m_automatic->GetValue();

    // Values that are worked out rather than typed: Altium hatches them
    wxColour shade = GetBackgroundColour().ChangeLightness( 92 );

    for( int col = 0; col < COL_COUNT; ++col )
    {
        wxGridCellAttr* attr = new wxGridCellAttr;
        bool readOnly = col == COL_DOCUMENT || col == COL_TOTAL || ( automatic && col == COL_SHEET );

        attr->SetReadOnly( readOnly );

        if( readOnly && col != COL_DOCUMENT )
            attr->SetBackgroundColour( shade );

        m_grid->SetColAttr( col, attr );
    }

    m_grid->ForceRefresh();

    int row = m_grid->GetGridCursorRow();

    m_autoSheet->Enable( !automatic );
    m_autoSheetMore->Enable( !automatic );
    m_up->Enable( row > 0 );
    m_down->Enable( row >= 0 && row < (int) m_sheets.size() - 1 );
}


void DIALOG_NUMBER_SCHEMATIC_SHEETS::move( int aBy )
{
    int row = m_grid->GetGridCursorRow();
    int to = row + aBy;

    if( row < 0 || to < 0 || to >= (int) m_sheets.size() )
        return;

    std::swap( m_sheets[row], m_sheets[to] );

    if( m_automatic->GetValue() )
        autoNumber( false );
    else
        fill();

    m_grid->SetGridCursor( to, COL_DOCUMENT );
    m_grid->SelectRow( to );
    updateControls();
}


std::vector<size_t> DIALOG_NUMBER_SCHEMATIC_SHEETS::order( SCHEME aScheme ) const
{
    std::vector<size_t> rows( m_sheets.size() );

    for( size_t ii = 0; ii < rows.size(); ++ii )
        rows[ii] = ii;

    if( aScheme == DISPLAY_ORDER )
        return rows;    // the list as shown (Move Up / Move Down)

    // The hierarchy walked down each branch in turn (KiCad's hierarchy order) ...
    std::vector<size_t> byHierarchy;

    for( const SCH_SHEET_PATH& path : m_frame->Schematic().Hierarchy() )
    {
        for( size_t ii = 0; ii < m_sheets.size(); ++ii )
        {
            if( m_sheets[ii].path == path )
                byHierarchy.push_back( ii );
        }
    }

    if( byHierarchy.size() == rows.size() )
        rows = byHierarchy;

    // ... or level by level
    if( aScheme == BREADTH_FIRST )
    {
        std::stable_sort( rows.begin(), rows.end(),
                          [this]( size_t a, size_t b )
                          {
                              return m_sheets[a].path.size() < m_sheets[b].path.size();
                          } );
    }

    return rows;
}


void DIALOG_NUMBER_SCHEMATIC_SHEETS::autoNumber( bool aDocument )
{
    SCHEME              scheme = aDocument ? m_docScheme : m_sheetScheme;
    bool                increasing = aDocument ? m_docIncreasing : m_sheetIncreasing;
    std::vector<size_t> rows = order( scheme );
    int                 count = (int) rows.size();

    for( int ii = 0; ii < count; ++ii )
    {
        wxString number = wxString::Format( wxS( "%d" ), increasing ? ii + 1 : count - ii );

        if( aDocument )
            m_sheets[rows[ii]].docNumber = number;
        else
            m_sheets[rows[ii]].number = number;
    }

    fill();
}


void DIALOG_NUMBER_SCHEMATIC_SHEETS::showSchemeMenu( wxButton* aAnchor, bool aDocument )
{
    enum { ID_DISPLAY = wxID_HIGHEST + 1, ID_DEPTH, ID_BREADTH, ID_INCREASING, ID_DECREASING };

    SCHEME& scheme = aDocument ? m_docScheme : m_sheetScheme;
    bool&   increasing = aDocument ? m_docIncreasing : m_sheetIncreasing;

    // Altium's drop-down: Select Numbering Scheme | Select Numbering Method
    wxMenu menu;
    menu.Append( wxID_ANY, _( "Select Numbering Scheme" ) )->Enable( false );
    menu.AppendRadioItem( ID_DISPLAY, _( "Display Order" ) )->Check( scheme == DISPLAY_ORDER );
    menu.AppendRadioItem( ID_DEPTH, _( "Sheets Hierarchical Structure - Depth First" ) )
            ->Check( scheme == DEPTH_FIRST );
    menu.AppendRadioItem( ID_BREADTH, _( "Sheets Hierarchical Structure - Breadth First" ) )
            ->Check( scheme == BREADTH_FIRST );
    menu.AppendSeparator();
    menu.Append( wxID_ANY, _( "Select Numbering Method" ) )->Enable( false );
    menu.AppendRadioItem( ID_INCREASING, _( "Increasing" ) )->Check( increasing );
    menu.AppendRadioItem( ID_DECREASING, _( "Decreasing" ) )->Check( !increasing );

    switch( aAnchor->GetPopupMenuSelectionFromUser( menu, wxPoint( 0, aAnchor->GetSize().y ) ) )
    {
    case ID_DISPLAY:    scheme = DISPLAY_ORDER; break;
    case ID_DEPTH:      scheme = DEPTH_FIRST;   break;
    case ID_BREADTH:    scheme = BREADTH_FIRST; break;
    case ID_INCREASING: increasing = true;      break;
    case ID_DECREASING: increasing = false;     break;
    default:                                    return;
    }

    autoNumber( aDocument );
}


bool DIALOG_NUMBER_SCHEMATIC_SHEETS::TransferDataFromWindow()
{
    if( m_grid->IsCellEditControlShown() )
        m_grid->SaveEditControlValue();

    SCH_COMMIT commit( m_frame );
    bool       changed = false;

    for( SHEET& sheet : m_sheets )
    {
        if( !sheet.number.IsEmpty() && sheet.number != sheet.path.GetPageNumber() )
        {
            // The page number is kept with the sheet symbol in its parent (the root: its own)
            SCH_SCREEN* screen = m_frame->Schematic().Root().GetScreen();

            if( sheet.path.size() > 1 )
            {
                SCH_SHEET_PATH parent = sheet.path;
                parent.pop_back();
                screen = parent.LastScreen();
            }

            commit.Modify( sheet.path.Last(), screen );
            sheet.path.SetPageNumber( sheet.number );

            if( sheet.path == m_frame->GetCurrentSheet() && m_frame->GetScreen() )
                m_frame->GetScreen()->SetPageNumber( sheet.number );

            changed = true;
        }

        SCH_SCREEN* own = sheet.path.LastScreen();
        TITLE_BLOCK tb = own->GetTitleBlock();

        if( sheet.docNumber != tb.GetComment( DOC_NUMBER_COMMENT ) )
        {
            tb.SetComment( DOC_NUMBER_COMMENT, sheet.docNumber );
            own->SetTitleBlock( tb );
            own->SetContentModified();
            changed = true;
        }
    }

    if( changed )
    {
        if( !commit.Empty() )
            commit.Push( _( "Number Schematic Sheets" ) );

        m_frame->OnModify();
        m_frame->OnPageSettingsChange();
        m_frame->HardRedraw();
    }

    return true;
}
