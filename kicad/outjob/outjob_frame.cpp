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

#include "outjob_frame.h"

#include <algorithm>
#include <map>

#include <bitmaps.h>
#include <common.h>
#include <confirm.h>
#include <document_windows.h>
#include <frame_type.h>
#include <i18n_utility.h>
#include <id.h>
#include <kiway.h>
#include <kiway_mail.h>
#include <kiway_player.h>
#include <launch_ext.h>
#include <mail_type.h>
#include <project.h>
#include <reporter.h>
#include <wildcards_and_files_ext.h>
#include <widgets/ui_common.h>

#include <jobs/job.h>
#include <jobs/job_export_pcb_gerbers.h>
#include <lset.h>
#include <jobs/job_registry.h>
#include <jobs/jobset.h>
#include <jobs/job_special_copyfiles.h>
#include <jobs/job_special_execute.h>

#include <jobs_runner.h>
#include <kicad_manager_frame.h>
#include <dialogs/dialog_destination.h>
#include <dialogs/dialog_copyfiles_job_settings.h>
#include <dialogs/dialog_executecommand_job_settings.h>

#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/ffile.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/hyperlink.h>
#include <wx/generic/hyperlink.h>
#include <wx/grid.h>
#include <wx/menu.h>
#include <wx/menuitem.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/button.h>

#include <nlohmann/json.hpp>


extern KICOMMON_API
std::map<JOBSET_DESTINATION_T, JOBSET_DESTINATION_T_INFO> JobsetDestinationTypeInfos;


namespace
{

// ---------------------------------------------------------------------------------------------
// Altium's output categories, and where each KiCad job type sits in them
// ---------------------------------------------------------------------------------------------

enum CATEGORY
{
    CAT_NETLIST,
    CAT_SIMULATOR,
    CAT_DOCUMENTATION,
    CAT_ASSEMBLY,
    CAT_FABRICATION,
    CAT_REPORT,
    CAT_VALIDATION,
    CAT_EXPORT,
    CAT_POSTPROCESS,
    CAT_COUNT
};


struct CATEGORY_INFO
{
    const wxChar* title;      ///< "Fabrication Outputs"
    const wxChar* addLabel;   ///< "Add New Fabrication Output"
};


const CATEGORY_INFO g_categories[CAT_COUNT] = {
    { _HKI( "Netlist Outputs" ),       _HKI( "Add New Netlist Output" ) },
    { _HKI( "Simulator Outputs" ),     _HKI( "Add New Simulation Output" ) },
    { _HKI( "Documentation Outputs" ), _HKI( "Add New Documentation Output" ) },
    { _HKI( "Assembly Outputs" ),      _HKI( "Add New Assembly Output" ) },
    { _HKI( "Fabrication Outputs" ),   _HKI( "Add New Fabrication Output" ) },
    { _HKI( "Report Outputs" ),        _HKI( "Add New Report Output" ) },
    { _HKI( "Validation Outputs" ),    _HKI( "Add New Validation Output" ) },
    { _HKI( "Export Outputs" ),        _HKI( "Add New Export Output" ) },
    { _HKI( "Post-Process Outputs" ),  _HKI( "Add New Post-Process Output" ) },
};


struct OUTPUT_KIND
{
    const char* type;       ///< KiCad job type key
    const wxChar* name;     ///< Altium-style output name
    int         category;
};


const OUTPUT_KIND g_kinds[] = {
    { "sch_export_netlist",   _HKI( "Netlist" ),                          CAT_NETLIST },

    { "sch_export_plot_pdf",  _HKI( "Schematic Prints (PDF)" ),           CAT_DOCUMENTATION },
    { "sch_export_plot_svg",  _HKI( "Schematic Prints (SVG)" ),           CAT_DOCUMENTATION },
    { "sch_export_plot_ps",   _HKI( "Schematic Prints (PostScript)" ),    CAT_DOCUMENTATION },
    { "pcb_export_pdf",       _HKI( "PCB Prints (PDF)" ),                 CAT_DOCUMENTATION },
    { "pcb_export_svg",       _HKI( "PCB Prints (SVG)" ),                 CAT_DOCUMENTATION },
    { "pcb_export_ps",        _HKI( "PCB Prints (PostScript)" ),          CAT_DOCUMENTATION },
    { "pcb_render",           _HKI( "PCB 3D Print" ),                     CAT_DOCUMENTATION },

    { "pcb_export_pos",       _HKI( "Generates pick and place files" ),   CAT_ASSEMBLY },

    { "pcb_export_gerbers",   _HKI( "Gerber Files" ),                     CAT_FABRICATION },
    { "pcb_export_drill",     _HKI( "NC Drill Files" ),                   CAT_FABRICATION },
    { "pcb_export_odb",       _HKI( "ODB++ Files" ),                      CAT_FABRICATION },
    { "pcb_export_ipc2581",   _HKI( "IPC-2581 Files" ),                   CAT_FABRICATION },
    { "pcb_export_ipcd356",   _HKI( "Test Point Report (IPC-D-356)" ),    CAT_FABRICATION },

    { "sch_export_bom",       _HKI( "Bill of Materials" ),                CAT_REPORT },
    { "pcb_export_stats",     _HKI( "Board Information Report" ),         CAT_REPORT },

    { "pcb_drc",              _HKI( "Design Rules Check" ),               CAT_VALIDATION },
    { "sch_erc",              _HKI( "Electrical Rules Check" ),           CAT_VALIDATION },

    { "pcb_export_3d",        _HKI( "Export 3D Model (STEP, ...)" ),      CAT_EXPORT },
    { "pcb_export_dxf",       _HKI( "Export DXF (PCB)" ),                 CAT_EXPORT },
    { "sch_export_plot_dxf",  _HKI( "Export DXF (Schematic)" ),           CAT_EXPORT },
    { "pcb_export_gencad",    _HKI( "Export GenCAD" ),                    CAT_EXPORT },

    { "special_copyfiles",    _HKI( "Copy Files" ),                       CAT_POSTPROCESS },
    { "special_execute",      _HKI( "Run Command" ),                      CAT_POSTPROCESS },
};


const OUTPUT_KIND* kindOf( const wxString& aType )
{
    for( const OUTPUT_KIND& kind : g_kinds )
    {
        if( aType == kind.type )
            return &kind;
    }

    return nullptr;
}


int categoryOf( const wxString& aType )
{
    const OUTPUT_KIND* kind = kindOf( aType );
    return kind ? kind->category : CAT_POSTPROCESS;
}


wxString outputName( const wxString& aType )
{
    if( const OUTPUT_KIND* kind = kindOf( aType ) )
        return wxGetTranslation( kind->name );

    const JOB_REGISTRY::REGISTRY_MAP_T& registry = JOB_REGISTRY::GetRegistry();
    auto                                it = registry.find( aType );

    if( it != registry.end() )
        return wxGetTranslation( it->second.title );

    return aType;
}


/// Job types offered by the Add New ... Output menus, in category order.
std::vector<std::pair<int, wxString>> addableTypes()
{
    std::vector<std::pair<int, wxString>> result;
    const JOB_REGISTRY::REGISTRY_MAP_T&   registry = JOB_REGISTRY::GetRegistry();

    auto available =
            [&]( const wxString& aType )
            {
                auto it = registry.find( aType );
                return it != registry.end() && !it->second.deprecated;
            };

    for( const OUTPUT_KIND& kind : g_kinds )
    {
        if( available( kind.type ) )
            result.emplace_back( kind.category, wxString( kind.type ) );
    }

    // Job types this table does not know yet still get offered
    std::vector<wxString> extra;

    for( const auto& [type, entry] : registry )
    {
        if( !entry.deprecated && !kindOf( type ) )
            extra.push_back( type );
    }

    std::sort( extra.begin(), extra.end() );

    for( const wxString& type : extra )
        result.emplace_back( CAT_POSTPROCESS, type );

    return result;
}


// ---------------------------------------------------------------------------------------------
// Variant handling: every variant-aware job stores its variant in one of these parameters
// ---------------------------------------------------------------------------------------------

bool getJobVariant( JOB* aJob, wxString* aVariant )
{
    if( !aJob )
        return false;

    nlohmann::json j;
    aJob->ToJson( j );

    if( j.contains( "variant_names" ) && j["variant_names"].is_array() )
    {
        if( j["variant_names"].empty() || !j["variant_names"][0].is_string() )
            *aVariant = wxEmptyString;
        else
            *aVariant = wxString::FromUTF8( j["variant_names"][0].get<std::string>() );

        return true;
    }

    for( const char* key : { "variant", "variant_name" } )
    {
        if( j.contains( key ) && j[key].is_string() )
        {
            *aVariant = wxString::FromUTF8( j[key].get<std::string>() );
            return true;
        }
    }

    return false;
}


void setJobVariant( JOB* aJob, const wxString& aVariant )
{
    if( !aJob )
        return;

    nlohmann::json j;
    aJob->ToJson( j );

    std::string value = aVariant.utf8_string();
    bool        changed = false;

    for( const char* key : { "variant", "variant_name" } )
    {
        if( j.contains( key ) )
        {
            j[key] = value;
            changed = true;
        }
    }

    if( j.contains( "variant_names" ) )
    {
        j["variant_names"] = aVariant.IsEmpty() ? nlohmann::json::array()
                                                : nlohmann::json::array( { value } );
        changed = true;
    }

    if( changed )
        aJob->FromJson( j );
}


/// Variant names defined in the project file (schematic.variants).
wxArrayString projectVariants( PROJECT& aProject )
{
    wxArrayString names;
    wxFFile       file( aProject.GetProjectFullName(), wxS( "rb" ) );
    wxString      text;

    if( !file.IsOpened() || !file.ReadAll( &text, wxConvUTF8 ) )
        return names;

    nlohmann::json j = nlohmann::json::parse( text.utf8_string(), nullptr, false );

    if( j.is_discarded() || !j.contains( "schematic" ) || !j["schematic"].is_object() )
        return names;

    const nlohmann::json& sch = j["schematic"];

    if( !sch.contains( "variants" ) || !sch["variants"].is_array() )
        return names;

    for( const nlohmann::json& entry : sch["variants"] )
    {
        if( entry.is_object() && entry.contains( "name" ) && entry["name"].is_string() )
            names.Add( wxString::FromUTF8( entry["name"].get<std::string>() ) );
    }

    return names;
}


wxString containerKindName( JOBSET_DESTINATION_T aType )
{
    switch( aType )
    {
    case JOBSET_DESTINATION_T::FOLDER:  return _( "Folder Structure" );
    case JOBSET_DESTINATION_T::ARCHIVE: return _( "Zip Archive" );
    default:                            return _( "Output Container" );
    }
}


const wxString NO_JOBS_SENTINEL = wxS( "00000000-0000-0000-0000-000000000000" );


bool jobEnabledFor( JOBSET_DESTINATION* aDest, const wxString& aJobId )
{
    if( !aDest )
        return false;

    if( aDest->m_only.empty() )
        return true;

    return std::find( aDest->m_only.begin(), aDest->m_only.end(), aJobId ) != aDest->m_only.end();
}


enum COLUMNS
{
    COL_NAME,
    COL_SOURCE,
    COL_DESC,
    COL_ENABLED,
    COL_COUNT
};


enum MENU_IDS
{
    ID_SAVE = wxID_HIGHEST + 1,
    ID_CLOSE,
    ID_CONFIGURE,
    ID_RENAME,
    ID_DELETE,
    ID_MOVE_UP,
    ID_MOVE_DOWN,
    ID_ENABLE_ALL,
    ID_DISABLE_ALL,
    ID_GENERATE_ALL,
    ID_GENERATE_SELECTED,
    ID_ADD_FOLDER,
    ID_ADD_ARCHIVE,
    ID_OPEN_PROJECT_FOLDER,
    ID_ADD_FIRST,
    ID_ADD_LAST = ID_ADD_FIRST + 500
};


std::vector<OUTJOB_FRAME*>& openFrames()
{
    static std::vector<OUTJOB_FRAME*> frames;
    return frames;
}


wxString normalized( const wxString& aPath )
{
    wxFileName fn( aPath );
    fn.Normalize( wxPATH_NORM_ABSOLUTE | wxPATH_NORM_DOTS | wxPATH_NORM_LONG );
    return fn.GetFullPath();
}

} // namespace


// ---------------------------------------------------------------------------------------------
// One output container card on the right
// ---------------------------------------------------------------------------------------------

class OUTJOB_CONTAINER_PANEL : public wxPanel
{
public:
    OUTJOB_CONTAINER_PANEL( wxWindow* aParent, OUTJOB_FRAME* aFrame, const wxString& aId ) :
            wxPanel( aParent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                     wxBORDER_SIMPLE | wxTAB_TRAVERSAL ),
            m_frame( aFrame ),
            m_id( aId ),
            m_selected( false )
    {
        wxBoxSizer* outer = new wxBoxSizer( wxVERTICAL );
        wxBoxSizer* top = new wxBoxSizer( wxHORIZONTAL );

        m_icon = new wxStaticBitmap( this, wxID_ANY, wxNullBitmap );
        top->Add( m_icon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6 );

        wxBoxSizer* titles = new wxBoxSizer( wxVERTICAL );
        m_title = new wxStaticText( this, wxID_ANY, wxEmptyString );
        m_title->SetFont( m_title->GetFont().Bold() );
        titles->Add( m_title, 0 );

        m_kind = new wxStaticText( this, wxID_ANY, wxEmptyString );
        m_kind->SetFont( KIUI::GetSmallInfoFont( this ) );
        titles->Add( m_kind, 0 );

        top->Add( titles, 1, wxALIGN_CENTER_VERTICAL );

        m_status = new wxStaticBitmap( this, wxID_ANY, wxNullBitmap );
        m_status->SetCursor( wxCursor( wxCURSOR_HAND ) );
        top->Add( m_status, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 6 );

        outer->Add( top, 0, wxEXPAND | wxALL, 6 );

        m_path = new wxStaticText( this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                   wxST_ELLIPSIZE_MIDDLE | wxST_NO_AUTORESIZE );
        m_path->SetFont( KIUI::GetSmallInfoFont( this ) );
        m_path->SetMinSize( wxSize( FromDIP( 60 ), m_path->GetCharHeight() + 2 ) );
        outer->Add( m_path, 0, wxEXPAND | wxLEFT | wxRIGHT, 6 );

        m_count = new wxStaticText( this, wxID_ANY, wxEmptyString );
        m_count->SetFont( KIUI::GetSmallInfoFont( this ) );
        outer->Add( m_count, 0, wxEXPAND | wxLEFT | wxRIGHT, 6 );

        wxBoxSizer* links = new wxBoxSizer( wxHORIZONTAL );

        auto addLink =
                [&]( const wxString& aLabel, const wxString& aTip, std::function<void()> aAction )
                {
                    wxGenericHyperlinkCtrl* link = new wxGenericHyperlinkCtrl( this, wxID_ANY, aLabel,
                                                                               wxEmptyString );
                    link->SetToolTip( aTip );
                    link->Bind( wxEVT_HYPERLINK,
                                [aAction]( wxHyperlinkEvent& )
                                {
                                    aAction();
                                } );
                    links->Add( link, 0, wxRIGHT, 10 );
                    return link;
                };

        m_generate = addLink( _( "Generate Content" ),
                              _( "Run every output enabled for this container" ),
                              [this]() { m_frame->GenerateContainer( m_id ); } );
        addLink( _( "Change" ), _( "Container settings: name, type, output path, outputs" ),
                 [this]() { m_frame->EditContainer( m_id ); } );
        addLink( _( "Open" ), _( "Open the output folder or archive" ),
                 [this]() { m_frame->OpenContainerOutput( m_id ); } );
        addLink( _( "Remove" ), _( "Remove this output container" ),
                 [this]() { m_frame->RemoveContainer( m_id ); } );

        outer->Add( links, 0, wxEXPAND | wxALL, 6 );
        SetSizer( outer );

        // Clicking anywhere on the card selects the container
        for( wxWindow* win : { (wxWindow*) this, (wxWindow*) m_icon, (wxWindow*) m_title,
                               (wxWindow*) m_kind, (wxWindow*) m_path, (wxWindow*) m_count } )
        {
            win->Bind( wxEVT_LEFT_DOWN,
                       [this]( wxMouseEvent& aEvent )
                       {
                           m_frame->SelectContainer( m_id );
                           aEvent.Skip();
                       } );
        }

        m_status->Bind( wxEVT_LEFT_DOWN,
                        [this]( wxMouseEvent& )
                        {
                            m_frame->ShowContainerLog( m_id );
                        } );

        UpdateInfo();
    }

    const wxString& GetDestinationId() const { return m_id; }

    void SetSelected( bool aSelected )
    {
        m_selected = aSelected;

        wxColour bg = wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOW );

        if( aSelected )
        {
            wxColour hl = wxSystemSettings::GetColour( wxSYS_COLOUR_HIGHLIGHT );
            bg = wxColour( ( bg.Red() * 3 + hl.Red() ) / 4, ( bg.Green() * 3 + hl.Green() ) / 4,
                           ( bg.Blue() * 3 + hl.Blue() ) / 4 );
        }

        SetBackgroundColour( bg );

        for( wxWindow* child : GetChildren() )
            child->SetBackgroundColour( bg );

        Refresh();
    }

    void UpdateInfo()
    {
        JOBSET_DESTINATION* dest = m_frame->FindDestination( m_id );

        if( !dest )
            return;

        if( JobsetDestinationTypeInfos.contains( dest->m_type ) )
            m_icon->SetBitmap( KiBitmapBundle( JobsetDestinationTypeInfos[dest->m_type].bitmap ) );

        m_title->SetLabel( dest->GetDescription() );
        m_kind->SetLabel( containerKindName( dest->m_type ) );

        wxString path = dest->GetPathInfo();
        m_path->SetLabel( path.IsEmpty() ? _( "(no output path)" ) : path );
        m_path->SetToolTip( path );

        size_t count = m_frame->GetJobSet()->GetJobsForDestination( dest ).size();
        m_count->SetLabel( wxString::Format( _( "%zu output(s) enabled" ), count ) );
        m_generate->Enable( count > 0 );

        if( dest->m_lastRunSuccess.has_value() )
        {
            bool ok = dest->m_lastRunSuccess.value();
            m_status->SetBitmap( KiBitmapBundle( ok ? BITMAPS::checked_ok : BITMAPS::ercerr ) );
            m_status->SetToolTip( ok ? _( "Last run successful.  Click to view the log." )
                                     : _( "Last run failed.  Click to view the log." ) );
            m_status->Show();
        }
        else
        {
            m_status->SetBitmap( wxNullBitmap );
            m_status->Hide();
        }

        Layout();
    }

private:
    OUTJOB_FRAME*           m_frame;
    wxString                m_id;
    bool                    m_selected;
    wxStaticBitmap*         m_icon;
    wxStaticText*           m_title;
    wxStaticText*           m_kind;
    wxStaticText*           m_path;
    wxStaticText*           m_count;
    wxStaticBitmap*         m_status;
    wxGenericHyperlinkCtrl* m_generate;
};


// ---------------------------------------------------------------------------------------------
// Opening
// ---------------------------------------------------------------------------------------------

OUTJOB_FRAME* OUTJOB_FRAME::Open( KICAD_MANAGER_FRAME* aManager, const wxString& aPath )
{
    wxCHECK( aManager && !aPath.IsEmpty(), nullptr );

    wxString path = normalized( aPath );

    for( OUTJOB_FRAME* frame : openFrames() )
    {
        if( normalized( frame->GetFilePath() ) == path )
        {
            if( frame->IsIconized() )
                frame->Iconize( false );

            frame->Show();
            frame->Raise();
            return frame;
        }
    }

    try
    {
        std::unique_ptr<JOBSET> jobSet = std::make_unique<JOBSET>( path );
        bool                    isNew = !wxFileExists( path );

        jobSet->LoadFromFile();

        if( isNew )
        {
            // A new Output Job: one folder container, like Altium's default "Folder Structure"
            JOBSET_DESTINATION* dest = jobSet->AddNewDestination( JOBSET_DESTINATION_T::FOLDER );
            dest->SetDescription( _( "Folder Structure" ) );
            dest->m_outputHandler->SetOutputPath(
                    wxString::Format( wxS( "Project Outputs for %s" ),
                                      wxFileName( path ).GetName() ) );
            jobSet->SaveToFile( wxEmptyString, true );
        }

        OUTJOB_FRAME* frame = new OUTJOB_FRAME( aManager, std::move( jobSet ) );
        frame->Show();
        frame->Raise();
        return frame;
    }
    catch( ... )
    {
        DisplayErrorMessage( aManager, wxString::Format( _( "Error opening Output Job '%s'." ),
                                                         path ) );
    }

    return nullptr;
}


int OUTJOB_FRAME::ManagerMenuId()
{
    return ID_KICAD_MANAGER_END - 1;
}


void OUTJOB_FRAME::InstallManagerHooks( KICAD_MANAGER_FRAME* aManager )
{
    wxCHECK( aManager, /* void */ );

    // Editors (Projects panel, File > New > Output Job File) ask for an Output Job by mail
    aManager->Bind( EDA_KIWAY_MAIL_RECEIVED,
                    [aManager]( KIWAY_MAIL_EVENT& aEvent )
                    {
                        if( aEvent.Command() != MAIL_OPEN_OUTPUT_JOB )
                        {
                            aEvent.Skip();
                            return;
                        }

                        wxString path = wxString::FromUTF8( aEvent.GetPayload() );
                        OUTJOB_FRAME* frame = Open( aManager, path );
                        aEvent.SetPayload( frame ? "ok" : "failed" );
                    } );

    // File > Open Output Job...
    aManager->Bind( wxEVT_MENU,
                    [aManager]( wxCommandEvent& )
                    {
                        wxString dir = aManager->Prj().IsNullProject()
                                               ? wxString()
                                               : aManager->Prj().GetProjectPath();

                        wxFileDialog dlg( aManager, _( "Open Output Job" ), dir, wxEmptyString,
                                          FILEEXT::JobsetFileWildcard(),
                                          wxFD_OPEN | wxFD_FILE_MUST_EXIST );

                        if( dlg.ShowModal() == wxID_OK )
                            Open( aManager, dlg.GetPath() );
                    },
                    ManagerMenuId() );
}


// ---------------------------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------------------------

OUTJOB_FRAME::OUTJOB_FRAME( KICAD_MANAGER_FRAME* aManager, std::unique_ptr<JOBSET> aJobSet ) :
        wxFrame( nullptr, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize( 1100, 640 ),
                 wxDEFAULT_FRAME_STYLE | wxTAB_TRAVERSAL ),
        m_manager( aManager ),
        m_jobSet( std::move( aJobSet ) ),
        m_grid( nullptr ),
        m_containerList( nullptr ),
        m_containerSizer( nullptr ),
        m_variantChoice( nullptr ),
        m_pathLabel( nullptr )
{
    openFrames().push_back( this );

    SetIcons( aManager->GetIcons() );
    SetSize( FromDIP( wxSize( 1100, 640 ) ) );
    SetBackgroundColour( wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOW ) );

    buildMenuBar();

    wxPanel*    main = new wxPanel( this );
    wxBoxSizer* mainSizer = new wxBoxSizer( wxVERTICAL );

    // Header: file path, variant choice, Generate All
    wxBoxSizer* header = new wxBoxSizer( wxHORIZONTAL );

    // A small minimum width so a long path is ellipsized instead of pushing into the controls
    m_pathLabel = new wxStaticText( main, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                    wxDefaultSize, wxST_ELLIPSIZE_MIDDLE | wxST_NO_AUTORESIZE );
    m_pathLabel->SetFont( KIUI::GetInfoFont( main ) );
    m_pathLabel->SetMinSize( wxSize( FromDIP( 60 ), m_pathLabel->GetCharHeight() + 2 ) );
    header->Add( m_pathLabel, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10 );

    header->Add( new wxStaticText( main, wxID_ANY, _( "Variant Choice:" ) ), 0,
                 wxALIGN_CENTER_VERTICAL | wxRIGHT, 5 );
    m_variantChoice = new wxChoice( main, wxID_ANY );
    m_variantChoice->SetMinSize( FromDIP( wxSize( 180, -1 ) ) );
    m_variantChoice->SetToolTip( _( "Variant used by every variant-aware output in this "
                                    "Output Job" ) );
    m_variantChoice->Bind( wxEVT_CHOICE, &OUTJOB_FRAME::onVariantChoice, this );
    header->Add( m_variantChoice, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10 );

    wxButton* saveBtn = new wxButton( main, ID_SAVE, _( "Save" ) );
    header->Add( saveBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5 );

    wxButton* genAll = new wxButton( main, ID_GENERATE_ALL, _( "Generate All" ) );
    genAll->SetToolTip( _( "Generate the content of every output container" ) );
    header->Add( genAll, 0, wxALIGN_CENTER_VERTICAL );

    mainSizer->Add( header, 0, wxEXPAND | wxALL, 6 );

    // Body: outputs grid | containers
    wxBoxSizer* body = new wxBoxSizer( wxHORIZONTAL );

    wxBoxSizer* left = new wxBoxSizer( wxVERTICAL );
    wxStaticText* outputsTitle = new wxStaticText( main, wxID_ANY, _( "Outputs" ) );
    outputsTitle->SetFont( outputsTitle->GetFont().Bold() );
    left->Add( outputsTitle, 0, wxBOTTOM, 4 );

    m_grid = new wxGrid( main, wxID_ANY );
    m_grid->CreateGrid( 0, COL_COUNT );
    m_grid->SetRowLabelSize( 0 );
    m_grid->SetColLabelValue( COL_NAME, _( "Name" ) );
    m_grid->SetColLabelValue( COL_SOURCE, _( "Data Source" ) );
    m_grid->SetColLabelValue( COL_DESC, _( "Output Description" ) );
    m_grid->SetColLabelValue( COL_ENABLED, _( "Enabled" ) );
    m_grid->SetColSize( COL_NAME, FromDIP( 230 ) );
    m_grid->SetColSize( COL_SOURCE, FromDIP( 170 ) );
    m_grid->SetColSize( COL_DESC, FromDIP( 260 ) );
    m_grid->SetColSize( COL_ENABLED, FromDIP( 70 ) );
    m_grid->SetSelectionMode( wxGrid::wxGridSelectRows );
    m_grid->EnableDragRowSize( false );
    m_grid->DisableDragGridSize();
    m_grid->SetDefaultCellOverflow( false );
    m_grid->SetCellHighlightPenWidth( 0 );
    m_grid->SetDefaultRowSize( FromDIP( 22 ) );

    m_grid->Bind( wxEVT_GRID_CELL_LEFT_CLICK, &OUTJOB_FRAME::onGridLeftClick, this );
    m_grid->Bind( wxEVT_GRID_CELL_LEFT_DCLICK, &OUTJOB_FRAME::onGridDClick, this );
    m_grid->Bind( wxEVT_GRID_CELL_RIGHT_CLICK, &OUTJOB_FRAME::onGridRightClick, this );
    m_grid->Bind( wxEVT_GRID_CELL_CHANGED, &OUTJOB_FRAME::onGridCellChanged, this );
    m_grid->Bind( wxEVT_SIZE,
                  [this]( wxSizeEvent& aEvent )
                  {
                      int fixed = m_grid->GetColSize( COL_NAME ) + m_grid->GetColSize( COL_SOURCE )
                                  + m_grid->GetColSize( COL_ENABLED );
                      int width = m_grid->GetClientSize().x - fixed;
                      m_grid->SetColSize( COL_DESC, std::max( width, FromDIP( 120 ) ) );
                      aEvent.Skip();
                  } );

    left->Add( m_grid, 1, wxEXPAND );
    body->Add( left, 3, wxEXPAND | wxRIGHT, 8 );

    wxBoxSizer* right = new wxBoxSizer( wxVERTICAL );
    wxStaticText* containersTitle = new wxStaticText( main, wxID_ANY, _( "Output Containers" ) );
    containersTitle->SetFont( containersTitle->GetFont().Bold() );
    right->Add( containersTitle, 0, wxBOTTOM, 4 );

    m_containerList = new wxScrolledWindow( main, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                            wxVSCROLL | wxBORDER_THEME );
    m_containerList->SetScrollRate( 0, 10 );
    m_containerList->SetBackgroundColour( wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOW ) );
    m_containerSizer = new wxBoxSizer( wxVERTICAL );
    m_containerList->SetSizer( m_containerSizer );
    right->Add( m_containerList, 1, wxEXPAND );

    wxGenericHyperlinkCtrl* addContainer = new wxGenericHyperlinkCtrl( main, wxID_ANY,
                                                         _( "[Add New Output Container]" ),
                                                         wxEmptyString );
    addContainer->Bind( wxEVT_HYPERLINK,
                        [this]( wxHyperlinkEvent& )
                        {
                            wxMenu menu;
                            menu.Append( ID_ADD_FOLDER, _( "Folder Structure" ) );
                            menu.Append( ID_ADD_ARCHIVE, _( "Zip Archive" ) );
                            PopupMenu( &menu );
                        } );
    right->Add( addContainer, 0, wxTOP, 4 );

    body->Add( right, 2, wxEXPAND );
    mainSizer->Add( body, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6 );

    main->SetSizer( mainSizer );

    CreateStatusBar();

    Bind( wxEVT_MENU, &OUTJOB_FRAME::onMenu, this );
    Bind( wxEVT_BUTTON,
          [this]( wxCommandEvent& aEvent )
          {
              wxCommandEvent evt( wxEVT_MENU, aEvent.GetId() );
              onMenu( evt );
          } );
    Bind( wxEVT_CLOSE_WINDOW, &OUTJOB_FRAME::onClose, this );
    m_manager->Bind( wxEVT_CLOSE_WINDOW, &OUTJOB_FRAME::onManagerClose, this );

    if( !m_jobSet->GetDestinations().empty() )
        m_selectedContainer = m_jobSet->GetDestinations().front().m_id;

    updateVariantChoice();
    rebuildContainers();
    rebuildGrid();
    updateTitle();

    Centre();
}


OUTJOB_FRAME::~OUTJOB_FRAME()
{
    std::vector<OUTJOB_FRAME*>& frames = openFrames();
    frames.erase( std::remove( frames.begin(), frames.end(), this ), frames.end() );

    if( m_manager )
        m_manager->Unbind( wxEVT_CLOSE_WINDOW, &OUTJOB_FRAME::onManagerClose, this );
}


wxString OUTJOB_FRAME::GetFilePath() const
{
    return m_jobSet->GetFullFilename();
}


void OUTJOB_FRAME::buildMenuBar()
{
    wxMenuBar* bar = new wxMenuBar();

    wxMenu* file = new wxMenu();
    file->Append( ID_SAVE, _( "&Save" ) + wxS( "\tCtrl+S" ) );
    file->AppendSeparator();
    file->Append( ID_OPEN_PROJECT_FOLDER, _( "&Explore Project Folder" ) );
    file->AppendSeparator();
    file->Append( ID_CLOSE, _( "&Close" ) + wxS( "\tCtrl+W" ) );
    bar->Append( file, _( "&File" ) );

    wxMenu* edit = new wxMenu();

    for( int cat = 0; cat < CAT_COUNT; ++cat )
    {
        wxMenu* sub = new wxMenu();
        appendAddMenu( sub, cat );
        edit->AppendSubMenu( sub, wxGetTranslation( g_categories[cat].addLabel ) );
    }

    edit->AppendSeparator();
    edit->Append( ID_CONFIGURE, _( "&Configure Output..." ) );
    edit->Append( ID_RENAME, _( "Edit Output &Description" ) );
    edit->Append( ID_DELETE, _( "&Delete Output" ) );
    edit->AppendSeparator();
    edit->Append( ID_MOVE_UP, _( "Move &Up" ) );
    edit->Append( ID_MOVE_DOWN, _( "Move Do&wn" ) );
    bar->Append( edit, _( "&Edit" ) );

    wxMenu* tools = new wxMenu();
    tools->Append( ID_GENERATE_SELECTED, _( "Generate &Selected Container" ) + wxS( "\tF9" ) );
    tools->Append( ID_GENERATE_ALL, _( "Generate &All Containers" ) );
    tools->AppendSeparator();
    tools->Append( ID_ENABLE_ALL, _( "&Enable All Outputs for Selected Container" ) );
    tools->Append( ID_DISABLE_ALL, _( "&Disable All Outputs for Selected Container" ) );
    tools->AppendSeparator();
    tools->Append( ID_ADD_FOLDER, _( "Add &Folder Structure Container" ) );
    tools->Append( ID_ADD_ARCHIVE, _( "Add &Zip Archive Container" ) );
    bar->Append( tools, _( "&Tools" ) );

    SetMenuBar( bar );
}


void OUTJOB_FRAME::appendAddMenu( wxMenu* aMenu, int aCategory )
{
    std::vector<std::pair<int, wxString>> types = addableTypes();
    bool                                  any = false;

    for( size_t i = 0; i < types.size(); ++i )
    {
        if( types[i].first != aCategory )
            continue;

        aMenu->Append( ID_ADD_FIRST + (int) i, outputName( types[i].second ) + wxS( "..." ) );
        any = true;
    }

    if( !any )
    {
        wxMenuItem* item = aMenu->Append( wxID_ANY, _( "(no outputs of this kind available)" ) );
        item->Enable( false );
    }
}


void OUTJOB_FRAME::updateTitle()
{
    wxFileName fn( GetFilePath() );
    wxString   title = fn.GetFullName();

    if( m_jobSet->GetDirty() )
        title += wxS( "*" );

    SetTitle( title + wxS( " - " ) + _( "Output Job" ) );

    if( m_pathLabel )
        m_pathLabel->SetLabel( fn.GetFullPath() );

    if( GetStatusBar() )
    {
        SetStatusText( wxString::Format( _( "%zu output(s), %zu output container(s)" ),
                                         m_jobSet->GetJobs().size(),
                                         m_jobSet->GetDestinations().size() ) );
    }
}


void OUTJOB_FRAME::setDirty()
{
    m_jobSet->SetDirty();
    updateTitle();
}


void OUTJOB_FRAME::updateVariantChoice()
{
    wxArrayString names = projectVariants( m_manager->Prj() );
    wxString      common;
    bool          first = true;
    bool          mixed = false;

    for( JOBSET_JOB& job : m_jobSet->GetJobs() )
    {
        wxString variant;

        if( !getJobVariant( job.m_job.get(), &variant ) )
            continue;

        if( !variant.IsEmpty() && names.Index( variant ) == wxNOT_FOUND )
            names.Add( variant );

        if( first )
            common = variant;
        else if( variant != common )
            mixed = true;

        first = false;
    }

    m_variantChoice->Clear();
    m_variantValues.Clear();

    m_variantChoice->Append( _( "[No Variations]" ) );
    m_variantValues.Add( wxEmptyString );

    for( const wxString& name : names )
    {
        m_variantChoice->Append( name );
        m_variantValues.Add( name );
    }

    if( mixed )
    {
        m_variantChoice->Append( _( "<mixed>" ) );
        m_variantValues.Add( wxS( "\x01mixed" ) );
        m_variantChoice->SetSelection( m_variantChoice->GetCount() - 1 );
    }
    else
    {
        int idx = m_variantValues.Index( common );
        m_variantChoice->SetSelection( idx == wxNOT_FOUND ? 0 : idx );
    }
}


void OUTJOB_FRAME::onVariantChoice( wxCommandEvent& aEvent )
{
    int sel = m_variantChoice->GetSelection();

    if( sel < 0 || sel >= (int) m_variantValues.size() )
        return;

    wxString variant = m_variantValues[sel];

    if( variant.StartsWith( wxS( "\x01" ) ) )
        return;

    for( JOBSET_JOB& job : m_jobSet->GetJobs() )
    {
        wxString old;

        if( getJobVariant( job.m_job.get(), &old ) && old != variant )
        {
            setJobVariant( job.m_job.get(), variant );
            m_jobSet->SetDirty();
        }
    }

    updateVariantChoice();
    updateTitle();
}


void OUTJOB_FRAME::rebuildGrid()
{
    std::vector<JOBSET_JOB>& jobs = m_jobSet->GetJobs();
    JOBSET_DESTINATION*      dest = FindDestination( m_selectedContainer );
    PROJECT&                 prj = m_manager->Prj();

    m_grid->Freeze();

    if( m_grid->GetNumberRows() > 0 )
        m_grid->DeleteRows( 0, m_grid->GetNumberRows() );

    m_rows.clear();

    for( int cat = 0; cat < CAT_COUNT; ++cat )
    {
        m_rows.push_back( { ROW::CATEGORY, cat, 0 } );

        for( size_t i = 0; i < jobs.size(); ++i )
        {
            if( categoryOf( jobs[i].m_type ) == cat )
                m_rows.push_back( { ROW::JOB, cat, i } );
        }

        m_rows.push_back( { ROW::ADD, cat, 0 } );
    }

    m_grid->AppendRows( (int) m_rows.size() );

    wxColour catBg = wxSystemSettings::GetColour( wxSYS_COLOUR_BTNFACE );
    wxColour linkFg = wxSystemSettings::GetColour( wxSYS_COLOUR_HOTLIGHT );
    wxFont   bold = m_grid->GetDefaultCellFont().Bold();
    wxString pcbName = prj.GetProjectName() + wxS( "." ) + wxString( FILEEXT::KiCadPcbFileExtension );
    wxString schName = prj.GetProjectName() + wxS( "." ) + wxString( FILEEXT::KiCadSchematicFileExtension );

    for( int row = 0; row < (int) m_rows.size(); ++row )
    {
        const ROW& r = m_rows[row];

        for( int col = 0; col < COL_COUNT; ++col )
            m_grid->SetReadOnly( row, col, true );

        switch( r.kind )
        {
        case ROW::CATEGORY:
            m_grid->SetCellSize( row, 0, 1, COL_COUNT );
            m_grid->SetCellValue( row, COL_NAME, wxGetTranslation( g_categories[r.category].title ) );
            m_grid->SetCellFont( row, COL_NAME, bold );
            m_grid->SetCellBackgroundColour( row, COL_NAME, catBg );
            break;

        case ROW::ADD:
            m_grid->SetCellSize( row, 0, 1, COL_COUNT );
            m_grid->SetCellValue( row, COL_NAME,
                                  wxS( "    [" ) + wxGetTranslation( g_categories[r.category].addLabel )
                                          + wxS( "]" ) );
            m_grid->SetCellTextColour( row, COL_NAME, linkFg );
            break;

        case ROW::JOB:
        {
            JOBSET_JOB&   job = jobs[r.job];
            KIWAY::FACE_T face = JOB_REGISTRY::GetKifaceType( job.m_type );
            wxString      source;

            if( face == KIWAY::FACE_PCB )
                source = pcbName;
            else if( face == KIWAY::FACE_SCH )
                source = schName;
            else
                source = _( "[Project]" );

            m_grid->SetCellValue( row, COL_NAME, wxS( "    " ) + outputName( job.m_type ) );
            m_grid->SetCellValue( row, COL_SOURCE, source );
            m_grid->SetCellValue( row, COL_DESC, job.GetDescription() );
            m_grid->SetReadOnly( row, COL_DESC, false );

            if( dest )
            {
                m_grid->SetCellRenderer( row, COL_ENABLED, new wxGridCellBoolRenderer() );
                m_grid->SetCellAlignment( row, COL_ENABLED, wxALIGN_CENTRE, wxALIGN_CENTRE );
                m_grid->SetCellValue( row, COL_ENABLED,
                                      jobEnabledFor( dest, job.m_id ) ? wxS( "1" ) : wxS( "" ) );
            }

            break;
        }
        }
    }

    if( dest )
        m_grid->SetColLabelValue( COL_ENABLED, _( "Enabled" ) );
    else
        m_grid->SetColLabelValue( COL_ENABLED, _( "(no container)" ) );

    m_grid->Thaw();
    m_grid->ForceRefresh();

    for( OUTJOB_CONTAINER_PANEL* panel : m_containerPanels )
        panel->UpdateInfo();

    updateTitle();
}


void OUTJOB_FRAME::rebuildContainers()
{
    m_containerList->Freeze();
    m_containerSizer->Clear( true );
    m_containerPanels.clear();

    if( !FindDestination( m_selectedContainer ) )
    {
        m_selectedContainer = m_jobSet->GetDestinations().empty()
                                      ? wxString()
                                      : m_jobSet->GetDestinations().front().m_id;
    }

    for( JOBSET_DESTINATION& dest : m_jobSet->GetDestinations() )
    {
        OUTJOB_CONTAINER_PANEL* panel = new OUTJOB_CONTAINER_PANEL( m_containerList, this,
                                                                    dest.m_id );
        panel->SetSelected( dest.m_id == m_selectedContainer );
        m_containerSizer->Add( panel, 0, wxEXPAND | wxALL, 4 );
        m_containerPanels.push_back( panel );
    }

    m_containerList->FitInside();
    m_containerList->Layout();
    m_containerList->Thaw();
}


JOBSET_DESTINATION* OUTJOB_FRAME::FindDestination( const wxString& aDestinationId )
{
    if( aDestinationId.IsEmpty() )
        return nullptr;

    for( JOBSET_DESTINATION& dest : m_jobSet->GetDestinations() )
    {
        if( dest.m_id == aDestinationId )
            return &dest;
    }

    return nullptr;
}


void OUTJOB_FRAME::SelectContainer( const wxString& aDestinationId )
{
    if( m_selectedContainer == aDestinationId )
        return;

    m_selectedContainer = aDestinationId;

    for( OUTJOB_CONTAINER_PANEL* panel : m_containerPanels )
        panel->SetSelected( panel->GetDestinationId() == aDestinationId );

    rebuildGrid();
}


void OUTJOB_FRAME::EditContainer( const wxString& aDestinationId )
{
    JOBSET_DESTINATION* dest = FindDestination( aDestinationId );

    if( !dest )
        return;

    m_grid->SaveEditControlValue();

    DIALOG_DESTINATION dlg( this, m_jobSet.get(), dest );

    if( dlg.ShowModal() == wxID_OK )
    {
        dest->m_lastRunSuccess = std::nullopt;
        setDirty();
        rebuildGrid();
    }
}


void OUTJOB_FRAME::RemoveContainer( const wxString& aDestinationId )
{
    JOBSET_DESTINATION* dest = FindDestination( aDestinationId );

    if( !dest )
        return;

    if( !IsOK( this, wxString::Format( _( "Remove output container '%s'?" ),
                                       dest->GetDescription() ) ) )
    {
        return;
    }

    // The panel calling us is destroyed by the rebuild; defer it
    CallAfter(
            [this, aDestinationId]()
            {
                if( JOBSET_DESTINATION* d = FindDestination( aDestinationId ) )
                {
                    m_jobSet->RemoveDestination( d );
                    setDirty();
                    rebuildContainers();
                    rebuildGrid();
                }
            } );
}


void OUTJOB_FRAME::addContainer( int aType )
{
    JOBSET_DESTINATION_T type = aType == ID_ADD_ARCHIVE ? JOBSET_DESTINATION_T::ARCHIVE
                                                       : JOBSET_DESTINATION_T::FOLDER;
    bool                 wasDirty = m_jobSet->GetDirty();
    JOBSET_DESTINATION*  dest = m_jobSet->AddNewDestination( type );

    dest->SetDescription( containerKindName( type ) );

    wxString base = wxString::Format( wxS( "Project Outputs for %s" ),
                                      wxFileName( GetFilePath() ).GetName() );
    dest->m_outputHandler->SetOutputPath( type == JOBSET_DESTINATION_T::ARCHIVE
                                                  ? base + wxS( ".zip" )
                                                  : base );

    wxString id = dest->m_id;
    DIALOG_DESTINATION dlg( this, m_jobSet.get(), dest );

    if( dlg.ShowModal() == wxID_OK )
    {
        m_selectedContainer = id;
        setDirty();
    }
    else if( JOBSET_DESTINATION* d = FindDestination( id ) )
    {
        m_jobSet->RemoveDestination( d );
        m_jobSet->SetDirty( wasDirty );
    }

    rebuildContainers();
    rebuildGrid();
}


int OUTJOB_FRAME::selectedJob() const
{
    int row = m_grid->GetGridCursorRow();

    if( row < 0 || row >= (int) m_rows.size() || m_rows[row].kind != ROW::JOB )
        return -1;

    return (int) m_rows[row].job;
}


void OUTJOB_FRAME::ensureEditorsOpen()
{
    KIWAY&   kiway = m_manager->Kiway();
    PROJECT& project = kiway.Prj();

    auto open =
            [&]( FRAME_T aType, const wxString& aExt )
            {
                if( kiway.Player( aType, false ) )
                    return;

                wxFileName fn( project.GetProjectFullName() );
                fn.SetExt( aExt );

                if( !fn.FileExists() )
                    return;

                KIWAY_PLAYER* frame = kiway.Player( aType, true );

                if( !frame )
                    return;

                wxEventBlocker blocker( this );
                frame->OpenProjectFiles( std::vector<wxString>( 1, fn.GetFullPath() ) );

                // Loaded for the jobs: a tab behind the current document, not a switch
                DOCUMENT_WINDOWS::OpenInBackground( frame );
            };

    open( FRAME_PCB_EDITOR, FILEEXT::PcbFileExtension );
    open( FRAME_SCH, FILEEXT::KiCadSchematicFileExtension );
}


bool OUTJOB_FRAME::configureJob( size_t aJobIdx )
{
    if( aJobIdx >= m_jobSet->GetJobs().size() )
        return false;

    JOBSET_JOB&   job = m_jobSet->GetJobs()[aJobIdx];
    KIWAY::FACE_T face = JOB_REGISTRY::GetKifaceType( job.m_type );
    bool          ok = false;

    if( face < KIWAY::KIWAY_FACE_COUNT )
    {
        ensureEditorsOpen();
        ok = m_manager->Kiway().ProcessJobConfigDialog( face, job.m_job.get(), this );
    }
    else if( job.m_job && job.m_job->GetType() == "special_execute" )
    {
        DIALOG_EXECUTECOMMAND_JOB_SETTINGS dlg( this,
                                                static_cast<JOB_SPECIAL_EXECUTE*>( job.m_job.get() ),
                                                &m_manager->Prj() );
        ok = dlg.ShowQuasiModal() == wxID_OK;
    }
    else if( job.m_job && job.m_job->GetType() == "special_copyfiles" )
    {
        DIALOG_COPYFILES_JOB_SETTINGS dlg( this,
                                           static_cast<JOB_SPECIAL_COPYFILES*>( job.m_job.get() ) );
        ok = dlg.ShowModal() == wxID_OK;
    }
    else
    {
        DisplayErrorMessage( this, wxString::Format( _( "This version of KiCad does not support "
                                                        "the '%s' output type." ),
                                                     job.m_type ) );
    }

    if( ok )
        setDirty();

    Raise();
    return ok;
}


void OUTJOB_FRAME::addJob( const wxString& aType )
{
    m_grid->SaveEditControlValue();

    JOB* job = JOB_REGISTRY::CreateInstance<JOB>( aType );

    if( !job )
        return;

    bool   wasDirty = m_jobSet->GetDirty();
    size_t idx = m_jobSet->GetJobs().size();

    // A new Gerber output starts with the usual fabrication layers ticked, as Altium's Gerber
    // setup does; the default selection would otherwise be only the (unused) inner layers.
    // Layers the board doesn't have are skipped when plotting.
    if( JOB_EXPORT_PCB_GERBERS* gerbers = dynamic_cast<JOB_EXPORT_PCB_GERBERS*>( job ) )
    {
        LSET fab = LSET::AllCuMask()
                   | LSET( { F_Mask, B_Mask, F_Paste, B_Paste, F_SilkS, B_SilkS, Edge_Cuts } );

        gerbers->m_plotLayerSequence = fab.SeqStackupForPlotting();
    }

    m_jobSet->AddNewJob( aType, job );

    // New outputs follow the Output Job's variant choice
    int sel = m_variantChoice->GetSelection();

    if( sel >= 0 && sel < (int) m_variantValues.size()
            && !m_variantValues[sel].StartsWith( wxS( "\x01" ) ) )
    {
        setJobVariant( job, m_variantValues[sel] );
    }

    if( !configureJob( idx ) )
    {
        m_jobSet->RemoveJob( idx );
        m_jobSet->SetDirty( wasDirty );
        updateTitle();
        return;
    }

    // Containers that list their outputs explicitly get the new one if they are selected
    wxString id = m_jobSet->GetJobs()[idx].m_id;

    if( JOBSET_DESTINATION* dest = FindDestination( m_selectedContainer ) )
    {
        if( !dest->m_only.empty() )
        {
            std::erase( dest->m_only, NO_JOBS_SENTINEL );
            dest->m_only.push_back( id );
        }
    }

    rebuildGrid();

    for( int row = 0; row < (int) m_rows.size(); ++row )
    {
        if( m_rows[row].kind == ROW::JOB && m_rows[row].job == idx )
        {
            m_grid->SetGridCursor( row, COL_DESC );
            m_grid->SelectRow( row );
            m_grid->MakeCellVisible( row, COL_DESC );
            break;
        }
    }
}


void OUTJOB_FRAME::deleteJob( size_t aJobIdx )
{
    std::vector<JOBSET_JOB>& jobs = m_jobSet->GetJobs();

    if( aJobIdx >= jobs.size() )
        return;

    if( !IsOK( this, wxString::Format( _( "Delete output '%s'?" ),
                                       jobs[aJobIdx].GetDescription() ) ) )
    {
        return;
    }

    wxString id = jobs[aJobIdx].m_id;

    for( JOBSET_DESTINATION& dest : m_jobSet->GetDestinations() )
    {
        if( dest.m_only.empty() )
            continue;

        std::erase( dest.m_only, id );

        // An explicit list that loses its last job must not turn into "every job"
        if( dest.m_only.empty() )
            dest.m_only.push_back( NO_JOBS_SENTINEL );
    }

    m_jobSet->RemoveJob( aJobIdx );
    setDirty();
    updateVariantChoice();
    rebuildGrid();
}


void OUTJOB_FRAME::moveJob( size_t aJobIdx, bool aUp )
{
    std::vector<JOBSET_JOB>& jobs = m_jobSet->GetJobs();

    if( aJobIdx >= jobs.size() )
        return;

    int cat = categoryOf( jobs[aJobIdx].m_type );
    int target = -1;

    if( aUp )
    {
        for( int i = (int) aJobIdx - 1; i >= 0; --i )
        {
            if( categoryOf( jobs[i].m_type ) == cat )
            {
                target = i;
                break;
            }
        }
    }
    else
    {
        for( int i = (int) aJobIdx + 1; i < (int) jobs.size(); ++i )
        {
            if( categoryOf( jobs[i].m_type ) == cat )
            {
                target = i;
                break;
            }
        }
    }

    if( target < 0 )
    {
        wxBell();
        return;
    }

    wxString id = jobs[aJobIdx].m_id;
    size_t   idx = aJobIdx;

    while( idx != (size_t) target )
    {
        if( aUp )
            m_jobSet->MoveJobUp( idx-- );
        else
            m_jobSet->MoveJobDown( idx++ );
    }

    // Keep explicit container lists in job set order (that is the run order)
    for( JOBSET_DESTINATION& dest : m_jobSet->GetDestinations() )
    {
        if( dest.m_only.empty() || dest.m_only.front() == NO_JOBS_SENTINEL )
            continue;

        std::vector<wxString> ordered;

        for( const JOBSET_JOB& job : jobs )
        {
            if( std::find( dest.m_only.begin(), dest.m_only.end(), job.m_id ) != dest.m_only.end() )
                ordered.push_back( job.m_id );
        }

        dest.m_only = ordered;
    }

    setDirty();
    rebuildGrid();

    for( int row = 0; row < (int) m_rows.size(); ++row )
    {
        if( m_rows[row].kind == ROW::JOB && jobs[m_rows[row].job].m_id == id )
        {
            m_grid->SetGridCursor( row, COL_NAME );
            m_grid->SelectRow( row );
            break;
        }
    }
}


void OUTJOB_FRAME::toggleEnabled( size_t aJobIdx )
{
    JOBSET_DESTINATION*      dest = FindDestination( m_selectedContainer );
    std::vector<JOBSET_JOB>& jobs = m_jobSet->GetJobs();

    if( !dest || aJobIdx >= jobs.size() )
        return;

    std::vector<wxString> enabled;

    for( const JOBSET_JOB& job : jobs )
    {
        bool on = jobEnabledFor( dest, job.m_id );

        if( job.m_id == jobs[aJobIdx].m_id )
            on = !on;

        if( on )
            enabled.push_back( job.m_id );
    }

    if( enabled.empty() )
        enabled.push_back( NO_JOBS_SENTINEL );

    dest->m_only = enabled;
    dest->m_lastRunSuccess = std::nullopt;
    setDirty();
    rebuildGrid();
}


bool OUTJOB_FRAME::save()
{
    m_grid->SaveEditControlValue();

    if( !m_jobSet->SaveToFile( wxEmptyString, true ) )
    {
        DisplayErrorMessage( this, wxString::Format( _( "Could not save '%s'." ), GetFilePath() ) );
        return false;
    }

    m_jobSet->SetDirty( false );
    updateTitle();
    return true;
}


void OUTJOB_FRAME::runDestinations( const std::vector<wxString>& aIds )
{
    m_grid->SaveEditControlValue();

    std::vector<JOBSET_DESTINATION*> dests;

    for( const wxString& id : aIds )
    {
        if( JOBSET_DESTINATION* dest = FindDestination( id ) )
        {
            if( !m_jobSet->GetJobsForDestination( dest ).empty() )
                dests.push_back( dest );
        }
    }

    if( dests.empty() )
    {
        DisplayInfoMessage( this, _( "No outputs are enabled for the output container(s)." ) );
        return;
    }

    PROJECT& project = m_manager->Kiway().Prj();

    if( project.IsNullProject() )
    {
        DisplayErrorMessage( this, _( "Open the project this Output Job belongs to first." ) );
        return;
    }

    wxFileName jobDir( GetFilePath() );
    wxFileName prjDir( project.GetProjectFullName() );

    if( normalized( jobDir.GetPath() ) != normalized( prjDir.GetPath() )
            && !IsOK( this, wxString::Format( _( "This Output Job is not in the folder of the open "
                                                 "project '%s'.  Generate the outputs from that "
                                                 "project anyway?" ),
                                              project.GetProjectName() ) ) )
    {
        return;
    }

    for( JOBSET_DESTINATION* dest : dests )
        dest->m_lastRunSuccess = std::nullopt;

    for( OUTJOB_CONTAINER_PANEL* panel : m_containerPanels )
        panel->UpdateInfo();

    SetStatusText( _( "Generating outputs..." ) );

    std::vector<wxString> ids;

    for( JOBSET_DESTINATION* dest : dests )
        ids.push_back( dest->m_id );

    CallAfter(
            [this, ids]()
            {
                PROJECT& prj = m_manager->Kiway().Prj();

                ensureEditorsOpen();
                wxSetWorkingDirectory( wxFileName( prj.GetProjectFullName() ).GetPath() );

                int ok = 0;
                int failed = 0;

                {
                    JOBS_PROGRESS_REPORTER progress( this, _( "Generating Outputs" ) );
                    JOBS_RUNNER            runner( &m_manager->Kiway(), m_jobSet.get(), &prj,
                                                   NULL_REPORTER::GetInstance(), &progress );

                    for( const wxString& id : ids )
                    {
                        if( JOBSET_DESTINATION* dest = FindDestination( id ) )
                        {
                            if( runner.RunJobsForDestination( dest ) )
                                ok++;
                            else
                                failed++;
                        }
                    }
                }

                for( OUTJOB_CONTAINER_PANEL* panel : m_containerPanels )
                    panel->UpdateInfo();

                m_containerList->FitInside();

                SetStatusText( wxString::Format( _( "Generate Content: %d container(s) succeeded, "
                                                    "%d failed" ),
                                                 ok, failed ) );

                Raise();
            } );
}


void OUTJOB_FRAME::GenerateContainer( const wxString& aDestinationId )
{
    SelectContainer( aDestinationId );
    runDestinations( { aDestinationId } );
}


void OUTJOB_FRAME::OpenContainerOutput( const wxString& aDestinationId )
{
    JOBSET_DESTINATION* dest = FindDestination( aDestinationId );

    if( !dest )
        return;

    wxString path;

    if( dest->m_lastResolvedOutputPath.has_value() )
    {
        path = dest->m_lastResolvedOutputPath.value();
    }
    else
    {
        PROJECT* prj = &m_manager->Prj();
        path = ExpandEnvVarSubstitutions( ExpandTextVars( dest->GetPathInfo(), prj ), prj );

        if( path.StartsWith( wxS( "~" ) ) )
            path.Replace( wxS( "~" ), wxGetHomeDir(), false );
    }

    if( path.IsEmpty() )
        return;

    wxFileName fn( path );

    if( !fn.IsAbsolute() )
        fn.MakeAbsolute( wxFileName( m_manager->Prj().GetProjectFullName() ).GetPath() );

    if( !fn.Exists() )
    {
        DisplayInfoMessage( this, wxString::Format( _( "'%s' does not exist yet.  Use Generate "
                                                       "Content first." ),
                                                    fn.GetFullPath() ) );
        return;
    }

    if( !LaunchExternal( fn.GetFullPath() ) )
        DisplayError( this, wxString::Format( _( "Failed to open '%s'." ), fn.GetFullPath() ) );
}


void OUTJOB_FRAME::ShowContainerLog( const wxString& aDestinationId )
{
    JOBSET_DESTINATION* dest = FindDestination( aDestinationId );

    if( !dest || !dest->m_lastRunSuccess.has_value() )
        return;

    wxString text;

    for( const JOBSET_JOB& job : m_jobSet->GetJobsForDestination( dest ) )
    {
        wxString status = _( "not run" );

        if( dest->m_lastRunSuccessMap.contains( job.m_id )
                && dest->m_lastRunSuccessMap[job.m_id].has_value() )
        {
            status = dest->m_lastRunSuccessMap[job.m_id].value() ? _( "OK" ) : _( "FAILED" );
        }

        text << wxS( "== " ) << outputName( job.m_type ) << wxS( ": " ) << job.GetDescription()
             << wxS( "  [" ) << status << wxS( "]\n" );

        if( dest->m_lastRunReporters.contains( job.m_id ) && dest->m_lastRunReporters[job.m_id] )
            text << dest->m_lastRunReporters[job.m_id]->GetMessages() << wxS( "\n" );

        text << wxS( "\n" );
    }

    wxDialog dlg( this, wxID_ANY,
                  wxString::Format( _( "Generate Content Log - %s" ), dest->GetDescription() ),
                  wxDefaultPosition, FromDIP( wxSize( 700, 450 ) ),
                  wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER );
    wxBoxSizer* sizer = new wxBoxSizer( wxVERTICAL );
    wxTextCtrl* ctrl = new wxTextCtrl( &dlg, wxID_ANY, text, wxDefaultPosition, wxDefaultSize,
                                       wxTE_MULTILINE | wxTE_READONLY | wxHSCROLL );
    ctrl->SetFont( wxFont( wxFontInfo( ctrl->GetFont().GetPointSize() ).Family( wxFONTFAMILY_TELETYPE ) ) );
    sizer->Add( ctrl, 1, wxEXPAND | wxALL, 8 );
    sizer->Add( dlg.CreateStdDialogButtonSizer( wxOK ), 0, wxEXPAND | wxALL, 8 );
    dlg.SetSizer( sizer );
    dlg.ShowModal();
}


// ---------------------------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------------------------

void OUTJOB_FRAME::onGridLeftClick( wxGridEvent& aEvent )
{
    int row = aEvent.GetRow();

    if( row < 0 || row >= (int) m_rows.size() )
    {
        aEvent.Skip();
        return;
    }

    const ROW& r = m_rows[row];

    if( r.kind == ROW::ADD )
    {
        m_grid->SaveEditControlValue();
        wxMenu menu;
        appendAddMenu( &menu, r.category );
        PopupMenu( &menu );
        return;
    }

    if( r.kind == ROW::JOB && aEvent.GetCol() == COL_ENABLED )
    {
        m_grid->SetGridCursor( row, COL_NAME );
        toggleEnabled( r.job );
        return;
    }

    if( r.kind == ROW::CATEGORY )
    {
        m_grid->SetGridCursor( row, COL_NAME );
        return;
    }

    aEvent.Skip();
}


void OUTJOB_FRAME::onGridDClick( wxGridEvent& aEvent )
{
    int row = aEvent.GetRow();

    if( row >= 0 && row < (int) m_rows.size() && m_rows[row].kind == ROW::JOB
            && aEvent.GetCol() != COL_DESC && aEvent.GetCol() != COL_ENABLED )
    {
        size_t job = m_rows[row].job;
        CallAfter( [this, job]() { configureJob( job ); } );
        return;
    }

    aEvent.Skip();
}


void OUTJOB_FRAME::onGridRightClick( wxGridEvent& aEvent )
{
    int row = aEvent.GetRow();

    if( row >= 0 && row < (int) m_rows.size() )
    {
        m_grid->SetGridCursor( row, std::max( 0, aEvent.GetCol() ) );
        m_grid->SelectRow( row );
    }

    bool isJob = selectedJob() >= 0;

    wxMenu menu;

    for( int cat = 0; cat < CAT_COUNT; ++cat )
    {
        wxMenu* sub = new wxMenu();
        appendAddMenu( sub, cat );
        menu.AppendSubMenu( sub, wxGetTranslation( g_categories[cat].addLabel ) );
    }

    menu.AppendSeparator();
    menu.Append( ID_CONFIGURE, _( "Configure..." ) )->Enable( isJob );
    menu.Append( ID_RENAME, _( "Edit Output Description" ) )->Enable( isJob );
    menu.Append( ID_DELETE, _( "Delete" ) )->Enable( isJob );
    menu.AppendSeparator();
    menu.Append( ID_MOVE_UP, _( "Move Up" ) )->Enable( isJob );
    menu.Append( ID_MOVE_DOWN, _( "Move Down" ) )->Enable( isJob );
    menu.AppendSeparator();
    menu.Append( ID_GENERATE_SELECTED, _( "Generate Selected Container" ) )
            ->Enable( FindDestination( m_selectedContainer ) != nullptr );

    PopupMenu( &menu );
}


void OUTJOB_FRAME::onGridCellChanged( wxGridEvent& aEvent )
{
    int row = aEvent.GetRow();

    if( row >= 0 && row < (int) m_rows.size() && m_rows[row].kind == ROW::JOB
            && aEvent.GetCol() == COL_DESC )
    {
        JOBSET_JOB& job = m_jobSet->GetJobs()[m_rows[row].job];
        job.SetDescription( m_grid->GetCellValue( row, COL_DESC ) );

        // An empty description falls back to the job's default one
        m_grid->SetCellValue( row, COL_DESC, job.GetDescription() );
        setDirty();
    }
}


void OUTJOB_FRAME::onMenu( wxCommandEvent& aEvent )
{
    int id = aEvent.GetId();

    if( id >= ID_ADD_FIRST && id < ID_ADD_LAST )
    {
        std::vector<std::pair<int, wxString>> types = addableTypes();
        size_t                                idx = id - ID_ADD_FIRST;

        if( idx < types.size() )
        {
            wxString type = types[idx].second;
            CallAfter( [this, type]() { addJob( type ); } );
        }

        return;
    }

    int job = selectedJob();

    switch( id )
    {
    case ID_SAVE:
        save();
        break;

    case ID_CLOSE:
        Close();
        break;

    case ID_OPEN_PROJECT_FOLDER:
        LaunchExternal( wxFileName( GetFilePath() ).GetPath() );
        break;

    case ID_CONFIGURE:
        if( job >= 0 )
            CallAfter( [this, job]() { configureJob( job ); } );

        break;

    case ID_RENAME:
    {
        int row = m_grid->GetGridCursorRow();

        if( job >= 0 )
        {
            m_grid->SetGridCursor( row, COL_DESC );
            m_grid->EnableCellEditControl();
        }

        break;
    }

    case ID_DELETE:
        if( job >= 0 )
            deleteJob( job );

        break;

    case ID_MOVE_UP:
    case ID_MOVE_DOWN:
        if( job >= 0 )
            moveJob( job, id == ID_MOVE_UP );

        break;

    case ID_ENABLE_ALL:
    case ID_DISABLE_ALL:
        if( JOBSET_DESTINATION* dest = FindDestination( m_selectedContainer ) )
        {
            dest->m_only.clear();

            if( id == ID_DISABLE_ALL )
                dest->m_only.push_back( NO_JOBS_SENTINEL );

            dest->m_lastRunSuccess = std::nullopt;
            setDirty();
            rebuildGrid();
        }

        break;

    case ID_GENERATE_SELECTED:
        if( FindDestination( m_selectedContainer ) )
            runDestinations( { m_selectedContainer } );

        break;

    case ID_GENERATE_ALL:
    {
        std::vector<wxString> ids;

        for( const JOBSET_DESTINATION& dest : m_jobSet->GetDestinations() )
            ids.push_back( dest.m_id );

        runDestinations( ids );
        break;
    }

    case ID_ADD_FOLDER:
    case ID_ADD_ARCHIVE:
        CallAfter( [this, id]() { addContainer( id ); } );
        break;

    default:
        aEvent.Skip();
        break;
    }
}


bool OUTJOB_FRAME::ConfirmClose( bool aCanVeto )
{
    m_grid->SaveEditControlValue();

    if( !m_jobSet->GetDirty() )
        return true;

    wxString msg = wxString::Format( _( "Save changes to '%s' before closing?" ),
                                     wxFileName( GetFilePath() ).GetFullName() );

    if( !aCanVeto )
    {
        if( IsOK( this, msg ) )
            save();

        return true;
    }

    return HandleUnsavedChanges( this, msg,
                                 [this]() -> bool
                                 {
                                     return save();
                                 } );
}


void OUTJOB_FRAME::onClose( wxCloseEvent& aEvent )
{
    if( !ConfirmClose( aEvent.CanVeto() ) )
    {
        aEvent.Veto();
        return;
    }

    Destroy();
}


void OUTJOB_FRAME::onManagerClose( wxCloseEvent& aEvent )
{
    // The project manager is going away: settle unsaved changes, then go with it
    if( !ConfirmClose( aEvent.CanVeto() ) )
    {
        aEvent.Veto();
        return;
    }

    m_manager->Unbind( wxEVT_CLOSE_WINDOW, &OUTJOB_FRAME::onManagerClose, this );
    m_manager = nullptr;
    Hide();
    Destroy();
    aEvent.Skip();
}
