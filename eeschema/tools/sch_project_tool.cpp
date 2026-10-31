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

#include "sch_project_tool.h"

#include <bitmaps.h>
#include <dialogs/dialog_erc.h>
#include <erc/erc.h>
#include <erc/erc_item.h>
#include <frame_type.h>
#include <kiface_base.h>
#include <kiway.h>
#include <kiway_player.h>
#include <messages_store.h>
#include <project.h>
#include <sch_marker.h>
#include <sch_reference_list.h>
#include <sch_screen.h>
#include <sch_draw_panel.h>
#include <sch_view.h>
#include <schematic.h>
#include <tool/actions.h>
#include <tool/tool_manager.h>
#include <widgets/messages_panel.h>
#include <wildcards_and_files_ext.h>

#include <wx/aui/framemanager.h>
#include <wx/filename.h>
#include <wx/utils.h>


namespace
{
/// Identity of the summary rows a validation adds to the Messages panel
const int s_validateOrigin = 0;

const char* const PARITY_ACTION = "pcbnew.Project.checkParity";


MESSAGES_PANEL* messagesPanel( wxWindow* aFrame, bool aShow )
{
    wxAuiManager* mgr = aFrame ? wxAuiManager::GetManager( aFrame ) : nullptr;

    if( !mgr )
        return nullptr;

    wxAuiPaneInfo& pane = mgr->GetPane( MESSAGES_PANEL::PaneName() );

    if( !pane.IsOk() )      // GetPane() returns a shared null pane: never write to it
        return nullptr;

    if( aShow && !pane.IsShown() )
    {
        pane.Show();
        mgr->Update();
    }

    return dynamic_cast<MESSAGES_PANEL*>( pane.window );
}
} // namespace


SCH_PROJECT_TOOL::SCH_PROJECT_TOOL() :
        SCH_TOOL_BASE<SCH_EDIT_FRAME>( "eeschema.Project" )
{
}


void SCH_PROJECT_TOOL::Reset( RESET_REASON aReason )
{
    SCH_TOOL_BASE::Reset( aReason );

    // The Project and Design menus name the project and board, as Altium's do.  The menu bar
    // is built before a project is loaded, so rebuild it once the project is known.
    if( m_frame && ( aReason == MODEL_RELOAD || aReason == SUPERMODEL_RELOAD ) )
    {
        wxString project = m_frame->Prj().GetProjectName();

        if( project != m_menuProject )
        {
            m_menuProject = project;
            m_frame->ReCreateMenuBar();
        }
    }
}


wxString SCH_PROJECT_TOOL::ValidateLabel( SCH_EDIT_FRAME* aFrame )
{
    wxString project = aFrame ? aFrame->Prj().GetProjectName() : wxString();

    project.Replace( wxS( "&" ), wxS( "&&" ) );    // not a mnemonic

    if( project.IsEmpty() || Kiface().IsSingle() )
        return _( "Validate P&CB Project" );

    return wxString::Format( _( "Validate P&CB Project %s" ),
                             project + wxS( "." ) + FILEEXT::ProjectFileExtension );
}


wxString SCH_PROJECT_TOOL::UpdatePcbLabel( SCH_EDIT_FRAME* aFrame )
{
    wxString project = aFrame ? aFrame->Prj().GetProjectName() : wxString();

    project.Replace( wxS( "&" ), wxS( "&&" ) );    // not a mnemonic

    if( project.IsEmpty() || Kiface().IsSingle() )
        return _( "&Update PCB Document..." );

    return wxString::Format( _( "&Update PCB Document %s" ),
                             project + wxS( "." ) + FILEEXT::KiCadPcbFileExtension );
}


int SCH_PROJECT_TOOL::runErc()
{
    SCHEMATIC* sch = &m_frame->Schematic();

    sch->RecordERCExclusions();

    // Markers are deleted below; their views go with them (VIEW_ITEM's destructor)
    SCH_SCREENS screens( sch->Root() );
    screens.DeleteAllMarkers( MARKER_BASE::MARKER_ERC, true );

    // Same tests as Inspect > ERC, without the dialog
    int unannotated = m_frame->CheckAnnotate(
            []( ERCE_T aType, const wxString& aMsg, SCH_REFERENCE* aItemA, SCH_REFERENCE* aItemB )
            {
                std::shared_ptr<ERC_ITEM> ercItem = ERC_ITEM::Create( aType );
                ercItem->SetErrorMessage( aMsg );

                if( aItemB )
                    ercItem->SetItems( aItemA->GetSymbol(), aItemB->GetSymbol() );
                else
                    ercItem->SetItems( aItemA->GetSymbol() );

                SCH_MARKER* marker = new SCH_MARKER( std::move( ercItem ),
                                                     aItemA->GetSymbol()->GetPosition() );
                aItemA->GetSheetPath().LastScreen()->Append( marker );
            },
            ANNOTATE_ALL, true, SYMBOL_FILTER_NON_POWER );

    ERC_TESTER tester( sch );
    tester.RunTests( m_frame->GetCanvas()->GetView()->GetDrawingSheet(), m_frame,
                     m_frame->Kiway().KiFACE( KIWAY::FACE_CVPCB ), &m_frame->Prj(), nullptr );

    // Show the new markers of the sheet on screen
    KIGFX::SCH_VIEW* view = m_frame->GetCanvas()->GetView();

    for( SCH_ITEM* marker : m_frame->GetScreen()->Items().OfType( SCH_MARKER_T ) )
    {
        view->Remove( marker );
        view->Add( marker );
    }

    m_frame->GetCanvas()->Refresh();
    return unannotated;
}


int SCH_PROJECT_TOOL::ValidateProject( const TOOL_EVENT& aEvent )
{
    SCH_EDIT_FRAME* frame = m_frame;

    // An open ERC dialog lists the markers about to be replaced: close it (Altium has none)
    wxCommandEvent closeErc( EDA_EVT_CLOSE_ERC_DIALOG, wxID_ANY );
    frame->ProcessWindowEvent( closeErc );

    m_toolMgr->RunAction( ACTIONS::selectionClear );

    int unannotated = 0;

    {
        wxBusyCursor busy;
        unannotated = runErc();
    }

    // ERC rows first: let the schematic's Messages panel read the new markers now
    if( MESSAGES_PANEL* panel = messagesPanel( frame, true ) )
        panel->RefreshMarkers();

    int ercErrors = 0;
    int ercWarnings = 0;
    SCH_SCREENS screens( frame->Schematic().Root() );

    for( SCH_SCREEN* screen = screens.GetFirst(); screen; screen = screens.GetNext() )
    {
        for( SCH_ITEM* item : screen->Items().OfType( SCH_MARKER_T ) )
        {
            SCH_MARKER* marker = static_cast<SCH_MARKER*>( item );

            if( marker->GetMarkerType() != MARKER_BASE::MARKER_ERC || marker->IsExcluded() )
                continue;

            if( marker->GetSeverity() == RPT_SEVERITY_ERROR )
                ercErrors++;
            else if( marker->GetSeverity() == RPT_SEVERITY_WARNING )
                ercWarnings++;
        }
    }

    // Schematic / PCB parity, checked by the PCB editor against this schematic's netlist
    std::vector<PROJECT_MESSAGE> summary;
    wxString projectFile = frame->Prj().GetProjectName() + wxS( "." )
                           + FILEEXT::ProjectFileExtension;

    auto addSummary =
            [&]( SEVERITY aSeverity, const wxString& aText )
            {
                PROJECT_MESSAGE msg;
                msg.severity = aSeverity;
                msg.document = projectFile;
                msg.source = _( "Compiler" );
                msg.text = aText;
                msg.owner = frame;
                summary.push_back( std::move( msg ) );
            };

    KIWAY_PLAYER* pcb = Kiface().IsSingle() ? nullptr
                                            : frame->Kiway().Player( FRAME_PCB_EDITOR, false );
    bool parityRun = false;

    if( Kiface().IsSingle() )
    {
        // Stand-alone: no board to compare with
    }
    else if( unannotated > 0 )
    {
        addSummary( RPT_SEVERITY_WARNING,
                    _( "Schematic is not fully annotated: schematic / PCB parity not checked." ) );
    }
    else if( !pcb || !pcb->GetToolManager() )
    {
        addSummary( RPT_SEVERITY_INFO,
                    _( "PCB document is not open: schematic / PCB parity not checked." ) );
    }
    else
    {
        wxBusyCursor busy;
        pcb->GetToolManager()->RunAction( std::string( PARITY_ACTION ) );
        parityRun = true;
    }

    int parityErrors = 0;
    int parityWarnings = 0;

    if( parityRun )
    {
        for( const PROJECT_MESSAGE& msg : MESSAGES_STORE::Messages() )
        {
            if( !msg.key.StartsWith( wxS( "parity:" ) ) )
                continue;

            if( msg.severity == RPT_SEVERITY_ERROR )
                parityErrors++;
            else if( msg.severity == RPT_SEVERITY_WARNING )
                parityWarnings++;
        }
    }

    int errors = ercErrors + parityErrors;
    int warnings = ercWarnings + parityWarnings;

    if( errors == 0 && warnings == 0 )
    {
        addSummary( RPT_SEVERITY_INFO, _( "Compile successful, no errors found." ) );
    }
    else
    {
        addSummary( errors ? RPT_SEVERITY_ERROR : RPT_SEVERITY_WARNING,
                    wxString::Format( _( "Validation found %d error(s) and %d warning(s)." ),
                                      errors, warnings ) );
    }

    MESSAGES_STORE::SetMarkers( &s_validateOrigin, std::move( summary ) );

    frame->GetCanvas()->Refresh();
    return 0;
}


int SCH_PROJECT_TOOL::SmartPaste( const TOOL_EVENT& aEvent )
{
    // Paste Special is interactive (the pasted items follow the cursor): run it on its own
    m_toolMgr->PostAction( ACTIONS::pasteSpecial );
    return 0;
}


void SCH_PROJECT_TOOL::setTransitions()
{
    Go( &SCH_PROJECT_TOOL::SmartPaste,      SCH_PROJECT_ACTIONS::smartPaste.MakeEvent() );
    Go( &SCH_PROJECT_TOOL::ValidateProject, SCH_PROJECT_ACTIONS::validateProject.MakeEvent() );
}


// Statically-defined actions keep their strings untranslated (translated in the getters),
// as in sch_actions.cpp / pcb_actions.cpp.
#undef _
#define _(s) s

TOOL_ACTION SCH_PROJECT_ACTIONS::validateProject( TOOL_ACTION_ARGS()
        .Name( "eeschema.Project.validate" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Validate PCB Project" ) )
        .Tooltip( _( "Check the project (electrical rules and schematic / PCB parity) and list "
                     "the results in the Messages panel" ) )
        .Icon( BITMAPS::erc ) );

TOOL_ACTION SCH_PROJECT_ACTIONS::smartPaste( TOOL_ACTION_ARGS()
        .Name( "eeschema.Project.smartPaste" )
        .Scope( AS_GLOBAL )
        .DefaultHotkey( MD_CTRL + MD_SHIFT + 'V' )
        .FriendlyName( _( "Smart Paste..." ) )
        .Tooltip( _( "Paste item(s) from the clipboard with options" ) )
        .Icon( BITMAPS::paste_special ) );
