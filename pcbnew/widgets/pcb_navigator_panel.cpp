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

#include <widgets/pcb_navigator_panel.h>
#include <widgets/pcb_panel_utils.h>

#include <board.h>
#include <board_design_settings.h>
#include <component_classes/component_class.h>
#include <footprint.h>
#include <netclass.h>
#include <netinfo.h>
#include <pad.h>
#include <pcb_edit_frame.h>
#include <pcb_painter.h>
#include <pcb_track.h>
#include <string_utils.h>
#include <project/net_settings.h>
#include <settings/app_settings.h>
#include <settings/color_settings.h>
#include <tool/actions.h>
#include <tool/tool_manager.h>
#include <view/view.h>
#include <zone.h>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/listctrl.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/wupdlock.h>

#include <algorithm>
#include <climits>
#include <map>
#include <tuple>


namespace
{
// Net code no item has: turns on "dim everything else" without highlighting any net
constexpr int NO_NET = INT_MIN + 7;

enum HIGHLIGHT_MODE
{
    HL_NORMAL = 0,
    HL_MASK,
    HL_DIM
};

wxString sideName( PCB_LAYER_ID aLayer )
{
    return aLayer == B_Cu ? _( "Bottom" ) : _( "Top" );
}


/// Natural order for designators and net names (R2 before R10)
bool naturalLess( const wxString& a, const wxString& b )
{
    size_t i = 0, j = 0;

    while( i < a.length() && j < b.length() )
    {
        if( wxIsdigit( a[i] ) && wxIsdigit( b[j] ) )
        {
            size_t si = i, sj = j;

            while( i < a.length() && wxIsdigit( a[i] ) )
                ++i;

            while( j < b.length() && wxIsdigit( b[j] ) )
                ++j;

            unsigned long na = 0, nb = 0;
            a.Mid( si, i - si ).ToULong( &na );
            b.Mid( sj, j - sj ).ToULong( &nb );

            if( na != nb )
                return na < nb;

            continue;
        }

        wxChar ca = wxTolower( a[i] );
        wxChar cb = wxTolower( b[j] );

        if( ca != cb )
            return ca < cb;

        ++i;
        ++j;
    }

    return a.length() - i < b.length() - j;
}
}


PCB_NAVIGATOR_PANEL::PCB_NAVIGATOR_PANEL( wxWindow* aParent, PCB_EDIT_FRAME* aFrame ) :
        wxPanel( aParent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE ),
        m_frame( aFrame ),
        m_mode( MODE::NETS ),
        m_highlighting( false ),
        m_updating( false ),
        m_selectionPending( false ),
        m_boardSig( 0 )
{
    wxBoxSizer* sizer = new wxBoxSizer( wxVERTICAL );
    int         gap = FromDIP( 4 );

    // Browse mode, as at the top of Altium's PCB panel
    m_modeChoice = new wxChoice( this, wxID_ANY );
    m_modeChoice->Append( _( "Nets" ) );
    m_modeChoice->Append( _( "Components" ) );
    m_modeChoice->Append( _( "Differential Pairs Editor" ) );
    m_modeChoice->Append( _( "Polygons" ) );
    m_modeChoice->Append( _( "Hole Size Editor" ) );
    m_modeChoice->SetSelection( 0 );
    sizer->Add( m_modeChoice, 0, wxEXPAND | wxALL, gap );

    // Highlight options: Normal / Mask / Dim, Select, Zoom, Clear Existing
    wxBoxSizer* options = new wxBoxSizer( wxHORIZONTAL );
    m_highlightChoice = new wxChoice( this, wxID_ANY );
    m_highlightChoice->Append( _( "Normal" ) );
    m_highlightChoice->Append( _( "Mask" ) );
    m_highlightChoice->Append( _( "Dim" ) );
    m_highlightChoice->SetSelection( HL_MASK );
    m_highlightChoice->SetToolTip( _( "Normal: highlight the picked objects\n"
                                      "Mask: fade everything else strongly\n"
                                      "Dim: fade everything else a little" ) );
    options->Add( m_highlightChoice, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap );

    m_selectCheck = new wxCheckBox( this, wxID_ANY, _( "Select" ) );
    m_selectCheck->SetValue( true );
    options->Add( m_selectCheck, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap );

    m_zoomCheck = new wxCheckBox( this, wxID_ANY, _( "Zoom" ) );
    m_zoomCheck->SetValue( true );
    options->Add( m_zoomCheck, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap );

    m_clearExistingCheck = new wxCheckBox( this, wxID_ANY, _( "Clear Existing" ) );
    m_clearExistingCheck->SetValue( true );
    options->Add( m_clearExistingCheck, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap );

    options->AddStretchSpacer();

    wxButton* clearButton = new wxButton( this, wxID_ANY, _( "Clear" ), wxDefaultPosition,
                                          wxDefaultSize, wxBU_EXACTFIT );
    clearButton->SetToolTip( _( "Remove the highlighting (and the selection)" ) );
    options->Add( clearButton, 0, wxALIGN_CENTER_VERTICAL );

    sizer->Add( options, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, gap );

    auto makeList =
            [&]( long aStyle )
            {
                return new wxListView( this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                       wxLC_REPORT | aStyle );
            };

    m_classList = makeList( wxLC_SINGLE_SEL );
    m_objectList = makeList( 0 );
    m_primitiveList = makeList( 0 );

    m_objectCount = new wxStaticText( this, wxID_ANY, wxEmptyString );

    sizer->Add( m_classList, 2, wxEXPAND | wxLEFT | wxRIGHT, gap );
    sizer->Add( m_objectCount, 0, wxLEFT | wxRIGHT | wxTOP, gap );
    sizer->Add( m_objectList, 4, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, gap );
    sizer->Add( m_primitiveList, 3, wxEXPAND | wxALL, gap );
    SetSizer( sizer );

    m_modeChoice->Bind( wxEVT_CHOICE, [this]( wxCommandEvent& ) { onModeChanged(); } );
    clearButton->Bind( wxEVT_BUTTON,
                       [this]( wxCommandEvent& )
                       {
                           clearHighlight();
                           m_frame->GetToolManager()->RunAction( ACTIONS::selectionClear );
                       } );

    // Several selection events arrive for one click; handle the result once
    auto deferred =
            [this]( void ( PCB_NAVIGATOR_PANEL::*aHandler )() )
            {
                return [this, aHandler]( wxListEvent& )
                       {
                           if( m_updating || m_selectionPending )
                               return;

                           m_selectionPending = true;

                           CallAfter( [this, aHandler]()
                                      {
                                          m_selectionPending = false;
                                          ( this->*aHandler )();
                                      } );
                       };
            };

    m_classList->Bind( wxEVT_LIST_ITEM_SELECTED, deferred( &PCB_NAVIGATOR_PANEL::onClassSelected ) );
    m_objectList->Bind( wxEVT_LIST_ITEM_SELECTED, deferred( &PCB_NAVIGATOR_PANEL::onObjectSelected ) );
    m_primitiveList->Bind( wxEVT_LIST_ITEM_SELECTED,
                           deferred( &PCB_NAVIGATOR_PANEL::onPrimitiveSelected ) );

    // Double-click always zooms, like Altium
    auto zoomTo =
            [this]( wxListView* aList, std::vector<ROW>& aRows, bool aObjects )
            {
                std::vector<const ROW*> rows;

                for( size_t idx : selectedIndices( aList ) )
                {
                    size_t row = aObjects ? m_shownObjects[idx] : idx;

                    if( row < aRows.size() )
                        rows.push_back( &aRows[row] );
                }

                bool zoom = m_zoomCheck->GetValue();
                m_zoomCheck->SetValue( true );
                apply( rows );
                m_zoomCheck->SetValue( zoom );
            };

    m_objectList->Bind( wxEVT_LIST_ITEM_ACTIVATED,
                        [this, zoomTo]( wxListEvent& ) { zoomTo( m_objectList, m_objects, true ); } );
    m_primitiveList->Bind( wxEVT_LIST_ITEM_ACTIVATED,
                           [this, zoomTo]( wxListEvent& )
                           {
                               zoomTo( m_primitiveList, m_primitives, false );
                           } );

    Bind( wxEVT_SHOW,
          [this]( wxShowEvent& aEvent )
          {
              aEvent.Skip();

              if( aEvent.IsShown() )
                  CallAfter( [this]() { Rebuild(); } );
          } );

    Bind( wxEVT_IDLE, &PCB_NAVIGATOR_PANEL::onIdle, this );
}


PCB_NAVIGATOR_PANEL::~PCB_NAVIGATOR_PANEL()
{
}


size_t PCB_NAVIGATOR_PANEL::boardSignature() const
{
    BOARD* board = m_frame->GetBoard();

    if( !board )
        return 0;

    size_t sig = std::hash<const void*>()( board );
    sig = sig * 31 + board->Footprints().size();
    sig = sig * 31 + board->Tracks().size();
    sig = sig * 31 + board->Zones().size();
    sig = sig * 31 + board->GetNetCount();
    return sig;
}


void PCB_NAVIGATOR_PANEL::onIdle( wxIdleEvent& aEvent )
{
    aEvent.Skip();

    // Follow board edits while the panel is on screen (cheap: counts only)
    if( !IsShownOnScreen() )
        return;

    if( boardSignature() != m_boardSig )
        Rebuild();
}


void PCB_NAVIGATOR_PANEL::onModeChanged()
{
    clearHighlight();
    m_mode = static_cast<MODE>( std::max( 0, m_modeChoice->GetSelection() ) );
    Rebuild();
}


std::vector<size_t> PCB_NAVIGATOR_PANEL::selectedIndices( wxListView* aList ) const
{
    std::vector<size_t> result;

    for( long ii = aList->GetFirstSelected(); ii >= 0; ii = aList->GetNextSelected( ii ) )
        result.push_back( (size_t) ii );

    return result;
}


void PCB_NAVIGATOR_PANEL::Rebuild()
{
    BOARD* board = m_frame->GetBoard();
    m_boardSig = boardSignature();

    // Remember the picked class and objects by their first column
    wxString pickedClass;

    if( long sel = m_classList->GetFirstSelected(); sel >= 0 && sel < (long) m_classes.size() )
        pickedClass = m_classes[sel].cols[0];

    std::set<wxString> pickedObjects;

    for( size_t idx : selectedIndices( m_objectList ) )
    {
        if( idx < m_shownObjects.size() && m_shownObjects[idx] < m_objects.size() )
            pickedObjects.insert( m_objects[m_shownObjects[idx]].cols[0] );
    }

    m_classes.clear();
    m_objects.clear();
    m_shownObjects.clear();
    m_primitives.clear();

    if( board )
        buildRows();

    m_updating = true;

    std::vector<size_t> allClasses( m_classes.size() );

    for( size_t ii = 0; ii < allClasses.size(); ++ii )
        allClasses[ii] = ii;

    fillList( m_classList, m_classHeaders, m_classes, allClasses );

    long classRow = 0;

    for( size_t ii = 0; ii < m_classes.size(); ++ii )
    {
        if( m_classes[ii].cols[0] == pickedClass )
            classRow = (long) ii;
    }

    if( !m_classes.empty() )
        m_classList->Select( classRow );

    fillObjects();

    for( size_t ii = 0; ii < m_shownObjects.size(); ++ii )
    {
        if( pickedObjects.count( m_objects[m_shownObjects[ii]].cols[0] ) )
            m_objectList->Select( (long) ii );
    }

    std::vector<size_t> noRows;
    fillList( m_primitiveList, m_primitiveHeaders, m_primitives, noRows );

    if( long first = m_objectList->GetFirstSelected(); first >= 0 )
    {
        buildPrimitives( m_objects[m_shownObjects[first]] );

        std::vector<size_t> allPrims( m_primitives.size() );

        for( size_t ii = 0; ii < allPrims.size(); ++ii )
            allPrims[ii] = ii;

        fillList( m_primitiveList, m_primitiveHeaders, m_primitives, allPrims );
    }

    m_updating = false;
}


void PCB_NAVIGATOR_PANEL::fillList( wxListView* aList, const std::vector<wxString>& aHeaders,
                                    const std::vector<ROW>& aRows, const std::vector<size_t>& aShown )
{
    wxWindowUpdateLocker lock( aList );

    bool sameHeaders = aList->GetColumnCount() == (int) aHeaders.size();

    for( int col = 0; sameHeaders && col < (int) aHeaders.size(); ++col )
    {
        wxListItem item;
        item.SetMask( wxLIST_MASK_TEXT );
        aList->GetColumn( col, item );
        sameHeaders = item.GetText() == aHeaders[col];
    }

    if( !sameHeaders )
    {
        aList->ClearAll();

        for( size_t col = 0; col < aHeaders.size(); ++col )
            aList->AppendColumn( aHeaders[col], wxLIST_FORMAT_LEFT, FromDIP( col == 0 ? 130 : 80 ) );
    }
    else
    {
        aList->DeleteAllItems();
    }

    long row = 0;

    for( size_t idx : aShown )
    {
        const ROW& r = aRows[idx];
        aList->InsertItem( row, r.cols.empty() ? wxString() : r.cols[0] );

        for( size_t col = 1; col < r.cols.size() && col < aHeaders.size(); ++col )
            aList->SetItem( row, (long) col, r.cols[col] );

        ++row;
    }
}


void PCB_NAVIGATOR_PANEL::fillObjects()
{
    m_shownObjects.clear();

    long classRow = m_classList->GetFirstSelected();

    for( size_t ii = 0; ii < m_objects.size(); ++ii )
    {
        if( classRow < 0 || m_objects[ii].classes.count( (int) classRow ) )
            m_shownObjects.push_back( ii );
    }

    fillList( m_objectList, m_objectHeaders, m_objects, m_shownObjects );

    m_objectCount->SetLabel( wxString::Format( wxPLURAL( "%zu of %zu object", "%zu of %zu objects",
                                                         m_objects.size() ),
                                               m_shownObjects.size(), m_objects.size() ) );
}


void PCB_NAVIGATOR_PANEL::onClassSelected()
{
    m_updating = true;
    fillObjects();
    m_primitives.clear();
    fillList( m_primitiveList, m_primitiveHeaders, m_primitives, {} );
    m_updating = false;

    // A class highlights all of its members; the catch-all first class doesn't
    long classRow = m_classList->GetFirstSelected();

    if( classRow <= 0 )
    {
        clearHighlight();
        return;
    }

    std::vector<const ROW*> rows;

    for( size_t idx : m_shownObjects )
        rows.push_back( &m_objects[idx] );

    bool zoom = m_zoomCheck->GetValue();
    bool select = m_selectCheck->GetValue();

    // Selecting a whole class of nets (or zooming to it) is rarely wanted; just highlight
    m_zoomCheck->SetValue( false );
    m_selectCheck->SetValue( false );
    apply( rows );
    m_zoomCheck->SetValue( zoom );
    m_selectCheck->SetValue( select );
}


void PCB_NAVIGATOR_PANEL::onObjectSelected()
{
    std::vector<size_t>     picked = selectedIndices( m_objectList );
    std::vector<const ROW*> rows;

    for( size_t idx : picked )
    {
        if( idx < m_shownObjects.size() )
            rows.push_back( &m_objects[m_shownObjects[idx]] );
    }

    m_updating = true;
    m_primitives.clear();

    if( picked.size() == 1 && !rows.empty() )
        buildPrimitives( *rows.front() );

    std::vector<size_t> allPrims( m_primitives.size() );

    for( size_t ii = 0; ii < allPrims.size(); ++ii )
        allPrims[ii] = ii;

    fillList( m_primitiveList, m_primitiveHeaders, m_primitives, allPrims );
    m_updating = false;

    apply( rows );
}


void PCB_NAVIGATOR_PANEL::onPrimitiveSelected()
{
    std::vector<const ROW*> rows;

    for( size_t idx : selectedIndices( m_primitiveList ) )
    {
        if( idx < m_primitives.size() )
            rows.push_back( &m_primitives[idx] );
    }

    apply( rows );
}


void PCB_NAVIGATOR_PANEL::buildRows()
{
    BOARD* board = m_frame->GetBoard();

    switch( m_mode )
    {
    case MODE::NETS:        buildNets( board );       break;
    case MODE::COMPONENTS:  buildComponents( board ); break;
    case MODE::DIFF_PAIRS:  buildDiffPairs( board );  break;
    case MODE::POLYGONS:    buildPolygons( board );   break;
    case MODE::HOLES:       buildHoles( board );      break;
    }
}


namespace
{
struct NET_STATS
{
    int    pads = 0;
    int    vias = 0;
    double length = 0.0;
};

std::map<int, NET_STATS> netStats( BOARD* aBoard )
{
    std::map<int, NET_STATS> stats;

    for( FOOTPRINT* fp : aBoard->Footprints() )
    {
        for( PAD* pad : fp->Pads() )
            stats[pad->GetNetCode()].pads++;
    }

    for( PCB_TRACK* track : aBoard->Tracks() )
    {
        if( track->Type() == PCB_VIA_T )
            stats[track->GetNetCode()].vias++;
        else
            stats[track->GetNetCode()].length += track->GetLength();
    }

    return stats;
}

/// Names of the (atomic) net classes a net belongs to
std::vector<wxString> netClassNames( NETINFO_ITEM* aNet )
{
    std::vector<wxString> names;
    NETCLASS*             nc = aNet->GetNetClass();

    if( !nc )
        return names;

    for( NETCLASS* member : nc->GetConstituentNetclasses() )
        names.push_back( member->GetName() );

    if( names.empty() )
        names.push_back( nc->GetName() );

    return names;
}
}


void PCB_NAVIGATOR_PANEL::buildNets( BOARD* aBoard )
{
    m_classHeaders = { _( "Net Classes" ), _( "Nets" ) };
    m_objectHeaders = { _( "Net" ), _( "Nodes" ), _( "Vias" ), _( "Routed" ) };
    m_primitiveHeaders = { _( "Type" ), _( "Layer" ), _( "Detail" ) };

    std::map<wxString, int> classIdx;
    m_classes.push_back( { { _( "<All Nets>" ) } } );

    std::vector<wxString> classNames;

    if( std::shared_ptr<NET_SETTINGS> ns = aBoard->GetDesignSettings().m_NetSettings )
    {
        classNames.push_back( ns->GetDefaultNetclass()->GetName() );

        for( const auto& [name, nc] : ns->GetNetclasses() )
            classNames.push_back( name );
    }

    for( const wxString& name : classNames )
    {
        if( !classIdx.count( name ) )
        {
            classIdx[name] = (int) m_classes.size();
            m_classes.push_back( { { name } } );
        }
    }

    std::map<int, NET_STATS> stats = netStats( aBoard );
    std::vector<NETINFO_ITEM*> nets;

    for( NETINFO_ITEM* net : aBoard->GetNetInfo() )
    {
        if( net->GetNetCode() > 0 )
            nets.push_back( net );
    }

    std::sort( nets.begin(), nets.end(),
               []( NETINFO_ITEM* a, NETINFO_ITEM* b )
               {
                   return naturalLess( a->GetNetname(), b->GetNetname() );
               } );

    for( NETINFO_ITEM* net : nets )
    {
        const NET_STATS& s = stats[net->GetNetCode()];

        ROW row;
        row.cols = { UnescapeString( net->GetNetname() ), wxString::Format( wxS( "%d" ), s.pads ),
                     wxString::Format( wxS( "%d" ), s.vias ),
                     m_frame->MessageTextFromValue( s.length ) };
        row.nets.insert( net->GetNetCode() );
        row.classes.insert( 0 );

        for( const wxString& name : netClassNames( net ) )
        {
            if( !classIdx.count( name ) )
            {
                classIdx[name] = (int) m_classes.size();
                m_classes.push_back( { { name } } );
            }

            row.classes.insert( classIdx[name] );
        }

        m_objects.push_back( std::move( row ) );
    }

    for( size_t ii = 0; ii < m_classes.size(); ++ii )
    {
        size_t count = std::count_if( m_objects.begin(), m_objects.end(),
                                      [&]( const ROW& r ) { return r.classes.count( (int) ii ); } );
        m_classes[ii].cols.push_back( wxString::Format( wxS( "%zu" ), count ) );
    }
}


void PCB_NAVIGATOR_PANEL::buildComponents( BOARD* aBoard )
{
    m_classHeaders = { _( "Component Classes" ), _( "Components" ) };
    m_objectHeaders = { _( "Designator" ), _( "Footprint" ), _( "Layer" ), _( "Value" ) };
    m_primitiveHeaders = { _( "Pad" ), _( "Net" ), _( "Layer" ) };

    // Altium's built-in classes, then the board's component classes
    m_classes.push_back( { { _( "<All Components>" ) } } );
    m_classes.push_back( { { _( "<Top Side Components>" ) } } );
    m_classes.push_back( { { _( "<Bottom Side Components>" ) } } );

    std::map<wxString, int>  classIdx;
    std::vector<FOOTPRINT*>  footprints( aBoard->Footprints().begin(), aBoard->Footprints().end() );

    std::sort( footprints.begin(), footprints.end(),
               []( FOOTPRINT* a, FOOTPRINT* b )
               {
                   return naturalLess( a->GetReference(), b->GetReference() );
               } );

    for( FOOTPRINT* fp : footprints )
    {
        ROW row;
        row.cols = { fp->GetReference(), fp->GetFPID().GetLibItemName().wx_str(),
                     sideName( fp->GetLayer() ), fp->GetValue() };
        row.items.push_back( fp->m_Uuid );
        row.classes.insert( 0 );
        row.classes.insert( fp->GetLayer() == B_Cu ? 2 : 1 );

        std::vector<wxString> names;

        if( const COMPONENT_CLASS* cc = fp->GetComponentClass() )
        {
            for( const COMPONENT_CLASS* member : cc->GetConstituentClasses() )
                names.push_back( member->GetName() );

            if( names.empty() && !cc->IsEmpty() && !cc->GetName().IsEmpty() )
                names.push_back( cc->GetName() );
        }

        for( const wxString& name : names )
        {
            if( !classIdx.count( name ) )
            {
                classIdx[name] = (int) m_classes.size();
                m_classes.push_back( { { name } } );
            }

            row.classes.insert( classIdx[name] );
        }

        m_objects.push_back( std::move( row ) );
    }

    for( size_t ii = 0; ii < m_classes.size(); ++ii )
    {
        size_t count = std::count_if( m_objects.begin(), m_objects.end(),
                                      [&]( const ROW& r ) { return r.classes.count( (int) ii ); } );
        m_classes[ii].cols.push_back( wxString::Format( wxS( "%zu" ), count ) );
    }
}


void PCB_NAVIGATOR_PANEL::buildDiffPairs( BOARD* aBoard )
{
    m_classHeaders = { _( "Differential Pair Classes" ), _( "Pairs" ) };
    m_objectHeaders = { _( "Differential Pair" ), _( "Positive" ), _( "Negative" ) };
    m_primitiveHeaders = { _( "Net" ), _( "Polarity" ), _( "Routed" ) };

    m_classes.push_back( { { _( "<All Differential Pairs>" ) } } );

    std::map<wxString, int>  classIdx;
    std::vector<ROW>         pairs;

    for( NETINFO_ITEM* net : aBoard->GetNetInfo() )
    {
        if( net->GetNetCode() <= 0 )
            continue;

        wxString complement;

        // One row per pair, found from its positive net
        if( aBoard->MatchDpSuffix( net->GetNetname(), complement ) != 1 )
            continue;

        NETINFO_ITEM* negative = aBoard->FindNet( complement );

        if( !negative )
            continue;

        // The pair's name: the common part of both net names
        wxString pos = UnescapeString( net->GetNetname() );
        wxString neg = UnescapeString( negative->GetNetname() );
        size_t   common = 0;

        while( common < pos.length() && common < neg.length() && pos[common] == neg[common] )
            ++common;

        wxString name = pos.Left( common );

        while( !name.IsEmpty() && ( name.Last() == '_' || name.Last() == '-' ) )
            name.RemoveLast();

        if( name.IsEmpty() )
            name = pos;

        ROW row;
        row.cols = { name, pos, neg };
        row.nets = { net->GetNetCode(), negative->GetNetCode() };
        row.classes.insert( 0 );

        for( const wxString& className : netClassNames( net ) )
        {
            if( !classIdx.count( className ) )
            {
                classIdx[className] = (int) m_classes.size();
                m_classes.push_back( { { className } } );
            }

            row.classes.insert( classIdx[className] );
        }

        pairs.push_back( std::move( row ) );
    }

    std::sort( pairs.begin(), pairs.end(),
               []( const ROW& a, const ROW& b ) { return naturalLess( a.cols[0], b.cols[0] ); } );

    m_objects = std::move( pairs );

    for( size_t ii = 0; ii < m_classes.size(); ++ii )
    {
        size_t count = std::count_if( m_objects.begin(), m_objects.end(),
                                      [&]( const ROW& r ) { return r.classes.count( (int) ii ); } );
        m_classes[ii].cols.push_back( wxString::Format( wxS( "%zu" ), count ) );
    }
}


void PCB_NAVIGATOR_PANEL::buildPolygons( BOARD* aBoard )
{
    m_classHeaders = { _( "Polygon Classes" ), _( "Polygons" ) };
    m_objectHeaders = { _( "Name" ), _( "Net" ), _( "Layer" ), _( "Area" ), _( "Priority" ) };
    m_primitiveHeaders = { _( "Layer" ), _( "State" ) };

    m_classes.push_back( { { _( "<All Polygons>" ) } } );

    std::map<PCB_LAYER_ID, int> layerIdx;

    for( PCB_LAYER_ID layer : aBoard->GetEnabledLayers().CuStack() )
    {
        for( ZONE* zone : aBoard->Zones() )
        {
            if( !zone->GetIsRuleArea() && !zone->IsTeardropArea() && zone->IsOnLayer( layer ) )
            {
                layerIdx[layer] = (int) m_classes.size();
                m_classes.push_back( { { aBoard->GetLayerName( layer ) } } );
                break;
            }
        }
    }

    int unnamed = 0;

    for( ZONE* zone : aBoard->Zones() )
    {
        if( zone->GetIsRuleArea() || zone->IsTeardropArea() )
            continue;

        wxString name = zone->GetZoneName();

        if( name.IsEmpty() )
            name = wxString::Format( _( "Polygon %d" ), ++unnamed );

        LSEQ     layers = zone->GetLayerSet().CuStack();
        wxString layerText = layers.size() == 1 ? aBoard->GetLayerName( layers.front() )
                                                : wxString::Format( wxPLURAL( "%zu layer", "%zu layers",
                                                                             layers.size() ),
                                                                   layers.size() );

        ROW row;
        row.cols = { name, UnescapeString( zone->GetNetname() ), layerText,
                     m_frame->MessageTextFromValue( zone->CalculateOutlineArea(), true,
                                                    EDA_DATA_TYPE::AREA ),
                     wxString::Format( wxS( "%u" ), zone->GetAssignedPriority() ) };
        row.items.push_back( zone->m_Uuid );
        row.classes.insert( 0 );

        for( PCB_LAYER_ID layer : layers )
        {
            if( layerIdx.count( layer ) )
                row.classes.insert( layerIdx[layer] );
        }

        m_objects.push_back( std::move( row ) );
    }

    for( size_t ii = 0; ii < m_classes.size(); ++ii )
    {
        size_t count = std::count_if( m_objects.begin(), m_objects.end(),
                                      [&]( const ROW& r ) { return r.classes.count( (int) ii ); } );
        m_classes[ii].cols.push_back( wxString::Format( wxS( "%zu" ), count ) );
    }
}


void PCB_NAVIGATOR_PANEL::buildHoles( BOARD* aBoard )
{
    m_classHeaders = { _( "Hole Size" ), _( "Count" ) };
    m_objectHeaders = { _( "Object" ), _( "Net" ), _( "Hole Size" ), _( "Plated" ) };
    m_primitiveHeaders = { _( "Object" ), _( "Detail" ) };

    m_classes.push_back( { { _( "<All Holes>" ) } } );

    // Classes are the distinct hole sizes, smallest first
    struct HOLE_KEY
    {
        int  x;
        int  y;
        bool slot;
        bool plated;

        bool operator<( const HOLE_KEY& o ) const
        {
            return std::tie( x, y, slot, plated ) < std::tie( o.x, o.y, o.slot, o.plated );
        }
    };

    std::map<HOLE_KEY, std::vector<ROW>> groups;

    auto sizeText =
            [&]( const HOLE_KEY& k )
            {
                wxString text = m_frame->MessageTextFromValue( k.x );

                if( k.slot )
                    text << wxS( " x " ) << m_frame->MessageTextFromValue( k.y ) << wxS( " " )
                         << _( "Slot" );

                return text;
            };

    for( FOOTPRINT* fp : aBoard->Footprints() )
    {
        for( PAD* pad : fp->Pads() )
        {
            if( !pad->HasHole() )
                continue;

            HOLE_KEY key{ pad->GetDrillSize().x, pad->GetDrillSize().y,
                          pad->GetDrillShape() == PAD_DRILL_SHAPE::OBLONG
                                  && pad->GetDrillSize().x != pad->GetDrillSize().y,
                          pad->GetAttribute() != PAD_ATTRIB::NPTH };

            ROW row;
            row.cols = { wxString::Format( _( "Pad %s-%s" ), fp->GetReference(), pad->GetNumber() ),
                         UnescapeString( pad->GetNetname() ), sizeText( key ), key.plated ? _( "Yes" ) : _( "No" ) };
            row.items.push_back( pad->m_Uuid );
            groups[key].push_back( std::move( row ) );
        }
    }

    for( PCB_TRACK* track : aBoard->Tracks() )
    {
        if( track->Type() != PCB_VIA_T )
            continue;

        PCB_VIA* via = static_cast<PCB_VIA*>( track );
        HOLE_KEY key{ via->GetDrillValue(), via->GetDrillValue(), false, true };

        ROW row;
        row.cols = { wxString::Format( _( "Via %s - %s" ), aBoard->GetLayerName( via->TopLayer() ),
                                       aBoard->GetLayerName( via->BottomLayer() ) ),
                     UnescapeString( via->GetNetname() ), sizeText( key ), _( "Yes" ) };
        row.items.push_back( via->m_Uuid );
        groups[key].push_back( std::move( row ) );
    }

    size_t total = 0;

    for( auto& [key, rows] : groups )
    {
        int classIdx = (int) m_classes.size();
        m_classes.push_back( { { sizeText( key ) + wxS( " " )
                                         + ( key.plated ? _( "Plated" ) : _( "Non-plated" ) ),
                                 wxString::Format( wxS( "%zu" ), rows.size() ) } } );

        for( ROW& row : rows )
        {
            row.classes = { 0, classIdx };
            m_objects.push_back( std::move( row ) );
            ++total;
        }
    }

    m_classes[0].cols.push_back( wxString::Format( wxS( "%zu" ), total ) );
}


void PCB_NAVIGATOR_PANEL::buildPrimitives( const ROW& aObject )
{
    m_primitives.clear();

    BOARD* board = m_frame->GetBoard();

    if( !board )
        return;

    switch( m_mode )
    {
    case MODE::NETS:
    {
        std::set<int> nets = aObject.nets;

        for( FOOTPRINT* fp : board->Footprints() )
        {
            for( PAD* pad : fp->Pads() )
            {
                if( !nets.count( pad->GetNetCode() ) )
                    continue;

                ROW row;
                row.cols = { _( "Pad" ),
                             pad->GetAttribute() == PAD_ATTRIB::PTH ? _( "Multi-Layer" )
                                                                    : board->GetLayerName( pad->GetPrincipalLayer() ),
                             fp->GetReference() + wxS( "-" ) + pad->GetNumber() };
                row.items.push_back( pad->m_Uuid );
                m_primitives.push_back( std::move( row ) );
            }
        }

        for( PCB_TRACK* track : board->Tracks() )
        {
            if( !nets.count( track->GetNetCode() ) )
                continue;

            ROW row;

            if( track->Type() == PCB_VIA_T )
            {
                PCB_VIA* via = static_cast<PCB_VIA*>( track );
                row.cols = { _( "Via" ),
                             board->GetLayerName( via->TopLayer() ) + wxS( " - " )
                                     + board->GetLayerName( via->BottomLayer() ),
                             m_frame->MessageTextFromValue( via->GetWidth( via->TopLayer() ) )
                                     + wxS( " / " )
                                     + m_frame->MessageTextFromValue( via->GetDrillValue() ) };
            }
            else
            {
                row.cols = { track->Type() == PCB_ARC_T ? _( "Arc" ) : _( "Track" ),
                             board->GetLayerName( track->GetLayer() ),
                             m_frame->MessageTextFromValue( track->GetLength() ) };
            }

            row.items.push_back( track->m_Uuid );
            m_primitives.push_back( std::move( row ) );
        }

        for( ZONE* zone : board->Zones() )
        {
            if( zone->GetIsRuleArea() || !nets.count( zone->GetNetCode() ) )
                continue;

            ROW row;
            row.cols = { _( "Polygon" ), board->GetLayerName( zone->GetFirstLayer() ),
                         zone->GetZoneName() };
            row.items.push_back( zone->m_Uuid );
            m_primitives.push_back( std::move( row ) );
        }

        break;
    }

    case MODE::COMPONENTS:
    {
        if( aObject.items.empty() )
            break;

        FOOTPRINT* fp = dynamic_cast<FOOTPRINT*>( board->ResolveItem( aObject.items[0], true ) );

        if( !fp )
            break;

        std::vector<PAD*> pads( fp->Pads().begin(), fp->Pads().end() );

        std::sort( pads.begin(), pads.end(),
                   []( PAD* a, PAD* b ) { return naturalLess( a->GetNumber(), b->GetNumber() ); } );

        for( PAD* pad : pads )
        {
            ROW row;
            row.cols = { fp->GetReference() + wxS( "-" ) + pad->GetNumber(), UnescapeString( pad->GetNetname() ),
                         pad->GetAttribute() == PAD_ATTRIB::PTH ? _( "Multi-Layer" )
                                                                : board->GetLayerName( pad->GetPrincipalLayer() ) };
            row.items.push_back( pad->m_Uuid );

            if( pad->GetNetCode() > 0 )
                row.nets.insert( pad->GetNetCode() );

            m_primitives.push_back( std::move( row ) );
        }

        break;
    }

    case MODE::DIFF_PAIRS:
    {
        std::map<int, NET_STATS> stats = netStats( board );
        bool                     positive = true;

        for( int code : { *aObject.nets.begin(), *aObject.nets.rbegin() } )
        {
            NETINFO_ITEM* net = board->FindNet( code );

            if( !net )
                continue;

            wxString complement;
            positive = board->MatchDpSuffix( net->GetNetname(), complement ) == 1;

            ROW row;
            row.cols = { UnescapeString( net->GetNetname() ), positive ? _( "Positive" ) : _( "Negative" ),
                         m_frame->MessageTextFromValue( stats[code].length ) };
            row.nets.insert( code );
            m_primitives.push_back( std::move( row ) );
        }

        // Positive first
        std::sort( m_primitives.begin(), m_primitives.end(),
                   []( const ROW& a, const ROW& b ) { return a.cols[1] > b.cols[1]; } );
        break;
    }

    case MODE::POLYGONS:
    {
        if( aObject.items.empty() )
            break;

        ZONE* zone = dynamic_cast<ZONE*>( board->ResolveItem( aObject.items[0], true ) );

        if( !zone )
            break;

        for( PCB_LAYER_ID layer : zone->GetLayerSet().CuStack() )
        {
            ROW row;
            row.cols = { board->GetLayerName( layer ),
                         zone->HasFilledPolysForLayer( layer )
                                 && !zone->GetFilledPolysList( layer )->IsEmpty()
                                 ? _( "Poured" ) : _( "Not poured" ) };
            row.items.push_back( zone->m_Uuid );
            m_primitives.push_back( std::move( row ) );
        }

        break;
    }

    case MODE::HOLES:
    {
        if( aObject.items.empty() )
            break;

        BOARD_ITEM* item = board->ResolveItem( aObject.items[0], true );

        if( PAD* pad = dynamic_cast<PAD*>( item ) )
        {
            if( FOOTPRINT* fp = pad->GetParentFootprint() )
            {
                ROW row;
                row.cols = { _( "Component" ), fp->GetReference() };
                row.items.push_back( fp->m_Uuid );
                m_primitives.push_back( std::move( row ) );
            }
        }

        break;
    }
    }
}


void PCB_NAVIGATOR_PANEL::clearHighlight()
{
    BOARD* board = m_frame->GetBoard();
    KIGFX::VIEW* view = m_frame->GetCanvas()->GetView();
    bool   changed = false;

    if( board )
    {
        for( const KIID& id : m_brightened )
        {
            if( BOARD_ITEM* item = board->ResolveItem( id, true ) )
            {
                item->ClearBrightened();

                if( item->Type() == PCB_FOOTPRINT_T )
                {
                    static_cast<FOOTPRINT*>( item )->RunOnChildren(
                            [&]( BOARD_ITEM* child )
                            {
                                child->ClearBrightened();
                            },
                            RECURSE_MODE::RECURSE );
                }

                view->Update( item );
                changed = true;
            }
        }
    }

    m_brightened.clear();

    if( m_highlighting )
    {
        KIGFX::RENDER_SETTINGS* rs = view->GetPainter()->GetSettings();
        rs->SetHighlight( false );

        if( APP_SETTINGS_BASE* cfg = m_frame->config() )
            rs->SetHighlightFactor( cfg->m_Graphics.highlight_factor );

        rs->LoadColors( m_frame->GetColorSettings() );
        view->UpdateAllLayersColor();
        m_highlighting = false;
        changed = true;
    }

    if( changed )
        m_frame->GetCanvas()->Refresh();
}


void PCB_NAVIGATOR_PANEL::apply( const std::vector<const ROW*>& aRows )
{
    BOARD* board = m_frame->GetBoard();

    if( !board )
        return;

    if( m_clearExistingCheck->GetValue() )
        clearHighlight();

    if( aRows.empty() )
    {
        if( m_clearExistingCheck->GetValue() && m_selectCheck->GetValue() )
            m_frame->GetToolManager()->RunAction( ACTIONS::selectionClear );

        return;
    }

    // Gather the board items: the rows' own items, or everything on the rows' nets
    std::set<int>            nets;
    std::vector<BOARD_ITEM*> items;
    bool                     byNet = false;

    for( const ROW* row : aRows )
    {
        if( row->items.empty() )
            nets.insert( row->nets.begin(), row->nets.end() );

        for( const KIID& id : row->items )
        {
            if( BOARD_ITEM* item = board->ResolveItem( id, true ) )
                items.push_back( item );
        }
    }

    if( !nets.empty() )
    {
        byNet = true;

        for( FOOTPRINT* fp : board->Footprints() )
        {
            for( PAD* pad : fp->Pads() )
            {
                if( nets.count( pad->GetNetCode() ) )
                    items.push_back( pad );
            }
        }

        for( PCB_TRACK* track : board->Tracks() )
        {
            if( nets.count( track->GetNetCode() ) )
                items.push_back( track );
        }

        for( ZONE* zone : board->Zones() )
        {
            if( !zone->GetIsRuleArea() && nets.count( zone->GetNetCode() ) )
                items.push_back( zone );
        }
    }

    KIGFX::VIEW*            view = m_frame->GetCanvas()->GetView();
    KIGFX::RENDER_SETTINGS* rs = view->GetPainter()->GetSettings();
    int                     mode = m_highlightChoice->GetSelection();

    if( mode == HL_MASK || mode == HL_DIM )
    {
        // Mask fades the rest of the board much further than Dim
        rs->SetHighlightFactor( mode == HL_MASK ? 0.12f : 0.45f );
        rs->LoadColors( m_frame->GetColorSettings() );

        std::set<int> highlight = byNet ? nets : std::set<int>{ NO_NET };

        if( !m_clearExistingCheck->GetValue() && m_highlighting )
        {
            for( int code : rs->GetHighlightNetCodes() )
                highlight.insert( code );

            if( highlight.size() > 1 )
                highlight.erase( NO_NET );
        }

        rs->SetHighlight( highlight, true );
        view->UpdateAllLayersColor();
        m_highlighting = true;
    }

    // Objects that aren't nets stand out by brightening (it also overrides the fading)
    if( !byNet || mode == HL_NORMAL )
    {
        for( BOARD_ITEM* item : items )
        {
            item->SetBrightened();
            m_brightened.insert( item->m_Uuid );

            if( item->Type() == PCB_FOOTPRINT_T )
            {
                static_cast<FOOTPRINT*>( item )->RunOnChildren(
                        []( BOARD_ITEM* child )
                        {
                            child->SetBrightened();
                        },
                        RECURSE_MODE::RECURSE );
            }

            view->Update( item );
        }
    }

    TOOL_MANAGER* toolMgr = m_frame->GetToolManager();

    if( m_selectCheck->GetValue() )
    {
        if( m_clearExistingCheck->GetValue() )
            toolMgr->RunAction( ACTIONS::selectionClear );

        EDA_ITEMS selection( items.begin(), items.end() );

        if( !selection.empty() )
            toolMgr->RunAction<EDA_ITEMS*>( ACTIONS::selectItems, &selection );
    }

    if( m_zoomCheck->GetValue() && !items.empty() )
    {
        BOX2I bbox = items.front()->GetBoundingBox();

        for( BOARD_ITEM* item : items )
            bbox.Merge( item->GetBoundingBox() );

        PcbZoomToBox( m_frame, bbox );
    }

    m_frame->GetCanvas()->Refresh();
}
