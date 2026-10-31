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

/**
 * Altium's per-object layouts of the PCB Properties panel: a Component, Polygon Pour, Via, Pad,
 * Track, Arc, Text, Fill (rectangle), Region (polygon), graphic line (Altium track), Dimension and
 * Keepout (rule area) each get Altium's sections (Location, Properties, Net Information, Pad
 * Stack, ...) drawn by PROPERTIES_FORMs bound to the panel's property rows; nothing selected shows
 * the Board sections and tabs.  The footprint editor (Altium's PcbLib) uses the same layouts
 * without nets, and its Library Options show the footprint on a Footprint tab.  KiCad-only
 * properties stay in the grid under the sections.
 */

#include "pcb_properties_panel.h"

#include <board.h>
#include <board_commit.h>
#include <pcb_draw_panel_gal.h>
#include <pcb_view.h>
#include <wx/textdlg.h>
#include <board_design_settings.h>
#include <connectivity/connectivity_data.h>
#include <footprint.h>
#include <footprint_edit_frame.h>
#include <frame_type.h>
#include <netclass.h>
#include <pad.h>
#include <pcb_base_edit_frame.h>
#include <pcb_dimension.h>
#include <pcb_marker.h>
#include <pcb_shape.h>
#include <pcb_text.h>
#include <pcb_textbox.h>
#include <pcb_track.h>
#include <eda_group.h>
#include <router/pns_router.h>
#include <router/router_tool.h>
#include <font/font.h>
#include <footprint_library_adapter.h>
#include <project.h>
#include <project_pcb.h>
#include <project/board_project_settings.h>
#include <settings/color_settings.h>
#include <string_utils.h>
#include <template_fieldnames.h>
#include <tool/tool_manager.h>
#include <tools/pcb_actions.h>
#include <tools/pcb_picker_tool.h>
#include <tools/pcb_selection_tool.h>
#include <via_protection_ui_mixin.h>
#include <widgets/properties_form.h>
#include <widgets/altium_layer_tabs.h>
#include <pcbnew_settings.h>
#include <tool/action_manager.h>
#include <hotkeys_basic.h>
#include <zone.h>

#include <wx/choicdlg.h>
#include <wx/menu.h>
#include <wx/propgrid/propgrid.h>
#include <wx/settings.h>

#include <cmath>


namespace
{
const wxString LAYOUT_COMPONENT = wxS( "component" );
const wxString LAYOUT_POLYGON = wxS( "polygon" );
const wxString LAYOUT_VIA = wxS( "via" );
const wxString LAYOUT_PAD = wxS( "pad" );
const wxString LAYOUT_TRACK = wxS( "track" );
const wxString LAYOUT_TEXT = wxS( "text" );
const wxString LAYOUT_ARC = wxS( "arc" );
const wxString LAYOUT_LINE = wxS( "line" );
const wxString LAYOUT_FILL = wxS( "fill" );
const wxString LAYOUT_REGION = wxS( "region" );
const wxString LAYOUT_DIMENSION = wxS( "dimension" );
const wxString LAYOUT_KEEPOUT = wxS( "keepout" );
const wxString LAYOUT_LIBRARY = wxS( "library" );
const wxString LAYOUT_ROUTING = wxS( "routing" );    // Interactive Routing (Tab while routing)

using CELL = PROPERTIES_FORM::CELL;
using OPTS = PROPERTIES_FORM::OPTS;


CELL heading( const wxString& aText )
{
    CELL cell( aText );
    cell.heading = true;
    return cell;
}


CELL fixed( const wxString& aText )
{
    CELL cell( aText );
    cell.readOnly = true;
    return cell;
}


CELL bound( const wxString& aProp )
{
    CELL cell;
    cell.prop = aProp;
    return cell;
}


/// KiCad's IPC-4761 presets for a via, as the via properties dialog uses them
class VIA_PROTECTION : public VIA_PROTECTION_UI_MIXIN
{
public:
    int Get( const PCB_VIA* aVia ) { return (int) getViaConfiguration( aVia ); }

    void Set( PCB_VIA* aVia, int aPreset )
    {
        setViaConfiguration( aVia, static_cast<IPC4761_PRESET>( aPreset ) );
    }

    std::vector<wxString> Names() const
    {
        std::vector<wxString> names;

        for( int ii = 0; ii <= (int) IPC4761_PRESET::CUSTOM; ++ii )
            names.push_back( m_IPC4761Names.at( static_cast<IPC4761_PRESET>( ii ) ) );

        return names;
    }

    static int Custom() { return (int) IPC4761_PRESET::CUSTOM; }
};


/// Altium numbers copper layers from 1 (top) to N (bottom)
int altiumLayerNumber( PCB_LAYER_ID aLayer, int aCopperCount )
{
    if( aLayer == F_Cu )
        return 1;

    if( aLayer == B_Cu )
        return aCopperCount;

    return ( aLayer - B_Cu ) / 2 + 1;
}


wxColour copperColour( PCB_BASE_EDIT_FRAME* aFrame, PCB_LAYER_ID aLayer = F_Cu )
{
    if( COLOR_SETTINGS* cs = aFrame->GetColorSettings() )
        return cs->GetColor( aLayer ).ToColour();

    return wxColour( 0xC8, 0x8C, 0x32 );
}


/// The wxPGProperty's value as a long, or aDefault when unset / mixed
long propLong( wxPropertyGrid* aGrid, const wxString& aName, long aDefault = -1 )
{
    wxPGProperty* prop = aGrid->GetPropertyByName( aName );

    if( !prop || prop->IsValueUnspecified() )
        return aDefault;

    return prop->GetValue().GetLong();
}


/// A bool property as 0 / 1, or -1 when unset / mixed
long propBool( wxPropertyGrid* aGrid, const wxString& aName )
{
    wxPGProperty* prop = aGrid->GetPropertyByName( aName );

    if( !prop || prop->IsValueUnspecified() )
        return -1;

    return prop->GetValue().GetBool() ? 1 : 0;
}


wxColour panelText()
{
    return wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOWTEXT );
}


/**
 * Altium's net eyedropper: click an object on the board and the polygons that were selected
 * take its net.  The picker clears the selection, so the polygons are remembered by KIID and
 * selected again afterwards.
 */
class NET_EYEDROPPER : public PCB_PICKER_TOOL::RECEIVER
{
public:
    static NET_EYEDROPPER& Get()
    {
        static NET_EYEDROPPER instance;
        return instance;
    }

    void Start( PCB_BASE_EDIT_FRAME* aFrame, const std::vector<BOARD_ITEM*>& aTargets )
    {
        m_frame = aFrame;
        m_targets.clear();

        for( BOARD_ITEM* item : aTargets )
            m_targets.push_back( item->m_Uuid );

        PCB_PICKER_TOOL::INTERACTIVE_PARAMS params;
        params.m_Receiver = this;
        params.m_Prompt = _( "Click an object on the net to use..." );
        params.m_ItemFilter =
                []( EDA_ITEM* aItem )
                {
                    BOARD_CONNECTED_ITEM* item = dynamic_cast<BOARD_CONNECTED_ITEM*>( aItem );
                    return item && item->GetNetCode() > 0;
                };

        // Only objects with a net count: a click inside a component takes the pad or track
        // under the cursor, not the component
        params.m_CandidateFilter = params.m_ItemFilter;

        aFrame->GetToolManager()->RunAction( PCB_ACTIONS::selectItemInteractively, params );
    }

    void UpdatePickedPoint( const std::optional<VECTOR2I>& ) override {}

    void UpdatePickedItem( const EDA_ITEM* aItem ) override
    {
        const BOARD_CONNECTED_ITEM* source = dynamic_cast<const BOARD_CONNECTED_ITEM*>( aItem );

        if( !m_frame || !m_frame->GetBoard() || !source )
            return;

        BOARD_COMMIT           commit( m_frame );
        std::vector<EDA_ITEM*> targets;

        for( const KIID& id : m_targets )
        {
            BOARD_CONNECTED_ITEM* item = dynamic_cast<BOARD_CONNECTED_ITEM*>(
                    m_frame->GetBoard()->ResolveItem( id, true ) );

            if( !item )
                continue;

            targets.push_back( item );

            if( item->GetNetCode() != source->GetNetCode() )
            {
                commit.Modify( item );
                item->SetNetCode( source->GetNetCode() );
            }
        }

        if( !commit.Empty() )
            commit.Push( _( "Assign Net" ) );

        m_frame->GetToolManager()->RunAction( ACTIONS::selectionClear );
        m_frame->GetToolManager()->RunAction<std::vector<EDA_ITEM*>*>( ACTIONS::selectItems, &targets );
    }

private:
    PCB_BASE_EDIT_FRAME* m_frame = nullptr;
    std::vector<KIID>    m_targets;
};
} // namespace


// ------------------------------------------------------------------------------------- plumbing

std::vector<BOARD_ITEM*> PCB_PROPERTIES_PANEL::selectedItems()
{
    SELECTION                fallback;
    const SELECTION&         selection = getSelection( fallback );
    std::vector<BOARD_ITEM*> items;

    for( EDA_ITEM* item : selection )
    {
        if( item->IsBOARD_ITEM() )
            items.push_back( static_cast<BOARD_ITEM*>( item ) );
    }

    return items;
}


BOARD_ITEM* PCB_PROPERTIES_PANEL::singleItem()
{
    std::vector<BOARD_ITEM*> items = selectedItems();
    return items.size() == 1 ? items.front() : nullptr;
}


void PCB_PROPERTIES_PANEL::commitSelection( const wxString& aMessage,
                                            const std::function<void( BOARD_COMMIT&, BOARD_ITEM* )>& aChange )
{
    BOARD_COMMIT commit( m_frame );

    for( BOARD_ITEM* item : selectedItems() )
    {
        if( !item->IsNew() )
            aChange( commit, item );
    }

    if( !commit.Empty() )
        commit.Push( aMessage );

    m_frame->Refresh();
    AfterCommit();
}


bool PCB_PROPERTIES_PANEL::parsePropertyValue( const wxString& aName, const wxString& aText, wxVariant& aValue )
{
    wxPGProperty* prop = m_grid->GetPropertyByName( aName );
    return prop && PROPERTIES_FORM::ParseValue( prop, aText, aValue, this );
}


PROPERTIES_FORM* PCB_PROPERTIES_PANEL::addLayoutForm( const wxString& aTitle, const wxString& aLayout )
{
    // Created on the panel (for its font) and moved into the scrolled area as a section
    PROPERTIES_FORM* form = new PROPERTIES_FORM( this, this );
    addSelectionSection( form, aTitle, aLayout );
    m_layoutForms.push_back( { aLayout, form } );
    return form;
}


wxString PCB_PROPERTIES_PANEL::layoutFor( const SELECTION& aSelection ) const
{
    bool board = m_frame->IsType( FRAME_PCB_EDITOR );

    if( ( !board && !m_frame->IsType( FRAME_FOOTPRINT_EDITOR ) ) || aSelection.Empty() )
        return wxEmptyString;

    wxString layout;
    bool     graphicArcs = false;

    for( EDA_ITEM* item : aSelection )
    {
        wxString kind;

        switch( item->Type() )
        {
        // A placed component and a polygon pour exist on a board only (not in Altium's PcbLib)
        case PCB_FOOTPRINT_T: kind = board ? LAYOUT_COMPONENT : wxString(); break;
        case PCB_VIA_T:       kind = LAYOUT_VIA;   break;
        case PCB_PAD_T:       kind = LAYOUT_PAD;   break;
        case PCB_TRACE_T:     kind = LAYOUT_TRACK; break;
        case PCB_ARC_T:       kind = LAYOUT_ARC;   break;
        case PCB_FIELD_T:
        case PCB_TEXT_T:
        case PCB_TEXTBOX_T:   kind = LAYOUT_TEXT;  break;    // Altium's String | Frame text

        case PCB_DIM_ALIGNED_T:
        case PCB_DIM_ORTHOGONAL_T:
        case PCB_DIM_RADIAL_T:
        case PCB_DIM_LEADER_T:
        case PCB_DIM_CENTER_T:
            kind = LAYOUT_DIMENSION;
            break;

        // Altium's primitives: a line is a track, a rectangle a fill, a polygon a region
        case PCB_SHAPE_T:
            switch( static_cast<PCB_SHAPE*>( item )->GetShape() )
            {
            case SHAPE_T::SEGMENT:   kind = LAYOUT_LINE;   break;
            case SHAPE_T::RECTANGLE: kind = LAYOUT_FILL;   break;
            case SHAPE_T::POLY:      kind = LAYOUT_REGION; break;

            case SHAPE_T::ARC:
            case SHAPE_T::CIRCLE:
                kind = LAYOUT_ARC;
                graphicArcs = true;
                break;

            default:
                break;
            }

            break;

        case PCB_ZONE_T:
            if( static_cast<ZONE*>( item )->GetIsRuleArea() )
                kind = LAYOUT_KEEPOUT;
            else if( board )
                kind = LAYOUT_POLYGON;

            break;

        default:
            break;
        }

        if( kind.IsEmpty() )
            return wxEmptyString;

        // Routed tracks and arcs selected together show their common rows: the track's
        if( !layout.IsEmpty() && kind != layout )
        {
            bool routed = ( kind == LAYOUT_TRACK || kind == LAYOUT_ARC )
                          && ( layout == LAYOUT_TRACK || layout == LAYOUT_ARC );

            if( !routed )
                return wxEmptyString;

            kind = LAYOUT_TRACK;
        }

        layout = kind;
    }

    // ... but not graphic arcs with tracks
    if( layout == LAYOUT_TRACK && graphicArcs )
        return wxEmptyString;

    return layout;
}


bool PCB_PROPERTIES_PANEL::isBoardEditor() const
{
    return m_frame->IsType( FRAME_PCB_EDITOR );
}


wxString PCB_PROPERTIES_PANEL::documentLayout() const
{
    if( activeRouter() )
        return LAYOUT_ROUTING;

    return m_frame->IsType( FRAME_FOOTPRINT_EDITOR ) ? LAYOUT_LIBRARY : wxString();
}


wxString PCB_PROPERTIES_PANEL::commonText( const std::function<bool( BOARD_ITEM*, wxString& )>& aText )
{
    wxString result;
    bool     first = true;

    for( BOARD_ITEM* item : selectedItems() )
    {
        wxString text;

        if( !aText( item, text ) )
            continue;

        if( !first && text != result )
            return wxEmptyString;

        result = text;
        first = false;
    }

    return result;
}


void PCB_PROPERTIES_PANEL::selectionGridUpdated( const SELECTION& aSelection )
{
    wxString    layout = layoutFor( aSelection );
    const void* front = aSelection.Empty() ? nullptr : aSelection.Front();

    if( front != m_vertexItem )
    {
        m_vertexItem = front;
        m_vertexRow = -1;
    }

    m_currentLayout = layout;
    m_boundProps.clear();

    if( !layout.IsEmpty() )
    {
        for( LAYOUT_FORM& entry : m_layoutForms )
        {
            if( entry.layout == layout )
                entry.form->CollectBound( m_boundProps );
        }

        // Shown by the Solder Mask Expansion section rather than as rows
        if( layout == LAYOUT_VIA )
        {
            m_boundProps.insert( wxS( "Front tenting" ) );
            m_boundProps.insert( wxS( "Back tenting" ) );
        }
    }

    syncLayoutForms();
    setSelectionLayout( layout );
}


void PCB_PROPERTIES_PANEL::syncLayoutForms()
{
    for( LAYOUT_FORM& entry : m_layoutForms )
    {
        if( entry.layout == m_currentLayout )
            entry.form->Sync();
    }
}


int PCB_PROPERTIES_PANEL::defaultTab( const std::vector<wxString>& aTabs ) const
{
    // Altium's PcbLib Library Options open on the Footprint tab
    return m_frame->IsType( FRAME_FOOTPRINT_EDITOR ) && aTabs.size() == 2 ? 1 : 0;
}


std::vector<wxString> PCB_PROPERTIES_PANEL::panelTabs( const SELECTION& aSelection ) const
{
    // Altium's PcbLib Library Options: General (filter, snapping, units) and Footprint
    if( m_frame->IsType( FRAME_FOOTPRINT_EDITOR ) )
    {
        if( aSelection.Empty() )
            return { _( "General" ), _( "Footprint" ) };

        return {};
    }

    if( !m_frame->IsType( FRAME_PCB_EDITOR ) )
        return {};

    // The Interactive Routing page has no tabs
    if( aSelection.Empty() && activeRouter() )
        return {};

    if( aSelection.Empty() )
        return { _( "General" ), _( "Parameters" ), _( "Health Check" ) + wxS( " ✓" ) };

    if( layoutFor( aSelection ) == LAYOUT_COMPONENT )
        return { _( "General" ), _( "Parameters" ) };

    return {};
}


int PCB_PROPERTIES_PANEL::categoryTab( wxPGProperty* aCategory ) const
{
    // The Interactive Routing page is its sections only: the board's rows are hidden
    if( m_documentMode && activeRouter() )
        return -1;

    if( aCategory->GetName() == wxS( "doc.params" ) )
        return 1;

    // PcbLib Library Options: units are in the "Other" section (as in Altium), not KiCad's
    // General grid rows
    if( m_documentMode && m_frame->IsType( FRAME_FOOTPRINT_EDITOR )
            && aCategory->GetName() == wxS( "doc.general" ) )
    {
        return -1;
    }

    if( aCategory->GetName() == wxS( "doc.health" ) )
        return 2;

    // A component's fields are Altium's parameters
    if( !m_documentMode && m_currentLayout == LAYOUT_COMPONENT && aCategory->IsCategory()
            && aCategory->GetLabel() == wxGetTranslation( _HKI( "Fields" ) ) )
    {
        return 1;
    }

    // Under an Altium layout the sections are the whole panel, as in Altium; KiCad's other
    // settings for the object are in its properties dialog (Modify / double-click)
    if( !m_documentMode && !m_currentLayout.IsEmpty() )
        return -1;

    return 0;
}


// ------------------------------------------------------------------------------ selection filter

namespace
{
struct FILTER_KIND
{
    wxString label;
    bool PCB_SELECTION_FILTER_OPTIONS::* flag;
};


std::vector<FILTER_KIND> filterKinds()
{
    return { { _( "Components" ), &PCB_SELECTION_FILTER_OPTIONS::footprints },
             { _( "Tracks" ),     &PCB_SELECTION_FILTER_OPTIONS::tracks },
             { _( "Pads" ),       &PCB_SELECTION_FILTER_OPTIONS::pads },
             { _( "Vias" ),       &PCB_SELECTION_FILTER_OPTIONS::vias },
             { _( "Polygons" ),   &PCB_SELECTION_FILTER_OPTIONS::zones },
             { _( "Keepouts" ),   &PCB_SELECTION_FILTER_OPTIONS::keepouts },
             { _( "Texts" ),      &PCB_SELECTION_FILTER_OPTIONS::text },
             { _( "Graphics" ),   &PCB_SELECTION_FILTER_OPTIONS::graphics },
             { _( "Dimensions" ), &PCB_SELECTION_FILTER_OPTIONS::dimensions },
             { _( "Points" ),     &PCB_SELECTION_FILTER_OPTIONS::points },
             { _( "Other" ),      &PCB_SELECTION_FILTER_OPTIONS::otherItems } };
}
} // namespace


wxString PCB_PROPERTIES_PANEL::selectionFilterSummary() const
{
    PCB_SELECTION_TOOL* tool = m_frame->GetToolManager()
                                       ? m_frame->GetToolManager()->GetTool<PCB_SELECTION_TOOL>()
                                       : nullptr;

    if( !tool )
        return wxEmptyString;

    PCB_SELECTION_FILTER_OPTIONS& filter = tool->GetFilter();
    wxString                      first;
    int                           more = 0;

    for( const FILTER_KIND& kind : filterKinds() )
    {
        if( !( filter.*kind.flag ) )
            continue;

        if( first.IsEmpty() )
            first = kind.label;
        else
            more++;
    }

    if( first.IsEmpty() )
        return _( "No Objects" );

    if( more == 0 )
        return first;

    return wxString::Format( _( "%s (and %d more)" ), first, more );
}


void PCB_PROPERTIES_PANEL::onSelectionFilterButton( wxWindow* aButton )
{
    PCB_SELECTION_TOOL* tool = m_frame->GetToolManager()->GetTool<PCB_SELECTION_TOOL>();

    if( !tool )
        return;

    PCB_SELECTION_FILTER_OPTIONS& filter = tool->GetFilter();
    std::vector<FILTER_KIND>      kinds = filterKinds();
    wxMenu                        menu;
    const int                     firstId = wxID_HIGHEST + 1;
    const int                     allOnId = firstId + (int) kinds.size();
    const int                     lockedId = allOnId + 1;

    for( size_t ii = 0; ii < kinds.size(); ++ii )
        menu.AppendCheckItem( firstId + (int) ii, kinds[ii].label )->Check( filter.*kinds[ii].flag );

    menu.AppendSeparator();
    menu.AppendCheckItem( lockedId, _( "Locked Objects" ) )->Check( filter.lockedItems );
    menu.Append( allOnId, _( "All - On" ) );

    int id = aButton->GetPopupMenuSelectionFromUser( menu, wxPoint( 0, aButton->GetSize().y ) );

    if( id >= firstId && id < allOnId )
        filter.*kinds[id - firstId].flag = !( filter.*kinds[id - firstId].flag );
    else if( id == allOnId )
        filter.SetAll( true );
    else if( id == lockedId )
        filter.lockedItems = !filter.lockedItems;
    else
        return;

    m_frame->SyncSelectionFilterPanel();
}


// ---------------------------------------------------------------------------------- the layouts

void PCB_PROPERTIES_PANEL::buildAltiumLayouts()
{
    buildComponentLayout();
    buildPolygonLayout();
    buildViaLayout();
    buildPadLayout();
    buildTrackLayout();
    buildTextLayout();
    buildArcLayout();
    buildLineLayout();
    buildFillLayout();
    buildRegionLayout();
    buildDimensionLayout();
    buildKeepoutLayout();

    if( m_frame->IsType( FRAME_PCB_EDITOR ) )
        buildRoutingLayout();

    if( m_frame->IsType( FRAME_FOOTPRINT_EDITOR ) )
        buildLibraryFootprintLayout();
}


void PCB_PROPERTIES_PANEL::buildNetInformation( const wxString& aLayout )
{
    PROPERTIES_FORM* form = addLayoutForm( _( "Net Information" ), aLayout );

    // The net common to the selection, or -1
    auto commonNet =
            [this]() -> int
            {
                int net = -1;

                for( BOARD_ITEM* item : selectedItems() )
                {
                    BOARD_CONNECTED_ITEM* connected = dynamic_cast<BOARD_CONNECTED_ITEM*>( item );

                    if( !connected || ( net >= 0 && connected->GetNetCode() != net ) )
                        return -1;

                    net = connected->GetNetCode();
                }

                return net;
            };

    // Altium draws Net Information as plain text: no fill or rules, row labels on the right
    OPTS plain;
    plain.plainTable = true;
    plain.visible = [this]() { return isBoardEditor(); };   // a library footprint has no nets

    form->AddTable( { 3, 5, 5 },
            [this, commonNet]()
            {
                PROPERTIES_FORM::TABLE_ROWS rows;
                rows.push_back( { heading( wxEmptyString ), heading( _( "Name" ) ), heading( _( "Class" ) ) } );

                int           net = commonNet();
                NETINFO_ITEM* info = net > 0 ? m_frame->GetBoard()->FindNet( net ) : nullptr;
                CELL          name( net == 0 ? _( "No Net" ) : wxString() );
                CELL          netClass( net == 0 ? _( "N/A" ) : wxString() );

                name.readOnly = true;
                netClass.readOnly = true;

                if( info )
                {
                    name.text = UnescapeString( info->GetNetname() );
                    name.link = true;
                    name.click = [this, net]()
                                 {
                                     m_frame->GetToolManager()->RunAction( PCB_ACTIONS::highlightNet, net );
                                 };

                    if( NETCLASS* nc = info->GetNetClass() )
                    {
                        netClass.text = nc->GetHumanReadableName();
                        netClass.link = true;
                        netClass.click = [this]()
                                         {
                                             m_frame->GetToolManager()->RunAction( PCB_ACTIONS::boardSetup );
                                         };
                    }
                }

                rows.push_back( { fixed( _( "Net" ) ), name, netClass } );
                return rows;
            },
            nullptr, plain );

    // A polygon pour or a fill shows its net only
    if( aLayout == LAYOUT_POLYGON || aLayout == LAYOUT_FILL )
        return;

    OPTS boardOnly;
    boardOnly.visible = plain.visible;
    form->AddSeparator( boardOnly );

    // Selected / Total lengths (KiCad has no propagation delay for a selection: N/A)
    form->AddTable( { 3, 5, 5 },
            [this, commonNet]()
            {
                PROPERTIES_FORM::TABLE_ROWS rows;
                rows.push_back( { heading( wxEmptyString ), heading( _( "Selected" ) ),
                                  heading( _( "Total" ) ) } );

                int    net = commonNet();
                double selected = 0.0;
                double total = 0.0;

                for( BOARD_ITEM* item : selectedItems() )
                {
                    if( item->Type() == PCB_TRACE_T || item->Type() == PCB_ARC_T )
                        selected += static_cast<PCB_TRACK*>( item )->GetLength();
                    else if( PCB_SHAPE* shape = dynamic_cast<PCB_SHAPE*>( item ) )
                        selected += shape->GetShape() == SHAPE_T::CIRCLE ? 6.283185307179586 * shape->GetRadius()
                                                                         : shape->GetLength();
                }

                if( net > 0 )
                {
                    for( PCB_TRACK* track : m_frame->GetBoard()->Tracks() )
                    {
                        if( track->GetNetCode() == net && track->Type() != PCB_VIA_T )
                            total += track->GetLength();
                    }
                }

                // Altium's Delay / Length labels are blue (they open the net's length report)
                CELL delay( _( "Delay" ) );
                CELL length( _( "Length" ) );
                delay.link = length.link = true;
                delay.readOnly = length.readOnly = true;

                rows.push_back( { delay, fixed( _( "N/A" ) ), fixed( _( "N/A" ) ) } );
                rows.push_back( { length,
                                  fixed( m_frame->MessageTextFromValue( selected ) ),
                                  fixed( net > 0 ? m_frame->MessageTextFromValue( total ) : _( "N/A" ) ) } );
                return rows;
            },
            nullptr, plain );
}


// ------------------------------------------------------------------------------------ component

void PCB_PROPERTIES_PANEL::buildComponentLayout()
{
    const wxString& L = LAYOUT_COMPONENT;

    auto footprints =
            [this]()
            {
                std::vector<FOOTPRINT*> result;

                for( BOARD_ITEM* item : selectedItems() )
                {
                    if( item->Type() == PCB_FOOTPRINT_T )
                        result.push_back( static_cast<FOOTPRINT*>( item ) );
                }

                return result;
            };

    // Location
    PROPERTIES_FORM* location = addLayoutForm( _( "Location" ), L );
    location->AddXY( wxS( "Position X" ), wxS( "Position Y" ), wxS( "Locked" ) );
    location->AddText( wxS( "Orientation" ), _( "Rotation" ) );

    // Properties
    PROPERTIES_FORM* props = addLayoutForm( _( "Properties" ), L );

    OPTS layer;
    layer.swatch = true;
    props->AddChoice( wxS( "Layer" ), _( "Layer" ), layer );

    // Designator and Comment: blue labels and an eye that shows / hides the text on the board
    auto fieldEye =
            [this, footprints]( FIELD_T aField )
            {
                OPTS opts;
                opts.linkLabel = true;
                opts.eyeState =
                        [footprints, aField]()
                        {
                            std::vector<FOOTPRINT*> fps = footprints();
                            return !fps.empty() && fps.front()->GetField( aField )
                                   && fps.front()->GetField( aField )->IsVisible();
                        };
                opts.eyeToggle =
                        [this, footprints, aField]()
                        {
                            std::vector<FOOTPRINT*> fps = footprints();

                            if( fps.empty() || !fps.front()->GetField( aField ) )
                                return;

                            bool show = !fps.front()->GetField( aField )->IsVisible();

                            commitSelection( show ? _( "Show Text" ) : _( "Hide Text" ),
                                    [aField, show]( BOARD_COMMIT& aCommit, BOARD_ITEM* aItem )
                                    {
                                        if( aItem->Type() != PCB_FOOTPRINT_T )
                                            return;

                                        FOOTPRINT* fp = static_cast<FOOTPRINT*>( aItem );

                                        if( PCB_FIELD* field = fp->GetField( aField ) )
                                        {
                                            aCommit.Modify( fp );
                                            field->SetVisible( show );
                                        }
                                    } );
                        };
                return opts;
            };

    props->AddText( wxS( "Reference" ), _( "Designator" ), fieldEye( FIELD_T::REFERENCE ) );
    props->AddText( wxS( "Value" ), _( "Comment" ), fieldEye( FIELD_T::VALUE ) );

    props->AddInfo( _( "Area" ),
            [this, footprints]()
            {
                std::vector<FOOTPRINT*> fps = footprints();

                if( fps.size() != 1 )
                    return wxString();

                BOX2I  box = fps.front()->GetBoundingBox( false );
                double area = (double) box.GetWidth() * (double) box.GetHeight();

                return m_frame->MessageTextFromValue( area, true, EDA_DATA_TYPE::AREA );
            },
            true );

    props->AddSeparator();

    OPTS description;
    description.lines = 3;
    props->AddText( GetCanonicalFieldName( FIELD_T::DESCRIPTION ), _( "Description" ), description );

    // Altium's component Type, expressed with KiCad's attributes: whether the part is in the
    // schematic, the BOM and the pick-and-place file
    struct COMPONENT_TYPE
    {
        wxString label;
        bool     notInSchematic;
        bool     noBom;
        bool     noPosition;
    };

    static const std::vector<COMPONENT_TYPE> types = {
        { _( "Standard" ),          false, false, false },
        { _( "Standard (No BOM)" ), false, true,  false },
        { _( "Mechanical" ),        true,  true,  false },
        { _( "Graphical" ),         true,  true,  true  } };

    std::vector<wxString> typeNames;

    for( const COMPONENT_TYPE& type : types )
        typeNames.push_back( type.label );

    props->AddCustomChoice( _( "Type" ), typeNames,
            [this]() -> int
            {
                long notInSch = propBool( m_grid, wxS( "Not in Schematic" ) );
                long noBom = propBool( m_grid, wxS( "Exclude From Bill of Materials" ) );
                long noPos = propBool( m_grid, wxS( "Exclude From Position Files" ) );

                for( size_t ii = 0; ii < types.size(); ++ii )
                {
                    if( notInSch == (long) types[ii].notInSchematic && noBom == (long) types[ii].noBom
                            && noPos == (long) types[ii].noPosition )
                    {
                        return (int) ii;
                    }
                }

                return -1;     // a combination Altium has no name for, or a mixed selection
            },
            [this]( int aType )
            {
                if( aType < 0 || aType >= (int) types.size() )
                    return;

                const COMPONENT_TYPE& type = types[aType];

                applyPropertyValues( { { wxS( "Not in Schematic" ), wxVariant( type.notInSchematic ) },
                                       { wxS( "Exclude From Bill of Materials" ), wxVariant( type.noBom ) },
                                       { wxS( "Exclude From Position Files" ), wxVariant( type.noPosition ) } } );
            } );

    // Footprint
    PROPERTIES_FORM* footprint = addLayoutForm( _( "Footprint" ), L );
    footprint->AddText( wxS( "Library Link" ), _( "Footprint Name" ) );

    OPTS libDescription;
    libDescription.lines = 2;
    footprint->AddText( wxS( "Library Description" ), _( "Description" ), libDescription );
    footprint->AddText( wxS( "Keywords" ), _( "Keywords" ) );
    // Altium edits components and pads here; KiCad's dialog keeps the settings the panel
    // has no field for
    footprint->AddLink( []() { return _( "More Properties..." ); },
            [this]()
            {
                BOARD_ITEM* item = singleItem();

                if( !item )
                    return;

                KIID id = item->m_Uuid;

                CallAfter( [this, id]()
                        {
                            BOARD_ITEM* target = m_frame->GetBoard()->ResolveItem( id, true );

                            if( !target )
                                return;

                            m_frame->OnEditItemRequest( target );
                            m_frame->GetToolManager()->ProcessEvent( EVENTS::SelectedItemsModified );
                        } );
            } );

    // Schematic Reference Information
    PROPERTIES_FORM* schematic = addLayoutForm( _( "Schematic Reference Information" ), L );

    auto single =
            [footprints]() -> FOOTPRINT*
            {
                std::vector<FOOTPRINT*> fps = footprints();
                return fps.size() == 1 ? fps.front() : nullptr;
            };

    schematic->AddInfo( _( "Designator" ),
            [single]()
            {
                FOOTPRINT* fp = single();
                return fp ? fp->GetReference() : wxString();
            },
            true );
    schematic->AddInfo( _( "Hierarchical Path" ),
            [single]()
            {
                FOOTPRINT* fp = single();

                if( !fp )
                    return wxString();

                if( fp->GetSheetname().IsEmpty() )
                    return fp->GetSheetfile();

                return fp->GetSheetname();
            },
            true );
    schematic->AddInfo( _( "Unique Id" ),
            [single]()
            {
                FOOTPRINT* fp = single();
                return fp ? fp->GetPath().AsString() : wxString();
            },
            true );
}


// -------------------------------------------------------------------------------- polygon pour

void PCB_PROPERTIES_PANEL::buildPolygonLayout()
{
    const wxString& L = LAYOUT_POLYGON;

    auto singleZone =
            [this]() -> ZONE*
            {
                return dynamic_cast<ZONE*>( singleItem() );
            };

    auto run =
            [this]( const TOOL_ACTION& aAction )
            {
                return [this, &aAction]()
                       {
                           m_frame->GetToolManager()->RunAction( aAction );
                           UpdateData();
                       };
            };

    // Actions
    PROPERTIES_FORM* actions = addLayoutForm( _( "Actions" ), L );

    actions->AddButtons( {
            { _( "Repour" ),        run( PCB_ACTIONS::zoneFill ) },
            { _( "Shelve" ),        run( PCB_ACTIONS::zoneUnfill ) },
            { _( "Assign net" ),
              [this]()
              {
                  wxArrayString names;
                  std::vector<int> codes;

                  for( unsigned ii = 0; ii < m_nets.GetCount(); ++ii )
                  {
                      names.Add( m_nets.GetLabel( ii ) );
                      codes.push_back( m_nets.GetValue( ii ) );
                  }

                  wxSingleChoiceDialog dlg( this, _( "Net:" ), _( "Assign Net" ), names );

                  if( dlg.ShowModal() == wxID_OK && dlg.GetSelection() >= 0 )
                      applyPropertyValues( { { wxS( "Net" ), wxVariant( (long) codes[dlg.GetSelection()] ) } } );
              } },
            { _( "Bring to front" ), run( PCB_ACTIONS::zonePriorityMoveToTop ) },
            { _( "Send to back" ),   run( PCB_ACTIONS::zonePriorityMoveToBottom ) },
            { _( "Modify" ),         run( PCB_ACTIONS::properties ) } } );

    buildNetInformation( L );

    // Properties
    PROPERTIES_FORM* props = addLayoutForm( _( "Properties" ), L );

    // Net, with Altium's eyedropper: pick the net from an object on the board
    OPTS net;
    net.linkLabel = true;
    net.pickerShown = []() { return true; };
    net.picker = [this]() { NET_EYEDROPPER::Get().Start( m_frame, selectedItems() ); };
    props->AddChoice( wxS( "Net" ), _( "Net" ), net );

    // Layer: a polygon pour is on one copper layer (KiCad's multi-layer zones keep their
    // layers in the zone dialog)
    auto copperLayers =
            [this]()
            {
                std::vector<PCB_LAYER_ID> layers;

                for( PCB_LAYER_ID layer : m_frame->GetBoard()->GetEnabledLayers().CuStack() )
                    layers.push_back( layer );

                return layers;
            };

    auto zoneLayer =
            [this]() -> PCB_LAYER_ID
            {
                PCB_LAYER_ID layer = UNDEFINED_LAYER;

                for( BOARD_ITEM* item : selectedItems() )
                {
                    ZONE* zone = dynamic_cast<ZONE*>( item );

                    if( !zone || zone->GetLayerSet().count() != 1 )
                        return UNDEFINED_LAYER;

                    if( layer != UNDEFINED_LAYER && zone->GetFirstLayer() != layer )
                        return UNDEFINED_LAYER;

                    layer = zone->GetFirstLayer();
                }

                return layer;
            };

    OPTS layer;
    layer.choices =
            [this, copperLayers]()
            {
                std::vector<wxString> names;

                for( PCB_LAYER_ID id : copperLayers() )
                    names.push_back( m_frame->GetBoard()->GetLayerName( id ) );

                return names;
            };
    layer.swatchColour = [this, zoneLayer]() { return copperColour( m_frame, zoneLayer() ); };
    layer.visible =
            [this]()
            {
                for( BOARD_ITEM* item : selectedItems() )
                {
                    if( ZONE* zone = dynamic_cast<ZONE*>( item ); !zone || zone->GetLayerSet().count() != 1 )
                        return false;
                }

                return true;
            };

    props->AddCustomChoice( _( "Layer" ), {},
            [copperLayers, zoneLayer]() -> int
            {
                std::vector<PCB_LAYER_ID> layers = copperLayers();
                auto it = std::find( layers.begin(), layers.end(), zoneLayer() );
                return it == layers.end() ? -1 : (int) ( it - layers.begin() );
            },
            [this, copperLayers]( int aIndex )
            {
                std::vector<PCB_LAYER_ID> layers = copperLayers();

                if( aIndex < 0 || aIndex >= (int) layers.size() )
                    return;

                commitSelection( _( "Change Polygon Layer" ),
                        [&]( BOARD_COMMIT& aCommit, BOARD_ITEM* aItem )
                        {
                            if( ZONE* zone = dynamic_cast<ZONE*>( aItem ) )
                            {
                                aCommit.Modify( zone );
                                zone->SetLayer( layers[aIndex] );
                            }
                        } );
            },
            layer );

    props->AddText( wxS( "Name" ), _( "Name" ) );
    props->AddInfo( _( "Area" ),
            [this, singleZone]()
            {
                ZONE* zone = singleZone();

                if( !zone )
                    return wxString();

                return m_frame->MessageTextFromValue( zone->CalculateOutlineArea(), true,
                                                      EDA_DATA_TYPE::AREA );
            } );
    props->AddSeparator();
    props->AddXY( wxS( "Position X" ), wxS( "Position Y" ), wxS( "Locked" ) );

    // Fill: Solid | Hatched | None (None: shelved, i.e. not filled)
    props->AddSeparator();
    props->AddCustomSegmented( { _( "Solid" ), _( "Hatched" ), _( "None" ) },
            [this, singleZone]() -> int
            {
                ZONE* zone = singleZone();

                if( !zone )
                    return (int) propLong( m_grid, wxS( "Fill Mode" ) );

                if( !zone->IsFilled() )
                    return 2;

                return zone->GetFillMode() == ZONE_FILL_MODE::HATCH_PATTERN ? 1 : 0;
            },
            [this]( int aSegment )
            {
                TOOL_MANAGER* toolMgr = m_frame->GetToolManager();

                if( aSegment == 2 )
                {
                    toolMgr->RunAction( PCB_ACTIONS::zoneUnfill );
                    UpdateData();
                    return;
                }

                long mode = aSegment == 1 ? (long) ZONE_FILL_MODE::HATCH_PATTERN
                                          : (long) ZONE_FILL_MODE::POLYGONS;

                if( propLong( m_grid, wxS( "Fill Mode" ) ) != mode )
                    applyPropertyValues( { { wxS( "Fill Mode" ), wxVariant( mode ) } } );

                bool unfilled = false;

                for( BOARD_ITEM* item : selectedItems() )
                {
                    if( ZONE* zone = dynamic_cast<ZONE*>( item ) )
                        unfilled |= !zone->IsFilled();
                }

                if( unfilled )
                {
                    toolMgr->RunAction( PCB_ACTIONS::zoneFill );
                    UpdateData();
                }
            } );

    auto hatched =
            [this]()
            {
                return propLong( m_grid, wxS( "Fill Mode" ) ) == (long) ZONE_FILL_MODE::HATCH_PATTERN;
            };

    auto solid =
            [this]()
            {
                return propLong( m_grid, wxS( "Fill Mode" ) ) == (long) ZONE_FILL_MODE::POLYGONS;
            };

    OPTS previewLink;
    previewLink.visible = [hatched, solid]() { return hatched() || solid(); };
    props->AddLink( [this]() { return m_showPreview ? _( "Hide preview" ) : _( "Show preview" ); },
                    [this]()
                    {
                        m_showPreview = !m_showPreview;
                        syncLayoutForms();
                    },
                    previewLink );

    OPTS hatchOnly;
    hatchOnly.visible = hatched;
    props->AddTwin( wxS( "Hatch Width" ), _( "Track Width" ), wxS( "Hatch Gap" ), _( "Grid Size" ),
                    hatchOnly );

    // Illustration: the hatch (track width and grid size), or a solid pour around a pad
    OPTS illustration;
    illustration.visible = [this, hatched, solid]() { return m_showPreview && ( hatched() || solid() ); };
    props->AddPaint( 96,
            [this, hatched]( wxDC& aDC, const wxRect& aRect )
            {
                wxColour copper = copperColour( m_frame );
                wxColour fg = panelText();
                wxRect   area = aRect.Deflate( FromDIP( 24 ), FromDIP( 8 ) );

                aDC.SetPen( *wxTRANSPARENT_PEN );

                if( hatched() )
                {
                    int width = FromDIP( 5 );
                    int pitch = FromDIP( 18 );

                    aDC.SetBrush( wxBrush( copper ) );

                    wxDCClipper clip( aDC, area );

                    for( int x = area.x; x < area.GetRight(); x += pitch )
                        aDC.DrawRectangle( x, area.y, width, area.height );

                    for( int y = area.y; y < area.GetBottom(); y += pitch )
                        aDC.DrawRectangle( area.x, y, area.width, width );

                    // Dimension marks: one track width and one grid gap
                    aDC.SetPen( wxPen( fg ) );
                    int my = area.y + pitch + width + FromDIP( 6 );
                    aDC.DrawLine( area.x + pitch, my, area.x + pitch + width, my );
                    aDC.DrawLine( area.x + pitch * 2 + width, my, area.x + pitch * 3, my );
                }
                else
                {
                    aDC.SetBrush( wxBrush( copper ) );
                    aDC.DrawRectangle( area );

                    // A pad in its clearance void, joined by thermal spokes
                    wxPoint centre( area.x + area.width / 2, area.y + area.height / 2 );
                    int     pad = std::min( area.width, area.height ) / 5;
                    int     gap = FromDIP( 6 );

                    aDC.SetBrush( wxBrush( GetBackgroundColour() ) );
                    aDC.DrawCircle( centre, pad + gap );
                    aDC.SetBrush( wxBrush( copper ) );
                    aDC.DrawCircle( centre, pad );
                    aDC.DrawRectangle( centre.x - FromDIP( 2 ), centre.y - pad - gap, FromDIP( 4 ),
                                       2 * ( pad + gap ) );
                    aDC.DrawRectangle( centre.x - pad - gap, centre.y - FromDIP( 2 ),
                                       2 * ( pad + gap ), FromDIP( 4 ) );
                }
            },
            illustration );

    OPTS hatchTitle;
    hatchTitle.visible = hatched;
    props->AddTitle( _( "Hatch mode" ), hatchTitle );

    // 90 Degree / 45 Degree; KiCad hatches always cross, so no Horizontal / Vertical only
    props->AddCustomSegmented( { _( "90 Degree" ), _( "45 Degree" ), _( "Horizontal" ), _( "Vertical" ) },
            [singleZone]() -> int
            {
                ZONE* zone = singleZone();

                if( !zone )
                    return -1;

                double angle = std::fmod( std::abs( zone->GetHatchOrientation().AsDegrees() ), 90.0 );

                if( angle < 0.01 || angle > 89.99 )
                    return 0;

                if( std::abs( angle - 45.0 ) < 0.01 )
                    return 1;

                return -1;
            },
            [this]( int aSegment )
            {
                wxVariant value;

                if( parsePropertyValue( wxS( "Hatch Orientation" ), aSegment == 1 ? wxS( "45" ) : wxS( "0" ),
                                        value ) )
                {
                    applyPropertyValues( { { wxS( "Hatch Orientation" ), value } } );
                }
            },
            { true, true, false, false }, wxEmptyString, hatchOnly );

    OPTS islandArea;
    islandArea.visible =
            [this]()
            {
                return propLong( m_grid, wxS( "Remove Islands" ) ) == (long) ISLAND_REMOVAL_MODE::AREA;
            };
    props->AddText( wxS( "Minimum Island Area" ), _( "Remove Islands Less Than" ), islandArea );

    props->AddText( wxS( "Minimum Width" ), _( "Min Prim Length" ) );

    // Remove Dead Copper: KiCad's island removal on (always) or off (never)
    props->AddCustomCheck( _( "Remove Dead Copper" ),
            [this]() -> int
            {
                long mode = propLong( m_grid, wxS( "Remove Islands" ), -2 );

                if( mode == -2 )
                    return 2;

                return mode != (long) ISLAND_REMOVAL_MODE::NEVER ? 1 : 0;
            },
            [this]( bool aOn )
            {
                long mode = aOn ? (long) ISLAND_REMOVAL_MODE::ALWAYS : (long) ISLAND_REMOVAL_MODE::NEVER;
                applyPropertyValues( { { wxS( "Remove Islands" ), wxVariant( mode ) } } );
            } );

    buildOutlineVertices( L );
}


void PCB_PROPERTIES_PANEL::buildOutlineVertices( const wxString& aLayout )
{
    // The outline of the single selected zone (polygon pour, keepout) or polygon shape (region)
    auto outline =
            [this]() -> std::pair<BOARD_ITEM*, SHAPE_POLY_SET*>
            {
                BOARD_ITEM* item = singleItem();

                if( ZONE* zone = dynamic_cast<ZONE*>( item ) )
                    return { zone, zone->Outline() };

                if( PCB_SHAPE* shape = dynamic_cast<PCB_SHAPE*>( item ) )
                {
                    if( shape->GetShape() == SHAPE_T::POLY )
                        return { shape, &shape->GetPolyShape() };
                }

                return { nullptr, nullptr };
            };

    // After an outline change a zone redraws its hatching
    auto changed =
            []( BOARD_ITEM* aItem )
            {
                if( ZONE* zone = dynamic_cast<ZONE*>( aItem ) )
                    zone->HatchBorder();
            };

    PROPERTIES_FORM* vertices = addLayoutForm( _( "Outline Vertices" ), aLayout );

    auto setVertex =
            [this, outline, changed]( int aIndex, bool aX, const wxString& aText )
            {
                auto [item, poly] = outline();

                if( !poly || aIndex < 0 || aIndex >= poly->TotalVertices() )
                    return;

                ORIGIN_TRANSFORMS& origin = m_frame->GetOriginTransforms();
                int                value = m_frame->ValueFromString( aText );
                VECTOR2I           pt = poly->CVertex( aIndex );

                if( aX )
                    pt.x = origin.FromDisplay( value, ORIGIN_TRANSFORMS::ABS_X_COORD );
                else
                    pt.y = origin.FromDisplay( value, ORIGIN_TRANSFORMS::ABS_Y_COORD );

                BOARD_COMMIT commit( m_frame );
                commit.Modify( item );
                poly->SetVertex( aIndex, pt );
                changed( item );
                commit.Push( _( "Edit Polygon Vertex" ) );

                m_frame->Refresh();
                AfterCommit();
            };

    vertices->AddTable( { 3, 5, 5, 5 },
            [this, outline, setVertex]()
            {
                PROPERTIES_FORM::TABLE_ROWS rows;
                rows.push_back( { heading( _( "Index" ) ), heading( wxS( "X" ) ), heading( wxS( "Y" ) ),
                                  heading( _( "Arc Angle (Neg = CW)" ) ) } );

                SHAPE_POLY_SET* poly = outline().second;

                if( !poly )
                    return rows;

                ORIGIN_TRANSFORMS& origin = m_frame->GetOriginTransforms();

                for( int ii = 0; ii < poly->TotalVertices(); ++ii )
                {
                    const VECTOR2I& pt = poly->CVertex( ii );
                    CELL x( m_frame->StringFromValue( origin.ToDisplay( pt.x, ORIGIN_TRANSFORMS::ABS_X_COORD ),
                                                      true ) );
                    CELL y( m_frame->StringFromValue( origin.ToDisplay( pt.y, ORIGIN_TRANSFORMS::ABS_Y_COORD ),
                                                      true ) );

                    x.edit = [setVertex, ii]( const wxString& aText ) { setVertex( ii, true, aText ); };
                    y.edit = [setVertex, ii]( const wxString& aText ) { setVertex( ii, false, aText ); };

                    // KiCad outlines are straight segments
                    rows.push_back( { fixed( wxString::Format( wxS( "%d" ), ii ) ), x, y,
                                      fixed( wxS( "0.000" ) ) } );
                }

                return rows;
            },
            &m_vertexRow );

    vertices->AddButtons( {
            { _( "Add" ),
              [this, outline, changed]()
              {
                  auto [item, poly] = outline();

                  if( !poly || poly->TotalVertices() == 0 )
                      return;

                  int vertex = m_vertexRow - 1;    // the table's first row is its heading

                  if( vertex < 0 || vertex >= poly->TotalVertices() )
                      vertex = poly->TotalVertices() - 1;

                  int prev = 0;
                  int next = 0;

                  if( !poly->GetNeighbourIndexes( vertex, &prev, &next ) )
                      return;

                  // A new vertex halfway along the edge before the chosen one
                  VECTOR2I mid = ( poly->CVertex( prev ) + poly->CVertex( vertex ) ) / 2;

                  BOARD_COMMIT commit( m_frame );
                  commit.Modify( item );
                  poly->InsertVertex( vertex, mid );
                  changed( item );
                  commit.Push( _( "Add Polygon Vertex" ) );

                  m_vertexRow = vertex + 1;
                  m_frame->Refresh();
                  AfterCommit();
              } },
            { _( "Delete" ),
              [this, outline, changed]()
              {
                  auto [item, poly] = outline();
                  int  vertex = m_vertexRow - 1;

                  if( !poly || vertex < 0 || vertex >= poly->TotalVertices() )
                      return;

                  SHAPE_POLY_SET::VERTEX_INDEX index;

                  if( !poly->GetRelativeIndices( vertex, &index ) )
                      return;

                  // A contour keeps at least three vertices
                  if( poly->CPolygon( index.m_polygon )[index.m_contour].PointCount() <= 3 )
                  {
                      wxBell();
                      return;
                  }

                  BOARD_COMMIT commit( m_frame );
                  commit.Modify( item );
                  poly->RemoveVertex( vertex );
                  changed( item );
                  commit.Push( _( "Delete Polygon Vertex" ) );

                  m_vertexRow = -1;
                  m_frame->Refresh();
                  AfterCommit();
              } } } );
}


// ------------------------------------------------------------------------------------------- via

void PCB_PROPERTIES_PANEL::buildViaLayout()
{
    const wxString& L = LAYOUT_VIA;

    auto singleVia =
            [this]() -> PCB_VIA*
            {
                BOARD_ITEM* item = singleItem();
                return item && item->Type() == PCB_VIA_T ? static_cast<PCB_VIA*>( item ) : nullptr;
            };

    buildNetInformation( L );

    // Definition
    PROPERTIES_FORM* def = addLayoutForm( _( "Definition" ), L );

    OPTS net;
    net.linkLabel = true;
    net.pickerShown = []() { return true; };
    net.picker = [this]() { NET_EYEDROPPER::Get().Start( m_frame, selectedItems() ); };
    def->AddChoice( wxS( "Net" ), _( "Net" ), net );

    // Altium names a via by its span: "Thru 1:4", "Blind 1:2", ...
    def->AddInfo( _( "Name" ),
            [this, singleVia]()
            {
                PCB_VIA* via = singleVia();

                if( !via )
                    return wxString();

                wxString kind;

                switch( via->GetViaType() )
                {
                case VIATYPE::BLIND:    kind = _( "Blind" );  break;
                case VIATYPE::BURIED:   kind = _( "Buried" ); break;
                case VIATYPE::MICROVIA: kind = _( "Micro" );  break;
                default:                kind = _( "Thru" );   break;
                }

                int count = m_frame->GetBoard()->GetCopperLayerCount();

                return wxString::Format( wxS( "%s %d:%d" ), kind,
                                         altiumLayerNumber( via->TopLayer(), count ),
                                         altiumLayerNumber( via->BottomLayer(), count ) );
            },
            true );

    def->AddChoice( wxS( "Via Type" ), _( "Via Type" ) );

    OPTS layer;
    layer.swatch = true;
    def->AddChoice( wxS( "Layer Top" ), _( "Start Layer" ), layer );
    def->AddChoice( wxS( "Layer Bottom" ), _( "End Layer" ), layer );
    def->AddSeparator();
    def->AddXY( wxS( "Position X" ), wxS( "Position Y" ), wxS( "Locked" ) );

    // Via Stack
    PROPERTIES_FORM* stack = addLayoutForm( _( "Via Stack" ), L );

    stack->AddLink( [this]() { return m_showPreview ? _( "Hide preview" ) : _( "Show preview" ); },
                    [this]()
                    {
                        m_showPreview = !m_showPreview;
                        syncLayoutForms();
                    } );

    // A cross-section of the board through the via: its pads on the layers it spans, the
    // plated barrel and the hole
    OPTS preview;
    preview.visible = [this, singleVia]() { return m_showPreview && singleVia(); };
    stack->AddPaint( 110,
            [this, singleVia]( wxDC& aDC, const wxRect& aRect )
            {
                PCB_VIA* via = singleVia();

                if( !via )
                    return;

                BOARD*   board = m_frame->GetBoard();
                int      count = std::max( 2, board->GetCopperLayerCount() );
                wxColour copper = copperColour( m_frame );
                wxColour core( 0x4E, 0x6B, 0x46 );
                wxColour fg = panelText();
                wxRect   slab = aRect.Deflate( FromDIP( 20 ), FromDIP( 18 ) );
                int      cx = slab.x + slab.width / 2;
                int      diameter = std::max( 1, via->GetWidth( F_Cu ) );
                int      hole = std::max( 1, via->GetDrillValue() );
                int      padW = slab.width * 2 / 5;
                int      holeW = std::max( FromDIP( 4 ), padW * hole / diameter );
                int      copperH = std::max( 2, FromDIP( 3 ) );

                aDC.SetPen( *wxTRANSPARENT_PEN );
                aDC.SetBrush( wxBrush( core ) );
                aDC.DrawRectangle( slab );

                auto layerY =
                        [&]( int aNumber )
                        {
                            return slab.y + ( slab.height - copperH ) * ( aNumber - 1 ) / ( count - 1 );
                        };

                int top = altiumLayerNumber( via->TopLayer(), count );
                int bottom = altiumLayerNumber( via->BottomLayer(), count );

                aDC.SetBrush( wxBrush( copper ) );

                // Copper planes (thin), then the via's pads on the layers it spans
                for( int ii = 1; ii <= count; ++ii )
                {
                    int y = layerY( ii );
                    aDC.SetBrush( wxBrush( PROPERTIES_FORM::Mix( core, copper, 0.35 ) ) );
                    aDC.DrawRectangle( slab.x, y + copperH / 3, slab.width, std::max( 1, copperH / 3 ) );

                    if( ii >= top && ii <= bottom )
                    {
                        aDC.SetBrush( wxBrush( copper ) );
                        aDC.DrawRectangle( cx - padW / 2, y, padW, copperH );
                    }
                }

                // Barrel and hole
                int y0 = layerY( top );
                int y1 = layerY( bottom ) + copperH;
                int wall = std::max( 2, FromDIP( 2 ) );

                aDC.SetBrush( wxBrush( copper ) );
                aDC.DrawRectangle( cx - holeW / 2 - wall, y0, holeW + 2 * wall, y1 - y0 );
                aDC.SetBrush( wxBrush( GetBackgroundColour() ) );
                aDC.DrawRectangle( cx - holeW / 2, y0, holeW, y1 - y0 );

                // Dimensions
                aDC.SetTextForeground( fg );
                aDC.SetPen( wxPen( fg ) );

                wxString dia = m_frame->MessageTextFromValue( diameter );
                wxSize   ext = aDC.GetTextExtent( dia );
                aDC.DrawLine( cx - padW / 2, slab.y - FromDIP( 6 ), cx + padW / 2, slab.y - FromDIP( 6 ) );
                aDC.DrawText( dia, cx - ext.x / 2, aRect.y );

                wxString drill = m_frame->MessageTextFromValue( hole );
                ext = aDC.GetTextExtent( drill );
                aDC.DrawLine( cx - holeW / 2, slab.GetBottom() + FromDIP( 5 ), cx + holeW / 2,
                              slab.GetBottom() + FromDIP( 5 ) );
                aDC.DrawText( drill, cx - ext.x / 2, aRect.GetBottom() - ext.y );
            },
            preview );

    // Simple / Top-Middle-Bottom / Full Stack: KiCad's padstack modes
    stack->AddCustomSegmented( { _( "Simple" ), _( "Top-Middle-Bottom" ), _( "Full Stack" ) },
            [singleVia]() -> int
            {
                PCB_VIA* via = singleVia();
                return via ? (int) via->Padstack().Mode() : -1;
            },
            [this]( int aMode )
            {
                commitSelection( _( "Change Via Stack" ),
                        [aMode]( BOARD_COMMIT& aCommit, BOARD_ITEM* aItem )
                        {
                            if( aItem->Type() != PCB_VIA_T )
                                return;

                            PCB_VIA* via = static_cast<PCB_VIA*>( aItem );

                            if( (int) via->Padstack().Mode() == aMode )
                                return;

                            aCommit.Modify( via );
                            via->Padstack().SetMode( static_cast<PADSTACK::MODE>( aMode ) );
                        } );
            } );

    stack->AddText( wxS( "Diameter" ), _( "Diameter" ) );

    // Per-layer diameters of a top/middle/bottom or full stack
    OPTS layered;
    layered.visible =
            [singleVia]()
            {
                PCB_VIA* via = singleVia();
                return via && via->Padstack().Mode() != PADSTACK::MODE::NORMAL;
            };
    stack->AddTable( { 5, 5 },
            [this, singleVia]()
            {
                PROPERTIES_FORM::TABLE_ROWS rows;
                PCB_VIA*                    via = singleVia();

                if( !via )
                    return rows;

                rows.push_back( { heading( _( "Layer" ) ), heading( _( "Diameter" ) ) } );

                for( PCB_LAYER_ID layer : via->Padstack().UniqueLayers() )
                {
                    wxString name = layer == PADSTACK::INNER_LAYERS && via->Padstack().Mode()
                                                                               == PADSTACK::MODE::FRONT_INNER_BACK
                                            ? _( "Mid Layers" )
                                            : m_frame->GetBoard()->GetLayerName( layer );

                    rows.push_back( { fixed( name ),
                                      fixed( m_frame->MessageTextFromValue( via->GetWidth( layer ) ) ) } );
                }

                return rows;
            },
            nullptr, layered );

    stack->AddText( wxS( "Hole" ), _( "Hole Size" ) );

    // Solder Mask Expansion: Rule (the board's tenting) or Manual (tented top / bottom)
    PROPERTIES_FORM* mask = addLayoutForm( _( "Solder Mask Expansion" ), L );

    auto tenting =
            [this]( const wxString& aProp ) -> long
            {
                return propLong( m_grid, aProp, -1 );
            };

    mask->AddCustomSegmented( { _( "Rule" ), _( "Manual" ) },
            [tenting]() -> int
            {
                long front = tenting( wxS( "Front tenting" ) );
                long back = tenting( wxS( "Back tenting" ) );

                if( front < 0 || back < 0 )
                    return -1;

                return front == (long) TENTING_MODE::FROM_BOARD && back == (long) TENTING_MODE::FROM_BOARD
                               ? 0 : 1;
            },
            [this]( int aSegment )
            {
                // Manual starts from what the board rules give (tented by default)
                long mode = aSegment == 0 ? (long) TENTING_MODE::FROM_BOARD : (long) TENTING_MODE::TENTED;

                applyPropertyValues( { { wxS( "Front tenting" ), wxVariant( mode ) },
                                       { wxS( "Back tenting" ), wxVariant( mode ) } } );
            } );

    OPTS manual;
    manual.visible =
            [tenting]()
            {
                return tenting( wxS( "Front tenting" ) ) > (long) TENTING_MODE::FROM_BOARD
                       || tenting( wxS( "Back tenting" ) ) > (long) TENTING_MODE::FROM_BOARD;
            };

    for( const auto& [prop, label] : { std::make_pair( wxString( wxS( "Front tenting" ) ), _( "Tented Top" ) ),
                                       std::make_pair( wxString( wxS( "Back tenting" ) ), _( "Tented Bottom" ) ) } )
    {
        wxString name = prop;

        mask->AddCustomCheck( label,
                [tenting, name]() -> int
                {
                    long mode = tenting( name );

                    if( mode == (long) TENTING_MODE::TENTED )
                        return 1;

                    return mode == (long) TENTING_MODE::NOT_TENTED ? 0 : 2;
                },
                [this, name]( bool aOn )
                {
                    long mode = aOn ? (long) TENTING_MODE::TENTED : (long) TENTING_MODE::NOT_TENTED;
                    applyPropertyValues( { { name, wxVariant( mode ) } } );
                },
                true, manual );
    }

    // Via Types & Features: IPC-4761
    PROPERTIES_FORM* features = addLayoutForm( _( "Via Types & Features" ), L );

    auto protection = std::make_shared<VIA_PROTECTION>();

    features->AddCustomChoice( _( "IPC 4761 Via Type" ), protection->Names(),
            [this, protection]() -> int
            {
                int preset = -1;

                for( BOARD_ITEM* item : selectedItems() )
                {
                    if( item->Type() != PCB_VIA_T )
                        return -1;

                    int viaPreset = protection->Get( static_cast<PCB_VIA*>( item ) );

                    if( preset >= 0 && viaPreset != preset )
                        return -1;

                    preset = viaPreset;
                }

                return preset;
            },
            [this, protection]( int aPreset )
            {
                if( aPreset < 0 || aPreset >= VIA_PROTECTION::Custom() )
                    return;

                commitSelection( _( "Change Via Type" ),
                        [protection, aPreset]( BOARD_COMMIT& aCommit, BOARD_ITEM* aItem )
                        {
                            if( aItem->Type() != PCB_VIA_T )
                                return;

                            aCommit.Modify( aItem );
                            protection->Set( static_cast<PCB_VIA*>( aItem ), aPreset );
                        } );
            } );
}


// ------------------------------------------------------------------------------------------- pad

void PCB_PROPERTIES_PANEL::buildPadLayout()
{
    const wxString& L = LAYOUT_PAD;

    auto singlePad =
            [this]() -> PAD*
            {
                BOARD_ITEM* item = singleItem();
                return item && item->Type() == PCB_PAD_T ? static_cast<PAD*>( item ) : nullptr;
            };

    buildNetInformation( L );

    // Properties
    PROPERTIES_FORM* props = addLayoutForm( _( "Properties" ), L );
    props->AddText( wxS( "Pad Number" ), _( "Designator" ) );

    // Altium's pad layer: the copper layer of a surface pad, Multi-Layer for a through hole
    props->AddInfo( _( "Layer" ),
            [this, singlePad]()
            {
                PAD* pad = singlePad();

                if( !pad )
                    return wxString();

                if( pad->GetAttribute() == PAD_ATTRIB::PTH || pad->GetAttribute() == PAD_ATTRIB::NPTH )
                    return _( "Multi-Layer" );

                PCB_LAYER_ID layer = pad->GetLayerSet().test( F_Cu ) ? F_Cu : B_Cu;
                return m_frame->GetBoard()->GetLayerName( layer );
            },
            true );

    OPTS net;
    net.linkLabel = true;
    net.pickerShown = []() { return true; };
    net.picker = [this]() { NET_EYEDROPPER::Get().Start( m_frame, selectedItems() ); };
    net.visible = [this]() { return isBoardEditor(); };
    props->AddChoice( wxS( "Net" ), _( "Net" ), net );
    props->AddText( wxS( "Pin Type" ), _( "Electrical Type" ) );
    props->AddText( wxS( "Pad To Die Delay" ), _( "Propagation Delay" ) );
    props->AddText( wxS( "Pad To Die Length" ), _( "Pin Package Length" ) );
    props->AddSeparator();
    props->AddXY( wxS( "Position X" ), wxS( "Position Y" ), wxEmptyString );
    props->AddText( wxS( "Orientation" ), _( "Rotation" ) );
    // Altium edits components and pads here; KiCad's dialog keeps the settings the panel
    // has no field for
    props->AddLink( []() { return _( "More Properties..." ); },
            [this]()
            {
                BOARD_ITEM* item = singleItem();

                if( !item )
                    return;

                KIID id = item->m_Uuid;

                CallAfter( [this, id]()
                        {
                            BOARD_ITEM* target = m_frame->GetBoard()->ResolveItem( id, true );

                            if( !target )
                                return;

                            m_frame->OnEditItemRequest( target );
                            m_frame->GetToolManager()->ProcessEvent( EVENTS::SelectedItemsModified );
                        } );
            } );

    // Pad Stack
    PROPERTIES_FORM* stack = addLayoutForm( _( "Pad Stack" ), L );

    stack->AddLink( [this]() { return m_showPreview ? _( "Hide preview" ) : _( "Show preview" ); },
                    [this]()
                    {
                        m_showPreview = !m_showPreview;
                        syncLayoutForms();
                    } );

    // The pad seen from above: its copper shape and hole
    OPTS preview;
    preview.visible = [this, singlePad]() { return m_showPreview && singlePad(); };
    stack->AddPaint( 110,
            [this, singlePad]( wxDC& aDC, const wxRect& aRect )
            {
                PAD* pad = singlePad();

                if( !pad )
                    return;

                PCB_LAYER_ID layer = pad->GetLayerSet().test( F_Cu ) ? F_Cu : B_Cu;
                VECTOR2I     size = pad->GetSize( layer );
                wxRect       area = aRect.Deflate( FromDIP( 16 ), FromDIP( 10 ) );
                double       scale = std::min( area.width / (double) std::max( 1, size.x ),
                                               area.height / (double) std::max( 1, size.y ) ) * 0.8;
                int          w = std::max( 2, (int) ( size.x * scale ) );
                int          h = std::max( 2, (int) ( size.y * scale ) );
                wxRect       shape( area.x + ( area.width - w ) / 2, area.y + ( area.height - h ) / 2, w, h );

                aDC.SetPen( *wxTRANSPARENT_PEN );
                aDC.SetBrush( wxBrush( copperColour( m_frame, layer ) ) );

                switch( pad->GetShape( layer ) )
                {
                case PAD_SHAPE::CIRCLE:
                    aDC.DrawEllipse( shape );
                    break;

                case PAD_SHAPE::OVAL:
                    aDC.DrawRoundedRectangle( shape, std::min( w, h ) / 2.0 );
                    break;

                case PAD_SHAPE::ROUNDRECT:
                case PAD_SHAPE::CHAMFERED_RECT:
                    aDC.DrawRoundedRectangle( shape, pad->GetRoundRectCornerRadius( layer ) * scale );
                    break;

                default:
                    aDC.DrawRectangle( shape );
                    break;
                }

                if( pad->HasHole() )
                {
                    VECTOR2I drill = pad->GetDrillSize();
                    int      dw = std::max( 2, (int) ( drill.x * scale ) );
                    int      dh = std::max( 2, (int) ( drill.y * scale ) );
                    wxRect   hole( shape.x + ( w - dw ) / 2, shape.y + ( h - dh ) / 2, dw, dh );

                    aDC.SetBrush( wxBrush( GetBackgroundColour() ) );
                    aDC.DrawRoundedRectangle( hole, std::min( dw, dh ) / 2.0 );
                }
            },
            preview );

    stack->AddCustomSegmented( { _( "Simple" ), _( "Top-Middle-Bottom" ), _( "Full Stack" ) },
            [singlePad]() -> int
            {
                PAD* pad = singlePad();
                return pad ? (int) pad->Padstack().Mode() : -1;
            },
            [this]( int aMode )
            {
                commitSelection( _( "Change Pad Stack" ),
                        [aMode]( BOARD_COMMIT& aCommit, BOARD_ITEM* aItem )
                        {
                            if( aItem->Type() != PCB_PAD_T )
                                return;

                            PAD* pad = static_cast<PAD*>( aItem );

                            if( (int) pad->Padstack().Mode() == aMode )
                                return;

                            aCommit.Modify( pad );
                            pad->Padstack().SetMode( static_cast<PADSTACK::MODE>( aMode ) );
                            pad->SetDirty();
                        } );
            } );

    // Altium's pad stack table: Copper, Hole, Paste and Solder sections
    auto setLayers =
            [this]( const wxString& aMessage, LSET aFront, LSET aBack, bool aOn )
            {
                commitSelection( aMessage,
                        [aFront, aBack, aOn]( BOARD_COMMIT& aCommit, BOARD_ITEM* aItem )
                        {
                            if( aItem->Type() != PCB_PAD_T )
                                return;

                            PAD* pad = static_cast<PAD*>( aItem );
                            LSET layers = pad->GetLayerSet();
                            LSET change;

                            if( layers.test( F_Cu ) )
                                change |= aFront;

                            if( layers.test( B_Cu ) )
                                change |= aBack;

                            LSET updated = aOn ? ( layers | change ) : ( layers & ~( aFront | aBack ) );

                            if( updated == layers )
                                return;

                            aCommit.Modify( pad );
                            pad->SetLayerSet( updated );
                        } );
            };

    stack->AddTable( { 5, 4, 4, 5, 4 },
            [this, singlePad, setLayers]()
            {
                PROPERTIES_FORM::TABLE_ROWS rows;
                PAD*                        pad = singlePad();

                if( !pad )
                    return rows;

                BOARD*          board = m_frame->GetBoard();
                const PADSTACK& padstack = pad->Padstack();
                bool            simple = padstack.Mode() == PADSTACK::MODE::NORMAL;
                bool            throughHole = pad->GetAttribute() == PAD_ATTRIB::PTH
                                              || pad->GetAttribute() == PAD_ATTRIB::NPTH;
                LSET            layers = pad->GetLayerSet();

                // Copper: per layer; the simple stack's values are editable
                rows.push_back( { heading( _( "COPPER" ) ), heading( _( "X-Size" ) ), heading( _( "Y-Size" ) ),
                                  heading( _( "Shape" ) ), heading( _( "Relief" ) ) } );

                if( simple )
                {
                    wxString name = throughHole ? _( "All Layers" )
                                                : board->GetLayerName( layers.test( F_Cu ) ? F_Cu : B_Cu );

                    rows.push_back( { fixed( name ), bound( wxS( "Size X" ) ), bound( wxS( "Size Y" ) ),
                                      bound( wxS( "Pad Shape" ) ), bound( wxS( "Zone Connection Style" ) ) } );
                }
                else
                {
                    wxString relief = pad->GetLocalZoneConnection() == ZONE_CONNECTION::INHERITED
                                              ? _( "From rules" )
                                              : wxString( _( "Override" ) );

                    for( PCB_LAYER_ID layer : padstack.UniqueLayers() )
                    {
                        wxString name = layer == PADSTACK::INNER_LAYERS
                                                && padstack.Mode() == PADSTACK::MODE::FRONT_INNER_BACK
                                                ? _( "Mid Layers" )
                                                : board->GetLayerName( layer );

                        rows.push_back( { fixed( name ),
                                          fixed( m_frame->MessageTextFromValue( pad->GetSize( layer ).x ) ),
                                          fixed( m_frame->MessageTextFromValue( pad->GetSize( layer ).y ) ),
                                          fixed( pad->ShowPadShape( layer ) ), fixed( relief ) } );
                    }
                }

                // Hole
                if( throughHole || pad->HasHole() )
                {
                    rows.push_back( { heading( _( "HOLE" ) ), heading( _( "Size" ) ), heading( _( "Length" ) ),
                                      heading( _( "Shape" ) ), heading( _( "Plated" ) ) } );

                    CELL plated( pad->GetAttribute() == PAD_ATTRIB::NPTH ? _( "No" ) : _( "Yes" ) );
                    plated.choices = { _( "Yes" ), _( "No" ) };
                    plated.choose =
                            [this]( int aChoice )
                            {
                                long attrib = aChoice == 0 ? (long) PAD_ATTRIB::PTH : (long) PAD_ATTRIB::NPTH;
                                applyPropertyValues( { { wxS( "Pad Type" ), wxVariant( attrib ) } } );
                            };

                    rows.push_back( { fixed( wxEmptyString ), bound( wxS( "Hole Size X" ) ),
                                      fixed( m_frame->MessageTextFromValue(
                                              board->GetDesignSettings().GetBoardThickness() ) ),
                                      bound( wxS( "Hole Shape" ) ), plated } );
                }

                // Paste
                bool paste = layers.test( F_Paste ) || layers.test( B_Paste );

                rows.push_back( { heading( _( "PASTE" ) ), heading( _( "Expansion" ) ), heading( wxS( "%" ) ),
                                  heading( _( "Shape" ) ), heading( _( "Enabled" ) ) } );

                CELL enabled( paste ? _( "Yes" ) : _( "No" ) );
                enabled.choices = { _( "Yes" ), _( "No" ) };
                enabled.choose =
                        [setLayers]( int aChoice )
                        {
                            setLayers( _( "Change Pad Paste" ), LSET( { F_Paste } ), LSET( { B_Paste } ),
                                       aChoice == 0 );
                        };

                rows.push_back( { fixed( wxEmptyString ), bound( wxS( "Solderpaste Margin Override" ) ),
                                  bound( wxS( "Solderpaste Margin Ratio Override" ) ),
                                  fixed( pad->ShowPadShape( PADSTACK::ALL_LAYERS ) ), enabled } );

                // Solder mask
                bool mask = layers.test( F_Mask ) || layers.test( B_Mask );

                rows.push_back( { heading( _( "SOLDER" ) ), heading( _( "Expansion" ) ), heading( _( "Shape" ) ),
                                  heading( _( "Tented" ) ) } );

                CELL tented( mask ? _( "No" ) : _( "Yes" ) );
                tented.choices = { _( "Yes" ), _( "No" ) };
                tented.choose =
                        [setLayers]( int aChoice )
                        {
                            // Tented: no solder mask opening
                            setLayers( _( "Change Pad Solder Mask" ), LSET( { F_Mask } ), LSET( { B_Mask } ),
                                       aChoice != 0 );
                        };

                rows.push_back( { fixed( wxEmptyString ), bound( wxS( "Soldermask Margin Override" ) ),
                                  fixed( pad->ShowPadShape( PADSTACK::ALL_LAYERS ) ), tented } );

                return rows;
            } );
}


// ----------------------------------------------------------------------------------- track / arc

void PCB_PROPERTIES_PANEL::buildTrackLayout()
{
    const wxString& L = LAYOUT_TRACK;

    buildNetInformation( L );

    PROPERTIES_FORM* props = addLayoutForm( _( "Properties" ), L );

    OPTS net;
    net.linkLabel = true;
    net.pickerShown = []() { return true; };
    net.picker = [this]() { NET_EYEDROPPER::Get().Start( m_frame, selectedItems() ); };
    net.visible = [this]() { return isBoardEditor(); };
    props->AddChoice( wxS( "Net" ), _( "Net" ), net );

    OPTS layer;
    layer.swatch = true;
    props->AddChoice( wxS( "Layer" ), _( "Layer" ), layer );
    props->AddSeparator();
    props->AddXY( wxS( "Start X" ), wxS( "Start Y" ), wxS( "Locked" ), _( "Start (X/Y)" ) );
    props->AddXY( wxS( "End X" ), wxS( "End Y" ), wxEmptyString, _( "End (X/Y)" ) );
    props->AddText( wxS( "Width" ), _( "Width" ) );
    props->AddInfo( _( "Length" ),
            [this]()
            {
                double length = 0.0;
                bool   any = false;

                for( BOARD_ITEM* item : selectedItems() )
                {
                    if( item->Type() == PCB_TRACE_T || item->Type() == PCB_ARC_T )
                    {
                        length += static_cast<PCB_TRACK*>( item )->GetLength();
                        any = true;
                    }
                }

                return any ? m_frame->MessageTextFromValue( length ) : wxString();
            },
            true );

    buildShapeSolderMask( L );
}


// ----------------------------------------------------------------------------- shared helpers

namespace
{
constexpr double PI_D = 3.14159265358979323846;


double normalizeDegrees( double aAngle )
{
    aAngle = std::fmod( aAngle, 360.0 );
    return aAngle < 0.0 ? aAngle + 360.0 : aAngle + 0.0;    // + 0.0: no "-0.000"
}


/// The angle of a point seen from a centre, as Altium shows it: degrees counter-clockwise
/// from +X with Y up (KiCad's Y points down)
double altiumAngle( const VECTOR2I& aCenter, const VECTOR2I& aPoint )
{
    double dx = (double) aPoint.x - aCenter.x;
    double dy = (double) aCenter.y - aPoint.y;
    double degrees = std::atan2( dy, dx ) * 180.0 / PI_D;

    // KiCad's arc centre is rounded to a nanometre: a quarter arc's end comes out as
    // 359.9994 degrees.  Within a few thousandths, show the whole degree.
    if( std::abs( degrees - std::round( degrees ) ) < 0.002 )
        degrees = std::round( degrees );

    return normalizeDegrees( degrees ) >= 359.9995 ? 0.0 : normalizeDegrees( degrees );
}


/// Altium's arc: a centre, a radius and the angles it runs between, counter-clockwise from
/// the start to the end.  A circle is an arc from 0 to 360 degrees.
struct ARC_GEOMETRY
{
    VECTOR2I center;
    int      radius = 0;
    double   start = 0.0;
    double   end = 360.0;
    bool     circle = false;
};


bool getArcGeometry( BOARD_ITEM* aItem, ARC_GEOMETRY& aArc )
{
    VECTOR2I start, mid, end;

    if( aItem->Type() == PCB_ARC_T )
    {
        PCB_ARC* arc = static_cast<PCB_ARC*>( aItem );
        aArc.center = arc->GetCenter();
        start = arc->GetStart();
        mid = arc->GetMid();
        end = arc->GetEnd();
    }
    else if( PCB_SHAPE* shape = dynamic_cast<PCB_SHAPE*>( aItem ) )
    {
        if( shape->GetShape() == SHAPE_T::CIRCLE )
        {
            aArc.center = shape->GetCenter();
            aArc.radius = shape->GetRadius();
            aArc.start = 0.0;
            aArc.end = 360.0;
            aArc.circle = true;
            return true;
        }

        if( shape->GetShape() != SHAPE_T::ARC )
            return false;

        aArc.center = shape->GetCenter();
        start = shape->GetStart();
        mid = shape->GetArcMid();
        end = shape->GetEnd();
    }
    else
    {
        return false;
    }

    aArc.circle = false;
    aArc.radius = KiROUND( ( start - aArc.center ).EuclideanNorm() );

    double s = altiumAngle( aArc.center, start );
    double m = altiumAngle( aArc.center, mid );
    double e = altiumAngle( aArc.center, end );

    // KiCad arcs run either way; Altium's run counter-clockwise
    if( normalizeDegrees( m - s ) <= normalizeDegrees( e - s ) )
    {
        aArc.start = s;
        aArc.end = e;
    }
    else
    {
        aArc.start = e;
        aArc.end = s;
    }

    return true;
}


void setArcGeometry( BOARD_ITEM* aItem, const ARC_GEOMETRY& aArc )
{
    auto pointAt =
            [&]( double aDegrees )
            {
                double rad = aDegrees * PI_D / 180.0;
                return aArc.center + VECTOR2I( KiROUND( aArc.radius * std::cos( rad ) ),
                                               KiROUND( -aArc.radius * std::sin( rad ) ) );
            };

    if( aArc.circle )
    {
        if( PCB_SHAPE* shape = dynamic_cast<PCB_SHAPE*>( aItem ) )
        {
            shape->SetStart( aArc.center );
            shape->SetEnd( aArc.center + VECTOR2I( aArc.radius, 0 ) );
        }

        return;
    }

    double sweep = normalizeDegrees( aArc.end - aArc.start );

    if( sweep < 0.001 )
        return;     // KiCad has no 360 degree arc (that is a circle)

    VECTOR2I start = pointAt( aArc.start );
    VECTOR2I mid = pointAt( aArc.start + sweep / 2.0 );
    VECTOR2I end = pointAt( aArc.end );

    if( aItem->Type() == PCB_ARC_T )
    {
        PCB_ARC* arc = static_cast<PCB_ARC*>( aItem );
        arc->SetStart( start );
        arc->SetMid( mid );
        arc->SetEnd( end );
    }
    else if( PCB_SHAPE* shape = dynamic_cast<PCB_SHAPE*>( aItem ) )
    {
        shape->SetArcGeometry( start, mid, end );
    }
}


bool isStrokeText( const EDA_TEXT* aText )
{
    return !aText->GetFont() || aText->GetFont()->IsStroke();
}
} // namespace


void PCB_PROPERTIES_PANEL::buildShapeSolderMask( const wxString& aLayout )
{
    // Altium's Solder Mask Expansion: from the rules, or a manual expansion.  KiCad tracks and
    // graphic shapes have one on outer copper layers only.
    PROPERTIES_FORM* mask = addLayoutForm( _( "Solder Mask Expansion" ), aLayout );

    auto available =
            [this]()
            {
                return m_grid->GetPropertyByName( wxS( "Soldermask Margin Override" ) ) != nullptr;
            };

    auto manual =
            [this]() -> int
            {
                int mode = -1;

                for( BOARD_ITEM* item : selectedItems() )
                {
                    std::optional<int> margin;

                    if( PCB_SHAPE* shape = dynamic_cast<PCB_SHAPE*>( item ) )
                        margin = shape->GetLocalSolderMaskMargin();
                    else if( PCB_TRACK* track = dynamic_cast<PCB_TRACK*>( item ) )
                        margin = track->GetLocalSolderMaskMargin();
                    else
                        continue;

                    int itemMode = margin.has_value() ? 1 : 0;

                    if( mode >= 0 && itemMode != mode )
                        return -1;

                    mode = itemMode;
                }

                return mode;
            };

    OPTS opts;
    opts.visible = available;

    mask->AddCustomSegmented( { _( "Rule" ), _( "Manual" ) }, manual,
            [this]( int aSegment )
            {
                wxVariant value;

                // Rule: no override; Manual starts from no expansion
                if( parsePropertyValue( wxS( "Soldermask Margin Override" ),
                                        aSegment == 0 ? wxString() : wxString( wxS( "0" ) ), value ) )
                {
                    applyPropertyValues( { { wxS( "Soldermask Margin Override" ), value } } );
                }
            },
            {}, wxEmptyString, opts );

    OPTS manualOnly;
    manualOnly.visible = [available, manual]() { return available() && manual() == 1; };
    mask->AddText( wxS( "Soldermask Margin Override" ), _( "Expansion" ), manualOnly );
    mask->AddCheck( wxS( "Soldermask" ), _( "Solder Mask Opening" ), true );
}


// ------------------------------------------------------------------------------------------ text

void PCB_PROPERTIES_PANEL::buildTextLayout()
{
    const wxString& L = LAYOUT_TEXT;

    auto texts =
            [this]()
            {
                std::vector<PCB_TEXT*> result;

                for( BOARD_ITEM* item : selectedItems() )
                {
                    if( item->Type() == PCB_TEXT_T || item->Type() == PCB_FIELD_T )
                        result.push_back( static_cast<PCB_TEXT*>( item ) );
                }

                return result;
            };

    // Texts and text boxes (Altium's String and Frame modes of a text)
    auto edaTexts =
            [this]()
            {
                std::vector<EDA_TEXT*> result;

                for( BOARD_ITEM* item : selectedItems() )
                {
                    if( item->Type() == PCB_TEXT_T || item->Type() == PCB_FIELD_T )
                        result.push_back( static_cast<PCB_TEXT*>( item ) );
                    else if( item->Type() == PCB_TEXTBOX_T )
                        result.push_back( static_cast<PCB_TEXTBOX*>( item ) );
                }

                return result;
            };

    // 0: all texts (String), 1: all text boxes (Frame), -1: mixed
    auto frameMode =
            [this]() -> int
            {
                int mode = -1;

                for( BOARD_ITEM* item : selectedItems() )
                {
                    int itemMode = item->Type() == PCB_TEXTBOX_T ? 1 : 0;

                    if( mode >= 0 && itemMode != mode )
                        return -1;

                    mode = itemMode;
                }

                return mode;
            };

    // A component's text (designator, comment, ...) is Altium's parameter
    auto allFields =
            [texts]()
            {
                std::vector<PCB_TEXT*> all = texts();

                for( PCB_TEXT* text : all )
                {
                    if( text->Type() != PCB_FIELD_T )
                        return false;
                }

                return !all.empty();
            };

    auto noFields =
            [texts]()
            {
                for( PCB_TEXT* text : texts() )
                {
                    if( text->Type() == PCB_FIELD_T )
                        return false;
                }

                return true;
            };

    // Location
    PROPERTIES_FORM* location = addLayoutForm( _( "Location" ), L );
    location->AddXY( wxS( "Position X" ), wxS( "Position Y" ), wxS( "Locked" ) );
    location->AddText( wxS( "Orientation" ), _( "Rotation" ) );

    // Properties
    PROPERTIES_FORM* props = addLayoutForm( _( "Properties" ), L );

    // String | Frame: a KiCad text or text box; switching converts between them.  A
    // component's own texts stay texts (KiCad's footprints keep them in the board editor).
    OPTS stringOrFrame;
    stringOrFrame.visible =
            [this, noFields]()
            {
                if( !noFields() )
                    return false;

                for( BOARD_ITEM* item : selectedItems() )
                {
                    if( isBoardEditor() && item->GetParentFootprint() )
                        return false;
                }

                return true;
            };

    props->AddCustomSegmented( { _( "String" ), _( "Frame" ) }, frameMode,
                               [this]( int aMode ) { convertTexts( aMode == 1 ); },
                               { true, true }, wxEmptyString, stringOrFrame );

    // Frame: the text box's size, margin and border
    OPTS frameOnly;
    frameOnly.visible = [frameMode]() { return frameMode() == 1; };

    auto boxes =
            [this]()
            {
                std::vector<PCB_TEXTBOX*> result;

                for( BOARD_ITEM* item : selectedItems() )
                {
                    if( item->Type() == PCB_TEXTBOX_T )
                        result.push_back( static_cast<PCB_TEXTBOX*>( item ) );
                }

                return result;
            };

    // The box's size before rotation (a rotated box is a polygon)
    auto boxSize =
            []( PCB_TEXTBOX* aBox ) -> VECTOR2I
            {
                std::vector<VECTOR2I> corners = aBox->GetCornersInSequence( aBox->GetDrawRotation() );

                if( corners.size() < 4 )
                    return VECTOR2I();

                return VECTOR2I( KiROUND( ( corners[1] - corners[0] ).EuclideanNorm() ),
                                 KiROUND( ( corners[3] - corners[0] ).EuclideanNorm() ) );
            };

    auto sizeText =
            [this, boxes, boxSize]( bool aWidth )
            {
                wxString result;
                bool     first = true;

                for( PCB_TEXTBOX* box : boxes() )
                {
                    VECTOR2I size = boxSize( box );
                    wxString text = m_frame->StringFromValue( aWidth ? size.x : size.y, true );

                    if( !first && text != result )
                        return wxString();

                    result = text;
                    first = false;
                }

                return result;
            };

    auto setSize =
            [this, boxSize]( bool aWidth, const wxString& aText )
            {
                int value = m_frame->ValueFromString( aText );

                if( value <= 0 )
                    return;

                commitSelection( _( "Change Text Frame Size" ),
                        [&]( BOARD_COMMIT& aCommit, BOARD_ITEM* aItem )
                        {
                            if( aItem->Type() != PCB_TEXTBOX_T )
                                return;

                            PCB_TEXTBOX* box = static_cast<PCB_TEXTBOX*>( aItem );
                            VECTOR2I     size = boxSize( box );
                            EDA_ANGLE    angle = box->GetTextAngle();

                            if( ( aWidth ? size.x : size.y ) == value )
                                return;

                            aCommit.Modify( box );

                            // Resize the unrotated box from its first corner, then turn it back
                            VECTOR2I origin = box->GetCornersInSequence( box->GetDrawRotation() )[0];

                            if( !angle.IsZero() )
                                box->Rotate( origin, -angle );

                            VECTOR2I start = box->GetStart();
                            VECTOR2I end = box->GetEnd();
                            int      sx = end.x >= start.x ? 1 : -1;
                            int      sy = end.y >= start.y ? 1 : -1;

                            if( aWidth )
                                box->SetEndX( start.x + sx * value );
                            else
                                box->SetEndY( start.y + sy * value );

                            if( !angle.IsZero() )
                                box->Rotate( origin, angle );
                        } );
            };

    props->AddCustomText( _( "Width" ), [sizeText]() { return sizeText( true ); },
                          [setSize]( const wxString& aText ) { setSize( true, aText ); }, frameOnly );
    props->AddCustomText( _( "Height" ), [sizeText]() { return sizeText( false ); },
                          [setSize]( const wxString& aText ) { setSize( false, aText ); }, frameOnly );

    // Altium's frame has one margin; a KiCad text box has one per side (this sets all four)
    props->AddCustomText( _( "Margin" ),
            [this, boxes]()
            {
                wxString result;
                bool     first = true;

                for( PCB_TEXTBOX* box : boxes() )
                {
                    int margin = box->GetMarginLeft();

                    if( box->GetMarginTop() != margin || box->GetMarginRight() != margin
                            || box->GetMarginBottom() != margin )
                    {
                        return wxString();
                    }

                    wxString text = m_frame->StringFromValue( margin, true );

                    if( !first && text != result )
                        return wxString();

                    result = text;
                    first = false;
                }

                return result;
            },
            [this]( const wxString& aText )
            {
                int value = m_frame->ValueFromString( aText );

                if( value < 0 )
                    return;

                commitSelection( _( "Change Text Frame Margin" ),
                        [value]( BOARD_COMMIT& aCommit, BOARD_ITEM* aItem )
                        {
                            if( aItem->Type() != PCB_TEXTBOX_T )
                                return;

                            PCB_TEXTBOX* box = static_cast<PCB_TEXTBOX*>( aItem );
                            aCommit.Modify( box );
                            box->SetMarginLeft( value );
                            box->SetMarginTop( value );
                            box->SetMarginRight( value );
                            box->SetMarginBottom( value );
                        } );
            },
            frameOnly );

    props->AddCheck( wxS( "Border" ), _( "Border" ), true, frameOnly );

    OPTS bordered;
    bordered.visible = [this, frameMode]()
                       {
                           return frameMode() == 1 && propBool( m_grid, wxS( "Border" ) ) == 1;
                       };
    props->AddText( wxS( "Border Width" ), _( "Border Width" ), bordered );
    props->AddSeparator( frameOnly );

    OPTS component;
    component.linkLabel = true;
    component.visible = allFields;
    props->AddInfo( _( "Component" ),
            [this]()
            {
                return commonText(
                        []( BOARD_ITEM* aItem, wxString& aText )
                        {
                            FOOTPRINT* fp = aItem->GetParentFootprint();
                            aText = fp ? fp->GetReference() : wxString();
                            return true;
                        } );
            },
            false, component );

    OPTS fieldOnly;
    fieldOnly.visible = allFields;
    props->AddText( wxS( "Name" ), _( "Name" ), fieldOnly );

    // A parameter's value, with Altium's eye: show or hide it on the board
    OPTS value;
    value.visible = allFields;
    value.eyeState = [this]() { return propBool( m_grid, wxS( "Visible" ) ) == 1; };
    value.eyeToggle =
            [this]()
            {
                bool shown = propBool( m_grid, wxS( "Visible" ) ) == 1;
                applyPropertyValues( { { wxS( "Visible" ), wxVariant( !shown ) } } );
            };
    props->AddText( wxS( "Text" ), _( "Value" ), value );

    OPTS text;
    text.visible = [allFields]() { return !allFields(); };
    props->AddText( wxS( "Text" ), _( "Text" ), text );

    OPTS layer;
    layer.swatch = true;
    props->AddChoice( wxS( "Layer" ), _( "Layer" ), layer );
    props->AddCheck( wxS( "Mirrored" ), _( "Mirror" ), true );
    props->AddText( wxS( "Height" ), _( "Text Height" ) );
    props->AddSeparator();

    // Font Type: KiCad's stroke font is Altium's Stroke; any other font is TrueType
    auto fontType =
            [edaTexts]() -> int
            {
                int type = -1;

                for( EDA_TEXT* text : edaTexts() )
                {
                    int textType = isStrokeText( text ) ? 1 : 0;

                    if( type >= 0 && textType != type )
                        return -1;

                    type = textType;
                }

                return type;
            };

    props->AddTitle( _( "Font Type" ) );
    props->AddCustomSegmented( { _( "TrueType" ), _( "Stroke" ), _( "BarCode" ) }, fontType,
            [this]( int aType )
            {
                if( aType != 0 && aType != 1 )
                    return;

                commitSelection( _( "Change Font" ),
                        [aType]( BOARD_COMMIT& aCommit, BOARD_ITEM* aItem )
                        {
                            EDA_TEXT* text = dynamic_cast<EDA_TEXT*>( aItem );

                            if( !text || isStrokeText( text ) == ( aType == 1 ) )
                                return;

                            aCommit.Modify( aItem );

                            // Altium's default TrueType font
                            text->SetFont( aType == 1 ? nullptr
                                                      : KIFONT::FONT::GetFont( wxS( "Arial" ), text->IsBold(),
                                                                               text->IsItalic() ) );
                        } );
            },
            { true, true, false } );

    // Justification: horizontal, then vertical
    props->AddSegmented( wxS( "Horizontal Justification" ), { _( "Left" ), _( "Center" ), _( "Right" ) } );
    props->AddSegmented( wxS( "Vertical Justification" ), { _( "Top" ), _( "Middle" ), _( "Bottom" ) } );

    OPTS trueType;
    trueType.visible = [fontType]() { return fontType() == 0; };
    props->AddChoice( wxS( "Font" ), _( "Font" ), trueType );

    // KiCad has one stroke font (Altium: Default, Sans Serif, Serif)
    OPTS stroke;
    stroke.visible = [fontType]() { return fontType() == 1; };
    props->AddInfo( _( "Font" ), []() { return _( "Default" ); }, true, stroke );
    props->AddText( wxS( "Thickness" ), _( "Stroke Width" ), stroke );

    props->AddCheck( wxS( "Bold" ), _( "Bold" ), true );
    props->AddCheck( wxS( "Italic" ), _( "Italic" ), true );
    props->AddCheck( wxS( "Knockout" ), _( "Inverted" ), true );
}


void PCB_PROPERTIES_PANEL::convertTexts( bool aFrame )
{
    // Altium's String | Frame is a mode of one text object; in KiCad they are a text and a
    // text box, so the mode change replaces each item (one undo step)
    std::vector<BOARD_ITEM*> sources;

    for( BOARD_ITEM* item : selectedItems() )
    {
        if( item->IsNew() || item->Type() != ( aFrame ? PCB_TEXT_T : PCB_TEXTBOX_T ) )
            continue;

        if( isBoardEditor() && item->GetParentFootprint() )
            continue;

        sources.push_back( item );
    }

    if( sources.empty() )
        return;

    BOARD_COMMIT commit( m_frame );
    EDA_ITEMS    created;

    for( BOARD_ITEM* item : sources )
    {
        BOARD_ITEM* parent = static_cast<BOARD_ITEM*>( item->GetParent() );
        BOARD_ITEM* replacement = nullptr;

        if( aFrame )
        {
            PCB_TEXT*    text = static_cast<PCB_TEXT*>( item );
            PCB_TEXTBOX* box = new PCB_TEXTBOX( parent );

            // Text, font, size, thickness, justification, ...: as the text, upright for now
            TEXT_ATTRIBUTES attrs = text->GetAttributes();
            attrs.m_Angle = ANGLE_0;
            attrs.m_Multiline = true;
            box->SetAttributes( attrs );
            box->SetText( text->GetText() );

            // The frame fits the text (as drawn upright at its position) plus the margins;
            // Altium's frame has no border until one is asked for
            int   margin = box->GetLegacyTextMargin();
            BOX2I textBox = text->GetTextBox( nullptr );

            box->SetMarginLeft( margin );
            box->SetMarginTop( margin );
            box->SetMarginRight( margin );
            box->SetMarginBottom( margin );
            box->SetBorderEnabled( false );
            box->SetStart( textBox.GetOrigin() - VECTOR2I( margin, margin ) );
            box->SetEnd( textBox.GetEnd() + VECTOR2I( margin, margin ) );

            // Then turned as the text was, about the text's position
            if( !text->GetTextAngle().IsZero() )
                box->Rotate( text->GetTextPos(), text->GetTextAngle() );

            replacement = box;
        }
        else
        {
            PCB_TEXTBOX* box = static_cast<PCB_TEXTBOX*>( item );
            PCB_TEXT*    text = nullptr;

            if( FOOTPRINT* fp = dynamic_cast<FOOTPRINT*>( parent ) )
                text = new PCB_TEXT( fp );
            else
                text = new PCB_TEXT( parent );

            // At the frame's text anchor: the text stays where it was drawn
            TEXT_ATTRIBUTES attrs = box->GetAttributes();
            text->SetAttributes( attrs );
            text->SetText( box->GetText() );
            text->SetTextPos( box->GetDrawPos() );

            replacement = text;
        }

        replacement->SetLayer( item->GetLayer() );
        replacement->SetLocked( item->IsLocked() );
        replacement->SetIsKnockout( item->IsKnockout() );

        if( EDA_GROUP* group = item->GetParentGroup() )
        {
            commit.Modify( group->AsEdaItem(), nullptr, RECURSE_MODE::NO_RECURSE );
            group->AddItem( replacement );
        }

        commit.Remove( item );
        commit.Add( replacement );
        created.push_back( replacement );
    }

    TOOL_MANAGER* toolMgr = m_frame->GetToolManager();

    toolMgr->RunAction( ACTIONS::selectionClear );
    commit.Push( aFrame ? _( "Convert Text to Text Frame" ) : _( "Convert Text Frame to Text" ) );
    toolMgr->RunAction<EDA_ITEMS*>( ACTIONS::selectItems, &created );

    m_frame->Refresh();
    AfterCommit();
}


// ----------------------------------------------------------------------- interactive routing

void PCB_PROPERTIES_PANEL::buildRoutingLayout()
{
    // Altium's Interactive Routing page (Tab while routing).  Edits apply to the track being
    // routed at once.
    const wxString& L = LAYOUT_ROUTING;

    auto refresh =
            [this]()
            {
                // Re-route the track in progress with the new settings
                if( ROUTER_TOOL* router = activeRouter() )
                    router->RefreshRoute();

                m_frame->UpdateProperties();
            };

    auto sizeText =
            [this]( int aWhich ) -> wxString
            {
                ROUTER_TOOL* router = activeRouter();

                if( !router )
                    return wxString();

                const PNS::SIZES_SETTINGS& sizes = router->Router()->Sizes();
                int value = aWhich == 0 ? sizes.TrackWidth()
                                        : aWhich == 1 ? sizes.ViaDrill() : sizes.ViaDiameter();

                return m_frame->StringFromValue( value, true );
            };

    auto setSizes =
            [this]( std::optional<int> aWidth, std::optional<int> aDrill, std::optional<int> aDiameter )
            {
                ROUTER_TOOL* router = activeRouter();

                if( !router )
                    return;

                const PNS::SIZES_SETTINGS& sizes = router->Router()->Sizes();
                int width = aWidth.value_or( sizes.TrackWidth() );
                int drill = aDrill.value_or( sizes.ViaDrill() );
                int diameter = aDiameter.value_or( sizes.ViaDiameter() );

                if( width <= 0 || drill <= 0 || diameter <= 0 )
                {
                    m_frame->ShowInfoBarError( _( "Sizes must be greater than 0." ) );
                    return;
                }

                if( drill >= diameter )
                {
                    m_frame->ShowInfoBarError( _( "The via hole must be smaller than the via diameter." ) );
                    return;
                }

                router->SetRouteSizes( width, diameter, drill );
                m_frame->UpdateProperties();
            };

    // Net Information: the net being routed, as Altium shows it while routing (plain text, a
    // Name | Class header, then Length and Delay)
    auto routedNet =
            [this]() -> NETINFO_ITEM*
            {
                ROUTER_TOOL* router = activeRouter();

                if( !router || router->Router()->GetCurrentNets().empty() )
                    return nullptr;

                return static_cast<NETINFO_ITEM*>( router->Router()->GetCurrentNets()[0] );
            };

    PROPERTIES_FORM* netInfo = addLayoutForm( _( "Net Information" ), L );

    OPTS plain;
    plain.plainTable = true;

    netInfo->AddTable( { 3, 5, 5 },
            [routedNet]()
            {
                PROPERTIES_FORM::TABLE_ROWS rows;
                rows.push_back( { heading( wxEmptyString ), heading( _( "Name" ) ), heading( _( "Class" ) ) } );

                NETINFO_ITEM* net = routedNet();
                CELL          name( _( "No Net" ) );
                CELL          netClass( _( "N/A" ) );

                name.readOnly = netClass.readOnly = true;

                if( net && net->GetNetCode() > 0 )
                {
                    name.text = UnescapeString( net->GetNetname() );
                    name.link = true;

                    if( NETCLASS* nc = net->GetNetClass() )
                    {
                        netClass.text = nc->GetHumanReadableName();
                        netClass.link = true;
                    }
                }

                rows.push_back( { fixed( _( "Net" ) ), name, netClass } );
                return rows;
            },
            nullptr, plain );

    netInfo->AddSeparator();

    // Routed length so far: the net's placed tracks plus the route in progress
    netInfo->AddTable( { 3, 10 },
            [this, routedNet]()
            {
                PROPERTIES_FORM::TABLE_ROWS rows;
                NETINFO_ITEM*               net = routedNet();
                ROUTER_TOOL*                router = activeRouter();
                double                      length = router ? router->RoutedNetLength() : 0.0;

                CELL lengthLabel( _( "Length" ) );
                CELL delayLabel( _( "Delay" ) );
                lengthLabel.link = delayLabel.link = true;
                lengthLabel.readOnly = delayLabel.readOnly = true;

                rows.push_back( { lengthLabel, fixed( net ? m_frame->MessageTextFromValue( length )
                                                          : wxString() ) } );

                // KiCad has no propagation delay for a route in progress
                rows.push_back( { delayLabel, fixed( _( "N/A" ) ) } );
                return rows;
            },
            nullptr, plain );

    // Properties: the layer (with its colour), then the via preview carrying the via and track
    // sizes, as Altium draws them
    PROPERTIES_FORM* props = addLayoutForm( _( "Properties" ), L );

    auto copperLayers =
            [this]()
            {
                std::vector<PCB_LAYER_ID> layers;

                for( PCB_LAYER_ID layer : m_frame->GetBoard()->GetEnabledLayers().CuStack() )
                    layers.push_back( layer );

                return layers;
            };

    auto layerLabel =
            [this]( PCB_LAYER_ID aLayer )
            {
                return ALTIUM_LAYER_TABS::DisplayName( aLayer, m_frame->GetBoard()->GetLayerName( aLayer ) );
            };

    OPTS layerOpts;
    layerOpts.swatch = true;
    layerOpts.choices =
            [copperLayers, layerLabel]()
            {
                std::vector<wxString> labels;

                for( PCB_LAYER_ID layer : copperLayers() )
                    labels.push_back( layerLabel( layer ) );

                return labels;
            };
    layerOpts.swatchColour =
            [this]()
            {
                return m_frame->GetColorSettings()->GetColor( m_frame->GetActiveLayer() ).ToColour();
            };

    props->AddCustomChoice( _( "Layer" ), {},
            [this, copperLayers]() -> int
            {
                std::vector<PCB_LAYER_ID> layers = copperLayers();
                auto it = std::find( layers.begin(), layers.end(), m_frame->GetActiveLayer() );
                return it == layers.end() ? -1 : (int) ( it - layers.begin() );
            },
            [this, copperLayers]( int aIndex )
            {
                std::vector<PCB_LAYER_ID> layers = copperLayers();

                // Through the router's layer command: the route continues on that layer, with a
                // via where it needs one (Altium's layer change while routing)
                if( aIndex >= 0 && aIndex < (int) layers.size() )
                    m_frame->GetToolManager()->RunAction( PCB_ACTIONS::layerTop, layers[aIndex] );

                m_frame->UpdateProperties();
            },
            layerOpts );

    props->AddLink( [this]() { return m_showPreview ? _( "Hide preview" ) : _( "Show preview" ); },
                    [this]()
                    {
                        m_showPreview = !m_showPreview;
                        syncLayoutForms();
                    } );

    // The via preview: a via on the end of a track, with its diameter, hole and the track width
    // marked (our own drawing of what the rows below set)
    OPTS previewOnly;
    previewOnly.visible = [this]() { return m_showPreview; };

    props->AddPaint( 110,
            [this]( wxDC& aDC, const wxRect& aRect )
            {
                wxColour copper = copperColour( m_frame );
                wxColour fg = panelText();
                int      r = std::min( aRect.height / 3, FromDIP( 26 ) );
                wxPoint  centre( aRect.x + aRect.width / 3, aRect.y + r + FromDIP( 8 ) );
                int      hole = r / 2;
                int      trackW = std::max( FromDIP( 6 ), r / 3 );

                aDC.SetPen( *wxTRANSPARENT_PEN );

                // The track leaving the via downwards
                aDC.SetBrush( wxBrush( copper ) );
                aDC.DrawRectangle( centre.x - trackW / 2, centre.y, trackW,
                                   aRect.GetBottom() - centre.y - FromDIP( 4 ) );

                // The via ring and its hole
                aDC.DrawCircle( centre, r );
                aDC.SetBrush( wxBrush( GetBackgroundColour() ) );
                aDC.DrawCircle( centre, hole );

                // Dimension marks: the diameter (across the ring), the hole, the track width
                aDC.SetPen( wxPen( fg ) );
                aDC.DrawLine( centre.x - r - FromDIP( 10 ), centre.y - r - FromDIP( 4 ),
                              centre.x + r + FromDIP( 10 ), centre.y + r + FromDIP( 4 ) );
                aDC.DrawLine( centre.x - hole, centre.y, centre.x + hole, centre.y );

                int ty = aRect.GetBottom() - FromDIP( 14 );
                aDC.DrawLine( centre.x - trackW / 2 - FromDIP( 10 ), ty, centre.x - trackW / 2, ty );
                aDC.DrawLine( centre.x + trackW / 2, ty, centre.x + trackW / 2 + FromDIP( 10 ), ty );

                // Captions pointing at the marks
                aDC.SetTextForeground( fg );
                aDC.SetFont( GetFont().Smaller() );
                int tx = centre.x + r + FromDIP( 18 );
                aDC.DrawText( _( "Diameter" ), tx, centre.y - r );
                aDC.DrawText( _( "Hole" ), tx, centre.y - aDC.GetCharHeight() / 2 );
                aDC.DrawText( _( "Width" ), tx, ty - aDC.GetCharHeight() / 2 );
                aDC.SetFont( GetFont() );
            },
            previewOnly );

    props->AddCustomText( _( "Via Diameter" ), [sizeText]() { return sizeText( 2 ); },
            [this, setSizes]( const wxString& aText )
            {
                setSizes( std::nullopt, std::nullopt, m_frame->ValueFromString( aText ) );
            } );

    props->AddCustomText( _( "Via Hole Size" ), [sizeText]() { return sizeText( 1 ); },
            [this, setSizes]( const wxString& aText )
            {
                setSizes( std::nullopt, m_frame->ValueFromString( aText ), std::nullopt );
            } );

    // Width: Altium's drop-down of the width rule's sizes (minimum, preferred, maximum, then the
    // board's preset widths); a typed width (Shift+W / Q) shows as its own first entry
    auto widthChoices =
            [this]()
            {
                std::vector<ROUTER_TOOL::SIZE_CHOICE> choices;

                if( ROUTER_TOOL* router = activeRouter() )
                {
                    choices = router->RouteWidthChoices( true, true );
                    int width = router->Router()->Sizes().TrackWidth();

                    if( std::none_of( choices.begin(), choices.end(),
                                      [&]( const ROUTER_TOOL::SIZE_CHOICE& aChoice )
                                      {
                                          return aChoice.size == width;
                                      } ) )
                    {
                        ROUTER_TOOL::SIZE_CHOICE custom;
                        custom.size = width;
                        custom.label = m_frame->StringFromValue( width, true );
                        choices.insert( choices.begin(), custom );
                    }
                }

                return choices;
            };

    OPTS widthOpts;
    widthOpts.choices =
            [widthChoices]()
            {
                std::vector<wxString> labels;

                for( const ROUTER_TOOL::SIZE_CHOICE& choice : widthChoices() )
                    labels.push_back( choice.label );

                return labels;
            };

    props->AddCustomChoice( _( "Width" ), {},
            [this, widthChoices]() -> int
            {
                ROUTER_TOOL* router = activeRouter();

                if( !router )
                    return -1;

                std::vector<ROUTER_TOOL::SIZE_CHOICE> choices = widthChoices();
                int width = router->Router()->Sizes().TrackWidth();

                for( size_t ii = 0; ii < choices.size(); ++ii )
                {
                    if( choices[ii].size == width )
                        return (int) ii;
                }

                return -1;
            },
            [widthChoices, setSizes]( int aIndex )
            {
                std::vector<ROUTER_TOOL::SIZE_CHOICE> choices = widthChoices();

                if( aIndex >= 0 && aIndex < (int) choices.size() )
                    setSizes( choices[aIndex].size, std::nullopt, std::nullopt );
            },
            widthOpts );

    // Interactive Routing Options: the router's conflict mode, corner style and clean-ups, in
    // Altium's order.  Options KiCad's router doesn't have are shown greyed.
    PROPERTIES_FORM* options = addLayoutForm( _( "Interactive Routing Options" ), L );

    auto settings =
            [this]() -> PNS::ROUTING_SETTINGS*
            {
                ROUTER_TOOL* router = activeRouter();
                return router ? &router->Router()->Settings() : nullptr;
            };

    // A shortcut as Altium prints it ("Shift + R"), from the action's current hotkey
    auto hint =
            [this]( const char* aActionName ) -> wxString
            {
                ACTION_MANAGER* mgr = m_frame->GetToolManager()->GetActionManager();
                TOOL_ACTION*    action = mgr ? mgr->FindAction( aActionName ) : nullptr;
                int             key = action ? mgr->GetHotKey( *action ) : 0;

                if( !key )
                    return wxString();

                wxString text = KeyNameFromKeyCode( key );
                text.Replace( wxS( "+" ), wxS( " + " ) );
                return text;
            };

    OPTS greyed;
    greyed.readOnly = true;

    // Altium's Conflict Resolution names for KiCad's three conflict modes: KiCad's shove mode
    // pushes and hugs what it can't push, as Altium's HugNPush does
    static const PNS::PNS_MODE modes[] = { PNS::RM_MarkObstacles, PNS::RM_Walkaround, PNS::RM_Shove };

    OPTS modeHint;
    modeHint.hint = hint( "pcbnew.InteractiveRouter.CycleRouterMode" );

    if( !modeHint.hint.IsEmpty() )
        options->AddTitle( wxEmptyString, modeHint );

    options->AddCustomChoice( _( "Conflict Resolution" ),
            { _( "Ignore Obstacles" ), _( "Walkaround Obstacles" ), _( "HugNPush Obstacles" ) },
            [settings]() -> int
            {
                PNS::ROUTING_SETTINGS* cfg = settings();

                for( int ii = 0; cfg && ii < 3; ++ii )
                {
                    if( cfg->Mode() == modes[ii] )
                        return ii;
                }

                return -1;
            },
            [settings, refresh]( int aIndex )
            {
                PNS::ROUTING_SETTINGS* cfg = settings();

                if( !cfg || aIndex < 0 || aIndex > 2 )
                    return;

                cfg->SetMode( modes[aIndex] );

                // The router draws any-angle lines only when ignoring obstacles
                if( modes[aIndex] != PNS::RM_MarkObstacles )
                    cfg->SetFreeAngleMode( false );

                refresh();
            } );

    // Corner style: Altium's five buttons (90°, 90° arc, 45°, 45° arc, any angle), drawn as
    // icons.  Any angle only exists when ignoring obstacles.
    static const DIRECTION_45::CORNER_MODE corners[] = { DIRECTION_45::CORNER_MODE::MITERED_90,
                                                         DIRECTION_45::CORNER_MODE::ROUNDED_90,
                                                         DIRECTION_45::CORNER_MODE::MITERED_45,
                                                         DIRECTION_45::CORNER_MODE::ROUNDED_45 };

    OPTS cornerTitle;
    cornerTitle.hint = hint( "pcbnew.InteractiveRouter.SwitchRoundingToNext" );
    options->AddTitle( _( "Corner Style" ), cornerTitle );

    OPTS cornerIcons;
    cornerIcons.segmentPaint =
            [this]( wxDC& aDC, const wxRect& aSeg, int aIndex, const wxColour& aColour )
            {
                // A track coming in from the left and turning up, in each corner style
                int     s = std::max( FromDIP( 6 ), std::min( aSeg.width, aSeg.height ) - FromDIP( 8 ) );
                wxPoint o( aSeg.x + ( aSeg.width - s ) / 2, aSeg.y + ( aSeg.height - s ) / 2 );
                wxPoint start( o.x, o.y + s );          // bottom left
                wxPoint end( o.x + s, o.y );            // top right
                wxPoint corner( o.x + s, o.y + s );     // the square corner
                int     c = s / 2;                      // chamfer / arc size

                aDC.SetPen( wxPen( aColour, std::max( 1, FromDIP( 2 ) ) ) );
                aDC.SetBrush( *wxTRANSPARENT_BRUSH );

                switch( aIndex )
                {
                case 0:     // 90°
                    aDC.DrawLine( start, corner );
                    aDC.DrawLine( corner, end );
                    break;

                case 1:     // 90° with an arc in the corner
                    aDC.DrawLine( start, wxPoint( corner.x - c, corner.y ) );
                    aDC.DrawArc( wxPoint( corner.x, corner.y - c ), wxPoint( corner.x - c, corner.y ),
                                 wxPoint( corner.x - c, corner.y - c ) );
                    aDC.DrawLine( wxPoint( corner.x, corner.y - c ), end );
                    break;

                case 2:     // 45°: a chamfer
                    aDC.DrawLine( start, wxPoint( corner.x - c, corner.y ) );
                    aDC.DrawLine( wxPoint( corner.x - c, corner.y ), wxPoint( corner.x, corner.y - c ) );
                    aDC.DrawLine( wxPoint( corner.x, corner.y - c ), end );
                    break;

                case 3:     // 45° with arcs: a wide, gentle curve
                    aDC.DrawLine( start, wxPoint( o.x + s / 4, corner.y ) );
                    aDC.DrawArc( wxPoint( corner.x, o.y + s / 4 ), wxPoint( o.x + s / 4, corner.y ),
                                 wxPoint( o.x + s / 4, o.y + s / 4 ) );
                    break;

                default:    // any angle: one straight line
                    aDC.DrawLine( start, end );
                    break;
                }
            };

    options->AddCustomSegmented( { _( "90°" ), _( "90° Arc" ), _( "45°" ), _( "45° Arc" ), _( "Any Angle" ) },
            [settings]() -> int
            {
                PNS::ROUTING_SETTINGS* cfg = settings();

                if( !cfg )
                    return -1;

                if( cfg->GetFreeAngleMode() )
                    return 4;

                for( int ii = 0; ii < 4; ++ii )
                {
                    if( cfg->GetCornerMode() == corners[ii] )
                        return ii;
                }

                return -1;
            },
            [settings, refresh]( int aIndex )
            {
                PNS::ROUTING_SETTINGS* cfg = settings();

                if( !cfg || aIndex < 0 || aIndex > 4 )
                    return;

                if( aIndex == 4 )
                {
                    if( cfg->Mode() != PNS::RM_MarkObstacles )
                        return;

                    cfg->SetFreeAngleMode( true );
                }
                else
                {
                    cfg->SetFreeAngleMode( false );
                    cfg->SetCornerMode( corners[aIndex] );
                }

                refresh();
            },
            {}, wxEmptyString, cornerIcons );

    // Gloss Effort (Routed): KiCad's optimizer effort.  KiCad doesn't gloss neighbouring
    // tracks separately, so (Neighbor) is shown greyed.
    static const PNS::PNS_OPTIMIZATION_EFFORT efforts[] = { PNS::OE_LOW, PNS::OE_MEDIUM,
                                                            PNS::OE_FULL };

    options->AddTitle( _( "Gloss Effort (Routed)" ) );
    options->AddCustomSegmented( { _( "Off" ), _( "Weak" ), _( "Strong" ) },
            [settings]() -> int
            {
                PNS::ROUTING_SETTINGS* cfg = settings();

                for( int ii = 0; cfg && ii < 3; ++ii )
                {
                    if( cfg->OptimizerEffort() == efforts[ii] )
                        return ii;
                }

                return -1;
            },
            [settings, refresh]( int aIndex )
            {
                PNS::ROUTING_SETTINGS* cfg = settings();

                if( !cfg || aIndex < 0 || aIndex > 2 )
                    return;

                cfg->SetOptimizerEffort( efforts[aIndex] );
                refresh();
            } );

    options->AddTitle( _( "Gloss Effort (Neighbor)" ) );
    options->AddCustomSegmented( { _( "Off" ), _( "Weak" ), _( "Strong" ) },
            []() { return -1; }, []( int ) {}, { false, false, false } );

    options->AddSeparator();

    auto option =
            [&]( const wxString& aLabel, bool ( PNS::ROUTING_SETTINGS::*aGet )() const,
                 void ( PNS::ROUTING_SETTINGS::*aSet )( bool ) )
            {
                options->AddCustomCheck( aLabel,
                        [settings, aGet]() -> int
                        {
                            PNS::ROUTING_SETTINGS* cfg = settings();
                            return cfg ? ( ( cfg->*aGet )() ? 1 : 0 ) : -1;
                        },
                        [settings, aSet, refresh]( bool aOn )
                        {
                            if( PNS::ROUTING_SETTINGS* cfg = settings() )
                            {
                                ( cfg->*aSet )( aOn );
                                refresh();
                            }
                        } );
            };

    // An Altium option with no KiCad equivalent: greyed, unticked
    auto unsupported =
            [&]( const wxString& aLabel )
            {
                options->AddCustomCheck( aLabel, []() { return 0; }, []( bool ) {}, true, greyed );
            };

    options->AddCustomCheck( _( "Automatically Terminate Routing" ),
            [settings]() -> int
            {
                PNS::ROUTING_SETTINGS* cfg = settings();
                return cfg ? ( cfg->SuggestFinish() ? 1 : 0 ) : -1;
            },
            [settings, refresh]( bool aOn )
            {
                if( PNS::ROUTING_SETTINGS* cfg = settings() )
                {
                    cfg->SetSuggestFinish( aOn );
                    refresh();
                }
            } );

    option( _( "Automatically Remove Loops" ), &PNS::ROUTING_SETTINGS::RemoveLoops,
            &PNS::ROUTING_SETTINGS::SetRemoveLoops );
    unsupported( _( "Remove Loops With Vias" ) );
    unsupported( _( "Remove Net Antennas" ) );
    option( _( "Allow Via Pushing" ), &PNS::ROUTING_SETTINGS::ShoveVias,
            &PNS::ROUTING_SETTINGS::SetShoveVias );
    unsupported( _( "Pin Swapping" ) );
    unsupported( _( "Auto Shrinking" ) );

    options->AddSeparator();

    // Display Clearance Boundaries: KiCad's clearance outlines while routing
    options->AddCustomCheck( _( "Display Clearance Boundaries" ),
            [this]() -> int
            {
                PCBNEW_SETTINGS* cfg = m_frame->GetPcbNewSettings();
                return cfg ? ( cfg->m_Display.m_TrackClearance != DO_NOT_SHOW_CLEARANCE ? 1 : 0 ) : -1;
            },
            [this, refresh]( bool aOn )
            {
                if( PCBNEW_SETTINGS* cfg = m_frame->GetPcbNewSettings() )
                {
                    cfg->m_Display.m_TrackClearance = aOn ? SHOW_WITH_VIA_WHILE_ROUTING
                                                          : DO_NOT_SHOW_CLEARANCE;
                    refresh();
                }
            } );

    unsupported( _( "Reduce Clearance Display Area" ) );
    unsupported( _( "Show Length Gauge" ) );

    // Pad Entry Stability (a slider from Off to Max), Miter Ratio and Subnet Jumper Len: Altium
    // settings KiCad's router doesn't have, shown greyed
    options->AddTitle( _( "Pad Entry Stability" ) );
    options->AddPaint( 30,
            [this]( wxDC& aDC, const wxRect& aRect )
            {
                wxColour grey = PROPERTIES_FORM::Mix( panelText(), GetBackgroundColour(), 0.55 );
                int      y = aRect.y + FromDIP( 8 );
                int      x0 = aRect.x + FromDIP( 8 );
                int      x1 = aRect.GetRight() - FromDIP( 8 );

                aDC.SetPen( wxPen( grey ) );
                aDC.DrawLine( x0, y, x1, y );
                aDC.SetBrush( wxBrush( grey ) );
                aDC.DrawRectangle( x0 + ( x1 - x0 ) / 2 - FromDIP( 2 ), y - FromDIP( 5 ), FromDIP( 4 ),
                                   FromDIP( 10 ) );

                aDC.SetTextForeground( grey );
                aDC.SetFont( GetFont().Smaller() );
                aDC.DrawText( _( "Off" ), x0, y + FromDIP( 7 ) );
                wxString max = _( "Max" );
                aDC.DrawText( max, x1 - aDC.GetTextExtent( max ).x, y + FromDIP( 7 ) );
                aDC.SetFont( GetFont() );
            } );

    options->AddInfo( _( "Miter Ratio" ), []() { return wxString( wxS( "1x" ) ); }, true, greyed );
    options->AddInfo( _( "Subnet Jumper Len" ), []() { return wxString(); }, true, greyed );
}


// ------------------------------------------------------------------------------------------- arc

void PCB_PROPERTIES_PANEL::buildArcLayout()
{
    const wxString& L = LAYOUT_ARC;

    auto arcText =
            [this]( const std::function<wxString( const ARC_GEOMETRY& )>& aText )
            {
                return commonText(
                        [&]( BOARD_ITEM* aItem, wxString& aResult )
                        {
                            ARC_GEOMETRY arc;

                            if( !getArcGeometry( aItem, arc ) )
                                return false;

                            aResult = aText( arc );
                            return true;
                        } );
            };

    auto editArcs =
            [this]( const wxString& aMessage, const std::function<bool( ARC_GEOMETRY& )>& aChange )
            {
                commitSelection( aMessage,
                        [&]( BOARD_COMMIT& aCommit, BOARD_ITEM* aItem )
                        {
                            ARC_GEOMETRY arc;

                            if( !getArcGeometry( aItem, arc ) || !aChange( arc ) )
                                return;

                            aCommit.Modify( aItem );
                            setArcGeometry( aItem, arc );
                        } );
            };

    auto noCircles =
            [this]()
            {
                for( BOARD_ITEM* item : selectedItems() )
                {
                    ARC_GEOMETRY arc;

                    if( getArcGeometry( item, arc ) && arc.circle )
                        return false;
                }

                return true;
            };

    auto coordinate =
            [this]( int aValue, ORIGIN_TRANSFORMS::COORD_TYPES_T aType )
            {
                return m_frame->StringFromValue( m_frame->GetOriginTransforms().ToDisplay( aValue, aType ), true );
            };

    auto degrees = []( double aAngle ) { return wxString::Format( wxS( "%.3f" ), aAngle ); };

    buildNetInformation( L );

    // Location: the centre
    PROPERTIES_FORM* location = addLayoutForm( _( "Location" ), L );

    for( bool isX : { true, false } )
    {
        ORIGIN_TRANSFORMS::COORD_TYPES_T type = isX ? ORIGIN_TRANSFORMS::ABS_X_COORD
                                                    : ORIGIN_TRANSFORMS::ABS_Y_COORD;

        location->AddCustomText( isX ? _( "Center X" ) : _( "Center Y" ),
                [arcText, coordinate, isX, type]()
                {
                    return arcText( [&]( const ARC_GEOMETRY& aArc )
                                    {
                                        return coordinate( isX ? aArc.center.x : aArc.center.y, type );
                                    } );
                },
                [this, editArcs, isX, type]( const wxString& aText )
                {
                    int value = m_frame->GetOriginTransforms().FromDisplay( m_frame->ValueFromString( aText ),
                                                                             type );

                    editArcs( _( "Move Arc" ),
                              [&]( ARC_GEOMETRY& aArc )
                              {
                                  ( isX ? aArc.center.x : aArc.center.y ) = value;
                                  return true;
                              } );
                } );
    }

    location->AddCheck( wxS( "Locked" ), _( "Locked" ), true );

    // Properties
    PROPERTIES_FORM* props = addLayoutForm( _( "Properties" ), L );

    OPTS net;
    net.linkLabel = true;
    net.pickerShown = []() { return true; };
    net.picker = [this]() { NET_EYEDROPPER::Get().Start( m_frame, selectedItems() ); };
    net.visible = [this]() { return isBoardEditor(); };
    props->AddChoice( wxS( "Net" ), _( "Net" ), net );

    OPTS layer;
    layer.swatch = true;
    props->AddChoice( wxS( "Layer" ), _( "Layer" ), layer );
    props->AddSeparator();

    // A graphic arc's width is its line width; a routed arc's its track width
    props->AddText( wxS( "Line Width" ), _( "Width" ) );
    props->AddText( wxS( "Width" ), _( "Width" ) );

    auto radiusGet =
            [this, arcText]()
            {
                return arcText( [this]( const ARC_GEOMETRY& aArc )
                                {
                                    return m_frame->StringFromValue( aArc.radius, true );
                                } );
            };

    auto radiusSet =
            [this, editArcs]( const wxString& aText )
            {
                int radius = m_frame->ValueFromString( aText );

                if( radius <= 0 )
                    return;

                editArcs( _( "Change Arc Radius" ),
                          [&]( ARC_GEOMETRY& aArc )
                          {
                              aArc.radius = radius;
                              return true;
                          } );
            };

    OPTS angles;
    angles.visible = noCircles;

    // Altium's order: Start Angle, Radius, End Angle
    for( bool isStart : { true, false } )
    {
        if( !isStart )
            props->AddCustomText( _( "Radius" ), radiusGet, radiusSet );

        props->AddCustomText( isStart ? _( "Start Angle" ) : _( "End Angle" ),
                [arcText, degrees, isStart]()
                {
                    return arcText( [&]( const ARC_GEOMETRY& aArc )
                                    {
                                        return degrees( isStart ? aArc.start : aArc.end );
                                    } );
                },
                [this, editArcs, isStart]( const wxString& aText )
                {
                    double angle = normalizeDegrees( m_frame->AngleValueFromString( aText ).AsDegrees() );

                    editArcs( _( "Change Arc Angle" ),
                              [&]( ARC_GEOMETRY& aArc )
                              {
                                  ( isStart ? aArc.start : aArc.end ) = angle;
                                  return normalizeDegrees( aArc.end - aArc.start ) >= 0.001;
                              } );
                },
                angles );
    }

    // (Altium shows the arc's length under Net Information)
    buildShapeSolderMask( L );
}


// ------------------------------------------------------------------- graphic line (Altium track)

void PCB_PROPERTIES_PANEL::buildLineLayout()
{
    const wxString& L = LAYOUT_LINE;

    PROPERTIES_FORM* location = addLayoutForm( _( "Location" ), L );
    location->AddXY( wxS( "Start X" ), wxS( "Start Y" ), wxS( "Locked" ) );

    PROPERTIES_FORM* props = addLayoutForm( _( "Properties" ), L );

    OPTS net;
    net.linkLabel = true;
    net.pickerShown = []() { return true; };
    net.picker = [this]() { NET_EYEDROPPER::Get().Start( m_frame, selectedItems() ); };
    net.visible = [this]() { return isBoardEditor(); };
    props->AddChoice( wxS( "Net" ), _( "Net" ), net );

    OPTS layer;
    layer.swatch = true;
    props->AddChoice( wxS( "Layer" ), _( "Layer" ), layer );
    props->AddSeparator();
    props->AddText( wxS( "Line Width" ), _( "Width" ) );
    props->AddXY( wxS( "Start X" ), wxS( "Start Y" ), wxEmptyString, _( "Start (X/Y)" ) );
    props->AddXY( wxS( "End X" ), wxS( "End Y" ), wxEmptyString, _( "End (X/Y)" ) );
    props->AddInfo( _( "Length" ),
            [this]()
            {
                return commonText(
                        [this]( BOARD_ITEM* aItem, wxString& aText )
                        {
                            PCB_SHAPE* shape = dynamic_cast<PCB_SHAPE*>( aItem );

                            if( !shape )
                                return false;

                            aText = m_frame->MessageTextFromValue( shape->GetLength() );
                            return true;
                        } );
            },
            true );

    buildShapeSolderMask( L );
}


// ------------------------------------------------------------------------ rectangle (Altium fill)

void PCB_PROPERTIES_PANEL::buildFillLayout()
{
    const wxString& L = LAYOUT_FILL;

    // An outline-only shape (KiCad) has a line width; Altium's fills and regions are solid
    auto outlineOnly =
            [this]()
            {
                for( BOARD_ITEM* item : selectedItems() )
                {
                    if( PCB_SHAPE* shape = dynamic_cast<PCB_SHAPE*>( item ); shape && !shape->IsAnyFill() )
                        return true;
                }

                return false;
            };

    buildNetInformation( L );

    PROPERTIES_FORM* location = addLayoutForm( _( "Location" ), L );
    location->AddXY( wxS( "Start X" ), wxS( "Start Y" ), wxS( "Locked" ) );

    PROPERTIES_FORM* props = addLayoutForm( _( "Properties" ), L );

    OPTS net;
    net.linkLabel = true;
    net.pickerShown = []() { return true; };
    net.picker = [this]() { NET_EYEDROPPER::Get().Start( m_frame, selectedItems() ); };
    net.visible = [this]() { return isBoardEditor(); };
    props->AddChoice( wxS( "Net" ), _( "Net" ), net );

    OPTS layer;
    layer.swatch = true;
    props->AddChoice( wxS( "Layer" ), _( "Layer" ), layer );
    props->AddInfo( _( "Area" ),
            [this]()
            {
                PCB_SHAPE* shape = dynamic_cast<PCB_SHAPE*>( singleItem() );

                if( !shape || shape->GetShape() != SHAPE_T::RECTANGLE )
                    return wxString();

                double area = (double) shape->GetRectangleWidth() * (double) shape->GetRectangleHeight();
                return m_frame->MessageTextFromValue( std::abs( area ), true, EDA_DATA_TYPE::AREA );
            } );
    props->AddSeparator();

    // Altium's fill: Length along X, Width along Y
    props->AddText( wxS( "Width" ), _( "Length" ) );
    props->AddText( wxS( "Height" ), _( "Width" ) );

    OPTS outline;
    outline.visible = outlineOnly;
    props->AddText( wxS( "Line Width" ), _( "Outline Width" ), outline );

    buildShapeSolderMask( L );
}


// --------------------------------------------------------------------------- polygon (Altium region)

void PCB_PROPERTIES_PANEL::buildRegionLayout()
{
    const wxString& L = LAYOUT_REGION;

    auto outlineOnly =
            [this]()
            {
                for( BOARD_ITEM* item : selectedItems() )
                {
                    if( PCB_SHAPE* shape = dynamic_cast<PCB_SHAPE*>( item ); shape && !shape->IsAnyFill() )
                        return true;
                }

                return false;
            };

    PROPERTIES_FORM* props = addLayoutForm( _( "Properties" ), L );

    OPTS net;
    net.linkLabel = true;
    net.pickerShown = []() { return true; };
    net.picker = [this]() { NET_EYEDROPPER::Get().Start( m_frame, selectedItems() ); };
    net.visible = [this]() { return isBoardEditor(); };
    props->AddChoice( wxS( "Net" ), _( "Net" ), net );

    OPTS layer;
    layer.swatch = true;
    props->AddChoice( wxS( "Layer" ), _( "Layer" ), layer );

    props->AddInfo( _( "Area" ),
            [this]()
            {
                PCB_SHAPE* shape = dynamic_cast<PCB_SHAPE*>( singleItem() );

                if( !shape || shape->GetShape() != SHAPE_T::POLY )
                    return wxString();

                return m_frame->MessageTextFromValue( shape->GetPolyShape().Area(), true, EDA_DATA_TYPE::AREA );
            } );

    OPTS outline;
    outline.visible = outlineOnly;
    props->AddText( wxS( "Line Width" ), _( "Outline Width" ), outline );

    props->AddSeparator();
    props->AddXY( wxS( "Position X" ), wxS( "Position Y" ), wxS( "Locked" ) );

    buildOutlineVertices( L );
    buildShapeSolderMask( L );
}


// ------------------------------------------------------------------------------------- dimension

void PCB_PROPERTIES_PANEL::buildDimensionLayout()
{
    const wxString& L = LAYOUT_DIMENSION;

    auto dimensions =
            [this]()
            {
                std::vector<PCB_DIMENSION_BASE*> result;

                for( BOARD_ITEM* item : selectedItems() )
                {
                    if( PCB_DIMENSION_BASE* dim = dynamic_cast<PCB_DIMENSION_BASE*>( item ) )
                        result.push_back( dim );
                }

                return result;
            };

    // Linear (aligned and orthogonal) dimensions have extension lines
    auto linear =
            [dimensions]()
            {
                std::vector<PCB_DIMENSION_BASE*> dims = dimensions();

                for( PCB_DIMENSION_BASE* dim : dims )
                {
                    if( !dynamic_cast<PCB_DIM_ALIGNED*>( dim ) )
                        return false;
                }

                return !dims.empty();
            };

    auto withText =
            [dimensions]()
            {
                for( PCB_DIMENSION_BASE* dim : dimensions() )
                {
                    if( dim->Type() == PCB_DIM_CENTER_T )
                        return false;
                }

                return true;
            };

    // A size the dimension stores itself (not a property): shown and edited as text
    auto addSize =
            [this]( PROPERTIES_FORM* aForm, const wxString& aLabel,
                    std::function<int( PCB_DIMENSION_BASE* )> aGet,
                    std::function<void( PCB_DIMENSION_BASE*, int )> aSet, const OPTS& aOpts )
            {
                aForm->AddCustomText( aLabel,
                        [this, aGet]()
                        {
                            return commonText(
                                    [&]( BOARD_ITEM* aItem, wxString& aText )
                                    {
                                        PCB_DIMENSION_BASE* dim = dynamic_cast<PCB_DIMENSION_BASE*>( aItem );

                                        if( !dim )
                                            return false;

                                        aText = m_frame->StringFromValue( aGet( dim ), true );
                                        return true;
                                    } );
                        },
                        [this, aGet, aSet]( const wxString& aText )
                        {
                            int value = m_frame->ValueFromString( aText );

                            if( value < 0 )
                                return;

                            commitSelection( _( "Edit Dimension" ),
                                    [&]( BOARD_COMMIT& aCommit, BOARD_ITEM* aItem )
                                    {
                                        PCB_DIMENSION_BASE* dim = dynamic_cast<PCB_DIMENSION_BASE*>( aItem );

                                        if( !dim || aGet( dim ) == value )
                                            return;

                                        aCommit.Modify( dim );
                                        aSet( dim, value );
                                        dim->Update();
                                    } );
                        },
                        aOpts );
            };

    // Style
    PROPERTIES_FORM* style = addLayoutForm( _( "Style" ), L );

    addSize( style, _( "Width" ),
             []( PCB_DIMENSION_BASE* aDim ) { return aDim->GetLineThickness(); },
             []( PCB_DIMENSION_BASE* aDim, int aValue ) { aDim->SetLineThickness( aValue ); }, OPTS() );

    OPTS linearOnly;
    linearOnly.visible = linear;
    style->AddTitle( _( "Extension Line" ), linearOnly );
    style->AddSeparator( linearOnly );

    // Altium's Gap: from the measured object to the extension line; Offset: past the
    // dimension line
    addSize( style, _( "Gap" ),
             []( PCB_DIMENSION_BASE* aDim ) { return aDim->GetExtensionOffset(); },
             []( PCB_DIMENSION_BASE* aDim, int aValue ) { aDim->SetExtensionOffset( aValue ); }, linearOnly );
    style->AddText( wxS( "Extension Line Overshoot" ), _( "Offset" ) );

    style->AddTitle( _( "Arrow Style" ) );
    style->AddSeparator();
    addSize( style, _( "Arrow Length" ),
             []( PCB_DIMENSION_BASE* aDim ) { return aDim->GetArrowLength(); },
             []( PCB_DIMENSION_BASE* aDim, int aValue ) { aDim->SetArrowLength( aValue ); }, OPTS() );

    // Properties
    PROPERTIES_FORM* props = addLayoutForm( _( "Properties" ), L );

    OPTS layer;
    layer.swatch = true;
    props->AddChoice( wxS( "Layer" ), _( "Layer" ), layer );

    // KiCad's text positions: outside the dimension line (Altium's Automatic), in line with
    // it, or where it was dragged
    OPTS textOpts;
    textOpts.visible = withText;
    props->AddCustomChoice( _( "Text Position" ), { _( "Automatic" ), _( "Inline" ), _( "Manual" ) },
            [dimensions]() -> int
            {
                int mode = -1;

                for( PCB_DIMENSION_BASE* dim : dimensions() )
                {
                    int dimMode = (int) dim->GetTextPositionMode();

                    if( mode >= 0 && dimMode != mode )
                        return -1;

                    mode = dimMode;
                }

                return mode;
            },
            [this]( int aMode )
            {
                commitSelection( _( "Edit Dimension" ),
                        [aMode]( BOARD_COMMIT& aCommit, BOARD_ITEM* aItem )
                        {
                            PCB_DIMENSION_BASE* dim = dynamic_cast<PCB_DIMENSION_BASE*>( aItem );

                            if( !dim || (int) dim->GetTextPositionMode() == aMode )
                                return;

                            aCommit.Modify( dim );
                            dim->SetTextPositionMode( static_cast<DIM_TEXT_POSITION>( aMode ) );
                            dim->Update();
                        } );
            },
            textOpts );

    props->AddChoice( wxS( "Arrow Direction" ), _( "Arrow Position" ) );
    props->AddSeparator( textOpts );
    props->AddText( wxS( "Height" ), _( "Text Height" ), textOpts );
    props->AddText( wxS( "Orientation" ), _( "Rotation" ), textOpts );
    props->AddSeparator( textOpts );

    auto fontType =
            [dimensions]() -> int
            {
                int type = -1;

                for( PCB_DIMENSION_BASE* dim : dimensions() )
                {
                    int dimType = isStrokeText( dim ) ? 1 : 0;

                    if( type >= 0 && dimType != type )
                        return -1;

                    type = dimType;
                }

                return type;
            };

    props->AddTitle( _( "Font Type" ), textOpts );
    props->AddCustomSegmented( { _( "TrueType" ), _( "Stroke" ) }, fontType,
            [this]( int aType )
            {
                commitSelection( _( "Change Font" ),
                        [aType]( BOARD_COMMIT& aCommit, BOARD_ITEM* aItem )
                        {
                            PCB_DIMENSION_BASE* dim = dynamic_cast<PCB_DIMENSION_BASE*>( aItem );

                            if( !dim || isStrokeText( dim ) == ( aType == 1 ) )
                                return;

                            aCommit.Modify( dim );
                            dim->SetFont( aType == 1 ? nullptr
                                                     : KIFONT::FONT::GetFont( wxS( "Arial" ), dim->IsBold(),
                                                                              dim->IsItalic() ) );
                            dim->Update();
                        } );
            },
            {}, wxEmptyString, textOpts );

    OPTS trueType;
    trueType.visible = [fontType, withText]() { return withText() && fontType() == 0; };
    props->AddChoice( wxS( "Font" ), _( "Font" ), trueType );

    OPTS stroke;
    stroke.visible = [fontType, withText]() { return withText() && fontType() == 1; };
    props->AddInfo( _( "Font" ), []() { return _( "Default" ); }, true, stroke );
    props->AddText( wxS( "Thickness" ), _( "Stroke Width" ), stroke );

    // Units
    PROPERTIES_FORM* units = addLayoutForm( _( "Units" ), L );
    units->AddChoice( wxS( "Units" ), _( "Primary Units" ) );
    units->AddChoice( wxS( "Precision" ), _( "Value Precision" ) );

    // Value
    PROPERTIES_FORM* valueForm = addLayoutForm( _( "Value" ), L );
    valueForm->AddChoice( wxS( "Units Format" ), _( "Format" ) );
    valueForm->AddText( wxS( "Prefix" ), _( "Prefix" ) );
    valueForm->AddText( wxS( "Suffix" ), _( "Suffix" ) );
    valueForm->AddInfo( _( "Sample" ),
            [this]()
            {
                return commonText(
                        []( BOARD_ITEM* aItem, wxString& aText )
                        {
                            PCB_DIMENSION_BASE* dim = dynamic_cast<PCB_DIMENSION_BASE*>( aItem );

                            if( !dim )
                                return false;

                            aText = dim->GetShownText( false );
                            return true;
                        } );
            },
            true, textOpts );
}


// ------------------------------------------------------------------------- rule area (keepout)

void PCB_PROPERTIES_PANEL::buildKeepoutLayout()
{
    const wxString& L = LAYOUT_KEEPOUT;

    auto singleZone = [this]() -> ZONE* { return dynamic_cast<ZONE*>( singleItem() ); };

    PROPERTIES_FORM* props = addLayoutForm( _( "Properties" ), L );

    // A KiCad rule area may cover several layers; they are set in its dialog
    props->AddInfo( _( "Restricted for Layer" ),
            [this, singleZone]()
            {
                ZONE*    zone = singleZone();
                wxString names;

                if( !zone )
                    return names;

                for( PCB_LAYER_ID layer : zone->GetLayerSet().Seq() )
                {
                    if( !names.IsEmpty() )
                        names += wxS( ", " );

                    names += m_frame->GetBoard()->GetLayerName( layer );
                }

                return names;
            },
            true );

    props->AddText( wxS( "Name" ), _( "Name" ) );
    props->AddInfo( _( "Area" ),
            [this, singleZone]()
            {
                ZONE* zone = singleZone();

                if( !zone )
                    return wxString();

                return m_frame->MessageTextFromValue( zone->CalculateOutlineArea(), true, EDA_DATA_TYPE::AREA );
            } );
    props->AddSeparator();
    props->AddXY( wxS( "Position X" ), wxS( "Position Y" ), wxS( "Locked" ) );

    // Keepout Restrictions: the objects kept out
    PROPERTIES_FORM* restrictions = addLayoutForm( _( "Keepout Restrictions" ), L );
    // Altium: Via, Track, Copper, SMD Pad, TH Pad.  KiCad keeps out all pads or none, and can
    // also keep out components.
    restrictions->AddCheck( wxS( "Keep Out Vias" ), _( "Via" ), true );
    restrictions->AddCheck( wxS( "Keep Out Tracks" ), _( "Track" ), true );
    restrictions->AddCheck( wxS( "Keep Out Zone Fills" ), _( "Copper" ), true );
    restrictions->AddCheck( wxS( "Keep Out Pads" ), _( "Pad" ), true );
    restrictions->AddCheck( wxS( "Keep Out Footprints" ), _( "Component" ), true );

    buildOutlineVertices( L );
}


// ----------------------------------------------------- footprint editor: Library Options > Footprint

void PCB_PROPERTIES_PANEL::buildLibraryFootprintLayout()
{
    // General tab, last section: Altium's "Other" with the Units toggle
    {
        PROPERTIES_FORM* other = new PROPERTIES_FORM( this, this );
        addSelectionSection( other, _( "Other" ), LAYOUT_LIBRARY, 0 );
        m_layoutForms.push_back( { LAYOUT_LIBRARY, other } );

        other->AddTitle( _( "Units" ) );
        other->AddCustomSegmented( { _( "mm" ), _( "mils" ) },
                [this]() -> int
                {
                    switch( m_frame->GetUserUnits() )
                    {
                    case EDA_UNITS::MM:   return 0;
                    case EDA_UNITS::MILS: return 1;
                    default:              return -1;
                    }
                },
                [this]( int aIdx )
                {
                    documentValueChanged( wxS( "doc.units" ), wxVariant( (long) aIdx ) );
                    CallAfter( [this]() { UpdateData(); } );
                } );
    }

    auto footprint =
            [this]() -> FOOTPRINT*
            {
                return m_frame->GetBoard() ? m_frame->GetBoard()->GetFirstFootprint() : nullptr;
            };

    auto change =
            [this, footprint]( const wxString& aMessage, const std::function<void( FOOTPRINT* )>& aChange )
            {
                FOOTPRINT* fp = footprint();

                if( !fp )
                    return;

                BOARD_COMMIT commit( m_frame );
                commit.Modify( fp );
                aChange( fp );
                commit.Push( aMessage );

                m_frame->Refresh();
                UpdateData();
            };

    // On the Footprint tab (1) of the Library Options
    auto addForm =
            [this]( const wxString& aTitle )
            {
                PROPERTIES_FORM* form = new PROPERTIES_FORM( this, this );
                addSelectionSection( form, aTitle, LAYOUT_LIBRARY, 1 );
                m_layoutForms.push_back( { LAYOUT_LIBRARY, form } );
                return form;
            };

    PROPERTIES_FORM* props = addForm( _( "Properties" ) );

    props->AddCustomText( _( "Name" ),
            [footprint]()
            {
                FOOTPRINT* fp = footprint();
                return fp ? fp->GetFPID().GetLibItemName().wx_str() : wxString();
            },
            [this, footprint, change]( const wxString& aText )
            {
                FOOTPRINT* fp = footprint();
                wxString   name = aText;

                name.Trim().Trim( false );

                if( !fp || name == fp->GetFPID().GetLibItemName().wx_str() )
                    return;

                if( name.IsEmpty() )
                {
                    m_frame->ShowInfoBarError( _( "Footprint must have a name." ) );
                    return;
                }

                if( !FOOTPRINT::IsLibNameValid( name ) )
                {
                    m_frame->ShowInfoBarError( wxString::Format( _( "Footprint name may not contain '%s'." ),
                                                                 FOOTPRINT::StringLibNameInvalidChars( true ) ) );
                    return;
                }

                wxString library = fp->GetFPID().GetLibNickname().wx_str();

                if( !library.IsEmpty() )
                {
                    FOOTPRINT_LIBRARY_ADAPTER* adapter = PROJECT_PCB::FootprintLibAdapter( &m_frame->Prj() );

                    if( adapter && adapter->FootprintExists( library, name ) )
                    {
                        m_frame->ShowInfoBarError( wxString::Format( _( "Footprint '%s' already exists in "
                                                                        "library '%s'." ),
                                                                     name, library ) );
                        return;
                    }
                }

                change( _( "Rename Footprint" ),
                        [&]( FOOTPRINT* aFootprint )
                        {
                            LIB_ID id = aFootprint->GetFPID();
                            id.SetLibItemName( name );
                            aFootprint->SetFPID( id );
                        } );

                if( FOOTPRINT_EDIT_FRAME* editor = dynamic_cast<FOOTPRINT_EDIT_FRAME*>( m_frame ) )
                    editor->UpdateTitle();
            } );

    OPTS description;
    description.lines = 3;
    props->AddCustomText( _( "Description" ),
            [footprint]()
            {
                FOOTPRINT* fp = footprint();
                return fp ? fp->GetLibDescription() : wxString();
            },
            [change]( const wxString& aText )
            {
                change( _( "Edit Footprint Description" ),
                        [&]( FOOTPRINT* aFootprint ) { aFootprint->SetLibDescription( aText ); } );
            },
            description );

    // Altium's footprint Type, as for a component: KiCad's board-only / BOM / position file flags
    struct FOOTPRINT_TYPE
    {
        wxString label;
        bool     boardOnly;
        bool     noBom;
        bool     noPosition;
    };

    static const std::vector<FOOTPRINT_TYPE> types = {
        { _( "Standard" ),          false, false, false },
        { _( "Standard (No BOM)" ), false, true,  false },
        { _( "Mechanical" ),        true,  true,  false },
        { _( "Graphical" ),         true,  true,  true  } };

    std::vector<wxString> typeNames;

    for( const FOOTPRINT_TYPE& type : types )
        typeNames.push_back( type.label );

    props->AddCustomChoice( _( "Type" ), typeNames,
            [footprint]() -> int
            {
                FOOTPRINT* fp = footprint();

                if( !fp )
                    return -1;

                for( size_t ii = 0; ii < types.size(); ++ii )
                {
                    if( fp->IsBoardOnly() == types[ii].boardOnly && fp->IsExcludedFromBOM() == types[ii].noBom
                            && fp->IsExcludedFromPosFiles() == types[ii].noPosition )
                    {
                        return (int) ii;
                    }
                }

                return -1;
            },
            [change]( int aType )
            {
                if( aType < 0 || aType >= (int) types.size() )
                    return;

                change( _( "Change Footprint Type" ),
                        [&]( FOOTPRINT* aFootprint )
                        {
                            aFootprint->SetBoardOnly( types[aType].boardOnly );
                            aFootprint->SetExcludedFromBOM( types[aType].noBom );
                            aFootprint->SetExcludedFromPosFiles( types[aType].noPosition );
                        } );
            } );

    props->AddInfo( _( "Area" ),
            [this, footprint]()
            {
                FOOTPRINT* fp = footprint();

                if( !fp )
                    return wxString();

                BOX2I  box = fp->GetBoundingBox( false );
                double area = (double) box.GetWidth() * (double) box.GetHeight();

                return m_frame->MessageTextFromValue( area, true, EDA_DATA_TYPE::AREA );
            },
            true );

    // Parameters: the footprint's own (user) fields; values edit in place
    PROPERTIES_FORM* params = addForm( _( "Parameters" ) );

    params->AddTable( { 1, 1 },
            [footprint, change]()
            {
                PROPERTIES_FORM::TABLE_ROWS rows;
                rows.push_back( { heading( _( "Name" ) ), heading( _( "Value" ) ) } );

                FOOTPRINT* fp = footprint();

                if( !fp )
                    return rows;

                for( PCB_FIELD* field : fp->GetFields() )
                {
                    if( !field || field->IsMandatory() )
                        continue;

                    wxString name = field->GetName();
                    CELL     value( field->GetText() );

                    value.edit =
                            [change, name]( const wxString& aText )
                            {
                                change( _( "Edit Footprint Parameter" ),
                                        [&]( FOOTPRINT* aFootprint )
                                        {
                                            if( PCB_FIELD* f = aFootprint->GetField( name ) )
                                                f->SetText( aText );
                                        } );
                            };

                    rows.push_back( { fixed( name ), value } );
                }

                return rows;
            },
            &m_fpParamRow );

    // The user field on the selected row (row 0 is the heading)
    auto selectedField =
            [this, footprint]() -> PCB_FIELD*
            {
                FOOTPRINT* fp = footprint();
                int        row = 1;

                if( !fp )
                    return nullptr;

                for( PCB_FIELD* field : fp->GetFields() )
                {
                    if( !field || field->IsMandatory() )
                        continue;

                    if( row++ == m_fpParamRow )
                        return field;
                }

                return nullptr;
            };

    // Altium's Add / Remove under the table
    params->AddButtons( {
            { _( "Add..." ),
              [this, footprint]()
              {
                  FOOTPRINT* fp = footprint();

                  if( !fp )
                      return;

                  wxTextEntryDialog dlg( this, _( "Parameter name:" ), _( "Add Parameter" ) );

                  if( dlg.ShowModal() != wxID_OK )
                      return;

                  wxString name = dlg.GetValue();
                  name.Trim().Trim( false );

                  if( name.IsEmpty() )
                      return;

                  if( fp->GetField( name ) )
                  {
                      m_frame->ShowInfoBarError( wxString::Format( _( "Parameter '%s' already exists." ), name ) );
                      return;
                  }

                  // As KiCad's footprint dialog adds a field: hidden, on the fabrication layer
                  PCB_FIELD* field = new PCB_FIELD( fp, FIELD_T::USER, name );
                  field->SetLayer( fp->IsFlipped() ? B_Fab : F_Fab );
                  field->SetVisible( false );
                  field->SetTextSize( m_frame->GetDesignSettings().GetTextSize( field->GetLayer() ) );
                  field->SetTextThickness( m_frame->GetDesignSettings().GetTextThickness( field->GetLayer() ) );
                  field->SetPosition( fp->GetPosition() );

                  // A change of the footprint (as KiCad's footprint dialog makes): a commit's
                  // Remove of a field only hides it, and undo swaps the whole footprint back
                  BOARD_COMMIT commit( m_frame );
                  commit.Modify( fp );
                  fp->Add( field );
                  m_frame->GetCanvas()->GetView()->Add( field );
                  commit.Push( _( "Add Footprint Parameter" ) );

                  m_frame->Refresh();
                  UpdateData();
              } },
            { _( "Remove" ),
              [this, footprint, selectedField]()
              {
                  FOOTPRINT* fp = footprint();
                  PCB_FIELD* field = selectedField();

                  if( !fp || !field )
                      return;

                  BOARD_COMMIT commit( m_frame );
                  commit.Modify( fp );
                  m_frame->GetCanvas()->GetView()->Remove( field );
                  fp->Remove( field );
                  delete field;
                  commit.Push( _( "Remove Footprint Parameter" ) );

                  m_fpParamRow = -1;
                  m_frame->Refresh();
                  UpdateData();
              } } } );
}


// ----------------------------------------------------------------------------------------- board

void PCB_PROPERTIES_PANEL::buildBoardInformation()
{
    m_boardInfo = new PROPERTIES_FORM( this, this );

    auto stat =
            [this]( const wxString& aKey )
            {
                return [this, aKey]()
                       {
                           for( const auto& [key, value] : m_boardStats )
                           {
                               if( key == aKey )
                                   return value;
                           }

                           return wxString();
                       };
            };

    auto group =
            [&]( const wxString& aTitle, std::vector<std::pair<wxString, wxString>> aRows )
            {
                m_boardInfo->AddTitle( aTitle );

                for( const auto& [key, label] : aRows )
                    m_boardInfo->AddInfo( label, stat( key ) );
            };

    group( _( "Board Size" ), { { wxS( "width" ),     _( "Horizontal" ) },
                                { wxS( "height" ),    _( "Vertical" ) },
                                { wxS( "area" ),      _( "Area" ) },
                                { wxS( "compArea" ),  _( "Components Area" ) },
                                { wxS( "density" ),   _( "Density" ) } } );
    group( _( "Components" ), { { wxS( "fpTotal" ),   _( "Total" ) },
                                { wxS( "fpTop" ),     _( "Top" ) },
                                { wxS( "fpBottom" ),  _( "Bottom" ) } } );
    group( _( "Layers" ),     { { wxS( "layers" ),    _( "Total" ) },
                                { wxS( "signal" ),    _( "Signal" ) },
                                { wxS( "thickness" ), _( "Thickness" ) } } );
    group( _( "Nets" ),       { { wxS( "nets" ),      _( "Total" ) },
                                { wxS( "unrouted" ),  _( "Unrouted" ) } } );
    group( _( "Primitives & Others" ),
                              { { wxS( "arcs" ),      _( "Arcs" ) },
                                { wxS( "pads" ),      _( "Pads" ) },
                                { wxS( "strings" ),   _( "Strings" ) },
                                { wxS( "tracks" ),    _( "Tracks" ) },
                                { wxS( "vias" ),      _( "Vias" ) },
                                { wxS( "polygons" ),  _( "Polygons" ) },
                                { wxS( "holes" ),     _( "Pad/Via Holes" ) },
                                { wxS( "drc" ),       _( "DRC Violations" ) } } );

    // After the Selection Filter and Snap Options, as in Altium
    SetDocumentWidget( m_boardInfo, _( "Board Information" ) );
}


void PCB_PROPERTIES_PANEL::updateBoardInformation()
{
    BOARD* board = m_frame->GetBoard();

    if( !m_boardInfo || !board )
        return;

    auto count = []( size_t aValue ) { return wxString::Format( wxS( "%zu" ), aValue ); };

    BOX2I  edges = board->GetBoardEdgesBoundingBox();
    double boardArea = (double) edges.GetWidth() * (double) edges.GetHeight();
    double compArea = 0.0;
    size_t top = 0;
    size_t pads = 0;
    size_t strings = 0;
    size_t holes = 0;

    for( FOOTPRINT* fp : board->Footprints() )
    {
        BOX2I box = fp->GetBoundingBox( false );
        compArea += (double) box.GetWidth() * (double) box.GetHeight();

        if( !fp->IsFlipped() )
            top++;

        pads += fp->Pads().size();

        for( PAD* pad : fp->Pads() )
        {
            if( pad->HasHole() )
                holes++;
        }

        for( BOARD_ITEM* item : fp->GraphicalItems() )
        {
            if( item->Type() == PCB_TEXT_T || item->Type() == PCB_TEXTBOX_T )
                strings++;
        }

        strings += fp->GetFields().size();
    }

    for( BOARD_ITEM* item : board->Drawings() )
    {
        if( item->Type() == PCB_TEXT_T || item->Type() == PCB_TEXTBOX_T )
            strings++;
    }

    size_t tracks = 0;
    size_t arcs = 0;
    size_t vias = 0;

    for( PCB_TRACK* track : board->Tracks() )
    {
        if( track->Type() == PCB_VIA_T )
            vias++;
        else if( track->Type() == PCB_ARC_T )
            arcs++;
        else
            tracks++;
    }

    size_t polygons = 0;

    for( ZONE* zone : board->Zones() )
    {
        if( !zone->GetIsRuleArea() )
            polygons++;
    }

    size_t drc = 0;

    for( PCB_MARKER* marker : board->Markers() )
    {
        if( !marker->IsExcluded() )
            drc++;
    }

    std::shared_ptr<CONNECTIVITY_DATA> connectivity = board->GetConnectivity();

    m_boardStats = {
        { wxS( "width" ),     m_frame->MessageTextFromValue( edges.GetWidth() ) },
        { wxS( "height" ),    m_frame->MessageTextFromValue( edges.GetHeight() ) },
        { wxS( "area" ),      m_frame->MessageTextFromValue( boardArea, true, EDA_DATA_TYPE::AREA ) },
        { wxS( "compArea" ),  m_frame->MessageTextFromValue( compArea, true, EDA_DATA_TYPE::AREA ) },
        { wxS( "density" ),   boardArea > 0 ? wxString::Format( wxS( "%.0f%%" ), 100.0 * compArea / boardArea )
                                            : wxString( wxS( "0%" ) ) },
        { wxS( "fpTotal" ),   count( board->Footprints().size() ) },
        { wxS( "fpTop" ),     count( top ) },
        { wxS( "fpBottom" ),  count( board->Footprints().size() - top ) },
        { wxS( "layers" ),    count( board->GetEnabledLayers().count() ) },
        { wxS( "signal" ),    wxString::Format( wxS( "%d" ), board->GetCopperLayerCount() ) },
        { wxS( "thickness" ), m_frame->MessageTextFromValue( board->GetDesignSettings().GetBoardThickness() ) },
        { wxS( "nets" ),      wxString::Format( wxS( "%d" ), std::max( 0, (int) board->GetNetCount() - 1 ) ) },
        { wxS( "unrouted" ),  connectivity ? wxString::Format( wxS( "%u" ),
                                                               connectivity->GetUnconnectedCount( false ) )
                                           : wxString() },
        { wxS( "arcs" ),      count( arcs ) },
        { wxS( "pads" ),      count( pads ) },
        { wxS( "strings" ),   count( strings ) },
        { wxS( "tracks" ),    count( tracks ) },
        { wxS( "vias" ),      count( vias ) },
        { wxS( "polygons" ),  count( polygons ) },
        { wxS( "holes" ),     count( holes + vias ) },
        { wxS( "drc" ),       count( drc ) } };

    m_boardInfo->Sync();
}


void PCB_PROPERTIES_PANEL::appendBoardParameters()
{
    // Altium's board Parameters: the project's text variables (edited in Board Setup)
    std::map<wxString, wxString>& vars = m_frame->Prj().GetTextVars();
    wxPGProperty*                 category = m_grid->GetPropertyByName( wxS( "doc.params" ) );

    // Start the section over when the variables changed
    if( category )
    {
        bool same = category->GetChildCount() == std::max<size_t>( 1, vars.size() );

        for( const auto& [name, value] : vars )
            same &= m_grid->GetPropertyByName( wxS( "doc.param." ) + name ) != nullptr;

        if( !same )
        {
            m_grid->DeleteProperty( category );
            category = nullptr;
        }
    }

    bool created = !category;

    category = docCategory( wxS( "doc.params" ), _( "Parameters" ) );

    if( vars.empty() )
        docText( category, wxS( "doc.param.none" ), _( "Text variables" ), _( "None" ) );

    for( const auto& [name, value] : vars )
        docText( category, wxS( "doc.param." ) + name, name, value );

    if( created )
        applyFilter();
}


void PCB_PROPERTIES_PANEL::appendHealthCheck()
{
    // Altium's Health Check: what the last design rule check found
    BOARD* board = m_frame->GetBoard();
    size_t errors = 0;
    size_t warnings = 0;
    size_t excluded = 0;

    for( PCB_MARKER* marker : board->Markers() )
    {
        if( marker->IsExcluded() )
            excluded++;
        else if( marker->GetSeverity() == RPT_SEVERITY_ERROR )
            errors++;
        else if( marker->GetSeverity() == RPT_SEVERITY_WARNING )
            warnings++;
    }

    std::shared_ptr<CONNECTIVITY_DATA> connectivity = board->GetConnectivity();

    wxPGProperty* category = docCategory( wxS( "doc.health" ), _( "Health Check" ) );

    docText( category, wxS( "doc.health.errors" ), _( "DRC Errors" ), wxString::Format( wxS( "%zu" ), errors ) );
    docText( category, wxS( "doc.health.warnings" ), _( "DRC Warnings" ),
             wxString::Format( wxS( "%zu" ), warnings ) );
    docText( category, wxS( "doc.health.excluded" ), _( "Excluded" ), wxString::Format( wxS( "%zu" ), excluded ) );
    docText( category, wxS( "doc.health.unrouted" ), _( "Unrouted Connections" ),
             connectivity ? wxString::Format( wxS( "%u" ), connectivity->GetUnconnectedCount( false ) )
                          : wxString() );
}
