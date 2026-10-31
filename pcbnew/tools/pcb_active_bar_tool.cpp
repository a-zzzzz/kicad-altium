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

#include "pcb_active_bar_tool.h"

#include <board.h>
#include <board_commit.h>
#include <footprint.h>
#include <pad.h>
#include <pcb_base_edit_frame.h>
#include <pcb_track.h>
#include <zone.h>
#include <preview_items/selection_area.h>
#include <project/board_project_settings.h>
#include <tool/tool_manager.h>
#include <tools/pcb_actions.h>
#include <tools/pcb_grid_helper.h>
#include <tools/pcb_selection_tool.h>
#include <view/view.h>
#include <view/view_controls.h>

#include <wx/menu.h>


PCB_ACTIVE_BAR_TOOL::PCB_ACTIVE_BAR_TOOL() :
        PCB_TOOL_BASE( "pcbnew.ActiveBar" )
{
}


int PCB_ACTIVE_BAR_TOOL::SelectionFilterMenu( const TOOL_EVENT& aEvent )
{
    PCB_SELECTION_TOOL*           selTool = m_toolMgr->GetTool<PCB_SELECTION_TOOL>();
    PCB_SELECTION_FILTER_OPTIONS& filter = selTool->GetFilter();

    struct ENTRY
    {
        wxString label;
        bool*    flag;
    };

    const std::vector<ENTRY> kinds = {
        { _( "Components" ),  &filter.footprints },
        { _( "Tracks" ),      &filter.tracks },
        { _( "Vias" ),        &filter.vias },
        { _( "Pads" ),        &filter.pads },
        { _( "Polygons" ),    &filter.zones },
        { _( "Keepouts" ),    &filter.keepouts },
        { _( "Texts" ),       &filter.text },
        { _( "Graphics" ),    &filter.graphics },
        { _( "Dimensions" ),  &filter.dimensions },
        { _( "Points" ),      &filter.points },
        { _( "Other" ),       &filter.otherItems },
    };

    enum { ID_ALL_ON = wxID_HIGHEST + 1, ID_ALL_OFF, ID_LOCKED, ID_FIRST_KIND };

    // Like Altium's drop-down, the menu stays up while kinds are switched on and off.
    while( true )
    {
        wxMenu menu;
        menu.Append( ID_ALL_ON, _( "All - On" ) );
        menu.Append( ID_ALL_OFF, _( "All - Off" ) );
        menu.AppendSeparator();

        for( size_t ii = 0; ii < kinds.size(); ++ii )
            menu.AppendCheckItem( ID_FIRST_KIND + (int) ii, kinds[ii].label )->Check( *kinds[ii].flag );

        menu.AppendSeparator();
        menu.AppendCheckItem( ID_LOCKED, _( "Locked Objects" ) )->Check( filter.lockedItems );

        int id = frame()->GetPopupMenuSelectionFromUser( menu );

        if( id == wxID_NONE )
            break;

        if( id == ID_ALL_ON || id == ID_ALL_OFF )
        {
            for( const ENTRY& entry : kinds )
                *entry.flag = ( id == ID_ALL_ON );
        }
        else if( id == ID_LOCKED )
        {
            filter.lockedItems = !filter.lockedItems;
        }
        else if( id >= ID_FIRST_KIND && id < ID_FIRST_KIND + (int) kinds.size() )
        {
            bool* flag = kinds[id - ID_FIRST_KIND].flag;
            *flag = !*flag;
        }

        frame()->SyncSelectionFilterPanel();
    }

    return 0;
}


int PCB_ACTIVE_BAR_TOOL::SelectArea( const TOOL_EVENT& aEvent )
{
    // The DeSelect variants take the same input and remove what they find from the selection
    const bool deselect = aEvent.IsAction( &PCB_ACTIONS::deselectLasso )
                          || aEvent.IsAction( &PCB_ACTIONS::deselectInsideArea )
                          || aEvent.IsAction( &PCB_ACTIONS::deselectOutsideArea )
                          || aEvent.IsAction( &PCB_ACTIONS::deselectTouchingRect )
                          || aEvent.IsAction( &PCB_ACTIONS::deselectTouchingLine );
    const bool lineMode = aEvent.IsAction( &PCB_ACTIONS::selectTouchingLine )
                          || aEvent.IsAction( &PCB_ACTIONS::deselectTouchingLine );
    const bool lassoMode = aEvent.IsAction( &PCB_ACTIONS::deselectLasso );
    const bool outsideMode = aEvent.IsAction( &PCB_ACTIONS::selectOutsideArea )
                             || aEvent.IsAction( &PCB_ACTIONS::deselectOutsideArea );
    const bool touchingRect = aEvent.IsAction( &PCB_ACTIONS::selectTouchingRect )
                              || aEvent.IsAction( &PCB_ACTIONS::deselectTouchingRect );

    KIGFX::VIEW*          view = getView();
    KIGFX::VIEW_CONTROLS* controls = getViewControls();
    PCB_SELECTION_TOOL*   selTool = m_toolMgr->GetTool<PCB_SELECTION_TOOL>();

    KIGFX::PREVIEW::SELECTION_AREA area;
    area.SetMode( lineMode ? SELECTION_MODE::TOUCHING_LASSO
                           : lassoMode ? SELECTION_MODE::INSIDE_LASSO
                           : touchingRect ? SELECTION_MODE::TOUCHING_RECTANGLE
                                          : SELECTION_MODE::INSIDE_RECTANGLE );

    view->Add( &area );
    view->SetVisible( &area, false );

    frame()->PushTool( aEvent );
    Activate();

    controls->ShowCursor( true );
    controls->SetAutoPan( false );

    bool     started = false;
    bool     finished = false;
    VECTOR2I origin;
    VECTOR2I end;

    auto updatePreview =
            [&]()
            {
                if( lassoMode )
                {
                    // Freehand outline: a point wherever the pointer has moved on
                    SHAPE_LINE_CHAIN& poly = area.GetPoly();

                    if( poly.PointCount() == 0
                            || ( poly.CLastPoint() - end ).EuclideanNorm() > 2.0 / view->GetScale() )
                    {
                        poly.Append( end );
                    }
                }
                else if( lineMode )
                {
                    area.GetPoly().Clear();
                    area.GetPoly().Append( origin );
                    area.GetPoly().Append( end );
                }
                else
                {
                    area.SetOrigin( origin );
                    area.SetEnd( end );
                }

                view->SetVisible( &area, true );
                view->Update( &area );
            };

    while( TOOL_EVENT* evt = Wait() )
    {
        frame()->GetCanvas()->SetCurrentCursor( lineMode || lassoMode ? KICURSOR::PENCIL
                                                                      : KICURSOR::SELECT_WINDOW );

        VECTOR2I pos = evt->HasPosition() ? evt->Position() : controls->GetMousePosition();

        if( evt->IsCancelInteractive() || evt->IsActivate() || evt->IsClick( BUT_RIGHT ) )
        {
            break;
        }
        else if( evt->IsClick( BUT_LEFT ) )
        {
            if( !started )
            {
                origin = end = pos;
                started = true;
                controls->SetAutoPan( true );
                controls->CaptureCursor( true );
                updatePreview();
            }
            else
            {
                end = pos;
                finished = true;
                break;
            }
        }
        else if( started && ( evt->IsMotion() || evt->IsDrag( BUT_LEFT ) ) )
        {
            end = pos;
            updatePreview();
        }
        else
        {
            evt->SetPassEvent();
        }
    }

    controls->SetAutoPan( false );
    controls->CaptureCursor( false );
    view->SetVisible( &area, false );

    if( finished && origin != end )
    {
        if( !deselect )
            m_toolMgr->RunAction( ACTIONS::selectionClear );

        if( lassoMode )
        {
            area.GetPoly().SetClosed( true );
        }
        else if( lineMode )
        {
            // A sliver of a polygon along the line: anything it touches is crossed by the line.
            VECTOR2D dir = VECTOR2D( end - origin ).Resize( 1.0 );
            VECTOR2I n( KiROUND( -dir.y * 50 ), KiROUND( dir.x * 50 ) );

            SHAPE_LINE_CHAIN& poly = area.GetPoly();
            poly.Clear();
            poly.Append( origin + n );
            poly.Append( end + n );
            poly.Append( end - n );
            poly.Append( origin - n );
            poly.SetClosed( true );
        }
        else if( outsideMode )
        {
            // Everything fully inside the region between the board extents and the rectangle.
            // The region is one polygon: the outer frame clockwise, then the rectangle as a hole
            // reached through a zero-width slit.
            BOX2I inner = BOX2I::ByCorners( origin, end );
            BOX2I outer = board()->GetBoundingBox();
            outer.Merge( inner );
            outer.Inflate( pcbIUScale.mmToIU( 10 ) );

            SHAPE_LINE_CHAIN& poly = area.GetPoly();
            poly.Clear();
            poly.Append( outer.GetLeft(), outer.GetTop() );
            poly.Append( outer.GetRight(), outer.GetTop() );
            poly.Append( outer.GetRight(), outer.GetBottom() );
            poly.Append( outer.GetLeft(), outer.GetBottom() );
            poly.Append( outer.GetLeft(), inner.GetTop() );
            poly.Append( inner.GetLeft(), inner.GetTop() );
            poly.Append( inner.GetLeft(), inner.GetBottom() );
            poly.Append( inner.GetRight(), inner.GetBottom() );
            poly.Append( inner.GetRight(), inner.GetTop() );
            poly.Append( inner.GetLeft(), inner.GetTop() );
            poly.Append( outer.GetLeft(), inner.GetTop() );
            poly.SetClosed( true );

            area.SetMode( SELECTION_MODE::INSIDE_LASSO );
        }

        selTool->SelectMultiple( area, deselect );
    }

    view->Remove( &area );
    frame()->PopTool( aEvent );
    return 0;
}


std::vector<BOARD_ITEM*> PCB_ACTIVE_BAR_TOOL::allItems( bool aWithPads ) const
{
    std::vector<BOARD_ITEM*> items;

    if( IsFootprintEditor() )
    {
        if( FOOTPRINT* fp = footprint() )
        {
            for( PAD* pad : fp->Pads() )
                items.push_back( pad );

            for( BOARD_ITEM* item : fp->GraphicalItems() )
                items.push_back( item );

            for( ZONE* zone : fp->Zones() )
                items.push_back( zone );
        }

        return items;
    }

    for( FOOTPRINT* fp : board()->Footprints() )
    {
        items.push_back( fp );

        if( aWithPads )
        {
            for( PAD* pad : fp->Pads() )
                items.push_back( pad );
        }
    }

    for( PCB_TRACK* track : board()->Tracks() )
        items.push_back( track );

    for( BOARD_ITEM* item : board()->Drawings() )
        items.push_back( item );

    for( ZONE* zone : board()->Zones() )
        items.push_back( zone );

    return items;
}


void PCB_ACTIVE_BAR_TOOL::selectItems( const std::vector<BOARD_ITEM*>& aItems )
{
    PCB_SELECTION_TOOL* selTool = m_toolMgr->GetTool<PCB_SELECTION_TOOL>();
    EDA_ITEMS           selectable;

    for( BOARD_ITEM* item : aItems )
    {
        if( selTool->Selectable( item ) )
            selectable.push_back( item );
    }

    m_toolMgr->RunAction( ACTIONS::selectionClear );

    if( !selectable.empty() )
        selTool->AddItemsToSel( &selectable );

    frame()->SetStatusText( wxString::Format( wxPLURAL( "%zu object selected",
                                                        "%zu objects selected",
                                                        selectable.size() ),
                                              selectable.size() ) );
}


void PCB_ACTIVE_BAR_TOOL::deselectItems( const std::vector<BOARD_ITEM*>& aItems )
{
    PCB_SELECTION_TOOL* selTool = m_toolMgr->GetTool<PCB_SELECTION_TOOL>();
    EDA_ITEMS           selected;

    for( BOARD_ITEM* item : aItems )
    {
        if( item->IsSelected() )
            selected.push_back( item );
    }

    if( !selected.empty() )
        selTool->RemoveItemsFromSel( &selected );
}


int PCB_ACTIVE_BAR_TOOL::SelectByKind( const TOOL_EVENT& aEvent )
{
    std::vector<BOARD_ITEM*> found;

    const bool deselect = aEvent.IsAction( &PCB_ACTIONS::deselectAllOnLayer )
                          || aEvent.IsAction( &PCB_ACTIONS::deselectFreeObjects );

    if( aEvent.IsAction( &PCB_ACTIONS::selectAllOnLayer )
            || aEvent.IsAction( &PCB_ACTIONS::deselectAllOnLayer ) )
    {
        PCB_LAYER_ID layer = frame()->GetActiveLayer();

        for( BOARD_ITEM* item : allItems( false ) )
        {
            bool onLayer = item->Type() == PCB_FOOTPRINT_T ? item->GetLayer() == layer
                                                            : item->IsOnLayer( layer );

            if( onLayer )
                found.push_back( item );
        }
    }
    else if( aEvent.IsAction( &PCB_ACTIONS::selectFreeObjects )
             || aEvent.IsAction( &PCB_ACTIONS::deselectFreeObjects ) )
    {
        // Free primitives: anything that isn't part of a component
        for( BOARD_ITEM* item : allItems( false ) )
        {
            if( item->Type() != PCB_FOOTPRINT_T && item->Type() != PCB_PAD_T )
                found.push_back( item );
        }
    }
    else if( aEvent.IsAction( &PCB_ACTIONS::selectAllLocked ) )
    {
        for( BOARD_ITEM* item : allItems( false ) )
        {
            if( item->IsLocked() )
                found.push_back( item );
        }
    }
    else if( aEvent.IsAction( &PCB_ACTIONS::selectOffGridPads ) )
    {
        PCB_GRID_HELPER grid( m_toolMgr, frame()->GetMagneticItemsSettings() );

        for( BOARD_ITEM* item : allItems( true ) )
        {
            if( item->Type() != PCB_PAD_T )
                continue;

            VECTOR2I pos = item->GetPosition();

            if( grid.AlignGrid( pos, grid.GetItemGrid( item ) ) != pos )
                found.push_back( item );
        }
    }

    if( deselect )
        deselectItems( found );
    else
        selectItems( found );

    return 0;
}


int PCB_ACTIVE_BAR_TOOL::AdjustSpacing( const TOOL_EVENT& aEvent )
{
    const bool horizontal = aEvent.IsAction( &PCB_ACTIONS::increaseHorizontalSpacing )
                            || aEvent.IsAction( &PCB_ACTIONS::decreaseHorizontalSpacing );
    const bool increase = aEvent.IsAction( &PCB_ACTIONS::increaseHorizontalSpacing )
                          || aEvent.IsAction( &PCB_ACTIONS::increaseVerticalSpacing );

    PCB_SELECTION_TOOL*      selTool = m_toolMgr->GetTool<PCB_SELECTION_TOOL>();
    PCB_GRID_HELPER          grid( m_toolMgr, frame()->GetMagneticItemsSettings() );
    std::vector<BOARD_ITEM*> items;

    for( EDA_ITEM* edaItem : selTool->GetSelection() )
    {
        if( !edaItem->IsBOARD_ITEM() )
            continue;

        BOARD_ITEM* item = static_cast<BOARD_ITEM*>( edaItem );

        // Tracks are defined by their ends and vias join them; moving one would disconnect it
        if( item->Type() == PCB_TRACE_T || item->Type() == PCB_ARC_T || item->Type() == PCB_VIA_T )
            continue;

        // Locked objects stay put, as with every other move
        if( item->IsLocked() )
            continue;

        // In the board editor, parts of a component move with their component
        if( !IsFootprintEditor() && item->GetParentFootprint() )
            continue;

        items.push_back( item );
    }

    if( items.size() < 2 )
        return 0;

    // Keep the first object (leftmost / topmost) in place; each next one moves a grid step
    // further than the one before it, so every gap changes by one step
    std::sort( items.begin(), items.end(),
               [&]( BOARD_ITEM* a, BOARD_ITEM* b )
               {
                   VECTOR2I ca = a->GetBoundingBox().GetCenter();
                   VECTOR2I cb = b->GetBoundingBox().GetCenter();
                   return horizontal ? ca.x < cb.x : ca.y < cb.y;
               } );

    VECTOR2D step = grid.GetGrid();
    int      unit = KiROUND( horizontal ? step.x : step.y ) * ( increase ? 1 : -1 );
    BOARD_COMMIT commit( frame() );

    for( size_t ii = 1; ii < items.size(); ++ii )
    {
        VECTOR2I delta = horizontal ? VECTOR2I( unit * (int) ii, 0 ) : VECTOR2I( 0, unit * (int) ii );

        commit.Modify( items[ii], nullptr, RECURSE_MODE::RECURSE );
        items[ii]->Move( delta );
    }

    commit.Push( increase ? _( "Increase Spacing" ) : _( "Decrease Spacing" ) );
    return 0;
}


int PCB_ACTIVE_BAR_TOOL::ToggleSelection( const TOOL_EVENT& aEvent )
{
    KIGFX::VIEW_CONTROLS* controls = getViewControls();
    PCB_SELECTION_TOOL*   selTool = m_toolMgr->GetTool<PCB_SELECTION_TOOL>();

    frame()->PushTool( aEvent );
    Activate();
    controls->ShowCursor( true );

    while( TOOL_EVENT* evt = Wait() )
    {
        frame()->GetCanvas()->SetCurrentCursor( KICURSOR::ADD );

        if( evt->IsCancelInteractive() || evt->IsActivate() || evt->IsClick( BUT_RIGHT ) )
            break;

        if( evt->IsClick( BUT_LEFT ) )
            selTool->ToggleItemAt( evt->Position() );
        else
            evt->SetPassEvent();
    }

    frame()->PopTool( aEvent );
    return 0;
}


int PCB_ACTIVE_BAR_TOOL::AlignToGrid( const TOOL_EVENT& aEvent )
{
    PCB_SELECTION_TOOL* selTool = m_toolMgr->GetTool<PCB_SELECTION_TOOL>();
    PCB_GRID_HELPER     grid( m_toolMgr, frame()->GetMagneticItemsSettings() );
    BOARD_COMMIT        commit( frame() );
    int                 moved = 0;

    for( EDA_ITEM* edaItem : selTool->GetSelection() )
    {
        if( !edaItem->IsBOARD_ITEM() )
            continue;

        BOARD_ITEM* item = static_cast<BOARD_ITEM*>( edaItem );

        // Tracks are defined by their ends and vias join them; snapping one would disconnect it
        if( item->Type() == PCB_TRACE_T || item->Type() == PCB_ARC_T || item->Type() == PCB_VIA_T )
            continue;

        // Locked objects stay put, as with every other move
        if( item->IsLocked() )
            continue;

        // In the board editor, parts of a component move with their component
        if( !IsFootprintEditor() && item->GetParentFootprint() )
            continue;

        VECTOR2I pos = item->GetPosition();
        VECTOR2I delta = grid.AlignGrid( pos, grid.GetItemGrid( item ) ) - pos;

        if( delta == VECTOR2I( 0, 0 ) )
            continue;

        commit.Modify( item, nullptr, RECURSE_MODE::RECURSE );
        item->Move( delta );
        moved++;
    }

    if( moved )
        commit.Push( _( "Align to Grid" ) );

    return 0;
}


void PCB_ACTIVE_BAR_TOOL::setTransitions()
{
    Go( &PCB_ACTIVE_BAR_TOOL::SelectionFilterMenu, PCB_ACTIONS::selectionFilterMenu.MakeEvent() );
    Go( &PCB_ACTIVE_BAR_TOOL::SelectArea,          PCB_ACTIONS::selectInsideArea.MakeEvent() );
    Go( &PCB_ACTIVE_BAR_TOOL::SelectArea,          PCB_ACTIONS::selectOutsideArea.MakeEvent() );
    Go( &PCB_ACTIVE_BAR_TOOL::SelectArea,          PCB_ACTIONS::selectTouchingRect.MakeEvent() );
    Go( &PCB_ACTIVE_BAR_TOOL::SelectArea,          PCB_ACTIONS::selectTouchingLine.MakeEvent() );
    Go( &PCB_ACTIVE_BAR_TOOL::SelectByKind,        PCB_ACTIONS::selectAllOnLayer.MakeEvent() );
    Go( &PCB_ACTIVE_BAR_TOOL::SelectByKind,        PCB_ACTIONS::selectFreeObjects.MakeEvent() );
    Go( &PCB_ACTIVE_BAR_TOOL::SelectByKind,        PCB_ACTIONS::selectAllLocked.MakeEvent() );
    Go( &PCB_ACTIVE_BAR_TOOL::SelectByKind,        PCB_ACTIONS::selectOffGridPads.MakeEvent() );
    Go( &PCB_ACTIVE_BAR_TOOL::ToggleSelection,     PCB_ACTIONS::toggleSelection.MakeEvent() );
    Go( &PCB_ACTIVE_BAR_TOOL::AlignToGrid,         PCB_ACTIONS::alignToGrid.MakeEvent() );
    Go( &PCB_ACTIVE_BAR_TOOL::SelectArea,          PCB_ACTIONS::deselectLasso.MakeEvent() );
    Go( &PCB_ACTIVE_BAR_TOOL::SelectArea,          PCB_ACTIONS::deselectInsideArea.MakeEvent() );
    Go( &PCB_ACTIVE_BAR_TOOL::SelectArea,          PCB_ACTIONS::deselectOutsideArea.MakeEvent() );
    Go( &PCB_ACTIVE_BAR_TOOL::SelectArea,          PCB_ACTIONS::deselectTouchingRect.MakeEvent() );
    Go( &PCB_ACTIVE_BAR_TOOL::SelectArea,          PCB_ACTIONS::deselectTouchingLine.MakeEvent() );
    Go( &PCB_ACTIVE_BAR_TOOL::SelectByKind,        PCB_ACTIONS::deselectAllOnLayer.MakeEvent() );
    Go( &PCB_ACTIVE_BAR_TOOL::SelectByKind,        PCB_ACTIONS::deselectFreeObjects.MakeEvent() );
    Go( &PCB_ACTIVE_BAR_TOOL::AdjustSpacing,       PCB_ACTIONS::increaseHorizontalSpacing.MakeEvent() );
    Go( &PCB_ACTIVE_BAR_TOOL::AdjustSpacing,       PCB_ACTIONS::decreaseHorizontalSpacing.MakeEvent() );
    Go( &PCB_ACTIVE_BAR_TOOL::AdjustSpacing,       PCB_ACTIONS::increaseVerticalSpacing.MakeEvent() );
    Go( &PCB_ACTIVE_BAR_TOOL::AdjustSpacing,       PCB_ACTIONS::decreaseVerticalSpacing.MakeEvent() );
}
