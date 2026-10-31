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

#include <widgets/components_panel.h>

#include <bitmaps.h>
#include <eda_base_frame.h>
#include <eda_draw_frame.h>
#include <class_draw_panel_gal.h>
#include <tool/actions.h>
#include <tool/tool_manager.h>
#include <widgets/html_window.h>
#include <dialogs/eda_reorderable_list_dialog.h>
#include <dialogs/dialog_libraries_preferences.h>
#include <widgets/lib_tree.h>
#include <widgets/ui_common.h>

#include <wx/bmpbuttn.h>
#include <wx/dataview.h>
#include <wx/hyperlink.h>
#include <wx/uri.h>
#include <wx/choice.h>
#include <wx/menu.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/settings.h>
#include <wx/stattext.h>
#include <wx/utils.h>


COMPONENTS_PANEL::COMPONENTS_PANEL( wxWindow* aParent, EDA_BASE_FRAME* aFrame,
                                    const wxString& aSearchKey ) :
        wxPanel( aParent ),
        m_frame( aFrame ),
        m_searchKey( aSearchKey ),
        m_built( false ),
        m_libraryChoice( nullptr ),
        m_menuButton( nullptr ),
        m_splitter( nullptr ),
        m_treePanel( nullptr ),
        m_treeSizer( nullptr ),
        m_tree( nullptr ),
        m_details( nullptr ),
        m_count( nullptr ),
        m_lastCount( -1 ),
        m_pressed( false ),
        m_dragging( false )
{
    SetFont( KIUI::GetDockedPaneFont( this ) );
    SetSizer( new wxBoxSizer( wxVERTICAL ) );

    // Loading every library can take a while; wait until someone looks at the panel
    Bind( wxEVT_IDLE,
          [this]( wxIdleEvent& aEvent )
          {
              if( !m_built && IsShownOnScreen() )
                  build();

              // Altium's "Results: N" line follows the search as it is typed
              if( m_built && m_adapter && m_count && IsShownOnScreen() )
              {
                  int count = m_adapter->GetMatchingItemCount();

                  if( count != m_lastCount )
                  {
                      m_lastCount = count;
                      m_count->SetLabel( wxString::Format( _( "Results: %d" ), count ) );
                  }

                  // The list selects its best match by itself (no event): keep "Details <name>"
                  // in step with it
                  if( m_tree && !m_tree->GetDataViewCtrl()->IsFrozen() )
                  {
                      wxString name = detailsName( m_tree->GetSelectedLibId() );

                      if( name != m_detailsName )
                      {
                          m_detailsName = name;
                          updateDetailsBar();
                      }
                  }
              }

              aEvent.Skip();
          } );
}


COMPONENTS_PANEL::~COMPONENTS_PANEL()
{
    m_dragTimer.Stop();

    if( m_adapter )
        m_adapter->SaveSettings();
}


void COMPONENTS_PANEL::build()
{
    m_built = true;

    wxBoxSizer* sizer = static_cast<wxBoxSizer*>( GetSizer() );

    // Library selector and the panel menu, as in Altium's header
    wxBoxSizer* header = new wxBoxSizer( wxHORIZONTAL );

    m_libraryChoice = new wxChoice( this, wxID_ANY );
    header->Add( m_libraryChoice, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4 );

    m_menuButton = new wxBitmapButton( this, wxID_ANY, KiBitmapBundle( BITMAPS::panel_menu ),
                                       wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT );
    m_menuButton->SetToolTip( _( "Libraries and options" ) );
    header->Add( m_menuButton, 0, wxALIGN_CENTER_VERTICAL );

    sizer->Add( header, 0, wxEXPAND | wxALL, 4 );

    // Component list on top; the selected component's models and details below
    m_splitter = new wxSplitterWindow( this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                       wxSP_LIVE_UPDATE | wxSP_NOBORDER | wxSP_3DSASH );
    m_splitter->SetExtraStyle( wxWS_EX_TRANSIENT );

    m_treePanel = new wxPanel( m_splitter );
    m_treeSizer = new wxBoxSizer( wxVERTICAL );
    m_treePanel->SetSizer( m_treeSizer );

    m_count = new wxStaticText( m_treePanel, wxID_ANY, wxEmptyString );
    m_count->SetForegroundColour( wxSystemSettings::GetColour( wxSYS_COLOUR_GRAYTEXT ) );

    // Altium's pointer to a wider part search under the list
    m_moreParts = new wxPanel( m_treePanel );
    wxBoxSizer*   moreSizer = new wxBoxSizer( wxVERTICAL );
    wxStaticText* moreLabel = new wxStaticText( m_moreParts, wxID_ANY, _( "Need more components?" ) );
    wxBoxSizer*   linkRow = new wxBoxSizer( wxHORIZONTAL );
    wxStaticText* findIn = new wxStaticText( m_moreParts, wxID_ANY, _( "Find in" ) + wxS( " " ) );
    wxHyperlinkCtrl* link = new wxHyperlinkCtrl( m_moreParts, wxID_ANY, _( "Manufacturer Part Search" ),
                                                 wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                                 wxHL_DEFAULT_STYLE & ~wxHL_CONTEXTMENU );
    linkRow->Add( findIn, 0, wxALIGN_CENTER_VERTICAL );
    linkRow->Add( link, 0, wxALIGN_CENTER_VERTICAL );
    moreSizer->Add( moreLabel, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, 8 );
    moreSizer->Add( linkRow, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 8 );
    m_moreParts->SetSizer( moreSizer );

    link->Bind( wxEVT_HYPERLINK,
                [this]( wxHyperlinkEvent& )
                {
                    // A web part search for what is typed in the search box
                    wxString query = m_tree ? m_tree->GetSearchString() : wxString();
                    wxLaunchDefaultBrowser( wxS( "https://octopart.com/search?q=" )
                                            + wxURI( query ).BuildURI() );
                } );


    wxPanel*    detailsPanel = new wxPanel( m_splitter );
    wxBoxSizer* detailsSizer = new wxBoxSizer( wxVERTICAL );

    wxStaticText* modelsLabel = new wxStaticText( detailsPanel, wxID_ANY, _( "Models" ) );
    modelsLabel->SetFont( modelsLabel->GetFont().Bold() );
    detailsSizer->Add( modelsLabel, 0, wxLEFT | wxTOP, 6 );

    createPreviews( detailsPanel, detailsSizer );

    m_details = new HTML_WINDOW( detailsPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize );
    detailsSizer->Add( m_details, 2, wxEXPAND | wxALL, 4 );
    detailsPanel->SetSizer( detailsSizer );

    m_splitter->SetMinimumPaneSize( FromDIP( 80 ) );
    m_splitter->SetSashGravity( 0.5 );
    m_splitter->Initialize( m_treePanel );
    m_detailsPanel = detailsPanel;
    detailsPanel->Hide();
    sizer->Add( m_splitter, 1, wxEXPAND );

    // Altium's "Details <name>" bar: folded by default, a click opens the models and details
    m_detailsBar = new wxStaticText( this, wxID_ANY, wxEmptyString );
    m_detailsBar->SetFont( m_detailsBar->GetFont().Bold() );
    m_detailsBar->SetCursor( wxCursor( wxCURSOR_HAND ) );
    sizer->Add( m_detailsBar, 0, wxEXPAND | wxALL, 6 );
    m_detailsBar->Bind( wxEVT_LEFT_DOWN, [this]( wxMouseEvent& ) { toggleDetails(); } );
    updateDetailsBar();

    m_libraryChoice->Bind( wxEVT_CHOICE, [this]( wxCommandEvent& ) { rebuildTree(); } );
    m_menuButton->Bind( wxEVT_BUTTON, &COMPONENTS_PANEL::onMenuButton, this );
    Bind( EVT_LIBITEM_SELECTED, &COMPONENTS_PANEL::onSelected, this );

    // Keeps a drag going when the placement tool takes the mouse over
    Bind( wxEVT_MOUSE_CAPTURE_LOST, []( wxMouseCaptureLostEvent& ) {} );
    m_dragTimer.SetOwner( this );
    Bind( wxEVT_TIMER, &COMPONENTS_PANEL::onDragTimer, this, m_dragTimer.GetId() );
    Bind( EVT_LIBITEM_CHOSEN, &COMPONENTS_PANEL::onChosen, this );

    RefreshLibraries();
    Layout();
}


void COMPONENTS_PANEL::RefreshLibraries()
{
    if( !m_built )
        return;

    wxString current = m_libraryChoice->GetStringSelection();

    m_libraryChoice->Clear();
    m_libraryChoice->Append( _( "All Libraries" ) );

    std::vector<wxString> names = libraryNames();
    std::sort( names.begin(), names.end(),
               []( const wxString& a, const wxString& b ) { return a.CmpNoCase( b ) < 0; } );

    for( const wxString& name : names )
        m_libraryChoice->Append( name );

    if( current.IsEmpty() || !m_libraryChoice->SetStringSelection( current ) )
        m_libraryChoice->SetSelection( 0 );

    rebuildTree();
}


void COMPONENTS_PANEL::rebuildTree()
{
    wxString library = m_libraryChoice->GetSelection() > 0 ? m_libraryChoice->GetStringSelection()
                                                           : wxString();
    wxString search = m_tree ? m_tree->GetSearchString() : wxString();

    wxBusyCursor busy;
    Freeze();

    m_treeSizer->Detach( m_count );
    m_treeSizer->Detach( m_moreParts );

    if( m_adapter )
        m_adapter->SaveSettings();   // column choices and widths

    if( m_tree )
    {
        m_treeSizer->Detach( m_tree );
        m_tree->Destroy();
        m_tree = nullptr;
    }

    m_adapter = createAdapter( library );

    // One library: Altium's flat list (no library row) with a component icon on each row
    m_adapter->SetFlatList( !library.IsEmpty() );

    // Altium's headings
    m_adapter->SetColumnTitle( wxS( "Item" ), _( "Design Item ID" ) );
    m_adapter->SetColumnTitle( wxS( "Value" ), _( "Comment" ) );

    m_tree = new LIB_TREE( m_treePanel, m_searchKey, m_adapter,
                           LIB_TREE::FLAGS::SEARCH | LIB_TREE::FLAGS::DETAILS, m_details );
    m_adapter->FinishTreeInitialization();

    if( !search.IsEmpty() )
        m_tree->SetSearchString( search );

    // With one library picked, show its components directly, like Altium's flat list
    if( !library.IsEmpty() )
        m_tree->ExpandAll();

    m_treeSizer->Add( m_tree, 1, wxEXPAND );

    wxWindow* list = m_tree->GetDataViewCtrl()->GetMainWindow();
    list->Bind( wxEVT_LEFT_DOWN, &COMPONENTS_PANEL::onListMouse, this );
    list->Bind( wxEVT_LEFT_UP, &COMPONENTS_PANEL::onListMouse, this );
    list->Bind( wxEVT_MOTION, &COMPONENTS_PANEL::onListMouse, this );

    // Bound after LIB_TREE's own handler, so it runs first and replaces it
    m_tree->GetDataViewCtrl()->Bind( wxEVT_DATAVIEW_ITEM_CONTEXT_MENU,
                                     &COMPONENTS_PANEL::onItemMenu, this );
    m_treeSizer->Add( m_moreParts, 0, wxEXPAND );
    m_treeSizer->Add( m_count, 0, wxLEFT | wxTOP | wxBOTTOM, 4 );
    m_lastCount = -1;

    m_treePanel->Layout();
    Thaw();

    showPreview( LIB_ID(), 0 );

    m_detailsName.clear();
    updateDetailsBar();
}


void COMPONENTS_PANEL::onSelected( wxCommandEvent& aEvent )
{
    int    unit = 0;
    LIB_ID id = m_tree ? m_tree->GetSelectedLibId( &unit ) : LIB_ID();

    m_detailsName = detailsName( id );
    updateDetailsBar();
    showPreview( id, unit );
}


wxString COMPONENTS_PANEL::detailsName( const LIB_ID& aSelected ) const
{
    // Altium's "Details <name>" names the selected component, else the first one listed
    if( aSelected.IsValid() )
        return aSelected.GetUniStringLibItemName();

    wxDataViewItemArray top;

    if( !m_adapter || m_libraryChoice->GetSelection() <= 0
            || m_adapter->GetChildren( wxDataViewItem(), top ) == 0 )
    {
        return wxEmptyString;
    }

    wxVariant name;
    static_cast<wxDataViewModel*>( m_adapter.get() )->GetValue( name, top[0], 0 );

    wxString result = name.GetString();

    // (without the icon mark)
    if( !result.IsEmpty() && result[0] < wxUniChar( 0x20 ) )
        result = result.Mid( 1 );

    return result;
}


void COMPONENTS_PANEL::updateDetailsBar()
{
    if( !m_detailsBar )
        return;

    // Up arrow folded, down arrow open, as in Altium
    wxString arrow = m_detailsOpen ? wxString( wxS( "\u25BE " ) ) : wxString( wxS( "\u25B4 " ) );
    m_detailsBar->SetLabel( arrow + _( "Details" ) + wxS( "  " ) + m_detailsName );
}


void COMPONENTS_PANEL::toggleDetails()
{
    m_detailsOpen = !m_detailsOpen;

    if( m_detailsOpen )
    {
        m_detailsPanel->Show();
        m_splitter->SplitHorizontally( m_treePanel, m_detailsPanel, -FromDIP( 520 ) );
    }
    else if( m_splitter->IsSplit() )
    {
        m_splitter->Unsplit( m_detailsPanel );
    }

    updateDetailsBar();
    Layout();
}


void COMPONENTS_PANEL::onChosen( wxCommandEvent& aEvent )
{
    int    unit = 0;
    LIB_ID id = m_tree ? m_tree->GetSelectedLibId( &unit ) : LIB_ID();

    if( id.IsValid() )
        place( id, unit, true );
}


void COMPONENTS_PANEL::onItemMenu( wxDataViewEvent& aEvent )
{
    wxDataViewItem item = aEvent.GetItem();
    LIB_ID         id = item.IsOk() ? m_adapter->GetAliasFor( item ) : LIB_ID();

    // Library rows keep the tree's own menu
    if( !id.IsValid() )
    {
        aEvent.Skip();
        return;
    }

    int      unit = m_adapter->GetUnitFor( item );
    wxString name = id.GetUniStringLibItemName();

    m_tree->GetDataViewCtrl()->Select( item );

    // As in Altium: place it, or put it in place of the parts selected in the editor
    enum { ID_PLACE = wxID_HIGHEST + 1, ID_REPLACE, ID_REFRESH };

    wxMenu menu;
    menu.Append( ID_PLACE, wxString::Format( _( "Place %s" ), name ) );
    menu.Append( ID_REPLACE, wxString::Format( _( "Replace with %s" ), name ) )
            ->Enable( canReplace() );
    menu.AppendSeparator();
    menu.Append( ID_REFRESH, _( "Refresh" ) + wxS( "\tF5" ) );

    switch( GetPopupMenuSelectionFromUser( menu ) )
    {
    case ID_PLACE:   place( id, unit, true );  break;
    case ID_REPLACE: replace( id, unit );      break;
    case ID_REFRESH: RefreshLibraries();       break;
    default:                                   break;
    }
}


void COMPONENTS_PANEL::onMenuButton( wxCommandEvent& aEvent )
{
    enum { ID_REFRESH = wxID_HIGHEST + 1, ID_COLUMNS, ID_PREFERENCES, ID_LIBRARIES };

    wxMenu menu;
    menu.Append( ID_REFRESH, _( "Refresh" ) );
    menu.Append( ID_COLUMNS, _( "Select Columns..." ) );
    menu.AppendSeparator();
    menu.Append( ID_PREFERENCES, _( "File-based Libraries Preferences..." ) );
    menu.Append( ID_LIBRARIES, _( "Manage Libraries..." ) );

    switch( GetPopupMenuSelectionFromUser( menu, m_menuButton->GetRect().GetBottomLeft() ) )
    {
    case ID_REFRESH:     RefreshLibraries(); break;
    case ID_COLUMNS:     selectColumns();    break;
    case ID_LIBRARIES:   manageLibraries();  break;

    case ID_PREFERENCES:
        DIALOG_LIBRARIES_PREFERENCES::Run( m_frame );
        RefreshLibraries();
        break;

    default:
        break;
    }
}


void COMPONENTS_PANEL::selectColumns()
{
    // Parameter columns, as in Altium's list (also on the list header's right-click menu)
    if( !m_adapter || !m_tree )
        return;

    EDA_REORDERABLE_LIST_DIALOG dlg( m_frame, _( "Select Columns" ), m_adapter->GetAvailableColumns(),
                                     m_adapter->GetShownColumns() );

    if( dlg.ShowModal() == wxID_OK )
    {
        m_adapter->SetShownColumns( dlg.EnabledList() );
        m_tree->Regenerate( true );
        m_adapter->SaveSettings();
    }
}


wxWindow* COMPONENTS_PANEL::canvasUnderMouse() const
{
    EDA_DRAW_FRAME* drawFrame = dynamic_cast<EDA_DRAW_FRAME*>( m_frame );

    if( !drawFrame || !drawFrame->GetCanvas() )
        return nullptr;

    wxWindow* canvas = drawFrame->GetCanvas();

    if( !canvas->IsShownOnScreen() )
        return nullptr;

    return canvas->GetScreenRect().Contains( wxGetMousePosition() ) ? canvas : nullptr;
}


void COMPONENTS_PANEL::forwardToCanvas( wxWindow* aCanvas, wxEventType aType )
{
    wxMouseEvent evt( aType );
    wxPoint      pos = aCanvas->ScreenToClient( wxGetMousePosition() );

    evt.SetPosition( pos );
    evt.SetEventObject( aCanvas );
    evt.SetId( aCanvas->GetId() );
    evt.m_leftDown = ( aType == wxEVT_LEFT_DOWN );
    aCanvas->GetEventHandler()->ProcessEvent( evt );
}


void COMPONENTS_PANEL::onListMouse( wxMouseEvent& aEvent )
{
    aEvent.Skip();   // the list still selects and scrolls as usual

    if( aEvent.LeftDown() )
    {
        m_pressed = true;
        m_pressPos = wxGetMousePosition();
    }
    else if( aEvent.LeftUp() )
    {
        m_pressed = false;
    }
    else if( aEvent.Dragging() && m_pressed && !m_dragging )
    {
        // Like Altium: drag a component out of the list and onto the sheet or board.  The
        // list doesn't hold the mouse, so take it over as soon as a drag starts.
        wxPoint delta = wxGetMousePosition() - m_pressPos;

        if( std::abs( delta.x ) < FromDIP( 8 ) && std::abs( delta.y ) < FromDIP( 8 ) )
            return;

        int    unit = 0;
        LIB_ID id = m_tree ? m_tree->GetSelectedLibId( &unit ) : LIB_ID();

        m_pressed = false;

        if( !id.IsValid() )
            return;

        wxWindow* list = m_tree->GetDataViewCtrl()->GetMainWindow();

        if( list->HasCapture() )
            list->ReleaseMouse();

        m_dragging = true;
        m_lastDragPos = wxGetMousePosition();
        CaptureMouse();
        m_dragTimer.Start( 25 );
        place( id, unit, false );
    }
}


void COMPONENTS_PANEL::onDragTimer( wxTimerEvent& aEvent )
{
    // The drag is followed by polling the pointer: once a placement tool attaches the part it
    // may take the mouse, so this panel can't rely on receiving the button release.
    if( !m_dragging )
    {
        m_dragTimer.Stop();
        return;
    }

    wxWindow*      canvas = canvasUnderMouse();
    wxMouseState   state = wxGetMouseState();
    wxPoint        pos = wxGetMousePosition();

    if( state.LeftIsDown() )
    {
        // The part follows the pointer once it is over the canvas
        if( canvas && pos != m_lastDragPos )
            forwardToCanvas( canvas, wxEVT_MOTION );

        m_lastDragPos = pos;
        return;
    }

    m_dragging = false;
    m_dragTimer.Stop();

    if( HasCapture() )
        ReleaseMouse();

    EDA_DRAW_FRAME* drawFrame = dynamic_cast<EDA_DRAW_FRAME*>( m_frame );

    if( !drawFrame || !drawFrame->GetToolManager() )
        return;

    if( canvas )
    {
        // Dropped on the canvas: a click at the drop point puts the part down
        canvas->SetFocus();
        forwardToCanvas( canvas, wxEVT_MOTION );
        drawFrame->GetToolManager()->PostAction( ACTIONS::cursorClick );
    }
    else
    {
        // Dropped anywhere else: nothing is placed
        drawFrame->GetToolManager()->PostAction( ACTIONS::cancelInteractive );
    }
}
