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

#include "pcb_swap_room_tool.h"

#include <algorithm>
#include <map>

#include <bitmaps.h>
#include <board.h>
#include <board_commit.h>
#include <board_design_settings.h>
#include <component_classes/component_class.h>
#include <footprint.h>
#include <pad.h>
#include <pcb_base_edit_frame.h>
#include <pcb_group.h>
#include <zone.h>
#include <zone_settings.h>
#include <zones.h>
#include <autorouter/spread_footprints.h>
#include <geometry/convex_hull.h>
#include <geometry/shape_utils.h>
#include <gal/graphics_abstraction_layer.h>
#include <kiplatform/ui.h>
#include <preview_items/polygon_item.h>
#include <status_popup.h>
#include <tool/tool_manager.h>
#include <tools/multichannel_tool.h>
#include <tools/pcb_actions.h>
#include <tools/pcb_grid_helper.h>
#include <tools/pcb_selection_conditions.h>
#include <tools/pcb_selection_tool.h>
#include <view/view.h>
#include <view/view_controls.h>


// ---------------------------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------------------------

namespace
{

bool isRoom( const BOARD_ITEM* aItem )
{
    if( !aItem || aItem->Type() != PCB_ZONE_T )
        return false;

    const ZONE* zone = static_cast<const ZONE*>( aItem );
    return zone->GetIsRuleArea() && zone->GetPlacementAreaEnabled();
}


wxString padLabel( const PAD* aPad )
{
    const FOOTPRINT* fp = aPad->GetParentFootprint();
    return wxString::Format( wxT( "%s.%s" ), fp ? fp->GetReference() : wxString( wxT( "?" ) ),
                             aPad->GetNumber() );
}


/// RAII prompt that follows the mouse, like the picker tool's.
class CURSOR_PROMPT
{
public:
    CURSOR_PROMPT( PCB_BASE_EDIT_FRAME* aFrame, EDA_DRAW_PANEL_GAL* aCanvas ) :
            m_popup( aFrame ),
            m_canvas( aCanvas )
    {
        m_popup.Move( KIPLATFORM::UI::GetMousePosition() + wxPoint( 20, -50 ) );
        m_popup.Popup();
        m_canvas->SetStatusPopup( m_popup.GetPanel() );
    }

    ~CURSOR_PROMPT()
    {
        m_canvas->SetStatusPopup( nullptr );
        m_popup.Hide();
    }

    void SetText( const wxString& aText ) { m_popup.SetText( aText ); }
    void Follow() { m_popup.Move( KIPLATFORM::UI::GetMousePosition() + wxPoint( 20, -50 ) ); }

private:
    STATUS_TEXT_POPUP   m_popup;
    EDA_DRAW_PANEL_GAL* m_canvas;
};

} // namespace


// ---------------------------------------------------------------------------------------------
// Tool
// ---------------------------------------------------------------------------------------------

PCB_SWAP_ROOM_TOOL::PCB_SWAP_ROOM_TOOL() :
        PCB_TOOL_BASE( "pcbnew.SwapRoom" )
{
}


bool PCB_SWAP_ROOM_TOOL::Init()
{
    PCB_SELECTION_TOOL* selTool = m_toolMgr->GetTool<PCB_SELECTION_TOOL>();

    if( !selTool )
        return true;

    CONDITIONAL_MENU& menu = selTool->GetToolMenu().GetMenu();

    // Right-click > Pin/Part Swapping, offered when a pad is under the selection
    auto swapMenu = std::make_shared<CONDITIONAL_MENU>( this );
    swapMenu->SetUntranslatedTitle( _HKI( "Pin/Part Swapping" ) );
    swapMenu->SetIcon( BITMAPS::swap );
    swapMenu->AddItem( PCB_SWAP_ROOM_ACTIONS::interactivePinSwap, SELECTION_CONDITIONS::ShowAlways );
    swapMenu->AddItem( PCB_SWAP_ROOM_ACTIONS::interactivePartSwap, SELECTION_CONDITIONS::ShowAlways );
    selTool->GetToolMenu().RegisterSubMenu( swapMenu );

    // Right-click > Room Actions, offered on a room or on a set of components
    auto roomMenu = std::make_shared<CONDITIONAL_MENU>( this );
    roomMenu->SetUntranslatedTitle( _HKI( "Room Actions" ) );
    roomMenu->SetIcon( BITMAPS::add_keepout_area );

    auto onRoom =
            []( const SELECTION& aSel )
            {
                return aSel.Size() == 1 && isRoom( static_cast<BOARD_ITEM*>( aSel.Front() ) );
            };

    auto onFootprints = SELECTION_CONDITIONS::HasType( PCB_FOOTPRINT_T );

    roomMenu->AddItem( PCB_SWAP_ROOM_ACTIONS::moveRoom, onRoom );
    roomMenu->AddItem( PCB_SWAP_ROOM_ACTIONS::arrangeWithinRoom, onRoom );
    roomMenu->AddItem( PCB_ACTIONS::repeatLayout, onRoom );
    roomMenu->AddItem( PCB_SWAP_ROOM_ACTIONS::roomFromSelectionRect, onFootprints );
    roomMenu->AddItem( PCB_SWAP_ROOM_ACTIONS::roomFromSelectionOrthogonal, onFootprints );
    roomMenu->AddItem( PCB_SWAP_ROOM_ACTIONS::roomFromSelectionHull, onFootprints );
    selTool->GetToolMenu().RegisterSubMenu( roomMenu );

    auto hasPad = SELECTION_CONDITIONS::HasType( PCB_PAD_T );

    menu.AddMenu( swapMenu.get(), hasPad, 100 );
    menu.AddMenu( roomMenu.get(), SELECTION_CONDITION( onRoom ) || onFootprints, 100 );

    return true;
}


// ------------------------------------- Pin / part swapping -----------------------------------

PAD* PCB_SWAP_ROOM_TOOL::padAt( const VECTOR2I& aPos ) const
{
    PCB_LAYER_ID active = frame()->GetActiveLayer();
    PAD*         best = nullptr;
    bool         bestOnActive = false;

    for( FOOTPRINT* fp : board()->Footprints() )
    {
        if( !fp->GetBoundingBox( false ).Contains( aPos ) )
            continue;

        for( PAD* pad : fp->Pads() )
        {
            if( !pad->IsOnCopperLayer() || !pad->HitTest( aPos ) )
                continue;

            bool onActive = pad->IsOnLayer( active );

            if( !best || ( onActive && !bestOnActive ) )
            {
                best = pad;
                bestOnActive = onActive;
            }
        }
    }

    return best;
}


int PCB_SWAP_ROOM_TOOL::unitOfPad( const PAD* aPad )
{
    const FOOTPRINT* fp = aPad->GetParentFootprint();

    if( !fp )
        return -1;

    const auto& units = fp->GetUnitInfo();

    for( size_t i = 0; i < units.size(); ++i )
    {
        for( const wxString& pin : units[i].m_pins )
        {
            if( pin == aPad->GetNumber() )
                return static_cast<int>( i );
        }
    }

    return -1;
}


int PCB_SWAP_ROOM_TOOL::InteractivePinSwap( const TOOL_EVENT& aEvent )
{
    return swapLoop( aEvent, false );
}


int PCB_SWAP_ROOM_TOOL::InteractivePartSwap( const TOOL_EVENT& aEvent )
{
    return swapLoop( aEvent, true );
}


int PCB_SWAP_ROOM_TOOL::swapLoop( const TOOL_EVENT& aEvent, bool aGateMode )
{
    if( IsFootprintEditor() )
        return 0;

    PCB_SELECTION_TOOL*   selTool = m_toolMgr->GetTool<PCB_SELECTION_TOOL>();
    KIGFX::VIEW_CONTROLS* controls = getViewControls();
    PAD*                  first = nullptr;

    const wxString firstPrompt = aGateMode ? _( "Click a pin of the first gate (part) to swap" )
                                           : _( "Click the first pin to swap" );

    frame()->PushTool( aEvent );
    Activate();

    m_toolMgr->RunAction( ACTIONS::selectionClear );
    controls->ShowCursor( true );
    controls->ForceCursorPosition( false );

    CURSOR_PROMPT prompt( frame(), canvas() );
    prompt.SetText( firstPrompt );

    auto highlightFirst =
            [&]()
            {
                m_toolMgr->RunAction( ACTIONS::selectionClear );

                if( first )
                {
                    EDA_ITEMS items = { first };
                    selTool->AddItemsToSel( &items, true );
                }
            };

    auto reset =
            [&]()
            {
                first = nullptr;
                highlightFirst();
                prompt.SetText( firstPrompt );
            };

    while( TOOL_EVENT* evt = Wait() )
    {
        frame()->GetCanvas()->SetCurrentCursor( KICURSOR::BULLSEYE );

        VECTOR2I pos = evt->HasPosition() ? evt->Position() : controls->GetMousePosition();

        if( evt->IsCancelInteractive() || evt->IsClick( BUT_RIGHT ) )
        {
            // First Esc drops the half-made swap, the next one ends the tool
            if( first )
            {
                reset();
                continue;
            }

            break;
        }
        else if( evt->IsActivate() )
        {
            break;
        }
        else if( evt->IsMotion() )
        {
            prompt.Follow();
        }
        else if( evt->IsClick( BUT_LEFT ) )
        {
            PAD* pad = padAt( pos );

            if( !pad )
            {
                wxBell();
                continue;
            }

            if( !first || pad == first )
            {
                if( pad == first )
                {
                    reset();
                    continue;
                }

                if( aGateMode )
                {
                    const FOOTPRINT* fp = pad->GetParentFootprint();

                    if( !fp || fp->GetUnitInfo().size() < 2 || unitOfPad( pad ) < 0 )
                    {
                        frame()->ShowInfoBarError( _( "This component has only one part; part "
                                                      "swapping needs a multi-part component." ) );
                        continue;
                    }
                }

                first = pad;
                highlightFirst();

                if( aGateMode )
                {
                    const FOOTPRINT* fp = pad->GetParentFootprint();
                    prompt.SetText( wxString::Format( _( "Click a pin of the part to swap with "
                                                         "%s part %s" ),
                                                      fp->GetReference(),
                                                      fp->GetUnitInfo()[unitOfPad( pad )].m_unitName ) );
                }
                else
                {
                    prompt.SetText( wxString::Format( _( "Click the pin to swap with %s (%s)" ),
                                                      padLabel( pad ), pad->GetNetname() ) );
                }

                continue;
            }

            // Second click
            if( pad->GetParentFootprint() != first->GetParentFootprint() )
            {
                frame()->ShowInfoBarError( _( "Both pins must belong to the same component." ) );
                continue;
            }

            if( aGateMode )
            {
                int unitA = unitOfPad( first );
                int unitB = unitOfPad( pad );

                if( unitB < 0 || unitA == unitB )
                {
                    frame()->ShowInfoBarError( _( "Click a pin of a different part of the same "
                                                  "component." ) );
                    continue;
                }
            }
            else if( pad->GetNetCode() == first->GetNetCode() )
            {
                frame()->ShowInfoBarError( _( "Both pins are on the same net; nothing to swap." ) );
                continue;
            }

            // Hand the ordered pair to KiCad's swap commands, which commit with undo.
            m_toolMgr->RunAction( ACTIONS::selectionClear );
            EDA_ITEMS pair = { first, pad };
            selTool->AddItemsToSel( &pair, true );

            if( aGateMode )
                m_toolMgr->RunAction( PCB_ACTIONS::swapGateNets );
            else
                m_toolMgr->RunAction( PCB_ACTIONS::swapPadNets );

            reset();
        }
        else
        {
            evt->SetPassEvent();
        }
    }

    m_toolMgr->RunAction( ACTIONS::selectionClear );
    frame()->GetCanvas()->SetCurrentCursor( KICURSOR::ARROW );
    frame()->PopTool( aEvent );
    return 0;
}


// ------------------------------------------- Rooms -------------------------------------------

wxString PCB_SWAP_ROOM_TOOL::uniqueRoomName( const wxString& aBase ) const
{
    std::set<wxString> used;

    for( ZONE* zone : board()->Zones() )
        used.insert( zone->GetZoneName() );

    wxString base = aBase.IsEmpty() ? wxString( wxT( "Room" ) ) : aBase;

    if( !aBase.IsEmpty() && !used.count( base ) )
        return base;

    for( int i = 1;; ++i )
    {
        wxString name = wxString::Format( wxT( "%s%d" ), base, i );

        if( !used.count( name ) )
            return name;
    }
}


void PCB_SWAP_ROOM_TOOL::initRoomSettings( ZONE_SETTINGS&                 aSettings,
                                           const std::vector<FOOTPRINT*>& aMembers )
{
    aSettings = board()->GetDesignSettings().GetDefaultZoneSettings();

    aSettings.SetIsRuleArea( true );
    aSettings.m_Layers = LSET::AllCuMask( board()->GetCopperLayerCount() );
    aSettings.m_LayerProperties.clear();
    aSettings.m_Netcode = -1;
    aSettings.m_Locked = false;
    aSettings.m_ZoneBorderDisplayStyle = ZONE_BORDER_DISPLAY_STYLE::NO_HATCH;

    // A room only governs placement: no keepouts
    aSettings.SetDoNotAllowZoneFills( false );
    aSettings.SetDoNotAllowVias( false );
    aSettings.SetDoNotAllowTracks( false );
    aSettings.SetDoNotAllowPads( false );
    aSettings.SetDoNotAllowFootprints( false );

    aSettings.SetPlacementAreaEnabled( true );
    aSettings.SetPlacementAreaSourceType( PLACEMENT_SOURCE_T::SHEETNAME );
    aSettings.SetPlacementAreaSource( wxEmptyString );

    wxString nameBase;

    if( !aMembers.empty() )
    {
        // Prefer the sheet all members share, then a component class they share, then a
        // named group they share.
        std::set<wxString> sheets;

        for( FOOTPRINT* fp : aMembers )
            sheets.insert( fp->GetSheetname() );

        std::set<wxString> commonClasses;
        bool               firstFp = true;

        for( FOOTPRINT* fp : aMembers )
        {
            std::set<wxString> classes;

            if( const COMPONENT_CLASS* cc = fp->GetComponentClass() )
            {
                for( const COMPONENT_CLASS* single : cc->GetConstituentClasses() )
                    classes.insert( single->GetName() );
            }

            if( firstFp )
            {
                commonClasses = classes;
                firstFp = false;
            }
            else
            {
                std::set<wxString> both;
                std::set_intersection( commonClasses.begin(), commonClasses.end(), classes.begin(),
                                       classes.end(), std::inserter( both, both.begin() ) );
                commonClasses = both;
            }
        }

        EDA_GROUP* commonGroup = aMembers.front()->GetParentGroup();

        for( FOOTPRINT* fp : aMembers )
        {
            if( fp->GetParentGroup() != commonGroup )
                commonGroup = nullptr;
        }

        if( sheets.size() == 1 && *sheets.begin() != wxT( "/" ) && !sheets.begin()->IsEmpty() )
        {
            wxString sheet = *sheets.begin();
            aSettings.SetPlacementAreaSourceType( PLACEMENT_SOURCE_T::SHEETNAME );
            aSettings.SetPlacementAreaSource( sheet );

            wxString trimmed = sheet;
            trimmed.Trim().Trim( false );

            while( trimmed.EndsWith( wxT( "/" ) ) )
                trimmed.RemoveLast();

            nameBase = trimmed.AfterLast( '/' );
        }
        else if( !commonClasses.empty() )
        {
            aSettings.SetPlacementAreaSourceType( PLACEMENT_SOURCE_T::COMPONENT_CLASS );
            aSettings.SetPlacementAreaSource( *commonClasses.begin() );
            nameBase = *commonClasses.begin();
        }
        else if( commonGroup && !commonGroup->GetName().IsEmpty() )
        {
            aSettings.SetPlacementAreaSourceType( PLACEMENT_SOURCE_T::GROUP_PLACEMENT );
            aSettings.SetPlacementAreaSource( commonGroup->GetName() );
            nameBase = commonGroup->GetName();
        }
        else if( sheets.size() == 1 )
        {
            aSettings.SetPlacementAreaSource( *sheets.begin() );
        }
    }

    aSettings.m_Name = uniqueRoomName( nameBase );
}


bool PCB_SWAP_ROOM_TOOL::editRoomSettings( ZONE_SETTINGS& aSettings )
{
    // With no keepouts set and placement enabled, the dialog opens on its Placement page,
    // where the room is bound to a sheet, component class or group.
    return InvokeRuleAreaEditor( frame(), &aSettings, board() ) == wxID_OK;
}


ZONE* PCB_SWAP_ROOM_TOOL::commitRoom( const ZONE_SETTINGS& aSettings, const SHAPE_LINE_CHAIN& aOutline )
{
    if( aOutline.PointCount() < 3 )
        return nullptr;

    ZONE* room = new ZONE( board() );
    aSettings.ExportSetting( *room );

    SHAPE_LINE_CHAIN chain( aOutline );
    chain.SetClosed( true );
    chain.Simplify( true );

    room->Outline()->RemoveAllContours();
    room->Outline()->AddOutline( chain );
    room->HatchBorder();

    BOARD_COMMIT commit( this );
    commit.Add( room );
    commit.Push( _( "Place Room" ) );

    if( !board()->IsElementVisible( LAYER_ZONES ) )
        frame()->SetObjectVisible( LAYER_ZONES );

    m_toolMgr->RunAction( ACTIONS::selectionClear );
    m_toolMgr->RunAction<EDA_ITEM*>( ACTIONS::selectItem, room );
    return room;
}


int PCB_SWAP_ROOM_TOOL::PlaceRoom( const TOOL_EVENT& aEvent )
{
    if( IsFootprintEditor() )
        return 0;

    const bool rectMode = aEvent.IsAction( &PCB_SWAP_ROOM_ACTIONS::placeRectangularRoom );

    ZONE_SETTINGS settings;
    initRoomSettings( settings, {} );

    if( !editRoomSettings( settings ) )
        return 0;

    KIGFX::VIEW*          view = getView();
    KIGFX::VIEW_CONTROLS* controls = getViewControls();
    PCB_GRID_HELPER       grid( m_toolMgr, frame()->GetMagneticItemsSettings() );

    KIGFX::PREVIEW::POLYGON_ITEM preview;
    preview.SetStrokeColor( KIGFX::COLOR4D::WHITE );
    preview.SetFillColor( KIGFX::COLOR4D( 0.2, 0.6, 1.0, 0.2 ) );
    view->Add( &preview );
    view->SetVisible( &preview, false );

    SHAPE_LINE_CHAIN points;
    VECTOR2I         cursor;
    bool             finished = false;

    frame()->PushTool( aEvent );
    Activate();

    m_toolMgr->RunAction( ACTIONS::selectionClear );
    controls->ShowCursor( true );

    CURSOR_PROMPT prompt( frame(), canvas() );

    auto setPrompt =
            [&]()
            {
                if( rectMode )
                    prompt.SetText( points.PointCount() == 0 ? _( "Click the first corner of the room" )
                                                             : _( "Click the opposite corner" ) );
                else
                    prompt.SetText( points.PointCount() == 0
                                            ? _( "Click the first vertex of the room" )
                                            : _( "Click the next vertex; right-click, double-click "
                                                 "or click the first vertex to finish; Backspace "
                                                 "removes the last vertex" ) );
            };

    auto rectChain =
            [&]( const VECTOR2I& a, const VECTOR2I& b )
            {
                SHAPE_LINE_CHAIN r;
                r.Append( a.x, a.y );
                r.Append( b.x, a.y );
                r.Append( b.x, b.y );
                r.Append( a.x, b.y );
                return r;
            };

    auto updatePreview =
            [&]()
            {
                if( points.PointCount() == 0 )
                {
                    view->SetVisible( &preview, false );
                    return;
                }

                if( rectMode )
                {
                    SHAPE_LINE_CHAIN r = rectChain( points.CPoint( 0 ), cursor );
                    SHAPE_LINE_CHAIN loop;
                    loop.Append( r.CPoint( 3 ) );
                    loop.Append( r.CPoint( 0 ) );
                    preview.SetPoints( r, SHAPE_LINE_CHAIN(), loop );
                }
                else
                {
                    SHAPE_LINE_CHAIN leader, loop;
                    leader.Append( points.CLastPoint() );
                    leader.Append( cursor );
                    loop.Append( cursor );
                    loop.Append( points.CPoint( 0 ) );
                    preview.SetPoints( points, leader, loop );
                }

                view->SetVisible( &preview, true );
                view->Update( &preview );
            };

    auto restart =
            [&]()
            {
                points.Clear();
                controls->SetAutoPan( false );
                controls->CaptureCursor( false );
                updatePreview();
                setPrompt();
            };

    setPrompt();

    while( TOOL_EVENT* evt = Wait() )
    {
        frame()->GetCanvas()->SetCurrentCursor( KICURSOR::PENCIL );

        grid.SetSnap( !evt->Modifier( MD_SHIFT ) );
        grid.SetUseGrid( view->GetGAL()->GetGridSnapping() && !evt->DisableGridSnapping() );

        VECTOR2I pos = evt->HasPosition() ? evt->Position() : controls->GetMousePosition();
        cursor = grid.BestSnapAnchor( pos, LSET( { frame()->GetActiveLayer() } ), GRID_GRAPHICS );
        controls->ForceCursorPosition( true, cursor );

        const bool enterKey = evt->IsKeyPressed()
                              && ( evt->KeyCode() == WXK_RETURN || evt->KeyCode() == WXK_NUMPAD_ENTER );
        const bool backKey = evt->IsKeyPressed() && evt->KeyCode() == WXK_BACK;

        if( evt->IsCancelInteractive() || evt->IsClick( BUT_RIGHT ) || enterKey )
        {
            // A polygon with three or more vertices is finished by right-click, Esc or Enter
            if( !rectMode && points.PointCount() >= 3 )
            {
                finished = true;
                break;
            }

            if( points.PointCount() > 0 )
            {
                restart();
                continue;
            }

            if( enterKey )
                continue;

            break;
        }
        else if( evt->IsActivate() )
        {
            break;
        }
        else if( evt->IsClick( BUT_LEFT ) || evt->IsDblClick( BUT_LEFT )
                 || evt->IsAction( &PCB_ACTIONS::closeOutline ) )
        {
            if( rectMode )
            {
                if( points.PointCount() == 0 )
                {
                    points.Append( cursor );
                    controls->SetAutoPan( true );
                    controls->CaptureCursor( true );
                }
                else if( cursor.x != points.CPoint( 0 ).x && cursor.y != points.CPoint( 0 ).y )
                {
                    points = rectChain( points.CPoint( 0 ), cursor );
                    finished = true;
                    break;
                }
            }
            else
            {
                int  closeDist = KiROUND( view->ToWorld( 8 ) );
                bool closing = points.PointCount() >= 3
                               && ( evt->IsDblClick( BUT_LEFT )
                                    || evt->IsAction( &PCB_ACTIONS::closeOutline )
                                    || ( cursor - points.CPoint( 0 ) ).EuclideanNorm() <= closeDist );

                if( closing )
                {
                    finished = true;
                    break;
                }

                if( evt->IsClick( BUT_LEFT )
                    && ( points.PointCount() == 0 || points.CLastPoint() != cursor ) )
                {
                    points.Append( cursor );
                    controls->SetAutoPan( true );
                    controls->CaptureCursor( true );
                }
            }

            updatePreview();
            setPrompt();
        }
        else if( !rectMode && points.PointCount() > 0
                 && ( backKey || evt->IsAction( &PCB_ACTIONS::deleteLastPoint )
                      || evt->IsAction( &ACTIONS::doDelete ) || evt->IsAction( &ACTIONS::undo ) ) )
        {
            points.Remove( points.PointCount() - 1 );

            if( points.PointCount() == 0 )
                restart();
            else
                updatePreview();
        }
        else if( evt->IsMotion() || evt->IsDrag( BUT_LEFT ) )
        {
            prompt.Follow();
            updatePreview();
        }
        else
        {
            evt->SetPassEvent();
        }
    }

    view->SetVisible( &preview, false );
    view->Remove( &preview );

    controls->SetAutoPan( false );
    controls->CaptureCursor( false );
    controls->ForceCursorPosition( false );
    frame()->GetCanvas()->SetCurrentCursor( KICURSOR::ARROW );

    if( finished )
        commitRoom( settings, points );

    frame()->PopTool( aEvent );
    return 0;
}


int PCB_SWAP_ROOM_TOOL::RoomFromSelection( const TOOL_EVENT& aEvent )
{
    if( IsFootprintEditor() )
        return 0;

    std::vector<FOOTPRINT*> fps;

    for( EDA_ITEM* item : selection() )
    {
        if( item->Type() == PCB_FOOTPRINT_T )
            fps.push_back( static_cast<FOOTPRINT*>( item ) );
        else if( item->Type() == PCB_PAD_T )
        {
            if( FOOTPRINT* parent = static_cast<PAD*>( item )->GetParentFootprint() )
            {
                if( std::find( fps.begin(), fps.end(), parent ) == fps.end() )
                    fps.push_back( parent );
            }
        }
    }

    if( fps.empty() )
    {
        frame()->ShowInfoBarError( _( "Select the components the room should enclose first." ) );
        return 0;
    }

    const int margin = pcbIUScale.mmToIU( 0.5 );

    std::vector<VECTOR2I> corners;
    BOX2I                 bbox;

    for( FOOTPRINT* fp : fps )
    {
        BOX2I fpBox = fp->GetBoundingBox( false ).GetInflated( margin );
        KIGEOM::CollectBoxCorners( fpBox, corners );
        bbox.Merge( fpBox );
    }

    SHAPE_LINE_CHAIN outline;

    if( aEvent.IsAction( &PCB_SWAP_ROOM_ACTIONS::roomFromSelectionRect ) )
    {
        outline.Append( bbox.GetLeft(), bbox.GetTop() );
        outline.Append( bbox.GetRight(), bbox.GetTop() );
        outline.Append( bbox.GetRight(), bbox.GetBottom() );
        outline.Append( bbox.GetLeft(), bbox.GetBottom() );
    }
    else
    {
        std::vector<VECTOR2I> hull;
        BuildConvexHull( hull, corners );
        outline = SHAPE_LINE_CHAIN( hull );

        if( aEvent.IsAction( &PCB_SWAP_ROOM_ACTIONS::roomFromSelectionOrthogonal ) )
            outline = KIGEOM::RectifyPolygon( outline );
    }

    outline.SetClosed( true );

    ZONE_SETTINGS settings;
    initRoomSettings( settings, fps );

    if( !editRoomSettings( settings ) )
        return 0;

    commitRoom( settings, outline );
    return 0;
}


ZONE* PCB_SWAP_ROOM_TOOL::selectedRoom() const
{
    const PCB_SELECTION& sel = const_cast<PCB_SWAP_ROOM_TOOL*>( this )->selection();

    if( sel.Size() == 1 && isRoom( static_cast<BOARD_ITEM*>( sel.Front() ) ) )
        return static_cast<ZONE*>( sel.Front() );

    return nullptr;
}


ZONE* PCB_SWAP_ROOM_TOOL::roomAt( const VECTOR2I& aPos ) const
{
    ZONE*  best = nullptr;
    double bestArea = 0.0;
    int    accuracy = KiROUND( getView()->ToWorld( 6 ) );

    for( ZONE* zone : board()->Zones() )
    {
        if( !isRoom( zone ) )
            continue;

        if( !zone->Outline()->Contains( aPos ) && !zone->HitTestForEdge( aPos, accuracy ) )
            continue;

        double area = zone->Outline()->Area();

        if( !best || area < bestArea )
        {
            best = zone;
            bestArea = area;
        }
    }

    return best;
}


ZONE* PCB_SWAP_ROOM_TOOL::pickRoom( const TOOL_EVENT& aEvent, const wxString& aPrompt )
{
    if( ZONE* room = selectedRoom() )
        return room;

    bool hasRooms = false;

    for( ZONE* zone : board()->Zones() )
        hasRooms |= isRoom( zone );

    if( !hasRooms )
    {
        frame()->ShowInfoBarError( _( "The board has no rooms (placement rule areas)." ) );
        return nullptr;
    }

    KIGFX::VIEW_CONTROLS* controls = getViewControls();
    ZONE*                 picked = nullptr;

    frame()->PushTool( aEvent );
    Activate();

    m_toolMgr->RunAction( ACTIONS::selectionClear );
    controls->ShowCursor( true );

    {
        CURSOR_PROMPT prompt( frame(), canvas() );
        prompt.SetText( aPrompt );

        while( TOOL_EVENT* evt = Wait() )
        {
            frame()->GetCanvas()->SetCurrentCursor( KICURSOR::BULLSEYE );

            if( evt->IsCancelInteractive() || evt->IsActivate() || evt->IsClick( BUT_RIGHT ) )
                break;

            if( evt->IsMotion() )
            {
                prompt.Follow();
            }
            else if( evt->IsClick( BUT_LEFT ) )
            {
                picked = roomAt( evt->Position() );

                if( picked )
                    break;

                wxBell();
            }
            else
            {
                evt->SetPassEvent();
            }
        }
    }

    frame()->GetCanvas()->SetCurrentCursor( KICURSOR::ARROW );
    frame()->PopTool( aEvent );
    return picked;
}


std::set<FOOTPRINT*> PCB_SWAP_ROOM_TOOL::roomMembers( ZONE* aRoom )
{
    std::set<FOOTPRINT*> members;

    if( MULTICHANNEL_TOOL* mc = m_toolMgr->GetTool<MULTICHANNEL_TOOL>() )
    {
        mc->FindExistingRuleAreas();

        for( const RULE_AREA& ra : mc->GetData()->m_areas )
        {
            if( ra.m_zone == aRoom )
                members = ra.m_components;
        }
    }

    return members;
}


int PCB_SWAP_ROOM_TOOL::ArrangeWithinRoom( const TOOL_EVENT& aEvent )
{
    if( IsFootprintEditor() )
        return 0;

    ZONE* room = pickRoom( aEvent, _( "Click the room whose components should be arranged" ) );

    if( !room )
        return 0;

    std::vector<FOOTPRINT*> fps;

    for( FOOTPRINT* fp : roomMembers( room ) )
    {
        if( !fp->IsLocked() )
            fps.push_back( fp );
    }

    if( fps.empty() )
    {
        frame()->ShowInfoBarError( wxString::Format( _( "Room '%s' has no unlocked components. "
                                                        "Check its Placement settings." ),
                                                     room->GetZoneName() ) );
        return 0;
    }

    const BOX2I roomBox = room->Outline()->BBox();
    const int   inset = pcbIUScale.mmToIU( 0.5 );

    BOARD_COMMIT commit( this );

    for( FOOTPRINT* fp : fps )
        commit.Modify( fp );

    SpreadFootprints( &fps, roomBox.GetOrigin() + VECTOR2I( inset, inset ), false );

    // Centre the packed block in the room when it fits; otherwise keep it at the room's
    // top-left corner and say so.
    BOX2I packed;

    for( FOOTPRINT* fp : fps )
        packed.Merge( fp->GetBoundingBox( false ) );

    bool fits = packed.GetWidth() <= roomBox.GetWidth() && packed.GetHeight() <= roomBox.GetHeight();

    VECTOR2I shift = fits ? roomBox.Centre() - packed.Centre()
                          : roomBox.GetOrigin() + VECTOR2I( inset, inset ) - packed.GetOrigin();

    if( shift != VECTOR2I( 0, 0 ) )
    {
        for( FOOTPRINT* fp : fps )
            fp->Move( shift );
    }

    commit.Push( _( "Arrange Components Within Room" ) );

    if( !fits )
    {
        frame()->ShowInfoBarWarning( wxString::Format( _( "The components of room '%s' do not "
                                                          "fit inside it." ),
                                                       room->GetZoneName() ) );
    }

    return 0;
}


int PCB_SWAP_ROOM_TOOL::MoveRoom( const TOOL_EVENT& aEvent )
{
    if( IsFootprintEditor() )
        return 0;

    ZONE* room = pickRoom( aEvent, _( "Click the room to move" ) );

    if( !room )
        return 0;

    PCB_SELECTION_TOOL* selTool = m_toolMgr->GetTool<PCB_SELECTION_TOOL>();
    EDA_ITEMS           items = { room };

    // The room's components that sit inside it travel with it.
    for( FOOTPRINT* fp : roomMembers( room ) )
    {
        if( !room->Outline()->Contains( fp->GetBoundingBox( false ).Centre() ) )
            continue;

        // A grouped footprint is moved through its group
        EDA_ITEM* top = fp;

        while( top->GetParentGroup() )
            top = top->GetParentGroup()->AsEdaItem();

        if( std::find( items.begin(), items.end(), top ) == items.end()
            && selTool->Selectable( static_cast<BOARD_ITEM*>( top ) ) )
        {
            items.push_back( top );
        }
    }

    m_toolMgr->RunAction( ACTIONS::selectionClear );
    selTool->AddItemsToSel( &items, true );
    m_toolMgr->PostAction( PCB_ACTIONS::move );
    return 0;
}


void PCB_SWAP_ROOM_TOOL::setTransitions()
{
    Go( &PCB_SWAP_ROOM_TOOL::InteractivePinSwap,  PCB_SWAP_ROOM_ACTIONS::interactivePinSwap.MakeEvent() );
    Go( &PCB_SWAP_ROOM_TOOL::InteractivePartSwap, PCB_SWAP_ROOM_ACTIONS::interactivePartSwap.MakeEvent() );
    Go( &PCB_SWAP_ROOM_TOOL::PlaceRoom,           PCB_SWAP_ROOM_ACTIONS::placeRectangularRoom.MakeEvent() );
    Go( &PCB_SWAP_ROOM_TOOL::PlaceRoom,           PCB_SWAP_ROOM_ACTIONS::placePolygonalRoom.MakeEvent() );
    Go( &PCB_SWAP_ROOM_TOOL::RoomFromSelection,   PCB_SWAP_ROOM_ACTIONS::roomFromSelectionRect.MakeEvent() );
    Go( &PCB_SWAP_ROOM_TOOL::RoomFromSelection,   PCB_SWAP_ROOM_ACTIONS::roomFromSelectionOrthogonal.MakeEvent() );
    Go( &PCB_SWAP_ROOM_TOOL::RoomFromSelection,   PCB_SWAP_ROOM_ACTIONS::roomFromSelectionHull.MakeEvent() );
    Go( &PCB_SWAP_ROOM_TOOL::ArrangeWithinRoom,   PCB_SWAP_ROOM_ACTIONS::arrangeWithinRoom.MakeEvent() );
    Go( &PCB_SWAP_ROOM_TOOL::MoveRoom,            PCB_SWAP_ROOM_ACTIONS::moveRoom.MakeEvent() );
}


// ---------------------------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------------------------

// As in pcb_actions.cpp: statically-defined actions keep their strings untranslated here and
// translate in the getters, so _() must not call wxGetTranslation.
#undef _
#define _(s) s

TOOL_ACTION PCB_SWAP_ROOM_ACTIONS::interactivePinSwap( TOOL_ACTION_ARGS()
        .Name( "pcbnew.SwapRoom.interactivePinSwap" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Interactive Pin/Net Swapping" ) )
        .Tooltip( _( "Click a pin, then another pin of the same component, to swap their nets" ) )
        .Flags( AF_ACTIVATE )
        .Icon( BITMAPS::swap ) );

TOOL_ACTION PCB_SWAP_ROOM_ACTIONS::interactivePartSwap( TOOL_ACTION_ARGS()
        .Name( "pcbnew.SwapRoom.interactivePartSwap" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Interactive Part Swapping" ) )
        .Tooltip( _( "Click a pin of one gate, then a pin of another gate of the same component, "
                     "to swap the gates' nets" ) )
        .Flags( AF_ACTIVATE )
        .Icon( BITMAPS::swap ) );

TOOL_ACTION PCB_SWAP_ROOM_ACTIONS::placeRectangularRoom( TOOL_ACTION_ARGS()
        .Name( "pcbnew.SwapRoom.placeRectangularRoom" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Place Rectangular Room" ) )
        .Tooltip( _( "Draw a rectangular placement rule area bound to a sheet, component class "
                     "or group" ) )
        .Flags( AF_ACTIVATE )
        .Icon( BITMAPS::add_keepout_area ) );

TOOL_ACTION PCB_SWAP_ROOM_ACTIONS::placePolygonalRoom( TOOL_ACTION_ARGS()
        .Name( "pcbnew.SwapRoom.placePolygonalRoom" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Place Polygonal Room" ) )
        .Tooltip( _( "Draw a polygonal placement rule area bound to a sheet, component class "
                     "or group" ) )
        .Flags( AF_ACTIVATE )
        .Icon( BITMAPS::add_graphical_polygon ) );

TOOL_ACTION PCB_SWAP_ROOM_ACTIONS::roomFromSelectionRect( TOOL_ACTION_ARGS()
        .Name( "pcbnew.SwapRoom.roomFromSelectionRect" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Create Rectangle Room from selected components" ) )
        .Tooltip( _( "Create a rectangular room around the selected components" ) )
        .Icon( BITMAPS::add_rectangle ) );

TOOL_ACTION PCB_SWAP_ROOM_ACTIONS::roomFromSelectionOrthogonal( TOOL_ACTION_ARGS()
        .Name( "pcbnew.SwapRoom.roomFromSelectionOrthogonal" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Create Orthogonal Room from selected components" ) )
        .Tooltip( _( "Create a room with only horizontal and vertical edges around the selected "
                     "components" ) )
        .Icon( BITMAPS::add_keepout_area ) );

TOOL_ACTION PCB_SWAP_ROOM_ACTIONS::roomFromSelectionHull( TOOL_ACTION_ARGS()
        .Name( "pcbnew.SwapRoom.roomFromSelectionHull" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Create Non-Orthogonal Room from selected components" ) )
        .Tooltip( _( "Create a room hugging the selected components (convex hull)" ) )
        .Icon( BITMAPS::add_graphical_polygon ) );

TOOL_ACTION PCB_SWAP_ROOM_ACTIONS::arrangeWithinRoom( TOOL_ACTION_ARGS()
        .Name( "pcbnew.SwapRoom.arrangeWithinRoom" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Arrange Components Within Room" ) )
        .Tooltip( _( "Pack the components that belong to a room inside it" ) )
        .Flags( AF_ACTIVATE )
        .Icon( BITMAPS::mode_module ) );

TOOL_ACTION PCB_SWAP_ROOM_ACTIONS::moveRoom( TOOL_ACTION_ARGS()
        .Name( "pcbnew.SwapRoom.moveRoom" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Move Room" ) )
        .Tooltip( _( "Move a room together with the components inside it" ) )
        .Flags( AF_ACTIVATE )
        .Icon( BITMAPS::move ) );
