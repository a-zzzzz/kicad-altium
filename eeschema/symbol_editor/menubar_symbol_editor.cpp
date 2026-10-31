/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2007 Jean-Pierre Charras, jaen-pierre.charras@gipsa-lab.inpg.com
 * Copyright (C) 2009 Wayne Stambaugh <stambaughw@gmail.com>
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
#include <bitmaps.h>
#include <eeschema_id.h>
#include <tool/action_menu.h>
#include <tool/tool_manager.h>
#include <tools/sch_actions.h>
#include <tools/sch_selection_tool.h>
#include <symbol_library_manager.h>
#include "symbol_edit_frame.h"
#include <widgets/window_menu.h>
#include <widgets/wx_menubar.h>


void SYMBOL_EDIT_FRAME::doReCreateMenuBar()
{
    SCH_SELECTION_TOOL* selTool = m_toolManager->GetTool<SCH_SELECTION_TOOL>();
    // wxWidgets handles the Mac Application menu behind the scenes, but that means
    // we always have to start from scratch with a new wxMenuBar.
    wxMenuBar*  oldMenuBar = GetMenuBar();
    WX_MENUBAR* menuBar    = new WX_MENUBAR();

    // Menus follow Altium Designer's Schematic Library editor: File, Edit, View, Project,
    // Place, Tools, Reports, Window, Help.  The underlined letters are the ones typed as menu
    // chords (docs/altium-parity/chords.md) and are unique within each menu.  KiCad-only
    // commands come last, after a separator.

    //-- File menu -----------------------------------------------
    //
    ACTION_MENU* fileMenu = new ACTION_MENU( false, selTool );

    ACTION_MENU* newMenu = new ACTION_MENU( false, selTool );
    newMenu->SetTitle( _( "&New" ) );
    newMenu->SetIcon( BITMAPS::new_library );
    newMenu->Add( ACTIONS::newLibrary,    ACTION_MENU::NORMAL, _( "&Library..." ) );

    // KiCad-only
    newMenu->AppendSeparator();
    newMenu->Add( SCH_ACTIONS::newSymbol, ACTION_MENU::NORMAL, _( "&Component..." ) );
    fileMenu->Add( newMenu );

    fileMenu->Add( ACTIONS::addLibrary, ACTION_MENU::NORMAL, _( "&Open..." ) );
    fileMenu->Add( _( "&Close" ) + wxS( "\tCtrl+F4" ), _( "Close the library editor" ),
                   wxID_CLOSE, BITMAPS::exit );

    fileMenu->AppendSeparator();
    fileMenu->Add( ACTIONS::save,                 ACTION_MENU::NORMAL, _( "&Save" ) );
    fileMenu->Add( SCH_ACTIONS::saveSymbolAs,     ACTION_MENU::NORMAL, _( "Save &As..." ) );
    fileMenu->Add( SCH_ACTIONS::saveSymbolCopyAs, ACTION_MENU::NORMAL, _( "Save Cop&y As..." ) );

    if( !IsSymbolFromSchematic() )
        fileMenu->Add( ACTIONS::saveAll, ACTION_MENU::NORMAL, _( "Save A&ll" ) );

    fileMenu->AppendSeparator();

    ACTION_MENU* submenuImport = new ACTION_MENU( false, selTool );
    submenuImport->SetTitle( _( "&Import" ) );
    submenuImport->SetIcon( BITMAPS::import );

    submenuImport->Add( SCH_ACTIONS::importSymbol,   ACTION_MENU::NORMAL, _( "&Symbol..." ) );
    submenuImport->Add( SCH_ACTIONS::importGraphics, ACTION_MENU::NORMAL, _( "&Graphics..." ) );

    fileMenu->Add( submenuImport );

    ACTION_MENU* submenuExport = new ACTION_MENU( false, selTool );
    submenuExport->SetTitle( _( "&Export" ) );
    submenuExport->SetIcon( BITMAPS::export_file );
    submenuExport->Add( SCH_ACTIONS::exportSymbol,      ACTION_MENU::NORMAL, _( "&Symbol..." ) );
    submenuExport->Add( SCH_ACTIONS::exportSymbolView,  ACTION_MENU::NORMAL, _( "View as &PNG..." ) );
    submenuExport->Add( SCH_ACTIONS::exportSymbolAsSVG, ACTION_MENU::NORMAL, _( "Symbol as S&VG..." ) );
    fileMenu->Add( submenuExport );

    fileMenu->AppendSeparator();
    // Altium: Exit (Alt+F4) leaves the application
    fileMenu->Add( _( "E&xit" ) + wxS( "\tAlt+F4" ), _( "Close all documents and exit" ),
                   wxID_EXIT, BITMAPS::exit );

    // KiCad-only
    fileMenu->AppendSeparator();
    fileMenu->Add( SCH_ACTIONS::saveLibraryAs, ACTION_MENU::NORMAL, _( "Save Li&brary As..." ) );
    fileMenu->Add( ACTIONS::revert,            ACTION_MENU::NORMAL, _( "&Revert" ) );
    fileMenu->Add( SCH_ACTIONS::editLibSymbolWithLibEdit );


    //-- Edit menu -----------------------------------------------
    //
    ACTION_MENU* editMenu = new ACTION_MENU( false, selTool );

    editMenu->Add( ACTIONS::undo, ACTION_MENU::NORMAL, _( "&Undo" ) );
    editMenu->Add( ACTIONS::redo, ACTION_MENU::NORMAL, _( "&Redo" ) );

    editMenu->AppendSeparator();
    editMenu->Add( ACTIONS::cut,        ACTION_MENU::NORMAL, _( "Cu&t" ) );
    editMenu->Add( ACTIONS::copy,       ACTION_MENU::NORMAL, _( "&Copy" ) );
    editMenu->Add( ACTIONS::copyAsText, ACTION_MENU::NORMAL, _( "Copy As Text" ) );
    editMenu->Add( ACTIONS::paste,      ACTION_MENU::NORMAL, _( "&Paste" ) );

    editMenu->AppendSeparator();
    editMenu->Add( ACTIONS::find,           ACTION_MENU::NORMAL, _( "&Find Text..." ) );
    editMenu->Add( ACTIONS::findAndReplace, ACTION_MENU::NORMAL, _( "Rep&lace Text..." ) );
    editMenu->Add( ACTIONS::findNext,       ACTION_MENU::NORMAL, _( "Find Ne&xt" ) );

    editMenu->AppendSeparator();

    ACTION_MENU* selectSubMenu = new ACTION_MENU( false, selTool );
    selectSubMenu->SetTitle( _( "&Select" ) );
    selectSubMenu->Add( ACTIONS::selectSetLasso, ACTION_MENU::CHECK,  _( "Lasso S&elect" ) );
    selectSubMenu->Add( ACTIONS::selectSetRect,  ACTION_MENU::CHECK,  _( "&Inside Area" ) );
    selectSubMenu->Add( ACTIONS::selectAll,      ACTION_MENU::NORMAL, _( "&All" ) );
    editMenu->Add( selectSubMenu );

    ACTION_MENU* deselectSubMenu = new ACTION_MENU( false, selTool );
    deselectSubMenu->SetTitle( _( "D&eSelect" ) );
    deselectSubMenu->Add( ACTIONS::unselectAll, ACTION_MENU::NORMAL, _( "&All" ) );
    editMenu->Add( deselectSubMenu );

    editMenu->AppendSeparator();
    editMenu->Add( ACTIONS::doDelete,  ACTION_MENU::NORMAL, _( "&Delete" ) );
    editMenu->Add( ACTIONS::duplicate, ACTION_MENU::NORMAL, _( "Duplicate" ) );

    ACTION_MENU* moveSubMenu = new ACTION_MENU( false, selTool );
    moveSubMenu->SetTitle( _( "&Move" ) );
    moveSubMenu->SetIcon( BITMAPS::move );
    moveSubMenu->Add( SCH_ACTIONS::move,      ACTION_MENU::NORMAL, _( "&Move" ) );
    moveSubMenu->Add( SCH_ACTIONS::rotateCCW, ACTION_MENU::NORMAL, _( "Rota&te Selection" ) );
    moveSubMenu->Add( SCH_ACTIONS::rotateCW,  ACTION_MENU::NORMAL, _( "Rotate Selecti&on Clockwise" ) );

    // KiCad-only
    moveSubMenu->AppendSeparator();
    moveSubMenu->Add( SCH_ACTIONS::mirrorH,   ACTION_MENU::NORMAL, _( "Mirror &Horizontally" ) );
    moveSubMenu->Add( SCH_ACTIONS::mirrorV,   ACTION_MENU::NORMAL, _( "Mirror &Vertically" ) );
    moveSubMenu->Add( SCH_ACTIONS::swap,      ACTION_MENU::NORMAL, _( "S&wap" ) );
    editMenu->Add( moveSubMenu );

    ACTION_MENU* alignSubMenu = new ACTION_MENU( false, selTool );
    alignSubMenu->SetTitle( _( "Ali&gn" ) );
    alignSubMenu->SetIcon( BITMAPS::align_items );
    alignSubMenu->Add( SCH_ACTIONS::alignToGrid, ACTION_MENU::NORMAL, _( "Align To &Grid" ) );
    editMenu->Add( alignSubMenu );

    editMenu->AppendSeparator();

    wxMenu* jumpSubMenu = new wxMenu;
    jumpSubMenu->Append( ID_LIBEDIT_JUMP_ORIGIN,   _( "&Origin" ) + wxS( "\tCtrl+Home" ) );
    jumpSubMenu->Append( ID_LIBEDIT_JUMP_LOCATION, _( "New &Location..." ) );
    editMenu->AppendSubMenu( jumpSubMenu, _( "&Jump" ) );

    // KiCad-only
    editMenu->AppendSeparator();
    editMenu->Add( ACTIONS::deleteTool );


    //-- View menu -----------------------------------------------
    //
    ACTION_MENU* viewMenu = new ACTION_MENU( false, selTool );

    viewMenu->Add( ACTIONS::zoomFitScreen,    ACTION_MENU::NORMAL, _( "&Fit All Objects" ) );
    viewMenu->Add( ACTIONS::zoomTool,         ACTION_MENU::NORMAL, _( "&Area" ) );
    viewMenu->Add( ACTIONS::zoomCenter,       ACTION_MENU::NORMAL, _( "Around &Point" ) );
    viewMenu->Add( ACTIONS::zoomFitSelection, ACTION_MENU::NORMAL, _( "S&elected Objects" ) );

    viewMenu->AppendSeparator();
    viewMenu->Add( ACTIONS::zoomInCenter,     ACTION_MENU::NORMAL, _( "Zoom &In" ) );
    viewMenu->Add( ACTIONS::zoomOutCenter,    ACTION_MENU::NORMAL, _( "Zoom &Out" ) );

    viewMenu->AppendSeparator();
    viewMenu->AppendCheckItem( ID_LIBEDIT_FULL_SCREEN, _( "Full Screen" ) + wxS( "\tAlt+F5" ) );

    viewMenu->AppendSeparator();

    ACTION_MENU* showHidePanels = new ACTION_MENU( false, selTool );
    showHidePanels->SetTitle( _( "Pa&nels" ) );
    showHidePanels->AppendCheckItem( ID_LIBEDIT_SHOW_SCH_LIBRARY, _( "SCH &Library" ) );
    showHidePanels->Add( ACTIONS::showProperties,  ACTION_MENU::CHECK, _( "&Properties" ) );
    viewMenu->Add( showHidePanels );

    viewMenu->AppendSeparator();
    viewMenu->AppendCheckItem( ID_LIBEDIT_STATUS_BAR, _( "S&tatus Bar" ) );

    viewMenu->AppendSeparator();

    ACTION_MENU* gridsSubMenu = new ACTION_MENU( false, selTool );
    gridsSubMenu->SetTitle( _( "&Grids" ) );
    gridsSubMenu->SetIcon( BITMAPS::grid );
    gridsSubMenu->Add( ACTIONS::toggleGrid,          ACTION_MENU::CHECK, _( "Toggle &Visible Grid" ) );
    gridsSubMenu->Add( ACTIONS::toggleGridOverrides, ACTION_MENU::CHECK );
    gridsSubMenu->AppendSeparator();
    gridsSubMenu->Add( ACTIONS::gridProperties, ACTION_MENU::NORMAL, _( "Set Snap &Grid..." ) );
    viewMenu->Add( gridsSubMenu );
    viewMenu->Add( ACTIONS::toggleUnits, ACTION_MENU::NORMAL, _( "Toggle &Units" ) );

    viewMenu->AppendSeparator();
    viewMenu->Add( SCH_ACTIONS::showHiddenPins, ACTION_MENU::CHECK, _( "Show &Hidden Pins" ) );

    // KiCad-only
    viewMenu->AppendSeparator();
    viewMenu->Add( ACTIONS::showSymbolBrowser );
    viewMenu->Add( ACTIONS::zoomRedraw,              ACTION_MENU::NORMAL, _( "Refresh" ) );
    viewMenu->Add( SCH_ACTIONS::showElectricalTypes, ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::showHiddenFields,    ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::togglePinAltIcons,   ACTION_MENU::CHECK );


    //-- Project menu -----------------------------------------------
    //
    ACTION_MENU* projectMenu = new ACTION_MENU( false, selTool );

    projectMenu->Add( ACTIONS::configurePaths );
    projectMenu->Add( ACTIONS::showSymbolLibTable );


    //-- Place menu -----------------------------------------------
    //
    ACTION_MENU* placeMenu = new ACTION_MENU( false, selTool );

    placeMenu->Add( SCH_ACTIONS::placeSymbolPin,    ACTION_MENU::NORMAL, _( "&Pin" ) );

    placeMenu->AppendSeparator();
    placeMenu->Add( SCH_ACTIONS::drawArc,           ACTION_MENU::NORMAL, _( "&Arc" ) );
    placeMenu->Add( SCH_ACTIONS::drawCircle,        ACTION_MENU::NORMAL, _( "F&ull Circle" ) );

    placeMenu->AppendSeparator();
    placeMenu->Add( SCH_ACTIONS::drawSymbolLines,   ACTION_MENU::NORMAL, _( "&Line" ) );
    placeMenu->Add( SCH_ACTIONS::drawRectangle,     ACTION_MENU::NORMAL, _( "&Rectangle" ) );

    placeMenu->AppendSeparator();
    placeMenu->Add( SCH_ACTIONS::drawSymbolPolygon, ACTION_MENU::NORMAL, _( "Pol&ygon" ) );
    placeMenu->Add( SCH_ACTIONS::drawBezier,        ACTION_MENU::NORMAL, _( "&Bezier" ) );
    placeMenu->Add( SCH_ACTIONS::placeSymbolText,   ACTION_MENU::NORMAL, _( "&Text String" ) );
    placeMenu->Add( SCH_ACTIONS::drawSymbolTextBox, ACTION_MENU::NORMAL, _( "Text &Frame" ) );

    placeMenu->AppendSeparator();
    placeMenu->Add( SCH_ACTIONS::importGraphics,    ACTION_MENU::NORMAL, _( "&Graphic..." ) );

    // KiCad-only
    placeMenu->AppendSeparator();
    placeMenu->Add( SCH_ACTIONS::placeSymbolAnchor, ACTION_MENU::NORMAL, _( "Symbol Anc&hor" ) );


    //-- Tools menu -----------------------------------------------
    //
    ACTION_MENU* toolsMenu = new ACTION_MENU( false, selTool );

    toolsMenu->Add( SCH_ACTIONS::newSymbol,    ACTION_MENU::NORMAL, _( "New &Component" ) );
    toolsMenu->Add( SCH_ACTIONS::deleteSymbol, ACTION_MENU::NORMAL, _( "&Remove Component" ) );

    toolsMenu->AppendSeparator();
    toolsMenu->Add( SCH_ACTIONS::copySymbol,   ACTION_MENU::NORMAL, _( "Cop&y Component" ) );

    toolsMenu->AppendSeparator();

    // Altium's display modes are KiCad's body styles (e.g. De Morgan)
    wxMenu* modeSubMenu = new wxMenu;
    modeSubMenu->Append( ID_LIBEDIT_MODE_PREVIOUS, _( "&Previous" ) );
    modeSubMenu->Append( ID_LIBEDIT_MODE_NEXT,     _( "&Next" ) );
    modeSubMenu->AppendSeparator();
    modeSubMenu->AppendCheckItem( ID_LIBEDIT_MODE_NORMAL, _( "N&ormal" ) );
    toolsMenu->AppendSubMenu( modeSubMenu, _( "&Mode" ) );

    toolsMenu->AppendSeparator();
    toolsMenu->Add( ACTIONS::openPreferences, ACTION_MENU::NORMAL, _( "&Preferences..." ) );

    // KiCad-only
    toolsMenu->AppendSeparator();
    toolsMenu->Add( SCH_ACTIONS::deriveFromExistingSymbol, ACTION_MENU::NORMAL,
                    _( "&Derive Component..." ) );
    toolsMenu->Add( SCH_ACTIONS::renameSymbol,     ACTION_MENU::NORMAL, _( "Ren&ame Component..." ) );
    toolsMenu->Add( SCH_ACTIONS::duplicateSymbol,  ACTION_MENU::NORMAL, _( "D&uplicate Component" ) );
    toolsMenu->Add( SCH_ACTIONS::symbolProperties, ACTION_MENU::NORMAL,
                    _( "Component Propert&ies..." ) );
    toolsMenu->Add( SCH_ACTIONS::pinTable,         ACTION_MENU::NORMAL, _( "Pin &Table..." ) );
    toolsMenu->Add( SCH_ACTIONS::previousUnit );
    toolsMenu->Add( SCH_ACTIONS::nextUnit );
    toolsMenu->Add( SCH_ACTIONS::updateSymbolFields );
    toolsMenu->Add( SCH_ACTIONS::toggleSyncedPinsMode, ACTION_MENU::CHECK );
    toolsMenu->Add( SCH_ACTIONS::addSymbolToSchematic, ACTION_MENU::NORMAL,
                    _( "Place in &Schematic" ) );
    AddMenuLanguageList( toolsMenu, selTool );


    //-- Reports menu -----------------------------------------------
    //
    ACTION_MENU* reportsMenu = new ACTION_MENU( false, selTool );

    reportsMenu->Add( SCH_ACTIONS::checkSymbol, ACTION_MENU::NORMAL, _( "Component &Rule Check..." ) );

    // KiCad-only
    reportsMenu->AppendSeparator();
    reportsMenu->Add( ACTIONS::showDatasheet,   ACTION_MENU::NORMAL, _( "&Datasheet" ) );


    //-- Menubar -------------------------------------------------------------
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
