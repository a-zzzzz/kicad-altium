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

#include "pcb_outputs_tool.h"

#include <bitmaps.h>
#include <confirm.h>
#include <kiface_base.h>
#include <kiway.h>
#include <kiway_player.h>
#include <outputs/output_job_link.h>
#include <pcb_edit_frame.h>
#include <tool/tool_manager.h>
#include <tools/pcb_actions.h>


PCB_OUTPUTS_TOOL::PCB_OUTPUTS_TOOL() :
        PCB_TOOL_BASE( "pcbnew.Outputs" )
{
}


int PCB_OUTPUTS_TOOL::NewOutputJob( const TOOL_EVENT& aEvent )
{
    PCB_EDIT_FRAME* editFrame = getEditFrame<PCB_EDIT_FRAME>();
    OUTPUT_JOB_LINK::New( editFrame, editFrame );
    return 0;
}


int PCB_OUTPUTS_TOOL::OpenOutputJob( const TOOL_EVENT& aEvent )
{
    PCB_EDIT_FRAME* editFrame = getEditFrame<PCB_EDIT_FRAME>();
    OUTPUT_JOB_LINK::OpenFromProject( editFrame, editFrame );
    return 0;
}


int PCB_OUTPUTS_TOOL::RunInSchematic( const TOOL_EVENT& aEvent )
{
    PCB_EDIT_FRAME* editFrame = getEditFrame<PCB_EDIT_FRAME>();

    if( Kiface().IsSingle() )
    {
        DisplayErrorMessage( editFrame, _( "Variants and the ActiveBOM live in the schematic.  "
                                           "Open this design through the KiCad project manager." ) );
        return 0;
    }

    std::string action = aEvent.IsAction( &PCB_OUTPUTS_ACTIONS::activeBom )
                                 ? "eeschema.Outputs.activeBom"
                                 : "eeschema.Outputs.variantManager";

    // Opens (and loads) the schematic editor if needed
    m_toolMgr->RunAction( PCB_ACTIONS::showEeschema );

    KIWAY_PLAYER* sch = editFrame->Kiway().Player( FRAME_SCH, false );

    if( !sch || !sch->GetToolManager() )
        return 0;

    // Let the schematic editor finish showing before its dialog opens
    sch->CallAfter(
            [sch, action]()
            {
                sch->GetToolManager()->RunAction( action );
            } );

    return 0;
}


void PCB_OUTPUTS_TOOL::setTransitions()
{
    Go( &PCB_OUTPUTS_TOOL::NewOutputJob,   PCB_OUTPUTS_ACTIONS::newOutputJob.MakeEvent() );
    Go( &PCB_OUTPUTS_TOOL::OpenOutputJob,  PCB_OUTPUTS_ACTIONS::openOutputJob.MakeEvent() );
    Go( &PCB_OUTPUTS_TOOL::RunInSchematic, PCB_OUTPUTS_ACTIONS::activeBom.MakeEvent() );
    Go( &PCB_OUTPUTS_TOOL::RunInSchematic, PCB_OUTPUTS_ACTIONS::variantManager.MakeEvent() );
}


// Statically-defined actions keep their strings untranslated (translated in the getters),
// as in sch_actions.cpp / pcb_actions.cpp.
#undef _
#define _(s) s

TOOL_ACTION PCB_OUTPUTS_ACTIONS::newOutputJob( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Outputs.newOutputJob" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Output Job File" ) )
        .Tooltip( _( "Create a new Output Job file (.kicad_jobset) in the project and open it" ) )
        .Icon( BITMAPS::jobset ) );

TOOL_ACTION PCB_OUTPUTS_ACTIONS::openOutputJob( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Outputs.openOutputJob" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Output Jobs..." ) )
        .Tooltip( _( "Open one of the project's Output Job files" ) )
        .Icon( BITMAPS::jobset_open ) );

TOOL_ACTION PCB_OUTPUTS_ACTIONS::activeBom( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Outputs.activeBom" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "ActiveBOM..." ) )
        .Tooltip( _( "Open the schematic's ActiveBOM view" ) )
        .Icon( BITMAPS::post_bom ) );

TOOL_ACTION PCB_OUTPUTS_ACTIONS::variantManager( TOOL_ACTION_ARGS()
        .Name( "pcbnew.Outputs.variantManager" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Variants..." ) )
        .Tooltip( _( "Variant Management (variants are defined in the schematic)" ) )
        .Icon( BITMAPS::options_generic ) );
