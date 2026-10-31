/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2017 Jean-Pierre Charras, jp.charras at wanadoo.fr
 * Copyright (C) 2012 SoftPLC Corporation, Dick Hollenbeck <dick@softplc.com>
 * Copyright (C) 2012 Wayne Stambaugh <stambaughw@gmail.com>
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
#include <project.h>
#include <wx/filename.h>
#include <map>
#include <bitmaps.h>
#include <file_history.h>
#include <kiface_base.h>
#include <pcb_edit_frame.h>
#include <pcbnew_id.h>
#include <python_scripting.h>
#include <tool/action_manager.h>
#include <tool/actions.h>
#include <tool/tool_manager.h>
#include <tools/pcb_actions.h>
#include <tools/pcb_selection_tool.h>
#include <board.h>
#include <zone.h>
#include <tools/pcb_swap_room_tool.h>
#include <tools/pcb_outputs_tool.h>
#include <tools/pcb_project_tool.h>
#include <widgets/window_menu.h>
#include <widgets/wx_menubar.h>


/**
 * Tools > Polygon Pours as Altium shows it: icons on the first three items only, no shortcut
 * text, and live counts ("Shelve 10 Polygon(s)", "Restore 0 Shelved Polygon(s)") refreshed each
 * time the menu opens.  Items carry their own ids (not the actions' ids) so the menu shows no
 * accelerators; choosing one runs the action.
 */
class POLYGON_POURS_MENU : public ACTION_MENU
{
public:
    POLYGON_POURS_MENU( PCB_EDIT_FRAME* aFrame, TOOL_INTERACTIVE* aTool ) :
            ACTION_MENU( false, aTool ),
            m_frame( aFrame )
    {
        SetTitle( _( "Poly&gon Pours" ) );
        SetIcon( BITMAPS::add_zone );
        build();
    }

protected:
    ACTION_MENU* create() const override { return new POLYGON_POURS_MENU( m_frame, m_tool ); }

    void update() override
    {
        int filled = 0;
        int shelved = 0;

        if( BOARD* board = m_frame->GetBoard() )
        {
            for( ZONE* zone : board->Zones() )
            {
                if( zone->GetIsRuleArea() )
                    continue;

                if( zone->IsFilled() )
                    filled++;
                else
                    shelved++;
            }
        }

        // The accelerator letters stay put: S&helve, R&estore
        SetLabel( ID_SHELVE, wxString::Format( _( "S&helve %d Polygon(s)" ), filled ) );
        SetLabel( ID_RESTORE, wxString::Format( _( "R&estore %d Shelved Polygon(s)" ), shelved ) );
        Enable( ID_SHELVE, filled > 0 );
        Enable( ID_RESTORE, shelved > 0 );
    }

    OPT_TOOL_EVENT eventHandler( const wxMenuEvent& aEvent ) override
    {
        switch( aEvent.GetId() )
        {
        case ID_MANAGER:   return PCB_ACTIONS::zonesManager.MakeEvent();
        case ID_SHELVE:    return PCB_ACTIONS::zoneUnfillAll.MakeEvent();
        case ID_RESTORE:   return PCB_ACTIONS::zoneRestoreShelved.MakeEvent();
        case ID_SELECTED:  return PCB_ACTIONS::zoneFill.MakeEvent();
        case ID_ALL:       return PCB_ACTIONS::zoneFillAll.MakeEvent();
        case ID_VIOLATING: return PCB_ACTIONS::zoneRepourViolating.MakeEvent();
        case ID_MODIFIED:  return PCB_ACTIONS::zoneFillDirty.MakeEvent();
        default:           return OPT_TOOL_EVENT();
        }
    }

private:
    enum
    {
        ID_MANAGER = wxID_HIGHEST + 7300,
        ID_SHELVE,
        ID_RESTORE,
        ID_SELECTED,
        ID_ALL,
        ID_VIOLATING,
        ID_MODIFIED
    };

    void build()
    {
        Add( _( "Polygon &Manager" ), wxEmptyString, ID_MANAGER, BITMAPS::show_zone );
        AppendSeparator();
        Add( _( "S&helve Polygon(s)" ), wxEmptyString, ID_SHELVE, BITMAPS::zone_unfill );
        Add( _( "R&estore Shelved Polygon(s)" ), wxEmptyString, ID_RESTORE, BITMAPS::fill_zone );
        AppendSeparator();
        Append( ID_SELECTED, _( "&Repour Selected" ) );
        Append( ID_ALL, _( "Repour &All" ) );
        Append( ID_VIOLATING, _( "Repour &Violating Polygons" ) );
        Append( ID_MODIFIED, _( "Repour M&odified" ) );
    }

    PCB_EDIT_FRAME* m_frame;
};


/**
 * A submenu that can list actions without their shortcut text, as Altium does where an item
 * has no shortcut of its own (the action keeps its hotkey elsewhere).  Such items are taken out
 * of the hotkey bookkeeping and dispatched by eventHandler(); their enable/check state still
 * follows the action's conditions, as the item keeps the action's id.
 */
class ALTIUM_SUBMENU : public ACTION_MENU
{
public:
    ALTIUM_SUBMENU( TOOL_INTERACTIVE* aTool ) :
            ACTION_MENU( false, aTool )
    {
    }

    wxMenuItem* AddPlain( const TOOL_ACTION& aAction, const wxString& aLabel,
                          bool aCheck = ACTION_MENU::NORMAL )
    {
        wxMenuItem* item = Add( aAction, aCheck, aLabel );
        m_toolActions.erase( aAction.GetUIId() );
        m_plain[aAction.GetUIId()] = &aAction;
        return item;
    }

protected:
    ACTION_MENU* create() const override
    {
        ALTIUM_SUBMENU* menu = new ALTIUM_SUBMENU( m_tool );
        menu->m_plain = m_plain;
        return menu;
    }

    OPT_TOOL_EVENT eventHandler( const wxMenuEvent& aEvent ) override
    {
        auto it = m_plain.find( aEvent.GetId() );

        if( it == m_plain.end() )
            return OPT_TOOL_EVENT();

        return it->second->MakeEvent();
    }

private:
    std::map<int, const TOOL_ACTION*> m_plain;
};


// Altium's Edit submenus, shared with the S / X / M / A / J popup chords
// (PCB_EDIT_FRAME::OpenChordPopup) so the menu and the chord always list the same items.
// Accelerator letters follow Altium's and are unique within each submenu.

ACTION_MENU* BuildPcbSelectMenu( TOOL_INTERACTIVE* aTool )
{
    ALTIUM_SUBMENU* menu = new ALTIUM_SUBMENU( aTool );
    menu->Add( ACTIONS::selectSetLasso,            ACTION_MENU::NORMAL, _( "Lasso S&elect" ) );
    menu->Add( PCB_ACTIONS::selectInsideArea,      ACTION_MENU::NORMAL, _( "&Inside Area" ) );
    menu->Add( PCB_ACTIONS::selectOutsideArea,     ACTION_MENU::NORMAL, _( "&Outside Area" ) );
    menu->Add( PCB_ACTIONS::selectTouchingRect,    ACTION_MENU::NORMAL, _( "To&uching Rectangle" ) );
    menu->Add( PCB_ACTIONS::selectTouchingLine,    ACTION_MENU::NORMAL, _( "Touching &Line" ) );
    menu->Add( ACTIONS::selectAll,                 ACTION_MENU::NORMAL, _( "&All" ) );
    menu->Add( PCB_ACTIONS::selectBoard,           ACTION_MENU::NORMAL, _( "&Board" ) );
    menu->AppendSeparator();
    menu->Add( PCB_ACTIONS::selectNet,             ACTION_MENU::NORMAL, _( "&Net" ) );
    menu->Add( PCB_ACTIONS::selectConnectedCopper, ACTION_MENU::NORMAL, _( "Connected Co&pper" ) );
    menu->AddPlain( PCB_ACTIONS::selectConnection,  _( "Physical &Connection" ) );
    menu->AppendSeparator();
    menu->Add( PCB_ACTIONS::selectAllOnLayer,      ACTION_MENU::NORMAL, _( "All on La&yer" ) );
    menu->Add( PCB_ACTIONS::selectFreeObjects,     ACTION_MENU::NORMAL, _( "&Free Objects" ) );
    menu->Add( PCB_ACTIONS::selectAllLocked,       ACTION_MENU::NORMAL, _( "All Loc&ked" ) );
    menu->Add( PCB_ACTIONS::selectOffGridPads,     ACTION_MENU::NORMAL, _( "Off &Grid Pads" ) );
    menu->AppendSeparator();
    menu->Add( PCB_ACTIONS::toggleSelection,       ACTION_MENU::NORMAL, _( "&Toggle Selection" ) );

    // KiCad-only
    menu->AppendSeparator();
    menu->Add( PCB_ACTIONS::selectUnconnected,     ACTION_MENU::NORMAL, _( "Unconnecte&d" ) );
    menu->Add( PCB_ACTIONS::selectSameSheet,       ACTION_MENU::NORMAL, _( "Same &Sheet" ) );
    menu->Add( PCB_ACTIONS::selectOnSchematic,     ACTION_MENU::NORMAL, _( "Select on Sc&hematic" ) );
    return menu;
}


ACTION_MENU* BuildPcbDeselectMenu( TOOL_INTERACTIVE* aTool )
{
    ALTIUM_SUBMENU* menu = new ALTIUM_SUBMENU( aTool );
    menu->Add( PCB_ACTIONS::deselectLasso,         ACTION_MENU::NORMAL, _( "Lasso Des&elect" ) );
    menu->Add( PCB_ACTIONS::deselectInsideArea,    ACTION_MENU::NORMAL, _( "&Inside Area" ) );
    menu->Add( PCB_ACTIONS::deselectOutsideArea,   ACTION_MENU::NORMAL, _( "&Outside Area" ) );
    menu->Add( PCB_ACTIONS::deselectTouchingLine,  ACTION_MENU::NORMAL, _( "Touching &Line" ) );
    menu->Add( PCB_ACTIONS::deselectTouchingRect,  ACTION_MENU::NORMAL, _( "Touching &Rectangle" ) );
    menu->AddPlain( ACTIONS::unselectAll,           _( "&All" ) );
    menu->AppendSeparator();
    menu->Add( PCB_ACTIONS::deselectAllOnLayer,    ACTION_MENU::NORMAL, _( "All on La&yer" ) );
    menu->Add( PCB_ACTIONS::deselectFreeObjects,   ACTION_MENU::NORMAL, _( "&Free Objects" ) );
    menu->AppendSeparator();
    menu->Add( PCB_ACTIONS::toggleSelection,       ACTION_MENU::NORMAL, _( "&Toggle Selection" ) );

    // KiCad-only
    menu->AppendSeparator();
    menu->Add( PCB_ACTIONS::deselectNet,           ACTION_MENU::NORMAL, _( "&Net" ) );
    return menu;
}


ACTION_MENU* BuildPcbMoveMenu( TOOL_INTERACTIVE* aTool )
{
    ALTIUM_SUBMENU* menu = new ALTIUM_SUBMENU( aTool );
    menu->Add( PCB_ACTIONS::move,             ACTION_MENU::NORMAL, _( "&Move" ) );
    menu->Add( PCB_ACTIONS::drag45Degree,     ACTION_MENU::NORMAL, _( "&Drag" ) );
    menu->Add( PCB_ACTIONS::moveIndividually, ACTION_MENU::NORMAL, _( "&Component" ) );
    menu->AppendSeparator();
    menu->Add( PCB_ACTIONS::breakTrack,       ACTION_MENU::NORMAL, _( "&Break Track" ) );
    menu->Add( PCB_ACTIONS::dragFreeAngle,    ACTION_MENU::NORMAL, _( "Drag Track &End" ) );
    menu->AppendSeparator();
    menu->Add( PCB_ACTIONS::moveExact,        ACTION_MENU::NORMAL, _( "Move &Selection by X, Y..." ) );
    menu->Add( PCB_ACTIONS::rotateCcw,        ACTION_MENU::NORMAL, _( "R&otate Selection" ) );
    menu->Add( PCB_ACTIONS::flip,             ACTION_MENU::NORMAL, _( "Fl&ip Selection" ) );

    // KiCad-only
    menu->AppendSeparator();
    menu->Add( PCB_ACTIONS::positionRelative, ACTION_MENU::NORMAL, _( "&Position Relative To..." ) );
    menu->Add( PCB_ACTIONS::rotateCw,         ACTION_MENU::NORMAL, _( "Rotate Clock&wise" ) );
    menu->Add( PCB_ACTIONS::mirrorH,          ACTION_MENU::NORMAL, _( "Mirror &Horizontally" ) );
    menu->Add( PCB_ACTIONS::mirrorV,          ACTION_MENU::NORMAL, _( "Mirror &Vertically" ) );
    menu->Add( PCB_ACTIONS::swapLayers,       ACTION_MENU::NORMAL, _( "Swap &Layers..." ) );
    return menu;
}


ACTION_MENU* BuildPcbAlignMenu( TOOL_INTERACTIVE* aTool )
{
    ALTIUM_SUBMENU* menu = new ALTIUM_SUBMENU( aTool );
    menu->Add( PCB_ACTIONS::alignDialog,    ACTION_MENU::NORMAL, _( "&Align..." ) );
    menu->AppendSeparator();
    menu->Add( PCB_ACTIONS::alignLeft,      ACTION_MENU::NORMAL, _( "Align &Left" ) );
    menu->Add( PCB_ACTIONS::alignRight,     ACTION_MENU::NORMAL, _( "Align &Right" ) );
    menu->Add( PCB_ACTIONS::alignCenterX,   ACTION_MENU::NORMAL, _( "Align Horizontal &Centers" ) );
    menu->Add( PCB_ACTIONS::distributeHorizontallyCenters, ACTION_MENU::NORMAL,
               _( "&Distribute Horizontally" ) );
    menu->Add( PCB_ACTIONS::increaseHorizontalSpacing, ACTION_MENU::NORMAL,
               _( "Increase Horizontal Spacing" ) );
    menu->Add( PCB_ACTIONS::decreaseHorizontalSpacing, ACTION_MENU::NORMAL,
               _( "Decrease Horizontal Spacing" ) );
    menu->AppendSeparator();
    menu->Add( PCB_ACTIONS::alignTop,       ACTION_MENU::NORMAL, _( "Align &Top" ) );
    menu->Add( PCB_ACTIONS::alignBottom,    ACTION_MENU::NORMAL, _( "Align &Bottom" ) );
    menu->Add( PCB_ACTIONS::alignCenterY,   ACTION_MENU::NORMAL, _( "Align &Vertical Centers" ) );
    menu->Add( PCB_ACTIONS::distributeVerticallyCenters, ACTION_MENU::NORMAL,
               _( "Di&stribute Vertically" ) );
    menu->Add( PCB_ACTIONS::increaseVerticalSpacing, ACTION_MENU::NORMAL,
               _( "Increase Vertical Spacing" ) );
    menu->Add( PCB_ACTIONS::decreaseVerticalSpacing, ACTION_MENU::NORMAL,
               _( "Decrease Vertical Spacing" ) );
    menu->AppendSeparator();
    menu->Add( PCB_ACTIONS::alignToGrid,    ACTION_MENU::NORMAL, _( "Align To &Grid" ) );

    // KiCad-only
    menu->AppendSeparator();
    menu->Add( PCB_ACTIONS::distributeHorizontallyGaps, ACTION_MENU::NORMAL,
               _( "Distribute Horizontal Gaps" ) );
    menu->Add( PCB_ACTIONS::distributeVerticallyGaps, ACTION_MENU::NORMAL,
               _( "Distribute Vertical Gaps" ) );
    return menu;
}


ACTION_MENU* BuildPcbJumpMenu( TOOL_INTERACTIVE* aTool )
{
    ALTIUM_SUBMENU* menu = new ALTIUM_SUBMENU( aTool );
    menu->Add( PCB_ACTIONS::jumpAbsoluteOrigin, ACTION_MENU::NORMAL, _( "&Absolute Origin" ) );
    menu->Add( PCB_ACTIONS::jumpCurrentOrigin,  ACTION_MENU::NORMAL, _( "Current &Origin" ) );
    menu->Add( PCB_ACTIONS::jumpNewLocation,    ACTION_MENU::NORMAL, _( "New &Location..." ) );
    menu->AppendSeparator();
    menu->Add( PCB_ACTIONS::jumpComponent,      ACTION_MENU::NORMAL, _( "&Component..." ) );
    menu->Add( PCB_ACTIONS::jumpNet,            ACTION_MENU::NORMAL, _( "&Net..." ) );
    menu->Add( PCB_ACTIONS::jumpPad,            ACTION_MENU::NORMAL, _( "&Pad..." ) );
    menu->Add( PCB_ACTIONS::jumpString,         ACTION_MENU::NORMAL, _( "&String..." ) );
    menu->AddPlain( ACTIONS::nextMarker,         _( "&Error Marker" ) );
    menu->AddPlain( ACTIONS::centerSelection,    _( "Selec&tion" ) );

    // KiCad-only
    menu->AppendSeparator();
    menu->AddPlain( ACTIONS::centerContents,     _( "Board Cente&r" ) );
    return menu;
}


void PCB_EDIT_FRAME::doReCreateMenuBar()
{
    PCB_SELECTION_TOOL* selTool = m_toolManager->GetTool<PCB_SELECTION_TOOL>();
    // wxWidgets handles the Mac Application menu behind the scenes, but that means
    // we always have to start from scratch with a new wxMenuBar.
    wxMenuBar*  oldMenuBar = GetMenuBar();
    WX_MENUBAR* menuBar    = new WX_MENUBAR();

    // Recreate all menus:

    // Menus follow Altium Designer's PCB editor (docs/altium-parity/menus.md): same items,
    // order, accelerators and shortcuts where KiCad has the function.  KiCad-only commands
    // sit at the end of the closest menu, after a separator.

    //-- File menu -----------------------------------------------------------
    //
    ACTION_MENU*   fileMenu = new ACTION_MENU( false, selTool );
    static ACTION_MENU* openRecentMenu;

    ALTIUM_SUBMENU* submenuNew = new ALTIUM_SUBMENU( selTool );
    submenuNew->SetTitle( _( "&New" ) );
    submenuNew->SetIcon( BITMAPS::new_generic );

    // Altium's list, where KiCad has the document type
    if( Kiface().IsSingle() )   // not when under a project mgr
    {
        submenuNew->AddPlain( ACTIONS::doNew, _( "&PCB" ) );
    }
    else
    {
        submenuNew->AddPlain( PCB_ACTIONS::showEeschema, _( "&Schematic" ) );
        submenuNew->Add( PCB_OUTPUTS_ACTIONS::activeBom, ACTION_MENU::NORMAL, _( "Active&BOM" ) );
        submenuNew->Add( PCB_OUTPUTS_ACTIONS::newOutputJob, ACTION_MENU::NORMAL, _( "Outp&ut Job File" ) );
    }

    submenuNew->AppendSeparator();
    submenuNew->AddPlain( ACTIONS::newLibrary, _( "&Library..." ) );

    fileMenu->Add( submenuNew );

    if( Kiface().IsSingle() )
        fileMenu->Add( ACTIONS::open, ACTION_MENU::NORMAL, _( "&Open..." ) );

    fileMenu->Add( PCB_ACTIONS::closeDocument, ACTION_MENU::NORMAL, _( "&Close" ) );

    fileMenu->AppendSeparator();
    fileMenu->Add( ACTIONS::save, ACTION_MENU::NORMAL, _( "&Save" ) );

    // Under a project mgr the board keeps the project's name (save a copy instead)
    if( Kiface().IsSingle() )
        fileMenu->Add( ACTIONS::saveAs, ACTION_MENU::NORMAL, _( "Save &As..." ) );

    fileMenu->Add( ACTIONS::saveCopy, ACTION_MENU::NORMAL, _( "Save Cop&y As..." ) );

    // Import submenu
    ACTION_MENU* submenuImport = new ACTION_MENU( false, selTool );
    submenuImport->SetTitle( _( "&Import" ) );
    submenuImport->SetIcon( BITMAPS::import );

    // Altium's order: DXF/DWG, ..., Specctra Route (the formats KiCad reads)
    submenuImport->Add( PCB_ACTIONS::placeImportedGraphics,  ACTION_MENU::NORMAL, _( "&DXF/DWG..." ) );
    submenuImport->Add( PCB_ACTIONS::importSpecctraSession,  ACTION_MENU::NORMAL, _( "Specctra &Route..." ) );

    // KiCad-only
    submenuImport->AppendSeparator();
    submenuImport->Add( PCB_ACTIONS::importNetlist,          ACTION_MENU::NORMAL, _( "Netlist..." ) );
    submenuImport->Add( PCB_ACTIONS::openNonKicadBoard );
    submenuImport->Add( PCB_ACTIONS::appendBoard );

    fileMenu->AppendSeparator();
    fileMenu->Add( submenuImport );

    // Export submenu
    ACTION_MENU* submenuExport = new ACTION_MENU( false, selTool );
    submenuExport->SetTitle( _( "&Export" ) );
    submenuExport->SetIcon( BITMAPS::export_file );

    // Altium's order (the formats KiCad writes)
    submenuExport->Add( PCB_ACTIONS::exportHyperlynx,   ACTION_MENU::NORMAL, _( "&Hyperlynx..." ) );
    submenuExport->Add( PCB_ACTIONS::exportIDF,         ACTION_MENU::NORMAL, _( "&IDF Board..." ) );
    submenuExport->Add( PCB_ACTIONS::exportSpecctraDSN, ACTION_MENU::NORMAL, _( "&Specctra Design..." ) );
    submenuExport->Add( PCB_ACTIONS::exportSTEP,        ACTION_MENU::NORMAL, _( "S&TEP 3D..." ) );
    submenuExport->Add( PCB_ACTIONS::exportVRML,        ACTION_MENU::NORMAL, _( "&VRML..." ) );

    // KiCad-only
    submenuExport->AppendSeparator();
    submenuExport->Add( PCB_ACTIONS::exportGenCAD,      ACTION_MENU::NORMAL, _( "GenCAD..." ) );
    submenuExport->Add( PCB_ACTIONS::exportCmpFile,     ACTION_MENU::NORMAL, _( "Footprint Association (.cmp) File..." ) );

    if( ADVANCED_CFG::GetCfg().m_ShowPcbnewExportNetlist && m_exportNetlistAction )
        submenuExport->Add( *m_exportNetlistAction );

    submenuExport->AppendSeparator();
    submenuExport->Add( PCB_ACTIONS::exportFootprints,  ACTION_MENU::NORMAL, _( "Footprints..." ) );

    fileMenu->Add( submenuExport );

    // Fabrication Outputs submenu
    ACTION_MENU* submenuFabOutputs = new ACTION_MENU( false, selTool );
    submenuFabOutputs->SetTitle( _( "&Fabrication Outputs" ) );
    submenuFabOutputs->SetIcon( BITMAPS::fabrication );

    // Altium's order (the outputs KiCad generates)
    submenuFabOutputs->Add( PCB_ACTIONS::generateGerbers,     ACTION_MENU::NORMAL, _( "&Gerber Files" ) );
    submenuFabOutputs->Add( PCB_ACTIONS::generateIPC2581File, ACTION_MENU::NORMAL, _( "&IPC-2581" ) );
    submenuFabOutputs->Add( PCB_ACTIONS::generateDrillFiles,  ACTION_MENU::NORMAL, _( "&NC Drill Files" ) );
    submenuFabOutputs->Add( PCB_ACTIONS::generateODBPPFile,   ACTION_MENU::NORMAL, _( "&ODB++ Files" ) );
    submenuFabOutputs->Add( PCB_ACTIONS::generateD356File,    ACTION_MENU::NORMAL, _( "&Test Point Report" ) );

    // KiCad-only
    submenuFabOutputs->AppendSeparator();
    submenuFabOutputs->Add( PCB_ACTIONS::generateReportFile );

    fileMenu->AppendSeparator();
    fileMenu->Add( submenuFabOutputs );

    // Assembly Outputs submenu (Altium splits these from fabrication outputs)
    ACTION_MENU* submenuAssemblyOutputs = new ACTION_MENU( false, selTool );
    submenuAssemblyOutputs->SetTitle( _( "Assembl&y Outputs" ) );
    submenuAssemblyOutputs->SetIcon( BITMAPS::post_compo );

    submenuAssemblyOutputs->Add( PCB_ACTIONS::generatePosFile, ACTION_MENU::NORMAL,
                                 _( "&Generates pick and place files" ) );
    submenuAssemblyOutputs->Add( PCB_ACTIONS::generateD356File, ACTION_MENU::NORMAL,
                                 _( "&Test Point Report" ) );

    // KiCad-only
    submenuAssemblyOutputs->AppendSeparator();
    submenuAssemblyOutputs->Add( PCB_ACTIONS::generateBOM );
    fileMenu->Add( submenuAssemblyOutputs );

    fileMenu->AppendSeparator();
    fileMenu->Add( ACTIONS::print, ACTION_MENU::NORMAL, _( "&Print..." ) );

    // Altium's Smart PDF: KiCad's plot dialog writes the PDF
    fileMenu->AppendSeparator();
    fileMenu->Add( ACTIONS::plot, ACTION_MENU::NORMAL, _( "S&mart PDF..." ) );

    if( Kiface().IsSingle() )
    {
        FILE_HISTORY& fileHistory = GetFileHistory();

        // Create the menu if it does not exist. Adding a file to/from the history
        // will automatically refresh the menu.
        if( !openRecentMenu )
        {
            openRecentMenu = new ACTION_MENU( false, selTool );
            openRecentMenu->SetIcon( BITMAPS::recent );

            fileHistory.UseMenu( openRecentMenu );
            fileHistory.AddFilesToMenu();
        }

        // Ensure the title is up to date after changing language
        openRecentMenu->SetTitle( _( "&Recent Documents" ) );
        fileHistory.UpdateClearText( openRecentMenu, _( "Clear Recent Files" ) );

        fileMenu->AppendSeparator();
        wxMenuItem* item = fileMenu->Add( openRecentMenu->Clone() );

        // Add the file menu condition here since it needs the item ID for the submenu
        ACTION_CONDITIONS cond;
        cond.Enable( FILE_HISTORY::FileHistoryNotEmpty( fileHistory ) );
        RegisterUIUpdateHandler( item->GetId(), cond );
    }

    // Altium: Exit (Alt+F4) leaves the application
    fileMenu->AppendSeparator();
    fileMenu->Add( _( "E&xit" ) + wxS( "\tAlt+F4" ), _( "Close all documents and exit" ),
                   wxID_EXIT, BITMAPS::exit );

    // KiCad-only
    fileMenu->AppendSeparator();
    fileMenu->Add( ACTIONS::revert );
    fileMenu->Add( PCB_ACTIONS::rescueAutosave );
    fileMenu->Add( ACTIONS::pageSettings );

    //-- Edit menu -----------------------------------------------------------
    //
    ACTION_MENU* editMenu = new ACTION_MENU( false, selTool );

    editMenu->Add( ACTIONS::undo, ACTION_MENU::NORMAL, _( "&Undo" ) );
    editMenu->Add( ACTIONS::redo, ACTION_MENU::NORMAL, _( "&Redo" ) );

    editMenu->AppendSeparator();
    editMenu->Add( ACTIONS::cut,          ACTION_MENU::NORMAL, _( "Cu&t" ) );
    editMenu->Add( ACTIONS::copy,         ACTION_MENU::NORMAL, _( "&Copy" ) );
    editMenu->Add( ACTIONS::copyAsText,   ACTION_MENU::NORMAL, _( "Copy As Text" ) );
    editMenu->Add( ACTIONS::paste,        ACTION_MENU::NORMAL, _( "&Paste" ) );
    editMenu->Add( ACTIONS::pasteSpecial, ACTION_MENU::NORMAL, _( "P&aste Special..." ) );

    // Altium's Paste Array (a button of its Paste Special dialog): KiCad's Create Array, which
    // arrays the selection (still selected after a copy)
    editMenu->Add( PCB_ACTIONS::createArray, ACTION_MENU::NORMAL, _( "Paste Arra&y..." ) );

    editMenu->AppendSeparator();

    ACTION_MENU* selectSubMenu = BuildPcbSelectMenu( selTool );
    selectSubMenu->SetTitle( _( "&Select" ) );
    editMenu->Add( selectSubMenu );

    ACTION_MENU* deselectSubMenu = BuildPcbDeselectMenu( selTool );
    deselectSubMenu->SetTitle( _( "D&eSelect" ) );
    editMenu->Add( deselectSubMenu );

    editMenu->AppendSeparator();
    editMenu->Add( ACTIONS::doDelete,  ACTION_MENU::NORMAL, _( "&Delete" ) );
    editMenu->Add( ACTIONS::duplicate, ACTION_MENU::NORMAL, _( "Duplicate" ) );

    ACTION_MENU* moveSubMenu = BuildPcbMoveMenu( selTool );
    moveSubMenu->SetTitle( _( "&Move" ) );
    moveSubMenu->SetIcon( BITMAPS::move );
    editMenu->Add( moveSubMenu );

    ACTION_MENU* alignSubMenu = BuildPcbAlignMenu( selTool );
    alignSubMenu->SetTitle( _( "Ali&gn" ) );
    alignSubMenu->SetIcon( BITMAPS::align_items );
    editMenu->Add( alignSubMenu );

    ACTION_MENU* originSubMenu = new ACTION_MENU( false, selTool );
    originSubMenu->SetTitle( _( "&Origin" ) );
    originSubMenu->SetIcon( BITMAPS::set_origin );
    originSubMenu->Add( ACTIONS::gridSetOrigin,   ACTION_MENU::NORMAL, _( "&Set" ) );
    originSubMenu->Add( ACTIONS::gridResetOrigin, ACTION_MENU::NORMAL, _( "&Reset" ) );
    originSubMenu->AppendSeparator();
    originSubMenu->Add( ACTIONS::gridOrigin );
    originSubMenu->Add( PCB_ACTIONS::drillOrigin );
    originSubMenu->Add( PCB_ACTIONS::drillResetOrigin );
    editMenu->Add( originSubMenu );

    ACTION_MENU* jumpSubMenu = BuildPcbJumpMenu( selTool );
    jumpSubMenu->SetTitle( _( "&Jump" ) );
    editMenu->Add( jumpSubMenu );

    // Selection Memory: Store / Recall / Recall Plus (add to the selection), slots 1 - 8
    ACTION_MENU* selMemorySubMenu = new ACTION_MENU( false, selTool );
    selMemorySubMenu->SetTitle( _( "Selection Memor&y" ) );

    const TOOL_ACTION* storeActions[] = {
        &PCB_ACTIONS::selectionMemoryStore1, &PCB_ACTIONS::selectionMemoryStore2,
        &PCB_ACTIONS::selectionMemoryStore3, &PCB_ACTIONS::selectionMemoryStore4,
        &PCB_ACTIONS::selectionMemoryStore5, &PCB_ACTIONS::selectionMemoryStore6,
        &PCB_ACTIONS::selectionMemoryStore7, &PCB_ACTIONS::selectionMemoryStore8 };
    const TOOL_ACTION* recallActions[] = {
        &PCB_ACTIONS::selectionMemoryRecall1, &PCB_ACTIONS::selectionMemoryRecall2,
        &PCB_ACTIONS::selectionMemoryRecall3, &PCB_ACTIONS::selectionMemoryRecall4,
        &PCB_ACTIONS::selectionMemoryRecall5, &PCB_ACTIONS::selectionMemoryRecall6,
        &PCB_ACTIONS::selectionMemoryRecall7, &PCB_ACTIONS::selectionMemoryRecall8 };
    const TOOL_ACTION* recallAddActions[] = {
        &PCB_ACTIONS::selectionMemoryRecallAdd1, &PCB_ACTIONS::selectionMemoryRecallAdd2,
        &PCB_ACTIONS::selectionMemoryRecallAdd3, &PCB_ACTIONS::selectionMemoryRecallAdd4,
        &PCB_ACTIONS::selectionMemoryRecallAdd5, &PCB_ACTIONS::selectionMemoryRecallAdd6,
        &PCB_ACTIONS::selectionMemoryRecallAdd7, &PCB_ACTIONS::selectionMemoryRecallAdd8 };

    auto memorySlots =
            [&]( const wxString& aTitle, const TOOL_ACTION* const* aActions )
            {
                ACTION_MENU* slots = new ACTION_MENU( false, selTool );
                slots->SetTitle( aTitle );

                for( int ii = 0; ii < 8; ++ii )
                {
                    slots->Add( *aActions[ii], ACTION_MENU::NORMAL,
                                wxString::Format( wxS( "&%d" ), ii + 1 ) );
                }

                selMemorySubMenu->Add( slots );
            };

    memorySlots( _( "&Store" ), storeActions );
    memorySlots( _( "&Recall" ), recallActions );
    memorySlots( _( "Recall P&lus" ), recallAddActions );
    editMenu->Add( selMemorySubMenu );

    editMenu->AppendSeparator();
    editMenu->Add( PCB_ACTIONS::findSimilar, ACTION_MENU::NORMAL, _( "Fi&nd Similar Objects" ) );

    // KiCad-only
    editMenu->AppendSeparator();
    editMenu->Add( ACTIONS::group );
    editMenu->Add( ACTIONS::ungroup );
    editMenu->Add( PCB_ACTIONS::lock );
    editMenu->Add( PCB_ACTIONS::unlock );
    editMenu->Add( PCB_ACTIONS::editTracksAndVias );
    editMenu->Add( PCB_ACTIONS::editTextAndGraphics );
    editMenu->Add( PCB_ACTIONS::changeFootprints );
    editMenu->Add( ACTIONS::deleteTool );
    editMenu->Add( PCB_ACTIONS::globalDeletions );


    //----- View menu -----------------------------------------------------------
    //
    ACTION_MENU* viewMenu = new ACTION_MENU( false, selTool );

    viewMenu->Add( PCB_ACTIONS::layoutMode2D, ACTION_MENU::CHECK, _( "&2D Layout Mode" ) );
    viewMenu->Add( PCB_ACTIONS::layoutMode3D, ACTION_MENU::CHECK, _( "&3D Layout Mode" ) );

    viewMenu->AppendSeparator();
    viewMenu->Add( ACTIONS::zoomFitScreen,    ACTION_MENU::NORMAL, _( "Fit &Document" ) );
    viewMenu->Add( ACTIONS::zoomFitObjects,   ACTION_MENU::NORMAL, _( "&Fit Board" ) );
    viewMenu->Add( ACTIONS::zoomTool,         ACTION_MENU::NORMAL, _( "&Area" ) );
    viewMenu->Add( ACTIONS::zoomCenter,       ACTION_MENU::NORMAL, _( "Around &Point" ) );
    viewMenu->Add( ACTIONS::zoomFitSelection, ACTION_MENU::NORMAL, _( "S&elected Objects" ) );

    viewMenu->AppendSeparator();
    viewMenu->Add( ACTIONS::zoomIn,           ACTION_MENU::NORMAL, _( "Zoom &In" ) );
    viewMenu->Add( ACTIONS::zoomOut,          ACTION_MENU::NORMAL, _( "Zoom &Out" ) );

    viewMenu->AppendSeparator();
    viewMenu->Add( PCB_ACTIONS::flipBoard,    ACTION_MENU::CHECK,  _( "Flip &Board" ) );

    viewMenu->AppendSeparator();
    viewMenu->Add( PCB_ACTIONS::fullScreen,   ACTION_MENU::CHECK,  _( "Full Screen" ) );

    viewMenu->AppendSeparator();
    // Altium's mask level dims what isn't in focus; KiCad's layer opacity is the closest
    viewMenu->Add( PCB_ACTIONS::layerAlphaInc, ACTION_MENU::NORMAL, _( "Increase Mask &Level" ) );
    viewMenu->Add( PCB_ACTIONS::layerAlphaDec, ACTION_MENU::NORMAL, _( "Decrease Mas&k Level" ) );

    viewMenu->AppendSeparator();
    ACTION_MENU* showHidePanels = new ACTION_MENU( false, selTool );
    showHidePanels->SetTitle( _( "Pa&nels" ) );
    // Altium's order (its alphabetical list, the panels we have)
    showHidePanels->Add( PCB_ACTIONS::showPcbList,       ACTION_MENU::CHECK, _( "PCB &List" ) );
    showHidePanels->Add( PCB_ACTIONS::showSearch,        ACTION_MENU::CHECK, _( "PCB &Filter" ) );
    showHidePanels->Add( PCB_ACTIONS::showPcbNavigator,  ACTION_MENU::CHECK, _( "&PCB" ) );
    showHidePanels->Add( ACTIONS::showMessages,          ACTION_MENU::CHECK, _( "&Messages" ) );

    if( ADVANCED_CFG::GetCfg().m_EnablePcbDesignBlocks )
        showHidePanels->Add( PCB_ACTIONS::showDesignBlockPanel, ACTION_MENU::CHECK, _( "Design Reuse" ) );

    showHidePanels->Add( PCB_ACTIONS::showLayersManager, ACTION_MENU::CHECK, _( "View Configuration" ) );
    showHidePanels->Add( ACTIONS::showProperties,        ACTION_MENU::CHECK, _( "Properties" ) );

    // KiCad-only
    showHidePanels->AppendSeparator();
    showHidePanels->Add( PCB_ACTIONS::showNetInspector,  ACTION_MENU::CHECK );

    viewMenu->Add( showHidePanels );

    viewMenu->AppendSeparator();
    viewMenu->Add( PCB_ACTIONS::toggleStatusBar, ACTION_MENU::CHECK, _( "S&tatus Bar" ) );

    viewMenu->AppendSeparator();
    ACTION_MENU* insightSubMenu = new ACTION_MENU( false, selTool );
    insightSubMenu->SetTitle( _( "Board Insig&ht" ) );
    insightSubMenu->Add( PCB_ACTIONS::toggleBoardInsight, ACTION_MENU::CHECK,
                         _( "Toggle Heads Up &Display" ) );
    insightSubMenu->Add( PCB_ACTIONS::toggleInsightLens, ACTION_MENU::CHECK,
                         _( "Toggle &Lens" ) );
    insightSubMenu->Add( PCB_ACTIONS::toggleInsightLensPin, ACTION_MENU::CHECK,
                         _( "Pin Lens to &Cursor" ) );
    insightSubMenu->Add( ACTIONS::resetLocalCoords, ACTION_MENU::NORMAL,
                         _( "Resets Heads Up Delta &Origin" ) );
    viewMenu->Add( insightSubMenu );

    ACTION_MENU* connectionsSubMenu = new ACTION_MENU( false, selTool );
    connectionsSubMenu->SetTitle( _( "&Connections" ) );
    connectionsSubMenu->SetIcon( BITMAPS::general_ratsnest );
    connectionsSubMenu->Add( PCB_ACTIONS::showNetInRatsnest, ACTION_MENU::NORMAL, _( "Show &Net" ) );
    connectionsSubMenu->Add( PCB_ACTIONS::ratsnestShowAll,   ACTION_MENU::NORMAL, _( "Show &All" ) );
    connectionsSubMenu->AppendSeparator();
    connectionsSubMenu->Add( PCB_ACTIONS::hideNetInRatsnest, ACTION_MENU::NORMAL, _( "Hide Net" ) );
    connectionsSubMenu->Add( PCB_ACTIONS::ratsnestHideAll,   ACTION_MENU::NORMAL, _( "&Hide All" ) );

    // KiCad-only
    connectionsSubMenu->AppendSeparator();
    connectionsSubMenu->Add( PCB_ACTIONS::ratsnestLineMode,  ACTION_MENU::CHECK );
    viewMenu->Add( connectionsSubMenu );

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
    viewMenu->Add( ACTIONS::zoomRedraw, ACTION_MENU::NORMAL, _( "Refresh" ) );
    viewMenu->Add( ACTIONS::showFootprintBrowser );

    // Drawing Mode Submenu
    ACTION_MENU* drawingModeSubMenu = new ACTION_MENU( false, selTool );
    drawingModeSubMenu->SetTitle( _( "Drawing Mode" ) );
    drawingModeSubMenu->SetIcon( BITMAPS::add_zone );

    drawingModeSubMenu->Add( PCB_ACTIONS::zoneDisplayFilled,   ACTION_MENU::CHECK );
    drawingModeSubMenu->Add( PCB_ACTIONS::zoneDisplayOutline,  ACTION_MENU::CHECK );

    if( ADVANCED_CFG::GetCfg().m_ExtraZoneDisplayModes )
    {
        drawingModeSubMenu->Add( PCB_ACTIONS::zoneDisplayFractured,    ACTION_MENU::CHECK );
        drawingModeSubMenu->Add( PCB_ACTIONS::zoneDisplayTriangulated, ACTION_MENU::CHECK );
    }

    drawingModeSubMenu->AppendSeparator();
    drawingModeSubMenu->Add( PCB_ACTIONS::padDisplayMode,      ACTION_MENU::CHECK );
    drawingModeSubMenu->Add( PCB_ACTIONS::viaDisplayMode,      ACTION_MENU::CHECK );
    drawingModeSubMenu->Add( PCB_ACTIONS::trackDisplayMode,    ACTION_MENU::CHECK );

    drawingModeSubMenu->AppendSeparator();
    drawingModeSubMenu->Add( PCB_ACTIONS::graphicsOutlines,    ACTION_MENU::CHECK );
    drawingModeSubMenu->Add( PCB_ACTIONS::textOutlines,        ACTION_MENU::CHECK );

    viewMenu->Add( drawingModeSubMenu );

    // Single layer mode is Altium's name for KiCad's contrast mode
    ACTION_MENU* contrastModeSubMenu = new ACTION_MENU( false, selTool );
    contrastModeSubMenu->SetTitle( _( "Single Layer Mode" ) );
    contrastModeSubMenu->SetIcon( BITMAPS::contrast_mode );
    contrastModeSubMenu->Add( ACTIONS::highContrastMode,    ACTION_MENU::CHECK );
    contrastModeSubMenu->Add( ACTIONS::highContrastModeCycle );
    viewMenu->Add( contrastModeSubMenu );

#ifdef __APPLE__
    viewMenu->AppendSeparator();
#endif

    //-- Project menu --------------------------------------------------------
    //
    ACTION_MENU* projectMenu = new ACTION_MENU( false, selTool );

    // Altium: Validate PCB Project <name> (ERC plus schematic / PCB parity into Messages)
    projectMenu->Add( PCB_PROJECT_ACTIONS::validateProject, ACTION_MENU::NORMAL,
                      PCB_PROJECT_TOOL::ValidateLabel( this ) );
    projectMenu->AppendSeparator();

    if( !Kiface().IsSingle() )
    {
        projectMenu->Add( ACTIONS::showProjectManager, ACTION_MENU::NORMAL,
                          _( "Project &Documents..." ) );
        projectMenu->AppendSeparator();
    }

    projectMenu->Add( PCB_OUTPUTS_ACTIONS::variantManager, ACTION_MENU::NORMAL, _( "&Variants..." ) );

    // KiCad-only
    projectMenu->AppendSeparator();
    projectMenu->Add( PCB_ACTIONS::showEeschema );
    projectMenu->Add( PCB_OUTPUTS_ACTIONS::openOutputJob, ACTION_MENU::NORMAL, _( "Output &Jobs..." ) );
    projectMenu->Add( ACTIONS::configurePaths );
    projectMenu->Add( ACTIONS::showFootprintLibTable );

    if( ADVANCED_CFG::GetCfg().m_EnablePcbDesignBlocks )
        projectMenu->Add( ACTIONS::showDesignBlockLibTable );

    //-- Place Menu ----------------------------------------------------------
    //
    ACTION_MENU* placeMenu = new ACTION_MENU( false, selTool );

    placeMenu->Add( PCB_ACTIONS::placeFootprint, ACTION_MENU::NORMAL, _( "&Component..." ) );

    if( ADVANCED_CFG::GetCfg().m_EnablePcbDesignBlocks )
        placeMenu->Add( PCB_ACTIONS::placeDesignBlock, ACTION_MENU::NORMAL, _( "Re&use Block..." ) );

    placeMenu->AppendSeparator();
    placeMenu->Add( PCB_ACTIONS::drawRectangle, ACTION_MENU::NORMAL, _( "&Fill" ) );
    placeMenu->Add( PCB_ACTIONS::drawPolygon, ACTION_MENU::NORMAL, _( "Solid &Region" ) );

    ALTIUM_SUBMENU* arcSubmenu = new ALTIUM_SUBMENU( selTool );
    arcSubmenu->SetTitle( _( "&Arc" ) );
    arcSubmenu->SetIcon( BITMAPS::add_arc );
    arcSubmenu->AddPlain( PCB_ACTIONS::drawArc,    _( "&Arc (Center)" ) );
    arcSubmenu->AddPlain( PCB_ACTIONS::drawCircle, _( "F&ull Circle" ) );

    // KiCad-only
    arcSubmenu->AppendSeparator();
    arcSubmenu->AddPlain( PCB_ACTIONS::drawBezier, _( "&Bezier" ) );
    placeMenu->Add( arcSubmenu );

    placeMenu->Add( PCB_ACTIONS::drawLine, ACTION_MENU::NORMAL, _( "&Line" ) );
    placeMenu->Add( PCB_ACTIONS::placeText, ACTION_MENU::NORMAL, _( "&String" ) );
    placeMenu->Add( PCB_ACTIONS::drawTextBox, ACTION_MENU::NORMAL, _( "Te&xt Frame" ) );
    placeMenu->Add( PCB_ACTIONS::drawVia, ACTION_MENU::NORMAL, _( "&Via" ) );
    placeMenu->Add( PCB_ACTIONS::routeSingleTrack, ACTION_MENU::NORMAL, _( "&Track" ) );
    placeMenu->Add( PCB_ACTIONS::placeReferenceImage, ACTION_MENU::NORMAL, _( "Grap&hics..." ) );

    placeMenu->AppendSeparator();
    ACTION_MENU* keepoutSubmenu = new ACTION_MENU( false, selTool );
    keepoutSubmenu->SetTitle( _( "&Keepout" ) );
    keepoutSubmenu->SetIcon( BITMAPS::add_keepout_area );
    // Altium's keepout arcs, fills and tracks are KiCad rule areas; one tool draws them
    keepoutSubmenu->Add( PCB_ACTIONS::drawRuleArea, ACTION_MENU::NORMAL, _( "Solid &Region" ) );
    placeMenu->Add( keepoutSubmenu );

    placeMenu->AppendSeparator();
    placeMenu->Add( PCB_ACTIONS::drawZone, ACTION_MENU::NORMAL, _( "Poly&gon Pour..." ) );
    placeMenu->Add( PCB_ACTIONS::drawZoneCutout, ACTION_MENU::NORMAL, _( "Polygon Pour Cutout" ) );

    placeMenu->AppendSeparator();
    placeMenu->Add( PCB_ACTIONS::placeStackup, ACTION_MENU::NORMAL, _( "Layer Stac&k Table" ) );

    placeMenu->AppendSeparator();
    ACTION_MENU* dimensionSubmenu = new ACTION_MENU( false, selTool );
    dimensionSubmenu->SetTitle( _( "&Dimension" ) );
    dimensionSubmenu->SetIcon( BITMAPS::add_aligned_dimension );
    // Altium's order (the dimension kinds KiCad has)
    dimensionSubmenu->Add( PCB_ACTIONS::drawOrthogonalDimension, ACTION_MENU::NORMAL, _( "&Linear" ) );
    dimensionSubmenu->Add( PCB_ACTIONS::drawRadialDimension,     ACTION_MENU::NORMAL, _( "&Radial" ) );
    dimensionSubmenu->Add( PCB_ACTIONS::drawLeader,              ACTION_MENU::NORMAL, _( "L&eader" ) );
    dimensionSubmenu->Add( PCB_ACTIONS::drawCenterDimension,     ACTION_MENU::NORMAL, _( "&Center" ) );

    // KiCad-only
    dimensionSubmenu->AppendSeparator();
    dimensionSubmenu->Add( PCB_ACTIONS::drawAlignedDimension,    ACTION_MENU::NORMAL, _( "Aligned" ) );
    placeMenu->Add( dimensionSubmenu );

    // KiCad-only
    placeMenu->AppendSeparator();
    placeMenu->Add( PCB_ACTIONS::placePoint, ACTION_MENU::NORMAL, _( "Coordinate" ) );
    placeMenu->Add( PCB_ACTIONS::drawTable );
    placeMenu->Add( PCB_ACTIONS::placeBarcode );
    placeMenu->Add( PCB_ACTIONS::placeCharacteristics );

    ACTION_MENU* muwaveSubmenu = new ACTION_MENU( false, selTool );
    muwaveSubmenu->SetTitle( _( "Microwave Shapes" ) );
    muwaveSubmenu->SetIcon( BITMAPS::mw_add_line );
    muwaveSubmenu->Add( PCB_ACTIONS::microwaveCreateLine );
    muwaveSubmenu->Add( PCB_ACTIONS::microwaveCreateGap );
    muwaveSubmenu->Add( PCB_ACTIONS::microwaveCreateStub );
    muwaveSubmenu->Add( PCB_ACTIONS::microwaveCreateStubArc );
    muwaveSubmenu->Add( PCB_ACTIONS::microwaveCreateFunctionShape );
    placeMenu->Add( muwaveSubmenu );

    //-- Design Menu ---------------------------------------------------------
    //
    ACTION_MENU* designMenu = new ACTION_MENU( false, selTool );

    // As in Altium, the commands name the project ("Update Schematics in Blinky.kicad_pro")
    wxString project = Prj().IsNullProject() ? wxString()
                                             : wxFileName( Prj().GetProjectFullName() ).GetFullName();

    designMenu->Add( ACTIONS::updateSchematicFromPcb, ACTION_MENU::NORMAL,
                     project.IsEmpty() ? _( "&Update Schematics..." )
                                       : wxString::Format( _( "&Update Schematics in %s" ),
                                                           project ) )
            ->Enable( !Kiface().IsSingle() );
    designMenu->Add( ACTIONS::updatePcbFromSchematic, ACTION_MENU::NORMAL,
                     project.IsEmpty() ? _( "&Import Changes From Schematics..." )
                                       : wxString::Format( _( "&Import Changes From %s" ),
                                                           project ) )
            ->Enable( !Kiface().IsSingle() );

    designMenu->AppendSeparator();
    designMenu->Add( PCB_ACTIONS::drcRuleEditor, ACTION_MENU::NORMAL, _( "&Rules..." ) );
    designMenu->Add( PCB_ACTIONS::ruleWizard, ACTION_MENU::NORMAL, _( "Rule &Wizard..." ) );

    designMenu->AppendSeparator();
    ACTION_MENU* netlistSubmenu = new ACTION_MENU( false, selTool );
    netlistSubmenu->SetTitle( _( "&Netlist" ) );
    netlistSubmenu->SetIcon( BITMAPS::netlist );
    netlistSubmenu->Add( PCB_ACTIONS::assignNetClass, ACTION_MENU::NORMAL,
                         _( "&Add Selected Net to NetClass" ) );

    // KiCad-only
    netlistSubmenu->AppendSeparator();
    netlistSubmenu->Add( PCB_ACTIONS::importNetlist, ACTION_MENU::NORMAL, _( "&Import Netlist..." ) );
    designMenu->Add( netlistSubmenu );

    designMenu->Add( PCB_ACTIONS::layerStackManager, ACTION_MENU::NORMAL,
                     _( "Layer Stac&k Manager..." ) );

    // Rooms: placement rule areas bound to a sheet, component class or group
    ACTION_MENU* roomsSubmenu = new ACTION_MENU( false, selTool );
    roomsSubmenu->SetTitle( _( "R&ooms" ) );
    roomsSubmenu->SetIcon( BITMAPS::add_keepout_area );
    roomsSubmenu->Add( PCB_SWAP_ROOM_ACTIONS::placeRectangularRoom, ACTION_MENU::NORMAL,
                       _( "Place &Rectangular Room" ) );
    roomsSubmenu->Add( PCB_SWAP_ROOM_ACTIONS::placePolygonalRoom, ACTION_MENU::NORMAL,
                       _( "Place Polygonal Roo&m" ) );
    roomsSubmenu->Add( PCB_SWAP_ROOM_ACTIONS::moveRoom, ACTION_MENU::NORMAL, _( "Mo&ve Room" ) );
    roomsSubmenu->AppendSeparator();
    roomsSubmenu->Add( PCB_ACTIONS::repeatLayout, ACTION_MENU::NORMAL, _( "&Copy Room Formats" ) );
    roomsSubmenu->AppendSeparator();
    roomsSubmenu->Add( PCB_SWAP_ROOM_ACTIONS::roomFromSelectionHull, ACTION_MENU::NORMAL,
                       _( "Create &Non-Orthogonal Room from selected components" ) );
    roomsSubmenu->Add( PCB_SWAP_ROOM_ACTIONS::roomFromSelectionOrthogonal, ACTION_MENU::NORMAL,
                       _( "Create &Orthogonal Room from selected components" ) );
    roomsSubmenu->Add( PCB_SWAP_ROOM_ACTIONS::roomFromSelectionRect, ACTION_MENU::NORMAL,
                       _( "Create Rec&tangle Room from selected components" ) );

    // KiCad-only
    roomsSubmenu->AppendSeparator();
    roomsSubmenu->Add( PCB_ACTIONS::generatePlacementRuleAreas, ACTION_MENU::NORMAL,
                       _( "Generate Rooms from Sheets / Classes..." ) );
    designMenu->Add( roomsSubmenu );

    designMenu->Add( PCB_ACTIONS::netClasses, ACTION_MENU::NORMAL, _( "&Classes..." ) );

    designMenu->AppendSeparator();
    designMenu->Add( PCB_ACTIONS::exportFootprints, ACTION_MENU::NORMAL, _( "Make &PCB Library" ) );

    // KiCad-only
    designMenu->AppendSeparator();
    designMenu->Add( PCB_ACTIONS::boardSetup, ACTION_MENU::NORMAL, _( "Board Options..." ) );

    //-- Tools menu ----------------------------------------------------------
    //
    ACTION_MENU* toolsMenu = new ACTION_MENU( false, selTool );

    toolsMenu->Add( PCB_ACTIONS::runDRC, ACTION_MENU::NORMAL, _( "&Design Rule Check..." ) );
    toolsMenu->Add( PCB_ACTIONS::resetErrorMarkers, ACTION_MENU::NORMAL, _( "Reset Error &Markers" ) );

    toolsMenu->AppendSeparator();
    toolsMenu->Add( PCB_ACTIONS::browseViolations, ACTION_MENU::NORMAL, _( "Browse Violations" ) );
    toolsMenu->Add( PCB_ACTIONS::browseObjects,    ACTION_MENU::NORMAL, _( "Browse Objects" ) );

    toolsMenu->AppendSeparator();
    ACTION_MENU* pourSubmenu = new POLYGON_POURS_MENU( this, selTool );
    toolsMenu->Add( pourSubmenu );

    ACTION_MENU* autoplaceSubmenu = new ACTION_MENU( false, selTool );
    autoplaceSubmenu->SetTitle( _( "C&omponent Placement" ) );
    autoplaceSubmenu->SetIcon( BITMAPS::mode_module );
    autoplaceSubmenu->Add( PCB_SWAP_ROOM_ACTIONS::arrangeWithinRoom, ACTION_MENU::NORMAL,
                           _( "Arrange Within &Room" ) );
    autoplaceSubmenu->Add( PCB_ACTIONS::autoplaceOffboardComponents, ACTION_MENU::NORMAL,
                           _( "Arrange &Outside Board" ) );
    autoplaceSubmenu->AppendSeparator();
    autoplaceSubmenu->Add( PCB_ACTIONS::moveIndividually, ACTION_MENU::NORMAL,
                           _( "Reposition Selected &Components" ) );
    autoplaceSubmenu->AppendSeparator();
    autoplaceSubmenu->Add( PCB_ACTIONS::swapComponents, ACTION_MENU::NORMAL, _( "S&wap Components" ) );

    // KiCad-only
    autoplaceSubmenu->AppendSeparator();
    autoplaceSubmenu->Add( PCB_ACTIONS::autoplaceSelectedComponents, ACTION_MENU::NORMAL,
                           _( "&Place Selected Footprints" ) );
    toolsMenu->Add( autoplaceSubmenu );

    toolsMenu->AppendSeparator();
    toolsMenu->Add( PCB_ACTIONS::boardReannotate, ACTION_MENU::NORMAL, _( "Re-A&nnotate..." ) );

    toolsMenu->AppendSeparator();
    toolsMenu->Add( PCB_ACTIONS::updateFootprints, ACTION_MENU::NORMAL,
                    _( "Update From PCB &Libraries..." ) );

    ACTION_MENU* swapSubmenu = new ACTION_MENU( false, selTool );
    swapSubmenu->SetTitle( _( "Pin/Part S&wapping" ) );
    swapSubmenu->SetIcon( BITMAPS::swap );
    swapSubmenu->Add( PCB_SWAP_ROOM_ACTIONS::interactivePinSwap, ACTION_MENU::NORMAL,
                      _( "&Interactive Pin/Net Swapping" ) );
    swapSubmenu->Add( PCB_SWAP_ROOM_ACTIONS::interactivePartSwap, ACTION_MENU::NORMAL,
                      _( "Interactive &Part Swapping" ) );
    toolsMenu->Add( swapSubmenu );

    toolsMenu->AppendSeparator();
    toolsMenu->Add( PCB_ACTIONS::selectOnSchematic, ACTION_MENU::NORMAL, _( "&Cross Probe" ) );
    toolsMenu->Add( PCB_ACTIONS::crossSelectMode,   ACTION_MENU::CHECK,  _( "Cross Select Mode" ) );

    toolsMenu->AppendSeparator();
    ACTION_MENU* convertSubmenu = new ACTION_MENU( false, selTool );
    convertSubmenu->SetTitle( _( "Con&vert" ) );
    convertSubmenu->SetIcon( BITMAPS::convert );
    if( ADVANCED_CFG::GetCfg().m_EnablePcbDesignBlocks )
    {
        convertSubmenu->Add( PCB_ACTIONS::saveSelectionAsDesignBlock, ACTION_MENU::NORMAL,
                             _( "Create Snippet from selected objects" ) );
        convertSubmenu->AppendSeparator();
    }

    convertSubmenu->Add( PCB_ACTIONS::convertToZone,    ACTION_MENU::NORMAL,
                         _( "Create Pol&ygon from Selected Primitives" ) );
    convertSubmenu->Add( PCB_ACTIONS::convertToPoly,    ACTION_MENU::NORMAL,
                         _( "Create Re&gion from Selected Primitives" ) );
    convertSubmenu->AppendSeparator();
    convertSubmenu->Add( PCB_ACTIONS::convertToKeepout, ACTION_MENU::NORMAL,
                         _( "Convert Selected Primitives to &Keepouts" ) );

    // KiCad-only
    convertSubmenu->AppendSeparator();
    convertSubmenu->Add( PCB_ACTIONS::convertToLines );
    convertSubmenu->Add( PCB_ACTIONS::convertToArc );
    convertSubmenu->Add( PCB_ACTIONS::convertToTracks );
    toolsMenu->Add( convertSubmenu );

    toolsMenu->Add( PCB_ACTIONS::removeUnusedPads, ACTION_MENU::NORMAL, _( "Remove Unused Pad Shapes..." ) );
    toolsMenu->Add( PCB_ACTIONS::editTeardrops, ACTION_MENU::NORMAL, _( "T&eardrops..." ) );

    toolsMenu->AppendSeparator();
    toolsMenu->Add( ACTIONS::openPreferences, ACTION_MENU::NORMAL, _( "&Preferences..." ) );

    // KiCad-only
    toolsMenu->AppendSeparator();
    toolsMenu->Add( ACTIONS::prevMarker );
    toolsMenu->Add( ACTIONS::nextMarker );
    toolsMenu->Add( ACTIONS::excludeMarker );

    ACTION_MENU* multichannelSubmenu = new ACTION_MENU( false, selTool );
    multichannelSubmenu->SetTitle( _( "Multi-Channel" ) );
    multichannelSubmenu->SetIcon( BITMAPS::mode_module );
    multichannelSubmenu->Add( PCB_ACTIONS::generatePlacementRuleAreas );
    multichannelSubmenu->Add( PCB_ACTIONS::repeatLayout );
    toolsMenu->Add( multichannelSubmenu );

    toolsMenu->Add( ACTIONS::showFootprintEditor );
    toolsMenu->Add( PCB_ACTIONS::migrate3DModels );
    toolsMenu->Add( PCB_ACTIONS::collect3DModels );

    if( ADVANCED_CFG::GetCfg().m_EnableGenerators )
    {
        toolsMenu->Add( PCB_ACTIONS::generatorsShowManager );
        toolsMenu->Add( PCB_ACTIONS::regenerateAll );
        toolsMenu->Add( PCB_ACTIONS::regenerateSelected );
    }

    toolsMenu->Add( PCB_ACTIONS::cleanupTracksAndVias );
    toolsMenu->Add( PCB_ACTIONS::cleanupGraphics );
    toolsMenu->Add( PCB_ACTIONS::repairBoard );
    toolsMenu->Add( ACTIONS::showCalculatorTools );

    if( SCRIPTING::IsWxAvailable() )
        toolsMenu->Add( PCB_ACTIONS::showPythonConsole );

    ACTION_MENU* submenuActionPlugins = new ACTION_MENU( false, selTool );
    submenuActionPlugins->SetTitle( _( "External Plugins" ) );
    submenuActionPlugins->SetIcon( BITMAPS::puzzle_piece );

    submenuActionPlugins->Add( ACTIONS::pluginsReload );
    submenuActionPlugins->Add( PCB_ACTIONS::pluginsShowFolder );

    // Populate the Action Plugin sub-menu: Must be done before Add
    // Since the object is cloned by Add
    submenuActionPlugins->AppendSeparator();
    buildActionPluginMenus( submenuActionPlugins );

    toolsMenu->Add( submenuActionPlugins );

    AddMenuLanguageList( toolsMenu, selTool );

    //-- Route Menu ----------------------------------------------------------
    //
    ACTION_MENU* routeMenu = new ACTION_MENU( false, selTool );

    routeMenu->Add( PCB_ACTIONS::routerAutorouteSelected, ACTION_MENU::NORMAL, _( "ActiveRoute" ) );
    routeMenu->Add( PCB_ACTIONS::routeSingleTrack, ACTION_MENU::NORMAL, _( "Interactive Rou&ting" ) );
    routeMenu->Add( PCB_ACTIONS::routeDiffPair, ACTION_MENU::NORMAL,
                    _( "Interactive D&ifferential Pair Routing" ) );
    routeMenu->Add( PCB_ACTIONS::routerRouteSelected, ACTION_MENU::NORMAL, _( "&Quick Routing" ) );

    routeMenu->AppendSeparator();
    routeMenu->Add( PCB_ACTIONS::tuneSingleTrack, ACTION_MENU::NORMAL, _( "Inte&ractive Length Tuning" ) );
    routeMenu->Add( PCB_ACTIONS::tuneDiffPair, ACTION_MENU::NORMAL,
                    _( "Interactive Diff &Pair Length Tuning" ) );

    routeMenu->AppendSeparator();
    ALTIUM_SUBMENU* autoRouteSubmenu = new ALTIUM_SUBMENU( selTool );
    autoRouteSubmenu->SetTitle( _( "&Auto Route" ) );
    autoRouteSubmenu->Add( PCB_ACTIONS::autorouteAll,                ACTION_MENU::NORMAL, _( "&All..." ) );
    autoRouteSubmenu->AppendSeparator();
    autoRouteSubmenu->Add( PCB_ACTIONS::autorouteNet,                ACTION_MENU::NORMAL, _( "&Net" ) );
    autoRouteSubmenu->AddPlain( PCB_ACTIONS::routerRouteSelected,     _( "&Connection" ) );
    autoRouteSubmenu->AddPlain( PCB_ACTIONS::routerAutorouteSelected, _( "C&omponent" ) );
    autoRouteSubmenu->AppendSeparator();
    autoRouteSubmenu->AddPlain( PCB_ACTIONS::routerSettingsDialog, _( "&Setup..." ) );
    routeMenu->Add( autoRouteSubmenu );

    routeMenu->AppendSeparator();
    ALTIUM_SUBMENU* unrouteSubmenu = new ALTIUM_SUBMENU( selTool );
    unrouteSubmenu->SetTitle( _( "&Un-Route" ) );
    unrouteSubmenu->SetIcon( BITMAPS::general_deletions );
    unrouteSubmenu->AddPlain( PCB_ACTIONS::globalDeletions, _( "&All..." ) );
    unrouteSubmenu->Add( PCB_ACTIONS::unrouteNet,           ACTION_MENU::NORMAL, _( "&Net" ) );
    unrouteSubmenu->AddPlain( PCB_ACTIONS::unrouteSegment,  _( "&Connection" ) );
    unrouteSubmenu->Add( PCB_ACTIONS::unrouteSelected, ACTION_MENU::NORMAL, _( "C&omponent" ) );
    routeMenu->Add( unrouteSubmenu );

    // KiCad-only
    routeMenu->AppendSeparator();
    routeMenu->Add( PCB_ACTIONS::tuneSkew );
    routeMenu->Add( PCB_ACTIONS::regenerateAllTuning );
    routeMenu->Add( PCB_ACTIONS::selectLayerPair );
    routeMenu->Add( PCB_ACTIONS::routerSettingsDialog );

    //-- Reports Menu --------------------------------------------------------
    //
    ACTION_MENU* reportsMenu = new ACTION_MENU( false, selTool );

    reportsMenu->Add( PCB_ACTIONS::generateBOM, ACTION_MENU::NORMAL, _( "Bill of &Materials..." ) );

    ACTION_MENU* projectReportsSubmenu = new ACTION_MENU( false, selTool );
    projectReportsSubmenu->SetTitle( _( "Project &Reports" ) );
    projectReportsSubmenu->SetIcon( BITMAPS::post_compo );
    projectReportsSubmenu->Add( PCB_OUTPUTS_ACTIONS::activeBom, ACTION_MENU::NORMAL,
                                _( "B&ill of Materials" ) );
    projectReportsSubmenu->Add( PCB_ACTIONS::showFootprintAssociations, ACTION_MENU::NORMAL,
                                _( "&Component Cross Reference" ) );

    // KiCad-only
    projectReportsSubmenu->AppendSeparator();
    projectReportsSubmenu->Add( PCB_ACTIONS::diffFootprint );
    reportsMenu->Add( projectReportsSubmenu );

    reportsMenu->AppendSeparator();
    reportsMenu->Add( ACTIONS::measureTool, ACTION_MENU::NORMAL, _( "Measure &Distance" ) );
    reportsMenu->Add( PCB_ACTIONS::inspectClearance, ACTION_MENU::NORMAL, _( "Measure &Primitives" ) );
    reportsMenu->Add( PCB_ACTIONS::measureSelected, ACTION_MENU::NORMAL,
                      _( "Measure &Selected Objects" ) );

    reportsMenu->AppendSeparator();
    reportsMenu->Add( PCB_ACTIONS::boardStatistics, ACTION_MENU::NORMAL, _( "&Board Information..." ) );

    // KiCad-only
    reportsMenu->AppendSeparator();
    reportsMenu->Add( PCB_ACTIONS::inspectConstraints );


    //--MenuBar -----------------------------------------------------------
    //
    menuBar->Append( fileMenu,    _( "&File" ) );
    menuBar->Append( editMenu,    _( "&Edit" ) );
    menuBar->Append( viewMenu,    _( "&View" ) );
    menuBar->Append( projectMenu, _( "Proje&ct" ) );
    menuBar->Append( placeMenu,   _( "&Place" ) );
    menuBar->Append( designMenu,  _( "&Design" ) );
    menuBar->Append( toolsMenu,   _( "&Tools" ) );
    menuBar->Append( routeMenu,   _( "Ro&ute" ) );
    menuBar->Append( reportsMenu, _( "&Reports" ) );
    menuBar->Append( new WINDOW_MENU( this ), _( "&Window" ) );
    AddStandardHelpMenu( menuBar );

    SetMenuBar( menuBar );
    delete oldMenuBar;

}
