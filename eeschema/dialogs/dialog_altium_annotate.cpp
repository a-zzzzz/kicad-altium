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

#include <dialogs/dialog_altium_annotate.h>

#include <confirm.h>
#include <dialogs/dialog_altium_eco.h>
#include <sch_commit.h>
#include <sch_reference_list.h>
#include <schematic_settings.h>
#include <sch_edit_frame.h>
#include <sch_field.h>
#include <sch_screen.h>
#include <sch_symbol.h>
#include <schematic.h>
#include <string_utils.h>
#include <tool/actions.h>
#include <tool/tool_manager.h>

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/dcbuffer.h>
#include <wx/filename.h>
#include <wx/grid.h>
#include <wx/listctrl.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>


namespace
{
// The dialog's choices are kept for the session, as Altium keeps them with the project
struct LAST_SETTINGS
{
    int                order = DIALOG_ALTIUM_ANNOTATE::ACROSS_THEN_DOWN;
    int                completePackages = 0;
    int                processLocation = 0;
    int                replaceSubParts = 0;
    std::set<wxString> params = { wxS( "Comment" ), wxS( "Library Reference" ) };
    std::set<wxString> notStrict;
};

LAST_SETTINGS g_last;

const wxString COMMENT = wxS( "Comment" );
const wxString LIB_REF = wxS( "Library Reference" );

enum SHEET_COLS { SC_ON = 0, SC_NAME, SC_SCOPE, SC_ORDER, SC_INDEX_ON, SC_INDEX, SC_SUFFIX, SC_COUNT };
enum PARAM_COLS { PC_USE = 0, PC_NAME, PC_STRICT, PC_COUNT };
enum CHANGE_COLS { CC_LOCK = 0, CC_CUR, CC_LOCK_SUB, CC_CUR_SUB, CC_NEW, CC_NEW_SUB, CC_SHEET, CC_COUNT };


wxColour mix( const wxColour& a, const wxColour& b, double t )
{
    return wxColour( a.Red() + ( b.Red() - a.Red() ) * t, a.Green() + ( b.Green() - a.Green() ) * t,
                     a.Blue() + ( b.Blue() - a.Blue() ) * t );
}


/// "R12" -> "R" and 12; "R?" -> "R" and -1.
void splitDesignator( const wxString& aRef, wxString& aPrefix, long& aNumber )
{
    size_t end = aRef.length();

    while( end > 0 && ( wxIsdigit( aRef[end - 1] ) || aRef[end - 1] == '?' ) )
        end--;

    aPrefix = aRef.Left( end );

    if( !aRef.Mid( end ).ToLong( &aNumber ) )
        aNumber = -1;
}


wxString boolCell( bool aValue )
{
    return aValue ? wxString( wxS( "1" ) ) : wxString();
}


bool isAnnotated( const wxString& aRef )
{
    return !aRef.IsEmpty() && !aRef.EndsWith( wxS( "?" ) );
}


/**
 * The picture under Order of Processing: four parts on a sheet, numbered in the chosen order,
 * with the path the numbering takes.
 */
class ORDER_PICTURE : public wxWindow
{
public:
    ORDER_PICTURE( wxWindow* aParent ) :
            wxWindow( aParent, wxID_ANY )
    {
        SetBackgroundStyle( wxBG_STYLE_PAINT );
        SetMinSize( FromDIP( wxSize( 180, 104 ) ) );
        Bind( wxEVT_PAINT, [this]( wxPaintEvent& ) { paint(); } );
    }

    void SetOrder( int aOrder )
    {
        m_order = aOrder;
        Refresh();
    }

private:
    void paint()
    {
        wxAutoBufferedPaintDC dc( this );
        wxSize                sz = GetClientSize();

        dc.SetBackground( wxBrush( GetParent()->GetBackgroundColour() ) );
        dc.Clear();

        // A sheet of paper with a thin border
        wxRect sheet( 0, 0, sz.x, sz.y );
        sheet.Deflate( FromDIP( 2 ) );
        dc.SetPen( wxPen( wxColour( 150, 150, 150 ) ) );
        dc.SetBrush( wxBrush( wxColour( 250, 248, 240 ) ) );
        dc.DrawRectangle( sheet );

        // Grid cells: 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right
        static const int cells[4][4] = { { 2, 0, 3, 1 },     // up then across
                                         { 2, 3, 0, 1 },     // across then up
                                         { 0, 2, 1, 3 },     // down then across
                                         { 0, 1, 2, 3 } };   // across then down

        wxPoint centres[4];
        int     cw = sheet.width / 2;
        int     ch = sheet.height / 2;

        for( int cell = 0; cell < 4; ++cell )
        {
            centres[cell] = wxPoint( sheet.x + cw / 2 + ( cell % 2 ) * cw,
                                     sheet.y + ch / 2 + ( cell / 2 ) * ch );
        }

        // Resistor bodies, then their labels
        wxFont font = GetFont();
        font.SetPointSize( font.GetPointSize() - 1 );
        dc.SetFont( font );

        int bw = FromDIP( 26 );
        int bh = FromDIP( 9 );

        for( int ii = 0; ii < 4; ++ii )
        {
            wxPoint c = centres[cells[m_order][ii]];

            dc.SetPen( wxPen( wxColour( 120, 40, 40 ), FromDIP( 1 ) ) );
            dc.DrawLine( c.x - bw, c.y + FromDIP( 6 ), c.x + bw, c.y + FromDIP( 6 ) );
            dc.SetBrush( wxBrush( wxColour( 255, 255, 200 ) ) );
            dc.DrawRectangle( c.x - bw / 2, c.y + FromDIP( 6 ) - bh / 2, bw, bh );

            dc.SetTextForeground( wxColour( 30, 30, 120 ) );
            wxString label = wxString::Format( wxS( "R%d" ), ii + 1 );
            dc.DrawText( label, c.x - bw, c.y - FromDIP( 12 ) );
        }

        // The numbering path
        dc.SetPen( wxPen( wxColour( 20, 150, 90 ), FromDIP( 3 ) ) );

        for( int ii = 0; ii < 3; ++ii )
        {
            wxPoint a = centres[cells[m_order][ii]];
            wxPoint b = centres[cells[m_order][ii + 1]];
            dc.DrawLine( a, b );
        }

        // Arrow head at the last part
        wxPoint a = centres[cells[m_order][2]];
        wxPoint b = centres[cells[m_order][3]];
        double  len = std::hypot( b.x - a.x, b.y - a.y );

        if( len > 0 )
        {
            double  ux = ( b.x - a.x ) / len;
            double  uy = ( b.y - a.y ) / len;
            int     s = FromDIP( 8 );
            wxPoint head[3] = { b,
                                wxPoint( b.x - ux * s - uy * s / 2, b.y - uy * s + ux * s / 2 ),
                                wxPoint( b.x - ux * s + uy * s / 2, b.y - uy * s - ux * s / 2 ) };
            dc.SetBrush( wxBrush( wxColour( 20, 150, 90 ) ) );
            dc.DrawPolygon( 3, head );
        }
    }

    int m_order = 0;
};


/// A group box like Altium's: a bold title over the content.
wxStaticText* sectionTitle( wxWindow* aParent, const wxString& aTitle )
{
    wxStaticText* text = new wxStaticText( aParent, wxID_ANY, aTitle );
    text->SetFont( text->GetFont().Bold() );
    return text;
}


void setupGrid( wxGrid* aGrid, int aCols )
{
    aGrid->CreateGrid( 0, aCols );
    aGrid->SetRowLabelSize( 0 );
    aGrid->SetColLabelSize( aGrid->FromDIP( 22 ) );
    aGrid->SetDefaultRowSize( aGrid->FromDIP( 20 ) );
    aGrid->EnableDragRowSize( false );
    aGrid->SetCellHighlightPenWidth( 0 );
    aGrid->SetSelectionMode( wxGrid::wxGridSelectRows );
    aGrid->SetLabelFont( aGrid->GetFont() );
}


void setBoolColumn( wxGrid* aGrid, int aCol )
{
    wxGridCellAttr* attr = new wxGridCellAttr;
    attr->SetRenderer( new wxGridCellBoolRenderer() );
    attr->SetReadOnly( true );       // toggled by a single click (see the click handlers)
    attr->SetAlignment( wxALIGN_CENTER, wxALIGN_CENTER );
    aGrid->SetColAttr( aCol, attr );
}


void setReadOnlyColumn( wxGrid* aGrid, int aCol )
{
    wxGridCellAttr* attr = new wxGridCellAttr;
    attr->SetReadOnly( true );
    aGrid->SetColAttr( aCol, attr );
}


} // namespace


DIALOG_ALTIUM_ANNOTATE::DIALOG_ALTIUM_ANNOTATE( SCH_EDIT_FRAME* aParent ) :
        DIALOG_SHIM( aParent, wxID_ANY, _( "Annotate" ),
                     wxDefaultPosition, wxDefaultSize,
                     wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxMAXIMIZE_BOX ),
        m_frame( aParent ),
        m_filling( false )
{
    wxBoxSizer* main = new wxBoxSizer( wxVERTICAL );
    wxBoxSizer* columns = new wxBoxSizer( wxHORIZONTAL );

    // ---- Left: Schematic Annotation Configuration ----
    wxBoxSizer* left = new wxBoxSizer( wxVERTICAL );
    left->Add( sectionTitle( this, _( "Schematic Annotation Configuration" ) ), 0, wxBOTTOM, 6 );

    wxBoxSizer* config = new wxBoxSizer( wxHORIZONTAL );

    // Order of processing, process location, sub-parts
    wxBoxSizer* orderBox = new wxBoxSizer( wxVERTICAL );
    orderBox->Add( sectionTitle( this, _( "Order of Processing" ) ), 0, wxBOTTOM, 4 );

    m_order = new wxChoice( this, wxID_ANY );
    m_order->Append( _( "Up Then Across" ) );
    m_order->Append( _( "Across Then Up" ) );
    m_order->Append( _( "Down Then Across" ) );
    m_order->Append( _( "Across Then Down" ) );
    m_order->SetSelection( g_last.order );
    orderBox->Add( m_order, 0, wxEXPAND | wxBOTTOM, 4 );

    ORDER_PICTURE* picture = new ORDER_PICTURE( this );
    picture->SetOrder( g_last.order );
    m_orderPicture = picture;
    orderBox->Add( picture, 0, wxEXPAND | wxBOTTOM, 8 );

    orderBox->Add( sectionTitle( this, _( "Process Location of" ) ), 0, wxBOTTOM, 4 );
    m_processLocation = new wxChoice( this, wxID_ANY );
    m_processLocation->Append( _( "Designator" ) );
    m_processLocation->Append( _( "Part" ) );
    m_processLocation->SetSelection( g_last.processLocation );
    orderBox->Add( m_processLocation, 0, wxEXPAND | wxBOTTOM, 8 );

    orderBox->Add( sectionTitle( this, _( "Replace Sub-Parts" ) ), 0, wxBOTTOM, 4 );
    m_replaceSubParts = new wxChoice( this, wxID_ANY );
    m_replaceSubParts->Append( _( "Off" ) );
    m_replaceSubParts->Append( _( "On" ) );
    m_replaceSubParts->SetSelection( g_last.replaceSubParts );
    m_replaceSubParts->SetToolTip( _( "On: parts of multi-part components may be given another "
                                      "sub-part letter to fill packages" ) );
    orderBox->Add( m_replaceSubParts, 0, wxEXPAND );

    config->Add( orderBox, 0, wxRIGHT, 10 );

    // Matching options
    wxBoxSizer* matchBox = new wxBoxSizer( wxVERTICAL );
    matchBox->Add( sectionTitle( this, _( "Matching Options" ) ), 0, wxBOTTOM, 4 );

    wxBoxSizer* complete = new wxBoxSizer( wxHORIZONTAL );
    complete->Add( new wxStaticText( this, wxID_ANY, _( "Complete Existing Packages" ) ), 0,
                   wxALIGN_CENTER_VERTICAL | wxRIGHT, 6 );
    m_completePackages = new wxChoice( this, wxID_ANY );
    m_completePackages->Append( _( "None" ) );
    m_completePackages->Append( _( "Per Sheet" ) );
    m_completePackages->Append( _( "Whole Project" ) );
    m_completePackages->SetSelection( g_last.completePackages );
    complete->Add( m_completePackages, 1, wxALIGN_CENTER_VERTICAL );
    matchBox->Add( complete, 0, wxEXPAND | wxBOTTOM, 4 );

    m_paramsGrid = new wxGrid( this, wxID_ANY );
    setupGrid( m_paramsGrid, PC_COUNT );
    m_paramsGrid->SetColLabelValue( PC_USE, wxEmptyString );
    m_paramsGrid->SetColLabelValue( PC_NAME, _( "Component Parameter" ) );
    m_paramsGrid->SetColLabelValue( PC_STRICT, _( "Strictly" ) );
    m_paramsGrid->SetColSize( PC_USE, FromDIP( 24 ) );
    m_paramsGrid->SetColSize( PC_NAME, FromDIP( 190 ) );
    m_paramsGrid->SetColSize( PC_STRICT, FromDIP( 56 ) );
    setBoolColumn( m_paramsGrid, PC_USE );
    setReadOnlyColumn( m_paramsGrid, PC_NAME );
    setBoolColumn( m_paramsGrid, PC_STRICT );
    m_paramsGrid->SetMinSize( FromDIP( wxSize( 280, 190 ) ) );
    matchBox->Add( m_paramsGrid, 1, wxEXPAND );

    config->Add( matchBox, 1, wxEXPAND );
    left->Add( config, 0, wxEXPAND | wxBOTTOM, 8 );

    // Sheets to annotate
    left->Add( sectionTitle( this, _( "Schematic Sheets To Annotate" ) ),
               0, wxBOTTOM, 4 );

    m_sheetsGrid = new wxGrid( this, wxID_ANY );
    setupGrid( m_sheetsGrid, SC_COUNT );
    m_sheetsGrid->SetColLabelValue( SC_ON, wxEmptyString );
    m_sheetsGrid->SetColLabelValue( SC_NAME, _( "Schematic Sheet" ) );
    m_sheetsGrid->SetColLabelValue( SC_SCOPE, _( "Annotation Scope" ) );
    m_sheetsGrid->SetColLabelValue( SC_ORDER, _( "Order" ) );
    m_sheetsGrid->SetColLabelValue( SC_INDEX_ON, _( "Index Control" ) );
    m_sheetsGrid->SetColLabelValue( SC_INDEX, _( "Start Index" ) );
    m_sheetsGrid->SetColLabelValue( SC_SUFFIX, _( "Add Suffix" ) );

    const int sheetWidths[] = { 24, 170, 120, 46, 84, 70, 66 };

    for( int col = 0; col < SC_COUNT; ++col )
        m_sheetsGrid->SetColSize( col, FromDIP( sheetWidths[col] ) );

    setBoolColumn( m_sheetsGrid, SC_ON );
    setReadOnlyColumn( m_sheetsGrid, SC_NAME );
    setBoolColumn( m_sheetsGrid, SC_INDEX_ON );

    {
        wxGridCellAttr* attr = new wxGridCellAttr;
        wxString        scopes[] = { _( "All" ), _( "Ignore Selected Parts" ),
                                     _( "Only Selected Parts" ) };
        attr->SetEditor( new wxGridCellChoiceEditor( 3, scopes ) );
        m_sheetsGrid->SetColAttr( SC_SCOPE, attr );

        attr = new wxGridCellAttr;
        attr->SetEditor( new wxGridCellNumberEditor( 0, 9999 ) );
        m_sheetsGrid->SetColAttr( SC_ORDER, attr );

        attr = new wxGridCellAttr;
        attr->SetEditor( new wxGridCellNumberEditor( 0, 999999 ) );
        m_sheetsGrid->SetColAttr( SC_INDEX, attr );
    }

    m_sheetsGrid->SetMinSize( FromDIP( wxSize( 600, 150 ) ) );
    left->Add( m_sheetsGrid, 1, wxEXPAND | wxBOTTOM, 6 );

    wxBoxSizer* allButtons = new wxBoxSizer( wxHORIZONTAL );
    wxButton*   allOn = new wxButton( this, wxID_ANY, _( "All On" ) );
    wxButton*   allOff = new wxButton( this, wxID_ANY, _( "All Off" ) );
    allButtons->Add( allOn, 0, wxRIGHT, 4 );
    allButtons->Add( allOff, 0 );
    left->Add( allButtons, 0 );

    columns->Add( left, 0, wxEXPAND | wxALL, 8 );

    // ---- Right: Proposed Change List ----
    wxBoxSizer* right = new wxBoxSizer( wxVERTICAL );
    right->Add( sectionTitle( this, _( "Proposed Change List" ) ), 0, wxBOTTOM, 6 );

    m_changesGrid = new wxGrid( this, wxID_ANY );
    setupGrid( m_changesGrid, CC_COUNT );
    m_changesGrid->SetColLabelValue( CC_LOCK, _( "Lock" ) );
    m_changesGrid->SetColLabelValue( CC_CUR, _( "Current Designator" ) );
    m_changesGrid->SetColLabelValue( CC_LOCK_SUB, _( "Lock" ) );
    m_changesGrid->SetColLabelValue( CC_CUR_SUB, _( "Sub" ) );
    m_changesGrid->SetColLabelValue( CC_NEW, _( "Proposed Designator" ) );
    m_changesGrid->SetColLabelValue( CC_NEW_SUB, _( "Sub" ) );
    m_changesGrid->SetColLabelValue( CC_SHEET, _( "Location of Part" ) );

    const int changeWidths[] = { 40, 130, 40, 40, 130, 40, 200 };

    for( int col = 0; col < CC_COUNT; ++col )
        m_changesGrid->SetColSize( col, FromDIP( changeWidths[col] ) );

    m_changesGrid->SetColLabelAlignment( wxALIGN_LEFT, wxALIGN_CENTER );
    setBoolColumn( m_changesGrid, CC_LOCK );
    setBoolColumn( m_changesGrid, CC_LOCK_SUB );

    for( int col : { CC_CUR, CC_CUR_SUB, CC_NEW, CC_NEW_SUB, CC_SHEET } )
        setReadOnlyColumn( m_changesGrid, col );

    m_changesGrid->SetMinSize( FromDIP( wxSize( 560, 320 ) ) );
    right->Add( m_changesGrid, 1, wxEXPAND | wxBOTTOM, 6 );

    right->Add( sectionTitle( this, _( "Annotation Summary" ) ), 0, wxBOTTOM, 2 );
    m_summary = new wxStaticText( this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                  FromDIP( wxSize( 540, 70 ) ) );
    right->Add( m_summary, 0, wxEXPAND | wxBOTTOM, 6 );

    wxBoxSizer* actions = new wxBoxSizer( wxHORIZONTAL );
    wxButton*   update = new wxButton( this, wxID_ANY, _( "Update Changes List" ) );
    wxButton*   reset = new wxButton( this, wxID_ANY, _( "Reset All" ) );
    wxButton*   resetMore = new wxButton( this, wxID_ANY, wxS( "▾" ), wxDefaultPosition,
                                          wxDefaultSize, wxBU_EXACTFIT );
    wxButton*   back = new wxButton( this, wxID_ANY, _( "Back Annotate" ) );
    m_acceptButton = new wxButton( this, wxID_ANY, _( "Accept Changes (Create ECO)" ) );

    reset->SetToolTip( _( "Set the proposed designators to their unannotated form (e.g. R?)" ) );
    back->SetToolTip( _( "Take the designators from the PCB (Update Schematic from PCB)" ) );

    actions->Add( update, 0, wxRIGHT, 4 );
    actions->Add( reset, 0 );
    actions->Add( resetMore, 0, wxRIGHT, 4 );
    actions->Add( back, 0, wxRIGHT, 4 );
    actions->AddStretchSpacer();
    actions->Add( m_acceptButton, 0 );
    right->Add( actions, 0, wxEXPAND );

    columns->Add( right, 1, wxEXPAND | wxTOP | wxRIGHT | wxBOTTOM, 8 );
    main->Add( columns, 1, wxEXPAND );

    wxBoxSizer* bottom = new wxBoxSizer( wxHORIZONTAL );
    bottom->AddStretchSpacer();
    bottom->Add( new wxButton( this, wxID_CANCEL, _( "Close" ) ), 0 );
    main->Add( bottom, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8 );

    SetSizer( main );

    // ---- Behaviour ----
    m_order->Bind( wxEVT_CHOICE,
                   [this, picture]( wxCommandEvent& )
                   {
                       g_last.order = m_order->GetSelection();
                       picture->SetOrder( g_last.order );
                   } );
    m_processLocation->Bind( wxEVT_CHOICE, [this]( wxCommandEvent& )
                             { g_last.processLocation = m_processLocation->GetSelection(); } );
    m_replaceSubParts->Bind( wxEVT_CHOICE, [this]( wxCommandEvent& )
                             { g_last.replaceSubParts = m_replaceSubParts->GetSelection(); } );
    m_completePackages->Bind( wxEVT_CHOICE,
                              [this]( wxCommandEvent& )
                              {
                                  g_last.completePackages = m_completePackages->GetSelection();
                                  updateSummary();
                              } );

    allOn->Bind( wxEVT_BUTTON,
                 [this]( wxCommandEvent& )
                 {
                     for( SHEET& sheet : m_sheets )
                         sheet.enabled = true;

                     fillSheetsGrid();
                     updateSummary();
                 } );
    allOff->Bind( wxEVT_BUTTON,
                  [this]( wxCommandEvent& )
                  {
                      for( SHEET& sheet : m_sheets )
                          sheet.enabled = false;

                      fillSheetsGrid();
                      updateSummary();
                  } );

    update->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { updateChangesList(); } );
    reset->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { resetProposed( false ); } );
    resetMore->Bind( wxEVT_BUTTON,
                     [this, resetMore]( wxCommandEvent& )
                     {
                         wxMenu menu;
                         menu.Append( 1, _( "Reset All" ) );
                         menu.Append( 2, _( "Reset Duplicates" ) );

                         int picked = resetMore->GetPopupMenuSelectionFromUser(
                                 menu, wxPoint( 0, resetMore->GetSize().y ) );

                         if( picked == 1 || picked == 2 )
                             resetProposed( picked == 2 );
                     } );
    back->Bind( wxEVT_BUTTON,
                [this]( wxCommandEvent& )
                {
                    // Altium reads the PCB's changes; KiCad updates the schematic from the board
                    SCH_EDIT_FRAME* frame = m_frame;
                    EndModal( wxID_CANCEL );
                    frame->CallAfter( [frame]()
                            { frame->GetToolManager()->RunAction( ACTIONS::updateSchematicFromPcb ); } );
                } );
    m_acceptButton->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { acceptChanges(); } );

    m_sheetsGrid->Bind( wxEVT_GRID_CELL_LEFT_CLICK, &DIALOG_ALTIUM_ANNOTATE::onSheetsClick, this );
    m_sheetsGrid->Bind( wxEVT_GRID_CELL_CHANGED, &DIALOG_ALTIUM_ANNOTATE::onSheetsChanged, this );
    m_paramsGrid->Bind( wxEVT_GRID_CELL_LEFT_CLICK, &DIALOG_ALTIUM_ANNOTATE::onParamsClick, this );
    m_changesGrid->Bind( wxEVT_GRID_CELL_LEFT_CLICK, &DIALOG_ALTIUM_ANNOTATE::onChangesClick, this );

    collect();
    fillParamsGrid();
    fillSheetsGrid();
    fillChangesGrid();
    updateSummary();

    finishDialogSettings();
    SetSize( FromDIP( wxSize( 1320, 720 ) ) );
    Centre();
}


void DIALOG_ALTIUM_ANNOTATE::collect()
{
    m_parts.clear();
    m_sheets.clear();

    SCH_SHEET_LIST hierarchy = m_frame->Schematic().Hierarchy();

    // A sheet file used more than once is named with its instance path
    std::map<wxString, int> fileUses;

    for( const SCH_SHEET_PATH& path : hierarchy )
        fileUses[path.LastScreen()->GetFileName()]++;

    std::set<wxString> paramNames;

    for( const SCH_SHEET_PATH& path : hierarchy )
    {
        SHEET    sheet;
        wxString file = wxFileName( path.LastScreen()->GetFileName() ).GetFullName();

        sheet.path = path;
        sheet.order = (int) m_sheets.size();
        sheet.name = fileUses[path.LastScreen()->GetFileName()] > 1
                             ? file + wxS( "  " ) + path.PathHumanReadable( false )
                             : file;

        for( SCH_ITEM* item : path.LastScreen()->Items().OfType( SCH_SYMBOL_T ) )
        {
            SCH_SYMBOL* symbol = static_cast<SCH_SYMBOL*>( item );

            if( symbol->IsPower() )
                continue;

            PART part;
            part.symbol = symbol;
            part.sheet = path;
            part.sheetRow = (int) m_sheets.size();
            part.current = symbol->GetRef( &path, false );

            if( part.current.StartsWith( wxS( "#" ) ) )
                continue;

            long number = -1;
            splitDesignator( part.current, part.prefix, number );

            if( part.prefix.IsEmpty() )
                part.prefix = symbol->GetPrefix();

            part.unitCount = std::max( 1, symbol->GetUnitCount() );
            part.currentUnit = std::max( 1, symbol->GetUnitSelection( &path ) );
            part.proposed = part.current;
            part.proposedUnit = part.currentUnit;
            part.selected = symbol->IsSelected();
            part.partPos = symbol->GetPosition();
            part.designatorPos = symbol->GetField( FIELD_T::REFERENCE )->GetPosition();
            part.libId = symbol->GetLibId().Format().wx_str();

            // The Designator lock of the Properties panel keeps the designator as it is
            if( const SCH_FIELD* lock = symbol->GetField( wxS( "Designator Locked" ) ) )
                part.lockDesignator = lock->GetText() == wxS( "1" );

            for( const SCH_FIELD& field : symbol->GetFields() )
            {
                if( !field.IsMandatory() && !field.IsPrivate() && !field.GetName().IsEmpty() )
                    paramNames.insert( field.GetName() );
            }

            m_parts.push_back( part );
        }

        m_sheets.push_back( sheet );
    }

    // Comment (the value) and Footprint first, then the components' own parameters
    m_params.clear();

    auto addParam =
            [&]( const wxString& aName )
            {
                PARAM param;
                param.name = aName;
                param.use = g_last.params.count( aName ) > 0;
                param.strictly = g_last.notStrict.count( aName ) == 0;
                m_params.push_back( param );
            };

    addParam( COMMENT );
    addParam( wxS( "Description" ) );
    addParam( wxS( "Footprint" ) );
    addParam( LIB_REF );

    for( const wxString& name : paramNames )
        addParam( name );

    for( PART& part : m_parts )
    {
        part.paramValues.clear();

        for( const PARAM& param : m_params )
        {
            if( param.name == LIB_REF )
            {
                part.paramValues.push_back( part.libId );
                continue;
            }

            const SCH_FIELD* field = param.name == COMMENT
                                             ? part.symbol->GetField( FIELD_T::VALUE )
                                     : param.name == wxS( "Footprint" )
                                             ? part.symbol->GetField( FIELD_T::FOOTPRINT )
                                     : param.name == wxS( "Description" )
                                             ? part.symbol->GetField( FIELD_T::DESCRIPTION )
                                             : part.symbol->GetField( param.name );

            part.paramValues.push_back( field ? field->GetShownText( &part.sheet, false )
                                              : wxString() );
        }
    }

    // Listed by designator, as Altium lists them
    std::stable_sort( m_parts.begin(), m_parts.end(),
                      []( const PART& a, const PART& b )
                      {
                          int cmp = StrNumCmp( a.current, b.current, true );

                          if( cmp != 0 )
                              return cmp < 0;

                          return a.currentUnit < b.currentUnit;
                      } );
}


void DIALOG_ALTIUM_ANNOTATE::fillParamsGrid()
{
    m_filling = true;
    m_paramsGrid->ClearGrid();

    if( m_paramsGrid->GetNumberRows() )
        m_paramsGrid->DeleteRows( 0, m_paramsGrid->GetNumberRows() );

    m_paramsGrid->AppendRows( (int) m_params.size() );

    for( size_t ii = 0; ii < m_params.size(); ++ii )
    {
        m_paramsGrid->SetCellValue( ii, PC_USE, boolCell( m_params[ii].use ) );
        m_paramsGrid->SetCellValue( ii, PC_NAME, m_params[ii].name );
        m_paramsGrid->SetCellValue( ii, PC_STRICT, boolCell( m_params[ii].strictly ) );
    }

    m_filling = false;
}


void DIALOG_ALTIUM_ANNOTATE::fillSheetsGrid()
{
    static const wxString scopes[] = { _( "All" ), _( "Ignore Selected Parts" ),
                                       _( "Only Selected Parts" ) };

    m_filling = true;

    if( m_sheetsGrid->GetNumberRows() != (int) m_sheets.size() )
    {
        if( m_sheetsGrid->GetNumberRows() )
            m_sheetsGrid->DeleteRows( 0, m_sheetsGrid->GetNumberRows() );

        m_sheetsGrid->AppendRows( (int) m_sheets.size() );
    }

    for( size_t ii = 0; ii < m_sheets.size(); ++ii )
    {
        const SHEET& sheet = m_sheets[ii];

        m_sheetsGrid->SetCellValue( ii, SC_ON, boolCell( sheet.enabled ) );
        m_sheetsGrid->SetCellValue( ii, SC_NAME, sheet.name );
        m_sheetsGrid->SetCellValue( ii, SC_SCOPE, scopes[sheet.scope] );
        m_sheetsGrid->SetCellValue( ii, SC_ORDER, wxString::Format( wxS( "%d" ), sheet.order ) );
        m_sheetsGrid->SetCellValue( ii, SC_INDEX_ON, boolCell( sheet.useStartIndex ) );
        m_sheetsGrid->SetCellValue( ii, SC_INDEX, wxString::Format( wxS( "%ld" ), sheet.startIndex ) );
        m_sheetsGrid->SetCellValue( ii, SC_SUFFIX, sheet.suffix );
    }

    m_filling = false;
}


void DIALOG_ALTIUM_ANNOTATE::fillChangesGrid()
{
    m_filling = true;

    if( m_changesGrid->GetNumberRows() != (int) m_parts.size() )
    {
        if( m_changesGrid->GetNumberRows() )
            m_changesGrid->DeleteRows( 0, m_changesGrid->GetNumberRows() );

        m_changesGrid->AppendRows( (int) m_parts.size() );
    }

    wxColour normal = m_changesGrid->GetDefaultCellTextColour();
    wxColour changed = wxSystemSettings::GetAppearance().IsDark() ? wxColour( 255, 200, 80 )
                                                                  : wxColour( 0, 90, 200 );

    auto sub =
            []( const PART& aPart, int aUnit ) -> wxString
            {
                return aPart.unitCount > 1 ? wxString::Format( wxS( "%d" ), aUnit ) : wxString();
            };

    for( size_t ii = 0; ii < m_parts.size(); ++ii )
    {
        const PART& part = m_parts[ii];
        bool        desChanged = part.proposed != part.current;
        bool        subChanged = part.proposedUnit != part.currentUnit;

        m_changesGrid->SetCellValue( ii, CC_LOCK, boolCell( part.lockDesignator ) );
        m_changesGrid->SetCellValue( ii, CC_CUR, part.current );
        m_changesGrid->SetCellValue( ii, CC_LOCK_SUB, boolCell( part.lockSub ) );
        m_changesGrid->SetCellValue( ii, CC_CUR_SUB, sub( part, part.currentUnit ) );
        m_changesGrid->SetCellValue( ii, CC_NEW, part.proposed );
        m_changesGrid->SetCellValue( ii, CC_NEW_SUB, sub( part, part.proposedUnit ) );
        m_changesGrid->SetCellValue( ii, CC_SHEET, m_sheets[part.sheetRow].name );

        m_changesGrid->SetCellTextColour( ii, CC_NEW, desChanged ? changed : normal );
        m_changesGrid->SetCellTextColour( ii, CC_NEW_SUB, subChanged ? changed : normal );
    }

    m_changesGrid->ForceRefresh();
    m_filling = false;

    updateSummary();
}


void DIALOG_ALTIUM_ANNOTATE::updateSummary()
{
    int enabled = 0;

    for( const SHEET& sheet : m_sheets )
        enabled += sheet.enabled ? 1 : 0;

    int used = 0;
    int strict = 0;

    for( const PARAM& param : m_params )
    {
        if( param.use )
        {
            used++;
            strict += param.strictly ? 1 : 0;
        }
    }

    int changes = 0;

    for( const PART& part : m_parts )
    {
        if( part.proposed != part.current || part.proposedUnit != part.currentUnit )
            changes++;
    }

    wxString text;

    if( enabled == (int) m_sheets.size() )
        text = _( "Annotation is enabled for all schematic documents." );
    else if( enabled == 0 )
        text = _( "Annotation is disabled for all schematic documents." );
    else
        text.Printf( _( "Annotation is enabled for %d of %d schematic documents." ), enabled,
                     (int) m_sheets.size() );

    text << wxS( " " );

    if( used == 0 )
        text << _( "Parts will be matched by component only." );
    else
        text << wxString::Format( _( "Parts will be matched using %d parameters, %d of which will "
                                     "be strictly matched." ),
                                  used, strict );

    text << wxS( " " );

    switch( m_completePackages->GetSelection() )
    {
    case 1:  text << _( "Existing packages will be completed on each sheet." ); break;
    case 2:  text << _( "Existing packages will be completed across the project." ); break;
    default: text << _( "Existing packages will not be completed. All new parts will be put into "
                        "new packages." );
             break;
    }

    if( changes > 0 )
        text << wxS( "\n" ) << wxString::Format( _( "%d changes proposed." ), changes );

    m_summary->SetLabel( text );
    m_summary->Wrap( std::max( m_changesGrid->GetClientSize().x, FromDIP( 300 ) ) );
    Layout();
    m_acceptButton->Enable( changes > 0 );
}


bool DIALOG_ALTIUM_ANNOTATE::processes( const PART& aPart ) const
{
    const SHEET& sheet = m_sheets[aPart.sheetRow];

    if( !sheet.enabled || aPart.lockDesignator )
        return false;

    if( sheet.scope == 1 && aPart.selected )
        return false;

    if( sheet.scope == 2 && !aPart.selected )
        return false;

    return true;
}


void DIALOG_ALTIUM_ANNOTATE::updateChangesList()
{
    std::vector<std::pair<wxString, int>> previous;

    for( const PART& part : m_parts )
        previous.emplace_back( part.proposed, part.proposedUnit );

    updateProposed();

    int fromPrevious = 0;
    int fromOriginal = 0;

    for( size_t ii = 0; ii < m_parts.size(); ++ii )
    {
        const PART& part = m_parts[ii];

        if( part.proposed != previous[ii].first || part.proposedUnit != previous[ii].second )
            fromPrevious++;

        if( part.proposed != part.current || part.proposedUnit != part.currentUnit )
            fromOriginal++;
    }

    wxString msg = wxString::Format( _( "%d change(s) were made from previous state\n"
                                        "%d change(s) were made from original state" ),
                                     fromPrevious, fromOriginal );

    wxMessageDialog info( this, msg, _( "Information" ), wxOK | wxICON_INFORMATION );
    info.SetExtendedMessage( wxEmptyString );
    info.SetMessage( _( "Change(s) made" ) );
    info.SetExtendedMessage( msg );
    info.ShowModal();
}


void DIALOG_ALTIUM_ANNOTATE::updateProposed()
{
    // Designators kept as they are (annotated, locked or out of scope) are taken
    std::map<wxString, std::set<long>> used;   // prefix + suffix -> numbers

    struct PACKAGE
    {
        wxString       designator;
        wxString       key;
        int            sheetRow;
        std::set<int>  units;
        int            unitCount;
        bool           existing;
    };

    std::vector<PACKAGE>   packages;
    std::vector<PART*>     todo;

    auto matchKey =
            [&]( const PART& aPart ) -> wxString
            {
                wxString key = aPart.prefix;

                for( size_t ii = 0; ii < m_params.size(); ++ii )
                {
                    if( m_params[ii].use )
                        key << wxS( "\t" ) << aPart.paramValues[ii];
                }

                return key;
            };

    auto findPackage =
            [&]( const wxString& aDesignator ) -> PACKAGE*
            {
                for( PACKAGE& pkg : packages )
                {
                    if( pkg.designator == aDesignator )
                        return &pkg;
                }

                return nullptr;
            };

    for( PART& part : m_parts )
    {
        bool keep = !processes( part ) || isAnnotated( part.proposed );

        if( !keep )
        {
            todo.push_back( &part );
            continue;
        }

        if( !isAnnotated( part.proposed ) )
            continue;

        wxString prefix;
        long     number = -1;
        wxString suffix = m_sheets[part.sheetRow].suffix;
        wxString base = part.proposed;

        if( !suffix.IsEmpty() && base.EndsWith( suffix ) )
            base = base.Left( base.length() - suffix.length() );
        else
            suffix.clear();

        splitDesignator( base, prefix, number );

        if( number >= 0 )
            used[prefix + wxS( "\t" ) + suffix].insert( number );

        if( part.unitCount > 1 )
        {
            PACKAGE* pkg = findPackage( part.proposed );

            if( !pkg )
            {
                packages.push_back( { part.proposed, matchKey( part ), part.sheetRow, {},
                                      part.unitCount, true } );
                pkg = &packages.back();
            }

            pkg->units.insert( part.proposedUnit );
        }
    }

    // Processing order: sheets by their Order, then the parts' locations on the sheet
    int  order = m_order->GetSelection();
    bool byPart = m_processLocation->GetSelection() == 1;

    std::stable_sort( todo.begin(), todo.end(),
                      [&]( const PART* a, const PART* b )
                      {
                          int oa = m_sheets[a->sheetRow].order;
                          int ob = m_sheets[b->sheetRow].order;

                          if( oa != ob )
                              return oa < ob;

                          if( a->sheetRow != b->sheetRow )
                              return a->sheetRow < b->sheetRow;

                          VECTOR2I pa = byPart ? a->partPos : a->designatorPos;
                          VECTOR2I pb = byPart ? b->partPos : b->designatorPos;

                          // Y grows downwards on the sheet: "up" is decreasing Y
                          switch( order )
                          {
                          case UP_THEN_ACROSS:
                              return pa.x != pb.x ? pa.x < pb.x : pa.y > pb.y;
                          case ACROSS_THEN_UP:
                              return pa.y != pb.y ? pa.y > pb.y : pa.x < pb.x;
                          case DOWN_THEN_ACROSS:
                              return pa.x != pb.x ? pa.x < pb.x : pa.y < pb.y;
                          default:
                              return pa.y != pb.y ? pa.y < pb.y : pa.x < pb.x;
                          }
                      } );

    int  complete = m_completePackages->GetSelection();
    bool replaceSubs = m_replaceSubParts->GetSelection() == 1;

    auto nextNumber =
            [&]( const PART& aPart ) -> wxString
            {
                const SHEET& sheet = m_sheets[aPart.sheetRow];
                long         start = sheet.useStartIndex ? std::max( 0L, sheet.startIndex ) : 1;
                std::set<long>& numbers = used[aPart.prefix + wxS( "\t" ) + sheet.suffix];
                long         n = start;

                while( numbers.count( n ) )
                    n++;

                numbers.insert( n );
                return aPart.prefix + wxString::Format( wxS( "%ld" ), n ) + sheet.suffix;
            };

    for( PART* part : todo )
    {
        if( part->unitCount <= 1 )
        {
            part->proposed = nextNumber( *part );
            part->proposedUnit = part->currentUnit;
            continue;
        }

        // Multi-part component: fill a package with the same component and matching parameters
        wxString key = matchKey( *part );
        PACKAGE* target = nullptr;
        int      unit = part->currentUnit;

        for( PACKAGE& pkg : packages )
        {
            if( pkg.key != key || pkg.unitCount != part->unitCount )
                continue;

            if( pkg.existing && ( complete == 0 || ( complete == 1 && pkg.sheetRow != part->sheetRow ) ) )
                continue;

            if( !part->lockSub && replaceSubs )
            {
                for( int u = 1; u <= pkg.unitCount; ++u )
                {
                    if( !pkg.units.count( u ) )
                    {
                        target = &pkg;
                        unit = u;
                        break;
                    }
                }
            }
            else if( !pkg.units.count( part->currentUnit ) )
            {
                target = &pkg;
                unit = part->currentUnit;
            }

            if( target )
                break;
        }

        if( !target )
        {
            packages.push_back( { nextNumber( *part ), key, part->sheetRow, {}, part->unitCount,
                                  false } );
            target = &packages.back();
            unit = ( replaceSubs && !part->lockSub ) ? 1 : part->currentUnit;
        }

        target->units.insert( unit );
        part->proposed = target->designator;
        part->proposedUnit = unit;
    }

    fillChangesGrid();
}


void DIALOG_ALTIUM_ANNOTATE::resetProposed( bool aDuplicatesOnly )
{
    if( !aDuplicatesOnly )
    {
        for( PART& part : m_parts )
        {
            if( processes( part ) )
            {
                part.proposed = part.prefix + wxS( "?" );
                part.proposedUnit = part.currentUnit;
            }
        }
    }
    else
    {
        // The first use of a designator (and sub-part) keeps it; the others are reset
        std::map<wxString, int> uses;

        for( PART& part : m_parts )
        {
            if( !isAnnotated( part.proposed ) )
                continue;

            wxString key = part.proposed;

            if( part.unitCount > 1 )
                key << wxS( "\t" ) << part.proposedUnit;

            if( uses[key]++ > 0 && processes( part ) )
                part.proposed = part.prefix + wxS( "?" );
        }
    }

    fillChangesGrid();
}


void DIALOG_ALTIUM_ANNOTATE::acceptChanges()
{
    std::vector<DIALOG_ALTIUM_ECO::ITEM> listed;
    std::vector<PART*>                   changed;

    for( PART& part : m_parts )
    {
        if( part.proposed == part.current && part.proposedUnit == part.currentUnit )
            continue;

        auto label =
                []( const PART& aPart, const wxString& aRef, int aUnit )
                {
                    return aPart.unitCount > 1 ? aRef + aPart.symbol->SubReference( aUnit, false )
                                               : aRef;
                };

        DIALOG_ALTIUM_ECO::ITEM item;
        item.group = _( "Annotate Component" );
        item.action = _( "Modify" );
        item.object = label( part, part.current, part.currentUnit ) + wxS( " -> " )
                      + label( part, part.proposed, part.proposedUnit );
        item.preposition = _( "In" );
        item.document = m_sheets[part.sheetRow].name;
        listed.push_back( item );
        changed.push_back( &part );
    }

    if( changed.empty() )
        return;

    // Executed in one commit: one undo step
    DIALOG_ALTIUM_ECO eco( this, listed,
            [&]( std::vector<DIALOG_ALTIUM_ECO::ITEM>& aItems, const std::vector<int>& aEnabled )
            {
                SCH_COMMIT commit( m_frame );

                for( int ii : aEnabled )
                {
                    PART* part = changed[ii];

                    commit.Modify( part->symbol, part->sheet.LastScreen() );
                    part->symbol->SetRef( &part->sheet, part->proposed );

                    if( part->unitCount > 1 )
                    {
                        part->symbol->SetUnitSelection( &part->sheet, part->proposedUnit );
                        part->symbol->SetUnit( part->proposedUnit );
                    }
                }

                // Annotate Quietly / Force Annotate use the nearest KiCad order: columns or
                // rows first
                int order = m_order->GetSelection();
                m_frame->Schematic().Settings().m_AnnotateSortOrder =
                        ( order == UP_THEN_ACROSS || order == DOWN_THEN_ACROSS )
                                ? SORT_BY_X_POSITION : SORT_BY_Y_POSITION;

                m_frame->GetCurrentSheet().UpdateAllScreenReferences();
                commit.Push( _( "Annotate Schematics" ) );
                m_frame->HardRedraw();
            } );

    eco.ShowModal();

    if( eco.WasExecuted() )
    {
        // The changes are now the current designators
        collect();
        fillChangesGrid();
    }
}


void DIALOG_ALTIUM_ANNOTATE::onSheetsClick( wxGridEvent& aEvent )
{
    int row = aEvent.GetRow();
    int col = aEvent.GetCol();

    if( row < 0 || row >= (int) m_sheets.size() )
        return aEvent.Skip();

    if( col == SC_ON )
        m_sheets[row].enabled = !m_sheets[row].enabled;
    else if( col == SC_INDEX_ON )
        m_sheets[row].useStartIndex = !m_sheets[row].useStartIndex;
    else
        return aEvent.Skip();

    fillSheetsGrid();
    updateSummary();
}


void DIALOG_ALTIUM_ANNOTATE::onSheetsChanged( wxGridEvent& aEvent )
{
    if( m_filling )
        return;

    int row = aEvent.GetRow();

    if( row < 0 || row >= (int) m_sheets.size() )
        return;

    SHEET&   sheet = m_sheets[row];
    wxString value = m_sheetsGrid->GetCellValue( row, aEvent.GetCol() );
    long     number = 0;

    switch( aEvent.GetCol() )
    {
    case SC_SCOPE:
        sheet.scope = value == _( "Ignore Selected Parts" ) ? 1
                      : value == _( "Only Selected Parts" ) ? 2 : 0;
        break;

    case SC_ORDER:
        if( value.ToLong( &number ) )
            sheet.order = (int) number;

        break;

    case SC_INDEX:
        if( value.ToLong( &number ) )
        {
            sheet.startIndex = number;
            sheet.useStartIndex = true;
        }

        break;

    case SC_SUFFIX:
        sheet.suffix = value.Trim().Trim( false );
        break;

    default:
        break;
    }

    fillSheetsGrid();
}


void DIALOG_ALTIUM_ANNOTATE::onParamsClick( wxGridEvent& aEvent )
{
    int row = aEvent.GetRow();

    if( row < 0 || row >= (int) m_params.size() )
        return aEvent.Skip();

    PARAM& param = m_params[row];

    if( aEvent.GetCol() == PC_USE )
    {
        param.use = !param.use;

        if( param.use )
            g_last.params.insert( param.name );
        else
            g_last.params.erase( param.name );
    }
    else if( aEvent.GetCol() == PC_STRICT )
    {
        param.strictly = !param.strictly;

        if( param.strictly )
            g_last.notStrict.erase( param.name );
        else
            g_last.notStrict.insert( param.name );
    }
    else
    {
        return aEvent.Skip();
    }

    fillParamsGrid();
    updateSummary();
}


void DIALOG_ALTIUM_ANNOTATE::onChangesClick( wxGridEvent& aEvent )
{
    int row = aEvent.GetRow();

    if( row < 0 || row >= (int) m_parts.size() )
        return aEvent.Skip();

    PART& part = m_parts[row];

    if( aEvent.GetCol() == CC_LOCK )
    {
        part.lockDesignator = !part.lockDesignator;

        // A locked part keeps its designator
        if( part.lockDesignator )
            part.proposed = part.current;
    }
    else if( aEvent.GetCol() == CC_LOCK_SUB && part.unitCount > 1 )
    {
        part.lockSub = !part.lockSub;

        if( part.lockSub )
            part.proposedUnit = part.currentUnit;
    }
    else
    {
        return aEvent.Skip();
    }

    fillChangesGrid();
}
