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

#include <widgets/sch_navigator_lists.h>

#include <base_units.h>
#include <bitmaps.h>
#include <connection_graph.h>
#include <sch_edit_frame.h>
#include <sch_label.h>
#include <sch_pin.h>
#include <sch_sheet.h>
#include <sch_sheet_pin.h>
#include <sch_symbol.h>
#include <schematic.h>
#include <string_utils.h>
#include <tool/actions.h>
#include <tool/tool_manager.h>
#include <tools/sch_actions.h>
#include <view/view.h>

#include <wx/imaglist.h>
#include <wx/menu.h>
#include <wx/sizer.h>
#include <wx/settings.h>
#include <wx/splitter.h>
#include <wx/wupdlock.h>

#include <map>


namespace
{
enum ICON
{
    ICON_COMPONENT = 0,
    ICON_NET,
    ICON_FOLDER,
    ICON_PIN,
    ICON_PORT,
    ICON_LABEL,
    ICON_SHEET_ENTRY,
    ICON_POWER,
    ICON_PARAMETER
};


enum MENU_ID
{
    ID_VALIDATE = wxID_HIGHEST + 3100,
    ID_REFRESH,
    ID_JUMP,
    ID_SHOW_GRAPH,
    ID_ZOOM,
    ID_MASK,
    ID_SELECT,
    ID_REPORT,
    ID_PORT_XREF,
    ID_SHOW
};


wxString pinText( const SCH_PIN* aPin, const SCH_SHEET_PATH& aSheet )
{
    if( const SCH_SYMBOL* symbol = dynamic_cast<const SCH_SYMBOL*>( aPin->GetParentSymbol() ) )
        return symbol->GetRef( &aSheet, false ) + wxS( "-" ) + UnescapeString( aPin->GetNumber() );

    return UnescapeString( aPin->GetNumber() );
}
}


SCH_NAVIGATOR_LISTS::OPTIONS& SCH_NAVIGATOR_LISTS::Options()
{
    static OPTIONS options;
    return options;
}


SCH_NAVIGATOR_LISTS::SCH_NAVIGATOR_LISTS( wxWindow* aParent, SCH_EDIT_FRAME* aFrame ) :
        wxPanel( aParent, wxID_ANY ),
        m_frame( aFrame ),
        m_dirty( true ),
        m_building( false ),
        m_placed( false )
{
    wxBoxSizer* sizer = new wxBoxSizer( wxVERTICAL );

    // Three lists one above the other, with sashes between them as in Altium
    m_split1 = new wxSplitterWindow( this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                     wxSP_LIVE_UPDATE | wxSP_3DSASH | wxSP_NO_XP_THEME );
    m_split1->SetMinimumPaneSize( FromDIP( 40 ) );
    m_split1->SetSashGravity( 1.0 / 3.0 );

    m_components = makeList( m_split1, { _( "Instance" ), _( "Comment" ), _( "Type" ) } );

    m_split2 = new wxSplitterWindow( m_split1, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                     wxSP_LIVE_UPDATE | wxSP_3DSASH | wxSP_NO_XP_THEME );
    m_split2->SetMinimumPaneSize( FromDIP( 40 ) );
    m_split2->SetSashGravity( 0.5 );

    m_nets = makeList( m_split2, { _( "Net / Bus" ), _( "Scope" ) } );
    m_pins = makeList( m_split2, { _( "Net Pins" ), _( "Electrical Type" ), _( "Type" ) } );

    m_split2->SplitHorizontally( m_nets, m_pins );
    m_split1->SplitHorizontally( m_components, m_split2 );

    sizer->Add( m_split1, 1, wxEXPAND );
    SetSizer( sizer );

    // Even thirds once the panel has a height
    Bind( wxEVT_SIZE,
          [this]( wxSizeEvent& aEvent )
          {
              aEvent.Skip();

              int h = GetClientSize().y;

              if( !m_placed && h > FromDIP( 150 ) )
              {
                  m_placed = true;
                  CallAfter(
                          [this, h]()
                          {
                              m_split1->SetSashPosition( h / 3 );
                              m_split2->SetSashPosition( h / 3 );
                          } );
              }
          } );

    for( wxTreeListCtrl* list : { m_components, m_nets, m_pins } )
    {
        list->Bind( wxEVT_TREELIST_SELECTION_CHANGED,
                    [this, list]( wxTreeListEvent& aEvent )
                    {
                        if( !m_building )
                            onPicked( list, aEvent.GetItem() );
                    } );

        list->Bind( wxEVT_TREELIST_ITEM_CONTEXT_MENU,
                    [this, list]( wxTreeListEvent& aEvent )
                    {
                        onContextMenu( list, aEvent );
                    } );
    }

    Bind( wxEVT_IDLE, &SCH_NAVIGATOR_LISTS::onIdle, this );
}


SCH_NAVIGATOR_LISTS::~SCH_NAVIGATOR_LISTS()
{
    Unbind( wxEVT_IDLE, &SCH_NAVIGATOR_LISTS::onIdle, this );
}


wxTreeListCtrl* SCH_NAVIGATOR_LISTS::makeList( wxWindow* aParent,
                                               const std::vector<wxString>& aColumns )
{
    wxTreeListCtrl* list = new wxTreeListCtrl( aParent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                               wxTL_SINGLE | wxBORDER_NONE );

    wxImageList* images = new wxImageList( FromDIP( 16 ), FromDIP( 16 ), true, 9 );

    for( BITMAPS icon : { BITMAPS::add_component, BITMAPS::net_highlight_schematic,
                          BITMAPS::small_folder, BITMAPS::pin, BITMAPS::add_hierarchical_label,
                          BITMAPS::add_label, BITMAPS::add_hierar_pin, BITMAPS::add_power,
                          BITMAPS::edit_comp_value } )
    {
        images->Add( KiBitmapBundle( icon ).GetBitmap( FromDIP( wxSize( 16, 16 ) ) ) );
    }

    list->AssignImageList( images );

    // Columns share the list's width (the first one wider), so none is cut off at the right
    list->Bind( wxEVT_SIZE,
                [list]( wxSizeEvent& aEvent )
                {
                    aEvent.Skip();

                    unsigned cols = list->GetColumnCount();
                    int      width = list->GetClientSize().x
                                     - wxSystemSettings::GetMetric( wxSYS_VSCROLL_X, list )
                                     - list->FromDIP( 6 );

                    if( cols < 2 || width < 50 )
                        return;

                    int first = width * ( cols == 2 ? 50 : 40 ) / 100;
                    int rest = ( width - first ) / int( cols - 1 );

                    list->SetColumnWidth( 0, first );

                    for( unsigned ii = 1; ii < cols; ++ii )
                        list->SetColumnWidth( ii, rest );
                } );

    for( size_t ii = 0; ii < aColumns.size(); ++ii )
    {
        list->AppendColumn( aColumns[ii], ii == 0 ? FromDIP( 160 ) : FromDIP( 110 ),
                            wxALIGN_LEFT, wxCOL_RESIZABLE );
    }

    return list;
}


wxString SCH_NAVIGATOR_LISTS::rowKey( const ROW* aRow )
{
    if( !aRow )
        return wxEmptyString;

    switch( aRow->kind )
    {
    case ROW::COMPONENT: return wxS( "C:" ) + aRow->ref;
    case ROW::NET:       return wxS( "N:" ) + aRow->net;
    default:             return wxS( "I:" ) + aRow->id.AsString();
    }
}


std::set<wxString> SCH_NAVIGATOR_LISTS::expandedKeys( wxTreeListCtrl* aList ) const
{
    std::set<wxString> keys;

    for( wxTreeListItem item = aList->GetFirstChild( aList->GetRootItem() ); item.IsOk();
         item = aList->GetNextSibling( item ) )
    {
        if( aList->IsExpanded( item ) )
            keys.insert( rowKey( static_cast<ROW*>( aList->GetItemData( item ) ) ) );
    }

    return keys;
}


void SCH_NAVIGATOR_LISTS::restoreExpanded( wxTreeListCtrl* aList, const std::set<wxString>& aKeys )
{
    if( aKeys.empty() )
        return;

    for( wxTreeListItem item = aList->GetFirstChild( aList->GetRootItem() ); item.IsOk();
         item = aList->GetNextSibling( item ) )
    {
        if( aKeys.count( rowKey( static_cast<ROW*>( aList->GetItemData( item ) ) ) ) )
            aList->Expand( item );
    }
}


void SCH_NAVIGATOR_LISTS::onIdle( wxIdleEvent& aEvent )
{
    aEvent.Skip();

    if( m_dirty && IsShownOnScreen() && m_frame->Schematic().HasHierarchy() )
        rebuild();
}


void SCH_NAVIGATOR_LISTS::rebuild()
{
    m_dirty = false;
    m_building = true;

    wxWindowUpdateLocker lock( this );

    wxString selComponent;
    wxString selNet;

    if( wxTreeListItem sel = m_components->GetSelection(); sel.IsOk() )
        selComponent = rowKey( static_cast<ROW*>( m_components->GetItemData( sel ) ) );

    if( wxTreeListItem sel = m_nets->GetSelection(); sel.IsOk() )
        selNet = rowKey( static_cast<ROW*>( m_nets->GetItemData( sel ) ) );

    std::set<wxString> openComponents = expandedKeys( m_components );
    std::set<wxString> openNets = expandedKeys( m_nets );

    buildComponents();
    buildNets();

    restoreExpanded( m_components, openComponents );
    restoreExpanded( m_nets, openNets );

    for( const auto& [list, key] : { std::make_pair( m_components, selComponent ),
                               std::make_pair( m_nets, selNet ) } )
    {
        if( key.IsEmpty() )
            continue;

        for( wxTreeListItem item = list->GetFirstChild( list->GetRootItem() ); item.IsOk();
             item = list->GetNextSibling( item ) )
        {
            if( rowKey( static_cast<ROW*>( list->GetItemData( item ) ) ) == key )
            {
                list->Select( item );
                list->EnsureVisible( item );
                break;
            }
        }
    }

    // Keep the pins of whatever they showed, or clear them if it is gone
    wxString pinsFor = m_pinsFor;
    m_pins->DeleteAllItems();

    for( wxTreeListCtrl* list : { m_components, m_nets } )
    {
        for( wxTreeListItem item = list->GetFirstChild( list->GetRootItem() ); item.IsOk();
             item = list->GetNextSibling( item ) )
        {
            ROW* row = static_cast<ROW*>( list->GetItemData( item ) );

            if( row && rowKey( row ) == pinsFor )
                fillPins( *row );
        }
    }

    m_building = false;
}


void SCH_NAVIGATOR_LISTS::buildComponents()
{
    m_components->DeleteAllItems();

    // One row per reference: the units of a multi-unit part are one instance, as in Altium
    struct INSTANCE
    {
        SCH_SYMBOL*    symbol = nullptr;
        SCH_SHEET_PATH sheet;
        std::vector<std::pair<SCH_PIN*, SCH_SHEET_PATH>> pins;
    };

    std::map<wxString, INSTANCE, std::function<bool( const wxString&, const wxString& )>> instances(
            []( const wxString& a, const wxString& b )
            {
                return StrNumCmp( a, b, true ) < 0;
            } );

    for( const SCH_SHEET_PATH& sheet : m_frame->Schematic().Hierarchy() )
    {
        for( SCH_ITEM* item : sheet.LastScreen()->Items().OfType( SCH_SYMBOL_T ) )
        {
            SCH_SYMBOL* symbol = static_cast<SCH_SYMBOL*>( item );

            wxString ref = symbol->GetRef( &sheet, false );

            // Power symbols (and legacy ones, marked by a '#' reference) are power ports in
            // Altium, not component instances
            if( symbol->IsPower() || ref.StartsWith( wxS( "#" ) ) )
                continue;

            INSTANCE& inst = instances[ref];

            if( !inst.symbol )
            {
                inst.symbol = symbol;
                inst.sheet = sheet;
            }

            for( SCH_PIN* pin : symbol->GetPins( &sheet ) )
                inst.pins.emplace_back( pin, sheet );
        }
    }

    wxTreeListItem root = m_components->GetRootItem();

    for( auto& [ref, inst] : instances )
    {
        ROW* row = new ROW;
        row->kind = ROW::COMPONENT;
        row->sheet = inst.sheet;
        row->id = inst.symbol->m_Uuid;
        row->ref = ref;

        wxTreeListItem item = m_components->AppendItem( root, ref, ICON_COMPONENT, ICON_COMPONENT,
                                                        row );
        m_components->SetItemText( item, 1, inst.symbol->GetValue( true, &inst.sheet, false ) );
        m_components->SetItemText( item, 2, _( "Component" ) );

        // Parameters
        wxTreeListItem params = m_components->AppendItem( item, _( "Parameters" ), ICON_FOLDER,
                                                          ICON_FOLDER, new ROW );

        for( const SCH_FIELD& field : inst.symbol->GetFields() )
        {
            wxString value = field.GetShownText( &inst.sheet, false );

            if( value.IsEmpty() )
                continue;

            ROW* fieldRow = new ROW;
            fieldRow->kind = ROW::FIELD;
            fieldRow->sheet = inst.sheet;
            fieldRow->id = inst.symbol->m_Uuid;

            wxTreeListItem f = m_components->AppendItem( params, field.GetName(), ICON_PARAMETER,
                                                         ICON_PARAMETER, fieldRow );
            m_components->SetItemText( f, 1, value );
            m_components->SetItemText( f, 2, _( "Parameter" ) );
        }

        // Pins
        wxTreeListItem pins = m_components->AppendItem( item, _( "Pins" ), ICON_FOLDER, ICON_FOLDER,
                                                        new ROW );

        for( auto& [pin, sheet] : inst.pins )
        {
            ROW* pinRow = new ROW;
            pinRow->kind = ROW::PIN;
            pinRow->sheet = sheet;
            pinRow->id = pin->m_Uuid;

            wxTreeListItem p = m_components->AppendItem( pins, pinText( pin, sheet ), ICON_PIN,
                                                         ICON_PIN, pinRow );
            m_components->SetItemText( p, 1, pin->GetElectricalTypeName() );
            m_components->SetItemText( p, 2, _( "Pin" ) );
        }
    }
}


void SCH_NAVIGATOR_LISTS::buildNets()
{
    m_nets->DeleteAllItems();

    CONNECTION_GRAPH* graph = m_frame->Schematic().ConnectionGraph();

    if( !graph )
        return;

    // Local nets carry their sheet path; Altium lists them by name
    auto shownName =
            []( const wxString& aName )
            {
                wxString shown = UnescapeString( aName );

                if( shown.StartsWith( wxS( "/" ) ) && shown.AfterLast( '/' ).length() )
                    shown = shown.AfterLast( '/' );

                return shown;
            };

    std::map<wxString, std::vector<CONNECTION_SUBGRAPH*>,
             std::function<bool( const wxString&, const wxString& )>> nets(
            [&]( const wxString& a, const wxString& b )
            {
                int cmp = StrNumCmp( shownName( a ), shownName( b ), true );
                return cmp != 0 ? cmp < 0 : a < b;
            } );

    for( const auto& [key, subgraphs] : graph->GetNetMap() )
    {
        if( key.Name.IsEmpty() )
            continue;

        std::vector<CONNECTION_SUBGRAPH*>& all = nets[key.Name];
        all.insert( all.end(), subgraphs.begin(), subgraphs.end() );
    }

    wxTreeListItem root = m_nets->GetRootItem();

    for( auto& [name, subgraphs] : nets )
    {
        struct ENTRY
        {
            const SCH_ITEM* item;
            SCH_SHEET_PATH  sheet;
        };

        std::vector<ENTRY> pins, ports, entries, labels, power;
        bool               isBus = false;

        for( CONNECTION_SUBGRAPH* sg : subgraphs )
        {
            if( !sg )
                continue;

            if( sg->GetDriverConnection() && sg->GetDriverConnection()->IsBus() )
                isBus = true;

            for( const SCH_ITEM* item : sg->GetItems() )
            {
                switch( item->Type() )
                {
                case SCH_PIN_T:
                {
                    const SCH_PIN* pin = static_cast<const SCH_PIN*>( item );

                    if( pin->IsPower() )
                        power.push_back( { item, sg->GetSheet() } );
                    else
                        pins.push_back( { item, sg->GetSheet() } );

                    break;
                }
                case SCH_GLOBAL_LABEL_T:
                case SCH_HIER_LABEL_T:     ports.push_back( { item, sg->GetSheet() } );   break;
                case SCH_SHEET_PIN_T:      entries.push_back( { item, sg->GetSheet() } ); break;
                case SCH_LABEL_T:          labels.push_back( { item, sg->GetSheet() } );  break;
                default:                                                                   break;
                }
            }
        }

        // Altium's scope: power ports and off-sheet ports make a net global; sheet entries and
        // hierarchical ports tie it to a sheet interface
        wxString scope = _( "Local To Document" );
        bool     global = !power.empty();
        bool     iface = !entries.empty();

        for( const ENTRY& e : ports )
        {
            if( e.item->Type() == SCH_GLOBAL_LABEL_T )
                global = true;
            else
                iface = true;
        }

        if( global )
            scope = _( "Global" );
        else if( iface )
            scope = _( "Sheet Interface" );

        ROW* row = new ROW;
        row->kind = ROW::NET;
        row->net = name;

        if( !subgraphs.empty() && subgraphs.front() )
            row->sheet = subgraphs.front()->GetSheet();

        wxTreeListItem item = m_nets->AppendItem( root, shownName( name ), ICON_NET, ICON_NET,
                                                  row );
        m_nets->SetItemText( item, 1, isBus ? _( "Bus" ) + wxS( ", " ) + scope : scope );

        auto addFolder =
                [&]( const wxString& aTitle, const std::vector<ENTRY>& aEntries, int aIcon )
                {
                    if( aEntries.empty() )
                        return;

                    wxTreeListItem folder = m_nets->AppendItem( item, aTitle, ICON_FOLDER,
                                                                ICON_FOLDER, new ROW );

                    for( const ENTRY& e : aEntries )
                    {
                        ROW* child = new ROW;
                        child->kind = e.item->Type() == SCH_PIN_T ? ROW::PIN : ROW::ITEM;
                        child->sheet = e.sheet;
                        child->id = e.item->m_Uuid;

                        wxString text;

                        if( e.item->Type() == SCH_PIN_T )
                            text = pinText( static_cast<const SCH_PIN*>( e.item ), e.sheet );
                        else if( e.item->Type() == SCH_SHEET_PIN_T )
                            text = UnescapeString( static_cast<const SCH_SHEET_PIN*>( e.item )->GetText() );
                        else if( const SCH_LABEL_BASE* label = dynamic_cast<const SCH_LABEL_BASE*>( e.item ) )
                            text = UnescapeString( label->GetText() );

                        wxTreeListItem c = m_nets->AppendItem( folder, text, aIcon, aIcon, child );
                        m_nets->SetItemText( c, 1, e.sheet.PathHumanReadable( false, true ) );
                    }
                };

        addFolder( _( "Pins" ), pins, ICON_PIN );
        addFolder( _( "Ports" ), ports, ICON_PORT );
        addFolder( _( "Sheet Entries" ), entries, ICON_SHEET_ENTRY );
        addFolder( _( "Net Labels" ), labels, ICON_LABEL );
        addFolder( _( "Power Ports" ), power, ICON_POWER );
    }
}


void SCH_NAVIGATOR_LISTS::fillPins( const ROW& aRow )
{
    m_pins->DeleteAllItems();
    m_pinsFor = rowKey( &aRow );

    wxTreeListItem root = m_pins->GetRootItem();

    auto addPin =
            [&]( const SCH_PIN* aPin, const SCH_SHEET_PATH& aSheet )
            {
                ROW* row = new ROW;
                row->kind = ROW::PIN;
                row->sheet = aSheet;
                row->id = aPin->m_Uuid;

                wxTreeListItem p = m_pins->AppendItem( root, pinText( aPin, aSheet ), ICON_PIN,
                                                       ICON_PIN, row );
                m_pins->SetItemText( p, 1, aPin->GetElectricalTypeName() );
                m_pins->SetItemText( p, 2, _( "Pin" ) );
            };

    if( aRow.kind == ROW::COMPONENT )
    {
        m_pins->GetDataView()->GetColumn( 0 )->SetTitle( _( "Component Pins" ) );

        for( const SCH_SHEET_PATH& sheet : m_frame->Schematic().Hierarchy() )
        {
            for( SCH_ITEM* item : sheet.LastScreen()->Items().OfType( SCH_SYMBOL_T ) )
            {
                SCH_SYMBOL* symbol = static_cast<SCH_SYMBOL*>( item );

                if( symbol->GetRef( &sheet, false ) == aRow.ref )
                {
                    for( SCH_PIN* pin : symbol->GetPins( &sheet ) )
                        addPin( pin, sheet );
                }
            }
        }
    }
    else if( aRow.kind == ROW::NET )
    {
        m_pins->GetDataView()->GetColumn( 0 )->SetTitle( _( "Net Pins" ) );

        if( CONNECTION_GRAPH* graph = m_frame->Schematic().ConnectionGraph() )
        {
            std::vector<std::pair<const SCH_PIN*, SCH_SHEET_PATH>> pins;

            for( const CONNECTION_SUBGRAPH* sg : graph->GetAllSubgraphs( aRow.net ) )
            {
                for( const SCH_ITEM* item : sg->GetItems() )
                {
                    if( item->Type() == SCH_PIN_T )
                        pins.emplace_back( static_cast<const SCH_PIN*>( item ), sg->GetSheet() );
                }
            }

            std::sort( pins.begin(), pins.end(),
                       []( const auto& a, const auto& b )
                       {
                           return StrNumCmp( pinText( a.first, a.second ),
                                             pinText( b.first, b.second ), true ) < 0;
                       } );

            for( const auto& [pin, sheet] : pins )
                addPin( pin, sheet );
        }
    }
}


void SCH_NAVIGATOR_LISTS::onPicked( wxTreeListCtrl* aList, wxTreeListItem aItem )
{
    if( !aItem.IsOk() )
        return;

    ROW* row = static_cast<ROW*>( aList->GetItemData( aItem ) );

    if( !row || row->kind == ROW::FOLDER )
        return;

    // Picking a component or net lists its pins below
    if( row->kind == ROW::COMPONENT || row->kind == ROW::NET )
    {
        ROW copy = *row;
        fillPins( copy );
    }

    jumpTo( *row );
}


void SCH_NAVIGATOR_LISTS::jumpTo( const ROW& aRow )
{
    switch( aRow.kind )
    {
    case ROW::COMPONENT: jumpToComponent( aRow.ref );         break;
    case ROW::NET:       jumpToNet( aRow.net );                break;
    case ROW::FIELD:
    case ROW::PIN:
    case ROW::ITEM:      jumpToItem( aRow.sheet, aRow.id );    break;
    default:                                                   break;
    }
}


void SCH_NAVIGATOR_LISTS::jumpToItem( const SCH_SHEET_PATH& aSheet, const KIID& aId )
{
    if( !aSheet.LastScreen() )
        return;

    SCH_SHEET_PATH sheet = aSheet;

    if( m_frame->GetCurrentSheet() != sheet )
        m_frame->GetToolManager()->RunAction<SCH_SHEET_PATH*>( SCH_ACTIONS::changeSheet, &sheet );

    SCH_ITEM* item = sheet.ResolveItem( aId );

    if( !item )
        return;

    const OPTIONS& opts = Options();

    // A net's object stays highlighted only while its net is picked
    if( !m_frame->GetHighlightedConnection().IsEmpty() )
    {
        m_frame->SetHighlightedConnection( wxEmptyString );
        m_frame->GetToolManager()->RunAction( SCH_ACTIONS::updateNetHighlighting );
    }

    if( opts.zoom )
    {
        BOX2I box = item->GetBoundingBox();

        if( item->Type() == SCH_PIN_T )
            box.Inflate( std::max( box.GetWidth(), box.GetHeight() ) * 4 );
        else
            box.Inflate( std::max( box.GetWidth(), box.GetHeight() ) / 2 );

        m_frame->GetCanvas()->GetView()->SetViewport( BOX2D( box.GetOrigin(), box.GetSize() ) );
    }

    if( opts.mask )
        m_frame->FocusOnItem( item, !opts.zoom );

    if( opts.select )
    {
        m_frame->GetToolManager()->RunAction( ACTIONS::selectionClear );
        m_frame->GetToolManager()->RunAction<EDA_ITEM*>( ACTIONS::selectItem, item );
    }

    m_frame->GetCanvas()->Refresh();
}


void SCH_NAVIGATOR_LISTS::jumpToComponent( const wxString& aRef )
{
    // The first unit, preferably on the sheet already on screen
    SCH_SHEET_PATH sheet;
    KIID           id = niluuid;

    for( const SCH_SHEET_PATH& candidate : m_frame->Schematic().Hierarchy() )
    {
        for( SCH_ITEM* item : candidate.LastScreen()->Items().OfType( SCH_SYMBOL_T ) )
        {
            SCH_SYMBOL* symbol = static_cast<SCH_SYMBOL*>( item );

            if( symbol->GetRef( &candidate, false ) != aRef )
                continue;

            if( id == niluuid || candidate == m_frame->GetCurrentSheet() )
            {
                sheet = candidate;
                id = symbol->m_Uuid;
            }
        }
    }

    if( id != niluuid )
        jumpToItem( sheet, id );
}


void SCH_NAVIGATOR_LISTS::jumpToNet( const wxString& aNet )
{
    CONNECTION_GRAPH* graph = m_frame->Schematic().ConnectionGraph();

    if( !graph )
        return;

    const std::vector<CONNECTION_SUBGRAPH*>& subgraphs = graph->GetAllSubgraphs( aNet );

    if( subgraphs.empty() )
        return;

    // The sheet on screen if the net is on it, else its first sheet
    SCH_SHEET_PATH sheet = subgraphs.front()->GetSheet();

    for( const CONNECTION_SUBGRAPH* sg : subgraphs )
    {
        if( sg->GetSheet() == m_frame->GetCurrentSheet() )
        {
            sheet = sg->GetSheet();
            break;
        }
    }

    if( m_frame->GetCurrentSheet() != sheet )
        m_frame->GetToolManager()->RunAction<SCH_SHEET_PATH*>( SCH_ACTIONS::changeSheet, &sheet );

    const OPTIONS& opts = Options();

    m_frame->SetHighlightedConnection( opts.mask ? aNet : wxString() );
    m_frame->GetToolManager()->RunAction( SCH_ACTIONS::updateNetHighlighting );

    std::vector<SCH_ITEM*> items;
    BOX2I                  box;

    for( const CONNECTION_SUBGRAPH* sg : subgraphs )
    {
        if( sg->GetSheet() != sheet )
            continue;

        for( SCH_ITEM* item : sg->GetItems() )
        {
            items.push_back( item );

            if( box.GetWidth() == 0 && box.GetHeight() == 0 )
                box = item->GetBoundingBox();
            else
                box.Merge( item->GetBoundingBox() );
        }
    }

    if( opts.zoom && !items.empty() )
    {
        box.Inflate( std::max( box.GetWidth(), box.GetHeight() ) / 10 + schIUScale.MilsToIU( 200 ) );
        m_frame->GetCanvas()->GetView()->SetViewport( BOX2D( box.GetOrigin(), box.GetSize() ) );
    }

    if( opts.select )
    {
        m_frame->GetToolManager()->RunAction( ACTIONS::selectionClear );

        for( SCH_ITEM* item : items )
        {
            if( item->Type() != SCH_PIN_T )
                m_frame->GetToolManager()->RunAction<EDA_ITEM*>( ACTIONS::selectItem, item );
        }
    }

    m_frame->GetCanvas()->Refresh();
}


void SCH_NAVIGATOR_LISTS::ShowNet( const wxString& aNetName )
{
    if( m_building || aNetName.IsEmpty() )
        return;

    if( m_dirty && IsShownOnScreen() )
        rebuild();

    for( wxTreeListItem item = m_nets->GetFirstChild( m_nets->GetRootItem() ); item.IsOk();
         item = m_nets->GetNextSibling( item ) )
    {
        ROW* row = static_cast<ROW*>( m_nets->GetItemData( item ) );

        if( row && row->net == aNetName )
        {
            m_building = true;     // no jump: the net is already highlighted
            m_nets->Select( item );
            m_nets->EnsureVisible( item );
            m_building = false;

            ROW copy = *row;
            fillPins( copy );
            return;
        }
    }
}


void SCH_NAVIGATOR_LISTS::onContextMenu( wxTreeListCtrl* aList, wxTreeListEvent& aEvent )
{
    wxTreeListItem item = aEvent.GetItem();
    ROW*           row = item.IsOk() ? static_cast<ROW*>( aList->GetItemData( item ) ) : nullptr;

    if( row && row->kind == ROW::FOLDER )
        row = nullptr;

    wxString label = row ? aList->GetItemText( item, 0 ) : wxString();

    if( row )
    {
        ROW copy = *row;
        ShowContextMenu( aList, label, [this, copy]() { jumpTo( copy ); } );
    }
    else
    {
        ShowContextMenu( aList, wxEmptyString, nullptr );
    }
}


void SCH_NAVIGATOR_LISTS::ShowContextMenu( wxWindow* aWindow, const wxString& aJumpLabel,
                                           std::function<void()> aJump )
{
    OPTIONS& opts = Options();
    wxMenu   menu;

    menu.Append( ID_VALIDATE, _( "Validate Project" ) );
    menu.AppendSeparator();
    menu.Append( ID_REFRESH, _( "Refresh" ) );
    menu.AppendSeparator();

    wxMenuItem* jump = menu.Append( ID_JUMP, aJumpLabel.IsEmpty()
                                                     ? _( "Jump" )
                                                     : wxString::Format( _( "Jump to %s" ), aJumpLabel ) );
    jump->Enable( aJump != nullptr );
    menu.AppendSeparator();

    // Altium's connective graph has no KiCad counterpart
    menu.Append( ID_SHOW_GRAPH, _( "Show Graph" ) )->Enable( false );
    menu.AppendCheckItem( ID_ZOOM, _( "Zoom on Objects" ) )->Check( opts.zoom );
    menu.AppendCheckItem( ID_MASK, _( "Mask Objects" ) )->Check( opts.mask );
    menu.AppendCheckItem( ID_SELECT, _( "Select Objects" ) )->Check( opts.select );
    menu.AppendSeparator();
    menu.Append( ID_REPORT, _( "Report" ) )->Enable( false );
    menu.AppendSeparator();

    wxMenu* xref = new wxMenu;
    xref->Append( wxID_ANY, _( "Add To Sheet" ) )->Enable( false );
    menu.AppendSubMenu( xref, _( "Port Cross References" ) )->Enable( false );

    wxMenu* show = new wxMenu;
    show->Append( wxID_ANY, _( "Pins" ) )->Enable( false );
    menu.AppendSubMenu( show, _( "Show" ) )->Enable( false );

    int picked = aWindow->GetPopupMenuSelectionFromUser( menu );

    switch( picked )
    {
    case ID_VALIDATE:
        m_frame->GetToolManager()->RunAction( std::string( "eeschema.Project.validate" ) );
        break;

    case ID_REFRESH:
        m_frame->UpdateHierarchyNavigator();
        m_dirty = true;
        rebuild();
        break;

    case ID_JUMP:
        if( aJump )
        {
            bool zoom = opts.zoom;
            opts.zoom = true;
            aJump();
            opts.zoom = zoom;
        }

        break;

    case ID_ZOOM:   opts.zoom = !opts.zoom;     break;
    case ID_MASK:   opts.mask = !opts.mask;     break;
    case ID_SELECT: opts.select = !opts.select; break;
    default:                                    break;
    }
}
