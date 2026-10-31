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

#include "pcb_project_tool.h"

#include <bitmaps.h>
#include <board.h>
#include <confirm.h>
#include <document_windows.h>
#include <footprint.h>
#include <frame_type.h>
#include <kiface_base.h>
#include <kiway.h>
#include <kiway_player.h>
#include <mail_type.h>
#include <messages_store.h>
#include <netlist_reader/board_netlist_updater.h>
#include <netlist_reader/netlist_reader.h>
#include <netlist_reader/pcb_netlist.h>
#include <pcb_edit_frame.h>
#include <project.h>
#include <reporter.h>
#include <richio.h>
#include <string_utils.h>
#include <tool/actions.h>
#include <tool/tool_manager.h>
#include <widgets/messages_panel.h>
#include <wildcards_and_files_ext.h>

#include <wx/aui/framemanager.h>
#include <wx/filename.h>


namespace
{
/// Identity of the parity rows in the Messages panel
const int s_parityOrigin = 0;

const char* const VALIDATE_ACTION = "eeschema.Project.validate";


void showMessagesPane( wxWindow* aFrame )
{
    wxAuiManager* mgr = aFrame ? wxAuiManager::GetManager( aFrame ) : nullptr;

    if( !mgr )
        return;

    wxAuiPaneInfo& pane = mgr->GetPane( MESSAGES_PANEL::PaneName() );

    if( !pane.IsOk() )      // GetPane() returns a shared null pane: never write to it
        return;

    if( !pane.IsShown() )
    {
        pane.Show();
        mgr->Update();
    }

    if( MESSAGES_PANEL* panel = dynamic_cast<MESSAGES_PANEL*>( pane.window ) )
        panel->RefreshMarkers();
}


/// True if \p aText names \p aRef as a whole word ("R1" but not "R10").
bool mentions( const wxString& aText, const wxString& aRef )
{
    if( aRef.IsEmpty() )
        return false;

    size_t pos = 0;

    while( ( pos = aText.find( aRef, pos ) ) != wxString::npos )
    {
        size_t end = pos + aRef.length();
        bool   startOk = pos == 0 || !wxIsalnum( aText[pos - 1] );
        bool   endOk = end >= aText.length() || !wxIsalnum( aText[end] );

        if( startOk && endOk )
            return true;

        pos = end;
    }

    return false;
}


/// Collects the dry-run report of the netlist updater: every change it would make is a
/// difference between the schematic and the board.
class PARITY_REPORTER : public REPORTER
{
public:
    REPORTER& Report( const wxString& aText, SEVERITY aSeverity ) override
    {
        REPORTER::Report( aText, aSeverity );

        // Totals, progress notes and blank spacer lines are not differences
        if( wxString( aText ).Trim().Trim( false ).IsEmpty() )
            return *this;

        if( aSeverity == RPT_SEVERITY_ACTION || aSeverity == RPT_SEVERITY_WARNING
            || aSeverity == RPT_SEVERITY_ERROR )
        {
            m_rows.emplace_back( aSeverity, UnescapeHTML( aText ) );
        }

        return *this;
    }

    bool HasMessage() const override { return !m_rows.empty(); }

    std::vector<std::pair<SEVERITY, wxString>> m_rows;
};
} // namespace


PCB_PROJECT_TOOL::PCB_PROJECT_TOOL() :
        PCB_TOOL_BASE( "pcbnew.Project" )
{
}


void PCB_PROJECT_TOOL::Reset( RESET_REASON aReason )
{
    PCB_TOOL_BASE::Reset( aReason );

    // Project > Validate PCB Project names the project, as Altium's does.  The menu bar is
    // built before a project is loaded, so rebuild it once the project is known.
    PCB_EDIT_FRAME* frame = getEditFrame<PCB_EDIT_FRAME>();

    if( frame && ( aReason == MODEL_RELOAD || aReason == SUPERMODEL_RELOAD ) )
    {
        wxString project = frame->Prj().GetProjectName();

        if( project != m_menuProject )
        {
            m_menuProject = project;
            frame->ReCreateMenuBar();
        }
    }
}


wxString PCB_PROJECT_TOOL::ValidateLabel( PCB_EDIT_FRAME* aFrame )
{
    wxString project = aFrame ? aFrame->Prj().GetProjectName() : wxString();

    project.Replace( wxS( "&" ), wxS( "&&" ) );    // not a mnemonic

    if( project.IsEmpty() || Kiface().IsSingle() )
        return _( "Validate P&CB Project" );

    return wxString::Format( _( "Validate P&CB Project %s" ),
                             project + wxS( "." ) + FILEEXT::ProjectFileExtension );
}


int PCB_PROJECT_TOOL::ValidateProject( const TOOL_EVENT& aEvent )
{
    PCB_EDIT_FRAME* frame = getEditFrame<PCB_EDIT_FRAME>();

    if( Kiface().IsSingle() )
    {
        DisplayErrorMessage( frame, _( "Validating a PCB project needs its schematic.  Open this "
                                       "design through the KiCad project manager." ) );
        return 0;
    }

    KIWAY_PLAYER* sch = frame->Kiway().Player( FRAME_SCH, false );

    // The schematic is checked by the schematic editor: load it if it isn't open yet
    if( !sch || !DOCUMENT_WINDOWS::IsOpen( sch ) )
    {
        if( frame->TestStandalone() <= 0 )
            return 0;

        sch = frame->Kiway().Player( FRAME_SCH, false );
    }

    if( !sch || !sch->GetToolManager() )
        return 0;

    // ERC, then parity: the schematic's validation calls back CheckParity(), which this tool
    // can only take once this handler has returned
    frame->CallAfter(
            [frame, sch]()
            {
                sch->GetToolManager()->RunAction( std::string( VALIDATE_ACTION ) );
                showMessagesPane( frame );
            } );

    return 0;
}


int PCB_PROJECT_TOOL::CheckParity( const TOOL_EVENT& aEvent )
{
    PCB_EDIT_FRAME* frame = getEditFrame<PCB_EDIT_FRAME>();
    BOARD*          board = frame->GetBoard();

    if( Kiface().IsSingle() || !board )
        return 0;

    wxString document = wxFileName( board->GetFileName() ).GetFullName();

    std::vector<PROJECT_MESSAGE> rows;

    auto addRow =
            [&]( SEVERITY aSeverity, const wxString& aText )
            {
                PROJECT_MESSAGE msg;
                msg.severity = aSeverity;
                msg.document = document;
                msg.source = _( "Compiler" );    // Altium lists all validation rows as Compiler
                msg.text = aText;
                msg.key = wxS( "parity:" ) + aText;
                msg.owner = frame;

                // Double-click: select the footprint the row names, if any
                FOOTPRINT* target = nullptr;

                for( FOOTPRINT* fp : board->Footprints() )
                {
                    if( mentions( aText, fp->GetReference() ) )
                    {
                        target = fp;
                        break;
                    }
                }

                if( target )
                {
                    KIID           id = target->m_Uuid;
                    PCB_EDIT_FRAME* f = frame;

                    msg.activate =
                            [f, id]()
                            {
                                BOARD_ITEM* item = f->GetBoard()->ResolveItem( id, true );

                                if( !item )
                                    return;

                                f->GetToolManager()->RunAction( ACTIONS::selectionClear );
                                f->GetToolManager()->RunAction<EDA_ITEM*>( ACTIONS::selectItem,
                                                                           item );
                                f->GetToolManager()->RunAction( ACTIONS::zoomFitSelection );
                            };
                }

                rows.push_back( std::move( msg ) );
            };

    // The schematic's netlist, without its annotation prompt (validation reports that)
    std::string payload;
    frame->Kiway().ExpressMail( FRAME_SCH, MAIL_SCH_GET_NETLIST, payload, frame );

    NETLIST netlist;
    bool    loaded = false;

    if( !payload.empty() )
    {
        try
        {
            STRING_LINE_READER* lineReader = new STRING_LINE_READER( payload,
                                                                     _( "Eeschema netlist" ) );
            KICAD_NETLIST_READER netlistReader( lineReader, &netlist );
            netlistReader.LoadNetlist();
            loaded = true;
        }
        catch( const IO_ERROR& e )
        {
            addRow( RPT_SEVERITY_ERROR,
                    wxString::Format( _( "Could not read the schematic netlist: %s" ),
                                      e.What() ) );
        }
    }
    else
    {
        addRow( RPT_SEVERITY_ERROR, _( "Could not get the schematic netlist." ) );
    }

    if( loaded )
    {
        netlist.SortByReference();
        netlist.SetFindByTimeStamp( true );
        netlist.SetReplaceFootprints( true );

        // Same comparison as Design > Import Changes From Schematic, without applying it
        PARITY_REPORTER       reporter;
        BOARD_NETLIST_UPDATER updater( frame, board );
        updater.SetReporter( &reporter );
        updater.SetIsDryRun( true );
        updater.SetLookupByTimestamp( true );
        updater.SetDeleteUnusedFootprints( true );
        updater.SetReplaceFootprints( true );
        updater.SetTransferGroups( false );
        updater.SetOverrideLocks( true );
        updater.SetUpdateFields( true );
        updater.SetRemoveExtraFields( false );
        updater.UpdateNetlist( netlist );

        for( const auto& [severity, text] : reporter.m_rows )
        {
            // A pending change is a difference: a warning, as Altium's compiler reports them
            addRow( severity == RPT_SEVERITY_ERROR ? RPT_SEVERITY_ERROR : RPT_SEVERITY_WARNING,
                    severity == RPT_SEVERITY_ACTION
                            ? wxString::Format( _( "PCB differs from schematic: %s" ), text )
                            : text );
        }
    }

    MESSAGES_STORE::SetMarkers( &s_parityOrigin, std::move( rows ) );
    return 0;
}


void PCB_PROJECT_TOOL::setTransitions()
{
    Go( &PCB_PROJECT_TOOL::ValidateProject, PCB_PROJECT_ACTIONS::validateProject.MakeEvent() );
    Go( &PCB_PROJECT_TOOL::CheckParity,     PCB_PROJECT_ACTIONS::checkParity.MakeEvent() );
}


// Statically-defined actions keep their strings untranslated (translated in the getters),
// as in sch_actions.cpp / pcb_actions.cpp.
#undef _
#define _(s) s

TOOL_ACTION PCB_PROJECT_ACTIONS::validateProject( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Project.validate" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Validate PCB Project" ) )
        .Tooltip( _( "Check the project (electrical rules and schematic / PCB parity) and list "
                     "the results in the Messages panel" ) )
        .Icon( BITMAPS::erc ) );

TOOL_ACTION PCB_PROJECT_ACTIONS::checkParity( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Project.checkParity" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Check Schematic Parity" ) )
        .Tooltip( _( "List the differences between the board and its schematic in the Messages "
                     "panel" ) ) );
