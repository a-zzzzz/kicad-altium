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

#include <id.h>
#include <tools/pcb_altium_context_menu.h>

#include <advanced_config.h>
#include <bitmaps.h>
#include <footprint.h>
#include <frame_type.h>
#include <pad.h>
#include <pcb_base_frame.h>
#include <pcb_field.h>
#include <tool/actions.h>
#include <tool/action_menu.h>
#include <tool/selection.h>
#include <tools/pcb_actions.h>
#include <zone.h>

#include <tool/tool_manager.h>

#include <map>
#include <set>

#include <wx/menu.h>


// Edit > Align, shared with the menu bar (menubar_pcb_editor.cpp)
ACTION_MENU* BuildPcbAlignMenu( TOOL_INTERACTIVE* aTool );


namespace
{
/// What the right-clicked selection contains, to grey out what doesn't apply.
struct SEL_INFO
{
    int        footprints = 0;
    int        zones = 0;          ///< copper pours
    int        rooms = 0;          ///< rule areas
    int        pads = 0;
    int        connected = 0;      ///< items on a net
    int        total = 0;
    FOOTPRINT* firstFootprint = nullptr;
};


SEL_INFO inspect( const SELECTION& aSelection )
{
    SEL_INFO info;

    for( EDA_ITEM* item : aSelection )
    {
        info.total++;

        switch( item->Type() )
        {
        case PCB_FOOTPRINT_T:
            info.footprints++;

            if( !info.firstFootprint )
                info.firstFootprint = static_cast<FOOTPRINT*>( item );

            for( PAD* pad : static_cast<FOOTPRINT*>( item )->Pads() )
            {
                if( pad->GetNetCode() > 0 )
                {
                    info.connected++;
                    break;
                }
            }

            break;

        case PCB_ZONE_T:
            if( static_cast<ZONE*>( item )->GetIsRuleArea() )
                info.rooms++;
            else
                info.zones++;

            if( static_cast<ZONE*>( item )->GetNetCode() > 0 )
                info.connected++;

            break;

        case PCB_PAD_T:
            info.pads++;

            if( static_cast<PAD*>( item )->GetNetCode() > 0 )
                info.connected++;

            if( !info.firstFootprint )
                info.firstFootprint = static_cast<PAD*>( item )->GetParentFootprint();

            break;

        case PCB_TRACE_T:
        case PCB_ARC_T:
        case PCB_VIA_T:
            if( static_cast<BOARD_CONNECTED_ITEM*>( item )->GetNetCode() > 0 )
                info.connected++;

            break;

        case PCB_FIELD_T:
        case PCB_TEXT_T:
        case PCB_SHAPE_T:
            if( !info.firstFootprint )
                info.firstFootprint = static_cast<BOARD_ITEM*>( item )->GetParentFootprint();

            break;

        default:
            break;
        }
    }

    return info;
}


/**
 * A menu whose items can hide their shortcut text (as Altium shows none on them) and can act on
 * the component the right-clicked pad or text belongs to: choosing such an item first selects
 * that component, then runs the action.
 */
class CONTEXT_MENU : public ACTION_MENU
{
public:
    CONTEXT_MENU( TOOL_INTERACTIVE* aTool, FOOTPRINT* aComponent = nullptr ) :
            ACTION_MENU( false, aTool ),
            m_component( aComponent )
    {
    }

    /// Item without shortcut text (the action keeps its hotkey elsewhere).
    wxMenuItem* AddPlain( const TOOL_ACTION& aAction, const wxString& aLabel )
    {
        wxMenuItem* item = Add( aAction, ACTION_MENU::NORMAL, aLabel );
        m_toolActions.erase( aAction.GetUIId() );
        m_plain[aAction.GetUIId()] = &aAction;
        return item;
    }

    /// Item acting on the component.  It gets its own menu id: the same action may also be in
    /// another submenu (Net Actions, Align, ...), and ACTION_MENU would dispatch the action's
    /// own id to that plain copy, skipping the "select the component first" step.
    wxMenuItem* AddForComponent( const TOOL_ACTION& aAction, const wxString& aLabel )
    {
        int         id = ID_POPUP_MENU_END - 200 + m_nextOwnId++;
        wxMenuItem* item = Add( aLabel, id, aAction.GetIcon() );

        m_plain[id] = &aAction;
        m_forComponent.insert( id );
        return item;
    }

protected:
    ACTION_MENU* create() const override
    {
        CONTEXT_MENU* menu = new CONTEXT_MENU( m_tool, m_component );
        menu->m_plain = m_plain;
        menu->m_forComponent = m_forComponent;
        menu->m_nextOwnId = m_nextOwnId;
        return menu;
    }

    OPT_TOOL_EVENT eventHandler( const wxMenuEvent& aEvent ) override
    {
        auto it = m_plain.find( aEvent.GetId() );

        if( it == m_plain.end() )
            return OPT_TOOL_EVENT();

        if( m_component && m_forComponent.count( aEvent.GetId() ) && !m_component->IsSelected() )
        {
            TOOL_MANAGER* toolMgr = getToolManager();
            toolMgr->RunAction( ACTIONS::selectionClear );
            toolMgr->RunAction<EDA_ITEM*>( ACTIONS::selectItem, m_component );
        }

        return it->second->MakeEvent();
    }

private:
    FOOTPRINT*                        m_component;
    std::map<int, const TOOL_ACTION*> m_plain;
    std::set<int>                     m_forComponent;
    int                               m_nextOwnId = 0;
};


CONTEXT_MENU* submenu( TOOL_INTERACTIVE* aTool, const wxString& aTitle,
                       BITMAPS aIcon = BITMAPS::INVALID_BITMAP, FOOTPRINT* aComponent = nullptr )
{
    CONTEXT_MENU* menu = new CONTEXT_MENU( aTool, aComponent );
    menu->SetTitle( aTitle );

    if( aIcon != BITMAPS::INVALID_BITMAP )
        menu->SetIcon( aIcon );

    return menu;
}


/// Add a submenu, greyed out when nothing in it applies.
void addSubmenu( ACTION_MENU* aParent, ACTION_MENU* aMenu, bool aEnabled )
{
    wxMenuItem* item = aParent->Add( aMenu );
    item->Enable( aEnabled );
}


void enable( wxMenuItem* aItem, bool aEnabled )
{
    if( aItem )
        aItem->Enable( aEnabled );
}


ACTION_MENU* buildPlaceMenu( TOOL_INTERACTIVE* aTool, bool aBoardEditor )
{
    ACTION_MENU* menu = submenu( aTool, _( "&Place" ) );

    if( aBoardEditor )
    {
        menu->Add( PCB_ACTIONS::placeFootprint, ACTION_MENU::NORMAL, _( "&Component..." ) );

        if( ADVANCED_CFG::GetCfg().m_EnablePcbDesignBlocks )
            menu->Add( PCB_ACTIONS::placeDesignBlock, ACTION_MENU::NORMAL, _( "Re&use Block..." ) );

        menu->AppendSeparator();
    }

    menu->Add( PCB_ACTIONS::drawRectangle, ACTION_MENU::NORMAL, _( "&Fill" ) );
    menu->Add( PCB_ACTIONS::drawPolygon,   ACTION_MENU::NORMAL, _( "Solid &Region" ) );

    ACTION_MENU* arc = submenu( aTool, _( "&Arc" ), BITMAPS::add_arc );
    arc->Add( PCB_ACTIONS::drawArc,    ACTION_MENU::NORMAL, _( "&Arc (Center)" ) );
    arc->Add( PCB_ACTIONS::drawCircle, ACTION_MENU::NORMAL, _( "F&ull Circle" ) );
    menu->Add( arc );

    menu->Add( PCB_ACTIONS::drawLine,    ACTION_MENU::NORMAL, _( "&Line" ) );
    menu->Add( PCB_ACTIONS::placeText,   ACTION_MENU::NORMAL, _( "&String" ) );
    menu->Add( PCB_ACTIONS::drawTextBox, ACTION_MENU::NORMAL, _( "Te&xt Frame" ) );

    if( !aBoardEditor )
        menu->Add( PCB_ACTIONS::placePad, ACTION_MENU::NORMAL, _( "&Pad" ) );

    if( aBoardEditor )
    {
        menu->Add( PCB_ACTIONS::drawVia,          ACTION_MENU::NORMAL, _( "&Via" ) );
        menu->Add( PCB_ACTIONS::routeSingleTrack, ACTION_MENU::NORMAL, _( "&Track" ) );
    }

    menu->Add( PCB_ACTIONS::placeReferenceImage, ACTION_MENU::NORMAL, _( "Grap&hics..." ) );

    menu->AppendSeparator();
    ACTION_MENU* keepout = submenu( aTool, _( "&Keepout" ), BITMAPS::add_keepout_area );
    keepout->Add( PCB_ACTIONS::drawRuleArea, ACTION_MENU::NORMAL, _( "Solid &Region" ) );
    menu->Add( keepout );

    if( aBoardEditor )
    {
        menu->AppendSeparator();
        menu->Add( PCB_ACTIONS::drawZone,       ACTION_MENU::NORMAL, _( "Poly&gon Pour..." ) );
        menu->Add( PCB_ACTIONS::drawZoneCutout, ACTION_MENU::NORMAL, _( "Polygon Pour Cutout" ) );

        menu->AppendSeparator();
        menu->Add( PCB_ACTIONS::placeStackup, ACTION_MENU::NORMAL, _( "Layer Stac&k Table" ) );
    }

    menu->AppendSeparator();
    ACTION_MENU* dimension = submenu( aTool, _( "&Dimension" ), BITMAPS::add_aligned_dimension );
    dimension->Add( PCB_ACTIONS::drawOrthogonalDimension, ACTION_MENU::NORMAL, _( "&Linear" ) );
    dimension->Add( PCB_ACTIONS::drawRadialDimension,     ACTION_MENU::NORMAL, _( "&Radial" ) );
    dimension->Add( PCB_ACTIONS::drawLeader,              ACTION_MENU::NORMAL, _( "L&eader" ) );
    dimension->Add( PCB_ACTIONS::drawCenterDimension,     ACTION_MENU::NORMAL, _( "&Center" ) );
    menu->Add( dimension );

    return menu;
}
} // namespace


ACTION_MENU* BuildAltiumPcbContextMenu( TOOL_INTERACTIVE* aTool, PCB_BASE_FRAME* aFrame,
                                        const SELECTION& aSelection )
{
    const bool     board = aFrame->IsType( FRAME_PCB_EDITOR );
    const SEL_INFO sel = inspect( aSelection );

    CONTEXT_MENU* menu = new CONTEXT_MENU( aTool );

    enable( menu->AddPlain( PCB_ACTIONS::findSimilar, _( "Fi&nd Similar Objects..." ) ),
            sel.total > 0 );
    menu->Add( PCB_ACTIONS::clearHighlight, ACTION_MENU::NORMAL, _( "&Clear Filter" ) );
    menu->AppendSeparator();

    addSubmenu( menu, buildPlaceMenu( aTool, board ), true );

    if( board )
    {
        // xSignals: no KiCad counterpart for the wizard
        ACTION_MENU* xsignals = submenu( aTool, _( "&xSignals" ) );
        xsignals->Append( wxID_ANY, _( "Run xSignals &Wizard" ) )->Enable( false );
        menu->Add( xsignals );

        // Unions are KiCad's groups
        ACTION_MENU* unions = submenu( aTool, _( "&Unions" ) );
        enable( unions->Add( ACTIONS::group, ACTION_MENU::NORMAL,
                             _( "Create Union from selected objects" ) ),
                sel.total > 1 );
        unions->Append( wxID_ANY, _( "Explode Length Tuning To Free Primitives" ) )->Enable( false );
        menu->Add( unions );
    }

    if( board )
    {
        // Component Actions: for the selected component, or the one the right-clicked pad or
        // text belongs to; the labels follow the state, as Altium shows them
        FOOTPRINT*    fp = sel.firstFootprint;
        CONTEXT_MENU* component = submenu( aTool, _( "Compo&nent Actions" ), BITMAPS::INVALID_BITMAP, fp );
        bool          refShown = fp && fp->Reference().IsVisible();
        bool          valShown = fp && fp->Value().IsVisible();

        component->AddForComponent( PCB_ACTIONS::ctxToggleDesignator,
                                    refShown ? _( "Hide &Designator" ) : _( "Show &Designator" ) );
        component->AddForComponent( PCB_ACTIONS::ctxToggleComment,
                                    valShown ? _( "Hide &Comment" ) : _( "Show &Comment" ) );
        component->AppendSeparator();
        component->AddForComponent( PCB_ACTIONS::move,        _( "&Move Component" ) );
        component->AddForComponent( PCB_ACTIONS::unrouteSelected, _( "Unroute C&omponent" ) );
        component->AddForComponent( PCB_ACTIONS::routerAutorouteSelected, _( "&Autoroute Component" ) );
        component->Append( wxID_ANY, _( "&Fanout Component" ) )->Enable( false );
        component->AppendSeparator();
        component->AddForComponent( PCB_ACTIONS::showNetInRatsnest, _( "&Show Nets" ) );
        component->AddForComponent( PCB_ACTIONS::hideNetInRatsnest, _( "&Hide Nets" ) );
        component->AppendSeparator();
        component->AddForComponent( PCB_ACTIONS::selectConnection, _( "Select Component Connections" ) );
        component->AddForComponent( PCB_ACTIONS::ctxSelectComponentNets, _( "Select Component Nets" ) );
        component->AppendSeparator();
        component->Append( wxID_ANY, _( "Explode Component To Free Primitives" ) )->Enable( false );
        component->AppendSeparator();
        component->AddForComponent( PCB_ACTIONS::ctxManage3DBodies, _( "Manage 3D &Bodies" ) );
        component->AddForComponent( PCB_ACTIONS::updateFootprint,
                                    _( "Update Current Components From PCB Libraries" ) );
        component->AppendSeparator();
        component->Append( wxID_ANY, _( "Configure Pin/Part Swapping..." ) )->Enable( false );
        component->AppendSeparator();
        component->AddPlain( PCB_ACTIONS::swapComponents, _( "Swap Components" ) );
        // Altium keeps the submenu open whatever was clicked: Swap Components needs no component
        if( !fp )
        {
            for( wxMenuItem* item : component->GetMenuItems() )
            {
                if( !item->IsSeparator() && item->GetId() != PCB_ACTIONS::swapComponents.GetUIId() )
                    item->Enable( false );
            }
        }

        addSubmenu( menu, component, true );

        CONTEXT_MENU* references = submenu( aTool, _( "Reference&s" ), BITMAPS::INVALID_BITMAP, fp );
        auto hasField =
                [&]( const wxString& aName )
                {
                    if( !fp )
                        return false;

                    PCB_FIELD* field = fp->GetField( aName );
                    return field && !field->GetText().IsEmpty();
                };

        enable( references->AddForComponent( PCB_ACTIONS::ctxManufacturerUrl, _( "&Manufacturer URL" ) ),
                hasField( wxS( "Manufacturer URL" ) ) );
        enable( references->AddForComponent( PCB_ACTIONS::ctxDatasheet, _( "&Datasheet" ) ),
                fp != nullptr );
        enable( references->AddForComponent( PCB_ACTIONS::ctxPackageSpec, _( "&Package Specification" ) ),
                hasField( wxS( "Package Specification" ) ) );
        enable( references->AddForComponent( PCB_ACTIONS::ctxProductUrl, _( "P&roduct URL" ) ),
                hasField( wxS( "Product URL" ) ) );
        addSubmenu( menu, references, fp != nullptr );

        ACTION_MENU* room = submenu( aTool, _( "Roo&m Actions" ) );
        room->Add( PCB_ACTIONS::ctxUnrouteRoom, ACTION_MENU::NORMAL, _( "&Unroute Room" ) );
        addSubmenu( menu, room, sel.rooms > 0 );

        // Polygon Actions: the board-wide items always apply, the rest need a selected pour
        ACTION_MENU* polygon = submenu( aTool, _( "Polygon Actions" ) );
        bool         pour = sel.zones > 0;

        polygon->Add( PCB_ACTIONS::zonesManager, ACTION_MENU::NORMAL, _( "Polygon &Manager" ) );
        polygon->AppendSeparator();
        enable( polygon->Add( PCB_ACTIONS::zoneFill, ACTION_MENU::NORMAL, _( "&Repour Selected" ) ), pour );
        polygon->Add( PCB_ACTIONS::zoneFillAll,         ACTION_MENU::NORMAL, _( "Repour &All" ) );
        polygon->Add( PCB_ACTIONS::zoneRepourViolating, ACTION_MENU::NORMAL,
                      _( "Repour &Violating Polygons" ) );
        polygon->Add( PCB_ACTIONS::zoneFillDirty,       ACTION_MENU::NORMAL, _( "Repour M&odified" ) );
        polygon->AppendSeparator();
        enable( polygon->Add( PCB_ACTIONS::zoneUnfill, ACTION_MENU::NORMAL, _( "Shelve S&elected" ) ), pour );
        polygon->Add( PCB_ACTIONS::zoneUnfillAll, ACTION_MENU::NORMAL, _( "S&helve All" ) );
        polygon->AppendSeparator();
        enable( polygon->Add( PCB_ACTIONS::zonePriorityMoveToTop, ACTION_MENU::NORMAL,
                              _( "Bring to front" ) ), pour );
        enable( polygon->Add( PCB_ACTIONS::zonePriorityMoveToBottom, ACTION_MENU::NORMAL,
                              _( "Send to back" ) ), pour );
        polygon->AppendSeparator();
        enable( polygon->Add( PCB_ACTIONS::move, ACTION_MENU::NORMAL, _( "Move Polygon" ) ), pour );
        polygon->AppendSeparator();
        enable( polygon->Add( PCB_ACTIONS::zoneMerge, ACTION_MENU::NORMAL,
                              _( "Combine Selected Polygons" ) ),
                sel.zones > 1 );
        addSubmenu( menu, polygon, pour );

        ACTION_MENU* net = submenu( aTool, _( "&Net Actions" ) );
        net->Add( PCB_ACTIONS::showNetInRatsnest, ACTION_MENU::NORMAL, _( "&Show Nets" ) );
        net->Add( PCB_ACTIONS::hideNetInRatsnest, ACTION_MENU::NORMAL, _( "&Hide Nets" ) );
        net->AppendSeparator();
        net->Add( PCB_ACTIONS::showNetInspector, ACTION_MENU::NORMAL, _( "&Properties..." ) );
        addSubmenu( menu, net, sel.connected > 0 );
    }

    // Pad Actions: custom pad shapes are edited in the footprint editor
    ACTION_MENU* pad = submenu( aTool, _( "Pad Actions" ) );
    enable( pad->Add( PCB_ACTIONS::recombinePad, ACTION_MENU::NORMAL,
                      _( "Add Selected Region to Custom Pad" ) ),
            !board && sel.pads > 0 );
    enable( pad->Add( PCB_ACTIONS::explodePad, ACTION_MENU::NORMAL,
                      _( "Explode Custom Pad to Free Primitives" ) ),
            !board && sel.pads > 0 );
    addSubmenu( menu, pad, sel.pads > 0 );

    ACTION_MENU* align = BuildPcbAlignMenu( aTool );
    align->SetTitle( _( "&Align" ) );
    addSubmenu( menu, align, sel.total > 0 );

    if( board )
    {
        enable( menu->Add( PCB_ACTIONS::selectOnSchematic, ACTION_MENU::NORMAL, _( "&Cross Probe" ) ),
                sel.firstFootprint != nullptr || sel.connected > 0 );
    }

    menu->AppendSeparator();
    enable( menu->Add( ACTIONS::cut,  ACTION_MENU::NORMAL, _( "Cu&t" ) ), sel.total > 0 );
    enable( menu->Add( ACTIONS::copy, ACTION_MENU::NORMAL, _( "&Copy" ) ), sel.total > 0 );
    menu->Add( ACTIONS::paste, ACTION_MENU::NORMAL, _( "&Paste" ) );

    // Altium's review comments have no KiCad counterpart
    menu->AppendSeparator();
    menu->Append( wxID_ANY, _( "Comment..." ) )->Enable( false );

    if( board )
    {
        menu->AppendSeparator();
        enable( menu->Add( PCB_ACTIONS::inspectConstraints, ACTION_MENU::NORMAL,
                           _( "Applicable Unary Rules..." ) ),
                sel.total == 1 );
        enable( menu->Add( PCB_ACTIONS::inspectClearance, ACTION_MENU::NORMAL,
                           _( "Applicable &Binary Rules..." ) ),
                sel.total == 2 );
    }

    menu->AppendSeparator();
    menu->AddPlain( ACTIONS::openPreferences, _( "&Preferences..." ) );
    menu->AppendSeparator();
    enable( menu->AddPlain( PCB_ACTIONS::properties, _( "P&roperties..." ) ), sel.total > 0 );

    // KiCad-only
    if( sel.total > 0 )
    {
        menu->AppendSeparator();
        menu->Add( PCB_ACTIONS::rotateCcw );
        menu->Add( PCB_ACTIONS::flip );
        menu->Add( ACTIONS::duplicate );
        menu->Add( ACTIONS::doDelete );
        menu->Add( PCB_ACTIONS::toggleLock );

        if( board && sel.footprints > 0 )
        {
            menu->Add( PCB_ACTIONS::editFpInFpEditor );
            menu->Add( PCB_ACTIONS::changeFootprint );
        }
    }

    return menu;
}
