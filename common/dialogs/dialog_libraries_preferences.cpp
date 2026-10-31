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

#include <dialogs/dialog_libraries_preferences.h>

#include <confirm.h>
#include <dialogs/dialog_configure_paths.h>
#include <eda_base_frame.h>
#include <env_vars.h>
#include <frame_type.h>
#include <kiway.h>
#include <kiway_mail.h>
#include <libraries/library_manager.h>
#include <mail_type.h>
#include <pgm_base.h>
#include <project.h>
#include <widgets/altium_sub_tabs.h>
#include <wildcards_and_files_ext.h>

#include <wx/button.h>
#include <wx/dataview.h>
#include <wx/dirdlg.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/ffile.h>
#include <wx/sizer.h>
#include <wx/simplebook.h>
#include <wx/stattext.h>


namespace
{
wxString typeName( LIBRARY_TABLE_TYPE aType )
{
    switch( aType )
    {
    case LIBRARY_TABLE_TYPE::SYMBOL:       return _( "Schematic" );
    case LIBRARY_TABLE_TYPE::FOOTPRINT:    return _( "Footprint" );
    case LIBRARY_TABLE_TYPE::DESIGN_BLOCK: return _( "Design Block" );
    default:                               return wxEmptyString;
    }
}


const std::vector<LIBRARY_TABLE_TYPE> TYPES = { LIBRARY_TABLE_TYPE::SYMBOL, LIBRARY_TABLE_TYPE::FOOTPRINT };
} // namespace


DIALOG_LIBRARIES_PREFERENCES::DIALOG_LIBRARIES_PREFERENCES( EDA_BASE_FRAME* aFrame, TAB aTab ) :
        DIALOG_SHIM( aFrame, wxID_ANY, _( "Libraries Preferences" ), wxDefaultPosition,
                     wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER ),
        m_frame( aFrame )
{
    wxBoxSizer* main = new wxBoxSizer( wxVERTICAL );

    m_tabs = new ALTIUM_SUB_TABS( this );
    m_book = new wxSimplebook( this );
    main->Add( m_tabs, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP( 10 ) );
    main->Add( m_book, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP( 10 ) );

    auto makeList =
            [&]( wxWindow* aParent, const wxString& aFirst, bool aActivated )
            {
                wxDataViewListCtrl* list = new wxDataViewListCtrl( aParent, wxID_ANY, wxDefaultPosition,
                                                                   wxDefaultSize, wxDV_ROW_LINES | wxDV_SINGLE );
                list->AppendTextColumn( aFirst, wxDATAVIEW_CELL_INERT, FromDIP( 220 ) );

                if( aActivated )
                    list->AppendToggleColumn( _( "Activated" ), wxDATAVIEW_CELL_ACTIVATABLE, FromDIP( 70 ) );

                list->AppendTextColumn( _( "Path" ), wxDATAVIEW_CELL_INERT, FromDIP( 430 ) );
                list->AppendTextColumn( _( "Type" ), wxDATAVIEW_CELL_INERT, FromDIP( 120 ) );
                return list;
            };

    auto button =
            [&]( wxWindow* aParent, wxSizer* aSizer, const wxString& aLabel, std::function<void()> aClick,
                 int aFlags = wxLEFT )
            {
                wxButton* btn = new wxButton( aParent, wxID_ANY, aLabel );
                btn->Bind( wxEVT_BUTTON, [aClick]( wxCommandEvent& ) { aClick(); } );
                aSizer->Add( btn, 0, aFlags, FromDIP( 6 ) );
                return btn;
            };

    // ---- Project
    wxPanel*    project = new wxPanel( m_book );
    wxBoxSizer* projectSizer = new wxBoxSizer( wxVERTICAL );
    m_projectList = makeList( project, _( "Project Libraries" ), false );
    projectSizer->Add( m_projectList, 1, wxEXPAND | wxTOP, FromDIP( 8 ) );

    wxBoxSizer* projectButtons = new wxBoxSizer( wxHORIZONTAL );
    button( project, projectButtons, _( "Move &Up" ),
            [this]() { moveSelected( LIBRARY_TABLE_SCOPE::PROJECT, -1 ); }, 0 );
    button( project, projectButtons, _( "Move &Down" ),
            [this]() { moveSelected( LIBRARY_TABLE_SCOPE::PROJECT, 1 ); } );
    projectButtons->AddStretchSpacer();
    button( project, projectButtons, _( "&Add Library..." ),
            [this]() { addLibraries( LIBRARY_TABLE_SCOPE::PROJECT ); } );
    button( project, projectButtons, _( "&Remove" ),
            [this]() { removeSelected( LIBRARY_TABLE_SCOPE::PROJECT ); } );
    projectSizer->Add( projectButtons, 0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP( 8 ) );
    project->SetSizer( projectSizer );

    // ---- Installed
    wxPanel*    installed = new wxPanel( m_book );
    wxBoxSizer* installedSizer = new wxBoxSizer( wxVERTICAL );
    m_installedList = makeList( installed, _( "Installed Libraries" ), true );
    installedSizer->Add( m_installedList, 1, wxEXPAND | wxTOP, FromDIP( 8 ) );

    wxBoxSizer* installedButtons = new wxBoxSizer( wxHORIZONTAL );
    button( installed, installedButtons, _( "Move &Up" ),
            [this]() { moveSelected( LIBRARY_TABLE_SCOPE::GLOBAL, -1 ); }, 0 );
    button( installed, installedButtons, _( "Move &Down" ),
            [this]() { moveSelected( LIBRARY_TABLE_SCOPE::GLOBAL, 1 ); } );
    installedButtons->AddStretchSpacer();
    button( installed, installedButtons, _( "&Install..." ),
            [this]() { addLibraries( LIBRARY_TABLE_SCOPE::GLOBAL ); } );
    button( installed, installedButtons, _( "&Remove" ),
            [this]() { removeSelected( LIBRARY_TABLE_SCOPE::GLOBAL ); } );
    installedSizer->Add( installedButtons, 0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP( 8 ) );
    installed->SetSizer( installedSizer );

    m_installedList->Bind( wxEVT_DATAVIEW_ITEM_VALUE_CHANGED,
                           &DIALOG_LIBRARIES_PREFERENCES::onActivatedToggled, this );

    // ---- Search Path
    wxPanel*    search = new wxPanel( m_book );
    wxBoxSizer* searchSizer = new wxBoxSizer( wxVERTICAL );
    m_searchList = new wxDataViewListCtrl( search, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                           wxDV_ROW_LINES | wxDV_SINGLE );
    m_searchList->AppendTextColumn( _( "Search Path Libraries" ), wxDATAVIEW_CELL_INERT, FromDIP( 260 ) );
    m_searchList->AppendTextColumn( _( "Path" ), wxDATAVIEW_CELL_INERT, FromDIP( 480 ) );
    m_searchList->AppendTextColumn( _( "Type" ), wxDATAVIEW_CELL_INERT, FromDIP( 100 ) );
    searchSizer->Add( m_searchList, 1, wxEXPAND | wxTOP, FromDIP( 8 ) );

    wxBoxSizer* searchButtons = new wxBoxSizer( wxHORIZONTAL );
    searchButtons->AddStretchSpacer();
    button( search, searchButtons, _( "&Paths..." ),
            [this]()
            {
                DIALOG_CONFIGURE_PATHS dlg( this );

                if( dlg.ShowModal() == wxID_OK )
                    fillSearchPaths();
            } );
    button( search, searchButtons, _( "Re&fresh" ), [this]() { fillSearchPaths(); } );
    searchSizer->Add( searchButtons, 0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP( 8 ) );
    search->SetSizer( searchSizer );

    m_book->AddPage( project, _( "Project" ) );
    m_book->AddPage( installed, _( "Installed" ) );
    m_book->AddPage( search, _( "Search Path" ) );
    m_book->SetSelection( aTab );
    m_tabs->AttachBook( m_book );

    // ---- Close
    wxBoxSizer* bottom = new wxBoxSizer( wxHORIZONTAL );
    bottom->AddStretchSpacer();
    wxButton* close = new wxButton( this, wxID_CANCEL, _( "&Close" ) );
    bottom->Add( close, 0 );
    main->Add( bottom, 0, wxEXPAND | wxALL, FromDIP( 10 ) );
    SetEscapeId( wxID_CANCEL );

    SetSizer( main );

    rebuild();

    finishDialogSettings();
    SetSize( FromDIP( wxSize( 930, 700 ) ) );
    Centre();
}


void DIALOG_LIBRARIES_PREFERENCES::Run( EDA_BASE_FRAME* aFrame, TAB aTab )
{
    DIALOG_LIBRARIES_PREFERENCES dlg( aFrame, aTab );
    dlg.ShowModal();
}


wxDataViewListCtrl* DIALOG_LIBRARIES_PREFERENCES::listFor( LIBRARY_TABLE_SCOPE aScope ) const
{
    return aScope == LIBRARY_TABLE_SCOPE::PROJECT ? m_projectList : m_installedList;
}


std::vector<DIALOG_LIBRARIES_PREFERENCES::ENTRY>&
DIALOG_LIBRARIES_PREFERENCES::entriesFor( LIBRARY_TABLE_SCOPE aScope )
{
    return aScope == LIBRARY_TABLE_SCOPE::PROJECT ? m_projectEntries : m_installedEntries;
}


int DIALOG_LIBRARIES_PREFERENCES::selectedEntry( LIBRARY_TABLE_SCOPE aScope ) const
{
    wxDataViewListCtrl* list = listFor( aScope );
    return list->GetSelectedRow();
}


void DIALOG_LIBRARIES_PREFERENCES::selectEntry( LIBRARY_TABLE_SCOPE aScope, LIBRARY_TABLE_TYPE aType,
                                                const wxString& aNickname )
{
    std::vector<ENTRY>& entries = entriesFor( aScope );

    for( int ii = 0; ii < (int) entries.size(); ++ii )
    {
        if( entries[ii].type == aType && entries[ii].nickname == aNickname )
        {
            listFor( aScope )->SelectRow( ii );
            listFor( aScope )->EnsureVisible( listFor( aScope )->RowToItem( ii ) );
            return;
        }
    }
}


LIBRARY_TABLE* DIALOG_LIBRARIES_PREFERENCES::table( LIBRARY_TABLE_TYPE aType, LIBRARY_TABLE_SCOPE aScope,
                                                    bool aCreate )
{
    LIBRARY_MANAGER&              manager = Pgm().GetLibraryManager();
    std::optional<LIBRARY_TABLE*> found = manager.Table( aType, aScope );

    if( found )
        return *found;

    if( !aCreate || aScope != LIBRARY_TABLE_SCOPE::PROJECT || m_frame->Prj().IsNullProject() )
        return nullptr;

    // A project without a table of this kind yet: start an empty one
    wxString   fileName = aType == LIBRARY_TABLE_TYPE::SYMBOL ? FILEEXT::SymbolLibraryTableFileName
                                                              : FILEEXT::FootprintLibraryTableFileName;
    wxString   tag = aType == LIBRARY_TABLE_TYPE::SYMBOL ? wxS( "sym_lib_table" ) : wxS( "fp_lib_table" );
    wxFileName fn( m_frame->Prj().GetProjectPath(), fileName );

    if( !fn.FileExists() )
    {
        wxFFile file( fn.GetFullPath(), wxS( "w" ) );

        if( !file.IsOpened() )
            return nullptr;

        file.Write( wxString::Format( wxS( "(%s\n  (version 7)\n)\n" ), tag ) );
        file.Close();
    }

    manager.LoadProjectTables( { aType } );
    found = manager.Table( aType, aScope );
    return found ? *found : nullptr;
}


void DIALOG_LIBRARIES_PREFERENCES::saved( LIBRARY_TABLE_TYPE aType, LIBRARY_TABLE_SCOPE aScope )
{
    LIBRARY_MANAGER& manager = Pgm().GetLibraryManager();

    if( LIBRARY_TABLE* tbl = table( aType, aScope, false ) )
    {
        tbl->Save().map_error(
                [&]( const LIBRARY_ERROR& aError )
                {
                    DisplayErrorMessage( this, _( "Error saving the library table." ), aError.message );
                } );
    }

    manager.AbortAsyncLoads();

    if( aScope == LIBRARY_TABLE_SCOPE::PROJECT )
        manager.LoadProjectTables( { aType } );
    else
        manager.LoadGlobalTables( { aType } );

    // Let the open editors and panels list the libraries again
    std::string packet;

    for( FRAME_T frame : { FRAME_SCH, FRAME_SCH_SYMBOL_EDITOR, FRAME_PCB_EDITOR, FRAME_FOOTPRINT_EDITOR } )
    {
        if( m_frame->Kiway().Player( frame, false ) )
            m_frame->Kiway().ExpressMail( frame, MAIL_RELOAD_LIB, packet );
    }

    rebuild();
}


void DIALOG_LIBRARIES_PREFERENCES::fillList( wxDataViewListCtrl* aList, LIBRARY_TABLE_SCOPE aScope,
                                             std::vector<ENTRY>& aEntries, bool aActivated )
{
    aList->DeleteAllItems();
    aEntries.clear();

    for( LIBRARY_TABLE_TYPE type : TYPES )
    {
        LIBRARY_TABLE* tbl = table( type, aScope, false );

        if( !tbl )
            continue;

        for( const LIBRARY_TABLE_ROW& row : tbl->Rows() )
        {
            if( row.Hidden() )
                continue;

            wxVector<wxVariant> values;
            values.push_back( wxVariant( row.Nickname() ) );

            if( aActivated )
                values.push_back( wxVariant( !row.Disabled() ) );

            values.push_back( wxVariant( LIBRARY_MANAGER::GetFullURI( &row, true ) ) );
            values.push_back( wxVariant( typeName( type ) ) );
            aList->AppendItem( values );

            aEntries.push_back( { type, aScope, row.Nickname() } );
        }
    }
}


void DIALOG_LIBRARIES_PREFERENCES::fillSearchPaths()
{
    m_searchList->DeleteAllItems();

    // The path variables library paths are written with (Altium's search paths)
    for( const auto& [name, item] : Pgm().GetLocalEnvVariables() )
    {
        if( !name.StartsWith( wxS( "KICAD" ) ) && name != wxS( "KIPRJMOD" ) )
            continue;

        wxVector<wxVariant> values;
        values.push_back( wxVariant( wxS( "${" ) + name + wxS( "}" ) ) );
        values.push_back( wxVariant( item.GetValue() ) );
        values.push_back( wxVariant( _( "Path" ) ) );
        m_searchList->AppendItem( values );
    }

    if( !m_frame->Prj().IsNullProject() )
    {
        wxVector<wxVariant> values;
        values.push_back( wxVariant( wxS( "${KIPRJMOD}" ) ) );
        values.push_back( wxVariant( m_frame->Prj().GetProjectPath() ) );
        values.push_back( wxVariant( _( "Project" ) ) );
        m_searchList->AppendItem( values );
    }
}


void DIALOG_LIBRARIES_PREFERENCES::rebuild()
{
    m_filling = true;
    fillList( m_projectList, LIBRARY_TABLE_SCOPE::PROJECT, m_projectEntries, false );
    fillList( m_installedList, LIBRARY_TABLE_SCOPE::GLOBAL, m_installedEntries, true );
    fillSearchPaths();
    m_filling = false;
}


void DIALOG_LIBRARIES_PREFERENCES::addLibraries( LIBRARY_TABLE_SCOPE aScope )
{
    if( aScope == LIBRARY_TABLE_SCOPE::PROJECT && m_frame->Prj().IsNullProject() )
    {
        DisplayError( this, _( "Open a project to add project libraries." ) );
        return;
    }

    // Symbol libraries are files; a footprint library is a folder, chosen through one of its
    // footprints (or added with the folder button)
    wxString wildcard = _( "Schematic and footprint libraries" )
                        + wxS( " (*.kicad_sym;*.kicad_mod)|*.kicad_sym;*.kicad_mod|" )
                        + _( "Schematic libraries" ) + wxS( " (*.kicad_sym)|*.kicad_sym|" )
                        + _( "Footprints (adds their library folder)" ) + wxS( " (*.kicad_mod)|*.kicad_mod" );

    wxString start = aScope == LIBRARY_TABLE_SCOPE::PROJECT ? m_frame->Prj().GetProjectPath() : wxString();
    wxFileDialog dlg( this, aScope == LIBRARY_TABLE_SCOPE::PROJECT ? _( "Add Library" ) : _( "Install Library" ),
                      start, wxEmptyString, wildcard, wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_MULTIPLE );

    if( dlg.ShowModal() != wxID_OK )
        return;

    wxArrayString paths;
    dlg.GetPaths( paths );

    std::set<LIBRARY_TABLE_TYPE> changed;
    LIBRARY_TABLE_TYPE           lastType = LIBRARY_TABLE_TYPE::UNINITIALIZED;
    wxString                     lastName;

    for( const wxString& path : paths )
    {
        wxFileName         fn( path );
        LIBRARY_TABLE_TYPE type = LIBRARY_TABLE_TYPE::SYMBOL;
        wxString           libPath = fn.GetFullPath();

        if( fn.GetExt().Lower() == wxString( FILEEXT::KiCadFootprintFileExtension ) )
        {
            // The .pretty folder holding the footprint
            type = LIBRARY_TABLE_TYPE::FOOTPRINT;
            libPath = fn.GetPath();
            fn = wxFileName( libPath );
        }

        LIBRARY_TABLE* tbl = table( type, aScope, true );

        if( !tbl )
            continue;

        // A project-relative path keeps the project table portable
        wxString uri = libPath;

        if( aScope == LIBRARY_TABLE_SCOPE::PROJECT )
        {
            wxFileName rel( libPath );

            if( rel.MakeRelativeTo( m_frame->Prj().GetProjectPath() )
                && !rel.GetFullPath().StartsWith( wxS( ".." ) ) )
            {
                uri = wxS( "${KIPRJMOD}/" ) + rel.GetFullPath( wxPATH_UNIX );
            }
        }

        wxString nickname = fn.GetName();

        for( int ii = 1; tbl->HasRow( nickname ); ++ii )
            nickname = wxString::Format( wxS( "%s_%d" ), fn.GetName(), ii );

        LIBRARY_TABLE_ROW& row = tbl->InsertRow();
        row.SetNickname( nickname );
        row.SetURI( uri );
        row.SetType( wxS( "KiCad" ) );
        changed.insert( type );
        lastType = type;
        lastName = nickname;
    }

    for( LIBRARY_TABLE_TYPE type : changed )
        saved( type, aScope );

    if( !lastName.IsEmpty() )
        selectEntry( aScope, lastType, lastName );
}


void DIALOG_LIBRARIES_PREFERENCES::removeSelected( LIBRARY_TABLE_SCOPE aScope )
{
    int idx = selectedEntry( aScope );

    if( idx < 0 || idx >= (int) entriesFor( aScope ).size() )
        return;

    ENTRY          entry = entriesFor( aScope )[idx];
    LIBRARY_TABLE* tbl = table( entry.type, aScope, false );

    if( !tbl )
        return;

    if( !IsOK( this, wxString::Format( _( "Remove '%s' from the libraries list?  The library files "
                                          "are not deleted." ),
                                       entry.nickname ) ) )
    {
        return;
    }

    std::deque<LIBRARY_TABLE_ROW>& rows = tbl->Rows();

    std::erase_if( rows,
                   [&]( const LIBRARY_TABLE_ROW& aRow )
                   {
                       return aRow.Nickname() == entry.nickname;
                   } );

    saved( entry.type, aScope );

    int count = (int) entriesFor( aScope ).size();

    if( count > 0 )
        listFor( aScope )->SelectRow( std::min( idx, count - 1 ) );
}


void DIALOG_LIBRARIES_PREFERENCES::moveSelected( LIBRARY_TABLE_SCOPE aScope, int aDelta )
{
    int idx = selectedEntry( aScope );

    if( idx < 0 || idx >= (int) entriesFor( aScope ).size() )
        return;

    ENTRY          entry = entriesFor( aScope )[idx];
    LIBRARY_TABLE* tbl = table( entry.type, aScope, false );

    if( !tbl )
        return;

    // Moves within its own table (symbol and footprint libraries keep their own order)
    std::deque<LIBRARY_TABLE_ROW>& rows = tbl->Rows();

    auto it = std::find_if( rows.begin(), rows.end(),
                            [&]( const LIBRARY_TABLE_ROW& aRow )
                            {
                                return aRow.Nickname() == entry.nickname;
                            } );

    if( it == rows.end() )
        return;

    int pos = (int) ( it - rows.begin() );
    int target = pos + aDelta;

    // Skip hidden rows, which the list doesn't show
    while( target >= 0 && target < (int) rows.size() && rows[target].Hidden() )
        target += aDelta;

    if( target < 0 || target >= (int) rows.size() )
        return;

    std::swap( rows[pos], rows[target] );
    saved( entry.type, aScope );
    selectEntry( aScope, entry.type, entry.nickname );
}


void DIALOG_LIBRARIES_PREFERENCES::onActivatedToggled( wxDataViewEvent& aEvent )
{
    if( m_filling )
        return;

    int row = m_installedList->ItemToRow( aEvent.GetItem() );

    if( row < 0 || row >= (int) m_installedEntries.size() || aEvent.GetColumn() != 1 )
        return;

    ENTRY          entry = m_installedEntries[row];
    LIBRARY_TABLE* tbl = table( entry.type, LIBRARY_TABLE_SCOPE::GLOBAL, false );

    if( !tbl )
        return;

    bool active = m_installedList->GetToggleValue( row, 1 );

    if( std::optional<LIBRARY_TABLE_ROW*> tableRow = tbl->Row( entry.nickname ) )
        ( *tableRow )->SetDisabled( !active );

    // Rebuilding the list inside its own event is unsafe: do it afterwards
    CallAfter( [this, entry]()
               {
                   saved( entry.type, LIBRARY_TABLE_SCOPE::GLOBAL );
                   selectEntry( LIBRARY_TABLE_SCOPE::GLOBAL, entry.type, entry.nickname );
               } );
}
