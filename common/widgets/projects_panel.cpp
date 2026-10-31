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

#include <widgets/projects_panel.h>

#include <bitmaps.h>
#include <confirm.h>
#include <document_windows.h>
#include <eda_base_frame.h>
#include <executable_names.h>
#include <frame_type.h>
#include <gestfich.h>
#include <kiface_base.h>
#include <kiway.h>
#include <kiid.h>
#include <kiway_player.h>
#include <libraries/library_manager.h>
#include <libraries/library_table.h>
#include <mail_type.h>
#include <pgm_base.h>
#include <project.h>
#include <settings/settings_manager.h>
#include <tool/tool_manager.h>
#include <string_utils.h>
#include <wildcards_and_files_ext.h>
#include <widgets/ui_common.h>
#include <outputs/output_job_link.h>

#include <wx/app.h>
#include <wx/bmpbuttn.h>
#include <wx/statline.h>
#include <wx/dir.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/ffile.h>
#include <wx/textdlg.h>
#include <wx/menu.h>
#include <wx/regex.h>
#include <wx/sizer.h>
#include <wx/srchctrl.h>
#include <wx/textfile.h>

#include <algorithm>
#include <functional>


namespace
{
struct DOC_DATA : public wxTreeItemData
{
    explicit DOC_DATA( const wxString& aPath ) : m_Path( aPath ) {}
    wxString m_Path;
};


/// Files in aDir (not recursive) with extension aExt, sorted by name.
std::vector<wxString> filesWithExt( const wxString& aDir, const wxString& aExt )
{
    std::vector<wxString> files;
    wxDir                 dir( aDir );
    wxString              name;

    if( dir.IsOpened() && dir.GetFirst( &name, wxS( "*." ) + aExt, wxDIR_FILES ) )
    {
        do
        {
            files.push_back( wxFileName( aDir, name ).GetFullPath() );
        } while( dir.GetNext( &name ) );
    }

    std::sort( files.begin(), files.end() );
    return files;
}


/// Sub-sheet files referenced by a schematic, in file order (without loading the schematic).
std::vector<wxString> sheetFilesOf( const wxString& aSchematic )
{
    std::vector<wxString> sheets;
    wxTextFile            file;

    if( !wxFileExists( aSchematic ) || !file.Open( aSchematic ) )
        return sheets;

    wxString dir = wxFileName( aSchematic ).GetPath();
    wxRegEx  sheetfileRe( wxS( "\\(property\\s+\"Sheetfile\"\\s+\"([^\"]+)\"" ) );

    for( wxString line = file.GetFirstLine(); !file.Eof(); line = file.GetNextLine() )
    {
        if( sheetfileRe.Matches( line ) )
        {
            wxFileName sheetFn( sheetfileRe.GetMatch( line, 1 ) );

            if( !sheetFn.IsAbsolute() )
                sheetFn.MakeAbsolute( dir );

            sheets.push_back( sheetFn.GetFullPath() );
        }
    }

    return sheets;
}

/// The project manager when it hosts the editors, else nullptr (an editor run on its own).
EDA_BASE_FRAME* projectManager( EDA_BASE_FRAME* aFrame )
{
    // dynamic_cast doesn't work over the Kiway() on macOS (see COMMON_CONTROL)
    EDA_BASE_FRAME* top = static_cast<EDA_BASE_FRAME*>( aFrame->Kiway().GetTop() );

    return top && top->GetFrameType() == KICAD_MAIN_FRAME_T ? top : nullptr;
}


/// Shows an editor of the project (opening the project's schematic or board in it if it has
/// no document yet).  nullptr when that editor is not available.
KIWAY_PLAYER* showEditor( EDA_BASE_FRAME* aFrame, FRAME_T aType )
{
    if( aFrame->IsType( aType ) )
        return static_cast<KIWAY_PLAYER*>( aFrame );

    if( !projectManager( aFrame ) )
        return nullptr;

    KIWAY_PLAYER* player = aFrame->Kiway().Player( aType, true );

    if( !player )
        return nullptr;

    PROJECT& prj = aFrame->Prj();
    wxString file;

    if( aType == FRAME_SCH )
        file = wxFileName( prj.GetProjectPath(), prj.GetProjectName(), FILEEXT::KiCadSchematicFileExtension ).GetFullPath();
    else if( aType == FRAME_PCB_EDITOR )
        file = wxFileName( prj.GetProjectPath(), prj.GetProjectName(), FILEEXT::KiCadPcbFileExtension ).GetFullPath();

    if( !DOCUMENT_WINDOWS::IsOpen( player ) && !file.IsEmpty() )
        player->OpenProjectFiles( std::vector<wxString>( 1, file ) );

    player->Show( true );

    if( player->IsIconized() )
        player->Iconize( false );

    player->Raise();
    return player;
}


/// Runs a tool action (by name) in one of the project's editors.
void runInEditor( EDA_BASE_FRAME* aFrame, FRAME_T aType, const char* aAction )
{
    if( KIWAY_PLAYER* player = showEditor( aFrame, aType ) )
        player->GetToolManager()->RunAction( aAction );
}


/// Runs a project manager action (by name) once the menu has closed: some of them close the
/// editor the menu was opened from.
void runInManager( EDA_BASE_FRAME* aFrame, const char* aAction )
{
    if( EDA_BASE_FRAME* manager = projectManager( aFrame ) )
    {
        std::string action( aAction );
        manager->CallAfter( [manager, action]() { manager->GetToolManager()->RunAction( action ); } );
    }
}


/// Add New to Project > Schematic: an empty sheet in the project folder (listed among the
/// project's source documents, ready to be placed as a sheet symbol).
void addNewSchematic( wxWindow* aParent, PROJECT& aPrj )
{
    wxString name;

    for( int ii = 1; name.IsEmpty()
                     || wxFileExists( wxFileName( aPrj.GetProjectPath(), name ).GetFullPath() );
         ++ii )
    {
        name = wxString::Format( wxS( "Sheet%d.kicad_sch" ), ii );
    }

    wxTextEntryDialog dlg( aParent, _( "File name:" ), _( "Add New Schematic to Project" ), name );

    if( dlg.ShowModal() != wxID_OK )
        return;

    wxString   entered = dlg.GetValue();
    wxFileName fn( aPrj.GetProjectPath(), entered.Trim().Trim( false ) );

    if( fn.GetName().IsEmpty() )
        return;

    fn.SetExt( FILEEXT::KiCadSchematicFileExtension );

    if( fn.FileExists() )
    {
        DisplayError( aParent, wxString::Format( _( "'%s' already exists." ), fn.GetFullName() ) );
        return;
    }

    wxFFile file( fn.GetFullPath(), wxS( "wb" ) );

    if( !file.IsOpened() )
        return;

    file.Write( wxString::Format( wxS( "(kicad_sch\n"
                                       "\t(version 20260101)\n"
                                       "\t(generator \"eeschema\")\n"
                                       "\t(generator_version \"10.0\")\n"
                                       "\t(uuid \"%s\")\n"
                                       "\t(paper \"A4\")\n"
                                       "\t(lib_symbols)\n"
                                       "\t(sheet_instances\n"
                                       "\t\t(path \"/\"\n"
                                       "\t\t\t(page \"1\")\n"
                                       "\t\t)\n"
                                       "\t)\n"
                                       "\t(embedded_fonts no)\n"
                                       ")\n" ),
                                  KIID().AsString() ) );
}


/// Add Existing to Project: libraries join the project's library tables (like Altium, they
/// stay where they are); other documents are copied into the project folder.
void addExisting( wxWindow* aParent, EDA_BASE_FRAME* aFrame )
{
    PROJECT&     prj = aFrame->Prj();
    wxFileDialog dlg( aParent, _( "Add Existing Document to Project" ), prj.GetProjectPath(),
                      wxEmptyString,
                      _( "KiCad documents" )
                              + wxS( " (*.kicad_sch;*.kicad_pcb;*.kicad_sym;*.kicad_mod)|"
                                     "*.kicad_sch;*.kicad_pcb;*.kicad_sym;*.kicad_mod|" )
                              + _( "All files" ) + wxS( " (*.*)|*.*" ),
                      wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_MULTIPLE );

    if( dlg.ShowModal() != wxID_OK )
        return;

    wxArrayString paths;
    dlg.GetPaths( paths );

    LIBRARY_MANAGER&             manager = Pgm().GetLibraryManager();
    std::set<LIBRARY_TABLE_TYPE> changed;

    for( const wxString& path : paths )
    {
        wxFileName         fn( path );
        wxString           ext = fn.GetExt().Lower();
        LIBRARY_TABLE_TYPE type = LIBRARY_TABLE_TYPE::UNINITIALIZED;

        // A footprint stands for the .pretty library holding it
        if( ext == FILEEXT::KiCadFootprintFileExtension
                && fn.GetPath().Lower().EndsWith( wxS( ".pretty" ) ) )
        {
            fn = wxFileName( fn.GetPath() );
            type = LIBRARY_TABLE_TYPE::FOOTPRINT;
        }
        else if( ext == FILEEXT::KiCadSymbolLibFileExtension )
        {
            type = LIBRARY_TABLE_TYPE::SYMBOL;
        }

        if( type == LIBRARY_TABLE_TYPE::UNINITIALIZED )
        {
            wxFileName dest( prj.GetProjectPath(), fn.GetFullName() );

            if( dest.SameAs( fn ) )
                continue;

            if( dest.FileExists()
                    && !IsOK( aParent, wxString::Format( _( "'%s' already exists in the project "
                                                            "folder.  Replace it?" ),
                                                         dest.GetFullName() ) ) )
            {
                continue;
            }

            wxCopyFile( fn.GetFullPath(), dest.GetFullPath() );
            continue;
        }

        std::optional<LIBRARY_TABLE*> table = manager.Table( type, LIBRARY_TABLE_SCOPE::PROJECT );

        if( !table || ( *table )->HasRow( fn.GetName() ) )
            continue;

        // Inside the project folder: a project-relative path keeps the table portable
        wxFileName rel( fn );
        wxString   uri = fn.GetFullPath();

        if( rel.MakeRelativeTo( prj.GetProjectPath() )
                && !rel.GetFullPath().StartsWith( wxS( ".." ) ) )
        {
            uri = wxS( "${KIPRJMOD}/" ) + rel.GetFullPath( wxPATH_UNIX );
        }

        LIBRARY_TABLE_ROW& row = ( *table )->InsertRow();
        row.SetNickname( fn.GetName() );
        row.SetURI( uri );
        row.SetType( wxS( "KiCad" ) );
        changed.insert( type );
    }

    for( LIBRARY_TABLE_TYPE type : changed )
    {
        if( std::optional<LIBRARY_TABLE*> table = manager.Table( type, LIBRARY_TABLE_SCOPE::PROJECT ) )
        {
            ( *table )->Save().map_error(
                    [&]( const LIBRARY_ERROR& aError )
                    {
                        DisplayErrorMessage( aParent, _( "Error saving project library table." ),
                                             aError.message );
                    } );
        }
    }

    if( !changed.empty() )
    {
        manager.AbortAsyncLoads();
        manager.LoadProjectTables( { LIBRARY_TABLE_TYPE::SYMBOL, LIBRARY_TABLE_TYPE::FOOTPRINT } );

        // Let the open editors list the new libraries
        std::string packet;

        for( FRAME_T type : { FRAME_SCH_SYMBOL_EDITOR, FRAME_FOOTPRINT_EDITOR } )
        {
            if( aFrame->Kiway().Player( type, false ) )
                aFrame->Kiway().ExpressMail( type, MAIL_RELOAD_LIB, packet );
        }
    }
}
} // namespace


PROJECTS_PANEL::PROJECTS_PANEL( wxWindow* aParent, KIWAY_PLAYER* aFrame ) :
        wxPanel( aParent ),
        m_frame( aFrame ),
        m_timer( this )
{
    wxBoxSizer* sizer = new wxBoxSizer( wxVERTICAL );

    // Altium's toolbar above the search box: Save, Save All, Explore, Project Options |
    // Preferences
    wxBoxSizer* tools = new wxBoxSizer( wxHORIZONTAL );

    auto addTool =
            [this, tools]( BITMAPS aIcon, const wxString& aTip, std::function<void()> aDo )
            {
                wxBitmapButton* btn = new wxBitmapButton( this, wxID_ANY,
                                                          KiBitmapBundleDef( aIcon, 24 ),
                                                          wxDefaultPosition, wxDefaultSize,
                                                          wxBORDER_NONE );
                btn->SetToolTip( aTip );
                btn->Bind( wxEVT_BUTTON, [aDo]( wxCommandEvent& ) { aDo(); } );
                tools->Add( btn, 0, wxRIGHT, FromDIP( 4 ) );
            };

    auto run =
            [this]( const char* aAction )
            {
                if( TOOL_MANAGER* mgr = m_frame->GetToolManager() )
                    mgr->RunAction( aAction );
            };

    bool inPcb = m_frame->IsType( FRAME_PCB_EDITOR ) || m_frame->IsType( FRAME_FOOTPRINT_EDITOR );

    addTool( BITMAPS::save, _( "Save Project" ),
             []() { Pgm().GetSettingsManager().SaveProject(); } );
    addTool( BITMAPS::save_all, _( "Save All" ),
             [run]() { run( "common.Control.saveAllDocuments" ); } );
    addTool( BITMAPS::directory_browser, _( "Explore" ),
             [this]() { wxLaunchDefaultApplication( m_frame->Prj().GetProjectPath() ); } );
    addTool( inPcb ? BITMAPS::options_board : BITMAPS::options_schematic, _( "Project Options" ),
             [run, inPcb]()
             {
                 run( inPcb ? "pcbnew.EditorControl.boardSetup"
                            : "eeschema.EditorControl.schematicSetup" );
             } );
    tools->Add( new wxStaticLine( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_VERTICAL ),
                0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP( 4 ) );
    addTool( BITMAPS::preference, _( "Preferences" ),
             [run]() { run( "common.SuiteControl.openPreferences" ); } );

    sizer->Add( tools, 0, wxLEFT | wxRIGHT | wxTOP, 4 );

    m_search = new wxSearchCtrl( this, wxID_ANY );
    m_search->ShowCancelButton( true );
    m_search->SetDescriptiveText( _( "Search" ) );
    sizer->Add( m_search, 0, wxEXPAND | wxALL, 4 );

    m_tree = new wxTreeCtrl( this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                             wxTR_HAS_BUTTONS | wxTR_HIDE_ROOT | wxTR_NO_LINES | wxTR_SINGLE
                                     | wxTR_FULL_ROW_HIGHLIGHT | wxTR_TWIST_BUTTONS
                                     | wxBORDER_NONE );
    sizer->Add( m_tree, 1, wxEXPAND );

    std::vector<wxBitmapBundle> images;
    images.push_back( KiBitmapBundleDef( BITMAPS::doc_project, 16 ) );
    images.push_back( KiBitmapBundleDef( BITMAPS::doc_folder, 16 ) );
    images.push_back( KiBitmapBundleDef( BITMAPS::doc_schematic, 16 ) );
    images.push_back( KiBitmapBundleDef( BITMAPS::doc_pcb, 16 ) );
    images.push_back( KiBitmapBundleDef( BITMAPS::doc_sch_lib, 16 ) );
    images.push_back( KiBitmapBundleDef( BITMAPS::doc_pcb_lib, 16 ) );
    images.push_back( KiBitmapBundleDef( BITMAPS::doc_settings, 16 ) );
    images.push_back( KiBitmapBundleDef( BITMAPS::unknown, 16 ) );
    images.push_back( KiBitmapBundleDef( BITMAPS::post_bom, 16 ) );
    images.push_back( KiBitmapBundleDef( BITMAPS::add_component, 16 ) );
    images.push_back( KiBitmapBundleDef( BITMAPS::net_highlight, 16 ) );
    m_tree->SetImages( images );

    SetSizer( sizer );
    SetFont( KIUI::GetDockedPaneFont( this ) );

    m_activeDocFunc = [this]() { return m_frame->GetCurrentFileName(); };

    m_search->Bind( wxEVT_TEXT, [this]( wxCommandEvent& ) { Rebuild(); } );
    m_search->Bind( wxEVT_SEARCH_CANCEL,
                    [this]( wxCommandEvent& )
                    {
                        m_search->ChangeValue( wxEmptyString );
                        Rebuild();
                    } );
    m_tree->Bind( wxEVT_TREE_ITEM_ACTIVATED, &PROJECTS_PANEL::onActivated, this );
    m_tree->Bind( wxEVT_TREE_ITEM_MENU, &PROJECTS_PANEL::onContextMenu, this );
    Bind( wxEVT_TIMER, &PROJECTS_PANEL::onTimer, this );

    // Pick up new, renamed or re-arranged documents without a manual refresh
    m_timer.Start( 2000 );
}


PROJECTS_PANEL::~PROJECTS_PANEL()
{
    m_timer.Stop();
}


bool PROJECTS_PANEL::matchesFilter( const wxString& aPath ) const
{
    wxString filter = m_search->GetValue().Lower();
    filter.Trim( true ).Trim( false );

    return filter.IsEmpty() || wxFileName( aPath ).GetFullName().Lower().Contains( filter );
}


std::set<wxString> PROJECTS_PANEL::modifiedDocuments() const
{
    // Altium marks documents with unsaved changes "Board.PcbDoc *"
    std::set<wxString> files;

    for( FRAME_T type : { FRAME_SCH, FRAME_PCB_EDITOR } )
    {
        KIWAY_PLAYER* player = m_frame->Kiway().Player( type, false );

        if( player && player->IsContentModified() && !player->GetCurrentFileName().IsEmpty() )
        {
            wxFileName fn( player->GetCurrentFileName() );
            fn.Normalize( wxPATH_NORM_ABSOLUTE | wxPATH_NORM_DOTS );
            files.insert( fn.GetFullPath() );
        }
    }

    return files;
}


wxTreeItemId PROJECTS_PANEL::addDocument( const wxTreeItemId& aParent, const wxString& aPath,
                                          ICON aIcon, const wxString& aPrefix )
{
    wxFileName fn( aPath );
    wxString   label = aPrefix + fn.GetFullName();

    if( !fn.FileExists() && !fn.DirExists() )
        label += wxS( "  " ) + _( "(missing)" );

    fn.Normalize( wxPATH_NORM_ABSOLUTE | wxPATH_NORM_DOTS );

    if( m_modified.count( fn.GetFullPath() ) )
        label += wxS( " *" );

    return m_tree->AppendItem( aParent, label, aIcon, aIcon, new DOC_DATA( aPath ) );
}


void PROJECTS_PANEL::addSheetChildren( const wxTreeItemId& aParent, const wxString& aSheet,
                                       std::set<wxString>& aStack, std::set<wxString>& aSeen )
{
    // Like Altium, a sheet used more than once is listed under each parent; aStack only
    // guards against recursive hierarchies.
    if( aStack.count( aSheet ) )
        return;

    aStack.insert( aSheet );

    for( const wxString& child : sheetFilesOf( aSheet ) )
    {
        aSeen.insert( child );

        // Sheet numbers in hierarchy order, as Altium prefixes them: "[2] Power.SchDoc"
        wxTreeItemId item = addDocument( aParent, child, ICON_SCHEMATIC,
                                         wxString::Format( wxS( "[%d] " ), ++m_pageCounter ) );
        addSheetChildren( item, child, aStack, aSeen );
    }

    aStack.erase( aSheet );
}


void PROJECTS_PANEL::pruneEmptyFolders( const wxTreeItemId& aItem )
{
    wxTreeItemIdValue cookie;
    std::vector<wxTreeItemId> children;

    for( wxTreeItemId child = m_tree->GetFirstChild( aItem, cookie ); child.IsOk();
         child = m_tree->GetNextChild( aItem, cookie ) )
    {
        children.push_back( child );
    }

    for( const wxTreeItemId& child : children )
    {
        pruneEmptyFolders( child );

        DOC_DATA* data = static_cast<DOC_DATA*>( m_tree->GetItemData( child ) );
        bool      isFolder = !data;
        bool      keep = isFolder ? m_tree->ItemHasChildren( child ) : matchesFilter( data->m_Path );

        // A matching child keeps its (non-matching) parent sheet visible
        if( !isFolder && !keep && m_tree->ItemHasChildren( child ) )
            keep = true;

        if( !keep )
            m_tree->Delete( child );
    }
}


void PROJECTS_PANEL::Rebuild()
{
    m_signature = signature();
    m_modified = modifiedDocuments();
    m_pageCounter = 0;

    PROJECT&   prj = m_frame->Prj();
    wxString   activeDoc = m_activeDocFunc ? m_activeDocFunc() : wxString();
    wxFileName activeFn( activeDoc );
    activeFn.Normalize( wxPATH_NORM_ABSOLUTE | wxPATH_NORM_DOTS );

    m_tree->Freeze();
    m_tree->DeleteAllItems();

    wxTreeItemId root = m_tree->AddRoot( wxS( "root" ) );

    if( prj.IsNullProject() )
    {
        // Altium lists documents opened without a project under "Free Documents"
        wxTreeItemId free = m_tree->AppendItem( root, _( "Free Documents" ), ICON_FOLDER );

        if( !activeDoc.IsEmpty() )
        {
            wxString ext = activeFn.GetExt().Lower();
            addDocument( free, activeFn.GetFullPath(),
                         ext == wxS( "kicad_pcb" ) ? ICON_PCB : ICON_SCHEMATIC );
        }
    }
    else
    {
        wxString dir = prj.GetProjectPath();
        wxString name = prj.GetProjectName();

        wxTreeItemId project = addDocument( root, prj.GetProjectFullName(), ICON_PROJECT );
        m_tree->SetItemBold( project );

        // The project shows "*" while any of its documents has unsaved changes
        if( !m_modified.empty() && !m_tree->GetItemText( project ).EndsWith( wxS( " *" ) ) )
            m_tree->SetItemText( project, m_tree->GetItemText( project ) + wxS( " *" ) );

        // Source documents: the schematic hierarchy from the root sheet, any other sheets in
        // the folder, then the boards
        wxTreeItemId sources = m_tree->AppendItem( project, _( "Source Documents" ), ICON_FOLDER );
        wxString     rootSch = wxFileName( dir, name, wxS( "kicad_sch" ) ).GetFullPath();
        std::set<wxString> seen;

        if( wxFileExists( rootSch ) )
        {
            std::set<wxString> stack;
            seen.insert( rootSch );

            wxTreeItemId rootItem = addDocument( sources, rootSch, ICON_SCHEMATIC,
                                                 wxString::Format( wxS( "[%d] " ),
                                                                   ++m_pageCounter ) );
            addSheetChildren( rootItem, rootSch, stack, seen );
        }

        for( const wxString& sch : filesWithExt( dir, wxS( "kicad_sch" ) ) )
        {
            if( !seen.count( sch ) )
                addDocument( sources, sch, ICON_SCHEMATIC );
        }

        // Altium's ActiveBOM entry: "+ Create" until the project has a BOM
        if( wxFileExists( rootSch ) )
        {
            m_tree->AppendItem( sources, _( "ActiveBOM" ) + wxS( "   + " ) + _( "Create" ), ICON_BOM,
                                ICON_BOM, new DOC_DATA( wxS( "?activebom" ) ) );
        }

        for( const wxString& pcb : filesWithExt( dir, wxS( "kicad_pcb" ) ) )
            addDocument( sources, pcb, ICON_PCB );

        // Libraries kept in the project folder
        std::vector<wxString> symLibs = filesWithExt( dir, wxS( "kicad_sym" ) );
        std::vector<wxString> fpLibs;
        wxDir                 projectDir( dir );
        wxString              entry;

        if( projectDir.IsOpened() && projectDir.GetFirst( &entry, wxS( "*.pretty" ), wxDIR_DIRS ) )
        {
            do
            {
                fpLibs.push_back( wxFileName( dir, entry ).GetFullPath() );
            } while( projectDir.GetNext( &entry ) );
        }

        // ... and those added to the project's library tables (Altium lists the libraries added
        // to the project, wherever they are)
        LIBRARY_MANAGER& libMgr = Pgm().GetLibraryManager();

        for( LIBRARY_TABLE_TYPE type : { LIBRARY_TABLE_TYPE::SYMBOL, LIBRARY_TABLE_TYPE::FOOTPRINT } )
        {
            std::vector<wxString>& list = type == LIBRARY_TABLE_TYPE::SYMBOL ? symLibs : fpLibs;
            const wxString         ext = type == LIBRARY_TABLE_TYPE::SYMBOL ? wxS( "kicad_sym" )
                                                                           : wxS( "pretty" );

            for( LIBRARY_TABLE_ROW* row : libMgr.Rows( type, LIBRARY_TABLE_SCOPE::PROJECT, true ) )
            {
                wxFileName fn( LIBRARY_MANAGER::GetFullURI( row, true ) );
                fn.Normalize( wxPATH_NORM_ABSOLUTE | wxPATH_NORM_DOTS, dir );

                if( row->Disabled() || fn.GetExt().Lower() != ext )
                    continue;

                bool known = std::any_of( list.begin(), list.end(),
                                          [&]( const wxString& aPath )
                                          {
                                              return wxFileName( aPath ).SameAs( fn );
                                          } );

                if( !known )
                    list.push_back( fn.GetFullPath() );
            }
        }

        std::sort( symLibs.begin(), symLibs.end() );
        std::sort( fpLibs.begin(), fpLibs.end() );

        if( !symLibs.empty() || !fpLibs.empty() )
        {
            wxTreeItemId libs = m_tree->AppendItem( project, _( "Libraries" ), ICON_FOLDER );

            if( !symLibs.empty() )
            {
                wxTreeItemId folder = m_tree->AppendItem( libs, _( "Schematic Library Documents" ),
                                                          ICON_FOLDER );

                for( const wxString& lib : symLibs )
                    addDocument( folder, lib, ICON_SCH_LIB );
            }

            if( !fpLibs.empty() )
            {
                wxTreeItemId folder = m_tree->AppendItem( libs, _( "PCB Library Documents" ),
                                                          ICON_FOLDER );

                for( const wxString& lib : fpLibs )
                    addDocument( folder, lib, ICON_PCB_LIB );
            }
        }

        // Altium lists the compiled project's components and nets (folded)
        std::vector<wxString> components;
        std::vector<wxString> nets;

        if( m_designDataFunc )
            m_designDataFunc( components, nets );

        // Natural order, as Altium lists them: R2 before R10
        auto natural = []( const wxString& a, const wxString& b ) { return StrNumCmp( a, b, true ) < 0; };
        std::sort( components.begin(), components.end(), natural );
        std::sort( nets.begin(), nets.end(), natural );

        wxTreeItemId compFolder = m_tree->AppendItem( project, _( "Components" ), ICON_FOLDER );

        for( const wxString& ref : components )
        {
            if( m_search->GetValue().IsEmpty()
                    || ref.Lower().Contains( m_search->GetValue().Lower() ) )
            {
                m_tree->AppendItem( compFolder, ref, ICON_COMPONENT, ICON_COMPONENT );
            }
        }

        wxTreeItemId netFolder = m_tree->AppendItem( project, _( "Nets" ), ICON_FOLDER );

        for( const wxString& net : nets )
        {
            if( m_search->GetValue().IsEmpty()
                    || net.Lower().Contains( m_search->GetValue().Lower() ) )
            {
                m_tree->AppendItem( netFolder, net, ICON_NET, ICON_NET );
            }
        }

        // Settings: rules, library tables and job sets
        wxTreeItemId settings = m_tree->AppendItem( project, _( "Settings" ), ICON_FOLDER );

        for( const wxString& ext : { wxS( "kicad_dru" ), wxS( "kicad_jobset" ) } )
        {
            for( const wxString& file : filesWithExt( dir, ext ) )
                addDocument( settings, file, ICON_SETTINGS );
        }

        for( const wxString& table : { wxS( "sym-lib-table" ), wxS( "fp-lib-table" ),
                                       wxS( "design-block-lib-table" ) } )
        {
            wxFileName fn( dir, table );

            if( fn.FileExists() )
                addDocument( settings, fn.GetFullPath(), ICON_SETTINGS );
        }

        if( !m_search->GetValue().IsEmpty() )
            pruneEmptyFolders( project );
    }

    // Open everything and highlight the document shown in this window
    std::function<void( const wxTreeItemId& )> visit =
            [&]( const wxTreeItemId& aItem )
            {
                wxTreeItemIdValue cookie;

                for( wxTreeItemId child = m_tree->GetFirstChild( aItem, cookie ); child.IsOk();
                     child = m_tree->GetNextChild( aItem, cookie ) )
                {
                    if( DOC_DATA* data = static_cast<DOC_DATA*>( m_tree->GetItemData( child ) ) )
                    {
                        wxFileName fn( data->m_Path );
                        fn.Normalize( wxPATH_NORM_ABSOLUTE | wxPATH_NORM_DOTS );

                        if( activeFn.IsOk() && fn.SameAs( activeFn )
                                && !m_tree->GetSelection().IsOk() )
                        {
                            m_tree->SelectItem( child );
                        }
                    }

                    visit( child );
                }
            };

    visit( root );

    // Settings stay folded like Altium's; everything else is open
    wxTreeItemIdValue cookie;

    for( wxTreeItemId top = m_tree->GetFirstChild( root, cookie ); top.IsOk();
         top = m_tree->GetNextChild( root, cookie ) )
    {
        m_tree->ExpandAllChildren( top );
        m_tree->Expand( top );

        wxTreeItemIdValue cookie2;

        for( wxTreeItemId child = m_tree->GetFirstChild( top, cookie2 ); child.IsOk();
             child = m_tree->GetNextChild( top, cookie2 ) )
        {
            wxString text = m_tree->GetItemText( child );

            if( ( text == _( "Settings" ) || text == _( "Components" ) || text == _( "Nets" ) )
                    && m_search->GetValue().IsEmpty() )
            {
                m_tree->Collapse( child );
            }
        }
    }

    m_tree->Thaw();
}


wxString PROJECTS_PANEL::signature() const
{
    PROJECT& prj = m_frame->Prj();
    wxString sig = m_activeDocFunc ? m_activeDocFunc() : wxString();

    sig << wxS( "|" ) << prj.GetProjectFullName();

    if( prj.IsNullProject() )
        return sig;

    for( const wxString& file : modifiedDocuments() )
        sig << wxS( "|*" ) << file;

    if( m_designDataFunc )
    {
        std::vector<wxString> components;
        std::vector<wxString> nets;
        m_designDataFunc( components, nets );

        for( const wxString& item : components )
            sig << wxS( "|c" ) << item;

        for( const wxString& item : nets )
            sig << wxS( "|n" ) << item;
    }

    wxDir    dir( prj.GetProjectPath() );
    wxString name;

    if( dir.IsOpened() && dir.GetFirst( &name ) )
    {
        do
        {
            sig << wxS( "|" ) << name;

            // Sheets added to or removed from a schematic change its contents, and libraries
            // added to or removed from the project change its tables
            if( name.EndsWith( wxS( ".kicad_sch" ) ) || name.EndsWith( wxS( "-lib-table" ) ) )
            {
                wxFileName fn( prj.GetProjectPath(), name );
                sig << wxS( "@" ) << fn.GetModificationTime().GetValue().ToString();
            }
        } while( dir.GetNext( &name ) );
    }

    return sig;
}


void PROJECTS_PANEL::onTimer( wxTimerEvent& aEvent )
{
    if( !IsShownOnScreen() )
        return;

    if( m_tree->IsEmpty() || signature() != m_signature )
        Rebuild();
}


void PROJECTS_PANEL::openDocument( const wxString& aPath )
{
    OpenDocument( m_frame, aPath );
}


void PROJECTS_PANEL::OpenDocument( KIWAY_PLAYER* aFrame, const wxString& aPath )
{
    // ActiveBOM "+ Create": KiCad's BOM generator in the schematic editor
    if( aPath == wxS( "?activebom" ) )
    {
        if( aFrame->IsType( FRAME_SCH ) )
            aFrame->GetToolManager()->RunAction( "eeschema.EditorControl.generateBOM" );
        else
            runInEditor( aFrame, FRAME_SCH, "eeschema.EditorControl.generateBOM" );

        return;
    }

    wxFileName fn( aPath );
    wxString   ext = fn.GetExt().Lower();
    KIWAY&     kiway = aFrame->Kiway();
    PROJECT&   prj = aFrame->Prj();
    wxString   rootSch = wxFileName( prj.GetProjectPath(), prj.GetProjectName(),
                                     wxS( "kicad_sch" ) ).GetFullPath();

    auto showPlayer =
            [&]( FRAME_T aType, const wxString& aFile ) -> KIWAY_PLAYER*
            {
                KIWAY_PLAYER* player = kiway.Player( aType, true );

                if( !player )
                    return nullptr;

                // A document parked behind the current one is open already
                if( !DOCUMENT_WINDOWS::IsOpen( player ) && !aFile.IsEmpty() )
                    player->OpenProjectFiles( std::vector<wxString>( 1, aFile ) );

                player->Show( true );

                if( player->IsIconized() )
                    player->Iconize( false );

                player->Raise();
                return player;
            };

    if( ext == wxS( "kicad_sch" ) )
    {
        bool inProject = !prj.IsNullProject() && wxFileExists( rootSch );

        if( Kiface().IsSingle() && !aFrame->IsType( FRAME_SCH ) )
        {
            ExecuteFile( EESCHEMA_EXE, inProject ? rootSch : aPath );
            return;
        }

        KIWAY_PLAYER* sch = aFrame->IsType( FRAME_SCH )
                                    ? aFrame
                                    : showPlayer( FRAME_SCH, inProject ? rootSch : aPath );

        if( sch )
        {
            std::string packet = aPath.ToStdString();
            kiway.ExpressMail( FRAME_SCH, MAIL_SCH_NAVIGATE_TO_SHEET, packet );
        }
    }
    else if( ext == wxS( "kicad_pcb" ) )
    {
        if( aFrame->IsType( FRAME_PCB_EDITOR ) )
            return;

        if( Kiface().IsSingle() )
            ExecuteFile( PCBNEW_EXE, aPath );
        else
            showPlayer( FRAME_PCB_EDITOR, aPath );
    }
    else if( ( ext == wxS( "kicad_sym" ) || ext == wxS( "pretty" ) ) && !Kiface().IsSingle() )
    {
        // As in Altium, the library opens in its editor with its contents listed in the
        // SCH Library / PCB Library panel
        bool    symbols = ext == wxS( "kicad_sym" );
        FRAME_T type = symbols ? FRAME_SCH_SYMBOL_EDITOR : FRAME_FOOTPRINT_EDITOR;

        if( showPlayer( type, wxEmptyString ) )
        {
            std::string packet = fn.GetFullPath().ToStdString();
            kiway.ExpressMail( type, symbols ? MAIL_LIB_EDIT : MAIL_FP_EDIT, packet );
        }
    }
    else if( ext == wxS( "kicad_jobset" ) )
    {
        // Output Job editor (lives in the project manager)
        OUTPUT_JOB_LINK::Open( aFrame, aFrame, aPath );
    }
}


void PROJECTS_PANEL::onActivated( wxTreeEvent& aEvent )
{
    if( DOC_DATA* data = static_cast<DOC_DATA*>( m_tree->GetItemData( aEvent.GetItem() ) ) )
        openDocument( data->m_Path );
    else
        m_tree->Toggle( aEvent.GetItem() );
}


void PROJECTS_PANEL::onContextMenu( wxTreeEvent& aEvent )
{
    DOC_DATA* data = static_cast<DOC_DATA*>( m_tree->GetItemData( aEvent.GetItem() ) );

    if( data && wxFileName( data->m_Path ).GetExt().Lower() == FILEEXT::ProjectFileExtension )
    {
        m_tree->SelectItem( aEvent.GetItem() );
        ShowProjectMenu( this, m_frame );
        return;
    }

    enum { ID_OPEN = wxID_HIGHEST + 1, ID_EXPLORE, ID_REFRESH };

    wxMenu menu;

    if( data )
    {
        menu.Append( ID_OPEN, _( "Open" ) );
        menu.Append( ID_EXPLORE, _( "Explore" ) );
        menu.AppendSeparator();
    }

    menu.Append( ID_REFRESH, _( "Refresh" ) );

    switch( GetPopupMenuSelectionFromUser( menu ) )
    {
    case ID_OPEN:
        openDocument( data->m_Path );
        break;

    case ID_EXPLORE:
        wxLaunchDefaultApplication( wxFileName( data->m_Path ).GetPath() );
        break;

    case ID_REFRESH:
        Rebuild();
        break;

    default:
        break;
    }
}

void PROJECTS_PANEL::ShowProjectMenu( wxWindow* aParent, EDA_BASE_FRAME* aFrame )
{
    enum PROJECT_MENU_ID
    {
        ID_VALIDATE = wxID_HIGHEST + 100,
        ID_NEW_SCHEMATIC,
        ID_NEW_PCB,
        ID_NEW_PCB3D,
        ID_NEW_ACTIVEBOM,
        ID_NEW_DRAFTSMAN,
        ID_NEW_SCH_LIB,
        ID_NEW_PCB_LIB,
        ID_NEW_PAD_VIA_LIB,
        ID_NEW_CAM,
        ID_NEW_OUTJOB,
        ID_NEW_DB_LINK,
        ID_ADD_EXISTING,
        ID_SAVE,
        ID_RENAME,
        ID_CLOSE_DOCUMENTS,
        ID_CLOSE_PROJECT,
        ID_EXPLORE,
        ID_VARIANTS,
        ID_VCS_ADD,
        ID_VCS_STORAGE,
        ID_VCS_LOCAL_HISTORY,
        ID_PACKAGER,
        ID_RELEASER,
        ID_SHARE,
        ID_OPTIONS
    };

    PROJECT&        prj = aFrame->Prj();
    EDA_BASE_FRAME* manager = projectManager( aFrame );
    bool            inPcb = aFrame->IsType( FRAME_PCB_EDITOR );
    bool            canSch = manager || aFrame->IsType( FRAME_SCH );
    wxFileName      board( prj.GetProjectPath(), prj.GetProjectName(),
                           FILEEXT::KiCadPcbFileExtension );

    wxMenu menu;

    // Laid out like Altium's; commands with no KiCad counterpart are listed but disabled
    KIUI::AddMenuItem( &menu, ID_VALIDATE,
                       wxString::Format( _( "Validate P&CB Project %s" ),
                                         wxFileName( prj.GetProjectFullName() ).GetFullName() ),
                       KiBitmapBundle( BITMAPS::erc ) );
    menu.AppendSeparator();

    wxMenu* addNew = new wxMenu();
    KIUI::AddMenuItem( addNew, ID_NEW_SCHEMATIC, _( "&Schematic" ), KiBitmapBundle( BITMAPS::icon_eeschema_16 ) );
    KIUI::AddMenuItem( addNew, ID_NEW_PCB, _( "&PCB" ), KiBitmapBundle( BITMAPS::icon_pcbnew_16 ) );
    addNew->Append( ID_NEW_PCB3D, _( "PCB3&D" ) );
    KIUI::AddMenuItem( addNew, ID_NEW_ACTIVEBOM, _( "Active&BOM" ), KiBitmapBundle( BITMAPS::post_bom ) );
    addNew->Append( ID_NEW_DRAFTSMAN, _( "Dra&ftsman Document" ) );
    addNew->AppendSeparator();
    KIUI::AddMenuItem( addNew, ID_NEW_SCH_LIB, _( "Schematic &Library" ), KiBitmapBundle( BITMAPS::icon_libedit_16 ) );
    KIUI::AddMenuItem( addNew, ID_NEW_PCB_LIB, _( "PCB Li&brary" ), KiBitmapBundle( BITMAPS::icon_modedit_16 ) );
    addNew->Append( ID_NEW_PAD_VIA_LIB, _( "Pad &Via Library" ) );
    addNew->AppendSeparator();
    KIUI::AddMenuItem( addNew, ID_NEW_CAM, _( "&CAM Document" ), KiBitmapBundle( BITMAPS::icon_gerbview_16 ) );
    KIUI::AddMenuItem( addNew, ID_NEW_OUTJOB, _( "&Output Job File" ), KiBitmapBundle( BITMAPS::jobset_open ) );
    addNew->Append( ID_NEW_DB_LINK, _( "Data&base Link File" ) );

    addNew->Enable( ID_NEW_SCHEMATIC, !prj.IsNullProject() );
    addNew->Enable( ID_NEW_PCB, manager && !board.FileExists() );   // one board per project
    addNew->Enable( ID_NEW_PCB3D, false );
    addNew->Enable( ID_NEW_ACTIVEBOM, canSch );
    addNew->Enable( ID_NEW_DRAFTSMAN, false );
    addNew->Enable( ID_NEW_SCH_LIB, manager || aFrame->IsType( FRAME_SCH_SYMBOL_EDITOR ) );
    addNew->Enable( ID_NEW_PCB_LIB, manager || aFrame->IsType( FRAME_FOOTPRINT_EDITOR ) );
    addNew->Enable( ID_NEW_PAD_VIA_LIB, false );
    addNew->Enable( ID_NEW_CAM, manager != nullptr );
    addNew->Enable( ID_NEW_OUTJOB, manager != nullptr );
    addNew->Enable( ID_NEW_DB_LINK, false );
    menu.AppendSubMenu( addNew, _( "Add &New to Project" ) );

    KIUI::AddMenuItem( &menu, ID_ADD_EXISTING, _( "&Add Existing to Project..." ),
                       KiBitmapBundle( BITMAPS::import_document ) );
    menu.Append( ID_SAVE, _( "Save" ) );
    menu.Append( ID_RENAME, _( "&Rename..." ) );
    menu.AppendSeparator();

    menu.Append( ID_CLOSE_DOCUMENTS, _( "Close Project Documents" ) );
    menu.Append( ID_CLOSE_PROJECT, _( "Close Project" ) );
    menu.Append( ID_EXPLORE, _( "Explore" ) );
    menu.AppendSeparator();

    KIUI::AddMenuItem( &menu, ID_VARIANTS, _( "&Variants..." ), KiBitmapBundle( BITMAPS::options_board ) );
    menu.AppendSeparator();

    wxMenu* history = new wxMenu();
    history->Append( ID_VCS_ADD, _( "Add Project to Version Control..." ) );
    history->Append( ID_VCS_STORAGE, _( "Storage Manager..." ) );
    history->Append( ID_VCS_LOCAL_HISTORY, _( "Local History (Legacy)" ) );
    history->Enable( ID_VCS_ADD, manager && !wxDirExists( wxFileName( prj.GetProjectPath(), wxS( ".git" ) ).GetFullPath() ) );
    history->Enable( ID_VCS_STORAGE, manager != nullptr );
    history->Enable( ID_VCS_LOCAL_HISTORY, manager != nullptr );
    KIUI::AddMenuItem( &menu, history, wxID_ANY, _( "History && &Version Control" ),
                       KiBitmapBundle( BITMAPS::recent ) );

    KIUI::AddMenuItem( &menu, ID_PACKAGER, _( "&Project Packager..." ), KiBitmapBundle( BITMAPS::zip ) );
    KIUI::AddMenuItem( &menu, ID_RELEASER, _( "Project &Releaser..." ), KiBitmapBundle( BITMAPS::file_gerber_job ) );
    menu.AppendSeparator();

    menu.Append( ID_SHARE, _( "&Share..." ) );
    KIUI::AddMenuItem( &menu, ID_OPTIONS, _( "Project &Options..." ),
                       KiBitmapBundle( BITMAPS::options_generic ) );

    bool hasProject = !prj.IsNullProject();

    menu.Enable( ID_VALIDATE, hasProject && ( canSch || inPcb ) );
    menu.Enable( ID_ADD_EXISTING, hasProject );
    menu.Enable( ID_SAVE, hasProject );
    menu.Enable( ID_RENAME, hasProject && manager );
    menu.Enable( ID_CLOSE_DOCUMENTS, hasProject && manager );
    menu.Enable( ID_CLOSE_PROJECT, hasProject && manager );
    menu.Enable( ID_VARIANTS, hasProject && ( canSch || inPcb ) );
    menu.Enable( ID_PACKAGER, hasProject && manager );
    menu.Enable( ID_RELEASER, false );
    menu.Enable( ID_SHARE, false );
    menu.Enable( ID_OPTIONS, hasProject && ( canSch || inPcb ) );

    switch( aParent->GetPopupMenuSelectionFromUser( menu ) )
    {
    case ID_VALIDATE:
        if( inPcb )
            runInEditor( aFrame, FRAME_PCB_EDITOR, "pcbnew.Project.validate" );
        else
            runInEditor( aFrame, FRAME_SCH, "eeschema.Project.validate" );

        break;

    case ID_NEW_SCHEMATIC:   addNewSchematic( aParent, prj );                                  break;
    case ID_NEW_PCB:         runInManager( aFrame, "kicad.Control.editPCB" );                  break;
    case ID_NEW_ACTIVEBOM:   runInEditor( aFrame, FRAME_SCH, "eeschema.EditorControl.generateBOM" ); break;
    case ID_NEW_SCH_LIB:     runInEditor( aFrame, FRAME_SCH_SYMBOL_EDITOR, "eeschema.SymbolLibraryControl.newProjectLibrary" ); break;
    case ID_NEW_PCB_LIB:     runInEditor( aFrame, FRAME_FOOTPRINT_EDITOR, "pcbnew.ModuleEditor.newProjectLibrary" ); break;
    case ID_NEW_CAM:         runInManager( aFrame, "kicad.Control.viewGerbers" );              break;
    case ID_NEW_OUTJOB:      runInManager( aFrame, "kicad.Control.newJobs" );                  break;
    case ID_ADD_EXISTING:    addExisting( aParent, aFrame );                                   break;
    case ID_SAVE:            Pgm().GetSettingsManager().SaveProject();                         break;

    // KiCad keeps a project's documents named after it: renaming is Save As
    case ID_RENAME:          runInManager( aFrame, "common.Control.saveAs" );                  break;

    case ID_CLOSE_DOCUMENTS:
    {
        // After the menu has closed: this may close the editor the menu belongs to
        KIWAY* kiway = &aFrame->Kiway();

        wxTheApp->CallAfter(
                [kiway]()
                {
                    for( FRAME_T type : { FRAME_SCH, FRAME_PCB_EDITOR, FRAME_SCH_SYMBOL_EDITOR,
                                          FRAME_FOOTPRINT_EDITOR } )
                    {
                        if( !kiway->PlayerClose( type, false ) )
                            break;   // the user cancelled saving
                    }
                } );
        break;
    }

    case ID_CLOSE_PROJECT:     runInManager( aFrame, "kicad.Control.closeProject" );          break;
    case ID_EXPLORE:           wxLaunchDefaultApplication( prj.GetProjectPath() );            break;

    case ID_VARIANTS:
        if( inPcb )
            runInEditor( aFrame, FRAME_PCB_EDITOR, "pcbnew.Outputs.variantManager" );
        else
            runInEditor( aFrame, FRAME_SCH, "eeschema.Outputs.variantManager" );

        break;

    case ID_VCS_ADD:           runInManager( aFrame, "kicad.Control.addToVersionControl" );   break;
    case ID_VCS_STORAGE:       runInManager( aFrame, "kicad.Control.showLocalHistory" );      break;
    case ID_VCS_LOCAL_HISTORY: runInManager( aFrame, "kicad.Control.restoreLocalHistory" );   break;
    case ID_PACKAGER:          runInManager( aFrame, "kicad.Control.archiveProject" );        break;

    case ID_OPTIONS:
        if( inPcb )
            runInEditor( aFrame, FRAME_PCB_EDITOR, "pcbnew.EditorControl.boardSetup" );
        else
            runInEditor( aFrame, FRAME_SCH, "eeschema.EditorControl.schematicSetup" );

        break;

    default:
        break;
    }
}
