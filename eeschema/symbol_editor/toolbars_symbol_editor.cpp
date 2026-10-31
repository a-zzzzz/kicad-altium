/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2004 Jean-Pierre Charras, jaen-pierre.charras@gipsa-lab.inpg.com
 * Copyright (C) 2008 Wayne Stambaugh <stambaughw@gmail.com>
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include <advanced_config.h>
#include <eeschema_id.h>
#include <symbol_edit_frame.h>
#include <sch_painter.h>
#include <symbol_editor_settings.h>
#include <symbol_library_manager.h>
#include <toolbars_symbol_editor.h>
#include <tool/action_menu.h>
#include <tool/action_toolbar.h>
#include <tool/tool_manager.h>
#include <tool/ui/toolbar_context_menu_registry.h>
#include <tools/sch_actions.h>
#include <tools/sch_selection_tool.h>
#include <widgets/sch_properties_panel.h>
#include <widgets/sch_properties_panel.h>
#include <widgets/wx_aui_utils.h>
#include <wx/combobox.h>

#ifdef __UNIX__
#define LISTBOX_WIDTH 140
#else
#define LISTBOX_WIDTH 120
#endif


std::optional<TOOLBAR_CONFIGURATION> SYMBOL_EDIT_TOOLBAR_SETTINGS::DefaultToolbarConfig( TOOLBAR_LOC aToolbar )
{
    TOOLBAR_CONFIGURATION config;

    // clang-format off
    switch( aToolbar )
    {
    case TOOLBAR_LOC::TOP_AUX:
        return std::nullopt;

    case TOOLBAR_LOC::LEFT:
        config.AppendAction( ACTIONS::toggleGrid )
              .WithContextMenu(
                      []( TOOL_MANAGER* aToolMgr )
                      {
                          SCH_SELECTION_TOOL* selTool = aToolMgr->GetTool<SCH_SELECTION_TOOL>();
                          auto               menu = std::make_unique<ACTION_MENU>( false, selTool );
                          menu->Add( ACTIONS::gridProperties );
                          return menu;
                      } )
              .AppendAction( ACTIONS::toggleGridOverrides )
              .AppendGroup( TOOLBAR_GROUP_CONFIG( _( "Units" ) )
                            .AddAction( ACTIONS::millimetersUnits )
                            .AddAction( ACTIONS::inchesUnits )
                            .AddAction( ACTIONS::milsUnits ) )
              .AppendGroup( TOOLBAR_GROUP_CONFIG( _( "Crosshair modes" ) )
                            .AddAction( ACTIONS::cursorSmallCrosshairs )
                            .AddAction( ACTIONS::cursorFullCrosshairs )
                            .AddAction( ACTIONS::cursor45Crosshairs ) );

        config.AppendSeparator()
              .AppendAction( SCH_ACTIONS::showElectricalTypes )
              .AppendAction( SCH_ACTIONS::showHiddenPins )
              .AppendAction( SCH_ACTIONS::showHiddenFields );
        //    .AppendAction( SCH_ACTIONS::togglePinAltIcons );

        if( ADVANCED_CFG::GetCfg().m_DrawBoundingBoxes )
            config.AppendAction( ACTIONS::toggleBoundingBoxes );

        config.AppendSeparator()
              .AppendAction( ACTIONS::showLibraryTree )
              .AppendAction( ACTIONS::showProperties );
        break;

    case TOOLBAR_LOC::RIGHT:
        // Laid out like Altium's Active Bar in the Schematic Library editor: one button per
        // tool family, with the rest of the family in the button's drop-down palette.
        config.AppendGroup( TOOLBAR_GROUP_CONFIG( _( "Move objects" ) )
                            .AddAction( SCH_ACTIONS::move )
                            .AddAction( SCH_ACTIONS::rotateCCW )
                            .AddAction( SCH_ACTIONS::rotateCW )
                            .AddAction( SCH_ACTIONS::mirrorV )
                            .AddAction( SCH_ACTIONS::mirrorH )
                            .AddAction( SCH_ACTIONS::alignToGrid ) )
              .AppendGroup( TOOLBAR_GROUP_CONFIG( _( "Select objects" ) )
                            .AddAction( ACTIONS::selectSetRect )
                            .AddAction( ACTIONS::selectSetLasso )
                            .AddAction( ACTIONS::selectAll ) );

        config.AppendSeparator()
              .AppendAction( SCH_ACTIONS::placeSymbolPin )
              .AppendGroup( TOOLBAR_GROUP_CONFIG( _( "Graphic tools" ) )
                            .AddAction( SCH_ACTIONS::drawSymbolLines )
                            .AddAction( SCH_ACTIONS::drawArc )
                            .AddAction( SCH_ACTIONS::drawCircle )
                            .AddAction( SCH_ACTIONS::drawRectangle )
                            .AddAction( SCH_ACTIONS::drawSymbolPolygon )
                            .AddAction( SCH_ACTIONS::drawBezier ) )
              .AppendGroup( TOOLBAR_GROUP_CONFIG( _( "Text tools" ) )
                            .AddAction( SCH_ACTIONS::placeSymbolText )
                            .AddAction( SCH_ACTIONS::drawSymbolTextBox ) );

        config.AppendSeparator()
              .AppendAction( SCH_ACTIONS::placeSymbolAnchor )
              .AppendAction( ACTIONS::deleteTool );
        break;

    case TOOLBAR_LOC::TOP_MAIN:
        // Sits at the right end of the menu row (the file and undo buttons are in the title
        // bar, as in Altium): the part and body style being edited.
        config.AppendControl( ACTION_TOOLBAR_CONTROLS::unitSelector )
              .AppendControl( ACTION_TOOLBAR_CONTROLS::bodyStyleSelector )
              .AppendAction( SCH_ACTIONS::toggleSyncedPinsMode );

        config.AppendSpacer( -1 )
              .AppendAction( ACTIONS::openPreferences );
        break;
    }

    // clang-format on
    return config;
}


void SYMBOL_EDIT_FRAME::configureToolbars()
{
    SCH_BASE_FRAME::configureToolbars();

    auto unitDisplayFactory =
            [this]( ACTION_TOOLBAR* aToolbar )
            {
                if( !m_unitSelectBox )
                {
                    m_unitSelectBox = new wxComboBox( aToolbar, ID_LIBEDIT_SELECT_UNIT_NUMBER,
                                                      wxEmptyString, wxDefaultPosition,
                                                      wxSize( LISTBOX_WIDTH, -1 ), 0,
                                                      nullptr, wxCB_READONLY );
                }

                aToolbar->Add( m_unitSelectBox );
            };

    auto bodyDisplayFactory =
            [this]( ACTION_TOOLBAR* aToolbar )
            {
                if( !m_bodyStyleSelectBox )
                {
                    m_bodyStyleSelectBox = new wxComboBox( aToolbar, ID_LIBEDIT_SELECT_BODY_STYLE,
                                                           wxEmptyString, wxDefaultPosition,
                                                           wxSize( LISTBOX_WIDTH, -1 ), 0,
                                                           nullptr, wxCB_READONLY );
                }

                aToolbar->Add( m_bodyStyleSelectBox );
            };

    RegisterCustomToolbarControlFactory( ACTION_TOOLBAR_CONTROLS::unitSelector, unitDisplayFactory );
    RegisterCustomToolbarControlFactory( ACTION_TOOLBAR_CONTROLS::bodyStyleSelector, bodyDisplayFactory );
}


void SYMBOL_EDIT_FRAME::ClearToolbarControl( int aId )
{
    SCH_BASE_FRAME::ClearToolbarControl( aId );

    switch( aId )
    {
    case ID_LIBEDIT_SELECT_UNIT_NUMBER: m_unitSelectBox = nullptr;      break;
    case ID_LIBEDIT_SELECT_BODY_STYLE:  m_bodyStyleSelectBox = nullptr; break;
    }
}


