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

#include <dialogs/dialog_create_project.h>

#include <confirm.h>
#include <wildcards_and_files_ext.h>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/collpane.h>
#include <wx/dirdlg.h>
#include <wx/filename.h>
#include <wx/panel.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/treectrl.h>


namespace
{
/// A project type row: an empty project or a KiCad template.
class TYPE_DATA : public wxTreeItemData
{
public:
    TYPE_DATA( bool aTemplate ) : m_template( aTemplate ) {}
    bool m_template;
};


wxColour mix( const wxColour& a, const wxColour& b, double t )
{
    return wxColour( a.Red() + ( b.Red() - a.Red() ) * t, a.Green() + ( b.Green() - a.Green() ) * t,
                     a.Blue() + ( b.Blue() - a.Blue() ) * t );
}
} // namespace


DIALOG_CREATE_PROJECT::DIALOG_CREATE_PROJECT( wxWindow* aParent, const wxString& aDefaultFolder ) :
        DIALOG_SHIM( aParent, wxID_ANY, _( "Create Project" ), wxDefaultPosition, wxDefaultSize,
                     wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER ),
        m_useTemplate( false )
{
    wxColour face = wxSystemSettings::GetColour( wxSYS_COLOUR_BTNFACE );
    wxColour text = wxSystemSettings::GetColour( wxSYS_COLOUR_BTNTEXT );
    wxColour dim = mix( face, text, 0.45 );

    wxBoxSizer* top = new wxBoxSizer( wxVERTICAL );
    wxBoxSizer* columns = new wxBoxSizer( wxHORIZONTAL );

    // LOCATIONS: only Local Projects applies to KiCad
    wxBoxSizer*   locations = new wxBoxSizer( wxVERTICAL );
    wxStaticText* locTitle = new wxStaticText( this, wxID_ANY, _( "LOCATIONS" ) );
    locTitle->SetForegroundColour( dim );
    locations->Add( locTitle, 0, wxBOTTOM, FromDIP( 10 ) );

    for( const wxString& label : { _( "Altium 365" ), _( "Version Control" ) } )
    {
        wxStaticText* item = new wxStaticText( this, wxID_ANY, label );
        item->SetForegroundColour( dim );
        item->SetToolTip( _( "Not available in KiCad" ) );
        locations->Add( item, 0, wxALL, FromDIP( 8 ) );
    }

    wxPanel*      local = new wxPanel( this );
    wxBoxSizer*   localSizer = new wxBoxSizer( wxHORIZONTAL );
    wxStaticText* localText = new wxStaticText( local, wxID_ANY, _( "Local Projects" ) );
    local->SetBackgroundColour( mix( face, *wxWHITE, 0.08 ) );
    localSizer->Add( localText, 1, wxALL, FromDIP( 8 ) );
    local->SetSizer( localSizer );
    locations->Add( local, 0, wxEXPAND );

    columns->Add( locations, 0, wxEXPAND | wxALL, FromDIP( 10 ) );

    // Project Type
    wxBoxSizer* typeSizer = new wxBoxSizer( wxVERTICAL );
    typeSizer->Add( new wxStaticText( this, wxID_ANY, _( "Project Type" ) ), 0, wxBOTTOM,
                    FromDIP( 4 ) );

    m_types = new wxTreeCtrl( this, wxID_ANY, wxDefaultPosition, FromDIP( wxSize( 280, 320 ) ),
                              wxTR_HAS_BUTTONS | wxTR_HIDE_ROOT | wxTR_NO_LINES | wxTR_SINGLE
                                      | wxTR_FULL_ROW_HIGHLIGHT | wxTR_TWIST_BUTTONS );

    wxTreeItemId root = m_types->AddRoot( wxS( "root" ) );
    wxTreeItemId pcb = m_types->AppendItem( root, _( "PCB" ) );
    m_types->SetItemBold( pcb );
    wxTreeItemId empty = m_types->AppendItem( pcb, _( "<Empty>" ), -1, -1, new TYPE_DATA( false ) );
    m_types->AppendItem( pcb, _( "<KiCad Template...>" ), -1, -1, new TYPE_DATA( true ) );

    for( const wxString& label : { _( "Multiboard" ), _( "Harness" ) } )
    {
        wxTreeItemId kind = m_types->AppendItem( root, label );
        m_types->SetItemBold( kind );
        m_types->SetItemTextColour( kind, dim );
        wxTreeItemId item = m_types->AppendItem( kind, _( "<Empty>" ) );
        m_types->SetItemTextColour( item, dim );
    }

    m_types->ExpandAll();
    m_types->SelectItem( empty );

    // Only PCB projects exist in KiCad: keep the pick on one of them
    m_types->Bind( wxEVT_TREE_SEL_CHANGING,
                   [this]( wxTreeEvent& aEvent )
                   {
                       if( !dynamic_cast<TYPE_DATA*>( m_types->GetItemData( aEvent.GetItem() ) ) )
                           aEvent.Veto();
                   } );

    typeSizer->Add( m_types, 1, wxEXPAND );
    columns->Add( typeSizer, 1, wxEXPAND | wxTOP | wxBOTTOM, FromDIP( 10 ) );

    // Name, folder and options
    wxBoxSizer* right = new wxBoxSizer( wxVERTICAL );

    right->Add( new wxStaticText( this, wxID_ANY, _( "Project Name" ) ), 0, wxBOTTOM, FromDIP( 4 ) );
    m_name = new wxTextCtrl( this, wxID_ANY, wxS( "PCB_Project" ) );
    right->Add( m_name, 0, wxEXPAND | wxBOTTOM, FromDIP( 12 ) );

    right->Add( new wxStaticText( this, wxID_ANY, _( "Folder" ) ), 0, wxBOTTOM, FromDIP( 4 ) );
    wxBoxSizer* folderRow = new wxBoxSizer( wxHORIZONTAL );
    m_folder = new wxTextCtrl( this, wxID_ANY, aDefaultFolder );
    wxButton* browse = new wxButton( this, wxID_ANY, wxS( "..." ), wxDefaultPosition,
                                     wxDefaultSize, wxBU_EXACTFIT );
    folderRow->Add( m_folder, 1, wxEXPAND | wxRIGHT, FromDIP( 4 ) );
    folderRow->Add( browse, 0 );
    right->Add( folderRow, 0, wxEXPAND | wxBOTTOM, FromDIP( 12 ) );

    wxCheckBox* constraints = new wxCheckBox( this, wxID_ANY, _( "Constraint Management" ) );
    constraints->Enable( false );
    constraints->SetToolTip( _( "KiCad keeps design rules with the board" ) );
    right->Add( constraints, 0, wxBOTTOM, FromDIP( 12 ) );

    wxCollapsiblePane* params = new wxCollapsiblePane( this, wxID_ANY, _( "Parameters" ) );
    wxStaticText*      paramNote = new wxStaticText( params->GetPane(), wxID_ANY,
                                                     _( "Project text variables can be added "
                                                        "in Project Options after creation." ) );
    paramNote->SetForegroundColour( dim );
    wxBoxSizer* paramSizer = new wxBoxSizer( wxVERTICAL );
    paramSizer->Add( paramNote, 0, wxALL, FromDIP( 6 ) );
    params->GetPane()->SetSizer( paramSizer );
    right->Add( params, 0, wxEXPAND );

    columns->Add( right, 2, wxEXPAND | wxALL, FromDIP( 10 ) );
    top->Add( columns, 1, wxEXPAND );

    wxStdDialogButtonSizer* buttons = new wxStdDialogButtonSizer();
    wxButton*               create = new wxButton( this, wxID_OK, _( "Create" ) );
    buttons->AddButton( create );
    buttons->AddButton( new wxButton( this, wxID_CANCEL, _( "Cancel" ) ) );
    buttons->Realize();
    top->Add( buttons, 0, wxEXPAND | wxALL, FromDIP( 8 ) );

    SetSizer( top );
    SetMinSize( FromDIP( wxSize( 900, 480 ) ) );
    top->SetSizeHints( this );

    browse->Bind( wxEVT_BUTTON,
                  [this]( wxCommandEvent& )
                  {
                      wxDirDialog dlg( this, _( "Choose Folder" ), m_folder->GetValue() );

                      if( dlg.ShowModal() == wxID_OK )
                          m_folder->SetValue( dlg.GetPath() );
                  } );

    create->SetDefault();
    SetInitialFocus( m_name );

    finishDialogSettings();
    Centre();
}


wxString DIALOG_CREATE_PROJECT::GetProjectName() const
{
    return m_name->GetValue().Strip( wxString::both );
}


wxString DIALOG_CREATE_PROJECT::GetFolder() const
{
    return m_folder->GetValue().Strip( wxString::both );
}


wxString DIALOG_CREATE_PROJECT::GetProjectFile() const
{
    wxFileName fn;
    fn.AssignDir( GetFolder() );
    fn.AppendDir( GetProjectName() );
    fn.SetName( GetProjectName() );
    fn.SetExt( FILEEXT::ProjectFileExtension );
    return fn.GetFullPath();
}


bool DIALOG_CREATE_PROJECT::TransferDataFromWindow()
{
    TYPE_DATA* data = dynamic_cast<TYPE_DATA*>( m_types->GetItemData( m_types->GetSelection() ) );
    m_useTemplate = data && data->m_template;

    wxString name = GetProjectName();

    if( name.IsEmpty() || name.find_first_of( wxFileName::GetForbiddenChars() ) != wxString::npos )
    {
        DisplayError( this, _( "Enter a valid project name." ) );
        return false;
    }

    if( GetFolder().IsEmpty() )
    {
        DisplayError( this, _( "Choose a folder for the project." ) );
        return false;
    }

    if( wxFileExists( GetProjectFile() ) )
    {
        DisplayError( this, wxString::Format( _( "A project already exists at '%s'." ),
                                              GetProjectFile() ) );
        return false;
    }

    return true;
}
