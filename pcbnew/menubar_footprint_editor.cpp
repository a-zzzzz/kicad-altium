/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2015 Jean-Pierre Charras, jp.charras at wanadoo.fr
 * Copyright (C) 2015 SoftPLC Corporation, Dick Hollenbeck <dick@softplc.com>
 * Copyright (C) 2015 Wayne Stambaugh <stambaughw@gmail.com>
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

#include "footprint_edit_frame.h"
#include "pcbnew_id.h"
#include <bitmaps.h>
#include <tool/actions.h>
#include <tool/action_menu.h>
#include <tool/tool_manager.h>
#include <tools/pcb_actions.h>
#include <tools/pcb_selection_tool.h>
#include <widgets/window_menu.h>
#include <widgets/wx_menubar.h>


void FOOTPRINT_EDIT_FRAME::doReCreateMenuBar()
{
    PCB_SELECTION_TOOL* selTool = m_toolManager->GetTool<PCB_SELECTION_TOOL>();
    // wxWidgets handles the Mac Application menu behind the scenes, but that means
    // we always have to start from scratch with a new wxMenuBar.
    wxMenuBar*  oldMenuBar = GetMenuBar();
    WX_MENUBAR* menuBar    = new WX_MENUBAR();

    // Menus follow Altium Designer's PCB Library editor: File, Edit, View, Project, Place,
    // Tools, Reports, Window, Help.  Accelerator letters are Altium's (they are typed as menu
    // chords) and unique within each menu.  KiCad-only commands come last, after a separator.

    //-- File menu ----------------------------------------------------------
    //
    ACTION_MENU* fileMenu = new ACTION_MENU( false, selTool );

    ACTION_MENU* newMenu = new ACTION_MENU( false, selTool );
    newMenu->SetTitle( _( "&New" ) );
    newMenu->SetIcon( BITMAPS::new_library );
    newMenu->Add( ACTIONS::newLibrary,       ACTION_MENU::NORMAL, _( "&Library..." ) );

    // KiCad-only
    newMenu->AppendSeparator();
    newMenu->Add( PCB_ACTIONS::newFootprint, ACTION_MENU::NORMAL, _( "&Footprint" ) );
    fileMenu->Add( newMenu );

    fileMenu->Add( ACTIONS::addLibrary,        ACTION_MENU::NORMAL, _( "&Open..." ) );
    fileMenu->Add( PCB_ACTIONS::closeDocument, ACTION_MENU::NORMAL, _( "&Close" ) );

    fileMenu->AppendSeparator();
    fileMenu->Add( ACTIONS::save,   ACTION_MENU::NORMAL, _( "&Save" ) );
    fileMenu->Add( ACTIONS::saveAs, ACTION_MENU::NORMAL, _( "Save &As..." ) );

    fileMenu->AppendSeparator();

    ACTION_MENU* submenuImport = new ACTION_MENU( false, selTool );
    submenuImport->SetTitle( _( "&Import" ) );
    submenuImport->SetIcon( BITMAPS::import );

    submenuImport->Add( PCB_ACTIONS::importFootprint,       ACTION_MENU::NORMAL, _( "&Footprint..." ) );
    submenuImport->Add( PCB_ACTIONS::placeImportedGraphics, ACTION_MENU::NORMAL, _( "&DXF/DWG..." ) );

    fileMenu->Add( submenuImport );

    ACTION_MENU* submenuExport = new ACTION_MENU( false, selTool );
    submenuExport->SetTitle( _( "&Export" ) );
    submenuExport->SetIcon( BITMAPS::export_file );

    submenuExport->Add( PCB_ACTIONS::exportFootprint, ACTION_MENU::NORMAL, _( "&Footprint..." ) );
    submenuExport->Add( _( "View as &PNG..." ),
                        _( "Create a PNG file from the current view" ),
                        ID_FPEDIT_SAVE_PNG,
                        BITMAPS::export_png );

    fileMenu->Add( submenuExport );

    fileMenu->AppendSeparator();
    fileMenu->Add( ACTIONS::print, ACTION_MENU::NORMAL, _( "&Print..." ) );

    fileMenu->AppendSeparator();
    // Altium: Exit (Alt+F4) leaves the application
    fileMenu->Add( _( "E&xit" ) + wxS( "\tAlt+F4" ), _( "Close all documents and exit" ),
                   wxID_EXIT, BITMAPS::exit );

    // KiCad-only
    fileMenu->AppendSeparator();
    fileMenu->Add( ACTIONS::revert, ACTION_MENU::NORMAL, _( "&Revert" ) );
    fileMenu->Add( PCB_ACTIONS::editLibFpInFpEditor );


    //-- Edit menu -------------------------------------------------------
    //
    ACTION_MENU* editMenu = new ACTION_MENU( false, selTool );

    editMenu->Add( ACTIONS::undo, ACTION_MENU::NORMAL, _( "&Undo" ) );
    editMenu->Add( ACTIONS::redo, ACTION_MENU::NORMAL, _( "&Redo" ) );

    editMenu->AppendSeparator();
    editMenu->Add( ACTIONS::cut,          ACTION_MENU::NORMAL, _( "Cu&t" ) );
    editMenu->Add( ACTIONS::copy,         ACTION_MENU::NORMAL, _( "&Copy" ) );
    editMenu->Add( ACTIONS::copyAsText,   ACTION_MENU::NORMAL, _( "Copy As Text" ) );
    editMenu->Add( ACTIONS::paste,        ACTION_MENU::NORMAL, _( "&Paste" ) );
    editMenu->Add( ACTIONS::pasteSpecial, ACTION_MENU::NORMAL, _( "Paste Spec&ial..." ) );

    editMenu->AppendSeparator();
    editMenu->Add( PCB_ACTIONS::copyFootprint,  ACTION_MENU::NORMAL, _( "Cop&y Component" ) );
    editMenu->Add( PCB_ACTIONS::pasteFootprint, ACTION_MENU::NORMAL, _( "Paste C&omponent" ) );

    editMenu->AppendSeparator();

    ACTION_MENU* selectSubMenu = new ACTION_MENU( false, selTool );
    selectSubMenu->SetTitle( _( "&Select" ) );
    selectSubMenu->Add( ACTIONS::selectSetLasso,         ACTION_MENU::NORMAL, _( "Lasso S&elect" ) );
    selectSubMenu->Add( PCB_ACTIONS::selectInsideArea,   ACTION_MENU::NORMAL, _( "&Inside Area" ) );
    selectSubMenu->Add( PCB_ACTIONS::selectOutsideArea,  ACTION_MENU::NORMAL, _( "&Outside Area" ) );
    selectSubMenu->Add( PCB_ACTIONS::selectTouchingRect, ACTION_MENU::NORMAL, _( "To&uching Rectangle" ) );
    selectSubMenu->Add( PCB_ACTIONS::selectTouchingLine, ACTION_MENU::NORMAL, _( "Touching &Line" ) );
    selectSubMenu->Add( PCB_ACTIONS::selectAllOnLayer,   ACTION_MENU::NORMAL, _( "All on La&yer" ) );
    selectSubMenu->Add( ACTIONS::selectAll,              ACTION_MENU::NORMAL, _( "&All" ) );
    selectSubMenu->Add( PCB_ACTIONS::selectOffGridPads,  ACTION_MENU::NORMAL, _( "Off &Grid Pads" ) );
    selectSubMenu->AppendSeparator();
    selectSubMenu->Add( PCB_ACTIONS::toggleSelection,    ACTION_MENU::NORMAL, _( "&Toggle Selection" ) );
    editMenu->Add( selectSubMenu );

    ACTION_MENU* deselectSubMenu = new ACTION_MENU( false, selTool );
    deselectSubMenu->SetTitle( _( "D&eSelect" ) );
    deselectSubMenu->Add( PCB_ACTIONS::deselectLasso,        ACTION_MENU::NORMAL, _( "Lasso Des&elect" ) );
    deselectSubMenu->Add( PCB_ACTIONS::deselectInsideArea,   ACTION_MENU::NORMAL, _( "&Inside Area" ) );
    deselectSubMenu->Add( PCB_ACTIONS::deselectOutsideArea,  ACTION_MENU::NORMAL, _( "&Outside Area" ) );
    deselectSubMenu->Add( PCB_ACTIONS::deselectTouchingLine, ACTION_MENU::NORMAL, _( "Touching &Line" ) );
    deselectSubMenu->Add( PCB_ACTIONS::deselectTouchingRect, ACTION_MENU::NORMAL, _( "Touching &Rectangle" ) );
    deselectSubMenu->Add( PCB_ACTIONS::deselectAllOnLayer,   ACTION_MENU::NORMAL, _( "All on La&yer" ) );
    deselectSubMenu->Add( ACTIONS::unselectAll,              ACTION_MENU::NORMAL, _( "&All" ) );
    deselectSubMenu->AppendSeparator();
    deselectSubMenu->Add( PCB_ACTIONS::toggleSelection,      ACTION_MENU::NORMAL, _( "&Toggle Selection" ) );
    editMenu->Add( deselectSubMenu );

    editMenu->AppendSeparator();
    editMenu->Add( ACTIONS::doDelete,  ACTION_MENU::NORMAL, _( "&Delete" ) );
    editMenu->Add( ACTIONS::duplicate, ACTION_MENU::NORMAL, _( "Duplicate" ) );

    ACTION_MENU* moveSubMenu = new ACTION_MENU( false, selTool );
    moveSubMenu->SetTitle( _( "&Move" ) );
    moveSubMenu->SetIcon( BITMAPS::move );
    moveSubMenu->Add( PCB_ACTIONS::move,      ACTION_MENU::NORMAL, _( "&Move" ) );
    moveSubMenu->AppendSeparator();
    moveSubMenu->Add( PCB_ACTIONS::moveExact, ACTION_MENU::NORMAL, _( "Move Selection by &X, Y..." ) );
    moveSubMenu->Add( PCB_ACTIONS::rotateCcw, ACTION_MENU::NORMAL, _( "Ro&tate Selection" ) );
    moveSubMenu->Add( PCB_ACTIONS::flip,      ACTION_MENU::NORMAL, _( "Fl&ip Selection" ) );

    // KiCad-only
    moveSubMenu->AppendSeparator();
    moveSubMenu->Add( PCB_ACTIONS::rotateCw,  ACTION_MENU::NORMAL, _( "Rotate Clock&wise" ) );
    moveSubMenu->Add( PCB_ACTIONS::mirrorH,   ACTION_MENU::NORMAL, _( "Mirror &Horizontally" ) );
    moveSubMenu->Add( PCB_ACTIONS::mirrorV,   ACTION_MENU::NORMAL, _( "Mirror &Vertically" ) );
    editMenu->Add( moveSubMenu );

    ACTION_MENU* alignSubMenu = new ACTION_MENU( false, selTool );
    alignSubMenu->SetTitle( _( "Ali&gn" ) );
    alignSubMenu->SetIcon( BITMAPS::align_items );
    alignSubMenu->Add( PCB_ACTIONS::alignLeft,    ACTION_MENU::NORMAL, _( "Align &Left" ) );
    alignSubMenu->Add( PCB_ACTIONS::alignRight,   ACTION_MENU::NORMAL, _( "Align &Right" ) );
    alignSubMenu->Add( PCB_ACTIONS::alignCenterX, ACTION_MENU::NORMAL, _( "Align Horizontal &Centers" ) );
    alignSubMenu->Add( PCB_ACTIONS::distributeHorizontallyCenters, ACTION_MENU::NORMAL,
                       _( "&Distribute Horizontally" ) );
    alignSubMenu->Add( PCB_ACTIONS::increaseHorizontalSpacing, ACTION_MENU::NORMAL,
                       _( "Increase Horizontal Spacing" ) );
    alignSubMenu->Add( PCB_ACTIONS::decreaseHorizontalSpacing, ACTION_MENU::NORMAL,
                       _( "Decrease Horizontal Spacing" ) );
    alignSubMenu->AppendSeparator();
    alignSubMenu->Add( PCB_ACTIONS::alignTop,     ACTION_MENU::NORMAL, _( "Align &Top" ) );
    alignSubMenu->Add( PCB_ACTIONS::alignBottom,  ACTION_MENU::NORMAL, _( "Align &Bottom" ) );
    alignSubMenu->Add( PCB_ACTIONS::alignCenterY, ACTION_MENU::NORMAL, _( "Align &Vertical Centers" ) );
    alignSubMenu->Add( PCB_ACTIONS::distributeVerticallyCenters, ACTION_MENU::NORMAL,
                       _( "Di&stribute Vertically" ) );
    alignSubMenu->Add( PCB_ACTIONS::increaseVerticalSpacing, ACTION_MENU::NORMAL,
                       _( "Increase Vertical Spacing" ) );
    alignSubMenu->Add( PCB_ACTIONS::decreaseVerticalSpacing, ACTION_MENU::NORMAL,
                       _( "Decrease Vertical Spacing" ) );
    alignSubMenu->AppendSeparator();
    alignSubMenu->Add( PCB_ACTIONS::alignToGrid,  ACTION_MENU::NORMAL, _( "Align To &Grid" ) );
    editMenu->Add( alignSubMenu );

    editMenu->AppendSeparator();

    ACTION_MENU* refSubMenu = new ACTION_MENU( false, selTool );
    refSubMenu->SetTitle( _( "Set Re&ference" ) );
    refSubMenu->SetIcon( BITMAPS::anchor );
    refSubMenu->Add( PCB_ACTIONS::setAnchorPin1,   ACTION_MENU::NORMAL, _( "&Pin 1" ) );
    refSubMenu->Add( PCB_ACTIONS::setAnchorCenter, ACTION_MENU::NORMAL, _( "&Center" ) );
    refSubMenu->Add( PCB_ACTIONS::setAnchor,       ACTION_MENU::NORMAL, _( "&Location" ) );
    editMenu->Add( refSubMenu );

    ACTION_MENU* jumpSubMenu = new ACTION_MENU( false, selTool );
    jumpSubMenu->SetTitle( _( "&Jump" ) );
    // The footprint reference point is the editor origin
    jumpSubMenu->Add( PCB_ACTIONS::jumpAbsoluteOrigin, ACTION_MENU::NORMAL, _( "&Reference" ) );
    jumpSubMenu->Add( PCB_ACTIONS::jumpNewLocation,    ACTION_MENU::NORMAL, _( "New &Location..." ) );
    jumpSubMenu->AppendSeparator();
    jumpSubMenu->Add( ACTIONS::centerSelection,        ACTION_MENU::NORMAL, _( "Selec&tion" ) );
    editMenu->Add( jumpSubMenu );

    editMenu->AppendSeparator();
    editMenu->Add( PCB_ACTIONS::findSimilar, ACTION_MENU::NORMAL, _( "Fi&nd Similar Objects" ) );

    // KiCad-only
    editMenu->AppendSeparator();
    editMenu->Add( ACTIONS::group );
    editMenu->Add( ACTIONS::ungroup );
    editMenu->Add( PCB_ACTIONS::editTextAndGraphics );
    editMenu->Add( PCB_ACTIONS::padTable );
    editMenu->Add( PCB_ACTIONS::defaultPadProperties );


    //-- View menu -------------------------------------------------------
    //
    ACTION_MENU* viewMenu = new ACTION_MENU( false, selTool );

    viewMenu->Add( ACTIONS::zoomFitScreen,    ACTION_MENU::NORMAL, _( "Fit &Document" ) );
    viewMenu->Add( ACTIONS::zoomFitObjects,   ACTION_MENU::NORMAL, _( "&Fit All Objects" ) );
    viewMenu->Add( ACTIONS::zoomTool,         ACTION_MENU::NORMAL, _( "&Area" ) );
    viewMenu->Add( ACTIONS::zoomCenter,       ACTION_MENU::NORMAL, _( "Around &Point" ) );
    viewMenu->Add( ACTIONS::zoomFitSelection, ACTION_MENU::NORMAL, _( "S&elected Objects" ) );

    viewMenu->AppendSeparator();
    viewMenu->Add( ACTIONS::zoomInCenter,  ACTION_MENU::NORMAL, _( "Zoom &In" ) );
    viewMenu->Add( ACTIONS::zoomOutCenter, ACTION_MENU::NORMAL, _( "Zoom &Out" ) );
    viewMenu->Add( PCB_ACTIONS::flipBoard, ACTION_MENU::CHECK,  _( "Flip &Board" ) );

    viewMenu->AppendSeparator();
    viewMenu->Add( PCB_ACTIONS::fullScreen, ACTION_MENU::CHECK, _( "Full Screen" ) );

    viewMenu->AppendSeparator();
    ACTION_MENU* showHidePanels = new ACTION_MENU( false, selTool );
    showHidePanels->SetTitle( _( "Pa&nels" ) );
    showHidePanels->Add( ACTIONS::showLibraryTree,       ACTION_MENU::CHECK, _( "PCB &Library" ) );
    showHidePanels->Add( PCB_ACTIONS::showLayersManager, ACTION_MENU::CHECK, _( "&View Configuration" ) );
    showHidePanels->Add( ACTIONS::showProperties,        ACTION_MENU::CHECK, _( "&Properties" ) );
    viewMenu->Add( showHidePanels );

    viewMenu->AppendSeparator();
    viewMenu->Add( PCB_ACTIONS::toggleStatusBar, ACTION_MENU::CHECK, _( "S&tatus Bar" ) );

    viewMenu->AppendSeparator();
    ACTION_MENU* gridsSubMenu = new ACTION_MENU( false, selTool );
    gridsSubMenu->SetTitle( _( "&Grids" ) );
    gridsSubMenu->SetIcon( BITMAPS::grid );
    gridsSubMenu->Add( ACTIONS::toggleGrid, ACTION_MENU::CHECK, _( "Toggle &Visible Grid" ) );
    gridsSubMenu->Add( PCB_ACTIONS::magneticSnapToggle, ACTION_MENU::NORMAL,
                       _( "Toggle Ob&ject Hotspot Snapping" ) );
    gridsSubMenu->AppendSeparator();
    gridsSubMenu->Add( ACTIONS::gridProperties, ACTION_MENU::NORMAL, _( "Set Global Snap &Grid..." ) );
    viewMenu->Add( gridsSubMenu );

    viewMenu->Add( ACTIONS::toggleUnits, ACTION_MENU::NORMAL, _( "Toggle &Units" ) );

    // KiCad-only
    viewMenu->AppendSeparator();
    viewMenu->Add( ACTIONS::showFootprintBrowser );
    viewMenu->Add( ACTIONS::show3DViewer );
    viewMenu->Add( ACTIONS::zoomRedraw, ACTION_MENU::NORMAL, _( "Refresh" ) );

    ACTION_MENU* drawingModeSubMenu = new ACTION_MENU( false, selTool );
    drawingModeSubMenu->SetTitle( _( "Drawing &Mode" ) );
    drawingModeSubMenu->SetIcon( BITMAPS::add_zone );
    drawingModeSubMenu->Add( PCB_ACTIONS::padDisplayMode,   ACTION_MENU::CHECK );
    drawingModeSubMenu->Add( PCB_ACTIONS::graphicsOutlines, ACTION_MENU::CHECK );
    drawingModeSubMenu->Add( PCB_ACTIONS::textOutlines,     ACTION_MENU::CHECK );
    viewMenu->Add( drawingModeSubMenu );

    ACTION_MENU* contrastModeSubMenu = new ACTION_MENU( false, selTool );
    contrastModeSubMenu->SetTitle( _( "Single La&yer Mode" ) );
    contrastModeSubMenu->SetIcon( BITMAPS::contrast_mode );
    contrastModeSubMenu->Add( ACTIONS::highContrastMode, ACTION_MENU::CHECK );
    contrastModeSubMenu->Add( PCB_ACTIONS::layerAlphaDec );
    contrastModeSubMenu->Add( PCB_ACTIONS::layerAlphaInc );
    viewMenu->Add( contrastModeSubMenu );


    //-- Place menu -------------------------------------------------------
    //
    ACTION_MENU* placeMenu = new ACTION_MENU( false, selTool );

    placeMenu->Add( PCB_ACTIONS::drawArc,    ACTION_MENU::NORMAL, _( "&Arc (Center)" ) );
    placeMenu->Add( PCB_ACTIONS::drawCircle, ACTION_MENU::NORMAL, _( "F&ull Circle" ) );

    placeMenu->AppendSeparator();
    placeMenu->Add( PCB_ACTIONS::drawRectangle,  ACTION_MENU::NORMAL, _( "&Fill" ) );
    placeMenu->Add( PCB_ACTIONS::drawPolygon,    ACTION_MENU::NORMAL, _( "Solid &Region" ) );
    placeMenu->Add( PCB_ACTIONS::drawLine,       ACTION_MENU::NORMAL, _( "&Line" ) );
    placeMenu->Add( PCB_ACTIONS::placeText,      ACTION_MENU::NORMAL, _( "&String" ) );
    placeMenu->Add( PCB_ACTIONS::drawTextBox,    ACTION_MENU::NORMAL, _( "Te&xt Frame" ) );
    placeMenu->Add( PCB_ACTIONS::placePad,       ACTION_MENU::NORMAL, _( "&Pad" ) );
    placeMenu->Add( PCB_ACTIONS::drawZoneCutout, ACTION_MENU::NORMAL, _( "Polygon Pour Cutout" ) );
    placeMenu->Add( PCB_ACTIONS::drawRectangle,  ACTION_MENU::NORMAL, _( "Re&ctangle" ) );
    placeMenu->Add( PCB_ACTIONS::placeImportedGraphics, ACTION_MENU::NORMAL, _( "Grap&hics" ) );

    placeMenu->AppendSeparator();

    ACTION_MENU* keepoutSubMenu = new ACTION_MENU( false, selTool );
    keepoutSubMenu->SetTitle( _( "&Keepout" ) );
    keepoutSubMenu->SetIcon( BITMAPS::add_keepout_area );
    keepoutSubMenu->Add( PCB_ACTIONS::drawRuleArea, ACTION_MENU::NORMAL, _( "Solid &Region" ) );
    placeMenu->Add( keepoutSubMenu );

    placeMenu->AppendSeparator();

    ACTION_MENU* dimensionSubMenu = new ACTION_MENU( false, selTool );
    dimensionSubMenu->SetTitle( _( "&Dimension" ) );
    dimensionSubMenu->SetIcon( BITMAPS::add_aligned_dimension );
    dimensionSubMenu->Add( PCB_ACTIONS::drawOrthogonalDimension, ACTION_MENU::NORMAL, _( "&Linear" ) );
    dimensionSubMenu->Add( PCB_ACTIONS::drawRadialDimension,     ACTION_MENU::NORMAL, _( "&Radial" ) );
    dimensionSubMenu->Add( PCB_ACTIONS::drawLeader,              ACTION_MENU::NORMAL, _( "L&eader" ) );
    dimensionSubMenu->Add( PCB_ACTIONS::drawCenterDimension,     ACTION_MENU::NORMAL, _( "&Center" ) );

    // KiCad-only
    dimensionSubMenu->AppendSeparator();
    dimensionSubMenu->Add( PCB_ACTIONS::drawAlignedDimension, ACTION_MENU::NORMAL, _( "&Aligned" ) );
    placeMenu->Add( dimensionSubMenu );

    // KiCad-only
    placeMenu->AppendSeparator();
    placeMenu->Add( PCB_ACTIONS::drawBezier );
    placeMenu->Add( PCB_ACTIONS::placeReferenceImage );
    placeMenu->Add( PCB_ACTIONS::drawTable );
    placeMenu->Add( PCB_ACTIONS::placePoint, ACTION_MENU::NORMAL, _( "C&oordinate" ) );
    placeMenu->Add( PCB_ACTIONS::placeBarcode );
    placeMenu->Add( ACTIONS::gridSetOrigin );
    placeMenu->Add( ACTIONS::gridResetOrigin );


    //-- Project menu ------------------------------------------------------
    //
    ACTION_MENU* projectMenu = new ACTION_MENU( false, selTool );

    projectMenu->Add( ACTIONS::configurePaths );
    projectMenu->Add( ACTIONS::showFootprintLibTable );

    //-- Tools menu --------------------------------------------------------
    //
    ACTION_MENU* toolsMenu = new ACTION_MENU( false, selTool );

    toolsMenu->Add( PCB_ACTIONS::newFootprint,       ACTION_MENU::NORMAL, _( "Ne&w Blank Footprint" ) );
    toolsMenu->Add( PCB_ACTIONS::ipcFootprintWizard, ACTION_MENU::NORMAL,
                    _( "&IPC Compliant Footprint Wizard..." ) );
    toolsMenu->Append( wxID_ANY, _( "IPC Compliant Footprints &Batch generator..." ) )->Enable( false );

    // KiCad's scripted footprint wizards
    toolsMenu->Add( PCB_ACTIONS::createFootprint, ACTION_MENU::NORMAL, _( "&Footprint Wizard..." ) );

    toolsMenu->AppendSeparator();
    toolsMenu->Add( PCB_ACTIONS::deleteFootprint,     ACTION_MENU::NORMAL, _( "&Remove Footprint" ) );
    toolsMenu->Add( PCB_ACTIONS::footprintProperties, ACTION_MENU::NORMAL, _( "F&ootprint Properties..." ) );

    // Altium's silkscreen clean-up has no KiCad counterpart
    toolsMenu->AppendSeparator();
    toolsMenu->Append( wxID_ANY, _( "Silkscreen Preparation" ) )->Enable( false );

    // The footprint's 3D models are on a page of its properties
    toolsMenu->AppendSeparator();
    toolsMenu->Add( PCB_ACTIONS::footprintProperties, ACTION_MENU::NORMAL,
                    _( "&Manage 3D Bodies for Current Component..." ) );

    toolsMenu->AppendSeparator();

    ACTION_MENU* convertSubMenu = new ACTION_MENU( false, selTool );
    convertSubMenu->SetTitle( _( "Con&vert" ) );
    convertSubMenu->SetIcon( BITMAPS::convert );
    convertSubMenu->Add( PCB_ACTIONS::convertToPoly, ACTION_MENU::NORMAL,
                         _( "Create Re&gion from Selected Primitives" ) );
    convertSubMenu->AppendSeparator();
    convertSubMenu->Add( PCB_ACTIONS::convertToKeepout, ACTION_MENU::NORMAL,
                         _( "Convert Selected Primitives to &Keepouts" ) );
    convertSubMenu->AppendSeparator();
    convertSubMenu->Add( PCB_ACTIONS::recombinePad, ACTION_MENU::NORMAL,
                         _( "Add Selected Region to C&ustom Pad" ) );
    convertSubMenu->Add( PCB_ACTIONS::explodePad, ACTION_MENU::NORMAL,
                         _( "E&xplode Custom Pad to Free Primitives" ) );

    // KiCad-only
    convertSubMenu->AppendSeparator();
    convertSubMenu->Add( PCB_ACTIONS::convertToLines, ACTION_MENU::NORMAL, _( "Create &Lines from Selection" ) );
    convertSubMenu->Add( PCB_ACTIONS::convertToArc,   ACTION_MENU::NORMAL, _( "Create &Arc from Selection" ) );
    toolsMenu->Add( convertSubMenu );

    toolsMenu->Add( PCB_ACTIONS::saveFpToBoard, ACTION_MENU::NORMAL,
                    _( "&Update PCB With Current Footprint" ) );
    toolsMenu->Add( PCB_ACTIONS::updateAllFootprintsOnBoard, ACTION_MENU::NORMAL,
                    _( "Update PCB With &All Footprints" ) );

    toolsMenu->AppendSeparator();
    toolsMenu->Append( wxID_ANY, _( "Layer Stac&k Manager..." ) )->Enable( false );
    toolsMenu->Add( ACTIONS::openPreferences, ACTION_MENU::NORMAL, _( "&Preferences..." ) );

    // KiCad-only
    toolsMenu->AppendSeparator();
    toolsMenu->Add( PCB_ACTIONS::loadFpFromBoard, ACTION_MENU::NORMAL, _( "Load Footprint From P&CB" ) );
    toolsMenu->Add( PCB_ACTIONS::cleanupGraphics, ACTION_MENU::NORMAL, _( "Clea&nup Graphics..." ) );
    toolsMenu->Add( PCB_ACTIONS::repairFootprint, ACTION_MENU::NORMAL, _( "Rep&air Footprint" ) );
    toolsMenu->Add( PCB_ACTIONS::enumeratePads,   ACTION_MENU::NORMAL, _( "Renum&ber Pads..." ) );
    AddMenuLanguageList( toolsMenu, selTool );

    //-- Reports menu ------------------------------------------------------
    //
    ACTION_MENU* reportsMenu = new ACTION_MENU( false, selTool );

    reportsMenu->Add( PCB_ACTIONS::checkFootprint, ACTION_MENU::NORMAL, _( "Component &Rule Check..." ) );

    reportsMenu->AppendSeparator();
    reportsMenu->Add( ACTIONS::measureTool, ACTION_MENU::NORMAL, _( "&Measure Distance" ) );

    // KiCad-only
    reportsMenu->AppendSeparator();
    reportsMenu->Add( PCB_ACTIONS::showDatasheet, ACTION_MENU::NORMAL, _( "&Datasheet" ) );

    //--MenuBar -----------------------------------------------------------
    //
    menuBar->Append( fileMenu,    _( "&File" ) );
    menuBar->Append( editMenu,    _( "&Edit" ) );
    menuBar->Append( viewMenu,    _( "&View" ) );
    menuBar->Append( projectMenu, _( "Proje&ct" ) );
    menuBar->Append( placeMenu,   _( "&Place" ) );
    menuBar->Append( toolsMenu,   _( "&Tools" ) );
    menuBar->Append( reportsMenu, _( "&Reports" ) );
    menuBar->Append( new WINDOW_MENU( this ), _( "&Window" ) );
    AddStandardHelpMenu( menuBar );

    SetMenuBar( menuBar );
    delete oldMenuBar;
}
