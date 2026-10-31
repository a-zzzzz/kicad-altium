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

#include "sch_outputs_tool.h"

#include <bitmaps.h>
#include <dialogs/dialog_active_bom.h>
#include <dialogs/dialog_variant_manager.h>
#include <outputs/output_job_link.h>
#include <tool/tool_manager.h>


SCH_OUTPUTS_TOOL::SCH_OUTPUTS_TOOL() :
        SCH_TOOL_BASE<SCH_EDIT_FRAME>( "eeschema.Outputs" )
{
}


int SCH_OUTPUTS_TOOL::NewOutputJob( const TOOL_EVENT& aEvent )
{
    OUTPUT_JOB_LINK::New( m_frame, m_frame );
    return 0;
}


int SCH_OUTPUTS_TOOL::OpenOutputJob( const TOOL_EVENT& aEvent )
{
    OUTPUT_JOB_LINK::OpenFromProject( m_frame, m_frame );
    return 0;
}


int SCH_OUTPUTS_TOOL::ActiveBom( const TOOL_EVENT& aEvent )
{
    if( !m_activeBom )
        m_activeBom = new DIALOG_ACTIVE_BOM( m_frame );

    DIALOG_ACTIVE_BOM* dlg = static_cast<DIALOG_ACTIVE_BOM*>( m_activeBom.get() );

    dlg->Rebuild();
    dlg->Show( true );

    if( dlg->IsIconized() )
        dlg->Iconize( false );

    dlg->Raise();
    return 0;
}


int SCH_OUTPUTS_TOOL::VariantManager( const TOOL_EVENT& aEvent )
{
    DIALOG_VARIANT_MANAGER dlg( m_frame );
    dlg.ShowModal();

    // The ActiveBOM shows variant data; keep it current
    if( m_activeBom )
        static_cast<DIALOG_ACTIVE_BOM*>( m_activeBom.get() )->Rebuild();

    return 0;
}


void SCH_OUTPUTS_TOOL::setTransitions()
{
    Go( &SCH_OUTPUTS_TOOL::NewOutputJob,   SCH_OUTPUTS_ACTIONS::newOutputJob.MakeEvent() );
    Go( &SCH_OUTPUTS_TOOL::OpenOutputJob,  SCH_OUTPUTS_ACTIONS::openOutputJob.MakeEvent() );
    Go( &SCH_OUTPUTS_TOOL::ActiveBom,      SCH_OUTPUTS_ACTIONS::activeBom.MakeEvent() );
    Go( &SCH_OUTPUTS_TOOL::VariantManager, SCH_OUTPUTS_ACTIONS::variantManager.MakeEvent() );
}


// Statically-defined actions keep their strings untranslated (translated in the getters),
// as in sch_actions.cpp / pcb_actions.cpp.
#undef _
#define _(s) s

TOOL_ACTION SCH_OUTPUTS_ACTIONS::newOutputJob( TOOL_ACTION_ARGS()
        .Name( "eeschema.Outputs.newOutputJob" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Output Job File" ) )
        .Tooltip( _( "Create a new Output Job file (.kicad_jobset) in the project and open it" ) )
        .Icon( BITMAPS::jobset ) );

TOOL_ACTION SCH_OUTPUTS_ACTIONS::openOutputJob( TOOL_ACTION_ARGS()
        .Name( "eeschema.Outputs.openOutputJob" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Output Jobs..." ) )
        .Tooltip( _( "Open one of the project's Output Job files" ) )
        .Icon( BITMAPS::jobset_open ) );

TOOL_ACTION SCH_OUTPUTS_ACTIONS::activeBom( TOOL_ACTION_ARGS()
        .Name( "eeschema.Outputs.activeBom" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "ActiveBOM..." ) )
        .Tooltip( _( "Bill of materials view: grouped line items, part data status and export" ) )
        .Icon( BITMAPS::post_bom ) );

TOOL_ACTION SCH_OUTPUTS_ACTIONS::variantManager( TOOL_ACTION_ARGS()
        .Name( "eeschema.Outputs.variantManager" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Variants..." ) )
        .Tooltip( _( "Variant Management: fitted / not fitted / alternate parameters for every "
                     "component in every variant" ) )
        .Icon( BITMAPS::options_generic ) );
