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

#include <dialogs/dialog_board_level_annotate.h>

#include <dialogs/dialog_altium_eco.h>
#include <algorithm>
#include <project.h>
#include <sch_commit.h>
#include <sch_edit_frame.h>
#include <sch_screen.h>
#include <sch_sheet.h>
#include <sch_symbol.h>
#include <schematic.h>
#include <string_utils.h>
#include <tool/actions.h>
#include <tool/tool_manager.h>
#include <widgets/altium_sub_tabs.h>

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/combobox.h>
#include <wx/dcbuffer.h>
#include <wx/filename.h>
#include <wx/grid.h>
#include <wx/menu.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <map>


namespace
{
// Kept for the session, as Altium keeps them with the project
struct BLA_SETTINGS
{
    wxString scheme = wxS( "$Component_$RoomName" );
    int      roomStyle = 0;
    wxString separator = wxS( "_" );
};

BLA_SETTINGS g_bla;

const wxString SCHEMES[] = { wxS( "$Component_$RoomName" ),
                             wxS( "$Component$ChannelAlpha" ),
                             wxS( "$Component_$ChannelPrefix$ChannelIndex" ),
                             wxS( "$Component$ChannelIndex" ),
                             wxS( "$Component_$ChannelIndex" ),
                             wxS( "$ComponentPrefix$ChannelIndex$ComponentIndex" ),
                             wxS( "$ComponentPrefix_$ChannelIndex_$ComponentIndex" ) };

enum CHANNEL_COLS { CH_SHEET = 0, CH_NAME, CH_ENABLED, CH_SCOPE, CH_COUNT };
enum ROW_COLS { RC_PATH = 0, RC_PREFIX, RC_INDEX, RC_ROOM, RC_APPLY, RC_DESIGNATOR, RC_COUNT };


wxString alpha( int aIndex )
{
    wxString s;

    for( int n = aIndex; n > 0; n = ( n - 1 ) / 26 )
        s.Prepend( wxUniChar( 'A' + ( n - 1 ) % 26 ) );

    return s;
}


/// "R12_CH2" -> "R12" (the designator the scheme started from); "R?" stays.
wxString baseDesignator( const wxString& aRef )
{
    size_t ii = 0;

    while( ii < aRef.length() && !wxIsdigit( aRef[ii] ) && aRef[ii] != '?' )
        ii++;

    while( ii < aRef.length() && ( wxIsdigit( aRef[ii] ) || aRef[ii] == '?' ) )
        ii++;

    return aRef.Left( ii );
}


wxString cellBool( bool aValue )
{
    return aValue ? wxString( wxS( "1" ) ) : wxString();
}


wxStaticText* title( wxWindow* aParent, const wxString& aText )
{
    wxStaticText* text = new wxStaticText( aParent, wxID_ANY, aText );
    text->SetFont( text->GetFont().Bold().Larger() );
    return text;
}


void setupGrid( wxGrid* aGrid, int aCols )
{
    aGrid->CreateGrid( 0, aCols );
    aGrid->SetRowLabelSize( 0 );
    aGrid->SetColLabelSize( aGrid->FromDIP( 22 ) );
    aGrid->SetDefaultRowSize( aGrid->FromDIP( 22 ) );
    aGrid->EnableDragRowSize( false );
    aGrid->SetCellHighlightPenWidth( 0 );
    aGrid->SetSelectionMode( wxGrid::wxGridSelectRows );
    aGrid->SetLabelFont( aGrid->GetFont() );
}


void columnAttr( wxGrid* aGrid, int aCol, bool aBool, const wxColour& aBackground = wxNullColour )
{
    wxGridCellAttr* attr = new wxGridCellAttr;
    attr->SetReadOnly( true );

    if( aBool )
    {
        attr->SetRenderer( new wxGridCellBoolRenderer() );
        attr->SetAlignment( wxALIGN_CENTER, wxALIGN_CENTER );
    }

    if( aBackground.IsOk() )
        attr->SetBackgroundColour( aBackground );

    aGrid->SetColAttr( aCol, attr );
}


/**
 * The example under Room Name Options: two banks of two channels, each with two components
 * named with the current scheme and room naming style.
 */
class ROOMS_PICTURE : public wxWindow
{
public:
    ROOMS_PICTURE( wxWindow* aParent, std::function<wxString( int, const wxString& )> aName ) :
            wxWindow( aParent, wxID_ANY ),
            m_name( std::move( aName ) )
    {
        SetBackgroundStyle( wxBG_STYLE_PAINT );
        SetMinSize( FromDIP( wxSize( 560, 230 ) ) );
        Bind( wxEVT_PAINT, [this]( wxPaintEvent& ) { paint(); } );
    }

private:
    void paint()
    {
        wxAutoBufferedPaintDC dc( this );
        wxSize                sz = GetClientSize();
        wxColour              bg = GetParent()->GetBackgroundColour();

        dc.SetBackground( wxBrush( bg ) );
        dc.Clear();
        dc.SetFont( GetFont() );

        int gap = FromDIP( 10 );
        int bankW = ( sz.x - gap ) / 2;

        for( int bank = 0; bank < 2; ++bank )
        {
            wxRect bankRect( bank * ( bankW + gap ), 0, bankW, sz.y );
            dc.SetPen( wxPen( wxColour( 40, 40, 40 ), FromDIP( 2 ) ) );
            dc.SetBrush( wxBrush( wxColour( 72, 72, 72 ) ) );
            dc.DrawRectangle( bankRect );

            wxString bankName = wxString::Format( wxS( "Bank%d" ), bank + 1 );
            wxSize   ext = dc.GetTextExtent( bankName );
            dc.SetTextForeground( wxColour( 220, 220, 220 ) );
            dc.DrawText( bankName, bankRect.x + ( bankRect.width - ext.x ) / 2, bankRect.y + FromDIP( 6 ) );

            int chTop = bankRect.y + FromDIP( 28 );
            int chH = ( bankRect.height - FromDIP( 36 ) ) / 2;

            for( int ch = 0; ch < 2; ++ch )
            {
                int    channel = bank * 2 + ch + 1;
                wxRect chRect( bankRect.x + FromDIP( 10 ), chTop + ch * chH,
                               bankRect.width - FromDIP( 20 ), chH - FromDIP( 6 ) );
                dc.SetPen( *wxTRANSPARENT_PEN );
                dc.SetBrush( wxBrush( wxColour( 112, 112, 112 ) ) );
                dc.DrawRectangle( chRect );

                wxString chName = wxString::Format( wxS( "Ch%d" ), channel );
                ext = dc.GetTextExtent( chName );
                dc.DrawText( chName, chRect.x + ( chRect.width - ext.x ) / 2, chRect.y + FromDIP( 3 ) );

                int partW = ( chRect.width - FromDIP( 30 ) ) / 2;

                for( int part = 0; part < 2; ++part )
                {
                    wxRect partRect( chRect.x + FromDIP( 10 ) + part * ( partW + FromDIP( 10 ) ),
                                     chRect.y + FromDIP( 22 ), partW,
                                     chRect.height - FromDIP( 30 ) );
                    dc.SetPen( wxPen( wxColour( 40, 40, 40 ) ) );
                    dc.SetBrush( wxBrush( wxColour( 196, 196, 140 ) ) );
                    dc.DrawRectangle( partRect );

                    wxString label = m_name( channel, wxString::Format( wxS( "U%d" ), part + 1 ) );
                    ext = dc.GetTextExtent( label );
                    dc.SetTextForeground( wxColour( 30, 30, 30 ) );
                    dc.DrawText( label, partRect.x + ( partRect.width - ext.x ) / 2,
                                 partRect.y + ( partRect.height - ext.y ) / 2 );
                    dc.SetTextForeground( wxColour( 220, 220, 220 ) );
                }
            }
        }
    }

    std::function<wxString( int, const wxString& )> m_name;
};

} // namespace


DIALOG_BOARD_LEVEL_ANNOTATE::DIALOG_BOARD_LEVEL_ANNOTATE( SCH_EDIT_FRAME* aParent ) :
        DIALOG_SHIM( aParent, wxID_ANY, _( "Board Level Annotate" ), wxDefaultPosition,
                     wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxMAXIMIZE_BOX ),
        m_frame( aParent ),
        m_filling( false )
{
    bool     dark = wxSystemSettings::GetAppearance().IsDark();
    wxColour source = dark ? wxColour( 66, 54, 54 ) : wxColour( 250, 236, 236 );
    wxColour design = dark ? wxColour( 54, 64, 56 ) : wxColour( 236, 248, 238 );

    wxBoxSizer* main = new wxBoxSizer( wxVERTICAL );
    wxBoxSizer* columns = new wxBoxSizer( wxHORIZONTAL );

    // ---- Left: Filter Options ----
    wxBoxSizer* left = new wxBoxSizer( wxVERTICAL );
    left->Add( title( this, _( "Filter Options" ) ), 0, wxBOTTOM, 8 );

    m_channelsGrid = new wxGrid( this, wxID_ANY );
    setupGrid( m_channelsGrid, CH_COUNT );
    m_channelsGrid->SetColLabelValue( CH_SHEET, _( "Schematic Sheet" ) );
    m_channelsGrid->SetColLabelValue( CH_NAME, _( "Channel Name" ) );
    m_channelsGrid->SetColLabelValue( CH_ENABLED, _( "Enabled" ) );
    m_channelsGrid->SetColLabelValue( CH_SCOPE, _( "Annotation Scope" ) );
    m_channelsGrid->SetColSize( CH_SHEET, FromDIP( 160 ) );
    m_channelsGrid->SetColSize( CH_NAME, FromDIP( 110 ) );
    m_channelsGrid->SetColSize( CH_ENABLED, FromDIP( 64 ) );
    m_channelsGrid->SetColSize( CH_SCOPE, FromDIP( 130 ) );
    columnAttr( m_channelsGrid, CH_SHEET, false );
    columnAttr( m_channelsGrid, CH_NAME, false );
    columnAttr( m_channelsGrid, CH_ENABLED, true );

    {
        wxGridCellAttr* attr = new wxGridCellAttr;
        wxString        scopes[] = { _( "All" ), _( "Ignore Selected Parts" ),
                                     _( "Only Selected Parts" ) };
        attr->SetEditor( new wxGridCellChoiceEditor( 3, scopes ) );
        m_channelsGrid->SetColAttr( CH_SCOPE, attr );
    }

    m_channelsGrid->SetMinSize( FromDIP( wxSize( 470, 400 ) ) );
    left->Add( m_channelsGrid, 1, wxEXPAND | wxBOTTOM, 8 );

    wxBoxSizer* allButtons = new wxBoxSizer( wxHORIZONTAL );
    wxButton*   allOn = new wxButton( this, wxID_ANY, _( "All On" ) );
    wxButton*   allOff = new wxButton( this, wxID_ANY, _( "All Off" ) );
    allButtons->Add( allOn, 0, wxRIGHT, 4 );
    allButtons->Add( allOff, 0 );
    left->Add( allButtons, 0 );

    columns->Add( left, 0, wxEXPAND | wxALL, 8 );

    // ---- Right: Proposed Change List ----
    wxBoxSizer* right = new wxBoxSizer( wxVERTICAL );
    right->Add( title( this, _( "Proposed Change List" ) ), 0, wxBOTTOM, 8 );

    m_rowsGrid = new wxGrid( this, wxID_ANY );
    setupGrid( m_rowsGrid, RC_COUNT );
    m_rowsGrid->SetColLabelValue( RC_PATH, _( "Hierarchy Path" ) );
    m_rowsGrid->SetColLabelValue( RC_PREFIX, _( "Prefix" ) );
    m_rowsGrid->SetColLabelValue( RC_INDEX, _( "Local Index" ) );
    m_rowsGrid->SetColLabelValue( RC_ROOM, _( "Room name" ) );
    m_rowsGrid->SetColLabelValue( RC_APPLY, _( "Apply" ) );
    m_rowsGrid->SetColLabelValue( RC_DESIGNATOR, _( "Designator" ) );
    m_rowsGrid->SetColLabelAlignment( wxALIGN_LEFT, wxALIGN_CENTER );

    const int widths[] = { 300, 70, 100, 180, 70, 200 };

    for( int col = 0; col < RC_COUNT; ++col )
        m_rowsGrid->SetColSize( col, FromDIP( widths[col] ) );

    columnAttr( m_rowsGrid, RC_PATH, false, source );
    columnAttr( m_rowsGrid, RC_PREFIX, false, source );
    columnAttr( m_rowsGrid, RC_INDEX, false, source );
    columnAttr( m_rowsGrid, RC_ROOM, false, design );
    columnAttr( m_rowsGrid, RC_APPLY, true );
    columnAttr( m_rowsGrid, RC_DESIGNATOR, false );
    m_rowsGrid->SetToolTip( _( "Schematic Source Component: Hierarchy Path, Prefix, Local Index.  "
                               "Calculated Design Data: Room name.  Naming Scheme: Apply.  "
                               "PCB Component Instance: Designator." ) );
    m_rowsGrid->SetMinSize( FromDIP( wxSize( 760, 400 ) ) );
    right->Add( m_rowsGrid, 1, wxEXPAND | wxBOTTOM, 8 );

    wxBoxSizer* actions = new wxBoxSizer( wxHORIZONTAL );
    wxButton*   options = new wxButton( this, wxID_ANY, _( "Annotate Options" ) );
    wxButton*   annotateBtn = new wxButton( this, wxID_ANY, _( "Annotate" ) );
    wxButton*   annotateMore = new wxButton( this, wxID_ANY, wxS( "▾" ), wxDefaultPosition,
                                             wxDefaultSize, wxBU_EXACTFIT );
    wxButton*   reset = new wxButton( this, wxID_ANY, _( "Reset All" ) );
    wxButton*   back = new wxButton( this, wxID_ANY, _( "Back Annotate" ) );

    annotateBtn->SetToolTip( _( "Annotate the undesignated components (the arrow offers all "
                                "or the selected ones)" ) );
    back->SetToolTip( _( "Take the designators from the PCB (Update Schematic from PCB)" ) );

    actions->Add( options, 0, wxRIGHT, 4 );
    actions->Add( annotateBtn, 0 );
    actions->Add( annotateMore, 0, wxRIGHT, 4 );
    actions->Add( reset, 0, wxRIGHT, 4 );
    actions->Add( back, 0 );
    right->Add( actions, 0 );

    columns->Add( right, 1, wxEXPAND | wxTOP | wxRIGHT | wxBOTTOM, 8 );
    main->Add( columns, 1, wxEXPAND );

    wxBoxSizer* bottom = new wxBoxSizer( wxHORIZONTAL );
    bottom->AddStretchSpacer();
    wxButton* accept = new wxButton( this, wxID_ANY, _( "Accept Changes (Create ECO)" ) );
    bottom->Add( accept, 0, wxRIGHT, 4 );
    bottom->Add( new wxButton( this, wxID_CANCEL, _( "Close" ) ), 0 );
    main->Add( bottom, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8 );

    SetSizer( main );

    // ---- Behaviour ----
    allOn->Bind( wxEVT_BUTTON,
                 [this]( wxCommandEvent& )
                 {
                     for( CHANNEL& channel : m_channels )
                         channel.enabled = true;

                     fillChannels();
                 } );
    allOff->Bind( wxEVT_BUTTON,
                  [this]( wxCommandEvent& )
                  {
                      for( CHANNEL& channel : m_channels )
                          channel.enabled = false;

                      fillChannels();
                  } );
    options->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { showOptions(); } );
    annotateBtn->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { annotate( ANNOTATE_UNDESIGNATED ); } );
    annotateMore->Bind( wxEVT_BUTTON,
                        [this, annotateMore]( wxCommandEvent& )
                        {
                            wxMenu menu;
                            menu.Append( 1 + ANNOTATE_UNDESIGNATED, _( "Annotate Undesignated" ) );
                            menu.Append( 1 + ANNOTATE_ALL_ROWS, _( "Annotate All" ) );
                            menu.Append( 1 + ANNOTATE_SELECTED, _( "Annotate Selected" ) );

                            int picked = annotateMore->GetPopupMenuSelectionFromUser(
                                    menu, wxPoint( 0, annotateMore->GetSize().y ) );

                            if( picked >= 1 && picked <= 3 )
                                annotate( (ANNOTATE_MODE) ( picked - 1 ) );
                        } );
    reset->Bind( wxEVT_BUTTON,
                 [this]( wxCommandEvent& )
                 {
                     for( ROW& row : m_rows )
                         row.proposed = row.current;

                     fillRows();
                 } );
    back->Bind( wxEVT_BUTTON,
                [this]( wxCommandEvent& )
                {
                    SCH_EDIT_FRAME* frame = m_frame;
                    EndModal( wxID_CANCEL );
                    frame->CallAfter( [frame]()
                            { frame->GetToolManager()->RunAction( ACTIONS::updateSchematicFromPcb ); } );
                } );
    accept->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { acceptChanges(); } );

    m_channelsGrid->Bind( wxEVT_GRID_CELL_LEFT_CLICK,
                          [this]( wxGridEvent& aEvent )
                          {
                              int row = aEvent.GetRow();

                              if( aEvent.GetCol() != CH_ENABLED || row < 0
                                  || row >= (int) m_channels.size() )
                              {
                                  aEvent.Skip();
                                  return;
                              }

                              m_channels[row].enabled = !m_channels[row].enabled;
                              fillChannels();
                          } );
    m_channelsGrid->Bind( wxEVT_GRID_CELL_CHANGED,
                          [this]( wxGridEvent& aEvent )
                          {
                              int row = aEvent.GetRow();

                              if( m_filling || aEvent.GetCol() != CH_SCOPE || row < 0
                                  || row >= (int) m_channels.size() )
                              {
                                  return;
                              }

                              wxString value = m_channelsGrid->GetCellValue( row, CH_SCOPE );
                              m_channels[row].scope = value == _( "Ignore Selected Parts" ) ? 1
                                                      : value == _( "Only Selected Parts" ) ? 2
                                                                                            : 0;
                          } );
    m_rowsGrid->Bind( wxEVT_GRID_CELL_LEFT_CLICK,
                      [this]( wxGridEvent& aEvent )
                      {
                          int row = aEvent.GetRow();

                          if( aEvent.GetCol() != RC_APPLY || row < 0 || row >= (int) m_rows.size() )
                          {
                              aEvent.Skip();
                              return;
                          }

                          m_rows[row].apply = !m_rows[row].apply;
                          fillRows();
                      } );

    collect();
    fillChannels();
    fillRows();

    finishDialogSettings();
    SetSize( FromDIP( wxSize( 1340, 760 ) ) );
    Centre();
}


void DIALOG_BOARD_LEVEL_ANNOTATE::collect()
{
    m_channels.clear();
    m_rows.clear();

    SCH_SHEET_LIST          hierarchy = m_frame->Schematic().Hierarchy();
    std::map<wxString, int> uses;
    std::map<wxString, int> firstUse;   // sheet file -> channel of its first use

    for( const SCH_SHEET_PATH& path : hierarchy )
        uses[path.LastScreen()->GetFileName()]++;

    std::map<wxString, int> seen;

    for( const SCH_SHEET_PATH& path : hierarchy )
    {
        CHANNEL  channel;
        wxString fileName = path.LastScreen()->GetFileName();

        channel.path = path;
        channel.file = wxFileName( fileName ).GetFullName();
        channel.name = path.size() <= 1 ? m_frame->Prj().GetProjectName() : path.Last()->GetName();
        channel.index = ++seen[fileName];
        channel.multi = uses[fileName] > 1;

        if( !firstUse.count( fileName ) )
            firstUse[fileName] = (int) m_channels.size();

        m_channels.push_back( channel );
    }

    for( size_t cc = 0; cc < m_channels.size(); ++cc )
    {
        const CHANNEL&  channel = m_channels[cc];
        const SCH_SHEET_PATH& path = channel.path;
        wxString        fileName = path.LastScreen()->GetFileName();
        const SCH_SHEET_PATH& firstPath = m_channels[firstUse[fileName]].path;

        for( SCH_ITEM* item : path.LastScreen()->Items().OfType( SCH_SYMBOL_T ) )
        {
            SCH_SYMBOL* symbol = static_cast<SCH_SYMBOL*>( item );

            if( symbol->IsPower() )
                continue;

            ROW row;
            row.symbol = symbol;
            row.sheet = path;
            row.channel = (int) cc;
            row.current = symbol->GetRef( &path, false );

            if( row.current.StartsWith( wxS( "#" ) ) )
                continue;

            // One row per physical component, as in Altium: the other parts of a multi-part
            // package (same designator in this channel) join the first one's row
            if( !row.current.EndsWith( wxS( "?" ) ) )
            {
                auto same = std::find_if( m_rows.begin(), m_rows.end(),
                                          [&]( const ROW& aOther )
                                          {
                                              return aOther.channel == (int) cc
                                                     && aOther.current == row.current;
                                          } );

                if( same != m_rows.end() )
                {
                    same->units.push_back( symbol );
                    same->selected |= symbol->IsSelected();
                    continue;
                }
            }

            row.units.push_back( symbol );

            // Altium's logical designator: the first use's one, without a channel suffix
            row.base = baseDesignator( channel.multi ? symbol->GetRef( &firstPath, false )
                                                     : row.current );

            size_t digits = 0;

            while( digits < row.base.length() && !wxIsdigit( row.base[digits] )
                   && row.base[digits] != '?' )
            {
                digits++;
            }

            row.prefix = row.base.Left( digits );
            row.localIndex = row.base.Mid( digits );
            row.proposed = row.current;
            row.selected = symbol->IsSelected();

            m_rows.push_back( row );
        }
    }

    std::stable_sort( m_rows.begin(), m_rows.end(),
                      []( const ROW& a, const ROW& b )
                      {
                          if( a.channel != b.channel )
                              return a.channel < b.channel;

                          return StrNumCmp( a.base, b.base, true ) < 0;
                      } );
}


wxString DIALOG_BOARD_LEVEL_ANNOTATE::roomName( const CHANNEL& aChannel ) const
{
    if( !aChannel.multi )
        return wxEmptyString;

    switch( g_bla.roomStyle )
    {
    case 0:     // Flat Numeric With Names
        return aChannel.name.IsEmpty() ? wxString::Format( wxS( "CH%d" ), aChannel.index )
                                       : aChannel.name;
    case 1:     // Flat Alpha With Names
        return aChannel.name.IsEmpty() ? wxS( "CH" ) + alpha( aChannel.index ) : aChannel.name;
    case 2:     // Flat Numeric
        return wxString::Format( wxS( "CH%d" ), aChannel.index );
    case 3:     // Flat Alpha
        return wxS( "CH" ) + alpha( aChannel.index );
    default:    // Name Path
    {
        wxString name;

        for( size_t ii = 1; ii < aChannel.path.size(); ++ii )
        {
            if( !name.IsEmpty() )
                name << g_bla.separator;

            name << aChannel.path.at( ii )->GetName();
        }

        return name;
    }
    }
}


wxString DIALOG_BOARD_LEVEL_ANNOTATE::designatorFor( const ROW& aRow ) const
{
    const CHANNEL& channel = m_channels[aRow.channel];

    // Components of sheets used once keep their designators
    if( !channel.multi || aRow.base.EndsWith( wxS( "?" ) ) )
        return aRow.base;

    wxString name = g_bla.scheme;

    // Longest names first, so $ComponentPrefix isn't read as $Component + "Prefix"
    name.Replace( wxS( "$ComponentPrefix" ), aRow.prefix );
    name.Replace( wxS( "$ComponentIndex" ), aRow.localIndex );
    name.Replace( wxS( "$Component" ), aRow.base );
    name.Replace( wxS( "$RoomName" ), roomName( channel ) );
    name.Replace( wxS( "$ChannelPrefix" ), wxS( "CH" ) );
    name.Replace( wxS( "$ChannelIndex" ), wxString::Format( wxS( "%d" ), channel.index ) );
    name.Replace( wxS( "$ChannelAlpha" ), alpha( channel.index ) );

    return name;
}


bool DIALOG_BOARD_LEVEL_ANNOTATE::inScope( const ROW& aRow ) const
{
    const CHANNEL& channel = m_channels[aRow.channel];

    if( !channel.enabled || !aRow.apply )
        return false;

    if( channel.scope == 1 && aRow.selected )
        return false;

    if( channel.scope == 2 && !aRow.selected )
        return false;

    return true;
}


void DIALOG_BOARD_LEVEL_ANNOTATE::annotate( ANNOTATE_MODE aMode )
{
    std::map<wxString, int> uses;

    for( const ROW& row : m_rows )
        uses[row.proposed]++;

    wxArrayInt selectedRows = m_rowsGrid->GetSelectedRows();

    for( size_t ii = 0; ii < m_rows.size(); ++ii )
    {
        ROW& row = m_rows[ii];

        if( !inScope( row ) )
            continue;

        bool take = false;

        switch( aMode )
        {
        case ANNOTATE_UNDESIGNATED:
            // Without a designator of their own: unannotated, or sharing one with another use
            take = row.proposed.EndsWith( wxS( "?" ) ) || uses[row.proposed] > 1
                   || ( m_channels[row.channel].multi && row.proposed == row.base );
            break;

        case ANNOTATE_ALL_ROWS:
            take = true;
            break;

        case ANNOTATE_SELECTED:
            take = selectedRows.Index( (int) ii ) != wxNOT_FOUND;
            break;
        }

        if( take )
            row.proposed = designatorFor( row );
    }

    fillRows();
}


void DIALOG_BOARD_LEVEL_ANNOTATE::fillChannels()
{
    static const wxString scopes[] = { _( "All" ), _( "Ignore Selected Parts" ),
                                       _( "Only Selected Parts" ) };

    m_filling = true;

    if( m_channelsGrid->GetNumberRows() != (int) m_channels.size() )
    {
        if( m_channelsGrid->GetNumberRows() )
            m_channelsGrid->DeleteRows( 0, m_channelsGrid->GetNumberRows() );

        m_channelsGrid->AppendRows( (int) m_channels.size() );
    }

    for( size_t ii = 0; ii < m_channels.size(); ++ii )
    {
        const CHANNEL& channel = m_channels[ii];

        m_channelsGrid->SetCellValue( ii, CH_SHEET, channel.file );
        m_channelsGrid->SetCellValue( ii, CH_NAME, channel.name );
        m_channelsGrid->SetCellValue( ii, CH_ENABLED, cellBool( channel.enabled ) );
        m_channelsGrid->SetCellValue( ii, CH_SCOPE, scopes[channel.scope] );
    }

    m_filling = false;
}


void DIALOG_BOARD_LEVEL_ANNOTATE::fillRows()
{
    m_filling = true;

    if( m_rowsGrid->GetNumberRows() != (int) m_rows.size() )
    {
        if( m_rowsGrid->GetNumberRows() )
            m_rowsGrid->DeleteRows( 0, m_rowsGrid->GetNumberRows() );

        m_rowsGrid->AppendRows( (int) m_rows.size() );
    }

    wxColour normal = m_rowsGrid->GetDefaultCellTextColour();
    wxColour changed = wxSystemSettings::GetAppearance().IsDark() ? wxColour( 255, 200, 80 )
                                                                  : wxColour( 0, 90, 200 );

    for( size_t ii = 0; ii < m_rows.size(); ++ii )
    {
        const ROW&     row = m_rows[ii];
        const CHANNEL& channel = m_channels[row.channel];
        wxString       path = m_channels[0].name;

        for( size_t pp = 1; pp < channel.path.size(); ++pp )
            path << wxS( "\\" ) << channel.path.at( pp )->GetName();

        m_rowsGrid->SetCellValue( ii, RC_PATH, path );
        m_rowsGrid->SetCellValue( ii, RC_PREFIX, row.prefix );
        m_rowsGrid->SetCellValue( ii, RC_INDEX, row.localIndex );
        m_rowsGrid->SetCellValue( ii, RC_ROOM, roomName( channel ) );
        m_rowsGrid->SetCellValue( ii, RC_APPLY, cellBool( row.apply ) );
        m_rowsGrid->SetCellValue( ii, RC_DESIGNATOR, row.proposed );
        m_rowsGrid->SetCellTextColour( ii, RC_DESIGNATOR,
                                       row.proposed != row.current ? changed : normal );
    }

    m_rowsGrid->ForceRefresh();
    m_filling = false;
}


void DIALOG_BOARD_LEVEL_ANNOTATE::showOptions()
{
    DIALOG_SHIM dlg( this, wxID_ANY, _( "Board Level Annotation Options" ), wxDefaultPosition,
                     wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER );

    wxBoxSizer* main = new wxBoxSizer( wxVERTICAL );

    wxStaticText* nameTitle = new wxStaticText( &dlg, wxID_ANY, _( "Name Scheme" ) );
    nameTitle->SetFont( nameTitle->GetFont().Bold() );
    main->Add( nameTitle, 0, wxALL, 8 );

    wxBoxSizer* schemeRow = new wxBoxSizer( wxHORIZONTAL );
    schemeRow->Add( new wxStaticText( &dlg, wxID_ANY, _( "Naming Scheme" ) ), 0,
                    wxALIGN_CENTER_VERTICAL | wxRIGHT, 8 );

    wxArrayString schemes;

    for( const wxString& scheme : SCHEMES )
        schemes.Add( scheme );

    wxComboBox* scheme = new wxComboBox( &dlg, wxID_ANY, g_bla.scheme, wxDefaultPosition,
                                         wxDefaultSize, schemes );
    schemeRow->Add( scheme, 1, wxALIGN_CENTER_VERTICAL );
    main->Add( schemeRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8 );

    ALTIUM_SUB_TABS* tabs = new ALTIUM_SUB_TABS( &dlg );
    tabs->SetTabs( { _( "Room Name Options" ) }, 0 );
    main->Add( tabs, 0, wxEXPAND | wxLEFT | wxRIGHT, 8 );

    wxFlexGridSizer* roomGrid = new wxFlexGridSizer( 2, 6, 8 );
    roomGrid->AddGrowableCol( 1 );

    roomGrid->Add( new wxStaticText( &dlg, wxID_ANY, _( "Room Naming Style" ) ), 0,
                   wxALIGN_CENTER_VERTICAL );
    wxChoice* style = new wxChoice( &dlg, wxID_ANY );
    style->Append( _( "Flat Numeric With Names" ) );
    style->Append( _( "Flat Alpha With Names" ) );
    style->Append( _( "Flat Numeric" ) );
    style->Append( _( "Flat Alpha" ) );
    style->Append( _( "Name Path" ) );
    style->SetSelection( g_bla.roomStyle );
    roomGrid->Add( style, 1, wxEXPAND );

    roomGrid->Add( new wxStaticText( &dlg, wxID_ANY, _( "Level Separator for Paths" ) ), 0,
                   wxALIGN_CENTER_VERTICAL );
    wxTextCtrl* separator = new wxTextCtrl( &dlg, wxID_ANY, g_bla.separator );
    separator->SetMinSize( FromDIP( wxSize( 160, -1 ) ) );
    roomGrid->Add( separator, 0 );
    main->Add( roomGrid, 0, wxEXPAND | wxALL, 12 );

    // Example of the names the options give
    BLA_SETTINGS preview = g_bla;
    ROOMS_PICTURE* picture = new ROOMS_PICTURE( &dlg,
            [&preview]( int aChannel, const wxString& aComponent ) -> wxString
            {
                wxString room = preview.roomStyle == 3 || preview.roomStyle == 1
                                        ? wxS( "Ch" ) + alpha( aChannel )
                                        : wxString::Format( wxS( "Ch%d" ), aChannel );
                wxString name = preview.scheme;
                wxString prefix = aComponent.Left( 1 );
                wxString index = aComponent.Mid( 1 );

                name.Replace( wxS( "$ComponentPrefix" ), prefix );
                name.Replace( wxS( "$ComponentIndex" ), index );
                name.Replace( wxS( "$Component" ), aComponent );
                name.Replace( wxS( "$RoomName" ), room );
                name.Replace( wxS( "$ChannelPrefix" ), wxS( "CH" ) );
                name.Replace( wxS( "$ChannelIndex" ), wxString::Format( wxS( "%d" ), aChannel ) );
                name.Replace( wxS( "$ChannelAlpha" ), alpha( aChannel ) );
                return name;
            } );
    main->Add( picture, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12 );

    auto refresh =
            [&]()
            {
                preview.scheme = scheme->GetValue();
                preview.roomStyle = style->GetSelection();
                preview.separator = separator->GetValue();
                picture->Refresh();
            };

    scheme->Bind( wxEVT_TEXT, [&]( wxCommandEvent& ) { refresh(); } );
    scheme->Bind( wxEVT_COMBOBOX, [&]( wxCommandEvent& ) { refresh(); } );
    style->Bind( wxEVT_CHOICE, [&]( wxCommandEvent& ) { refresh(); } );

    wxStdDialogButtonSizer* buttons = new wxStdDialogButtonSizer();
    buttons->AddButton( new wxButton( &dlg, wxID_OK ) );
    buttons->AddButton( new wxButton( &dlg, wxID_CANCEL ) );
    buttons->Realize();
    main->Add( buttons, 0, wxEXPAND | wxALL, 8 );

    dlg.SetSizer( main );
    dlg.SetupStandardButtons();
    dlg.Fit();
    dlg.Centre();

    if( dlg.ShowModal() != wxID_OK )
        return;

    g_bla.scheme = scheme->GetValue().Trim().Trim( false );
    g_bla.roomStyle = style->GetSelection();
    g_bla.separator = separator->GetValue();

    if( g_bla.scheme.IsEmpty() )
        g_bla.scheme = SCHEMES[0];

    fillRows();
}


void DIALOG_BOARD_LEVEL_ANNOTATE::acceptChanges()
{
    std::vector<DIALOG_ALTIUM_ECO::ITEM> items;
    std::vector<ROW*>                    changed;

    for( ROW& row : m_rows )
    {
        if( row.proposed == row.current )
            continue;

        DIALOG_ALTIUM_ECO::ITEM item;
        item.group = _( "Annotate Component" );
        item.action = _( "Modify" );
        item.object = row.current + wxS( " -> " ) + row.proposed;
        item.preposition = _( "In" );
        item.document = m_channels[row.channel].file;
        items.push_back( item );
        changed.push_back( &row );
    }

    if( changed.empty() )
        return;

    DIALOG_ALTIUM_ECO eco( this, items,
            [&]( std::vector<DIALOG_ALTIUM_ECO::ITEM>& aItems, const std::vector<int>& aEnabled )
            {
                SCH_COMMIT commit( m_frame );

                for( int ii : aEnabled )
                {
                    ROW* row = changed[ii];

                    // Every part of the package takes the component's new name
                    for( SCH_SYMBOL* unit : row->units )
                    {
                        commit.Modify( unit, row->sheet.LastScreen() );
                        unit->SetRef( &row->sheet, row->proposed );
                    }

                    aItems[ii].message = _( "Update the PCB to rename the footprint" );
                }

                m_frame->GetCurrentSheet().UpdateAllScreenReferences();
                commit.Push( _( "Board Level Annotate" ) );
                m_frame->HardRedraw();
            } );

    eco.ShowModal();

    if( eco.WasExecuted() )
    {
        collect();
        fillChannels();
        fillRows();
    }
}
