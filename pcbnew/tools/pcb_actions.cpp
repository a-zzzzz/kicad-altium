/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2013-2023 CERN
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 * @author Maciej Suminski <maciej.suminski@cern.ch>
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

#include "pcb_actions.h"
#include "tool/tool_action.h"
#include "tool/tool_event.h"
#include <pcbnew_id.h>
#include <bitmaps.h>
#include <layer_ids.h>
#include <footprint_viewer_frame.h>
#include <microwave/microwave_tool.h>
#include <pcb_reference_image.h>
#include <tool/tool_manager.h>
#include <tools/pcb_picker_tool.h>
#include <tools/pcb_selection_tool.h>
#include <router/pns_router.h>
#include <router/pns_routing_settings.h>
#include <geometry/geometry_utils.h>

// Actions, being statically-defined, require specialized I18N handling.  We continue to
// use the _() macro so that string harvesting by the I18N framework doesn't have to be
// specialized, but we don't translate on initialization and instead do it in the getters.

#undef _
#define _(s) s

// clang-format off

// CONVERT_TOOL
//
TOOL_ACTION PCB_ACTIONS::convertToPoly( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Convert.convertToPoly" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Create Polygon from Selection..." ) )
        .Tooltip( _( "Creates a graphic polygon from the selection" ) )
        .Icon( BITMAPS::add_graphical_polygon ) );

TOOL_ACTION PCB_ACTIONS::convertToZone( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Convert.convertToZone" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Create Zone from Selection..." ) )
        .Tooltip( _( "Creates a copper zone from the selection" ) )
        .Icon( BITMAPS::add_zone ) );

TOOL_ACTION PCB_ACTIONS::convertToKeepout( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Convert.convertToKeepout" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Create Rule Area from Selection..." ) )
        .Tooltip( _( "Creates a rule area from the selection" ) )
        .Icon( BITMAPS::add_keepout_area ) );

TOOL_ACTION PCB_ACTIONS::convertToLines( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Convert.convertToLines" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Create Lines from Selection..." ) )
        .Tooltip( _( "Creates graphic lines from the selection" ) )
        .Icon( BITMAPS::add_line ) );

TOOL_ACTION PCB_ACTIONS::convertToArc( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Convert.convertToArc" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Create Arc from Selection" ) )
        .Tooltip( _( "Creates an arc from the selected line segment" ) )
        .Icon( BITMAPS::add_arc ) );

TOOL_ACTION PCB_ACTIONS::convertToTracks( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Convert.convertToTracks" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Create Tracks from Selection" ) )
        .Tooltip( _( "Creates tracks from the selected graphic lines" ) )
        .Icon( BITMAPS::add_tracks ) );

TOOL_ACTION PCB_ACTIONS::outsetItems( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Convert.outsetItems" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Create Outsets from Selection..." ) )
        .Tooltip( _( "Create outset lines from the selected item" ) )
        .Icon( BITMAPS::outset_from_selection ) );


// DRAWING_TOOL
//
TOOL_ACTION PCB_ACTIONS::drawLine( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveDrawing.line" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_CTRL + MD_SHIFT + 'L' )
        .LegacyHotkeyName( "Draw Line" )
        .FriendlyName( _( "Draw Lines" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Icon( BITMAPS::add_graphical_segments )
        .Flags( AF_ACTIVATE ) );

TOOL_ACTION PCB_ACTIONS::drawPolygon( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveDrawing.graphicPolygon" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_CTRL + MD_SHIFT + 'P' )
        .LegacyHotkeyName( "Draw Graphic Polygon" )
        .FriendlyName( _( "Draw Polygons" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Icon( BITMAPS::add_graphical_polygon )
        .Flags( AF_ACTIVATE )
        .Parameter( ZONE_MODE::GRAPHIC_POLYGON ) );

TOOL_ACTION PCB_ACTIONS::drawRectangle( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveDrawing.rectangle" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Draw Rectangles" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Icon( BITMAPS::add_rectangle )
        .Flags( AF_ACTIVATE ) );

TOOL_ACTION PCB_ACTIONS::drawCircle( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveDrawing.circle" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_CTRL + MD_SHIFT + 'C' )
        .LegacyHotkeyName( "Draw Circle" )
        .FriendlyName( _( "Draw Circles" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Icon( BITMAPS::add_circle )
        .Flags( AF_ACTIVATE ) );

TOOL_ACTION PCB_ACTIONS::drawArc( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveDrawing.arc" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_CTRL + MD_SHIFT + 'A' )
        .LegacyHotkeyName( "Draw Arc" )
        .FriendlyName( _( "Draw Arcs" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Icon( BITMAPS::add_arc )
        .Flags( AF_ACTIVATE ) );

TOOL_ACTION PCB_ACTIONS::drawBezier( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveDrawing.bezier" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_CTRL + MD_SHIFT + 'B' )
        .FriendlyName( _( "Draw Bezier Curve" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Icon( BITMAPS::add_bezier )
        .Flags( AF_ACTIVATE ) );

TOOL_ACTION PCB_ACTIONS::placeBarcode( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveDrawing.barcode" )
        .Scope( AS_GLOBAL )
        .LegacyHotkeyName( "Add Barcode" )
        .FriendlyName( _( "Add Barcode" ) )
        .Tooltip( _( "Add a barcode" ) )
        .Icon( BITMAPS::add_barcode )
        .Flags( AF_ACTIVATE ) );


TOOL_ACTION PCB_ACTIONS::placeCharacteristics( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveDrawing.placeCharacteristics" )
        .Scope( AS_GLOBAL )
        .LegacyHotkeyName( "Add Board Characteristics" )
        .FriendlyName( _( "Add Board Characteristics" ) )
        .Tooltip( _( "Add a board characteristics table on a graphic layer" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Flags( AF_ACTIVATE ) );

TOOL_ACTION PCB_ACTIONS::placeStackup( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveDrawing.placeStackup" )
        .Scope( AS_GLOBAL )
        .LegacyHotkeyName( "Add Stackup Table" )
        .FriendlyName( _( "Add Stackup Table" ) )
        .Tooltip( _( "Add a board stackup table on a graphic layer" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Flags( AF_ACTIVATE ) );

TOOL_ACTION PCB_ACTIONS::placePoint( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveDrawing.placePoint" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Place Point" ) )
        .Tooltip( _( "Add reference/snap points" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Icon( BITMAPS::add_point )
        .Flags( AF_ACTIVATE ) );

TOOL_ACTION PCB_ACTIONS::placeReferenceImage( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveDrawing.placeReferenceImage" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Place Reference Images" ) )
        .Tooltip( _( "Add bitmap images to be used as reference (images will not be included in any output)" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Icon( BITMAPS::image )
        .Flags( AF_ACTIVATE )
        .Parameter<PCB_REFERENCE_IMAGE*>( nullptr ) );

TOOL_ACTION PCB_ACTIONS::placeText( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveDrawing.text" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_CTRL + MD_SHIFT + 'T' )
        .LegacyHotkeyName( "Add Text" )
        .FriendlyName( _( "Draw Text" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Icon( BITMAPS::text )
        .Flags( AF_ACTIVATE ) );

TOOL_ACTION PCB_ACTIONS::drawTextBox( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveDrawing.textbox" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Draw Text Boxes" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Icon( BITMAPS::add_textbox )
        .Flags( AF_ACTIVATE ) );

TOOL_ACTION PCB_ACTIONS::drawTable( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveDrawing.drawTable" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Draw Tables" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Icon( BITMAPS::table )
        .Flags( AF_ACTIVATE ) );

TOOL_ACTION PCB_ACTIONS::spacingIncrease( TOOL_ACTION_ARGS()
        .Name( "pcbnew.lengthTuner.SpacingIncrease" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( '1' )
        .LegacyHotkeyName( "Increase meander spacing by one step." )
        .FriendlyName( _( "Increase Spacing" ) )
        .Tooltip( _( "Increase tuning pattern spacing by one step." ) )
        .Icon( BITMAPS::router_len_tuner_dist_incr ) );

TOOL_ACTION PCB_ACTIONS::spacingDecrease( TOOL_ACTION_ARGS()
        .Name( "pcbnew.lengthTuner.SpacingDecrease" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( '2' )
        .LegacyHotkeyName( "Decrease meander spacing by one step." )
        .FriendlyName( _( "Decrease Spacing" ) )
        .Tooltip( _( "Decrease tuning pattern spacing by one step." ) )
        .Icon( BITMAPS::router_len_tuner_dist_decr ) );

TOOL_ACTION PCB_ACTIONS::amplIncrease( TOOL_ACTION_ARGS()
        .Name( "pcbnew.lengthTuner.AmplIncrease" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( '3' )
        .LegacyHotkeyName( "Increase meander amplitude by one step." )
        .FriendlyName( _( "Increase Amplitude" ) )
        .Tooltip( _( "Increase tuning pattern amplitude by one step." ) )
        .Icon( BITMAPS::router_len_tuner_amplitude_incr ) );

TOOL_ACTION PCB_ACTIONS::amplDecrease( TOOL_ACTION_ARGS()
        .Name( "pcbnew.lengthTuner.AmplDecrease" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( '4' )
        .LegacyHotkeyName( "Decrease meander amplitude by one step." )
        .FriendlyName( _( "Decrease Amplitude" ) )
        .Tooltip( _( "Decrease tuning pattern amplitude by one step." ) )
        .Icon( BITMAPS::router_len_tuner_amplitude_decr ) );


TOOL_ACTION PCB_ACTIONS::drawAlignedDimension( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveDrawing.alignedDimension" )
        .Scope( AS_GLOBAL )
        .LegacyHotkeyName( "Add Dimension" )
        .FriendlyName( _( "Draw Aligned Dimensions" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Icon( BITMAPS::add_aligned_dimension )
        .Flags( AF_ACTIVATE ) );

TOOL_ACTION PCB_ACTIONS::drawCenterDimension( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveDrawing.centerDimension" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Draw Center Dimensions" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Icon( BITMAPS::add_center_dimension )
        .Flags( AF_ACTIVATE ) );

TOOL_ACTION PCB_ACTIONS::drawRadialDimension( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveDrawing.radialDimension" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Draw Radial Dimensions" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Icon( BITMAPS::add_radial_dimension )
        .Flags( AF_ACTIVATE ) );

TOOL_ACTION PCB_ACTIONS::drawOrthogonalDimension( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveDrawing.orthogonalDimension" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_CTRL + MD_SHIFT + 'H' )
        .FriendlyName( _( "Draw Orthogonal Dimensions" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Icon( BITMAPS::add_orthogonal_dimension )
        .Flags( AF_ACTIVATE ) );

TOOL_ACTION PCB_ACTIONS::drawLeader( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveDrawing.leader" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Draw Leaders" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Icon( BITMAPS::add_leader )
        .Flags( AF_ACTIVATE ) );

TOOL_ACTION PCB_ACTIONS::drawZone( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveDrawing.zone" )
        .Scope( AS_GLOBAL )
#ifdef __WXOSX_MAC__
        .DefaultHotkey( MD_ALT + 'Z' )
#else
        .DefaultHotkey( MD_CTRL + MD_SHIFT + 'Z' )
#endif
        .LegacyHotkeyName( "Add Filled Zone" )
        .FriendlyName( _( "Polygon Pour" ) )
        .Tooltip( _( "Place a polygon pour (filled zone)" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Icon( BITMAPS::add_zone )
        .Flags( AF_ACTIVATE )
        .Parameter( ZONE_MODE::ADD ) );

TOOL_ACTION PCB_ACTIONS::drawVia( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveDrawing.via" )
        .Scope( AS_GLOBAL )
        .LegacyHotkeyName( "Add Vias" )
        .FriendlyName( _( "Place Vias" ) )
        .Tooltip( _( "Place free-standing vias" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Icon( BITMAPS::add_via )
        .Flags( AF_ACTIVATE ) );

TOOL_ACTION PCB_ACTIONS::drawRuleArea( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveDrawing.ruleArea" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_CTRL + MD_SHIFT + 'K' )
        .LegacyHotkeyName( "Add Keepout Area" )
        .FriendlyName( _( "Draw Rule Areas" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Icon( BITMAPS::add_keepout_area )
        .Flags( AF_ACTIVATE )
        .Parameter( ZONE_MODE::ADD ) );

TOOL_ACTION PCB_ACTIONS::drawZoneCutout( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveDrawing.zoneCutout" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_SHIFT + 'C' )
        .LegacyHotkeyName( "Add a Zone Cutout" )
        .FriendlyName( _( "Polygon Pour Cutout" ) )
        .Tooltip( _( "Add a cutout to an existing zone or rule area" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Icon( BITMAPS::add_zone_cutout )
        .Flags(  AF_ACTIVATE )
        .Parameter( ZONE_MODE::CUTOUT ) );

TOOL_ACTION PCB_ACTIONS::drawSimilarZone( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveDrawing.similarZone" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_CTRL + MD_SHIFT + '.' )
        .LegacyHotkeyName( "Add a Similar Zone" )
        .FriendlyName( _( "Add a Similar Zone" ) )
        .Tooltip( _( "Add a zone with the same settings as an existing zone" ) )
        .Icon( BITMAPS::add_zone )
        .Flags( AF_ACTIVATE )
        .Parameter( ZONE_MODE::SIMILAR ) );

TOOL_ACTION PCB_ACTIONS::placeImportedGraphics( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveDrawing.placeImportedGraphics" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_CTRL + MD_SHIFT + 'F' )
        .LegacyHotkeyName( "Place DXF" )
        .FriendlyName( _( "Import Graphics..." ) )
        .Tooltip( _( "Import 2D drawing file" ) )
        .Icon( BITMAPS::import_vector )
        .Flags( AF_ACTIVATE ) );

TOOL_ACTION PCB_ACTIONS::setAnchorPin1( TOOL_ACTION_ARGS()
        .Name( "pcbnew.FootprintEditor.setAnchorPin1" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Set Anchor to Pin 1" ) )
        .Tooltip( _( "Move the footprint anchor to the center of pad 1" ) )
        .Icon( BITMAPS::anchor ) );

TOOL_ACTION PCB_ACTIONS::setAnchorCenter( TOOL_ACTION_ARGS()
        .Name( "pcbnew.FootprintEditor.setAnchorCenter" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Set Anchor to Center" ) )
        .Tooltip( _( "Move the footprint anchor to the center of the footprint's pads" ) )
        .Icon( BITMAPS::anchor ) );

TOOL_ACTION PCB_ACTIONS::setAnchor( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveDrawing.setAnchor" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_CTRL + MD_SHIFT + 'N' )
        .LegacyHotkeyName( "Place the Footprint Anchor" )
        .FriendlyName( _( "Place the Footprint Anchor" ) )
        .Tooltip( _( "Set the anchor point of the footprint" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Icon( BITMAPS::anchor )
        .Flags( AF_ACTIVATE ) );

TOOL_ACTION PCB_ACTIONS::incWidth( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveDrawing.incWidth" )
        .Scope( AS_CONTEXT )
        .DefaultHotkey( MD_CTRL + '+' )
        .LegacyHotkeyName( "Increase Line Width" )
        .FriendlyName( _( "Increase Line Width" ) ) );

TOOL_ACTION PCB_ACTIONS::decWidth( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveDrawing.decWidth" )
        .Scope( AS_CONTEXT )
        .DefaultHotkey( MD_CTRL + '-' )
        .LegacyHotkeyName( "Decrease Line Width" )
        .FriendlyName( _( "Decrease Line Width" ) ) );

TOOL_ACTION PCB_ACTIONS::arcPosture( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveDrawing.arcPosture" )
        .Scope( AS_CONTEXT )
        .DefaultHotkey( '/' )
        .LegacyHotkeyName( "Switch Track Posture" )
        .FriendlyName( _( "Switch Arc Posture" ) ) );

TOOL_ACTION PCB_ACTIONS::changeDimensionArrows( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveDrawing.changeDimensionArrows" )
        .Scope( AS_CONTEXT )
        .FriendlyName( "Switch Dimension Arrows" )
        .Tooltip( "Switch between inward and outward dimension arrows" ) );


TOOL_ACTION PCB_ACTIONS::magneticSnapActiveLayer( TOOL_ACTION_ARGS()
        .Name( "common.Control.magneticSnapActiveLayer" )
        .Scope( AS_GLOBAL )
        .Icon( BITMAPS::snap_magnet )
        .FriendlyName( _( "Snap to Objects on the Active Layer Only" ) )
        .Tooltip( _( "Enables snapping to objects on the active layer only" ) ) );

TOOL_ACTION PCB_ACTIONS::magneticSnapAllLayers( TOOL_ACTION_ARGS()
        .Name( "common.Control.magneticSnapAllLayers" )
        .Scope( AS_GLOBAL )
        .Icon( BITMAPS::snap_magnet )
        .FriendlyName( _( "Snap to Objects on All Layers" ) )
        .Tooltip( _( "Enables snapping to objects on all visible layers" ) ) );

TOOL_ACTION PCB_ACTIONS::magneticSnapToggle( TOOL_ACTION_ARGS()
        .Name( "common.Control.magneticSnapToggle" )
        .Scope( AS_GLOBAL )
        .Icon( BITMAPS::snap_magnet )
        .DefaultHotkey( MD_SHIFT + 'S' )
        .FriendlyName( _( "Toggle Snapping Between Active and All Layers" ) )
        .Tooltip( _( "Toggles between snapping on all visible layers and only the active area" ) ) );

TOOL_ACTION PCB_ACTIONS::deleteLastPoint( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveDrawing.deleteLastPoint" )
        .Scope( AS_CONTEXT )
        .DefaultHotkey( WXK_BACK )
        .FriendlyName( _( "Delete Last Point" ) )
        .Tooltip( _( "Delete the last point added to the current item" ) )
        .Icon( BITMAPS::undo ) );

TOOL_ACTION PCB_ACTIONS::closeOutline( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveDrawing.closeOutline" )
        .Scope( AS_CONTEXT )
        .FriendlyName( _( "Close Outline" ) )
        .Tooltip( _( "Close the in progress outline" ) )
        .Icon( BITMAPS::checked_ok ) );

// DRC
//
TOOL_ACTION PCB_ACTIONS::runDRC( TOOL_ACTION_ARGS()
        .Name( "pcbnew.DRCTool.runDRC" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Design Rules Checker" ) )
        .Tooltip( _( "Show the design rules checker window" ) )
        .Icon( BITMAPS::erc ) );

// PCB_DESIGN_BLOCK_CONTROL
TOOL_ACTION PCB_ACTIONS::placeDesignBlock( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveDrawing.placeDesignBlock" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_SHIFT + 'B' )
        .FriendlyName( _( "Place Design Block" ) )
        .Tooltip( _( "Add selected design block to current board" ) )
        .Icon( BITMAPS::add_component )
        .Flags( AF_ACTIVATE )
        .Parameter<DESIGN_BLOCK*>( nullptr ) );

TOOL_ACTION PCB_ACTIONS::placeLinkedDesignBlock( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveDrawing.placeLinkedDesignBlock" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Place Linked Design Block" ) )
        .Tooltip( _( "Place design block linked to selected group" ) )
        .Icon( BITMAPS::add_component )
        .Flags( AF_ACTIVATE )
        .Parameter<bool*>( nullptr ) );

TOOL_ACTION PCB_ACTIONS::applyDesignBlockLayout( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveDrawing.applyDesignBlockLayout" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Apply Design Block Layout" ) )
        .Tooltip( _( "Apply linked design block layout to selected group" ) )
        .Icon( BITMAPS::add_component )
        .Flags( AF_ACTIVATE ) );

TOOL_ACTION PCB_ACTIONS::saveToLinkedDesignBlock( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveDrawing.saveToLinkedDesignBlock" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Save to Linked Design Block" ) )
        .Tooltip( _( "Save selected group to linked design block" ) )
        .Icon( BITMAPS::add_component )
        .Flags( AF_ACTIVATE ) );

TOOL_ACTION PCB_ACTIONS::showDesignBlockPanel( TOOL_ACTION_ARGS()
        .Name( "pcbnew.PcbDesignBlockControl.showDesignBlockPanel" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Design Blocks" ) )
        .Tooltip( _( "Show/hide design blocks library" ) )
        .Icon( BITMAPS::search_tree ) );

TOOL_ACTION PCB_ACTIONS::saveBoardAsDesignBlock( TOOL_ACTION_ARGS()
        .Name( "pcbnew.PcbDesignBlockControl.saveBoardAsDesignBlock" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Save Board as Design Block..." ) )
        .Tooltip( _( "Create a new design block from the current board" ) )
        .Icon( BITMAPS::new_component ) );

TOOL_ACTION PCB_ACTIONS::saveSelectionAsDesignBlock( TOOL_ACTION_ARGS()
        .Name( "pcbnew.PcbDesignBlockControl.saveSelectionAsDesignBlock" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Save Selection as Design Block..." ) )
        .Tooltip( _( "Create a new design block from the current selection" ) )
        .Icon( BITMAPS::new_component ) );

TOOL_ACTION PCB_ACTIONS::updateDesignBlockFromBoard( TOOL_ACTION_ARGS()
        .Name( "pcbnew.PcbDesignBlockControl.updateDesignBlockFromBoard" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Update Design Block from Board" ) )
        .Tooltip( _( "Set design block layout to current board" ) )
        .Icon( BITMAPS::save ) );

TOOL_ACTION PCB_ACTIONS::updateDesignBlockFromSelection( TOOL_ACTION_ARGS()
        .Name( "pcbnew.PcbDesignBlockControl.updateDesignBlockFromSelection" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Update Design Block from Selection" ) )
        .Tooltip( _( "Set design block layout to current selection" ) )
        .Icon( BITMAPS::save ) );

TOOL_ACTION PCB_ACTIONS::deleteDesignBlock( TOOL_ACTION_ARGS()
        .Name( "pcbnew.PcbDesignBlockControl.deleteDesignBlock" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Delete Design Block" ) )
        .Tooltip( _( "Remove the selected design block from its library" ) )
        .Icon( BITMAPS::trash ) );

TOOL_ACTION PCB_ACTIONS::editDesignBlockProperties( TOOL_ACTION_ARGS()
        .Name( "pcbnew.PcbDesignBlockControl.editDesignBlockProperties" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Properties..." ) )
        .Tooltip( _( "Edit properties of design block" ) )
        .Icon( BITMAPS::edit ) );

// EDIT_TOOL
//
TOOL_ACTION PCB_ACTIONS::editFpInFpEditor( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.EditFpInFpEditor" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_CTRL + 'E' )
        .LegacyHotkeyName( "Edit with Footprint Editor" )
        .FriendlyName( _( "Open in Footprint Editor" ) )
        .Icon( BITMAPS::module_editor ) );

TOOL_ACTION PCB_ACTIONS::editLibFpInFpEditor( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.EditLibFpInFpEditor" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_CTRL + MD_SHIFT + 'E' )
        .FriendlyName( _( "Edit Library Footprint..." ) )
        .Icon( BITMAPS::module_editor ) );

TOOL_ACTION PCB_ACTIONS::toggleExcludeFromBOM( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveEdit.toggleExcludeFromBOM" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Exclude from Bill of Materials" ) )
        .Tooltip( _( "Toggle the exclude from bill of materials attribute" ) ) );

TOOL_ACTION PCB_ACTIONS::toggleExcludeFromPosFiles( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveEdit.toggleExcludeFromPosFiles" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Exclude from Position Files" ) )
        .Tooltip( _( "Toggle the exclude from position files attribute" ) ) );

TOOL_ACTION PCB_ACTIONS::getAndPlace( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveEdit.FindMove" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( 'T' )
        .LegacyHotkeyName( "Get and Move Footprint" )
        .FriendlyName( _( "Get and Move Footprint" ) )
        .Tooltip( _( "Selects a footprint by reference designator and places it under the cursor for moving" ) )
        .Icon( BITMAPS::move )
        .Flags( AF_ACTIVATE ) );

TOOL_ACTION PCB_ACTIONS::move( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveMove.move" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( 'M' )
        .LegacyHotkeyName( "Move Item" )
        .FriendlyName( _( "Move" ) )
        .Icon( BITMAPS::move )
        .Flags( AF_ACTIVATE )
        .Parameter( ACTIONS::CURSOR_EVENT_TYPE::CURSOR_NONE ) );

TOOL_ACTION PCB_ACTIONS::moveIndividually( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveMove.moveIndividually" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_CTRL + 'M' )
        .FriendlyName( _( "Move Individually" ) )
        .Tooltip( _( "Moves the selected items one-by-one" ) )
        .Icon( BITMAPS::move )
        .Flags( AF_ACTIVATE )
        .Parameter( ACTIONS::CURSOR_EVENT_TYPE::CURSOR_NONE ) );

TOOL_ACTION PCB_ACTIONS::moveWithReference( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveMove.moveWithReference" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Move with Reference..." ) )
        .Tooltip( _( "Moves the selected item(s) with a specified starting point" ) )
        .Icon( BITMAPS::move )
        .Flags( AF_ACTIVATE )
        .Parameter( ACTIONS::CURSOR_EVENT_TYPE::CURSOR_NONE ) );

TOOL_ACTION PCB_ACTIONS::copyWithReference( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveMove.copyWithReference" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Copy with Reference..." ) )
        .Tooltip( _( "Copy selected item(s) to clipboard with a specified starting point" ) )
        .Icon( BITMAPS::copy )
        .Flags( AF_ACTIVATE ) );

TOOL_ACTION PCB_ACTIONS::duplicateIncrement(TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveEdit.duplicateIncrementPads" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_CTRL + MD_SHIFT + 'D' )
        .LegacyHotkeyName( "Duplicate Item and Increment" )
        .FriendlyName( _( "Duplicate and Increment" ) )
        .Tooltip( _( "Duplicates the selected item(s), incrementing pad numbers" ) )
        .Icon( BITMAPS::duplicate ) );

TOOL_ACTION PCB_ACTIONS::moveExact( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveEdit.moveExact" )
        .Scope( AS_GLOBAL )
        .LegacyHotkeyName( "Move Item Exactly" )
        .FriendlyName( _( "Move Exactly..." ) )
        .Tooltip( _( "Moves the selected item(s) by an exact amount" ) )
        .Icon( BITMAPS::move_exactly ) );

TOOL_ACTION PCB_ACTIONS::pointEditorMoveCorner( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveEdit.moveCorner" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Move Corner To..." ) )
        .Tooltip( _( "Move the active corner to an exact location" ) )
        .Icon( BITMAPS::move_exactly ) );

TOOL_ACTION PCB_ACTIONS::pointEditorMoveMidpoint( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveEdit.moveMidpoint" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Move Midpoint To..." ) )
        .Tooltip( _( "Move the active midpoint to an exact location" ) )
        .Icon( BITMAPS::move_exactly ) );

TOOL_ACTION PCB_ACTIONS::rotateCw( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveEdit.rotateCw" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_SHIFT + 'R' )
        // Don't be tempted to remove "Modern Toolset only".  It's in the legacy property name.
        .LegacyHotkeyName( "Rotate Item Clockwise (Modern Toolset only)" )
        .FriendlyName( _( "Rotate Clockwise" ) )
        .Icon( BITMAPS::rotate_cw )
        .Flags( AF_NONE )
        .Parameter( -1 ) );

TOOL_ACTION PCB_ACTIONS::rotateCcw( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveEdit.rotateCcw" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( 'R' )
        .LegacyHotkeyName( "Rotate Item" )
        .FriendlyName( _( "Rotate Counterclockwise" ) )
        .Icon( BITMAPS::rotate_ccw )
        .Flags( AF_NONE )
        .Parameter( 1 ) );

TOOL_ACTION PCB_ACTIONS::flip( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveEdit.flip" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( 'F' )
        .LegacyHotkeyName( "Flip Item" )
        .FriendlyName( _( "Change Side / Flip" ) )
        .Tooltip( _( "Flips selected item(s) to opposite side of board" ) )
        .Icon( BITMAPS::swap_layer ) );

TOOL_ACTION PCB_ACTIONS::mirrorH( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveEdit.mirrorHoriontally" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Mirror Horizontally" ) )
        .Tooltip( _( "Mirrors selected item(s) across the Y axis" ) )
        .Icon( BITMAPS::mirror_h ) );

TOOL_ACTION PCB_ACTIONS::mirrorV( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveEdit.mirrorVertically" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Mirror Vertically" ) )
        .Tooltip( _( "Mirrors selected item(s) across the X axis" ) )
        .Icon( BITMAPS::mirror_v ) );

TOOL_ACTION PCB_ACTIONS::swap( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveEdit.swap" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_ALT + 'S' )
        .FriendlyName( _( "Swap" ) )
        .Tooltip( _( "Swap positions of selected items" ) )
        .Icon( BITMAPS::swap ) );

TOOL_ACTION PCB_ACTIONS::swapComponents( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveEdit.swapComponents" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Swap Components" ) )
        .Tooltip( _( "Swap the locations of two components: the selected ones, or click one "
                     "then the other" ) )
        .Icon( BITMAPS::swap )
        .Flags( AF_ACTIVATE ) );

TOOL_ACTION PCB_ACTIONS::swapPadNets( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveEdit.swapPadNets" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Swap Pad Nets" ) )
        .Tooltip( _( "Swap nets between two selected pads and their connected copper" ) )
        .Icon( BITMAPS::swap ) );

TOOL_ACTION PCB_ACTIONS::swapGateNets( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveEdit.swapGateNets" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Swap Gate Nets" ) )
        .Tooltip( _( "Swap nets between gates of a footprint and their connected copper" ) )
        .Parameter<wxString>( wxString() )
        .Icon( BITMAPS::swap ) );

TOOL_ACTION PCB_ACTIONS::packAndMoveFootprints( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveEdit.packAndMoveFootprints" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( 'P' )
        .FriendlyName( _( "Pack and Move Footprints" ) )
        .Tooltip( _( "Sorts selected footprints by reference, packs based on size and initiates movement" ) )
        .Icon( BITMAPS::pack_footprints ) );

TOOL_ACTION PCB_ACTIONS::skip( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveEdit.skip" )
        .Scope( AS_CONTEXT )
        .DefaultHotkey( WXK_TAB )
        .FriendlyName( _( "Skip" ) )
        .Tooltip( _( "Skip to next item" ) )
        .Icon( BITMAPS::right ) );

TOOL_ACTION PCB_ACTIONS::changeTrackWidth( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveEdit.changeTrackWidth" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Change Track Width" ) )
        .Tooltip( _( "Updates selected track & via sizes" ) ) );

TOOL_ACTION PCB_ACTIONS::changeTrackLayerNext( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.changeTrackLayerNext" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_CTRL + '+' )
        .FriendlyName( "Switch Track to Next Layer" )
        .Tooltip( _( "Switch track to next enabled copper layer" ) ) );

TOOL_ACTION PCB_ACTIONS::changeTrackLayerPrev( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.changeTrackLayerPrev" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_CTRL + '-' )
        .FriendlyName( "Switch Track to Previous Layer" )
        .Tooltip( _( "Switch track to previous enabled copper layer" ) ) );

TOOL_ACTION PCB_ACTIONS::filletTracks( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveEdit.filletTracks" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Fillet Tracks" ) )
        .Tooltip( _( "Adds arcs tangent to the selected straight track segments" ) ) );

TOOL_ACTION PCB_ACTIONS::filletLines( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveEdit.filletLines" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Fillet Lines..." ) )
        .Tooltip( _( "Adds arcs tangent to the selected lines" ) )
        .Icon( BITMAPS::fillet ) );

TOOL_ACTION PCB_ACTIONS::chamferLines( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveEdit.chamferLines" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Chamfer Lines..." ) )
        .Tooltip( _( "Cut away corners between selected lines" ) )
        .Icon( BITMAPS::chamfer ) );

TOOL_ACTION PCB_ACTIONS::dogboneCorners( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveEdit.dogboneCorners" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Dogbone Corners..." ) )
        .Tooltip( _( "Add dogbone corners to selected lines" ) ) );

TOOL_ACTION PCB_ACTIONS::simplifyPolygons( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveEdit.simplifyPolygons" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Simplify Polygons" ) )
        .Tooltip( _( "Simplify polygon outlines, removing superfluous points" ) ) );

TOOL_ACTION PCB_ACTIONS::editVertices( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveEdit.editVertices" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Edit Corners..." ) )
        .Tooltip( _( "Edit polygon corners using a table" ) )
        .Icon( BITMAPS::edit ) );

TOOL_ACTION PCB_ACTIONS::healShapes( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveEdit.healShapes" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Heal Shapes" ) )
        .Tooltip( _( "Connect shapes, possibly extending or cutting them, or adding extra geometry" ) )
        .Icon( BITMAPS::heal_shapes ) );

TOOL_ACTION PCB_ACTIONS::extendLines( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveEdit.extendLines" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Extend Lines to Meet" ) )
        .Tooltip( _( "Extend lines to meet each other" ) ) );

TOOL_ACTION PCB_ACTIONS::mergePolygons( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveEdit.mergePolygons" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Merge Polygons" ) )
        .Tooltip( _( "Merge selected polygons into a single polygon" ) )
        .Icon( BITMAPS::merge_polygons ) );

TOOL_ACTION PCB_ACTIONS::subtractPolygons( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveEdit.subtractPolygons" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Subtract Polygons" ) )
        .Tooltip( _( "Subtract selected polygons from the last one selected" ) )
        .Icon( BITMAPS::subtract_polygons ) );

TOOL_ACTION PCB_ACTIONS::intersectPolygons( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveEdit.intersectPolygons" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Intersect Polygons" ) )
        .Tooltip( _( "Create the intersection of the selected polygons" ) )
        .Icon( BITMAPS::intersect_polygons ) );

TOOL_ACTION PCB_ACTIONS::deleteFull( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveEdit.deleteFull" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_SHIFT + static_cast<int>( WXK_DELETE ) )
        .LegacyHotkeyName( "Delete Full Track" )
        .FriendlyName( _( "Delete Full Track" ) )
        .Tooltip( _( "Deletes selected item(s) and copper connections" ) )
        .Icon( BITMAPS::delete_cursor )
        .Flags( AF_NONE )
        .Parameter( PCB_ACTIONS::REMOVE_FLAGS::ALT ) );

TOOL_ACTION PCB_ACTIONS::properties( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveEdit.properties" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( 'E' )
        .LegacyHotkeyName( "Edit Item" )
        .FriendlyName( _( "Properties..." ) )
        .Icon( BITMAPS::edit ) );

// ARRAY
//
TOOL_ACTION PCB_ACTIONS::createArray( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Array.createArray" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_CTRL + 'T' )
        .LegacyHotkeyName( "Create Array" )
        .FriendlyName( _( "Create Array..." ) )
        .Icon( BITMAPS::array )
        .Flags( AF_ACTIVATE ) );

// FOOTPRINT_EDITOR_CONTROL
//
TOOL_ACTION PCB_ACTIONS::newProjectLibrary( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ModuleEditor.newProjectLibrary" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "New Project PCB Library" ) )
        .Tooltip( _( "Add a new PCB library to the project" ) )
        .Icon( BITMAPS::new_library ) );

TOOL_ACTION PCB_ACTIONS::newFootprint( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ModuleEditor.newFootprint" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_CTRL + 'N' )
        .LegacyHotkeyName( "New" )
        .FriendlyName( _( "New Footprint" ) )
        .Tooltip( _( "Create a new, empty footprint" ) )
        .Icon( BITMAPS::new_footprint ) );

TOOL_ACTION PCB_ACTIONS::createFootprint( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ModuleEditor.createFootprint" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Create Footprint..." ) )
        .Tooltip( _( "Create a new footprint using the Footprint Wizard" ) )
        .Icon( BITMAPS::module_wizard ) );

TOOL_ACTION PCB_ACTIONS::ipcFootprintWizard( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ModuleEditor.ipcFootprintWizard" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "IPC Compliant Footprint Wizard..." ) )
        .Tooltip( _( "Create an IPC-7351 land pattern from the package dimensions" ) )
        .Icon( BITMAPS::module_wizard ) );

TOOL_ACTION PCB_ACTIONS::updateAllFootprintsOnBoard( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ModuleEditor.updateAllFootprintsOnBoard" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Update PCB With All Footprints" ) )
        .Tooltip( _( "Update every component on the board that uses a footprint of this library" ) )
        .Icon( BITMAPS::insert_module_board ) );

TOOL_ACTION PCB_ACTIONS::editFootprint( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ModuleEditor.editFootprint" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Edit Footprint" ) )
        .Tooltip( _( "Show selected footprint on editor canvas" ) )
        .Icon( BITMAPS::edit ) );

TOOL_ACTION PCB_ACTIONS::duplicateFootprint( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ModuleEditor.duplicateFootprint" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Duplicate Footprint" ) )
        .Icon( BITMAPS::duplicate ) );

TOOL_ACTION PCB_ACTIONS::renameFootprint( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ModuleEditor.renameFootprint" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Rename Footprint..." ) )
        .Icon( BITMAPS::edit ) );

TOOL_ACTION PCB_ACTIONS::deleteFootprint( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ModuleEditor.deleteFootprint" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Delete Footprint from Library" ) )
        .Icon( BITMAPS::trash ) );

TOOL_ACTION PCB_ACTIONS::cutFootprint( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ModuleEditor.cutFootprint" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Cut Footprint" ) )
        .Icon( BITMAPS::cut ) );

TOOL_ACTION PCB_ACTIONS::copyFootprint( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ModuleEditor.copyFootprint" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Copy Footprint" ) )
        .Icon( BITMAPS::copy ) );

TOOL_ACTION PCB_ACTIONS::pasteFootprint( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ModuleEditor.pasteFootprint" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Paste Footprint" ) )
        .Icon( BITMAPS::paste ) );

TOOL_ACTION PCB_ACTIONS::importFootprint( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ModuleEditor.importFootprint" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Import Footprint..." ) )
        .Tooltip( _( "Import footprint from file" ) )
        .Icon( BITMAPS::import_module ) );

TOOL_ACTION PCB_ACTIONS::exportFootprint( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ModuleEditor.exportFootprint" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Export Current Footprint..." ) )
        .Tooltip( _( "Export edited footprint to file" ) )
        .Icon( BITMAPS::export_module ) );

TOOL_ACTION PCB_ACTIONS::footprintProperties( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ModuleEditor.footprintProperties" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Footprint Properties..." ) )
        .Icon( BITMAPS::module_options ) );

TOOL_ACTION PCB_ACTIONS::padTable( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ModuleEditor.padTable" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Pad Table..." ) )
        .Tooltip( _( "Displays pad table for bulk editing of pads" ) )
        .Icon( BITMAPS::pin_table ) );

TOOL_ACTION PCB_ACTIONS::checkFootprint( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ModuleEditor.checkFootprint" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Footprint Checker" ) )
        .Tooltip( _( "Show the footprint checker window" ) )
        .Icon( BITMAPS::erc ) );

TOOL_ACTION PCB_ACTIONS::loadFpFromBoard( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ModuleEditor.loadFootprintFromBoard" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Load footprint from current PCB" ) )
        .Tooltip( _( "Load footprint from current board" ) )
        .Icon( BITMAPS::load_module_board ) );

TOOL_ACTION PCB_ACTIONS::saveFpToBoard( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ModuleEditor.saveFootprintToBoard" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Insert footprint into PCB" ) )
        .Tooltip( _( "Insert footprint into current board" ) )
        .Icon( BITMAPS::insert_module_board) );

TOOL_ACTION PCB_ACTIONS::previousFootprint( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.previousFootprint" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Display previous footprint" ) )
        .Icon( BITMAPS::lib_previous )
        .Parameter<FPVIEWER_CONSTANTS>( FPVIEWER_CONSTANTS::PREVIOUS_PART ) );

TOOL_ACTION PCB_ACTIONS::nextFootprint( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.nextFootprint" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Display next footprint" ) )
        .Icon( BITMAPS::lib_next )
        .Parameter<FPVIEWER_CONSTANTS>( FPVIEWER_CONSTANTS::NEXT_PART ) );

// GLOBAL_EDIT_TOOL
//
TOOL_ACTION PCB_ACTIONS::updateFootprint( TOOL_ACTION_ARGS()
        .Name( "pcbnew.GlobalEdit.updateFootprint" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Update Footprint..." ) )
        .Tooltip( _( "Update footprint to include any changes from the library" ) )
        .Icon( BITMAPS::refresh ) );

TOOL_ACTION PCB_ACTIONS::updateFootprints( TOOL_ACTION_ARGS()
        .Name( "pcbnew.GlobalEdit.updateFootprints" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Update Footprints from Library..." ) )
        .Tooltip( _( "Update footprints to include any changes from the library" ) )
        .Icon( BITMAPS::refresh ) );

TOOL_ACTION PCB_ACTIONS::migrate3DModels( TOOL_ACTION_ARGS()
        .Name( "pcbnew.GlobalEdit.migrate3DModels" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Migrate 3D Models..." ) )
        .Tooltip( _( "Replace obsolete WRL 3D model references with current STEP models" ) )
        .Icon( BITMAPS::refresh ) );

TOOL_ACTION PCB_ACTIONS::removeUnusedPads( TOOL_ACTION_ARGS()
        .Name( "pcbnew.GlobalEdit.removeUnusedPads" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Remove Unused Pads..." ) )
        .Tooltip( _( "Remove or restore the unconnected inner layers on through hole pads and vias" ) )
        .Icon( BITMAPS::pads_remove ) );

TOOL_ACTION PCB_ACTIONS::changeFootprint( TOOL_ACTION_ARGS()
        .Name( "pcbnew.GlobalEdit.changeFootprint" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Change Footprint..." ) )
        .Tooltip( _( "Assign a different footprint from the library" ) )
        .Icon( BITMAPS::exchange ) );

TOOL_ACTION PCB_ACTIONS::changeFootprints( TOOL_ACTION_ARGS()
        .Name( "pcbnew.GlobalEdit.changeFootprints" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Change Footprints..." ) )
        .Tooltip( _( "Assign different footprints from the library" ) )
        .Icon( BITMAPS::exchange ) );

TOOL_ACTION PCB_ACTIONS::swapLayers( TOOL_ACTION_ARGS()
        .Name( "pcbnew.GlobalEdit.swapLayers" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Swap Layers..." ) )
        .Tooltip( _( "Move tracks or drawings from one layer to another" ) )
        .Icon( BITMAPS::swap_layer ) );

TOOL_ACTION PCB_ACTIONS::editTracksAndVias( TOOL_ACTION_ARGS()
        .Name( "pcbnew.GlobalEdit.editTracksAndVias" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Edit Track & Via Properties..." ) )
        .Tooltip( _( "Edit track and via properties globally across board" ) )
        .Icon( BITMAPS::width_track_via ) );

TOOL_ACTION PCB_ACTIONS::editTextAndGraphics( TOOL_ACTION_ARGS()
        .Name( "pcbnew.GlobalEdit.editTextAndGraphics" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Edit Text & Graphics Properties..." ) )
        .Tooltip( _( "Edit Text and graphics properties globally across board" ) )
        .Icon( BITMAPS::text ) );

TOOL_ACTION PCB_ACTIONS::editTeardrops( TOOL_ACTION_ARGS()
        .Name( "pcbnew.GlobalEdit.editTeardrops" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Edit Teardrops..." ) )
        .Tooltip( _( "Add, remove or edit teardrops globally across board" ) )
        .Icon( BITMAPS::via ) );

TOOL_ACTION PCB_ACTIONS::globalDeletions( TOOL_ACTION_ARGS()
        .Name( "pcbnew.GlobalEdit.globalDeletions" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Global Deletions..." ) )
        .Tooltip( _( "Delete tracks, footprints and graphic items from board" ) )
        .Icon( BITMAPS::general_deletions ) );

TOOL_ACTION PCB_ACTIONS::cleanupTracksAndVias( TOOL_ACTION_ARGS()
        .Name( "pcbnew.GlobalEdit.cleanupTracksAndVias" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Cleanup Tracks & Vias..." ) )
        .Tooltip( _( "Cleanup redundant items, shorting items, etc." ) )
        .Icon( BITMAPS::cleanup_tracks_and_vias ) );

TOOL_ACTION PCB_ACTIONS::cleanupGraphics( TOOL_ACTION_ARGS()
        .Name( "pcbnew.GlobalEdit.cleanupGraphics" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Cleanup Graphics..." ) )
        .Tooltip( _( "Cleanup redundant items, etc." ) )
        .Icon( BITMAPS::cleanup_graphics ) );

// MICROWAVE_TOOL
//
TOOL_ACTION PCB_ACTIONS::microwaveCreateGap( TOOL_ACTION_ARGS()
        .Name( "pcbnew.MicrowaveTool.createGap" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Draw Microwave Gaps" ) )
        .Tooltip( _( "Create gap of specified length for microwave applications" ) )
        .Icon( BITMAPS::mw_add_gap )
        .Flags( AF_ACTIVATE )
        .Parameter( MICROWAVE_FOOTPRINT_SHAPE::GAP ) );

TOOL_ACTION PCB_ACTIONS::microwaveCreateStub( TOOL_ACTION_ARGS()
        .Name( "pcbnew.MicrowaveTool.createStub" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Draw Microwave Stubs" ) )
        .Tooltip( _( "Create stub of specified length for microwave applications" ) )
        .Icon( BITMAPS::mw_add_stub )
        .Flags( AF_ACTIVATE )
        .Parameter( MICROWAVE_FOOTPRINT_SHAPE::STUB ) );

TOOL_ACTION PCB_ACTIONS::microwaveCreateStubArc( TOOL_ACTION_ARGS()
        .Name( "pcbnew.MicrowaveTool.createStubArc" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Draw Microwave Arc Stubs" ) )
        .Tooltip( _( "Create stub (arc) of specified size for microwave applications" ) )
        .Icon( BITMAPS::mw_add_stub_arc )
        .Flags( AF_ACTIVATE )
        .Parameter( MICROWAVE_FOOTPRINT_SHAPE::STUB_ARC ) );

TOOL_ACTION PCB_ACTIONS::microwaveCreateFunctionShape( TOOL_ACTION_ARGS()
        .Name( "pcbnew.MicrowaveTool.createFunctionShape" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Draw Microwave Polygonal Shapes" ) )
        .Tooltip( _( "Create a microwave polygonal shape from a list of vertices" ) )
        .Icon( BITMAPS::mw_add_shape )
        .Flags( AF_ACTIVATE )
        .Parameter( MICROWAVE_FOOTPRINT_SHAPE::FUNCTION_SHAPE ) );

TOOL_ACTION PCB_ACTIONS::microwaveCreateLine( TOOL_ACTION_ARGS()
        .Name( "pcbnew.MicrowaveTool.createLine" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Draw Microwave Lines" ) )
        .Tooltip( _( "Create line of specified length for microwave applications" ) )
        .Icon( BITMAPS::mw_add_line )
        .Flags( AF_ACTIVATE ) );


// PAD_TOOL
//
TOOL_ACTION PCB_ACTIONS::copyPadSettings( TOOL_ACTION_ARGS()
        .Name( "pcbnew.PadTool.CopyPadSettings" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Copy Pad Properties to Default" ) )
        .Tooltip( _( "Copy current pad's properties" ) )
        .Icon( BITMAPS::copy_pad_settings ) );

TOOL_ACTION PCB_ACTIONS::applyPadSettings( TOOL_ACTION_ARGS()
        .Name( "pcbnew.PadTool.ApplyPadSettings" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Paste Default Pad Properties to Selected" ) )
        .Tooltip( _( "Replace the current pad's properties with those copied earlier" ) )
        .Icon( BITMAPS::apply_pad_settings ) );

TOOL_ACTION PCB_ACTIONS::pushPadSettings( TOOL_ACTION_ARGS()
        .Name( "pcbnew.PadTool.PushPadSettings" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Push Pad Properties to Other Pads..." ) )
        .Tooltip( _( "Copy the current pad's properties to other pads" ) )
        .Icon( BITMAPS::push_pad_settings ) );

TOOL_ACTION PCB_ACTIONS::enumeratePads( TOOL_ACTION_ARGS()
        .Name( "pcbnew.PadTool.enumeratePads" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Renumber Pads..." ) )
        .Tooltip( _( "Renumber pads by clicking on them in the desired order" ) )
        .Icon( BITMAPS::pad_enumerate )
        .Flags( AF_ACTIVATE ) );

TOOL_ACTION PCB_ACTIONS::placePad( TOOL_ACTION_ARGS()
        .Name( "pcbnew.PadTool.placePad" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Add Pad" ) )
        .Tooltip( _( "Add a pad" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Icon( BITMAPS::pad )
        .Flags( AF_ACTIVATE ) );

TOOL_ACTION PCB_ACTIONS::explodePad( TOOL_ACTION_ARGS()
        .Name( "pcbnew.PadTool.explodePad" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_CTRL + 'E' )
        .FriendlyName( _( "Edit Pad as Graphic Shapes" ) )
        .Tooltip( _( "Ungroups a custom-shaped pad for editing as individual graphic shapes" ) )
        .Icon( BITMAPS::custom_pad_to_primitives ) );

TOOL_ACTION PCB_ACTIONS::recombinePad( TOOL_ACTION_ARGS()
        .Name( "pcbnew.PadTool.recombinePad" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_CTRL + 'E' )
        .FriendlyName( _( "Finish Pad Edit" ) )
        .Tooltip( _( "Regroups all touching graphic shapes into the edited pad" ) )
        .Icon( BITMAPS::custom_pad_to_primitives ) );

TOOL_ACTION PCB_ACTIONS::defaultPadProperties( TOOL_ACTION_ARGS()
        .Name( "pcbnew.PadTool.defaultPadProperties" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Default Pad Properties..." ) )
        .Tooltip( _( "Edit the pad properties used when creating new pads" ) )
        .Icon( BITMAPS::options_pad ) );


// SCRIPTING TOOL
//

TOOL_ACTION PCB_ACTIONS::pluginsShowFolder( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ScriptingTool.pluginsShowFolder" )
        .Scope( AS_GLOBAL )
#ifdef __WXMAC__
        .FriendlyName( _( "Reveal Plugin Folder in Finder" ) )
        .Tooltip( _( "Reveals the plugins folder in a Finder window" ) )
#else
        .FriendlyName( _( "Open Plugin Directory" ) )
        .Tooltip( _( "Opens the directory in the default system file manager" ) )
#endif
        .Icon( BITMAPS::directory_open ) );

// BOARD_EDITOR_CONTROL
//
TOOL_ACTION PCB_ACTIONS::appendBoard( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.appendBoard" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Append Board..." ) )
        .Tooltip( _( "Open another board and append its contents to this board" ) )
        .Icon( BITMAPS::add_board ) );

TOOL_ACTION PCB_ACTIONS::rescueAutosave( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.rescueAutosave" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Rescue" ) )
        .Tooltip( _( "Clear board and get last rescue file automatically saved by PCB editor" ) )
        .Icon( BITMAPS::rescue ) );

TOOL_ACTION PCB_ACTIONS::openNonKicadBoard( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.openNonKicadBoard" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Import Non-KiCad Board File..." ) )
        .Tooltip( _( "Import board file from other applications" ) )
        .Icon( BITMAPS::import_brd_file ) );

TOOL_ACTION PCB_ACTIONS::exportFootprints( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.exportFootprints" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Export Footprints..." ) )
        .Tooltip( _( "Add footprints from board to a new or an existing footprint library\n"
                     "(does not remove other footprints from this library)" ) )
        .Icon( BITMAPS::library_archive ) );

TOOL_ACTION PCB_ACTIONS::boardSetup( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.boardSetup" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Board Setup..." ) )
        .Tooltip( _( "Edit board setup including layers, design rules and various defaults" ) )
        .Icon( BITMAPS::options_board ) );

TOOL_ACTION PCB_ACTIONS::importNetlist( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.importNetlist" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Import Netlist..." ) )
        .Tooltip( _( "Read netlist and update board connectivity" ) )
        .Icon( BITMAPS::netlist ) );

TOOL_ACTION PCB_ACTIONS::importSpecctraSession( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.importSpecctraSession" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Import Specctra Session..." ) )
        .Tooltip( _( "Import routed Specctra session (*.ses) file" ) )
        .Icon( BITMAPS::import ) );

TOOL_ACTION PCB_ACTIONS::exportSpecctraDSN( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.exportSpecctraDSN" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Export Specctra DSN..." ) )
        .Tooltip( _( "Export Specctra DSN routing info" ) )
        .Icon( BITMAPS::export_dsn ) );

TOOL_ACTION PCB_ACTIONS::generateGerbers( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.generateGerbers" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Gerbers (.gbr)..." ) )
        .Tooltip( _( "Generate Gerbers for fabrication" ) )
        .Icon( BITMAPS::post_gerber ) );

TOOL_ACTION PCB_ACTIONS::generateDrillFiles( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.generateDrillFiles" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Drill Files (.drl)..." ) )
        .Tooltip( _( "Generate Excellon drill file(s)" ) )
        .Icon( BITMAPS::post_drill ) );

TOOL_ACTION PCB_ACTIONS::generatePosFile( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.generatePosFile" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Component Placement (.pos, .gbr)..." ) )
        .Tooltip( _( "Generate component placement file(s) for pick and place" ) )
        .Icon( BITMAPS::post_compo ) );

TOOL_ACTION PCB_ACTIONS::generateReportFile( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.generateReportFile" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Footprint Report (.rpt)..." ) )
        .Tooltip( _( "Create report of all footprints from current board" ) )
        .Icon( BITMAPS::post_rpt ) );

TOOL_ACTION PCB_ACTIONS::generateIPC2581File( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.generateIPC2581File" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "IPC-2581 File (.xml)..." ) )
        .Tooltip( _( "Generate an IPC-2581 file" ) )
        .Icon( BITMAPS::post_xml ) );

TOOL_ACTION PCB_ACTIONS::generateODBPPFile( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.generateODBPPFile" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "ODB++ Output File..." ) )
        .Tooltip( _( "Generate ODB++ output files" ) )
        .Icon( BITMAPS::post_odb ) );

TOOL_ACTION PCB_ACTIONS::generateD356File( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.generateD356File" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "IPC-D-356 Netlist File..." ) )
        .Tooltip( _( "Generate IPC-D-356 netlist file" ) )
        .Icon( BITMAPS::post_d356 ) );

TOOL_ACTION PCB_ACTIONS::generateBOM( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.generateBOM" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Bill of Materials..." ) )
        .Tooltip( _( "Create bill of materials from board" ) )
        .Icon( BITMAPS::post_bom ) );

TOOL_ACTION PCB_ACTIONS::exportGenCAD( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.exportGenCAD" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Export GenCAD..." ) )
        .Tooltip( _( "Export GenCAD board representation" ) )
        .Icon( BITMAPS::post_gencad ) );

TOOL_ACTION PCB_ACTIONS::exportVRML( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.exportVRML" )
        .Scope( AS_GLOBAL )
        .FriendlyName(  _( "Export VRML..." ) )
        .Tooltip( _( "Export VRML 3D board representation" ) )
        .Icon( BITMAPS::export3d ) );

TOOL_ACTION PCB_ACTIONS::exportIDF( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.exportIDF" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Export IDFv3..." ) )
        .Tooltip( _( "Export IDF 3D board representation" ) )
        .Icon( BITMAPS::export_idf ) );

TOOL_ACTION PCB_ACTIONS::exportSTEP( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.exportSTEP" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Export STEP/GLB/BREP/XAO/PLY/STL..." ) )
        .Tooltip( _( "Export STEP, GLB, BREP, XAO, PLY or STL 3D board representation" ) )
        .Icon( BITMAPS::export_step ) );

TOOL_ACTION PCB_ACTIONS::exportCmpFile( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.exportFootprintAssociations" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Export Footprint Association (.cmp) File..." ) )
        .Tooltip( _( "Export footprint association file (*.cmp) for schematic back annotation" ) )
        .Icon( BITMAPS::export_cmp ) );

TOOL_ACTION PCB_ACTIONS::exportHyperlynx( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.exportHyperlynx" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Hyperlynx..." ) )
        .Icon( BITMAPS::export_step ) );

TOOL_ACTION PCB_ACTIONS::collect3DModels( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.collect3DModels" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Collect And Embed 3D Models" ) )
        .Tooltip( _( "Collect footprint 3D models and embed them into the board" ) )
        .Icon( BITMAPS::import3d ) );


// Track & via size control
TOOL_ACTION PCB_ACTIONS::trackWidthInc( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.trackWidthInc" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( 'W' )
        .LegacyHotkeyName( "Switch Track Width To Next" )
        .FriendlyName( _( "Switch Track Width to Next" ) )
        .Tooltip( _( "Change track width to next pre-defined size" ) ) );

TOOL_ACTION PCB_ACTIONS::trackWidthDec( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.trackWidthDec" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_SHIFT + 'W' )
        .LegacyHotkeyName( "Switch Track Width To Previous" )
        .FriendlyName( _( "Switch Track Width to Previous" ) )
        .Tooltip( _( "Change track width to previous pre-defined size" ) ) );

TOOL_ACTION PCB_ACTIONS::viaSizeInc( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.viaSizeInc" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( '\'' )
        .LegacyHotkeyName( "Increase Via Size" )
        .FriendlyName( _( "Increase Via Size" ) )
        .Tooltip( _( "Change via size to next pre-defined size" ) ) );

TOOL_ACTION PCB_ACTIONS::viaSizeDec( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.viaSizeDec" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( '\\' )
        .LegacyHotkeyName( "Decrease Via Size" )
        .FriendlyName( _( "Decrease Via Size" ) )
        .Tooltip( _( "Change via size to previous pre-defined size" ) ) );

TOOL_ACTION PCB_ACTIONS::autoTrackWidth( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.autoTrackWidth" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Automatically select track width" ) )
        .Tooltip( _( "When routing from an existing track use its width instead "
                     "of the current width setting" ) )
        .Icon( BITMAPS::auto_track_width )
        .ToolbarState( TOOLBAR_STATE::TOGGLE ) );

TOOL_ACTION PCB_ACTIONS::trackViaSizeChanged( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.trackViaSizeChanged" )
        .Scope( AS_GLOBAL )
        .Flags( AF_NOTIFY ) );

TOOL_ACTION PCB_ACTIONS::assignNetClass( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.assignNetclass" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Assign Netclass..." ) )
        .Tooltip( _( "Assign a netclass to nets matching a pattern" ) )
        .Icon( BITMAPS::netlist ) );

TOOL_ACTION PCB_ACTIONS::zoneMerge( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.zoneMerge" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Merge Zones" ) ) );

TOOL_ACTION PCB_ACTIONS::zoneDuplicate( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.zoneDuplicate" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Duplicate Zone onto Layer..." ) )
        .Icon( BITMAPS::zone_duplicate ) );

TOOL_ACTION PCB_ACTIONS::zonePriorityMoveToTop( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.zonePriorityMoveToTop" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Move to Top" ) )
        .Icon( BITMAPS::small_up ) );

TOOL_ACTION PCB_ACTIONS::zonePriorityRaise( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.zonePriorityRaise" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Raise" ) )
        .Icon( BITMAPS::small_up ) );

TOOL_ACTION PCB_ACTIONS::zonePriorityLower( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.zonePriorityLower" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Lower" ) )
        .Icon( BITMAPS::small_down ) );

TOOL_ACTION PCB_ACTIONS::zonePriorityMoveToBottom( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.zonePriorityMoveToBottom" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Move to Bottom" ) )
        .Icon( BITMAPS::small_down ) );

TOOL_ACTION PCB_ACTIONS::placeFootprint( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.placeFootprint" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( 'A' )
        .LegacyHotkeyName( "Add Footprint" )
        .FriendlyName( _( "Place Footprints" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Icon( BITMAPS::module )
        .Flags( AF_ACTIVATE )
        .Parameter<FOOTPRINT*>( nullptr ) );

TOOL_ACTION PCB_ACTIONS::drillOrigin( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.drillOrigin" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Drill/Place File Origin" ) )
        .Tooltip( _( "Place origin point for drill files and component placement files" ) )
        .Icon( BITMAPS::set_origin )
        .Flags( AF_ACTIVATE ) );

TOOL_ACTION PCB_ACTIONS::drillResetOrigin( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.drillResetOrigin" )
        .Scope( AS_GLOBAL )
        .LegacyHotkeyName( "Reset Drill Origin" )
        .FriendlyName( _( "Reset Drill Origin" ) ) );

TOOL_ACTION PCB_ACTIONS::drillSetOrigin( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.drillSetOrigin" )
        .Scope( AS_CONTEXT )
        .Parameter( VECTOR2I() ) );

TOOL_ACTION PCB_ACTIONS::toggleLock( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.toggleLock" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( 'L' )
        .LegacyHotkeyName( "Lock/Unlock Footprint" )
        .FriendlyName( _( "Toggle Lock" ) )
        .Tooltip( _( "Lock or unlock selected items" ) )
        .Icon( BITMAPS::lock_unlock ) );

// Line mode grouping and events (for PCB and Footprint editors)
TOOL_ACTION PCB_ACTIONS::lineModeFree( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.lineModeFree" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Line Modes" ) )
        .Tooltip( _( "Draw and drag at any angle" ) )
        .Icon( BITMAPS::lines_any )
        .Flags( AF_NONE )
        .Parameter( LEADER_MODE::DIRECT ) );

TOOL_ACTION PCB_ACTIONS::lineMode90( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.lineModeOrthonal" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Line Modes" ) )
        .Tooltip( _( "Constrain drawing and dragging to horizontal or vertical motions" ) )
        .Icon( BITMAPS::lines90 )
        .Flags( AF_NONE )
        .Parameter( LEADER_MODE::DEG90 ) );

TOOL_ACTION PCB_ACTIONS::lineMode45( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.lineMode45" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Line Modes" ) )
        .Tooltip( _( "Constrain drawing and dragging to horizontal, vertical, or 45-degree angle motions" ) )
        .Icon( BITMAPS::hv45mode )
        .Flags( AF_NONE )
        .Parameter( LEADER_MODE::DEG45 ) );

TOOL_ACTION PCB_ACTIONS::lineModeNext( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.lineModeNext" )
        .DefaultHotkey( MD_SHIFT + ' ' )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Line Modes" ) )
        .Tooltip( _( "Switch to next angle snapping mode" ) ) );

TOOL_ACTION PCB_ACTIONS::angleSnapModeChanged( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.angleSnapModeChanged" )
        .Scope( AS_GLOBAL )
        .Flags( AF_NOTIFY ) );

TOOL_ACTION PCB_ACTIONS::lock( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.lock" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Lock" ) )
        .Tooltip( _( "Prevent items from being moved and/or resized on the canvas" ) )
        .Icon( BITMAPS::locked ) );

TOOL_ACTION PCB_ACTIONS::unlock( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.unlock" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Unlock" ) )
        .Tooltip( _( "Allow items to be moved and/or resized on the canvas" ) )
        .Icon( BITMAPS::unlocked ) );

TOOL_ACTION PCB_ACTIONS::highlightNet( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.highlightNet" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( '`' )
        // Don't be tempted to remove "Modern Toolset only".  It's in the legacy property name.
        .LegacyHotkeyName( "Toggle Highlight of Selected Net (Modern Toolset only)" )
        .FriendlyName( _( "Highlight Net" ) )
        .Tooltip( _( "Highlight net under cursor" ) )
        .Icon( BITMAPS::net_highlight )
        .Parameter<int>( 0 ) );

TOOL_ACTION PCB_ACTIONS::toggleLastNetHighlight( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.toggleLastNetHighlight" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Toggle Last Net Highlight" ) )
        .Tooltip( _( "Toggle between last two highlighted nets" ) )
        .Parameter<int>( 0 ) );

TOOL_ACTION PCB_ACTIONS::clearHighlight( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.clearHighlight" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( '~' )
        .FriendlyName( _( "Clear Net Highlighting" ) ) );

TOOL_ACTION PCB_ACTIONS::toggleNetHighlight( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.toggleNetHighlight" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_ALT + '`' )
        .FriendlyName( _( "Toggle Net Highlight" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Icon( BITMAPS::net_highlight )
        .Parameter<int>( 0 ) );

TOOL_ACTION PCB_ACTIONS::highlightNetSelection( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.highlightNetSelection" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Highlight Net" ) )
        .Tooltip( _( "Highlight all copper items on the selected net(s)" ) )
        .Icon( BITMAPS::net_highlight )
        .Parameter<int>( 0 ) );

TOOL_ACTION PCB_ACTIONS::highlightItem( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.highlightItem" )
        .Scope( AS_GLOBAL ) );

TOOL_ACTION PCB_ACTIONS::hideNetInRatsnest( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.hideNet" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Hide Net in Ratsnest" ) )
        .Tooltip( _( "Hide the selected net in the ratsnest of unconnected net lines/arcs" ) )
        .Icon( BITMAPS::hide_ratsnest )
        .Parameter<int>( 0 ) );    // Default to hiding selected net

TOOL_ACTION PCB_ACTIONS::showNetInRatsnest( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.showNet" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Show Net in Ratsnest" ) )
        .Tooltip( _( "Show the selected net in the ratsnest of unconnected net lines/arcs" ) )
        .Icon( BITMAPS::show_ratsnest )
        .Parameter<int>( 0 ) );    // Default to showing selected net

TOOL_ACTION PCB_ACTIONS::showEeschema( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.showEeschema" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Switch to Schematic Editor" ) )
        .Tooltip( _( "Open schematic in schematic editor" ) )
        .Icon( BITMAPS::icon_eeschema_24 ) );

// DESIGN RULE EDITOR
TOOL_ACTION PCB_ACTIONS::drcRuleEditor( TOOL_ACTION_ARGS()
        .Name( "pcbnew.DRETool.drcRuleEditor" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "DRC Rule Editor" ) )
        .Tooltip( _( "Open DRC rule editor window" ) ) );

TOOL_ACTION PCB_ACTIONS::ruleWizard( TOOL_ACTION_ARGS()
        .Name( "pcbnew.DRETool.ruleWizard" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Rule Wizard..." ) )
        .Tooltip( _( "Create a new design rule step by step" ) ) );

// PCB_CONTROL
//

TOOL_ACTION PCB_ACTIONS::localRatsnestTool( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.localRatsnestTool" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Local Ratsnest" ) )
        .Tooltip( _( "Toggle ratsnest display of selected item(s)" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Icon( BITMAPS::tool_ratsnest )
        .Flags( AF_ACTIVATE ) );

TOOL_ACTION PCB_ACTIONS::hideLocalRatsnest( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.hideDynamicRatsnest" )
        .Scope( AS_GLOBAL ) );

TOOL_ACTION PCB_ACTIONS::updateLocalRatsnest( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.updateLocalRatsnest" )
        .Scope( AS_GLOBAL )
        .Parameter( VECTOR2I() ) );

TOOL_ACTION PCB_ACTIONS::showPythonConsole( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.showPythonConsole" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Scripting Console" ) )
        .Tooltip( _( "Show the Python scripting console" ) )
        .Icon( BITMAPS::py_script )
        .ToolbarState( TOOLBAR_STATE::TOGGLE) );

TOOL_ACTION PCB_ACTIONS::showLayersManager( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.showLayersManager" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Appearance" ) )
        .Tooltip( _( "Show/hide the appearance manager" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Icon( BITMAPS::layers_manager ) );

TOOL_ACTION PCB_ACTIONS::showNetInspector( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.showNetInspector" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Net Inspector" ) )
        .Tooltip( _( "Show/hide the net inspector" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Icon( BITMAPS::tools ) );

TOOL_ACTION PCB_ACTIONS::zonesManager( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.zonesManager" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Zone Manager..." ) )
        .Tooltip( _( "Show the zone manager dialog" ) )
        .Icon( BITMAPS::show_zone ) );

TOOL_ACTION PCB_ACTIONS::flipBoard( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.flipBoard" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Flip Board View" ) )
        .Tooltip( _( "View board from the opposite side" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Icon( BITMAPS::flip_board ) );

TOOL_ACTION PCB_ACTIONS::layoutMode2D( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.layoutMode2D" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( '2' )
        .FriendlyName( _( "2D Layout Mode" ) )
        .Tooltip( _( "Show the board in 2D for editing" ) ) );

TOOL_ACTION PCB_ACTIONS::layoutMode3D( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.layoutMode3D" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( '3' )
        .FriendlyName( _( "3D Layout Mode" ) )
        .Tooltip( _( "Show the board in 3D in this window" ) )
        .Icon( BITMAPS::three_d ) );

TOOL_ACTION PCB_ACTIONS::rehatchShapes( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.rehatchShapes" )
        .Scope( AS_CONTEXT ) );


// Display modes
TOOL_ACTION PCB_ACTIONS::showRatsnest( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.showRatsnest" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Show Ratsnest" ) )
        .Tooltip( _( "Show lines/arcs representing missing connections on the board" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Icon( BITMAPS::general_ratsnest ) );

TOOL_ACTION PCB_ACTIONS::toggleBoardInsight( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.toggleBoardInsight" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_SHIFT + 'H' )
        .FriendlyName( _( "Board Insight" ) )
        .Tooltip( _( "Show cursor position, offsets and layer next to the cursor" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE ) );

TOOL_ACTION PCB_ACTIONS::toggleInsightLens( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.toggleInsightLens" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_SHIFT + 'M' )
        .FriendlyName( _( "Board Insight Lens" ) )
        .Tooltip( _( "Show a magnifier that follows the cursor" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE ) );

TOOL_ACTION PCB_ACTIONS::toggleInsightLensPin( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.toggleInsightLensPin" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Pin Lens to Cursor" ) )
        .Tooltip( _( "Make the Board Insight lens follow the cursor, or leave it where it is" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE ) );

TOOL_ACTION PCB_ACTIONS::findSimilar( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.findSimilar" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_SHIFT + 'F' )
        .FriendlyName( _( "Find Similar Objects..." ) )
        .Tooltip( _( "Select objects whose properties match the selected object" ) )
        .Icon( BITMAPS::find ) );

TOOL_ACTION PCB_ACTIONS::showPcbList( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.showPcbList" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_SHIFT + WXK_F12 )
        .FriendlyName( _( "PCB List" ) )
        .Tooltip( _( "Show or hide the spreadsheet of board objects and their properties" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE ) );

// Altium's "PCB" panel: browse nets, components, pairs, polygons and holes
TOOL_ACTION PCB_ACTIONS::showPcbNavigator( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.showPcbNavigator" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "PCB" ) )
        .Tooltip( _( "Show or hide the PCB panel (browse nets, components, differential pairs, "
                     "polygons and hole sizes)" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE ) );

// Altium menu commands (docs/altium-parity/menus.md)
TOOL_ACTION PCB_ACTIONS::closeDocument( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.closeDocument" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_CTRL + WXK_F4 )
        .FriendlyName( _( "Close" ) )
        .Tooltip( _( "Close the current document" ) ) );

TOOL_ACTION PCB_ACTIONS::fullScreen( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.fullScreen" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_ALT + WXK_F5 )
        .FriendlyName( _( "Full Screen" ) )
        .Tooltip( _( "Use the whole screen for the editor window" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE ) );

TOOL_ACTION PCB_ACTIONS::toggleStatusBar( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.toggleStatusBar" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Status Bar" ) )
        .Tooltip( _( "Show or hide the status bar" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE ) );

TOOL_ACTION PCB_ACTIONS::resetErrorMarkers( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.resetErrorMarkers" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Reset Error Markers" ) )
        .Tooltip( _( "Remove the DRC error and warning markers from the board" ) ) );

TOOL_ACTION PCB_ACTIONS::browseViolations( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.browseViolations" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_SHIFT + 'V' )
        .FriendlyName( _( "Browse Violations" ) )
        .Tooltip( _( "Show the rule violations in the Messages panel" ) ) );

TOOL_ACTION PCB_ACTIONS::browseObjects( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.browseObjects" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_SHIFT + 'X' )
        .FriendlyName( _( "Browse Objects" ) )
        .Tooltip( _( "Browse the board's nets, components and other objects in the PCB panel" ) ) );

TOOL_ACTION PCB_ACTIONS::crossSelectMode( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.crossSelectMode" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_CTRL + MD_SHIFT + 'X' )
        .FriendlyName( _( "Cross Select Mode" ) )
        .Tooltip( _( "Select the same objects in the schematic when they are selected here" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE ) );

TOOL_ACTION PCB_ACTIONS::measureSelected( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.measureSelected" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Measure Selected Objects" ) )
        .Tooltip( _( "Report the total length of the selected tracks and arcs" ) ) );

TOOL_ACTION PCB_ACTIONS::jumpAbsoluteOrigin( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.jumpAbsoluteOrigin" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_CTRL + WXK_HOME )
        .FriendlyName( _( "Absolute Origin" ) )
        .Tooltip( _( "Center the view on the absolute origin" ) ) );

TOOL_ACTION PCB_ACTIONS::jumpCurrentOrigin( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.jumpCurrentOrigin" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_CTRL + WXK_END )
        .FriendlyName( _( "Current Origin" ) )
        .Tooltip( _( "Center the view on the grid origin" ) ) );

TOOL_ACTION PCB_ACTIONS::netClasses( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.netClasses" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Classes..." ) )
        .Tooltip( _( "Edit the net classes" ) ) );

TOOL_ACTION PCB_ACTIONS::zoneRestoreShelved( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.zoneRestoreShelved" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Restore Shelved Polygons" ) )
        .Tooltip( _( "Pour the polygons (zones) that are currently unpoured" ) )
        .Icon( BITMAPS::fill_zone ) );

TOOL_ACTION PCB_ACTIONS::zoneRepourViolating( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.zoneRepourViolating" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Repour Violating Polygons" ) )
        .Tooltip( _( "Repour the polygons (zones) that have design rule violations" ) ) );

TOOL_ACTION PCB_ACTIONS::ratsnestShowAll( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.ratsnestShowAll" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Show All Connections" ) )
        .Tooltip( _( "Show the connection lines (ratsnest) of every net" ) ) );

TOOL_ACTION PCB_ACTIONS::ratsnestHideAll( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.ratsnestHideAll" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Hide All Connections" ) )
        .Tooltip( _( "Hide the connection lines (ratsnest) of every net" ) ) );

TOOL_ACTION PCB_ACTIONS::tileWindows( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.tileWindows" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_SHIFT + WXK_F4 )
        .FriendlyName( _( "Tile" ) )
        .Tooltip( _( "Arrange the open documents in a grid" ) ) );

TOOL_ACTION PCB_ACTIONS::selectBoard( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.selectBoard" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_CTRL + 'B' )
        .FriendlyName( _( "Select Board" ) )
        .Tooltip( _( "Select every object inside the board outline" ) ) );

TOOL_ACTION PCB_ACTIONS::selectConnectedCopper( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.selectConnectedCopper" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_CTRL + 'H' )
        .FriendlyName( _( "Select Connected Copper" ) )
        .Tooltip( _( "Select all copper physically connected to the selected objects" ) ) );

TOOL_ACTION PCB_ACTIONS::jumpNewLocation( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.jumpNewLocation" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Jump to Location" ) )
        .Tooltip( _( "Center the view on a location you type in" ) ) );

TOOL_ACTION PCB_ACTIONS::jumpComponent( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.jumpComponent" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Jump to Component" ) )
        .Tooltip( _( "Center the view on a component chosen by designator" ) ) );

TOOL_ACTION PCB_ACTIONS::jumpNet( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.jumpNet" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Jump to Net" ) )
        .Tooltip( _( "Select a net and zoom to it" ) ) );

TOOL_ACTION PCB_ACTIONS::jumpPad( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.jumpPad" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Jump to Pad" ) )
        .Tooltip( _( "Center the view on a pad given as designator-pad (e.g. U1-3)" ) ) );

TOOL_ACTION PCB_ACTIONS::jumpString( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.jumpString" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Jump to String" ) )
        .Tooltip( _( "Center the view on the next text containing a string" ) ) );

TOOL_ACTION PCB_ACTIONS::alignDialog( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.alignDialog" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Align Objects" ) )
        .Tooltip( _( "Align or distribute the selected objects horizontally and vertically" ) ) );

TOOL_ACTION PCB_ACTIONS::autorouteAll( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.autorouteAll" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Auto Route All" ) )
        .Tooltip( _( "Automatically route every unrouted connection on the board" ) ) );

TOOL_ACTION PCB_ACTIONS::autorouteNet( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.autorouteNet" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Auto Route Net" ) )
        .Tooltip( _( "Automatically route the nets of the selected objects" ) ) );

TOOL_ACTION PCB_ACTIONS::unrouteNet( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.unrouteNet" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Un-Route Net" ) )
        .Tooltip( _( "Remove the tracks and vias of the nets of the selected objects" ) ) );

TOOL_ACTION PCB_ACTIONS::ctxToggleDesignator( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.ctxToggleDesignator" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Hide Designator" ) )
        .Tooltip( _( "Show or hide the designator of the selected components" ) ) );

TOOL_ACTION PCB_ACTIONS::ctxToggleComment( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.ctxToggleComment" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Show Comment" ) )
        .Tooltip( _( "Show or hide the comment (value) of the selected components" ) ) );

TOOL_ACTION PCB_ACTIONS::ctxSelectComponentNets( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.ctxSelectComponentNets" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Select Component Nets" ) )
        .Tooltip( _( "Select every object on the nets of the selected components" ) ) );

TOOL_ACTION PCB_ACTIONS::ctxManage3DBodies( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.ctxManage3DBodies" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Manage 3D Bodies" ) )
        .Tooltip( _( "Edit the 3D models of the component" ) ) );

TOOL_ACTION PCB_ACTIONS::ctxUnrouteRoom( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.ctxUnrouteRoom" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Unroute Room" ) )
        .Tooltip( _( "Remove the unlocked tracks and vias inside the selected rooms" ) ) );

TOOL_ACTION PCB_ACTIONS::ctxDatasheet( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.ctxDatasheet" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Datasheet" ) )
        .Tooltip( _( "Open the datasheet of the selected component" ) ) );

TOOL_ACTION PCB_ACTIONS::ctxManufacturerUrl( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.ctxManufacturerUrl" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Manufacturer URL" ) )
        .Tooltip( _( "Open the manufacturer URL of the selected component" ) ) );

TOOL_ACTION PCB_ACTIONS::ctxPackageSpec( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.ctxPackageSpec" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Package Specification" ) )
        .Tooltip( _( "Open the package specification of the selected component" ) ) );

TOOL_ACTION PCB_ACTIONS::ctxProductUrl( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.ctxProductUrl" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Product URL" ) )
        .Tooltip( _( "Open the product URL of the selected component" ) ) );

TOOL_ACTION PCB_ACTIONS::deselectLasso( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ActiveBar.deselectLasso" )
        .Scope( AS_GLOBAL )
        .Flags( AF_ACTIVATE )
        .FriendlyName( _( "Lasso Deselect" ) )
        .Tooltip( _( "Deselect objects inside a freehand outline (click to start and end)" ) ) );

TOOL_ACTION PCB_ACTIONS::deselectInsideArea( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ActiveBar.deselectInsideArea" )
        .Scope( AS_GLOBAL )
        .Flags( AF_ACTIVATE )
        .FriendlyName( _( "Deselect Inside Area" ) )
        .Tooltip( _( "Deselect objects fully inside a rectangle (click two corners)" ) ) );

TOOL_ACTION PCB_ACTIONS::deselectOutsideArea( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ActiveBar.deselectOutsideArea" )
        .Scope( AS_GLOBAL )
        .Flags( AF_ACTIVATE )
        .FriendlyName( _( "Deselect Outside Area" ) )
        .Tooltip( _( "Deselect objects fully outside a rectangle (click two corners)" ) ) );

TOOL_ACTION PCB_ACTIONS::deselectTouchingRect( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ActiveBar.deselectTouchingRect" )
        .Scope( AS_GLOBAL )
        .Flags( AF_ACTIVATE )
        .FriendlyName( _( "Deselect Touching Rectangle" ) )
        .Tooltip( _( "Deselect objects touched by a rectangle (click two corners)" ) ) );

TOOL_ACTION PCB_ACTIONS::deselectTouchingLine( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ActiveBar.deselectTouchingLine" )
        .Scope( AS_GLOBAL )
        .Flags( AF_ACTIVATE )
        .FriendlyName( _( "Deselect Touching Line" ) )
        .Tooltip( _( "Deselect objects crossed by a line (click two points)" ) ) );

TOOL_ACTION PCB_ACTIONS::deselectAllOnLayer( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ActiveBar.deselectAllOnLayer" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Deselect All on Layer" ) )
        .Tooltip( _( "Deselect every object on the current layer" ) ) );

TOOL_ACTION PCB_ACTIONS::deselectFreeObjects( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ActiveBar.deselectFreeObjects" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Deselect Free Objects" ) )
        .Tooltip( _( "Deselect every object that is not part of a component" ) ) );

TOOL_ACTION PCB_ACTIONS::increaseHorizontalSpacing( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ActiveBar.increaseHorizontalSpacing" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Increase Horizontal Spacing" ) )
        .Tooltip( _( "Move the selected objects one grid step further apart horizontally" ) ) );

TOOL_ACTION PCB_ACTIONS::decreaseHorizontalSpacing( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ActiveBar.decreaseHorizontalSpacing" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Decrease Horizontal Spacing" ) )
        .Tooltip( _( "Move the selected objects one grid step closer together horizontally" ) ) );

TOOL_ACTION PCB_ACTIONS::increaseVerticalSpacing( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ActiveBar.increaseVerticalSpacing" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Increase Vertical Spacing" ) )
        .Tooltip( _( "Move the selected objects one grid step further apart vertically" ) ) );

TOOL_ACTION PCB_ACTIONS::decreaseVerticalSpacing( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ActiveBar.decreaseVerticalSpacing" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Decrease Vertical Spacing" ) )
        .Tooltip( _( "Move the selected objects one grid step closer together vertically" ) ) );

// Selection memory (docs/altium-parity/selection-memory.md).  The parameter encodes
// mode * 10 + slot: mode 0 stores, 1 recalls, 2 adds the slot to the selection.
TOOL_ACTION PCB_ACTIONS::selectionMemoryStore1( TOOL_ACTION_ARGS()
        .Name( "pcbnew.SelectionMemory.store1" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_CTRL + '1' )
        .FriendlyName( _( "Store Selection 1" ) )
        .Tooltip( _( "Remember the current selection in memory 1" ) )
        .Parameter<int>( 1 ) );

TOOL_ACTION PCB_ACTIONS::selectionMemoryRecall1( TOOL_ACTION_ARGS()
        .Name( "pcbnew.SelectionMemory.recall1" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_ALT + '1' )
        .FriendlyName( _( "Recall Selection 1" ) )
        .Tooltip( _( "Replace the selection with memory 1" ) )
        .Parameter<int>( 11 ) );

TOOL_ACTION PCB_ACTIONS::selectionMemoryRecallAdd1( TOOL_ACTION_ARGS()
        .Name( "pcbnew.SelectionMemory.recallAdd1" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_SHIFT + MD_ALT + '1' )
        .FriendlyName( _( "Add Selection 1" ) )
        .Tooltip( _( "Add the objects in memory 1 to the selection" ) )
        .Parameter<int>( 21 ) );

TOOL_ACTION PCB_ACTIONS::selectionMemoryStore2( TOOL_ACTION_ARGS()
        .Name( "pcbnew.SelectionMemory.store2" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_CTRL + '2' )
        .FriendlyName( _( "Store Selection 2" ) )
        .Tooltip( _( "Remember the current selection in memory 2" ) )
        .Parameter<int>( 2 ) );

TOOL_ACTION PCB_ACTIONS::selectionMemoryRecall2( TOOL_ACTION_ARGS()
        .Name( "pcbnew.SelectionMemory.recall2" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_ALT + '2' )
        .FriendlyName( _( "Recall Selection 2" ) )
        .Tooltip( _( "Replace the selection with memory 2" ) )
        .Parameter<int>( 12 ) );

TOOL_ACTION PCB_ACTIONS::selectionMemoryRecallAdd2( TOOL_ACTION_ARGS()
        .Name( "pcbnew.SelectionMemory.recallAdd2" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_SHIFT + MD_ALT + '2' )
        .FriendlyName( _( "Add Selection 2" ) )
        .Tooltip( _( "Add the objects in memory 2 to the selection" ) )
        .Parameter<int>( 22 ) );

TOOL_ACTION PCB_ACTIONS::selectionMemoryStore3( TOOL_ACTION_ARGS()
        .Name( "pcbnew.SelectionMemory.store3" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_CTRL + '3' )
        .FriendlyName( _( "Store Selection 3" ) )
        .Tooltip( _( "Remember the current selection in memory 3" ) )
        .Parameter<int>( 3 ) );

TOOL_ACTION PCB_ACTIONS::selectionMemoryRecall3( TOOL_ACTION_ARGS()
        .Name( "pcbnew.SelectionMemory.recall3" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_ALT + '3' )
        .FriendlyName( _( "Recall Selection 3" ) )
        .Tooltip( _( "Replace the selection with memory 3" ) )
        .Parameter<int>( 13 ) );

TOOL_ACTION PCB_ACTIONS::selectionMemoryRecallAdd3( TOOL_ACTION_ARGS()
        .Name( "pcbnew.SelectionMemory.recallAdd3" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_SHIFT + MD_ALT + '3' )
        .FriendlyName( _( "Add Selection 3" ) )
        .Tooltip( _( "Add the objects in memory 3 to the selection" ) )
        .Parameter<int>( 23 ) );

TOOL_ACTION PCB_ACTIONS::selectionMemoryStore4( TOOL_ACTION_ARGS()
        .Name( "pcbnew.SelectionMemory.store4" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_CTRL + '4' )
        .FriendlyName( _( "Store Selection 4" ) )
        .Tooltip( _( "Remember the current selection in memory 4" ) )
        .Parameter<int>( 4 ) );

TOOL_ACTION PCB_ACTIONS::selectionMemoryRecall4( TOOL_ACTION_ARGS()
        .Name( "pcbnew.SelectionMemory.recall4" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_ALT + '4' )
        .FriendlyName( _( "Recall Selection 4" ) )
        .Tooltip( _( "Replace the selection with memory 4" ) )
        .Parameter<int>( 14 ) );

TOOL_ACTION PCB_ACTIONS::selectionMemoryRecallAdd4( TOOL_ACTION_ARGS()
        .Name( "pcbnew.SelectionMemory.recallAdd4" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_SHIFT + MD_ALT + '4' )
        .FriendlyName( _( "Add Selection 4" ) )
        .Tooltip( _( "Add the objects in memory 4 to the selection" ) )
        .Parameter<int>( 24 ) );

TOOL_ACTION PCB_ACTIONS::selectionMemoryStore5( TOOL_ACTION_ARGS()
        .Name( "pcbnew.SelectionMemory.store5" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_CTRL + '5' )
        .FriendlyName( _( "Store Selection 5" ) )
        .Tooltip( _( "Remember the current selection in memory 5" ) )
        .Parameter<int>( 5 ) );

TOOL_ACTION PCB_ACTIONS::selectionMemoryRecall5( TOOL_ACTION_ARGS()
        .Name( "pcbnew.SelectionMemory.recall5" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_ALT + '5' )
        .FriendlyName( _( "Recall Selection 5" ) )
        .Tooltip( _( "Replace the selection with memory 5" ) )
        .Parameter<int>( 15 ) );

TOOL_ACTION PCB_ACTIONS::selectionMemoryRecallAdd5( TOOL_ACTION_ARGS()
        .Name( "pcbnew.SelectionMemory.recallAdd5" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_SHIFT + MD_ALT + '5' )
        .FriendlyName( _( "Add Selection 5" ) )
        .Tooltip( _( "Add the objects in memory 5 to the selection" ) )
        .Parameter<int>( 25 ) );

TOOL_ACTION PCB_ACTIONS::selectionMemoryStore6( TOOL_ACTION_ARGS()
        .Name( "pcbnew.SelectionMemory.store6" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_CTRL + '6' )
        .FriendlyName( _( "Store Selection 6" ) )
        .Tooltip( _( "Remember the current selection in memory 6" ) )
        .Parameter<int>( 6 ) );

TOOL_ACTION PCB_ACTIONS::selectionMemoryRecall6( TOOL_ACTION_ARGS()
        .Name( "pcbnew.SelectionMemory.recall6" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_ALT + '6' )
        .FriendlyName( _( "Recall Selection 6" ) )
        .Tooltip( _( "Replace the selection with memory 6" ) )
        .Parameter<int>( 16 ) );

TOOL_ACTION PCB_ACTIONS::selectionMemoryRecallAdd6( TOOL_ACTION_ARGS()
        .Name( "pcbnew.SelectionMemory.recallAdd6" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_SHIFT + MD_ALT + '6' )
        .FriendlyName( _( "Add Selection 6" ) )
        .Tooltip( _( "Add the objects in memory 6 to the selection" ) )
        .Parameter<int>( 26 ) );

TOOL_ACTION PCB_ACTIONS::selectionMemoryStore7( TOOL_ACTION_ARGS()
        .Name( "pcbnew.SelectionMemory.store7" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_CTRL + '7' )
        .FriendlyName( _( "Store Selection 7" ) )
        .Tooltip( _( "Remember the current selection in memory 7" ) )
        .Parameter<int>( 7 ) );

TOOL_ACTION PCB_ACTIONS::selectionMemoryRecall7( TOOL_ACTION_ARGS()
        .Name( "pcbnew.SelectionMemory.recall7" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_ALT + '7' )
        .FriendlyName( _( "Recall Selection 7" ) )
        .Tooltip( _( "Replace the selection with memory 7" ) )
        .Parameter<int>( 17 ) );

TOOL_ACTION PCB_ACTIONS::selectionMemoryRecallAdd7( TOOL_ACTION_ARGS()
        .Name( "pcbnew.SelectionMemory.recallAdd7" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_SHIFT + MD_ALT + '7' )
        .FriendlyName( _( "Add Selection 7" ) )
        .Tooltip( _( "Add the objects in memory 7 to the selection" ) )
        .Parameter<int>( 27 ) );

TOOL_ACTION PCB_ACTIONS::selectionMemoryStore8( TOOL_ACTION_ARGS()
        .Name( "pcbnew.SelectionMemory.store8" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_CTRL + '8' )
        .FriendlyName( _( "Store Selection 8" ) )
        .Tooltip( _( "Remember the current selection in memory 8" ) )
        .Parameter<int>( 8 ) );

TOOL_ACTION PCB_ACTIONS::selectionMemoryRecall8( TOOL_ACTION_ARGS()
        .Name( "pcbnew.SelectionMemory.recall8" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_ALT + '8' )
        .FriendlyName( _( "Recall Selection 8" ) )
        .Tooltip( _( "Replace the selection with memory 8" ) )
        .Parameter<int>( 18 ) );

TOOL_ACTION PCB_ACTIONS::selectionMemoryRecallAdd8( TOOL_ACTION_ARGS()
        .Name( "pcbnew.SelectionMemory.recallAdd8" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_SHIFT + MD_ALT + '8' )
        .FriendlyName( _( "Add Selection 8" ) )
        .Tooltip( _( "Add the objects in memory 8 to the selection" ) )
        .Parameter<int>( 28 ) );

TOOL_ACTION PCB_ACTIONS::ratsnestLineMode( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.ratsnestLineMode" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Curved Ratsnest Lines" ) )
        .Tooltip( _( "Show ratsnest with curved lines" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Icon( BITMAPS::curved_ratsnest ) );

TOOL_ACTION PCB_ACTIONS::ratsnestModeCycle( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.ratsnestModeCycle" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Ratsnest Mode (3-state)" ) )
        .Tooltip( _( "Cycle between showing ratsnests for all layers, just visible layers, and none" ) ) );

TOOL_ACTION PCB_ACTIONS::netColorModeCycle( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.netColorMode" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Net Color Mode (3-state)" ) )
        .Tooltip( _( "Cycle between using net and netclass colors for all nets, just ratsnests, and none" ) ) );

TOOL_ACTION PCB_ACTIONS::trackDisplayMode( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.trackDisplayMode" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( 'K' )
        .LegacyHotkeyName( "Track Display Mode" )
        .FriendlyName( _( "Sketch Tracks" ) )
        .Tooltip( _( "Show tracks in outline mode" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Icon( BITMAPS::showtrack ) );

TOOL_ACTION PCB_ACTIONS::padDisplayMode( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.padDisplayMode" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Sketch Pads" ) )
        .Tooltip( _( "Show pads in outline mode" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Icon( BITMAPS::pad_sketch ) );

TOOL_ACTION PCB_ACTIONS::viaDisplayMode( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.viaDisplayMode" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Sketch Vias" ) )
        .Tooltip( _( "Show vias in outline mode" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Icon( BITMAPS::via_sketch ) );

TOOL_ACTION PCB_ACTIONS::graphicsOutlines( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.graphicOutlines" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Sketch Graphic Items" ) )
        .Tooltip( _( "Show graphic items in outline mode" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Icon( BITMAPS::show_mod_edge ) );

TOOL_ACTION PCB_ACTIONS::textOutlines( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.textOutlines" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Sketch Text Items" ) )
        .Tooltip( _( "Show footprint texts in line mode" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Icon( BITMAPS::text_sketch ) );

TOOL_ACTION PCB_ACTIONS::showPadNumbers( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.showPadNumbers" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Show Pad Numbers" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Icon( BITMAPS::pad_number ) );

TOOL_ACTION PCB_ACTIONS::zoneDisplayFilled( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.zoneDisplayEnable" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Draw Zone Fills" ) )
        .Tooltip( _( "Show filled areas of zones" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Icon( BITMAPS::show_zone ) );

TOOL_ACTION PCB_ACTIONS::zoneDisplayOutline( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.zoneDisplayDisable" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Draw Zone Outlines" ) )
        .Tooltip( _( "Show only zone boundaries" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Icon( BITMAPS::show_zone_disable ) );

TOOL_ACTION PCB_ACTIONS::zoneDisplayFractured( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.zoneDisplayOutlines" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Draw Zone Fill Fracture Borders" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Icon( BITMAPS::show_zone_outline_only ) );

TOOL_ACTION PCB_ACTIONS::zoneDisplayTriangulated( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.zoneDisplayTesselation" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Draw Zone Fill Triangulation" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Icon( BITMAPS::show_zone_triangulation ) );

TOOL_ACTION PCB_ACTIONS::zoneDisplayToggle( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.zoneDisplayToggle" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Toggle Zone Display" ) )
        .Tooltip( _( "Cycle between showing zone fills and just their outlines" ) )
        .Icon( BITMAPS::show_zone ) );


TOOL_ACTION PCB_ACTIONS::fpAutoZoom( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.fpAutoZoom" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Automatic zoom" ) )
        .Tooltip( _( "Automatic Zoom on footprint change" ) )
        .Icon( BITMAPS::zoom_auto_fit_in_page )
        .ToolbarState( TOOLBAR_STATE::TOGGLE ) );

// Layer control

// Translate aLayer to the action that switches to it
TOOL_ACTION* PCB_ACTIONS::LayerIDToAction( PCB_LAYER_ID aLayer )
{
    switch( aLayer )
    {
    case F_Cu:      return &PCB_ACTIONS::layerTop;
    case In1_Cu:    return &PCB_ACTIONS::layerInner1;
    case In2_Cu:    return &PCB_ACTIONS::layerInner2;
    case In3_Cu:    return &PCB_ACTIONS::layerInner3;
    case In4_Cu:    return &PCB_ACTIONS::layerInner4;
    case In5_Cu:    return &PCB_ACTIONS::layerInner5;
    case In6_Cu:    return &PCB_ACTIONS::layerInner6;
    case In7_Cu:    return &PCB_ACTIONS::layerInner7;
    case In8_Cu:    return &PCB_ACTIONS::layerInner8;
    case In9_Cu:    return &PCB_ACTIONS::layerInner9;
    case In10_Cu:   return &PCB_ACTIONS::layerInner10;
    case In11_Cu:   return &PCB_ACTIONS::layerInner11;
    case In12_Cu:   return &PCB_ACTIONS::layerInner12;
    case In13_Cu:   return &PCB_ACTIONS::layerInner13;
    case In14_Cu:   return &PCB_ACTIONS::layerInner14;
    case In15_Cu:   return &PCB_ACTIONS::layerInner15;
    case In16_Cu:   return &PCB_ACTIONS::layerInner16;
    case In17_Cu:   return &PCB_ACTIONS::layerInner17;
    case In18_Cu:   return &PCB_ACTIONS::layerInner18;
    case In19_Cu:   return &PCB_ACTIONS::layerInner19;
    case In20_Cu:   return &PCB_ACTIONS::layerInner20;
    case In21_Cu:   return &PCB_ACTIONS::layerInner21;
    case In22_Cu:   return &PCB_ACTIONS::layerInner22;
    case In23_Cu:   return &PCB_ACTIONS::layerInner23;
    case In24_Cu:   return &PCB_ACTIONS::layerInner24;
    case In25_Cu:   return &PCB_ACTIONS::layerInner25;
    case In26_Cu:   return &PCB_ACTIONS::layerInner26;
    case In27_Cu:   return &PCB_ACTIONS::layerInner27;
    case In28_Cu:   return &PCB_ACTIONS::layerInner28;
    case In29_Cu:   return &PCB_ACTIONS::layerInner29;
    case In30_Cu:   return &PCB_ACTIONS::layerInner30;
    case B_Cu:      return &PCB_ACTIONS::layerBottom;
    default:        return nullptr;
    }
}

// Implemented as an accessor + static variable to ensure it is initialized when used
// in static action constructors
TOOL_ACTION_GROUP PCB_ACTIONS::layerDirectSwitchActions()
{
    static TOOL_ACTION_GROUP s_toolActionGroup( "pcbnew.Control.DirectLayerActions" );
    return s_toolActionGroup;
}

TOOL_ACTION PCB_ACTIONS::layerTop( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.layerTop" )
        .Scope( AS_GLOBAL )
        .Group( PCB_ACTIONS::layerDirectSwitchActions() )
        .DefaultHotkey( WXK_PAGEUP )
        .LegacyHotkeyName( "Switch to Component (F.Cu) layer" )
        .FriendlyName( _( "Switch to Component (F.Cu) layer" ) )
        .Flags( AF_NOTIFY )
        .Parameter( F_Cu ) );

TOOL_ACTION PCB_ACTIONS::layerInner1( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.layerInner1" )
        .Scope( AS_GLOBAL )
        .Group( PCB_ACTIONS::layerDirectSwitchActions() )
        .LegacyHotkeyName( "Switch to Inner layer 1" )
        .FriendlyName( _( "Switch to Inner Layer 1" ) )
        .Flags( AF_NOTIFY )
        .Parameter( In1_Cu ) );

TOOL_ACTION PCB_ACTIONS::layerInner2( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.layerInner2" )
        .Scope( AS_GLOBAL )
        .Group( PCB_ACTIONS::layerDirectSwitchActions() )
        .LegacyHotkeyName( "Switch to Inner layer 2" )
        .FriendlyName( _( "Switch to Inner Layer 2" ) )
        .Flags( AF_NOTIFY )
        .Parameter( In2_Cu ) );

TOOL_ACTION PCB_ACTIONS::layerInner3( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.layerInner3" )
        .Scope( AS_GLOBAL )
        .Group( PCB_ACTIONS::layerDirectSwitchActions() )
        .LegacyHotkeyName( "Switch to Inner layer 3" )
        .FriendlyName( _( "Switch to Inner Layer 3" ) )
        .Flags( AF_NOTIFY )
        .Parameter( In3_Cu ) );

TOOL_ACTION PCB_ACTIONS::layerInner4( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.layerInner4" )
        .Scope( AS_GLOBAL )
        .Group( PCB_ACTIONS::layerDirectSwitchActions() )
        .LegacyHotkeyName( "Switch to Inner layer 4" )
        .FriendlyName( _( "Switch to Inner Layer 4" ) )
        .Flags( AF_NOTIFY )
        .Parameter( In4_Cu ) );

TOOL_ACTION PCB_ACTIONS::layerInner5( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.layerInner5" )
        .Scope( AS_GLOBAL )
        .Group( PCB_ACTIONS::layerDirectSwitchActions() )
        .LegacyHotkeyName( "Switch to Inner layer 5" )
        .FriendlyName( _( "Switch to Inner Layer 5" ) )
        .Flags( AF_NOTIFY )
        .Parameter( In5_Cu ) );

TOOL_ACTION PCB_ACTIONS::layerInner6( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.layerInner6" )
        .Scope( AS_GLOBAL )
        .Group( PCB_ACTIONS::layerDirectSwitchActions() )
        .LegacyHotkeyName( "Switch to Inner layer 6" )
        .FriendlyName( _( "Switch to Inner Layer 6" ) )
        .Flags( AF_NOTIFY )
        .Parameter( In6_Cu ) );

TOOL_ACTION PCB_ACTIONS::layerInner7( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.layerInner7" )
        .Scope( AS_GLOBAL )
        .Group( PCB_ACTIONS::layerDirectSwitchActions() )
        .FriendlyName( _( "Switch to Inner Layer 7" ) )
        .Flags( AF_NOTIFY )
        .Parameter( In7_Cu ) );

TOOL_ACTION PCB_ACTIONS::layerInner8( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.layerInner8" )
        .Scope( AS_GLOBAL )
        .Group( PCB_ACTIONS::layerDirectSwitchActions() )
        .FriendlyName( _( "Switch to Inner Layer 8" ) )
        .Flags( AF_NOTIFY )
        .Parameter( In8_Cu ) );

TOOL_ACTION PCB_ACTIONS::layerInner9( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.layerInner9" )
        .Scope( AS_GLOBAL )
        .Group( PCB_ACTIONS::layerDirectSwitchActions() )
        .FriendlyName( _( "Switch to Inner Layer 9" ) )
        .Flags( AF_NOTIFY )
        .Parameter( In9_Cu ) );

TOOL_ACTION PCB_ACTIONS::layerInner10( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.layerInner10" )
        .Scope( AS_GLOBAL )
        .Group( PCB_ACTIONS::layerDirectSwitchActions() )
        .FriendlyName( _( "Switch to Inner Layer 10" ) )
        .Flags( AF_NOTIFY )
        .Parameter( In10_Cu ) );

TOOL_ACTION PCB_ACTIONS::layerInner11( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.layerInner11" )
        .Scope( AS_GLOBAL )
        .Group( PCB_ACTIONS::layerDirectSwitchActions() )
        .FriendlyName( _( "Switch to Inner Layer 11" ) )
        .Flags( AF_NOTIFY )
        .Parameter( In11_Cu ) );

TOOL_ACTION PCB_ACTIONS::layerInner12( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.layerInner12" )
        .Scope( AS_GLOBAL )
        .Group( PCB_ACTIONS::layerDirectSwitchActions() )
        .FriendlyName( _( "Switch to Inner Layer 12" ) )
        .Flags( AF_NOTIFY )
        .Parameter( In12_Cu ) );

TOOL_ACTION PCB_ACTIONS::layerInner13( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.layerInner13" )
        .Scope( AS_GLOBAL )
        .Group( PCB_ACTIONS::layerDirectSwitchActions() )
        .FriendlyName( _( "Switch to Inner Layer 13" ) )
        .Flags( AF_NOTIFY )
        .Parameter( In13_Cu ) );

TOOL_ACTION PCB_ACTIONS::layerInner14( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.layerInner14" )
        .Scope( AS_GLOBAL )
        .Group( PCB_ACTIONS::layerDirectSwitchActions() )
        .FriendlyName( _( "Switch to Inner Layer 14" ) )
        .Flags( AF_NOTIFY )
        .Parameter( In14_Cu ) );

TOOL_ACTION PCB_ACTIONS::layerInner15( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.layerInner15" )
        .Scope( AS_GLOBAL )
        .Group( PCB_ACTIONS::layerDirectSwitchActions() )
        .FriendlyName( _( "Switch to Inner Layer 15" ) )
        .Flags( AF_NOTIFY )
        .Parameter( In15_Cu ) );

TOOL_ACTION PCB_ACTIONS::layerInner16( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.layerInner16" )
        .Scope( AS_GLOBAL )
        .Group( PCB_ACTIONS::layerDirectSwitchActions() )
        .FriendlyName( _( "Switch to Inner Layer 16" ) )
        .Flags( AF_NOTIFY )
        .Parameter( In16_Cu ) );

TOOL_ACTION PCB_ACTIONS::layerInner17( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.layerInner17" )
        .Scope( AS_GLOBAL )
        .Group( PCB_ACTIONS::layerDirectSwitchActions() )
        .FriendlyName( _( "Switch to Inner Layer 17" ) )
        .Flags( AF_NOTIFY )
        .Parameter( In17_Cu ) );

TOOL_ACTION PCB_ACTIONS::layerInner18( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.layerInner18" )
        .Scope( AS_GLOBAL )
        .Group( PCB_ACTIONS::layerDirectSwitchActions() )
        .FriendlyName( _( "Switch to Inner Layer 18" ) )
        .Flags( AF_NOTIFY )
        .Parameter( In18_Cu ) );

TOOL_ACTION PCB_ACTIONS::layerInner19( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.layerInner19" )
        .Scope( AS_GLOBAL )
        .Group( PCB_ACTIONS::layerDirectSwitchActions() )
        .FriendlyName( _( "Switch to Inner Layer 19" ) )
        .Flags( AF_NOTIFY )
        .Parameter( In19_Cu ) );

TOOL_ACTION PCB_ACTIONS::layerInner20( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.layerInner20" )
        .Scope( AS_GLOBAL )
        .Group( PCB_ACTIONS::layerDirectSwitchActions() )
        .FriendlyName( _( "Switch to Inner Layer 20" ) )
        .Flags( AF_NOTIFY )
        .Parameter( In20_Cu ) );

TOOL_ACTION PCB_ACTIONS::layerInner21( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.layerInner21" )
        .Scope( AS_GLOBAL )
        .Group( PCB_ACTIONS::layerDirectSwitchActions() )
        .FriendlyName( _( "Switch to Inner Layer 21" ) )
        .Flags( AF_NOTIFY )
        .Parameter( In21_Cu ) );

TOOL_ACTION PCB_ACTIONS::layerInner22( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.layerInner22" )
        .Scope( AS_GLOBAL )
        .Group( PCB_ACTIONS::layerDirectSwitchActions() )
        .FriendlyName( _( "Switch to Inner Layer 22" ) )
        .Flags( AF_NOTIFY )
        .Parameter( In22_Cu ) );

TOOL_ACTION PCB_ACTIONS::layerInner23( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.layerInner23" )
        .Scope( AS_GLOBAL )
        .Group( PCB_ACTIONS::layerDirectSwitchActions() )
        .FriendlyName( _( "Switch to Inner Layer 23" ) )
        .Flags( AF_NOTIFY )
        .Parameter( In23_Cu ) );

TOOL_ACTION PCB_ACTIONS::layerInner24( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.layerInner24" )
        .Scope( AS_GLOBAL )
        .Group( PCB_ACTIONS::layerDirectSwitchActions() )
        .FriendlyName( _( "Switch to Inner Layer 24" ) )
        .Flags( AF_NOTIFY )
        .Parameter( In24_Cu ) );

TOOL_ACTION PCB_ACTIONS::layerInner25( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.layerInner25" )
        .Scope( AS_GLOBAL )
        .Group( PCB_ACTIONS::layerDirectSwitchActions() )
        .FriendlyName( _( "Switch to Inner Layer 25" ) )
        .Flags( AF_NOTIFY )
        .Parameter( In25_Cu ) );

TOOL_ACTION PCB_ACTIONS::layerInner26( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.layerInner26" )
        .Scope( AS_GLOBAL )
        .Group( PCB_ACTIONS::layerDirectSwitchActions() )
        .FriendlyName( _( "Switch to Inner Layer 26" ) )
        .Flags( AF_NOTIFY )
        .Parameter( In26_Cu ) );

TOOL_ACTION PCB_ACTIONS::layerInner27( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.layerInner27" )
        .Scope( AS_GLOBAL )
        .Group( PCB_ACTIONS::layerDirectSwitchActions() )
        .FriendlyName( _( "Switch to Inner Layer 27" ) )
        .Flags( AF_NOTIFY )
        .Parameter( In27_Cu ) );

TOOL_ACTION PCB_ACTIONS::layerInner28( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.layerInner28" )
        .Scope( AS_GLOBAL )
        .Group( PCB_ACTIONS::layerDirectSwitchActions() )
        .FriendlyName( _( "Switch to Inner Layer 28" ) )
        .Flags( AF_NOTIFY )
        .Parameter( In28_Cu ) );

TOOL_ACTION PCB_ACTIONS::layerInner29( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.layerInner29" )
        .Scope( AS_GLOBAL )
        .Group( PCB_ACTIONS::layerDirectSwitchActions() )
        .FriendlyName( _( "Switch to Inner Layer 29" ) )
        .Flags( AF_NOTIFY )
        .Parameter( In29_Cu ) );

TOOL_ACTION PCB_ACTIONS::layerInner30( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.layerInner30" )
        .Scope( AS_GLOBAL )
        .Group( PCB_ACTIONS::layerDirectSwitchActions() )
        .FriendlyName( _( "Switch to Inner Layer 30" ) )
        .Flags( AF_NOTIFY )
        .Parameter( In30_Cu ) );

TOOL_ACTION PCB_ACTIONS::layerBottom( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.layerBottom" )
        .Scope( AS_GLOBAL )
        .Group( PCB_ACTIONS::layerDirectSwitchActions() )
        .DefaultHotkey( WXK_PAGEDOWN )
        .LegacyHotkeyName( "Switch to Copper (B.Cu) layer" )
        .FriendlyName( _( "Switch to Copper (B.Cu) Layer" ) )
        .Flags( AF_NOTIFY )
        .Parameter( B_Cu ) );

TOOL_ACTION PCB_ACTIONS::layerNext( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.layerNext" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( '+' )
        .LegacyHotkeyName( "Switch to Next Layer" )
        .FriendlyName( _( "Switch to Next Layer" ) )
        .Flags( AF_NOTIFY ) );

TOOL_ACTION PCB_ACTIONS::layerPrev( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.layerPrev" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( '-' )
        .LegacyHotkeyName( "Switch to Previous Layer" )
        .FriendlyName( _( "Switch to Previous Layer" ) )
        .Flags( AF_NOTIFY ) );

TOOL_ACTION PCB_ACTIONS::layerToggle( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.layerToggle" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( 'V' )
        .LegacyHotkeyName( "Add Through Via" )
        .FriendlyName( _( "Toggle Layer" ) )
        .Tooltip( _( "Switch between layers in active layer pair" ) )
        .Flags( AF_NOTIFY ) );

TOOL_ACTION PCB_ACTIONS::layerAlphaInc( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.layerAlphaInc" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( ']' )                 // Altium's Increase Mask Level
        // Don't be tempted to remove "Modern Toolset only".  It's in the legacy property name.
        .LegacyHotkeyName( "Increment Layer Transparency (Modern Toolset only)" )
        .FriendlyName( _( "Increase Layer Opacity" ) )
        .Tooltip( _( "Make the current layer less transparent" ) )
        .Icon( BITMAPS::contrast_mode ) );

TOOL_ACTION PCB_ACTIONS::layerAlphaDec( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.layerAlphaDec" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( '[' )                 // Altium's Decrease Mask Level
        // Don't be tempted to remove "Modern Toolset only".  It's in the legacy property name.
        .LegacyHotkeyName( "Decrement Layer Transparency (Modern Toolset only)" )
        .FriendlyName( _( "Decrease Layer Opacity" ) )
        .Tooltip( _( "Make the current layer more transparent" ) )
        .Icon( BITMAPS::contrast_mode ) );

TOOL_ACTION PCB_ACTIONS::layerPairPresetsCycle( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.layerPairPresetCycle" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_SHIFT + 'V' )
        .FriendlyName( _( "Cycle Layer Pair Presets" ) )
        .Tooltip( _( "Cycle between preset layer pairs" ) ) );

TOOL_ACTION PCB_ACTIONS::layerChanged( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.layerChanged" )
        .Scope( AS_GLOBAL )
        .Flags( AF_NOTIFY ) );

//Show board statistics tool
TOOL_ACTION PCB_ACTIONS::boardStatistics( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InspectionTool.ShowBoardStatistics" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Show Board Statistics" ) )
        .Tooltip( _( "Shows board statistics" ) )
        .Icon( BITMAPS::editor ) );

TOOL_ACTION PCB_ACTIONS::inspectClearance( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InspectionTool.InspectClearance" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Clearance Resolution" ) )
        .Tooltip( _( "Show clearance resolution for the active layer between two selected objects" ) )
        .Icon( BITMAPS::mw_add_gap ) );

TOOL_ACTION PCB_ACTIONS::inspectConstraints( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InspectionTool.InspectConstraints" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Constraints Resolution" ) )
        .Tooltip( _( "Show constraints resolution for the selected object" ) )
        .Icon( BITMAPS::mw_add_stub ) );

TOOL_ACTION PCB_ACTIONS::diffFootprint( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InspectionTool.DiffFootprint" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Compare Footprint with Library" ) )
        .Tooltip( _( "Show differences between board footprint and its library equivalent" ) )
        .Icon( BITMAPS::library ) );

TOOL_ACTION PCB_ACTIONS::showFootprintAssociations( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InspectionTool.ShowFootprintAssociations" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Show Footprint Associations" ) )
        .Tooltip( _( "Show footprint library and schematic symbol associations" ) )
        .Icon( BITMAPS::edit_cmp_symb_links ) );

//Geographic re-annotation tool
TOOL_ACTION PCB_ACTIONS::boardReannotate( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ReannotateTool.ShowReannotateDialog" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Geographical Reannotate..." ) )
        .Tooltip( _( "Reannotate PCB in geographical order" ) )
        .Icon( BITMAPS::annotate ) );

TOOL_ACTION PCB_ACTIONS::repairBoard( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.repairBoard" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Repair Board" ) )
        .Tooltip( _( "Run various diagnostics and attempt to repair board" ) )
        .Icon( BITMAPS::rescue )
        .Parameter( false ) );  // Don't repair quietly

TOOL_ACTION PCB_ACTIONS::repairFootprint( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ModuleEditor.repairFootprint" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Repair Footprint" ) )
        .Tooltip( _( "Run various diagnostics and attempt to repair footprint" ) )
        .Icon( BITMAPS::rescue ) );


// PLACEMENT_TOOL
//
TOOL_ACTION PCB_ACTIONS::alignTop( TOOL_ACTION_ARGS()
        .Name( "pcbnew.AlignAndDistribute.alignTop" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Align to Top" ) )
        .Tooltip( _( "Aligns selected items to the top edge of the item under the cursor" ) )
        .Icon( BITMAPS::align_items_top ) );

TOOL_ACTION PCB_ACTIONS::alignBottom( TOOL_ACTION_ARGS()
        .Name( "pcbnew.AlignAndDistribute.alignBottom" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Align to Bottom" ) )
        .Tooltip( _( "Aligns selected items to the bottom edge of the item under the cursor" ) )
        .Icon( BITMAPS::align_items_bottom ) );

TOOL_ACTION PCB_ACTIONS::alignLeft( TOOL_ACTION_ARGS()
        .Name( "pcbnew.AlignAndDistribute.alignLeft" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Align to Left" ) )
        .Tooltip( _( "Aligns selected items to the left edge of the item under the cursor" ) )
        .Icon( BITMAPS::align_items_left ) );

TOOL_ACTION PCB_ACTIONS::alignRight( TOOL_ACTION_ARGS()
        .Name( "pcbnew.AlignAndDistribute.alignRight" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Align to Right" ) )
        .Tooltip( _( "Aligns selected items to the right edge of the item under the cursor" ) )
        .Icon( BITMAPS::align_items_right ) );

TOOL_ACTION PCB_ACTIONS::alignCenterY( TOOL_ACTION_ARGS()
        .Name( "pcbnew.AlignAndDistribute.alignCenterY" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Align to Vertical Center" ) )
        .Tooltip( _( "Aligns selected items to the vertical center of the item under the cursor" ) )
        .Icon( BITMAPS::align_items_center ) );

TOOL_ACTION PCB_ACTIONS::alignCenterX( TOOL_ACTION_ARGS()
        .Name( "pcbnew.AlignAndDistribute.alignCenterX" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Align to Horizontal Center" ) )
        .Tooltip( _( "Aligns selected items to the horizontal center of the item under the cursor" ) )
        .Icon( BITMAPS::align_items_middle ) );

TOOL_ACTION PCB_ACTIONS::distributeHorizontallyCenters( TOOL_ACTION_ARGS()
        .Name( "pcbnew.AlignAndDistribute.distributeHorizontallyCenters" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Distribute Horizontally by Centers" ) )
        .Tooltip( _( "Distributes selected items between the left-most item and the right-most item "
                     "so that the item centers are equally distributed" ) )
        .Icon( BITMAPS::distribute_horizontal_centers ) );

TOOL_ACTION PCB_ACTIONS::distributeHorizontallyGaps( TOOL_ACTION_ARGS()
        .Name( "pcbnew.AlignAndDistribute.distributeHorizontallyGaps" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Distribute Horizontally with Even Gaps" ) )
        .Tooltip( _( "Distributes selected items between the left-most item and the right-most item "
                     "so that the gaps between items are equal" ) )
        .Icon( BITMAPS::distribute_horizontal_gaps ) );

TOOL_ACTION PCB_ACTIONS::distributeVerticallyGaps( TOOL_ACTION_ARGS()
        .Name( "pcbnew.AlignAndDistribute.distributeVerticallyGaps" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Distribute Vertically with Even Gaps" ) )
        .Tooltip( _( "Distributes selected items between the top-most item and the bottom-most item "
                     "so that the gaps between items are equal" ) )
        .Icon( BITMAPS::distribute_vertical_gaps ) );

TOOL_ACTION PCB_ACTIONS::distributeVerticallyCenters( TOOL_ACTION_ARGS()
        .Name( "pcbnew.AlignAndDistribute.distributeVerticallyCenters" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_CTRL + MD_SHIFT + 'V' )
        .FriendlyName( _( "Distribute Vertically by Centers" ) )
        .Tooltip( _( "Distributes selected items between the top-most item and the bottom-most item "
                     "so that the item centers are equally distributed" ) )
        .Icon( BITMAPS::distribute_vertical_centers ) );

// PCB_POINT_EDITOR
//
TOOL_ACTION PCB_ACTIONS::pointEditorAddCorner( TOOL_ACTION_ARGS()
        .Name( "pcbnew.PointEditor.addCorner" )
        .Scope( AS_GLOBAL )
#ifdef __WXMAC__
        .DefaultHotkey( WXK_F1 )
#else
        .DefaultHotkey( WXK_INSERT )
#endif
        .FriendlyName( _( "Create Corner" ) )
        .Tooltip( _( "Create a corner" ) )
        .Icon( BITMAPS::add_corner ) );

TOOL_ACTION PCB_ACTIONS::pointEditorRemoveCorner( TOOL_ACTION_ARGS()
        .Name( "pcbnew.PointEditor.removeCorner" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Remove Corner" ) )
        .Tooltip( _( "Remove corner" ) )
        .Icon( BITMAPS::delete_cursor ) );

TOOL_ACTION PCB_ACTIONS::pointEditorChamferCorner( TOOL_ACTION_ARGS()
        .Name( "pcbnew.PointEditor.chamferCorner" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Chamfer Corner" ) )
        .Tooltip( _( "Chamfer corner" ) )
        .Icon( BITMAPS::chamfer ) );

// POSITION_RELATIVE_TOOL
//
TOOL_ACTION PCB_ACTIONS::positionRelative( TOOL_ACTION_ARGS()
        .Name( "pcbnew.PositionRelative.positionRelative" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_SHIFT + 'P' )
        .LegacyHotkeyName( "Position Item Relative" )
        .FriendlyName( _( "Position Relative To..." ) )
        .Tooltip( _( "Positions the selected item(s) by an exact amount relative to another" ) )
        .Icon( BITMAPS::move_relative ) );

TOOL_ACTION PCB_ACTIONS::interactiveOffsetTool( TOOL_ACTION_ARGS()
        .Name( "pcbnew.PositionRelative.interactiveOffsetTool" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Interactive Offset Tool" ) )
        .Tooltip( _( "Interactive tool for offsetting items by exact amounts" ) )
        .Icon( BITMAPS::move_relative ) );

// PCIKER_TOOL
//
TOOL_ACTION PCB_ACTIONS::selectItemInteractively( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Picker.selectItemInteractively" )
        .Scope( AS_GLOBAL )
        .Parameter<PCB_PICKER_TOOL::INTERACTIVE_PARAMS>( {} ) );

TOOL_ACTION PCB_ACTIONS::selectPointInteractively( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Picker.selectPointInteractively" )
        .Scope( AS_GLOBAL )
        .Parameter<PCB_PICKER_TOOL::INTERACTIVE_PARAMS>( {} ));


TOOL_ACTION PCB_ACTIONS::selectConnection( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveSelection.SelectConnection" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( 'U' )
        .LegacyHotkeyName( "Select Single Track" )
        .FriendlyName( _( "Select/Expand Connection" ) )
        .Tooltip( _( "Selects a connection or expands an existing selection to junctions, pads, or entire connections" ) )
        .Icon( BITMAPS::add_tracks ) );

TOOL_ACTION PCB_ACTIONS::unrouteSelected( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveSelection.unrouteSelected" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Unroute Selected" ) )
        .Tooltip( _( "Unroutes selected items to the nearest pad." ) )
        .Icon( BITMAPS::general_deletions ) );

TOOL_ACTION PCB_ACTIONS::unrouteSegment( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveSelection.unrouteSegment" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( WXK_BACK )
        .FriendlyName( _( "Unroute Segment" ) )
        .Tooltip( _( "Unroutes segment to the nearest segment." ) )
        .Icon( BITMAPS::general_deletions ) );

TOOL_ACTION PCB_ACTIONS::syncSelection( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveSelection.SyncSelection" )
        .Scope( AS_GLOBAL ) );

TOOL_ACTION PCB_ACTIONS::syncSelectionWithNets( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveSelection.SyncSelectionWithNets" )
        .Scope( AS_GLOBAL ) );

TOOL_ACTION PCB_ACTIONS::selectNet( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveSelection.SelectNet" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Select All Tracks in Net" ) )
        .Tooltip( _( "Selects all tracks & vias belonging to the same net." ) )
        .Parameter<int>( 0 ) );

TOOL_ACTION PCB_ACTIONS::deselectNet( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveSelection.DeselectNet" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Deselect All Tracks in Net" ) )
        .Tooltip( _( "Deselects all tracks & vias belonging to the same net." ) )
        .Parameter<int>( 0 ) );

TOOL_ACTION PCB_ACTIONS::selectUnconnected( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveSelection.SelectUnconnected" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( 'O' )
        .FriendlyName( _( "Select All Unconnected Footprints" ) )
        .Tooltip( _( "Selects all unconnected footprints belonging to each selected net." ) ) );

TOOL_ACTION PCB_ACTIONS::grabUnconnected( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveSelection.GrabUnconnected" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_SHIFT + 'O' )
        .FriendlyName( _( "Grab Nearest Unconnected Footprints" ) )
        .Tooltip( _( "Selects and initiates moving the nearest unconnected footprint on each selected net." ) ) );

TOOL_ACTION PCB_ACTIONS::selectOnSheetFromEeschema( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveSelection.SelectOnSheet" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Sheet" ) )
        .Tooltip( _( "Selects all footprints and tracks in the schematic sheet" ) )
        .Icon( BITMAPS::select_same_sheet ) );

TOOL_ACTION PCB_ACTIONS::selectSameSheet( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveSelection.SelectSameSheet" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Items in Same Hierarchical Sheet" ) )
        .Tooltip( _( "Selects all footprints and tracks in the same schematic sheet" ) )
        .Icon( BITMAPS::select_same_sheet ) );

TOOL_ACTION PCB_ACTIONS::selectOnSchematic( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveSelection.SelectOnSchematic" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Select on Schematic" ) )
        .Tooltip( _( "Selects corresponding items in Schematic editor" ) )
        .Icon( BITMAPS::select_same_sheet ) );

TOOL_ACTION PCB_ACTIONS::filterSelection( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveSelection.FilterSelection" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Filter Selected Items..." ) )
        .Tooltip( _( "Remove items from the selection by type" ) )
        .Icon( BITMAPS::filter ) );


// ZONE_FILLER_TOOL
//
TOOL_ACTION PCB_ACTIONS::zoneFill( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ZoneFiller.zoneFill" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Draft Fill Selected Zone(s)" ) )
        .Tooltip( _( "Update copper fill of selected zone(s) without regard to other interacting zones" ) )
        .Icon( BITMAPS::fill_zone )
        .Parameter<ZONE*>( nullptr ) );

TOOL_ACTION PCB_ACTIONS::zoneFillAll( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ZoneFiller.zoneFillAll" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( 'B' )
        .LegacyHotkeyName( "Fill or Refill All Zones" )
        .FriendlyName( _( "Fill All Zones" ) )
        .Tooltip( _( "Update copper fill of all zones" ) )
        .Icon( BITMAPS::fill_zone ) );

TOOL_ACTION PCB_ACTIONS::zoneFillDirty( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ZoneFiller.zoneFillDirty" )
        .Scope( AS_CONTEXT ) );

TOOL_ACTION PCB_ACTIONS::zoneUnfill( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ZoneFiller.zoneUnfill" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Unfill Selected Zone(s)" ) )
        .Tooltip( _( "Remove copper fill from selected zone(s)" ) )
        .Icon( BITMAPS::zone_unfill ) );

TOOL_ACTION PCB_ACTIONS::zoneUnfillAll( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ZoneFiller.zoneUnfillAll" )
        .Scope( AS_GLOBAL )
        .LegacyHotkeyName( "Remove Filled Areas in All Zones" )
        .FriendlyName( _( "Unfill All Zones" ) )
        .Tooltip( _( "Remove copper fill from all zones" ) )
        .Icon( BITMAPS::zone_unfill ) );


// AUTOPLACER_TOOL
//
TOOL_ACTION PCB_ACTIONS::autoplaceSelectedComponents( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Autoplacer.autoplaceSelected" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Place Selected Footprints" ) )
        .Tooltip( _( "Performs automatic placement of selected components" ) ) );

TOOL_ACTION PCB_ACTIONS::autoplaceOffboardComponents( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Autoplacer.autoplaceOffboard" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Place Off-Board Footprints" ) )
        .Tooltip( _( "Performs automatic placement of components outside board area" ) ) );

TOOL_ACTION PCB_ACTIONS::generatePlacementRuleAreas( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Multichannel.generatePlacementRuleAreas" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Generate Placement Rule Areas..." ) )
        .Tooltip( _( "Creates best-fit placement rule areas" ) )
        .Icon( BITMAPS::add_keepout_area )
        .Flags( AF_ACTIVATE ) );

TOOL_ACTION PCB_ACTIONS::repeatLayout( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Multichannel.repeatLayout" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Repeat Layout..." ) )
        .Tooltip( _( "Clones placement & routing across multiple identical channels" ) )
        .Icon( BITMAPS::copy )
        );

// ROUTER_TOOL
//
TOOL_ACTION PCB_ACTIONS::routeSingleTrack( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveRouter.SingleTrack" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( 'X' )
        .LegacyHotkeyName( "Add New Track" )
        .FriendlyName( _( "Route Single Track" ) )
        .Tooltip( _( "Route tracks" ) )
        .Icon( BITMAPS::add_tracks )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Flags( AF_ACTIVATE )
        .Parameter( PNS::PNS_MODE_ROUTE_SINGLE ) );

TOOL_ACTION PCB_ACTIONS::routeDiffPair( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveRouter.DiffPair" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( '6' )
        // Don't be tempted to remove "Modern Toolset only".  It's in the legacy property name.
        .LegacyHotkeyName( "Route Differential Pair (Modern Toolset only)" )
        .FriendlyName( _( "Route Differential Pair" ) )
        .Tooltip( _( "Route differential pairs" ) )
        .Icon( BITMAPS::ps_diff_pair )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Flags( AF_ACTIVATE )
        .Parameter( PNS::PNS_MODE_ROUTE_DIFF_PAIR ) );

TOOL_ACTION PCB_ACTIONS::routerSettingsDialog( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveRouter.SettingsDialog" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_CTRL + '<' )
        .LegacyHotkeyName( "Routing Options" )
        .FriendlyName( _( "Interactive Router Settings..." ) )
        .Tooltip( _( "Open Interactive Router settings" ) )
        .Icon( BITMAPS::tools ) );

TOOL_ACTION PCB_ACTIONS::routerDiffPairDialog( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveRouter.DiffPairDialog" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Differential Pair Dimensions..." ) )
        .Tooltip( _( "Open Differential Pair Dimension settings" ) )
        .Icon( BITMAPS::ps_diff_pair_gap ) );

TOOL_ACTION PCB_ACTIONS::routerHighlightMode( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveRouter.HighlightMode" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Router Highlight Mode" ) )
        .Tooltip( _( "Switch router to highlight mode" ) )
        .Flags( AF_NONE )
        .Parameter( PNS::RM_MarkObstacles ) );

TOOL_ACTION PCB_ACTIONS::routerShoveMode( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveRouter.ShoveMode" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Router Shove Mode" ) )
        .Tooltip( _( "Switch router to shove mode" ) )
        .Flags( AF_NONE )
        .Parameter( PNS::RM_Shove ) );

TOOL_ACTION PCB_ACTIONS::routerWalkaroundMode( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveRouter.WalkaroundMode" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Router Walkaround Mode" ) )
        .Tooltip( _( "Switch router to walkaround mode" ) )
        .Flags( AF_NONE )
        .Parameter( PNS::RM_Walkaround ) );

TOOL_ACTION PCB_ACTIONS::cycleRouterMode( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveRouter.CycleRouterMode" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Cycle Router Mode" ) )
        .Tooltip( _( "Cycle router to the next mode" ) ) );

TOOL_ACTION PCB_ACTIONS::selectLayerPair( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveRouter.SelectLayerPair" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Set Layer Pair..." ) )
        .Tooltip( _( "Change active layer pair for routing" ) )
        .Icon( BITMAPS::select_layer_pair )
        .Flags( AF_ACTIVATE ) );

TOOL_ACTION PCB_ACTIONS::tuneSingleTrack( TOOL_ACTION_ARGS()
        .Name( "pcbnew.LengthTuner.TuneSingleTrack" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( '7' )
        // Don't be tempted to remove "Modern Toolset only".  It's in the legacy property name.
        .LegacyHotkeyName( "Tune Single Track (Modern Toolset only)" )
        .FriendlyName( _( "Tune Length of a Single Track" ) )
        .Icon( BITMAPS::ps_tune_length )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Flags( AF_ACTIVATE )
        .Parameter( PNS::PNS_MODE_TUNE_SINGLE ) );

TOOL_ACTION PCB_ACTIONS::tuneDiffPair( TOOL_ACTION_ARGS()
        .Name( "pcbnew.LengthTuner.TuneDiffPair" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( '8' )
        // Don't be tempted to remove "Modern Toolset only".  It's in the legacy property name.
        .LegacyHotkeyName( "Tune Differential Pair Length (Modern Toolset only)" )
        .FriendlyName( _( "Tune Length of a Differential Pair" ) )
        .Icon( BITMAPS::ps_diff_pair_tune_length )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Flags( AF_ACTIVATE )
        .Parameter( PNS::PNS_MODE_TUNE_DIFF_PAIR ) );

TOOL_ACTION PCB_ACTIONS::tuneSkew( TOOL_ACTION_ARGS()
        .Name( "pcbnew.LengthTuner.TuneDiffPairSkew" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( '9' )
        // Don't be tempted to remove "Modern Toolset only".  It's in the legacy property name.
        .LegacyHotkeyName( "Tune Differential Pair Skew (Modern Toolset only)" )
        .FriendlyName( _( "Tune Skew of a Differential Pair" ) )
        .Icon( BITMAPS::ps_diff_pair_tune_phase )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Flags( AF_ACTIVATE )
        .Parameter( PNS::PNS_MODE_TUNE_DIFF_PAIR_SKEW ) );

TOOL_ACTION PCB_ACTIONS::routerInlineDrag( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveRouter.InlineDrag" )
        .Scope( AS_CONTEXT )
        .Parameter<int>( PNS::DM_ANY ) );

TOOL_ACTION PCB_ACTIONS::routerUndoLastSegment( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveRouter.UndoLastSegment" )
        .Scope( AS_CONTEXT )
        .DefaultHotkey( WXK_BACK )
        .FriendlyName( _( "Undo Last Segment" ) )
        .Tooltip( _( "Walks the current track back one segment." ) ) );

TOOL_ACTION PCB_ACTIONS::routerContinueFromEnd( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveRouter.ContinueFromEnd" )
        .Scope( AS_CONTEXT )
        .DefaultHotkey( MD_CTRL + 'E' )
        .FriendlyName( _( "Route From Other End" ) )
        .Tooltip( _( "Commits current segments and starts next segment from nearest ratsnest end." ) ) );

TOOL_ACTION PCB_ACTIONS::routerAttemptFinish( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveRouter.AttemptFinish" )
        .Scope( AS_CONTEXT )
        .DefaultHotkey( 'F' )
        .FriendlyName( _( "Attempt Finish" ) )
        .Tooltip( _( "Attempts to complete current route to nearest ratsnest end." ) )
        .Parameter<bool*>( nullptr ) );

TOOL_ACTION PCB_ACTIONS::routerRouteSelected( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveRouter.RouteSelected" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Route Selected" ) )
        .Tooltip( _( "Sequentially route selected items from ratsnest anchor." ) )
        .Flags( AF_ACTIVATE )
        .Parameter( PNS::PNS_MODE_ROUTE_SINGLE ) );

TOOL_ACTION PCB_ACTIONS::routerRouteSelectedFromEnd( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveRouter.RouteSelectedFromEnd" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_SHIFT + 'E' )
        .FriendlyName( _( "Route Selected From Other End" ) )
        .Tooltip( _( "Sequentially route selected items from other end of ratsnest anchor." ) )
        .Flags( AF_ACTIVATE )
        .Parameter( PNS::PNS_MODE_ROUTE_SINGLE ) );

TOOL_ACTION PCB_ACTIONS::routerAutorouteSelected( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveRouter.Autoroute" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_SHIFT + 'A' )      // Altium's ActiveRoute
        .FriendlyName( _( "Attempt Finish Selected (Autoroute)" ) )
        .Tooltip( _( "Sequentially attempt to automatically route all selected pads." ) )
        .Flags( AF_ACTIVATE )
        .Parameter( PNS::PNS_MODE_ROUTE_SINGLE ) );

TOOL_ACTION PCB_ACTIONS::cancelCurrentItem( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveRouter.CancelCurrentItem" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Cancel Current Item" ) )
        .Tooltip( _( "Skip current item and route next selected item." ) ) );

TOOL_ACTION PCB_ACTIONS::breakTrack( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveRouter.BreakTrack" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Break Track" ) )
        .Tooltip( _( "Splits the track segment into two segments connected at the cursor position." ) )
        .Icon( BITMAPS::break_line ) );

TOOL_ACTION PCB_ACTIONS::drag45Degree( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveRouter.Drag45Degree" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( 'D' )
        .LegacyHotkeyName( "Drag Track Keep Slope" )
        .FriendlyName( _( "Drag 45 Degree Mode" ) )
        .Tooltip( _( "Drags the track segment while keeping connected tracks at 45 degrees." ) )
        .Icon( BITMAPS::drag_segment_withslope ) );

TOOL_ACTION PCB_ACTIONS::dragFreeAngle( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveRouter.DragFreeAngle" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( 'G' )
        .LegacyHotkeyName( "Drag Item" )
        .FriendlyName( _( "Drag Free Angle" ) )
        .Tooltip( _( "Drags the nearest joint in the track without restricting the track angle." ) )
        .Icon( BITMAPS::drag_segment ) );


// GENERATOR_TOOL
//

TOOL_ACTION PCB_ACTIONS::regenerateAllTuning( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Generator.regenerateAllTuning" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Update All Tuning Patterns" ) )
        .Tooltip( _( "Attempt to re-tune existing tuning patterns within their bounds" ) )
        .Icon( BITMAPS::router_len_tuner )
        .Parameter( wxString( wxS( "tuning_pattern" ) ) ) );

TOOL_ACTION PCB_ACTIONS::regenerateAll( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Generator.regenerateAll" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Rebuild All Generators" ) )
        .Tooltip( _( "Rebuilds geometry of all generators" ) )
        .Icon( BITMAPS::refresh )
        .Parameter( wxString( wxS( "*" ) ) ) );

TOOL_ACTION PCB_ACTIONS::regenerateSelected( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Generator.regenerateSelected" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Rebuild Selected Generators" ) )
        .Tooltip( _( "Rebuilds geometry of selected generator(s)" ) )
        .Icon( BITMAPS::refresh ) );


TOOL_ACTION PCB_ACTIONS::genStartEdit( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Generator.genStartEdit" )
        .Scope( AS_CONTEXT ) );

TOOL_ACTION PCB_ACTIONS::genUpdateEdit( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Generator.genUpdateEdit" )
        .Scope( AS_CONTEXT ) );

TOOL_ACTION PCB_ACTIONS::genFinishEdit( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Generator.genFinishEdit" )
        .Scope( AS_CONTEXT ) );

TOOL_ACTION PCB_ACTIONS::genCancelEdit( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Generator.genCacnelEdit" )
        .Scope( AS_CONTEXT ) );

TOOL_ACTION PCB_ACTIONS::genRemove( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Generator.genRemove" )
        .Scope( AS_CONTEXT ) );


TOOL_ACTION PCB_ACTIONS::generatorsShowManager( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Generator.showManager" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Generators Manager" ) )
        .Tooltip( _( "Show a manager dialog for Generator objects" ) )
        .Icon( BITMAPS::pin_table ) );


// LENGTH_TUNER_TOOL
//
TOOL_ACTION PCB_ACTIONS::lengthTunerSettings( TOOL_ACTION_ARGS()
        .Name( "pcbnew.LengthTuner.Settings" )
        .ToolbarState( TOOLBAR_STATE::HIDDEN )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_CTRL + 'L' )
        // Don't be tempted to remove "Modern Toolset only".  It's in the legacy property name.
        .LegacyHotkeyName( "Length Tuning Settings (Modern Toolset only)" )
        .FriendlyName( _( "Length Tuning Settings" ) )
        .MenuText( _( "Length Tuning Settings..." ) )
        .Tooltip( _( "Displays tuning pattern properties dialog" ) )
        .Icon( BITMAPS::router_len_tuner_setup ) );

// Drag and drop
TOOL_ACTION PCB_ACTIONS::ddAppendBoard( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.DdAppendBoard" )
        .Scope( AS_GLOBAL ) );

TOOL_ACTION PCB_ACTIONS::ddImportFootprint( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Control.ddImportFootprint" )
        .Scope( AS_GLOBAL ) );

TOOL_ACTION PCB_ACTIONS::ddImportGraphics( TOOL_ACTION_ARGS()
        .Name( "pcbnew.InteractiveDrawing.ddImportGraphics" )
        .Scope( AS_GLOBAL ) );

// actions for footprint wizard frame
TOOL_ACTION PCB_ACTIONS::showWizards( TOOL_ACTION_ARGS()
        .Name( "pcbnew.FpWizard.showWizards" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Show wizards selector" ) )
        .Tooltip( _( "Select wizard script to run" ) )
        .Icon( BITMAPS::module_wizard ) );

TOOL_ACTION PCB_ACTIONS::resetWizardPrms( TOOL_ACTION_ARGS()
        .Name( "pcbnew.FpWizard.resetWizardPrms" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Reset wizard parameters" ) )
        .Tooltip( _( "Reset wizard parameters to default" ) )
        .Icon( BITMAPS::reload ) );

TOOL_ACTION PCB_ACTIONS::selectPreviousWizardPage( TOOL_ACTION_ARGS()
        .Name( "pcbnew.FpWizard.selectPreviousWizardPage" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Select previous wizard page" ) )
        .Tooltip( _( "Select previous parameters page" ) )
        .Icon( BITMAPS::lib_previous ) );

TOOL_ACTION PCB_ACTIONS::selectNextWizardPage( TOOL_ACTION_ARGS()
        .Name( "pcbnew.FpWizard.selectNextWizardPage" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Select next wizard page" ) )
        .Tooltip( _( "Select next parameters page" ) )
        .Icon( BITMAPS::lib_next ) );

TOOL_ACTION PCB_ACTIONS::exportFpToEditor( TOOL_ACTION_ARGS()
        .Name( "pcbnew.FpWizard.exportFpToEditor" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Export footprint to editor" ) )
        .Tooltip( _( "Export footprint to editor" ) )
        .Icon( BITMAPS::export_footprint_names ) );


const TOOL_EVENT& PCB_EVENTS::SnappingModeChangedByKeyEvent()
{
    static TOOL_EVENT event = TOOL_EVENT( TC_MESSAGE, TA_ACTION,
                                          "common.Interactive.snappingModeChangedByKey" );

    return event;
}


const TOOL_EVENT& PCB_EVENTS::LayerPairPresetChangedByKeyEvent()
{
    static TOOL_EVENT event = TOOL_EVENT( TC_MESSAGE, TA_ACTION,
                                          "pcbnew.Control.layerPairPresetChangedByKey" );

    return event;
}


// Active Bar

TOOL_ACTION PCB_ACTIONS::selectionFilterMenu( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ActiveBar.selectionFilterMenu" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Selection Filter" ) )
        .Tooltip( _( "Choose which kinds of object can be selected" ) )
        .Icon( BITMAPS::filter ) );

TOOL_ACTION PCB_ACTIONS::selectInsideArea( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ActiveBar.selectInsideArea" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Inside Area" ) )
        .Tooltip( _( "Select objects fully inside a rectangle (click two corners)" ) )
        .Flags( AF_ACTIVATE )
        .Icon( BITMAPS::select_inside_area ) );

TOOL_ACTION PCB_ACTIONS::selectOutsideArea( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ActiveBar.selectOutsideArea" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Outside Area" ) )
        .Tooltip( _( "Select objects fully outside a rectangle (click two corners)" ) )
        .Flags( AF_ACTIVATE )
        .Icon( BITMAPS::select_outside_area ) );

TOOL_ACTION PCB_ACTIONS::selectTouchingRect( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ActiveBar.selectTouchingRect" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Touching Rectangle" ) )
        .Tooltip( _( "Select objects touched by a rectangle (click two corners)" ) )
        .Flags( AF_ACTIVATE )
        .Icon( BITMAPS::select_touching_rect ) );

TOOL_ACTION PCB_ACTIONS::selectTouchingLine( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ActiveBar.selectTouchingLine" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Touching Line" ) )
        .Tooltip( _( "Select objects crossed by a line (click two points)" ) )
        .Flags( AF_ACTIVATE )
        .Icon( BITMAPS::select_touching_line ) );

TOOL_ACTION PCB_ACTIONS::selectAllOnLayer( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ActiveBar.selectAllOnLayer" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "All on Layer" ) )
        .Tooltip( _( "Select every object on the current layer" ) )
        .Icon( BITMAPS::select_all_on_layer ) );

TOOL_ACTION PCB_ACTIONS::selectFreeObjects( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ActiveBar.selectFreeObjects" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Free Objects" ) )
        .Tooltip( _( "Select every object that is not part of a component" ) )
        .Icon( BITMAPS::select_free_objects ) );

TOOL_ACTION PCB_ACTIONS::selectAllLocked( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ActiveBar.selectAllLocked" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "All Locked" ) )
        .Tooltip( _( "Select every locked object" ) )
        .Icon( BITMAPS::select_all_locked ) );

TOOL_ACTION PCB_ACTIONS::selectOffGridPads( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ActiveBar.selectOffGridPads" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Off Grid Pads" ) )
        .Tooltip( _( "Select pads that are not on the current grid" ) )
        .Icon( BITMAPS::select_offgrid_pads ) );

TOOL_ACTION PCB_ACTIONS::toggleSelection( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ActiveBar.toggleSelection" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Toggle Selection" ) )
        .Tooltip( _( "Click objects to add them to or remove them from the selection" ) )
        .Flags( AF_ACTIVATE )
        .Icon( BITMAPS::select_toggle ) );

TOOL_ACTION PCB_ACTIONS::alignToGrid( TOOL_ACTION_ARGS()
        .Name( "pcbnew.ActiveBar.alignToGrid" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_CTRL + MD_SHIFT + 'D' )
        .FriendlyName( _( "Align to Grid" ) )
        .Tooltip( _( "Move the selected objects onto the grid" ) )
        .Icon( BITMAPS::align_elements_to_grid ) );

TOOL_ACTION PCB_ACTIONS::layerStackManager( TOOL_ACTION_ARGS()
        .Name( "pcbnew.EditorControl.layerStackManager" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Layer Stack Manager..." ) )
        .Tooltip( _( "Edit the board's layers, materials and thicknesses" ) )
        .Icon( BITMAPS::layers_manager ) );
