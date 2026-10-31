/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2015 Jean-Pierre Charras, jp.charras at wanadoo.fr
 * Copyright (C) 2012 SoftPLC Corporation, Dick Hollenbeck <dick@softplc.com>
 * Copyright (C) 2012 Wayne Stambaugh <stambaughw@gmail.com>
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

#include "toolbars_active_bar.h"
#include <tool/actions.h>
#include <tool/action_menu.h>
#include <footprint_edit_frame.h>
#include <pcbnew_id.h>
#include <bitmaps.h>
#include <lset.h>
#include <tool/action_toolbar.h>
#include <tool/tool_manager.h>
#include <tool/ui/toolbar_context_menu_registry.h>
#include <tools/pcb_actions.h>
#include <tools/pcb_selection_tool.h>
#include <pcb_layer_box_selector.h>
#include <wx/choice.h>
#include <wx/wupdlock.h>
#include <advanced_config.h>

#include <toolbars_footprint_editor.h>

std::optional<TOOLBAR_CONFIGURATION> FOOTPRINT_EDIT_TOOLBAR_SETTINGS::DefaultToolbarConfig( TOOLBAR_LOC aToolbar )
{
    TOOLBAR_CONFIGURATION config;

    // clang-format off
    switch( aToolbar )
    {
    // No Aux toolbar
    case TOOLBAR_LOC::TOP_AUX:
        return std::nullopt;

    case TOOLBAR_LOC::LEFT:
        config.AppendAction( ACTIONS::toggleGrid )
              .WithContextMenu(
                      []( TOOL_MANAGER* aToolMgr )
                      {
                          PCB_SELECTION_TOOL* selTool = aToolMgr->GetTool<PCB_SELECTION_TOOL>();
                          auto                menu = std::make_unique<ACTION_MENU>( false, selTool );
                          menu->Add( ACTIONS::gridProperties );
                          menu->Add( ACTIONS::gridOrigin );
                          return menu;
                      } )
              .AppendAction( ACTIONS::toggleGridOverrides )
              .AppendAction( PCB_ACTIONS::togglePolarCoords )
              .AppendGroup( TOOLBAR_GROUP_CONFIG( _( "Units" ) )
                            .AddAction( ACTIONS::millimetersUnits )
                            .AddAction( ACTIONS::inchesUnits )
                            .AddAction( ACTIONS::milsUnits ) )
              .AppendGroup( TOOLBAR_GROUP_CONFIG( _( "Crosshair modes" ) )
                            .AddAction( ACTIONS::cursorSmallCrosshairs )
                            .AddAction( ACTIONS::cursorFullCrosshairs )
                            .AddAction( ACTIONS::cursor45Crosshairs ) );

        config.AppendSeparator()
              .AppendGroup( TOOLBAR_GROUP_CONFIG( _( "Line modes" ) )
                            .AddAction( PCB_ACTIONS::lineModeFree )
                            .AddAction( PCB_ACTIONS::lineMode90 )
                            .AddAction( PCB_ACTIONS::lineMode45 ) );

        config.AppendSeparator()
              .AppendAction( PCB_ACTIONS::padDisplayMode )
              .AppendAction( PCB_ACTIONS::graphicsOutlines )
              .AppendAction( PCB_ACTIONS::textOutlines )
              .AppendAction( ACTIONS::highContrastMode );

        if( ADVANCED_CFG::GetCfg().m_DrawBoundingBoxes )
            config.AppendAction( ACTIONS::toggleBoundingBoxes );

        config.AppendSeparator()
              .AppendAction( ACTIONS::showLibraryTree )
              .AppendAction( PCB_ACTIONS::showLayersManager )
              .AppendAction( ACTIONS::showProperties );
        break;

    case TOOLBAR_LOC::RIGHT:
        // Laid out like Altium's Active Bar in the PCB Library editor: one button per tool
        // family, with the rest of the family in the button's drop-down palette.
        AppendActiveBarEditGroups( config, false );

        config.AppendSeparator()
              .AppendAction( PCB_ACTIONS::placePad )
              .AppendAction( PCB_ACTIONS::drawRuleArea );

        config.AppendSeparator()
              .AppendGroup( TOOLBAR_GROUP_CONFIG( _( "Text tools" ) )
                            .AddAction( PCB_ACTIONS::placeText )
                            .AddAction( PCB_ACTIONS::drawTextBox )
                            .AddAction( PCB_ACTIONS::drawTable ) )
              .AppendGroup( TOOLBAR_GROUP_CONFIG( _( "Graphic tools" ) )
                            .AddAction( PCB_ACTIONS::drawLine )
                            .AddAction( PCB_ACTIONS::drawArc )
                            .AddAction( PCB_ACTIONS::drawRectangle )
                            .AddAction( PCB_ACTIONS::drawCircle )
                            .AddAction( PCB_ACTIONS::drawPolygon )
                            .AddAction( PCB_ACTIONS::drawBezier )
                            .AddAction( PCB_ACTIONS::placeReferenceImage ) )
              .AppendGroup( TOOLBAR_GROUP_CONFIG( _( "Dimension objects" ) )
                            .AddAction( PCB_ACTIONS::drawAlignedDimension )
                            .AddAction( PCB_ACTIONS::drawOrthogonalDimension )
                            .AddAction( PCB_ACTIONS::drawCenterDimension )
                            .AddAction( PCB_ACTIONS::drawRadialDimension )
                            .AddAction( PCB_ACTIONS::drawLeader ) )
              .AppendGroup( TOOLBAR_GROUP_CONFIG( _( "Footprint origins and points" ) )
                            .AddAction( PCB_ACTIONS::setAnchor )
                            .AddAction( ACTIONS::gridSetOrigin )
                            .AddAction( PCB_ACTIONS::placePoint )
                            .AddAction( ACTIONS::measureTool )
                            .AddAction( ACTIONS::deleteTool ) );
        break;

    case TOOLBAR_LOC::TOP_MAIN:
        // Sits at the right end of the menu row (the file and undo buttons are in the title
        // bar, as in Altium).  Layers are picked from the layer tabs.
        config.AppendControl( ACTION_TOOLBAR_CONTROLS::gridSelect );

        config.AppendSpacer( -1 )
              .AppendAction( ACTIONS::openPreferences );
        break;
    }

    // clang-format on
    return config;
}


void FOOTPRINT_EDIT_FRAME::ReCreateLayerBox( bool aForceResizeToolbar )
{
    if( !m_SelLayerBox )
        return;

    m_SelLayerBox->SetToolTip( _( "+/- to switch" ) );
    m_SelLayerBox->Resync();

    if( aForceResizeToolbar )
        UpdateToolbarControlSizes();
}
