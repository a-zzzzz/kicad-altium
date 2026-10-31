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

#include <dialogs/dialog_annotate_compiled_sheets.h>

#include <project.h>
#include <sch_commit.h>
#include <sch_edit_frame.h>
#include <sch_screen.h>
#include <sch_sheet.h>
#include <schematic.h>

#include <wx/button.h>
#include <wx/filename.h>
#include <wx/grid.h>
#include <wx/menu.h>
#include <wx/sizer.h>


namespace
{
enum COLS { COL_DOCUMENT = 0, COL_PATH, COL_NUMBER, COL_COUNT };
}


DIALOG_ANNOTATE_COMPILED_SHEETS::DIALOG_ANNOTATE_COMPILED_SHEETS( SCH_EDIT_FRAME* aParent ) :
        DIALOG_SHIM( aParent, wxID_ANY, _( "Annotate Compiled Sheets" ), wxDefaultPosition,
                     wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER ),
        m_frame( aParent )
{
    wxString project = m_frame->Prj().GetProjectName();

    for( const SCH_SHEET_PATH& path : m_frame->Schematic().Hierarchy() )
    {
        SHEET sheet;
        sheet.path = path;
        sheet.document = wxFileName( path.LastScreen()->GetFileName() ).GetFullName();
        sheet.hierarchy = project;

        for( size_t ii = 1; ii < path.size(); ++ii )
            sheet.hierarchy << wxS( "\\" ) << path.at( ii )->GetName();

        sheet.number = path.GetPageNumber();
        m_sheets.push_back( sheet );
    }

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
    m_grid->SetColLabelValue( COL_DOCUMENT, _( "Document" ) );
    m_grid->SetColLabelValue( COL_PATH, _( "Hierarchy Path" ) );
    m_grid->SetColLabelValue( COL_NUMBER, _( "Sheet Number" ) );
    m_grid->SetColSize( COL_DOCUMENT, FromDIP( 200 ) );
    m_grid->SetColSize( COL_PATH, FromDIP( 560 ) );
    m_grid->SetColSize( COL_NUMBER, FromDIP( 120 ) );

    for( int col : { COL_DOCUMENT, COL_PATH } )
    {
        wxGridCellAttr* attr = new wxGridCellAttr;
        attr->SetReadOnly( true );
        m_grid->SetColAttr( col, attr );
    }

    m_grid->SetMinSize( FromDIP( wxSize( 900, 380 ) ) );
    main->Add( m_grid, 1, wxEXPAND | wxALL, 8 );

    wxBoxSizer* buttons = new wxBoxSizer( wxHORIZONTAL );
    wxButton*   annotate = new wxButton( this, wxID_ANY, _( "Annotate Sheet" ) );
    wxButton*   annotateMore = new wxButton( this, wxID_ANY, wxS( "▾" ), wxDefaultPosition,
                                             wxDefaultSize, wxBU_EXACTFIT );
    m_up = new wxButton( this, wxID_ANY, _( "Move &Up" ) );
    m_down = new wxButton( this, wxID_ANY, _( "Move &Down" ) );

    annotate->SetToolTip( _( "Number the sheets 1, 2, 3... in the listed order" ) );

    buttons->Add( annotate, 0 );
    buttons->Add( annotateMore, 0, wxRIGHT, 8 );
    buttons->Add( m_up, 0, wxRIGHT, 4 );
    buttons->Add( m_down, 0 );
    buttons->AddStretchSpacer();
    buttons->Add( new wxButton( this, wxID_OK ), 0, wxRIGHT, 4 );
    buttons->Add( new wxButton( this, wxID_CANCEL ), 0 );
    main->Add( buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8 );

    SetSizer( main );

    auto numberAll =
            [this]()
            {
                for( size_t ii = 0; ii < m_sheets.size(); ++ii )
                    m_sheets[ii].number = wxString::Format( wxS( "%d" ), (int) ii + 1 );

                fill();
            };

    annotate->Bind( wxEVT_BUTTON, [numberAll]( wxCommandEvent& ) { numberAll(); } );
    annotateMore->Bind( wxEVT_BUTTON,
                        [this, annotateMore, numberAll]( wxCommandEvent& )
                        {
                            wxMenu menu;
                            menu.Append( 1, _( "Annotate Sheet" ) );
                            menu.Append( 2, _( "Reset Sheet Numbers" ) );

                            int picked = annotateMore->GetPopupMenuSelectionFromUser(
                                    menu, wxPoint( 0, annotateMore->GetSize().y ) );

                            if( picked == 1 )
                            {
                                numberAll();
                            }
                            else if( picked == 2 )
                            {
                                // Back to the numbers the sheets have now
                                for( SHEET& sheet : m_sheets )
                                    sheet.number = sheet.path.GetPageNumber();

                                fill();
                            }
                        } );
    m_up->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { move( -1 ); } );
    m_down->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { move( 1 ); } );
    m_grid->Bind( wxEVT_GRID_SELECT_CELL,
                  [this]( wxGridEvent& aEvent )
                  {
                      aEvent.Skip();
                      CallAfter( [this]() { updateButtons(); } );
                  } );
    m_grid->Bind( wxEVT_GRID_CELL_CHANGED,
                  [this]( wxGridEvent& aEvent )
                  {
                      int row = aEvent.GetRow();

                      if( row >= 0 && row < (int) m_sheets.size() )
                          m_sheets[row].number = m_grid->GetCellValue( row, COL_NUMBER ).Trim();
                  } );

    fill();
    updateButtons();
    SetupStandardButtons();
    finishDialogSettings();
    Centre();
}


void DIALOG_ANNOTATE_COMPILED_SHEETS::fill()
{
    if( m_grid->GetNumberRows() != (int) m_sheets.size() )
    {
        if( m_grid->GetNumberRows() )
            m_grid->DeleteRows( 0, m_grid->GetNumberRows() );

        m_grid->AppendRows( (int) m_sheets.size() );
    }

    for( size_t ii = 0; ii < m_sheets.size(); ++ii )
    {
        m_grid->SetCellValue( ii, COL_DOCUMENT, m_sheets[ii].document );
        m_grid->SetCellValue( ii, COL_PATH, m_sheets[ii].hierarchy );
        m_grid->SetCellValue( ii, COL_NUMBER, m_sheets[ii].number );
    }
}


void DIALOG_ANNOTATE_COMPILED_SHEETS::move( int aBy )
{
    int row = m_grid->GetGridCursorRow();
    int to = row + aBy;

    if( row < 0 || to < 0 || to >= (int) m_sheets.size() )
        return;

    std::swap( m_sheets[row], m_sheets[to] );
    fill();
    m_grid->SetGridCursor( to, COL_DOCUMENT );
    m_grid->SelectRow( to );
    updateButtons();
}


void DIALOG_ANNOTATE_COMPILED_SHEETS::updateButtons()
{
    int row = m_grid->GetGridCursorRow();

    m_up->Enable( row > 0 );
    m_down->Enable( row >= 0 && row < (int) m_sheets.size() - 1 );
}


bool DIALOG_ANNOTATE_COMPILED_SHEETS::TransferDataFromWindow()
{
    if( m_grid->IsCellEditControlShown() )
        m_grid->SaveEditControlValue();

    SCH_COMMIT commit( m_frame );
    bool       changed = false;

    for( SHEET& sheet : m_sheets )
    {
        if( sheet.number.IsEmpty() || sheet.number == sheet.path.GetPageNumber() )
            continue;

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

    if( changed )
    {
        commit.Push( _( "Annotate Compiled Sheets" ) );
        m_frame->OnPageSettingsChange();
        m_frame->HardRedraw();
    }

    return true;
}
