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

#include "sch_active_bar_tool.h"

#include <lib_id.h>
#include <lib_symbol.h>
#include <power_port_style.h>
#include <sch_io/altium/altium_parser_sch.h>
#include <schematic.h>
#include <preview_items/selection_area.h>
#include <project/sch_project_settings.h>
#include <sch_screen.h>
#include <sch_symbol.h>
#include <tool/tool_manager.h>
#include <tools/sch_actions.h>
#include <tools/sch_selection_tool.h>
#include <view/view.h>
#include <view/view_controls.h>

#include <wx/menu.h>


SCH_ACTIVE_BAR_TOOL::SCH_ACTIVE_BAR_TOOL() :
        SCH_TOOL_BASE<SCH_EDIT_FRAME>( "eeschema.ActiveBar" )
{
}


int SCH_ACTIVE_BAR_TOOL::SelectionFilterMenu( const TOOL_EVENT& aEvent )
{
    SCH_SELECTION_FILTER_OPTIONS& filter = m_selectionTool->GetFilter();

    struct ENTRY
    {
        wxString label;
        bool*    flag;
    };

    const std::vector<ENTRY> kinds = {
        { _( "Components" ),      &filter.symbols },
        { _( "Wires" ),           &filter.wires },
        { _( "Net Labels" ),      &filter.labels },
        { _( "Pins" ),            &filter.pins },
        { _( "Texts" ),           &filter.text },
        { _( "Drawing Objects" ), &filter.graphics },
        { _( "Images" ),          &filter.images },
        { _( "Rule Areas" ),      &filter.ruleAreas },
        { _( "Other" ),           &filter.otherItems },
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

        int id = m_frame->GetPopupMenuSelectionFromUser( menu );

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

        m_frame->SyncSelectionFilterPanel();
    }

    return 0;
}


int SCH_ACTIVE_BAR_TOOL::SelectArea( const TOOL_EVENT& aEvent )
{
    const bool lineMode = aEvent.IsAction( &SCH_ACTIONS::selectTouchingLine );

    KIGFX::VIEW*          view = getView();
    KIGFX::VIEW_CONTROLS* controls = getViewControls();

    KIGFX::PREVIEW::SELECTION_AREA area;
    area.SetMode( lineMode ? SELECTION_MODE::TOUCHING_LASSO
                           : aEvent.IsAction( &SCH_ACTIONS::selectTouchingRect )
                                     ? SELECTION_MODE::TOUCHING_RECTANGLE
                                     : SELECTION_MODE::INSIDE_RECTANGLE );

    view->Add( &area );
    view->SetVisible( &area, false );

    m_frame->PushTool( aEvent );
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
                if( lineMode )
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
        m_frame->GetCanvas()->SetCurrentCursor( lineMode ? KICURSOR::PENCIL
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
        m_toolMgr->RunAction( ACTIONS::selectionClear );

        if( lineMode )
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
        else if( aEvent.IsAction( &SCH_ACTIONS::selectOutsideArea ) )
        {
            // Everything fully inside the region between the sheet extents and the rectangle:
            // the outer frame clockwise, then the rectangle as a hole via a zero-width slit.
            BOX2I inner = BOX2I::ByCorners( origin, end );
            VECTOR2D page = m_frame->GetScreen()->GetPageSettings().GetSizeIU( schIUScale.IU_PER_MILS );
            BOX2I    outer( VECTOR2I( 0, 0 ), VECTOR2I( KiROUND( page.x ), KiROUND( page.y ) ) );

            for( SCH_ITEM* item : m_frame->GetScreen()->Items() )
                outer.Merge( item->GetBoundingBox() );

            outer.Merge( inner );
            outer.Inflate( schIUScale.MilsToIU( 500 ) );

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

        m_selectionTool->SelectArea( area );
    }

    view->Remove( &area );
    m_frame->PopTool( aEvent );
    return 0;
}


int SCH_ACTIVE_BAR_TOOL::ToggleSelection( const TOOL_EVENT& aEvent )
{
    KIGFX::VIEW_CONTROLS* controls = getViewControls();

    m_frame->PushTool( aEvent );
    Activate();
    controls->ShowCursor( true );

    while( TOOL_EVENT* evt = Wait() )
    {
        m_frame->GetCanvas()->SetCurrentCursor( KICURSOR::ADD );

        if( evt->IsCancelInteractive() || evt->IsActivate() || evt->IsClick( BUT_RIGHT ) )
            break;

        if( evt->IsClick( BUT_LEFT ) )
        {
            m_selectionTool->SelectPoint( evt->Position(), { SCH_LOCATE_ANY_T }, nullptr, nullptr,
                                          false, false, false, true );
        }
        else
        {
            evt->SetPassEvent();
        }
    }

    m_frame->PopTool( aEvent );
    return 0;
}


int SCH_ACTIVE_BAR_TOOL::PlacePowerPort( const TOOL_EVENT& aEvent )
{
    // Altium's power ports: a net name in one of its port styles (no library involved)
    wxString name;
    int      style = (int) ASCH_POWER_PORT_STYLE::BAR;

    if( aEvent.IsAction( &SCH_ACTIONS::placePowerGnd ) )
    {
        name = wxS( "GND" );
        style = (int) ASCH_POWER_PORT_STYLE::POWER_GROUND;
    }
    else if( aEvent.IsAction( &SCH_ACTIONS::placePowerVcc ) )
    {
        name = wxS( "VCC" );
    }
    else if( aEvent.IsAction( &SCH_ACTIONS::placePowerPlus12 ) )
    {
        name = wxS( "+12" );
    }
    else if( aEvent.IsAction( &SCH_ACTIONS::placePowerPlus5 ) )
    {
        name = wxS( "+5" );
    }
    else if( aEvent.IsAction( &SCH_ACTIONS::placePowerMinus5 ) )
    {
        name = wxS( "-5" );
    }
    else if( aEvent.IsAction( &SCH_ACTIONS::placePowerSignalGnd ) )
    {
        name = wxS( "GND" );
        style = (int) ASCH_POWER_PORT_STYLE::SIGNAL_GROUND;
    }
    else if( aEvent.IsAction( &SCH_ACTIONS::placePowerEarth ) )
    {
        name = wxS( "GND" );
        style = (int) ASCH_POWER_PORT_STYLE::EARTH;
    }
    else
    {
        return 0;
    }

    SCH_SYMBOL* symbol = POWER_PORT_STYLE::Create( name, style, &m_frame->GetCurrentSheet(),
                                                   &m_frame->Schematic() );
    symbol->SetParent( m_frame->GetScreen() );

    m_toolMgr->PostAction( SCH_ACTIONS::placePower, SCH_ACTIONS::PLACE_SYMBOL_PARAMS{ symbol, true } );
    return 0;
}


void SCH_ACTIVE_BAR_TOOL::setTransitions()
{
    Go( &SCH_ACTIVE_BAR_TOOL::SelectionFilterMenu, SCH_ACTIONS::selectionFilterMenu.MakeEvent() );
    Go( &SCH_ACTIVE_BAR_TOOL::SelectArea,          SCH_ACTIONS::selectInsideArea.MakeEvent() );
    Go( &SCH_ACTIVE_BAR_TOOL::SelectArea,          SCH_ACTIONS::selectOutsideArea.MakeEvent() );
    Go( &SCH_ACTIVE_BAR_TOOL::SelectArea,          SCH_ACTIONS::selectTouchingRect.MakeEvent() );
    Go( &SCH_ACTIVE_BAR_TOOL::SelectArea,          SCH_ACTIONS::selectTouchingLine.MakeEvent() );
    Go( &SCH_ACTIVE_BAR_TOOL::ToggleSelection,     SCH_ACTIONS::toggleSelection.MakeEvent() );
    Go( &SCH_ACTIVE_BAR_TOOL::PlacePowerPort,      SCH_ACTIONS::placePowerGnd.MakeEvent() );
    Go( &SCH_ACTIVE_BAR_TOOL::PlacePowerPort,      SCH_ACTIONS::placePowerVcc.MakeEvent() );
    Go( &SCH_ACTIVE_BAR_TOOL::PlacePowerPort,      SCH_ACTIONS::placePowerPlus12.MakeEvent() );
    Go( &SCH_ACTIVE_BAR_TOOL::PlacePowerPort,      SCH_ACTIONS::placePowerPlus5.MakeEvent() );
    Go( &SCH_ACTIVE_BAR_TOOL::PlacePowerPort,      SCH_ACTIONS::placePowerMinus5.MakeEvent() );
    Go( &SCH_ACTIVE_BAR_TOOL::PlacePowerPort,      SCH_ACTIONS::placePowerSignalGnd.MakeEvent() );
    Go( &SCH_ACTIVE_BAR_TOOL::PlacePowerPort,      SCH_ACTIONS::placePowerEarth.MakeEvent() );
}
