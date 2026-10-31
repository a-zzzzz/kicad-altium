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
#include <widgets/sch_model_pane.h>
#include <widgets/native_list_theme.h>

#include <lib_symbol.h>
#include <pgm_base.h>
#include <project.h>
#include <sch_commit.h>
#include <sch_field.h>
#include <sch_pin.h>
#include <symbol_edit_frame.h>
#include <tool/actions.h>
#include <tool/tool_manager.h>
#include <libraries/library_manager.h>
#include <libraries/library_table.h>
#include <widgets/footprint_preview_widget.h>

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/dir.h>
#include <wx/dirdlg.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/listctrl.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/radiobut.h>
#include <wx/regex.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <algorithm>
#include <set>


namespace
{
// ---------------------------------------------------------------------------------------------
// Footprint libraries, read straight from the .pretty folders (the footprint plugins live in
// pcbnew; names, descriptions and pad numbers are all the pane needs)

struct FP_LIB
{
    wxString nickname;
    wxString path;       ///< expanded folder path
};


std::vector<FP_LIB> footprintLibraries( PROJECT& aProject )
{
    std::vector<FP_LIB> libs;

    for( LIBRARY_TABLE_ROW* row : Pgm().GetLibraryManager().Rows( LIBRARY_TABLE_TYPE::FOOTPRINT ) )
    {
        if( !row || row->Disabled() || row->Hidden() )
            continue;

        wxString path = LIBRARY_MANAGER::ExpandURI( row->URI(), aProject );

        if( wxDirExists( path ) )
            libs.push_back( { row->Nickname(), path } );
    }

    std::sort( libs.begin(), libs.end(),
               []( const FP_LIB& a, const FP_LIB& b )
               {
                   return a.nickname.CmpNoCase( b.nickname ) < 0;
               } );

    return libs;
}


wxString footprintFile( const wxString& aLibPath, const wxString& aName )
{
    return wxFileName( aLibPath, aName, wxS( "kicad_mod" ) ).GetFullPath();
}


std::vector<wxString> footprintNames( const wxString& aLibPath )
{
    std::vector<wxString> names;
    wxDir                 dir( aLibPath );
    wxString              file;

    if( dir.IsOpened() && dir.GetFirst( &file, wxS( "*.kicad_mod" ), wxDIR_FILES ) )
    {
        do
            names.push_back( wxFileName( file ).GetName() );
        while( dir.GetNext( &file ) );
    }

    std::sort( names.begin(), names.end(),
               []( const wxString& a, const wxString& b ) { return a.CmpNoCase( b ) < 0; } );
    return names;
}


wxString readFile( const wxString& aPath, size_t aMax = 0 )
{
    wxFFile  file( aPath, wxS( "rb" ) );
    wxString text;

    if( !file.IsOpened() )
        return text;

    if( aMax == 0 )
    {
        file.ReadAll( &text, wxConvUTF8 );
        return text;
    }

    std::string buf( aMax, '\0' );
    size_t      got = file.Read( buf.data(), aMax );
    buf.resize( got );
    return wxString::FromUTF8( buf );
}


wxString footprintDescription( const wxString& aFile )
{
    wxString text = readFile( aFile, 4096 );
    int      start = text.Find( wxS( "(descr \"" ) );

    if( start == wxNOT_FOUND )
        return wxEmptyString;

    wxString descr;
    bool     escaped = false;

    for( size_t ii = start + 8; ii < text.length(); ++ii )
    {
        wxUniChar ch = text[ii];

        if( escaped )
        {
            descr << ( ch == 'n' ? wxUniChar( ' ' ) : ch );
            escaped = false;
        }
        else if( ch == '\\' )
        {
            escaped = true;
        }
        else if( ch == '"' )
        {
            break;
        }
        else
        {
            descr << ch;
        }
    }

    return descr;
}


std::set<wxString> footprintPads( const wxString& aFile )
{
    std::set<wxString> pads;
    wxString           text = readFile( aFile );
    int                pos = 0;

    while( ( pos = text.find( wxS( "(pad \"" ), pos ) ) != wxNOT_FOUND )
    {
        pos += 6;
        int end = text.find( '"', pos );

        if( end == wxNOT_FOUND )
            break;

        wxString pad = text.Mid( pos, end - pos );

        if( !pad.IsEmpty() )
            pads.insert( pad );

        pos = end;
    }

    return pads;
}


/// The folder holding a footprint (in its own library, or any when it has none), or empty.
wxString findFootprint( PROJECT& aProject, const LIB_ID& aId, wxString* aNickname = nullptr )
{
    if( aId.GetLibItemName().empty() )
        return wxEmptyString;

    for( const FP_LIB& lib : footprintLibraries( aProject ) )
    {
        if( !aId.GetLibNickname().empty() && lib.nickname != aId.GetUniStringLibNickname() )
            continue;

        if( wxFileExists( footprintFile( lib.path, aId.GetUniStringLibItemName() ) ) )
        {
            if( aNickname )
                *aNickname = lib.nickname;

            return lib.path;
        }
    }

    return wxEmptyString;
}


wxStdDialogButtonSizer* okCancel( wxWindow* aParent, bool aCancel = true )
{
    wxStdDialogButtonSizer* buttons = new wxStdDialogButtonSizer();
    buttons->AddButton( new wxButton( aParent, wxID_OK ) );

    if( aCancel )
        buttons->AddButton( new wxButton( aParent, wxID_CANCEL ) );

    buttons->Realize();
    return buttons;
}


// ---------------------------------------------------------------------------------------------
/// Altium's "Browse Libraries": a footprint library, its footprints (with a mask) and a preview.
class DIALOG_BROWSE_FOOTPRINTS : public wxDialog
{
public:
    DIALOG_BROWSE_FOOTPRINTS( SCH_BASE_FRAME* aFrame, const LIB_ID& aStart ) :
            wxDialog( aFrame, wxID_ANY, _( "Browse Libraries" ), wxDefaultPosition,
                      wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER ),
            m_frame( aFrame )
    {
        wxBoxSizer* main = new wxBoxSizer( wxVERTICAL );

        wxBoxSizer* libRow = new wxBoxSizer( wxHORIZONTAL );
        libRow->Add( new wxStaticText( this, wxID_ANY, _( "&Libraries" ) ), 0,
                     wxALIGN_CENTER_VERTICAL | wxRIGHT, 8 );
        m_libChoice = new wxChoice( this, wxID_ANY );
        libRow->Add( m_libChoice, 1, wxALIGN_CENTER_VERTICAL );

        wxButton* libsBtn = new wxButton( this, wxID_ANY, wxS( "..." ), wxDefaultPosition,
                                          wxDefaultSize, wxBU_EXACTFIT );
        libsBtn->SetToolTip( _( "Manage footprint libraries" ) );
        libRow->Add( libsBtn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4 );
        main->Add( libRow, 0, wxEXPAND | wxALL, 8 );

        wxBoxSizer* body = new wxBoxSizer( wxHORIZONTAL );
        wxBoxSizer* left = new wxBoxSizer( wxVERTICAL );

        wxBoxSizer* maskRow = new wxBoxSizer( wxHORIZONTAL );
        maskRow->Add( new wxStaticText( this, wxID_ANY, _( "&Mask" ) ), 0,
                      wxALIGN_CENTER_VERTICAL | wxRIGHT, 8 );
        m_mask = new wxTextCtrl( this, wxID_ANY );
        maskRow->Add( m_mask, 1, wxALIGN_CENTER_VERTICAL );
        left->Add( maskRow, 0, wxEXPAND | wxBOTTOM, 6 );

        m_list = new wxListCtrl( this, wxID_ANY, wxDefaultPosition, FromDIP( wxSize( 440, 320 ) ),
                                 wxLC_REPORT | wxLC_SINGLE_SEL );
        m_list->AppendColumn( _( "Name" ), wxLIST_FORMAT_LEFT, FromDIP( 190 ) );
        m_list->AppendColumn( _( "Library" ), wxLIST_FORMAT_LEFT, FromDIP( 110 ) );
        m_list->AppendColumn( _( "Description" ), wxLIST_FORMAT_LEFT, FromDIP( 220 ) );
        left->Add( m_list, 1, wxEXPAND );

        m_count = new wxStaticText( this, wxID_ANY, wxEmptyString );
        left->Add( m_count, 0, wxTOP, 4 );
        body->Add( left, 1, wxEXPAND | wxRIGHT, 8 );

        m_preview = new FOOTPRINT_PREVIEW_WIDGET( this, aFrame->Kiway() );
        m_preview->SetMinSize( FromDIP( wxSize( 360, 320 ) ) );

        if( m_preview->IsInitialized() )
            m_preview->SetStatusText( _( "No Preview Available" ) );

        body->Add( m_preview, 1, wxEXPAND );
        main->Add( body, 1, wxEXPAND | wxLEFT | wxRIGHT, 8 );
        main->Add( okCancel( this ), 0, wxEXPAND | wxALL, 8 );

        SetSizerAndFit( main );
        Centre();

        fillLibraries( aStart.GetUniStringLibNickname() );

        m_libChoice->Bind( wxEVT_CHOICE, [this]( wxCommandEvent& ) { fill(); } );
        m_mask->Bind( wxEVT_TEXT, [this]( wxCommandEvent& ) { fill(); } );
        m_list->Bind( wxEVT_LIST_ITEM_SELECTED, [this]( wxListEvent& ) { onPick(); } );
        m_list->Bind( wxEVT_LIST_ITEM_ACTIVATED, [this]( wxListEvent& ) { EndModal( wxID_OK ); } );

        // "...": the library table (Altium's Libraries Preferences)
        libsBtn->Bind( wxEVT_BUTTON,
                       [this]( wxCommandEvent& )
                       {
                           DIALOG_LIBRARIES_PREFERENCES::Run( m_frame );
                           fillLibraries( m_libChoice->GetStringSelection() );
                           fill();
                       } );

        fill( aStart.GetUniStringLibItemName() );
    }

    LIB_ID GetPicked() const { return m_picked; }

private:
    void fillLibraries( const wxString& aSelect )
    {
        m_libs = footprintLibraries( m_frame->Prj() );
        m_libChoice->Clear();

        // As in Altium, the project's own library comes first when nothing else is asked for
        int      pick = 0;
        wxString projectPath = m_frame->Prj().GetProjectPath();
        bool     projectLib = false;

        for( size_t ii = 0; ii < m_libs.size(); ++ii )
        {
            m_libChoice->Append( m_libs[ii].nickname );

            if( m_libs[ii].nickname == aSelect )
            {
                pick = (int) ii;
                projectLib = true;     // an explicit pick wins
            }
            else if( aSelect.IsEmpty() && !projectLib && !projectPath.IsEmpty()
                     && m_libs[ii].path.StartsWith( projectPath ) )
            {
                pick = (int) ii;
                projectLib = true;
            }
        }

        if( !m_libs.empty() )
            m_libChoice->SetSelection( pick );
    }

    void fill( const wxString& aSelect = wxEmptyString )
    {
        m_list->DeleteAllItems();
        m_names.clear();

        int lib = m_libChoice->GetSelection();

        if( lib < 0 || lib >= (int) m_libs.size() )
            return;

        wxString mask = m_mask->GetValue().Lower();

        if( !mask.IsEmpty() && !mask.Contains( wxS( "*" ) ) && !mask.Contains( wxS( "?" ) ) )
            mask = wxS( "*" ) + mask + wxS( "*" );

        for( const wxString& name : footprintNames( m_libs[lib].path ) )
        {
            if( mask.IsEmpty() || name.Lower().Matches( mask ) )
                m_names.push_back( name );
        }

        for( size_t ii = 0; ii < m_names.size(); ++ii )
        {
            long row = m_list->InsertItem( (long) ii, m_names[ii] );
            m_list->SetItem( row, 1, m_libs[lib].nickname );

            if( !aSelect.IsEmpty() && m_names[ii] == aSelect )
            {
                m_list->SetItemState( row, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                                      wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED );
                m_list->EnsureVisible( row );
            }
        }

        m_count->SetLabel( wxString::Format( _( "%d items" ), (int) m_names.size() ) );
    }

    void onPick()
    {
        long sel = m_list->GetNextItem( -1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED );
        int  lib = m_libChoice->GetSelection();

        if( sel < 0 || sel >= (long) m_names.size() || lib < 0 )
            return;

        m_picked = LIB_ID( m_libs[lib].nickname, m_names[sel] );

        // Descriptions are read when a row is picked: big libraries have thousands of files
        if( m_list->GetItemText( sel, 2 ).IsEmpty() )
        {
            m_list->SetItem( sel, 2, footprintDescription( footprintFile( m_libs[lib].path,
                                                                          m_names[sel] ) ) );
        }

        if( m_preview->IsInitialized() )
        {
            m_preview->ClearStatus();
            m_preview->DisplayFootprint( m_picked );
        }
    }

    SCH_BASE_FRAME*           m_frame;
    wxChoice*                 m_libChoice;
    wxTextCtrl*               m_mask;
    wxListCtrl*               m_list;
    wxStaticText*             m_count;
    FOOTPRINT_PREVIEW_WIDGET* m_preview;
    std::vector<FP_LIB>       m_libs;
    std::vector<wxString>     m_names;
    LIB_ID                    m_picked;
};


}  // namespace


LIB_ID BrowseFootprintLibraries( SCH_BASE_FRAME* aFrame, const LIB_ID& aStart )
{
    DIALOG_BROWSE_FOOTPRINTS dlg( aFrame, aStart );

    if( dlg.ShowModal() == wxID_OK )
        return dlg.GetPicked();

    return LIB_ID();
}


namespace
{
// ---------------------------------------------------------------------------------------------
/// Altium's Pin Map: the symbol's pins against the footprint's pads.
void showPinMap( wxWindow* aParent, LIB_SYMBOL* aSymbol, const wxString& aFootprintFile )
{
    wxDialog    dlg( aParent, wxID_ANY, _( "Model Map" ), wxDefaultPosition, wxDefaultSize,
                     wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER );
    wxBoxSizer* main = new wxBoxSizer( wxVERTICAL );
    wxListCtrl* list = new wxListCtrl( &dlg, wxID_ANY, wxDefaultPosition,
                                       dlg.FromDIP( wxSize( 440, 300 ) ), wxLC_REPORT );

    list->AppendColumn( _( "Pin Designator" ), wxLIST_FORMAT_LEFT, dlg.FromDIP( 120 ) );
    list->AppendColumn( _( "Pin Name" ), wxLIST_FORMAT_LEFT, dlg.FromDIP( 130 ) );
    list->AppendColumn( _( "Model Pin Designator" ), wxLIST_FORMAT_LEFT, dlg.FromDIP( 160 ) );

    std::set<wxString> pads = footprintPads( aFootprintFile );
    std::set<wxString> seen;

    if( aSymbol )
    {
        for( SCH_PIN* pin : aSymbol->GetPins() )
        {
            if( !seen.insert( pin->GetNumber() ).second )
                continue;

            long row = list->InsertItem( list->GetItemCount(), pin->GetNumber() );
            list->SetItem( row, 1, pin->GetShownName() );
            list->SetItem( row, 2, pads.count( pin->GetNumber() ) ? pin->GetNumber()
                                                                  : _( "(no pad)" ) );
        }
    }

    for( const wxString& pad : pads )
    {
        if( !seen.count( pad ) )
        {
            long row = list->InsertItem( list->GetItemCount(), _( "(no pin)" ) );
            list->SetItem( row, 2, pad );
        }
    }

    main->Add( new wxStaticText( &dlg, wxID_ANY,
                                 _( "Pins map to the pads with the same designator." ) ),
               0, wxALL, 8 );
    main->Add( list, 1, wxEXPAND | wxLEFT | wxRIGHT, 8 );
    main->Add( okCancel( &dlg, false ), 0, wxEXPAND | wxALL, 8 );

    dlg.SetSizerAndFit( main );
    dlg.Centre();
    ApplyNativeListTheme( &dlg );   // plain wxDialog: no DIALOG_SHIM theming
    dlg.ShowModal();
}


// ---------------------------------------------------------------------------------------------
/// Altium's "PCB Model" dialog: the footprint's name and where to find it.
class DIALOG_PCB_MODEL : public wxDialog
{
public:
    DIALOG_PCB_MODEL( SYMBOL_EDIT_FRAME* aFrame, const wxString& aFootprint ) :
            wxDialog( aFrame, wxID_ANY, _( "PCB Model" ), wxDefaultPosition, wxDefaultSize,
                      wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER ),
            m_frame( aFrame )
    {
        wxBoxSizer* main = new wxBoxSizer( wxVERTICAL );

        auto section =
                [&]( const wxString& aTitle )
                {
                    wxStaticText* title = new wxStaticText( this, wxID_ANY, aTitle );
                    wxFont        font = title->GetFont();
                    font.MakeBold();
                    title->SetFont( font );
                    main->Add( title, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10 );
                };

        section( _( "Footprint Model" ) );

        wxFlexGridSizer* model = new wxFlexGridSizer( 4, 4, 8 );
        model->AddGrowableCol( 1 );
        model->Add( new wxStaticText( this, wxID_ANY, _( "Name" ) ), 0, wxALIGN_CENTER_VERTICAL );
        m_name = new wxTextCtrl( this, wxID_ANY );
        model->Add( m_name, 1, wxEXPAND );
        wxButton* browse = new wxButton( this, wxID_ANY, _( "&Browse..." ) );
        model->Add( browse );
        wxButton* pinMap = new wxButton( this, wxID_ANY, _( "&Pin Map..." ) );
        model->Add( pinMap );
        model->Add( new wxStaticText( this, wxID_ANY, _( "Description" ) ), 0,
                    wxALIGN_CENTER_VERTICAL );
        m_description = new wxTextCtrl( this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                        wxDefaultSize, wxTE_READONLY );
        model->Add( m_description, 1, wxEXPAND );
        model->AddSpacer( 0 );
        model->AddSpacer( 0 );
        main->Add( model, 0, wxEXPAND | wxALL, 10 );

        section( _( "PCB Library" ) );

        wxFlexGridSizer* where = new wxFlexGridSizer( 3, 4, 8 );
        where->AddGrowableCol( 1 );
        m_any = new wxRadioButton( this, wxID_ANY, _( "Any" ), wxDefaultPosition, wxDefaultSize,
                                   wxRB_GROUP );
        where->Add( m_any );
        where->AddSpacer( 0 );
        where->AddSpacer( 0 );
        m_byName = new wxRadioButton( this, wxID_ANY, _( "Library name" ) );
        where->Add( m_byName, 0, wxALIGN_CENTER_VERTICAL );
        m_libName = new wxTextCtrl( this, wxID_ANY );
        where->Add( m_libName, 1, wxEXPAND );
        where->AddSpacer( 0 );
        m_byPath = new wxRadioButton( this, wxID_ANY, _( "Library path" ) );
        where->Add( m_byPath, 0, wxALIGN_CENTER_VERTICAL );
        m_libPath = new wxTextCtrl( this, wxID_ANY );
        where->Add( m_libPath, 1, wxEXPAND );
        wxButton* choose = new wxButton( this, wxID_ANY, _( "Choose..." ) );
        where->Add( choose );
        main->Add( where, 0, wxEXPAND | wxALL, 10 );

        // Outside the grid, so its long label doesn't widen the radio column
        wxRadioButton* fromLib = new wxRadioButton( this, wxID_ANY,
                                                    _( "Use footprint from component library *" ) );
        fromLib->Enable( false );
        main->Add( fromLib, 0, wxLEFT | wxRIGHT, 10 );

        section( _( "Selected Footprint" ) );

        m_preview = new FOOTPRINT_PREVIEW_WIDGET( this, aFrame->Kiway() );
        m_preview->SetMinSize( FromDIP( wxSize( 480, 280 ) ) );
        main->Add( m_preview, 1, wxEXPAND | wxALL, 10 );

        m_foundIn = new wxStaticText( this, wxID_ANY, wxEmptyString );
        main->Add( m_foundIn, 0, wxEXPAND | wxLEFT | wxRIGHT, 10 );
        main->Add( okCancel( this ), 0, wxEXPAND | wxALL, 10 );

        SetSizerAndFit( main );
        Centre();

        LIB_ID id;

        if( !aFootprint.IsEmpty() )
            id.Parse( aFootprint );

        m_name->ChangeValue( aFootprint.IsEmpty() ? _( "Model Name" )
                                                  : id.GetUniStringLibItemName() );

        if( !id.GetLibNickname().empty() )
        {
            m_byName->SetValue( true );
            m_libName->ChangeValue( id.GetUniStringLibNickname() );
        }
        else
        {
            m_any->SetValue( true );
        }

        m_name->SelectAll();
        m_name->SetFocus();

        m_name->Bind( wxEVT_TEXT, [this]( wxCommandEvent& ) { resolve(); } );
        m_libName->Bind( wxEVT_TEXT,
                         [this]( wxCommandEvent& )
                         {
                             m_byName->SetValue( true );
                             resolve();
                         } );
        m_libPath->Bind( wxEVT_TEXT,
                         [this]( wxCommandEvent& )
                         {
                             m_byPath->SetValue( true );
                             resolve();
                         } );

        for( wxRadioButton* radio : { m_any, m_byName, m_byPath } )
            radio->Bind( wxEVT_RADIOBUTTON, [this]( wxCommandEvent& ) { resolve(); } );

        browse->Bind( wxEVT_BUTTON,
                      [this]( wxCommandEvent& )
                      {
                          DIALOG_BROWSE_FOOTPRINTS dlg( m_frame, m_found );

                          ApplyNativeListTheme( &dlg );   // plain wxDialog: no DIALOG_SHIM theming
                          if( dlg.ShowModal() == wxID_OK && dlg.GetPicked().IsValid() )
                          {
                              m_name->ChangeValue( dlg.GetPicked().GetUniStringLibItemName() );

                              // KiCad needs the library: keep the one browsed
                              m_byName->SetValue( true );
                              m_libName->ChangeValue( dlg.GetPicked().GetUniStringLibNickname() );
                              resolve();
                          }
                      } );

        choose->Bind( wxEVT_BUTTON,
                      [this]( wxCommandEvent& )
                      {
                          wxDirDialog dlg( this, _( "Choose Footprint Library Folder" ),
                                           m_libPath->GetValue() );

                          if( dlg.ShowModal() == wxID_OK )
                              m_libPath->SetValue( dlg.GetPath() );
                      } );

        pinMap->Bind( wxEVT_BUTTON,
                      [this]( wxCommandEvent& )
                      {
                          showPinMap( this, m_frame->GetCurSymbol(),
                                      footprintFile( m_foundPath,
                                                     m_found.GetUniStringLibItemName() ) );
                      } );

        resolve();
    }

    /// The footprint as the symbol stores it ("library:name").
    wxString GetFootprint() const
    {
        return m_found.IsValid() ? wxString( m_found.Format().wx_str() ) : wxString();
    }

    bool TransferDataFromWindow() override
    {
        if( !m_found.IsValid() )
        {
            wxMessageBox( _( "Footprint not found." ), _( "PCB Model" ), wxOK | wxICON_ERROR,
                          this );
            return false;
        }

        return true;
    }

private:
    void resolve()
    {
        wxString name = m_name->GetValue();
        name.Trim().Trim( false );

        m_found = LIB_ID();
        m_foundPath.clear();

        if( !name.IsEmpty() )
        {
            if( m_byPath->GetValue() )
            {
                wxFileName path( m_libPath->GetValue(), wxEmptyString );

                for( const FP_LIB& lib : footprintLibraries( m_frame->Prj() ) )
                {
                    if( wxFileName( lib.path, wxEmptyString ).SameAs( path )
                        && wxFileExists( footprintFile( lib.path, name ) ) )
                    {
                        m_found = LIB_ID( lib.nickname, name );
                        m_foundPath = lib.path;
                    }
                }
            }
            else
            {
                LIB_ID   id( m_byName->GetValue() ? m_libName->GetValue() : wxString(), name );
                wxString nickname;
                wxString path = findFootprint( m_frame->Prj(), id, &nickname );

                if( !path.IsEmpty() )
                {
                    m_found = LIB_ID( nickname, name );
                    m_foundPath = path;
                }
            }
        }

        if( m_found.IsValid() )
        {
            wxString file = footprintFile( m_foundPath, name );

            m_description->ChangeValue( footprintDescription( file ) );
            m_foundIn->SetLabel( _( "Found in:" ) + wxS( " " ) + file );

            if( m_preview->IsInitialized() )
            {
                m_preview->ClearStatus();
                m_preview->DisplayFootprint( m_found );
            }
        }
        else
        {
            m_description->ChangeValue( _( "Footprint not found" ) );
            m_foundIn->SetLabel( _( "Found in:" ) );

            if( m_preview->IsInitialized() )
                m_preview->SetStatusText( _( "No Preview Available" ) );
        }
    }

    SYMBOL_EDIT_FRAME*        m_frame;
    wxTextCtrl*               m_name;
    wxTextCtrl*               m_description;
    wxRadioButton*            m_any;
    wxRadioButton*            m_byName;
    wxRadioButton*            m_byPath;
    wxTextCtrl*               m_libName;
    wxTextCtrl*               m_libPath;
    FOOTPRINT_PREVIEW_WIDGET* m_preview;
    wxStaticText*             m_foundIn;
    LIB_ID                    m_found;
    wxString                  m_foundPath;
};


enum
{
    ID_ADD_FOOTPRINT = wxID_HIGHEST + 1,
    ID_ADD_MENU,
    ID_REMOVE,
    ID_EDIT
};
} // namespace


SCH_MODEL_PANE::SCH_MODEL_PANE( wxWindow* aParent, SYMBOL_EDIT_FRAME* aFrame ) :
        wxPanel( aParent ),
        m_frame( aFrame ),
        m_timer( this )
{
    wxBoxSizer* main = new wxBoxSizer( wxHORIZONTAL );
    wxBoxSizer* left = new wxBoxSizer( wxVERTICAL );

    m_list = new wxListCtrl( this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                             wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_NONE );
    m_list->AppendColumn( _( "Model" ), wxLIST_FORMAT_LEFT, FromDIP( 160 ) );
    m_list->AppendColumn( _( "Type" ), wxLIST_FORMAT_LEFT, FromDIP( 110 ) );
    m_list->AppendColumn( _( "Location" ), wxLIST_FORMAT_LEFT, FromDIP( 160 ) );
    m_list->AppendColumn( _( "Description" ), wxLIST_FORMAT_LEFT, FromDIP( 420 ) );
    left->Add( m_list, 1, wxEXPAND );

    // Add Footprint [v] | Remove | Edit...
    wxBoxSizer* buttons = new wxBoxSizer( wxHORIZONTAL );
    m_addBtn = new wxButton( this, ID_ADD_FOOTPRINT, _( "Add Footprint" ) );
    m_addMenuBtn = new wxButton( this, ID_ADD_MENU, wxString( wxUniChar( 0x25BE ) ),
                                 wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT );
    m_removeBtn = new wxButton( this, ID_REMOVE, _( "Remove" ) );
    m_editBtn = new wxButton( this, ID_EDIT, _( "Edit..." ) );
    buttons->Add( m_addBtn, 0 );
    buttons->Add( m_addMenuBtn, 0, wxRIGHT, 8 );
    buttons->Add( m_removeBtn, 0, wxRIGHT, 8 );
    buttons->Add( m_editBtn, 0 );
    left->Add( buttons, 0, wxALL, 4 );

    main->Add( left, 3, wxEXPAND );

    m_previewHolder = new wxPanel( this );
    wxBoxSizer* holder = new wxBoxSizer( wxVERTICAL );
    m_noPreview = new wxStaticText( m_previewHolder, wxID_ANY, _( "There is no preview available" ),
                                    wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL );
    holder->AddStretchSpacer();
    holder->Add( m_noPreview, 0, wxEXPAND );
    holder->AddStretchSpacer();
    m_previewHolder->SetSizer( holder );
    m_previewHolder->SetMinSize( FromDIP( wxSize( 240, -1 ) ) );
    main->Add( m_previewHolder, 1, wxEXPAND | wxLEFT, 4 );

    SetSizer( main );

    m_addBtn->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { EditFootprint( false ); } );
    m_addMenuBtn->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { onAddMenu(); } );
    m_removeBtn->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { removeModel(); } );
    m_editBtn->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { EditFootprint( true ); } );
    m_list->Bind( wxEVT_LIST_ITEM_SELECTED, &SCH_MODEL_PANE::onSelected, this );
    m_list->Bind( wxEVT_LIST_ITEM_ACTIVATED, [this]( wxListEvent& ) { EditFootprint( true ); } );

    Bind( wxEVT_TIMER, &SCH_MODEL_PANE::onTimer, this );
    m_timer.Start( 600 );
}


SCH_MODEL_PANE::~SCH_MODEL_PANE()
{
    m_timer.Stop();
}


void SCH_MODEL_PANE::onTimer( wxTimerEvent& aEvent )
{
    if( !IsShownOnScreen() )
        return;

    LIB_SYMBOL* symbol = m_frame->GetCurSymbol();
    wxString    sig = wxString::Format( wxS( "%p" ), (void*) symbol );

    if( symbol )
    {
        std::vector<SCH_FIELD*> fields;
        symbol->GetFields( fields );

        for( SCH_FIELD* field : fields )
        {
            if( field->GetId() == FIELD_T::FOOTPRINT || field->GetName().StartsWith( wxS( "Sim." ) ) )
                sig << wxS( "|" ) << field->GetName() << wxS( "=" ) << field->GetText();
        }
    }

    if( sig != m_sig )
    {
        m_sig = sig;
        Rebuild();
    }
}


void SCH_MODEL_PANE::Rebuild()
{
    m_rows.clear();
    m_list->DeleteAllItems();

    LIB_SYMBOL* symbol = m_frame->GetCurSymbol();

    if( symbol )
    {
        wxString footprint = symbol->GetFootprintField().GetText();

        if( !footprint.IsEmpty() )
        {
            ROW    row;
            LIB_ID id;
            id.Parse( footprint );

            row.footprint = true;
            row.fpid = id;
            row.model = id.GetUniStringLibItemName();
            row.type = _( "Footprint" );
            row.location = id.GetUniStringLibNickname();

            wxString path = findFootprint( m_frame->Prj(), id );
            row.description = path.IsEmpty()
                                      ? _( "Footprint not found" )
                                      : footprintDescription( footprintFile( path, row.model ) );
            m_rows.push_back( row );
        }

        // A simulation model, from the Sim.* fields
        const SCH_FIELD* device = symbol->FindFieldCaseInsensitive( wxS( "Sim.Device" ) );
        const SCH_FIELD* name = symbol->FindFieldCaseInsensitive( wxS( "Sim.Name" ) );
        const SCH_FIELD* library = symbol->FindFieldCaseInsensitive( wxS( "Sim.Library" ) );

        if( device || name || library )
        {
            ROW row;
            row.footprint = false;
            row.model = name ? name->GetText() : device ? device->GetText() : wxString();
            row.type = _( "Simulation" );
            row.location = library ? library->GetText() : wxString();
            row.description = device ? device->GetText() : wxString();
            m_rows.push_back( row );
        }
    }

    for( size_t ii = 0; ii < m_rows.size(); ++ii )
    {
        long row = m_list->InsertItem( (long) ii, m_rows[ii].model );
        m_list->SetItem( row, 1, m_rows[ii].type );
        m_list->SetItem( row, 2, m_rows[ii].location );
        m_list->SetItem( row, 3, m_rows[ii].description );
    }

    if( !m_rows.empty() )
        m_list->SetItemState( 0, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED );

    bool hasFootprint = !m_rows.empty() && m_rows.front().footprint;

    m_addBtn->Enable( symbol != nullptr );
    m_addMenuBtn->Enable( symbol != nullptr );
    m_removeBtn->Enable( hasFootprint );
    m_editBtn->Enable( hasFootprint );

    updatePreview();
}


void SCH_MODEL_PANE::onSelected( wxListEvent& aEvent )
{
    updatePreview();
}


void SCH_MODEL_PANE::updatePreview()
{
    long   sel = m_list->GetNextItem( -1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED );
    LIB_ID id;

    if( sel >= 0 && sel < (long) m_rows.size() && m_rows[sel].footprint )
        id = m_rows[sel].fpid;

    wxString key = id.IsValid() ? wxString( id.Format().wx_str() ) : wxString();

    if( key == m_previewId && ( m_preview || key.IsEmpty() ) )
        return;

    m_previewId = key;

    if( !id.IsValid() )
    {
        if( m_preview )
            m_preview->Hide();

        m_noPreview->Show();
        m_previewHolder->Layout();
        return;
    }

    // A canvas of its own: made the first time there is something to show
    if( !m_preview )
    {
        m_preview = new FOOTPRINT_PREVIEW_WIDGET( m_previewHolder, m_frame->Kiway() );

        if( !m_preview->IsInitialized() )
        {
            m_preview->Destroy();
            m_preview = nullptr;
            return;
        }

        m_preview->SetUserUnits( m_frame->GetUserUnits() );
        m_previewHolder->GetSizer()->Insert( 0, m_preview, 10, wxEXPAND );
    }

    m_noPreview->Hide();
    m_preview->Show();
    m_preview->ClearStatus();
    m_preview->DisplayFootprint( id );
    m_previewHolder->Layout();
}


void SCH_MODEL_PANE::onAddMenu()
{
    wxMenu menu;
    menu.Append( ID_ADD_FOOTPRINT, _( "Footprint" ) );
    menu.Append( wxID_HIGHEST + 50, _( "Simulation" ) )->Enable( false );
    menu.Append( wxID_HIGHEST + 51, _( "Ibis Model" ) )->Enable( false );
    menu.Append( wxID_HIGHEST + 52, _( "Signal Integrity" ) )->Enable( false );

    wxPoint below = m_addBtn->GetPosition() + wxPoint( 0, m_addBtn->GetSize().y );

    if( GetPopupMenuSelectionFromUser( menu, below ) == ID_ADD_FOOTPRINT )
        EditFootprint( false );
}


void SCH_MODEL_PANE::EditFootprint( bool aEdit )
{
    LIB_SYMBOL* symbol = m_frame->GetCurSymbol();

    if( !symbol )
        return;

    // KiCad keeps one footprint per symbol: adding one replaces it
    wxString         current = aEdit ? symbol->GetFootprintField().GetText() : wxString();
    DIALOG_PCB_MODEL dlg( m_frame, current );

    ApplyNativeListTheme( &dlg );   // plain wxDialog: no DIALOG_SHIM theming
    if( dlg.ShowModal() == wxID_OK )
        setFootprint( dlg.GetFootprint() );
}


void SCH_MODEL_PANE::removeModel()
{
    setFootprint( wxEmptyString );
}


void SCH_MODEL_PANE::setFootprint( const wxString& aFootprint )
{
    LIB_SYMBOL* symbol = m_frame->GetCurSymbol();

    if( !symbol || symbol->GetFootprintField().GetText() == aFootprint )
        return;

    SCH_COMMIT commit( m_frame );
    commit.Modify( symbol, m_frame->GetScreen() );
    symbol->GetFootprintField().SetText( aFootprint );
    commit.Push( aFootprint.IsEmpty() ? _( "Remove Footprint" ) : _( "Edit Footprint" ) );

    m_frame->RebuildView();
    m_sig.clear();
}
