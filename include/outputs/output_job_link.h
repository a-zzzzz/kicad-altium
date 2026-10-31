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

#ifndef OUTPUT_JOB_LINK_H
#define OUTPUT_JOB_LINK_H

/**
 * Editor side of the Altium-style Output Job editor.
 *
 * The editor itself (OUTJOB_FRAME) lives in the project manager, next to KiCad's job runner.
 * Editors reach it by sending MAIL_OPEN_OUTPUT_JOB to the manager (the KIWAY top frame) with
 * the .kicad_jobset path as payload; the manager answers "ok" in the payload.
 */

#include <confirm.h>
#include <frame_type.h>
#include <kiface_base.h>
#include <kiway.h>
#include <kiway_mail.h>
#include <kiway_player.h>
#include <mail_type.h>
#include <project.h>

#include <wx/choicdlg.h>
#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/intl.h>

#include <algorithm>
#include <vector>

namespace OUTPUT_JOB_LINK
{

/// The project manager window that hosts the Output Job editor, or nullptr (after telling
/// the user why) when this editor runs on its own.
inline wxWindow* editorHost( KIWAY_HOLDER* aHolder, wxWindow* aParent )
{
    if( Kiface().IsSingle() )
    {
        DisplayErrorMessage( aParent, _( "Output Job files are edited from a KiCad project.  "
                                         "Open this design through the KiCad project manager." ) );
        return nullptr;
    }

    wxWindow* top = aHolder->Kiway().GetTop();

    if( !top || dynamic_cast<KIWAY_PLAYER*>( top ) )
    {
        DisplayErrorMessage( aParent, _( "The KiCad project manager is not running." ) );
        return nullptr;
    }

    return top;
}


/// Open (or create, if missing) the Output Job aPath in the project manager's editor.
inline bool Open( KIWAY_HOLDER* aHolder, wxWindow* aParent, const wxString& aPath )
{
    wxWindow* top = editorHost( aHolder, aParent );

    if( !top )
        return false;

    std::string      payload = aPath.utf8_string();
    KIWAY_MAIL_EVENT mail( KICAD_MAIN_FRAME_T, MAIL_OPEN_OUTPUT_JOB, payload, aParent );

    top->GetEventHandler()->ProcessEvent( mail );
    return payload == "ok";
}


/// The project's .kicad_jobset files, sorted by name.
inline std::vector<wxString> ProjectOutputJobs( PROJECT& aProject )
{
    std::vector<wxString> files;

    if( aProject.IsNullProject() )
        return files;

    wxDir    dir( aProject.GetProjectPath() );
    wxString name;

    if( dir.IsOpened() && dir.GetFirst( &name, wxS( "*.kicad_jobset" ), wxDIR_FILES ) )
    {
        do
        {
            files.push_back( wxFileName( aProject.GetProjectPath(), name ).GetFullPath() );
        } while( dir.GetNext( &name ) );
    }

    std::sort( files.begin(), files.end() );
    return files;
}


/// File > New > Output Job File: creates Job1.kicad_jobset (Job2, ...) in the project folder.
inline bool New( KIWAY_HOLDER* aHolder, wxWindow* aParent )
{
    PROJECT& prj = aHolder->Prj();

    if( prj.IsNullProject() )
    {
        DisplayErrorMessage( aParent, _( "Output Job files belong to a project.  Open or create a "
                                         "project first." ) );
        return false;
    }

    wxFileName fn;

    for( int i = 1; i < 10000; ++i )
    {
        fn = wxFileName( prj.GetProjectPath(), wxString::Format( wxS( "Job%d" ), i ),
                         wxS( "kicad_jobset" ) );

        if( !fn.FileExists() )
            break;
    }

    return Open( aHolder, aParent, fn.GetFullPath() );
}


/// Open one of the project's Output Jobs (asks which when there are several; offers to create
/// one when there are none).
inline bool OpenFromProject( KIWAY_HOLDER* aHolder, wxWindow* aParent )
{
    std::vector<wxString> files = ProjectOutputJobs( aHolder->Prj() );

    // Say up front if Output Jobs can't be edited from here, rather than after asking questions
    if( !editorHost( aHolder, aParent ) )
        return false;

    if( files.empty() )
    {
        if( aHolder->Prj().IsNullProject() )
            return New( aHolder, aParent );

        if( !IsOK( aParent, _( "This project has no Output Job file.  Create one?" ) ) )
            return false;

        return New( aHolder, aParent );
    }

    if( files.size() == 1 )
        return Open( aHolder, aParent, files.front() );

    wxArrayString names;

    for( const wxString& file : files )
        names.Add( wxFileName( file ).GetFullName() );

    wxSingleChoiceDialog dlg( aParent, _( "Output Job file:" ), _( "Open Output Job" ), names );

    if( dlg.ShowModal() != wxID_OK )
        return false;

    return Open( aHolder, aParent, files[dlg.GetSelection()] );
}

} // namespace OUTPUT_JOB_LINK

#endif // OUTPUT_JOB_LINK_H
