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

#include <widgets/messages_panel.h>
#include <widgets/panel_tab_strip.h>

#include <document_windows.h>
#include <eda_base_frame.h>
#include <eda_draw_frame.h>
#include <bitmaps.h>
#include <widgets/wx_html_report_panel.h>

#include <wx/clipbrd.h>
#include <wx/dcmemory.h>
#include <wx/dialog.h>
#include <wx/graphics.h>
#include <wx/filename.h>
#include <wx/listctrl.h>
#include <wx/log.h>
#include <wx/menu.h>
#include <wx/regex.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/stattext.h>
#include <wx/thread.h>
#include <wx/toplevel.h>
#include <wx/treectrl.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>


namespace
{
enum COLUMNS
{
    COL_CLASS = 0,
    COL_DOCUMENT,
    COL_SOURCE,
    COL_MESSAGE,
    COL_TIME,
    COL_DATE,
    COL_NUMBER,
    COL_COUNT
};

enum MENU_IDS
{
    ID_COPY = wxID_HIGHEST + 1,
    ID_COPY_ALL,
    ID_CLEAR_ALL
};


std::vector<MESSAGES_PANEL*>& livePanels()
{
    static std::vector<MESSAGES_PANEL*> panels;
    return panels;
}


/// Report panels write HTML fragments; the Messages grid shows plain text.
wxString plainText( const wxString& aHtml )
{
    wxString text = aHtml;

    text.Replace( wxS( "<br>" ), wxS( " " ) );
    text.Replace( wxS( "<br/>" ), wxS( " " ) );

    static wxRegEx tags( wxS( "<[^>]*>" ), wxRE_ADVANCED );
    tags.ReplaceAll( &text, wxEmptyString );

    text.Replace( wxS( "&lt;" ), wxS( "<" ) );
    text.Replace( wxS( "&gt;" ), wxS( ">" ) );
    text.Replace( wxS( "&quot;" ), wxS( "\"" ) );
    text.Replace( wxS( "&nbsp;" ), wxS( " " ) );
    text.Replace( wxS( "&amp;" ), wxS( "&" ) );
    text.Replace( wxS( "\r" ), wxEmptyString );
    text.Replace( wxS( "\n" ), wxS( " " ) );
    text.Replace( wxS( "\t" ), wxS( " " ) );

    return text.Trim().Trim( false );
}


/// Draw a message class icon at \a aSize px (original drawing: a red disc with a cross for
/// errors, an amber triangle with "!" for warnings, a blue disc with "i" otherwise).
wxBitmap drawSeverityIcon( SEVERITY aSeverity, int aSize )
{
    wxImage img( aSize, aSize );
    img.InitAlpha();
    memset( img.GetAlpha(), 0, (size_t) aSize * aSize );

    wxBitmap   bmp( img, 32 );
    wxMemoryDC dc( bmp );

    std::unique_ptr<wxGraphicsContext> gc( wxGraphicsContext::Create( dc ) );

    if( !gc )
        return bmp;

    gc->SetAntialiasMode( wxANTIALIAS_DEFAULT );

    double s = aSize;
    double pen = std::max( 1.0, s / 8.0 );

    switch( aSeverity )
    {
    case RPT_SEVERITY_ERROR:
    {
        gc->SetPen( wxPen( wxColour( 150, 20, 20 ), 1 ) );
        gc->SetBrush( wxBrush( wxColour( 220, 45, 45 ) ) );
        gc->DrawEllipse( 0.5, 0.5, s - 1.5, s - 1.5 );

        double m = s * 0.31;
        gc->SetPen( wxPen( *wxWHITE, pen ) );
        gc->StrokeLine( m, m, s - 1 - m, s - 1 - m );
        gc->StrokeLine( s - 1 - m, m, m, s - 1 - m );
        break;
    }

    case RPT_SEVERITY_WARNING:
    {
        wxGraphicsPath tri = gc->CreatePath();
        tri.MoveToPoint( s / 2, 0.5 );
        tri.AddLineToPoint( s - 0.5, s - 1.0 );
        tri.AddLineToPoint( 0.5, s - 1.0 );
        tri.CloseSubpath();

        gc->SetPen( wxPen( wxColour( 160, 110, 0 ), 1 ) );
        gc->SetBrush( wxBrush( wxColour( 250, 190, 40 ) ) );
        gc->DrawPath( tri );

        gc->SetPen( wxPen( wxColour( 40, 30, 0 ), pen ) );
        gc->StrokeLine( s / 2, s * 0.33, s / 2, s * 0.62 );
        gc->StrokeLine( s / 2, s * 0.74, s / 2, s * 0.80 );
        break;
    }

    default:
    {
        gc->SetPen( wxPen( wxColour( 30, 80, 150 ), 1 ) );
        gc->SetBrush( wxBrush( wxColour( 60, 130, 210 ) ) );
        gc->DrawEllipse( 0.5, 0.5, s - 1.5, s - 1.5 );

        gc->SetPen( wxPen( *wxWHITE, pen ) );
        gc->StrokeLine( s / 2, s * 0.42, s / 2, s * 0.76 );
        gc->StrokeLine( s / 2, s * 0.22, s / 2, s * 0.29 );
        break;
    }
    }

    gc.reset();
    dc.SelectObject( wxNullBitmap );
    return bmp;
}


wxBitmapBundle severityIcon( SEVERITY aSeverity )
{
    wxVector<wxBitmap> bitmaps;

    for( int size : { 16, 20, 24, 32, 48 } )
        bitmaps.push_back( drawSeverityIcon( aSeverity, size ) );

    return wxBitmapBundle::FromBitmaps( bitmaps );
}


/// One line for a tree row (a native tree item shows only the first line of its text)
wxString oneLine( wxString aText )
{
    aText.Replace( wxS( "\r" ), wxEmptyString );
    aText.Replace( wxS( "\n" ), wxS( " " ) );
    aText.Replace( wxS( "\t" ), wxS( " " ) );
    return aText;
}


/// What identifies a row across list refreshes
wxString rowIdentity( const PROJECT_MESSAGE& aMessage )
{
    if( !aMessage.key.IsEmpty() )
        return aMessage.key;

    return aMessage.document + wxS( "\x1f" ) + aMessage.source + wxS( "\x1f" ) + aMessage.text
           + wxS( "\x1f" ) + aMessage.when.FormatISOCombined();
}


/// Tree item data: index into MESSAGES_PANEL::m_detailActions (-1 for the message itself)
class DETAIL_DATA : public wxTreeItemData
{
public:
    explicit DETAIL_DATA( int aIndex ) : m_index( aIndex ) {}
    int m_index;
};
}


/**
 * Virtual report-mode list: the panel owns the rows, the control only draws them.
 */
class MESSAGES_LIST : public wxListCtrl
{
public:
    MESSAGES_LIST( wxWindow* aParent, MESSAGES_PANEL* aPanel ) :
            wxListCtrl( aParent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                        wxLC_REPORT | wxLC_VIRTUAL | wxBORDER_NONE ),
            m_panel( aPanel )
    {
        // Brighter on a dark background so the class stays readable
        bool dark = wxSystemSettings::GetAppearance().IsDark();

        m_error.SetTextColour( dark ? wxColour( 255, 105, 97 ) : wxColour( 200, 30, 30 ) );
        m_warning.SetTextColour( dark ? wxColour( 255, 184, 77 ) : wxColour( 190, 110, 0 ) );
    }

    wxString OnGetItemText( long aItem, long aColumn ) const override
    {
        if( aItem < 0 || aItem >= (long) m_panel->GetCount() )
            return wxEmptyString;

        const MESSAGES_PANEL::MESSAGE& msg = m_panel->GetRow( aItem );

        switch( aColumn )
        {
        case COL_CLASS:    return MESSAGES_PANEL::ClassLabel( msg.severity );
        case COL_DOCUMENT: return msg.document;
        case COL_SOURCE:   return msg.source;
        case COL_MESSAGE:  return msg.text;
        case COL_TIME:     return msg.when.IsValid() ? msg.when.FormatTime() : wxString();
        case COL_DATE:     return msg.when.IsValid() ? msg.when.FormatDate() : wxString();
        case COL_NUMBER:   return wxString::Format( wxS( "%ld" ), aItem + 1 );
        default:           return wxEmptyString;
        }
    }

    // Only the Class column is coloured (as Altium marks a message's class); the message text
    // keeps the normal colour, so it stays readable, also on a selected row
    wxItemAttr* OnGetItemColumnAttr( long aItem, long aColumn ) const override
    {
        return aColumn == COL_CLASS ? OnGetItemAttr( aItem ) : nullptr;
    }

    wxItemAttr* OnGetItemAttr( long aItem ) const override
    {
        if( aItem < 0 || aItem >= (long) m_panel->GetCount() )
            return nullptr;

        switch( m_panel->GetRow( aItem ).severity )
        {
        case RPT_SEVERITY_ERROR:   return const_cast<wxItemAttr*>( &m_error );
        case RPT_SEVERITY_WARNING: return const_cast<wxItemAttr*>( &m_warning );
        default:                   return nullptr;
        }
    }

private:
    MESSAGES_PANEL* m_panel;
    wxItemAttr      m_error;
    wxItemAttr      m_warning;
};


MESSAGES_PANEL::MESSAGES_PANEL( wxWindow* aParent, EDA_DRAW_FRAME* aFrame ) :
        wxPanel( aParent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE ),
        m_frame( aFrame ),
        m_splitter( nullptr ),
        m_details( nullptr ),
        m_detailsOwner( nullptr ),
        m_list( nullptr ),
        m_listenerId( 0 ),
        m_markerSig( 0 ),
        m_markersRead( false )
{
    wxBoxSizer* sizer = new wxBoxSizer( wxVERTICAL );

    // As in Altium: the message list, and under it the Details of the selected message; the
    // split is resizable
    m_splitter = new wxSplitterWindow( this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                       wxSP_LIVE_UPDATE | wxSP_NOBORDER | wxSP_3DSASH );
    m_splitter->SetSashGravity( 1.0 );      // the list takes what the panel gains
    m_splitter->SetMinimumPaneSize( FromDIP( 36 ) );

    m_list = new MESSAGES_LIST( m_splitter, this );
    m_list->AppendColumn( _( "Class" ), wxLIST_FORMAT_LEFT, FromDIP( 80 ) );
    m_list->AppendColumn( _( "Document" ), wxLIST_FORMAT_LEFT, FromDIP( 140 ) );
    m_list->AppendColumn( _( "Source" ), wxLIST_FORMAT_LEFT, FromDIP( 110 ) );
    m_list->AppendColumn( _( "Message" ), wxLIST_FORMAT_LEFT, FromDIP( 520 ) );
    m_list->AppendColumn( _( "Time" ), wxLIST_FORMAT_LEFT, FromDIP( 80 ) );
    m_list->AppendColumn( _( "Date" ), wxLIST_FORMAT_LEFT, FromDIP( 90 ) );
    m_list->AppendColumn( _( "No." ), wxLIST_FORMAT_RIGHT, FromDIP( 50 ) );

    // As in Altium, every column stays in view: Message takes the width the others leave
    m_list->Bind( wxEVT_SIZE,
                  [this]( wxSizeEvent& aEvent )
                  {
                      aEvent.Skip();

                      int others = 0;

                      for( int col = 0; col < m_list->GetColumnCount(); ++col )
                      {
                          if( col != COL_MESSAGE )
                              others += m_list->GetColumnWidth( col );
                      }

                      int width = m_list->GetClientSize().x - others;
                      m_list->SetColumnWidth( COL_MESSAGE, std::max( width, FromDIP( 120 ) ) );
                  } );

    wxPanel*    detailsPanel = new wxPanel( m_splitter, wxID_ANY, wxDefaultPosition,
                                            wxDefaultSize, wxBORDER_NONE );
    wxBoxSizer* detailsSizer = new wxBoxSizer( wxVERTICAL );

    wxStaticText* detailsLabel = new wxStaticText( detailsPanel, wxID_ANY, _( "Details" ) );
    detailsSizer->Add( detailsLabel, 0, wxLEFT | wxTOP | wxBOTTOM, FromDIP( 3 ) );

    m_details = new wxTreeCtrl( detailsPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                wxTR_HAS_BUTTONS | wxTR_HIDE_ROOT | wxTR_LINES_AT_ROOT
                                        | wxTR_SINGLE | wxTR_FULL_ROW_HIGHLIGHT | wxBORDER_NONE );
    detailsSizer->Add( m_details, 1, wxEXPAND );
    detailsPanel->SetSizer( detailsSizer );

    m_splitter->SplitHorizontally( m_list, detailsPanel, -FromDIP( 100 ) );

    sizer->Add( m_splitter, 1, wxEXPAND );
    SetSizer( sizer );

    m_details->Bind( wxEVT_TREE_ITEM_ACTIVATED, &MESSAGES_PANEL::onDetailActivated, this );

    // The Details follow the selected (focused) row
    auto selectionChanged = [this]( wxListEvent& aEvent )
                            {
                                aEvent.Skip();
                                showDetails();
                            };

    m_list->Bind( wxEVT_LIST_ITEM_SELECTED, selectionChanged );
    m_list->Bind( wxEVT_LIST_ITEM_DESELECTED, selectionChanged );
    m_list->Bind( wxEVT_LIST_ITEM_FOCUSED, selectionChanged );

    m_list->Bind( wxEVT_LIST_ITEM_ACTIVATED, &MESSAGES_PANEL::onActivated, this );
    // Right-click on a row or below the last row: Copy / Clear All
    m_list->Bind( wxEVT_CONTEXT_MENU,
                  [this]( wxContextMenuEvent& )
                  {
                      wxListEvent evt;
                      onContextMenu( evt );
                  } );

    m_list->Bind( wxEVT_KEY_DOWN,
                  [this]( wxKeyEvent& aEvent )
                  {
                      if( aEvent.GetModifiers() == wxMOD_CONTROL && aEvent.GetKeyCode() == 'C' )
                          copySelection( false );
                      else if( aEvent.GetModifiers() == wxMOD_CONTROL && aEvent.GetKeyCode() == 'A' )
                      {
                          for( long ii = 0; ii < m_list->GetItemCount(); ++ii )
                              m_list->SetItemState( ii, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED );
                      }
                      else
                          aEvent.Skip();
                  } );

    // Every visible row is custom-drawn (dark theme, coloured class), so a long list costs a
    // few hundred ms per repaint: while the window is being resized, paint it only once the
    // size has settled instead of on every step
    m_resizeTimer.SetOwner( this, wxID_HIGHEST + 77 );
    m_list->Bind( wxEVT_SIZE,
                  [this]( wxSizeEvent& aEvent )
                  {
                      aEvent.Skip();

                      if( m_list->GetItemCount() < 20 )
                          return;

                      if( !m_list->IsFrozen() )
                          m_list->Freeze();

                      m_resizeTimer.StartOnce( 150 );
                  } );

    Bind( wxEVT_TIMER,
          [this]( wxTimerEvent& )
          {
              if( m_list->IsFrozen() )
                  m_list->Thaw();
          },
          wxID_HIGHEST + 77 );

    // Markers change when DRC / ERC run, items are edited or violations are excluded; poll for
    // that rather than hooking every place that touches markers.
    m_timer.SetOwner( this );
    Bind( wxEVT_TIMER, [this]( wxTimerEvent& aEvent )
          {
              if( aEvent.GetId() == wxID_HIGHEST + 77 )
              {
                  aEvent.Skip();
                  return;
              }

              syncMarkers( false );
              scrollIfPending();
          } );
    m_timer.Start( 1000 );

    // New rows that arrived while the panel was hidden (e.g. a DRC that then brings the
    // panel up) are scrolled into view once it shows
    Bind( wxEVT_SHOW,
          [this]( wxShowEvent& aEvent )
          {
              aEvent.Skip();

              if( aEvent.IsShown() )
                  CallAfter( [this]() { scrollIfPending(); } );
          } );

    livePanels().push_back( this );

    // The list is shared by every editor's panel: redraw whenever any of them changes it
    m_listenerId = MESSAGES_STORE::AddListener( [this]() { refreshList( true ); } );
    refreshList( false );

    // Each editor module has its own panel registry; hook it once into the shared report panel
    WX_HTML_REPORT_PANEL::AddReportListener( &MESSAGES_PANEL::PostReport );
}


MESSAGES_PANEL::~MESSAGES_PANEL()
{
    m_timer.Stop();
    m_resizeTimer.Stop();
    std::erase( livePanels(), this );

    MESSAGES_STORE::RemoveListener( m_listenerId );
    MESSAGES_STORE::RemoveOrigin( this );
}


wxAuiPaneInfo MESSAGES_PANEL::DefaultPaneInfo( wxWindow* aWindow )
{
    // As in Altium, a tab of the right-hand panel column by default
    return EDA_PANE().Name( PaneName() )
            .Right().Layer( 4 )
            .Caption( _( "Messages" ) ).PaneBorder( false )
            .MinSize( aWindow->FromDIP( wxSize( 240, 80 ) ) )
            .BestSize( aWindow->FromDIP( wxSize( 900, 200 ) ) )
            .FloatingSize( aWindow->FromDIP( wxSize( 900, 260 ) ) )
            .DestroyOnClose( false )
            .CloseButton( true )
            .Hide();
}


void MESSAGES_PANEL::TogglePane( wxWindow* aFrame )
{
    for( MESSAGES_PANEL* panel : livePanels() )
    {
        if( panel->m_frame != aFrame )
            continue;

        if( wxAuiManager* mgr = wxAuiManager::GetManager( panel ) )
        {
            wxAuiPaneInfo& pane = mgr->GetPane( panel );

            // Open behind another tab: the Panels menu closes it
            if( pane.IsOk() && !PANEL_TAB_STRIP::CloseIfBackground( *mgr, pane.name ) )
            {
                pane.Show( !pane.IsShown() );
                mgr->Update();

                if( pane.IsShown() )
                    panel->syncMarkers( true );
            }
        }

        return;
    }
}


void MESSAGES_PANEL::ShowPane( wxWindow* aFrame )
{
    for( MESSAGES_PANEL* panel : livePanels() )
    {
        if( panel->m_frame == aFrame )
        {
            panel->showPane();
            panel->syncMarkers( true );
            return;
        }
    }
}


bool MESSAGES_PANEL::IsPaneOpen( wxWindow* aFrame )
{
    for( MESSAGES_PANEL* panel : livePanels() )
    {
        if( panel->m_frame != aFrame )
            continue;

        if( wxAuiManager* mgr = wxAuiManager::GetManager( panel ) )
        {
            wxAuiPaneInfo& pane = mgr->GetPane( panel );
            return pane.IsOk() && PANEL_TAB_STRIP::IsPaneOpen( *mgr, pane.name );
        }
    }

    return false;
}


bool MESSAGES_PANEL::IsPaneShown( wxWindow* aFrame )
{
    for( MESSAGES_PANEL* panel : livePanels() )
    {
        if( panel->m_frame != aFrame )
            continue;

        if( wxAuiManager* mgr = wxAuiManager::GetManager( panel ) )
        {
            wxAuiPaneInfo& pane = mgr->GetPane( panel );
            return pane.IsOk() && pane.IsShown();
        }
    }

    return false;
}


wxString MESSAGES_PANEL::ClassLabel( SEVERITY aSeverity )
{
    switch( aSeverity )
    {
    case RPT_SEVERITY_ERROR:   return _( "[Error]" );
    case RPT_SEVERITY_WARNING: return _( "[Warning]" );
    default:                   return _( "[Info]" );
    }
}


wxString MESSAGES_PANEL::documentName() const
{
    if( !m_frame )
        return wxEmptyString;

    wxFileName fn( m_frame->GetCurrentFileName() );
    return fn.GetFullName();
}


void MESSAGES_PANEL::AddMessage( MESSAGE aMessage )
{
    aMessage.origin = this;

    if( !aMessage.owner )
        aMessage.owner = dynamic_cast<wxTopLevelWindow*>( m_frame );

    MESSAGES_STORE::Add( std::move( aMessage ) );
}


void MESSAGES_PANEL::ClearAll()
{
    MESSAGES_STORE::Clear();
}


void MESSAGES_PANEL::syncMarkers( bool aForce )
{
    size_t sig = markerSignature();

    if( !aForce && m_markersRead && sig == m_markerSig )
        return;

    // Markers already in the file when it was opened don't pop the panel up; ones a check
    // (DRC / ERC) adds later do
    bool firstRead = !m_markersRead;

    m_markerSig = sig;
    m_markersRead = true;

    std::vector<MESSAGE> fresh;
    collectMarkers( fresh );

    for( MESSAGE& msg : fresh )
        msg.owner = dynamic_cast<wxTopLevelWindow*>( m_frame );

    // As Altium does after a compile or DRC with problems: bring the Messages panel up
    if( MESSAGES_STORE::SetMarkers( this, std::move( fresh ) ) && !firstRead )
    {
        showPane();
        CallAfter( [this]() { scrollIfPending(); } );
    }
}


void MESSAGES_PANEL::scrollIfPending()
{
    if( !m_scrollPending || !IsShownOnScreen() )
        return;

    m_scrollPending = false;

    if( long count = m_list->GetItemCount(); count > 0 )
        m_list->EnsureVisible( count - 1 );
}


void MESSAGES_PANEL::showPane()
{
    wxAuiManager* mgr = wxAuiManager::GetManager( this );

    if( !mgr )
        return;

    wxAuiPaneInfo& pane = mgr->GetPane( this );

    if( pane.IsOk() && !pane.IsShown() )
    {
        pane.Show();
        mgr->Update();
    }
}


void MESSAGES_PANEL::refreshList( bool aScrollToEnd )
{
    long count = (long) GetCount();
    long selected = m_list->GetNextItem( -1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED );

    m_list->SetItemCount( count );
    m_list->Refresh();

    // The rows are re-read when markers change (and may move): keep the selection on the
    // message the Details show rather than on its old row number
    if( !m_detailsFor.IsEmpty()
            && ( selected < 0 || selected >= count
                 || rowIdentity( GetRow( selected ) ) != m_detailsFor ) )
    {
        long found = -1;

        for( long ii = 0; ii < count; ++ii )
        {
            if( rowIdentity( GetRow( ii ) ) == m_detailsFor )
            {
                found = ii;
                break;
            }
        }

        if( found >= 0 )
        {
            if( selected >= 0 && selected < count )
                m_list->SetItemState( selected, 0, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED );

            m_list->SetItemState( found, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                                  wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED );
        }
    }

    if( aScrollToEnd && count > 0 )
    {
        if( IsShownOnScreen() )
            m_list->EnsureVisible( count - 1 );
        else
            m_scrollPending = true;
    }

    showDetails();
}


void MESSAGES_PANEL::showDetails( bool aForce )
{
    if( !m_details )
        return;

    long idx = m_list->GetNextItem( -1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED );

    if( idx < 0 )
        idx = m_list->GetNextItem( -1, wxLIST_NEXT_ALL, wxLIST_STATE_FOCUSED );

    if( idx >= (long) GetCount() || ( idx >= 0
            && !( m_list->GetItemState( idx, wxLIST_STATE_SELECTED ) & wxLIST_STATE_SELECTED ) ) )
    {
        idx = -1;
    }

    wxString identity = idx >= 0 ? rowIdentity( GetRow( idx ) ) : wxString();

    // Markers are re-read when they change: don't collapse (or flicker) the tree of a row
    // that is still the same
    if( !aForce && identity == m_detailsFor )
        return;

    m_detailsFor = identity;
    m_detailActions.clear();
    m_detailsOwner = nullptr;

    m_details->Freeze();
    m_details->DeleteAllItems();

    if( idx < 0 )
    {
        m_details->Thaw();
        return;
    }

    const MESSAGE& msg = GetRow( idx );
    m_detailsOwner = msg.owner;

    // Images: the three message classes, then each object's own icon
    wxVector<wxBitmapBundle>    images = { severityIcon( RPT_SEVERITY_ERROR ),
                                           severityIcon( RPT_SEVERITY_WARNING ),
                                           severityIcon( RPT_SEVERITY_INFO ) };
    std::map<BITMAPS, int>      iconIndex;

    for( const PROJECT_MESSAGE_DETAIL& detail : msg.details )
    {
        if( detail.icon != BITMAPS::INVALID_BITMAP && !iconIndex.count( detail.icon ) )
        {
            iconIndex[detail.icon] = (int) images.size();
            images.push_back( KiBitmapBundle( detail.icon ) );
        }
    }

    m_details->SetImages( images );

    int classImage = msg.severity == RPT_SEVERITY_ERROR     ? 0
                     : msg.severity == RPT_SEVERITY_WARNING ? 1
                                                            : 2;

    wxTreeItemId root = m_details->AddRoot( wxEmptyString );
    wxTreeItemId item = m_details->AppendItem( root, oneLine( msg.text ), classImage, -1,
                                               new DETAIL_DATA( -1 ) );

    // Altium tints the message line of a violation
    bool dark = wxSystemSettings::GetAppearance().IsDark();

    if( msg.severity == RPT_SEVERITY_ERROR )
        m_details->SetItemBackgroundColour( item, dark ? wxColour( 140, 60, 60 )
                                                       : wxColour( 245, 200, 200 ) );
    else if( msg.severity == RPT_SEVERITY_WARNING )
        m_details->SetItemBackgroundColour( item, dark ? wxColour( 120, 95, 45 )
                                                       : wxColour( 250, 230, 190 ) );

    for( const PROJECT_MESSAGE_DETAIL& detail : msg.details )
    {
        auto it = iconIndex.find( detail.icon );
        int  image = it != iconIndex.end() ? it->second : -1;

        m_details->AppendItem( item, oneLine( detail.text ), image, -1,
                               new DETAIL_DATA( (int) m_detailActions.size() ) );
        m_detailActions.push_back( detail.activate );
    }

    m_details->Expand( item );
    m_details->Thaw();
}


void MESSAGES_PANEL::onDetailActivated( wxTreeEvent& aEvent )
{
    DETAIL_DATA* data = dynamic_cast<DETAIL_DATA*>( m_details->GetItemData( aEvent.GetItem() ) );

    if( !data )
    {
        aEvent.Skip();
        return;
    }

    if( data->m_index < 0 )
    {
        // The message itself: what double-clicking its row does
        long idx = m_list->GetNextItem( -1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED );

        if( idx >= 0 && idx < (long) GetCount() && rowIdentity( GetRow( idx ) ) == m_detailsFor )
            runAction( GetRow( idx ).activate, GetRow( idx ).owner );
        else
            aEvent.Skip();

        return;
    }

    if( data->m_index < (int) m_detailActions.size() )
        runAction( m_detailActions[data->m_index], m_detailsOwner );
}


void MESSAGES_PANEL::runAction( const std::function<void()>& aAction, wxTopLevelWindow* aOwner )
{
    if( !aAction )
        return;

    // A row of another document (e.g. an ERC result in the PCB's panel): switch to it first
    if( aOwner && aOwner != wxGetTopLevelParent( this ) && DOCUMENT_WINDOWS::IsDocument( aOwner ) )
        DOCUMENT_WINDOWS::Activate( aOwner );

    // Copy: the action may change the markers and so the rows
    std::function<void()> action = aAction;
    CallAfter( action );
}


void MESSAGES_PANEL::onActivated( wxListEvent& aEvent )
{
    long idx = aEvent.GetIndex();

    if( idx < 0 || idx >= (long) GetCount() )
        return;

    runAction( GetRow( idx ).activate, GetRow( idx ).owner );
}


void MESSAGES_PANEL::onContextMenu( wxListEvent& aEvent )
{
    wxMenu menu;
    menu.Append( ID_COPY, _( "Copy" ) );
    menu.Append( ID_COPY_ALL, _( "Copy All" ) );
    menu.AppendSeparator();
    menu.Append( ID_CLEAR_ALL, _( "Clear All" ) );

    menu.Enable( ID_COPY, m_list->GetSelectedItemCount() > 0 );
    menu.Enable( ID_COPY_ALL, GetCount() > 0 );
    menu.Enable( ID_CLEAR_ALL, GetCount() > 0 );

    switch( GetPopupMenuSelectionFromUser( menu ) )
    {
    case ID_COPY:      copySelection( false ); break;
    case ID_COPY_ALL:  copySelection( true );  break;
    case ID_CLEAR_ALL: ClearAll();             break;
    default:                                   break;
    }
}


void MESSAGES_PANEL::copySelection( bool aAll )
{
    wxString text;

    for( long ii = 0; ii < (long) GetCount(); ++ii )
    {
        if( !aAll && !( m_list->GetItemState( ii, wxLIST_STATE_SELECTED ) & wxLIST_STATE_SELECTED ) )
            continue;

        for( int col = 0; col < COL_COUNT; ++col )
        {
            if( col )
                text << wxS( "\t" );

            text << m_list->OnGetItemText( ii, col );
        }

        text << wxS( "\n" );
    }

    if( text.IsEmpty() )
        return;

    wxLogNull doNotLog;

    if( wxTheClipboard->Open() )
    {
        wxTheClipboard->SetData( new wxTextDataObject( text ) );
        wxTheClipboard->Flush();
        wxTheClipboard->Close();
    }
}


void MESSAGES_PANEL::PostReport( wxWindow* aOrigin, const wxString& aText, SEVERITY aSeverity )
{
    if( !aOrigin || livePanels().empty() || !wxIsMainThread() )
        return;

    // Only problems go to the project's message list: dialogs report every step they take
    // (e.g. the Engineering Change Order lists each symbol as soon as it opens), and Altium's
    // Messages panel holds the warnings and errors
    if( aSeverity != RPT_SEVERITY_WARNING && aSeverity != RPT_SEVERITY_ERROR )
        return;

    wxString text = plainText( aText );

    if( text.IsEmpty() )
        return;

    // Walk out of nested dialogs to the editor window that owns them
    wxString  source;
    wxWindow* window = aOrigin;

    for( int depth = 0; window && depth < 16; ++depth )
    {
        wxWindow* top = wxGetTopLevelParent( window );

        if( !top )
            return;

        if( wxDialog* dialog = dynamic_cast<wxDialog*>( top ) )
        {
            if( source.IsEmpty() )
                source = dialog->GetTitle();

            window = dialog->GetParent();
            continue;
        }

        for( MESSAGES_PANEL* panel : livePanels() )
        {
            if( panel->m_frame != top )
                continue;

            MESSAGE msg;
            msg.severity = aSeverity;
            msg.document = panel->documentName();
            msg.source = source;

            if( msg.source.IsEmpty() )
            {
                if( wxTopLevelWindow* tlw = dynamic_cast<wxTopLevelWindow*>( top ) )
                    msg.source = tlw->GetTitle();
            }

            msg.text = text;

            // Re-opening a dialog repeats its report: list each message once
            bool known = false;

            for( const MESSAGE& existing : MESSAGES_STORE::Messages() )
            {
                if( existing.severity == msg.severity && existing.text == msg.text
                        && existing.source == msg.source && existing.document == msg.document )
                {
                    known = true;
                    break;
                }
            }

            if( !known )
                panel->AddMessage( std::move( msg ) );
        }

        return;
    }
}
