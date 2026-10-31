/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2018 Jean-Pierre Charras, jp.charras at wanadoo.fr
 * Copyright (C) 2009 Wayne Stambaugh <stambaughw@gmail.com>
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 * Copyright (C) 2019 CERN
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
#include <file_history.h>
#include <kiface_base.h>
#include <schematic.h>
#include <tool/action_manager.h>
#include <tool/action_menu.h>
#include <tool/tool_manager.h>
#include <tools/sch_selection_tool.h>
#include <tools/sch_actions.h>
#include <tools/sch_outputs_tool.h>
#include <tools/sch_project_tool.h>
#include <widgets/window_menu.h>
#include "eeschema_id.h"
#include "sch_edit_frame.h"
#include <widgets/wx_menubar.h>
#include <widgets/panel_remote_symbol.h>
#include <advanced_config.h>


void SCH_EDIT_FRAME::doReCreateMenuBar()
{
    SCH_SELECTION_TOOL* selTool = m_toolManager->GetTool<SCH_SELECTION_TOOL>();

    // wxWidgets handles the Mac Application menu behind the scenes, but that means
    // we always have to start from scratch with a new wxMenuBar.
    wxMenuBar*  oldMenuBar = GetMenuBar();
    WX_MENUBAR* menuBar    = new WX_MENUBAR();

    //-- File menu -----------------------------------------------------------
    //
    ACTION_MENU*   fileMenu = new ACTION_MENU( false, selTool );
    static ACTION_MENU* openRecentMenu;

    if( Kiface().IsSingle() )   // When not under a project mgr
    {
        FILE_HISTORY& fileHistory = GetFileHistory();

        // Add this menu to the list of menus managed by the file history
        // (the file history will be updated when adding/removing files in history)
        if( !openRecentMenu )
        {
            openRecentMenu = new ACTION_MENU( false, selTool );
            openRecentMenu->SetIcon( BITMAPS::recent );

            fileHistory.UseMenu( openRecentMenu );
            fileHistory.AddFilesToMenu( openRecentMenu );
        }

        // Ensure the title is up to date after changing language
        openRecentMenu->SetTitle( _( "Open Recent" ) );
        fileHistory.UpdateClearText( openRecentMenu, _( "Clear Recent Files" ) );

        fileMenu->Add( ACTIONS::doNew );
        fileMenu->Add( ACTIONS::open );

        wxMenuItem* item = fileMenu->Add( openRecentMenu->Clone() );

        // Add the file menu condition here since it needs the item ID for the submenu
        ACTION_CONDITIONS cond;
        cond.Enable( FILE_HISTORY::FileHistoryNotEmpty( fileHistory ) );
        RegisterUIUpdateHandler( item->GetId(), cond );
        fileMenu->AppendSeparator();
    }

    if( !Kiface().IsSingle() )
    {
        // Altium: File > New > Output Job File
        ACTION_MENU* submenuNew = new ACTION_MENU( false, selTool );
        submenuNew->SetTitle( _( "&New" ) );
        submenuNew->SetIcon( BITMAPS::new_generic );
        submenuNew->Add( SCH_OUTPUTS_ACTIONS::newOutputJob );
        fileMenu->Add( submenuNew );
        fileMenu->AppendSeparator();
    }

    fileMenu->Add( ACTIONS::save );

    if( Kiface().IsSingle() )
        fileMenu->Add( ACTIONS::saveAs );
    else
        fileMenu->Add( SCH_ACTIONS::saveCurrSheetCopyAs );

    fileMenu->Add( ACTIONS::revert );

    fileMenu->AppendSeparator();

    // Import submenu
    ACTION_MENU* submenuImport = new ACTION_MENU( false, selTool );
    submenuImport->SetTitle( _( "Import" ) );
    submenuImport->SetIcon( BITMAPS::import );

    submenuImport->Add( SCH_ACTIONS::importNonKicadSchematic, ACTION_MENU::NORMAL, _( "Non-KiCad Schematic..." ) );
    submenuImport->Add( SCH_ACTIONS::importFPAssignments, ACTION_MENU::NORMAL, _( "Footprint Assignments..." ) );
    submenuImport->Add( SCH_ACTIONS::importGraphics, ACTION_MENU::NORMAL, _( "Graphics..." ) );

    fileMenu->Add( submenuImport );


    // Export submenu
    ACTION_MENU* submenuExport = new ACTION_MENU( false, selTool );
    submenuExport->SetTitle( _( "Export" ) );
    submenuExport->SetIcon( BITMAPS::export_file );
    submenuExport->Add( SCH_ACTIONS::drawSheetOnClipboard,   ACTION_MENU::NORMAL, _( "Drawing to Clipboard" ) );
    submenuExport->Add( SCH_ACTIONS::exportNetlist,          ACTION_MENU::NORMAL, _( "Netlist..." ) );
    submenuExport->Add( SCH_ACTIONS::exportSymbolsToLibrary, ACTION_MENU::NORMAL, _( "Symbols..." ) );
    fileMenu->Add( submenuExport );

    fileMenu->AppendSeparator();
    fileMenu->Add( ACTIONS::pageSettings );
    fileMenu->Add( ACTIONS::print );
    fileMenu->Add( ACTIONS::plot );

    fileMenu->AppendSeparator();
    fileMenu->AddQuitOrClose( &Kiface(), _( "Schematic Editor" ) );


    // The remaining menus follow Altium Designer's schematic editor layout:
    // Edit, View, Project, Place, Design, Tools, Simulate, Reports.

    //-- Edit menu -----------------------------------------------------------
    //
    ACTION_MENU* editMenu = new ACTION_MENU( false, selTool );

    editMenu->Add( ACTIONS::undo );
    editMenu->Add( ACTIONS::redo );

    editMenu->AppendSeparator();
    editMenu->Add( ACTIONS::cut );
    editMenu->Add( ACTIONS::copy );
    editMenu->Add( ACTIONS::copyAsText );
    editMenu->Add( ACTIONS::paste );
    // Altium: Smart Paste (Shift+Ctrl+V) is KiCad's Paste Special
    editMenu->Add( SCH_PROJECT_ACTIONS::smartPaste, ACTION_MENU::NORMAL, _( "Smart Paste..." ) );
    editMenu->Add( ACTIONS::duplicate, ACTION_MENU::NORMAL, _( "&Rubber Stamp" ) );
    editMenu->Add( ACTIONS::doDelete, ACTION_MENU::NORMAL, _( "&Delete" ) );

    editMenu->AppendSeparator();

    ACTION_MENU* selectSubMenu = new ACTION_MENU( false, selTool );
    selectSubMenu->SetTitle( _( "&Select" ) );
    selectSubMenu->Add( ACTIONS::selectAll );
    selectSubMenu->Add( ACTIONS::unselectAll );
    selectSubMenu->AppendSeparator();
    selectSubMenu->Add( SCH_ACTIONS::selectNode );
    selectSubMenu->Add( SCH_ACTIONS::selectConnection );
    editMenu->Add( selectSubMenu );

    editMenu->Add( ACTIONS::find, ACTION_MENU::NORMAL, _( "&Find Text..." ) );
    editMenu->Add( ACTIONS::findAndReplace );
    editMenu->Add( SCH_ACTIONS::findSimilar, ACTION_MENU::NORMAL, _( "Find Similar &Objects..." ) );

    ACTION_MENU* selMemorySubMenu = new ACTION_MENU( false, selTool );
    selMemorySubMenu->SetTitle( _( "Selection &Memory" ) );
    selMemorySubMenu->Add( SCH_ACTIONS::selectionMemoryStore1 );
    selMemorySubMenu->Add( SCH_ACTIONS::selectionMemoryStore2 );
    selMemorySubMenu->Add( SCH_ACTIONS::selectionMemoryStore3 );
    selMemorySubMenu->Add( SCH_ACTIONS::selectionMemoryStore4 );
    selMemorySubMenu->Add( SCH_ACTIONS::selectionMemoryStore5 );
    selMemorySubMenu->Add( SCH_ACTIONS::selectionMemoryStore6 );
    selMemorySubMenu->Add( SCH_ACTIONS::selectionMemoryStore7 );
    selMemorySubMenu->Add( SCH_ACTIONS::selectionMemoryStore8 );
    selMemorySubMenu->AppendSeparator();
    selMemorySubMenu->Add( SCH_ACTIONS::selectionMemoryRecall1 );
    selMemorySubMenu->Add( SCH_ACTIONS::selectionMemoryRecall2 );
    selMemorySubMenu->Add( SCH_ACTIONS::selectionMemoryRecall3 );
    selMemorySubMenu->Add( SCH_ACTIONS::selectionMemoryRecall4 );
    selMemorySubMenu->Add( SCH_ACTIONS::selectionMemoryRecall5 );
    selMemorySubMenu->Add( SCH_ACTIONS::selectionMemoryRecall6 );
    selMemorySubMenu->Add( SCH_ACTIONS::selectionMemoryRecall7 );
    selMemorySubMenu->Add( SCH_ACTIONS::selectionMemoryRecall8 );
    editMenu->Add( selMemorySubMenu );

    editMenu->AppendSeparator();

    ACTION_MENU* moveSubMenu = new ACTION_MENU( false, selTool );
    moveSubMenu->SetTitle( _( "&Move" ) );
    moveSubMenu->SetIcon( BITMAPS::move );
    moveSubMenu->Add( SCH_ACTIONS::move );
    moveSubMenu->Add( SCH_ACTIONS::drag );
    moveSubMenu->AppendSeparator();
    moveSubMenu->Add( SCH_ACTIONS::rotateCCW );
    moveSubMenu->Add( SCH_ACTIONS::rotateCW );
    moveSubMenu->Add( SCH_ACTIONS::mirrorH );
    moveSubMenu->Add( SCH_ACTIONS::mirrorV );
    moveSubMenu->Add( SCH_ACTIONS::swap );
    editMenu->Add( moveSubMenu );

    ACTION_MENU* alignSubMenu = new ACTION_MENU( false, selTool );
    alignSubMenu->SetTitle( _( "&Align" ) );
    alignSubMenu->SetIcon( BITMAPS::align_items );
    alignSubMenu->Add( SCH_ACTIONS::alignLeft );
    alignSubMenu->Add( SCH_ACTIONS::alignRight );
    alignSubMenu->Add( SCH_ACTIONS::alignTop );
    alignSubMenu->Add( SCH_ACTIONS::alignBottom );
    alignSubMenu->Add( SCH_ACTIONS::alignCenterX );
    alignSubMenu->Add( SCH_ACTIONS::alignCenterY );
    alignSubMenu->AppendSeparator();
    alignSubMenu->Add( SCH_ACTIONS::alignToGrid );
    editMenu->Add( alignSubMenu );

    editMenu->AppendSeparator();
    editMenu->Add( ACTIONS::group );
    editMenu->Add( ACTIONS::ungroup );

    editMenu->AppendSeparator();
    editMenu->Add( SCH_ACTIONS::editTextAndGraphics );
    editMenu->Add( SCH_ACTIONS::changeSymbols );
    editMenu->Add( SCH_ACTIONS::editPageNumber );

    ACTION_MENU* submenuAttributes = new ACTION_MENU( false, selTool );
    submenuAttributes->SetTitle( _( "Attributes" ) );

    submenuAttributes->Add( SCH_ACTIONS::setExcludeFromSim,    ACTION_MENU::CHECK );
    submenuAttributes->Add( SCH_ACTIONS::setExcludeFromBOM,    ACTION_MENU::CHECK );
    submenuAttributes->Add( SCH_ACTIONS::setExcludeFromBoard,  ACTION_MENU::CHECK );
    submenuAttributes->Add( SCH_ACTIONS::setExcludeFromPosFiles, ACTION_MENU::CHECK );
    submenuAttributes->Add( SCH_ACTIONS::setDNP,               ACTION_MENU::CHECK );

    editMenu->Add( submenuAttributes );

    editMenu->AppendSeparator();
    editMenu->Add( ACTIONS::deleteTool );

    //-- View menu -----------------------------------------------------------
    //
    ACTION_MENU* viewMenu = new ACTION_MENU( false, selTool );

    viewMenu->Add( ACTIONS::zoomFitScreen, ACTION_MENU::NORMAL, _( "Fit &Document" ) );
    viewMenu->Add( ACTIONS::zoomFitObjects, ACTION_MENU::NORMAL, _( "&Fit All Objects" ) );
    viewMenu->Add( ACTIONS::zoomFitSelection, ACTION_MENU::NORMAL, _( "Fit &Selected" ) );
    viewMenu->Add( ACTIONS::zoomTool, ACTION_MENU::NORMAL, _( "&Area" ) );
    viewMenu->Add( ACTIONS::zoomInCenter );
    viewMenu->Add( ACTIONS::zoomOutCenter );
    viewMenu->Add( ACTIONS::zoomRedraw,       ACTION_MENU::NORMAL, _( "Refresh" ) );

    viewMenu->AppendSeparator();

    // Show / Hide Panels submenu
    ACTION_MENU* showHidePanels = new ACTION_MENU( false, selTool );
    showHidePanels->SetTitle( _( "Panels" ) );

    showHidePanels->Add( ACTIONS::showProperties,    ACTION_MENU::CHECK );
    showHidePanels->Add( SCH_ACTIONS::showHierarchy, ACTION_MENU::CHECK, _( "Navigator" ) );
    showHidePanels->Add( ACTIONS::showSearch,        ACTION_MENU::CHECK );
    showHidePanels->Add( SCH_ACTIONS::showSchList,   ACTION_MENU::CHECK );
    showHidePanels->Add( ACTIONS::showMessages,      ACTION_MENU::CHECK );

    showHidePanels->Add( SCH_ACTIONS::showDesignBlockPanel, ACTION_MENU::CHECK, _( "Design Blocks" ) );
    wxMenuItem* remoteSymbolItem = showHidePanels->Add( SCH_ACTIONS::showRemoteSymbolPanel, ACTION_MENU::CHECK, _( "Remote Symbols" ) );

    if( m_remoteSymbolPane && !m_remoteSymbolPane->HasDataSources() )
    {
        remoteSymbolItem->Enable( false );
        remoteSymbolItem->SetHelp( _( "Search signed-in remote symbol providers and download verified libraries." ) );
    }

    viewMenu->Add( showHidePanels );
    viewMenu->Add( ACTIONS::showSymbolBrowser );

    viewMenu->AppendSeparator();
    ACTION_MENU* gridsSubMenu = new ACTION_MENU( false, selTool );
    gridsSubMenu->SetTitle( _( "&Grids" ) );
    gridsSubMenu->SetIcon( BITMAPS::grid );
    gridsSubMenu->Add( ACTIONS::toggleGrid, ACTION_MENU::CHECK );
    gridsSubMenu->Add( ACTIONS::gridProperties );
    viewMenu->Add( gridsSubMenu );
    viewMenu->Add( ACTIONS::toggleUnits, ACTION_MENU::NORMAL, _( "Toggle &Units" ) );

    viewMenu->AppendSeparator();
    viewMenu->Add( SCH_ACTIONS::navigateBack );
    viewMenu->Add( SCH_ACTIONS::navigateUp );
    viewMenu->Add( SCH_ACTIONS::navigateForward );
    viewMenu->Add( SCH_ACTIONS::navigatePrevious );
    viewMenu->Add( SCH_ACTIONS::navigateNext );

    viewMenu->AppendSeparator();
    viewMenu->Add( SCH_ACTIONS::toggleHiddenPins,      ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::toggleHiddenFields,    ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::toggleDirectiveLabels, ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::toggleERCErrors,       ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::toggleERCWarnings,     ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::toggleERCExclusions,   ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::markSimExclusions,     ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::toggleOPVoltages,      ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::toggleOPCurrents,      ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::togglePinAltIcons,     ACTION_MENU::CHECK );

#ifdef __APPLE__
    viewMenu->AppendSeparator();
#endif

    //-- Project menu --------------------------------------------------------
    //
    ACTION_MENU* projectMenu = new ACTION_MENU( false, selTool );

    // Altium: Validate PCB Project <name> (ERC plus schematic / PCB parity into Messages)
    projectMenu->Add( SCH_PROJECT_ACTIONS::validateProject, ACTION_MENU::NORMAL,
                      SCH_PROJECT_TOOL::ValidateLabel( this ) );
    projectMenu->AppendSeparator();

    if( !Kiface().IsSingle() )
    {
        projectMenu->Add( ACTIONS::showProjectManager, ACTION_MENU::NORMAL,
                          _( "Project Documents..." ) );
        projectMenu->AppendSeparator();
    }

    projectMenu->Add( SCH_OUTPUTS_ACTIONS::variantManager, ACTION_MENU::NORMAL, _( "&Variants..." ) );

    projectMenu->AppendSeparator();
    projectMenu->Add( SCH_OUTPUTS_ACTIONS::openOutputJob, ACTION_MENU::NORMAL, _( "Output &Jobs..." ) );

    projectMenu->AppendSeparator();
    projectMenu->Add( SCH_ACTIONS::schematicSetup, ACTION_MENU::NORMAL, _( "Project &Options..." ) );

    // KiCad-only
    projectMenu->AppendSeparator();
    projectMenu->Add( SCH_ACTIONS::showPcbNew );
    projectMenu->Add( ACTIONS::configurePaths );
    projectMenu->Add( ACTIONS::showSymbolLibTable );
    projectMenu->Add( ACTIONS::showDesignBlockLibTable );

    ACTION_MENU* submenuVariants = new ACTION_MENU( false, selTool );
    submenuVariants->SetTitle( _( "Variant Commands" ) );
    submenuVariants->Add( SCH_ACTIONS::addVariant );
    submenuVariants->Add( SCH_ACTIONS::removeVariant );
    submenuVariants->Add( SCH_ACTIONS::editVariantDescription );
    projectMenu->Add( submenuVariants );

    //-- Place menu -----------------------------------------------------------
    //
    ACTION_MENU* placeMenu = new ACTION_MENU( false, selTool );

    // Altium's order: connectivity objects, then directives, then text and drawing tools
    placeMenu->Add( SCH_ACTIONS::drawBus, ACTION_MENU::NORMAL, _( "&Bus" ) );
    placeMenu->Add( SCH_ACTIONS::placeBusWireEntry, ACTION_MENU::NORMAL, _( "B&us Entry" ) );
    placeMenu->Add( SCH_ACTIONS::placeSymbol, ACTION_MENU::NORMAL, _( "&Part..." ) );
    placeMenu->Add( SCH_ACTIONS::placePower, ACTION_MENU::NORMAL, _( "P&ower Port" ) );
    placeMenu->Add( SCH_ACTIONS::drawWire, ACTION_MENU::NORMAL, _( "&Wire" ) );
    placeMenu->Add( SCH_ACTIONS::placeLabel, ACTION_MENU::NORMAL, _( "&Net Label" ) );
    placeMenu->Add( SCH_ACTIONS::placeHierLabel, ACTION_MENU::NORMAL, _( "Po&rt" ) );
    placeMenu->Add( SCH_ACTIONS::placeGlobalLabel, ACTION_MENU::NORMAL, _( "Off Sheet &Connector" ) );
    placeMenu->Add( SCH_ACTIONS::drawSheet, ACTION_MENU::NORMAL, _( "&Sheet Symbol" ) );
    placeMenu->Add( SCH_ACTIONS::placeSheetPin, ACTION_MENU::NORMAL, _( "Sheet &Entry" ) );

    ACTION_MENU* directivesSubMenu = new ACTION_MENU( false, selTool );
    directivesSubMenu->SetTitle( _( "Directi&ves" ) );
    directivesSubMenu->SetIcon( BITMAPS::noconn );
    directivesSubMenu->Add( SCH_ACTIONS::placeNoConnect, ACTION_MENU::NORMAL, _( "Generic &No ERC" ) );
    directivesSubMenu->Add( SCH_ACTIONS::placeClassLabel );
    directivesSubMenu->Add( SCH_ACTIONS::drawRuleArea );
    placeMenu->Add( directivesSubMenu );

    placeMenu->AppendSeparator();
    placeMenu->Add( SCH_ACTIONS::placeSchematicText, ACTION_MENU::NORMAL, _( "&Text String" ) );
    placeMenu->Add( SCH_ACTIONS::drawTextBox, ACTION_MENU::NORMAL, _( "Text &Frame" ) );

    ACTION_MENU* drawingSubMenu = new ACTION_MENU( false, selTool );
    drawingSubMenu->SetTitle( _( "&Drawing Tools" ) );
    drawingSubMenu->SetIcon( BITMAPS::add_graphical_segments );
    drawingSubMenu->Add( SCH_ACTIONS::drawArc );
    drawingSubMenu->Add( SCH_ACTIONS::drawCircle );
    drawingSubMenu->Add( SCH_ACTIONS::drawLines );
    drawingSubMenu->Add( SCH_ACTIONS::drawRectangle );
    drawingSubMenu->Add( SCH_ACTIONS::drawBezier );
    drawingSubMenu->Add( SCH_ACTIONS::placeImage );
    placeMenu->Add( drawingSubMenu );

    // KiCad-only extras, kept apart at the end
    placeMenu->AppendSeparator();
    placeMenu->Add( SCH_ACTIONS::placeJunction, ACTION_MENU::NORMAL, _( "&Junction" ) );
    placeMenu->Add( SCH_ACTIONS::syncAllSheetsPins );
    placeMenu->Add( SCH_ACTIONS::importSheet );
    placeMenu->Add( SCH_ACTIONS::drawTable );

    //-- Design menu -----------------------------------------------------------
    //
    ACTION_MENU* designMenu = new ACTION_MENU( false, selTool );

    // Altium names the board: Update PCB Document <name>.  It opens the change list (Altium's
    // Engineering Change Order).
    designMenu->Add( ACTIONS::updatePcbFromSchematic, ACTION_MENU::NORMAL,
                     SCH_PROJECT_TOOL::UpdatePcbLabel( this ) )->Enable( !Kiface().IsSingle() );
    designMenu->Add( ACTIONS::updateSchematicFromPcb, ACTION_MENU::NORMAL,
                     _( "&Import Changes from PCB..." ) )->Enable( !Kiface().IsSingle() );

    designMenu->AppendSeparator();
    designMenu->Add( SCH_ACTIONS::exportNetlist, ACTION_MENU::NORMAL, _( "&Netlist for Project..." ) );
    designMenu->Add( SCH_ACTIONS::exportSymbolsToLibrary, ACTION_MENU::NORMAL, _( "&Make Schematic Library..." ) );

    designMenu->AppendSeparator();
    designMenu->Add( ACTIONS::pageSettings, ACTION_MENU::NORMAL, _( "Document &Options..." ) );

    //-- Tools menu -----------------------------------------------
    //
    ACTION_MENU* toolsMenu = new ACTION_MENU( false, selTool );

    // Altium's order: Up/Down Hierarchy | Parameter Manager, Footprint Manager, Update From
    // Libraries | Annotation | Signal Integrity | Convert | Cross Probe, Select PCB Components
    toolsMenu->Add( SCH_ACTIONS::upDownHierarchy, ACTION_MENU::NORMAL, _( "Up/Down &Hierarchy" ) );
    toolsMenu->AppendSeparator();
    toolsMenu->Add( SCH_ACTIONS::editSymbolFields, ACTION_MENU::NORMAL, _( "Pa&rameter Manager..." ) );
    toolsMenu->Add( SCH_ACTIONS::assignFootprints, ACTION_MENU::NORMAL, _( "Footprint Mana&ger..." ) );
    toolsMenu->Add( SCH_ACTIONS::updateSymbols, ACTION_MENU::NORMAL, _( "Update From &Libraries..." ) );
    toolsMenu->Add( SCH_ACTIONS::updateSymbol, ACTION_MENU::NORMAL,
                    _( "Update Selected From &Libraries..." ) );

    toolsMenu->AppendSeparator();
    ACTION_MENU* annotationSubMenu = new ACTION_MENU( false, selTool );
    annotationSubMenu->SetTitle( _( "&Annotation" ) );
    annotationSubMenu->SetIcon( BITMAPS::annotate );
    annotationSubMenu->Add( SCH_ACTIONS::annotate, ACTION_MENU::NORMAL, _( "&Annotate Schematics..." ) );
    annotationSubMenu->Add( SCH_ACTIONS::resetDesignators, ACTION_MENU::NORMAL,
                            _( "R&eset Schematic Designators..." ) );
    annotationSubMenu->Add( SCH_ACTIONS::resetDuplicateDesignators, ACTION_MENU::NORMAL,
                            _( "Reset Dupli&cate Schematic Designators..." ) );
    annotationSubMenu->Add( SCH_ACTIONS::annotateQuietly, ACTION_MENU::NORMAL,
                            _( "Annotate Schematics &Quietly..." ) );
    annotationSubMenu->Add( SCH_ACTIONS::forceAnnotateAll, ACTION_MENU::NORMAL,
                            _( "Force A&nnotate All Schematics..." ) );
    annotationSubMenu->Add( SCH_ACTIONS::backAnnotateSchematics, ACTION_MENU::NORMAL,
                            _( "&Back Annotate Schematics..." ) );
    annotationSubMenu->Add( SCH_ACTIONS::numberSchematicSheets, ACTION_MENU::NORMAL,
                            _( "Number Schematic Shee&ts..." ) );
    annotationSubMenu->AppendSeparator();
    annotationSubMenu->Add( SCH_ACTIONS::boardLevelAnnotate, ACTION_MENU::NORMAL,
                            _( "Board Level Annotate..." ) );
    annotationSubMenu->Add( SCH_ACTIONS::annotateCompiledSheets, ACTION_MENU::NORMAL,
                            _( "Annotate Co&mpiled Sheets..." ) );
    annotationSubMenu->AppendSeparator();
    annotationSubMenu->Add( SCH_ACTIONS::incrementAnnotations );
    toolsMenu->Add( annotationSubMenu );

    // Signal integrity analysis has no KiCad counterpart
    toolsMenu->AppendSeparator();
    toolsMenu->Append( wxID_ANY, _( "Signal Integrity..." ) )->Enable( false );

    toolsMenu->AppendSeparator();
    ACTION_MENU* convertSubMenu = new ACTION_MENU( false, selTool );
    convertSubMenu->SetTitle( _( "Con&vert" ) );
    convertSubMenu->Append( wxID_ANY, _( "Push Part to Sheet" ) )->Enable( false );
    convertSubMenu->Add( SCH_ACTIONS::convertPartToSheetSymbol, ACTION_MENU::NORMAL,
                         _( "Convert &Part To Sheet Symbol" ) );
    convertSubMenu->Add( SCH_ACTIONS::convertPartToPorts, ACTION_MENU::NORMAL,
                         _( "Convert Part To P&orts" ) );
    convertSubMenu->AppendSeparator();
    // Altium's unions are KiCad's groups
    convertSubMenu->Add( ACTIONS::group, ACTION_MENU::NORMAL, _( "Create Union from selected objects" ) );
    convertSubMenu->Add( ACTIONS::removeFromGroup, ACTION_MENU::NORMAL, _( "Break objects from Union" ) );
    convertSubMenu->Add( ACTIONS::ungroup, ACTION_MENU::NORMAL, _( "Break all objects Unions" ) );
    convertSubMenu->AppendSeparator();
    convertSubMenu->Append( wxID_ANY, _( "Convert Cross Junctions" ) )->Enable( false );
    convertSubMenu->AppendSeparator();
    convertSubMenu->Append( wxID_ANY, _( "Round coordinates of objects..." ) )->Enable( false );
    toolsMenu->Add( convertSubMenu );

    toolsMenu->AppendSeparator();
    toolsMenu->Add( SCH_ACTIONS::selectOnPCB, ACTION_MENU::NORMAL, _( "&Cross Probe" ) );
    toolsMenu->Add( SCH_ACTIONS::selectPcbComponents, ACTION_MENU::NORMAL,
                    _( "&Select PCB Components" ) )->Enable( !Kiface().IsSingle() );

    // KiCad-only
    toolsMenu->AppendSeparator();
    toolsMenu->Add( SCH_ACTIONS::editSymbolLibraryLinks );
    toolsMenu->Add( ACTIONS::showSymbolEditor );
    toolsMenu->Add( ACTIONS::showFootprintEditor );
    toolsMenu->Add( SCH_ACTIONS::rescueSymbols );
    toolsMenu->Add( SCH_ACTIONS::remapSymbols );

    toolsMenu->AppendSeparator();
    toolsMenu->Add( SCH_ACTIONS::runERC );
    toolsMenu->Add( ACTIONS::prevMarker );
    toolsMenu->Add( ACTIONS::nextMarker );
    toolsMenu->Add( ACTIONS::excludeMarker );

    toolsMenu->AppendSeparator();
    toolsMenu->Add( SCH_ACTIONS::showBusSyntaxHelp );
    toolsMenu->Add( ACTIONS::showCalculatorTools );

#ifdef KICAD_IPC_API
    toolsMenu->Add( ACTIONS::pluginsReload );
#endif

    toolsMenu->AppendSeparator();
    toolsMenu->Add( ACTIONS::openPreferences, ACTION_MENU::NORMAL, _( "&Preferences..." ) );
    AddMenuLanguageList( toolsMenu, selTool );

    //-- Simulate menu -----------------------------------------------
    //
    ACTION_MENU* simulateMenu = new ACTION_MENU( false, selTool );

    simulateMenu->Add( SCH_ACTIONS::showSimulator );
    simulateMenu->Add( SCH_ACTIONS::markSimExclusions, ACTION_MENU::CHECK );
    simulateMenu->Add( SCH_ACTIONS::toggleOPVoltages,  ACTION_MENU::CHECK );
    simulateMenu->Add( SCH_ACTIONS::toggleOPCurrents,  ACTION_MENU::CHECK );

    //-- Reports menu -----------------------------------------------
    //
    ACTION_MENU* reportsMenu = new ACTION_MENU( false, selTool );

    reportsMenu->Add( SCH_OUTPUTS_ACTIONS::activeBom, ACTION_MENU::NORMAL, _( "&Bill of Materials..." ) );
    reportsMenu->Add( SCH_ACTIONS::generateBOM, ACTION_MENU::NORMAL, _( "&Export Bill of Materials..." ) );
    reportsMenu->Add( SCH_ACTIONS::generateBOMLegacy );
    reportsMenu->Add( SCH_ACTIONS::diffSymbol );


    //-- Menubar -------------------------------------------------------------
    //
    menuBar->Append( fileMenu,     _( "&File" ) );
    menuBar->Append( editMenu,     _( "&Edit" ) );
    menuBar->Append( viewMenu,     _( "&View" ) );
    menuBar->Append( projectMenu,  _( "Proje&ct" ) );
    menuBar->Append( placeMenu,    _( "&Place" ) );
    menuBar->Append( designMenu,   _( "&Design" ) );
    menuBar->Append( toolsMenu,    _( "&Tools" ) );
    menuBar->Append( simulateMenu, _( "Simulate" ) );
    menuBar->Append( reportsMenu,  _( "&Reports" ) );
    menuBar->Append( new WINDOW_MENU( this ), _( "&Window" ) );
    AddStandardHelpMenu( menuBar );

    SetMenuBar( menuBar );
    delete oldMenuBar;
}
