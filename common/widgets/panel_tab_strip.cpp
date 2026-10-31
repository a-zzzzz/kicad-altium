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

#include <algorithm>
#include <map>
#include <widgets/wx_aui_utils.h>
#include <widgets/panel_tab_strip.h>
#include <document_windows.h>
#include <paths.h>
#include <wx/fileconf.h>
#include <wx/filename.h>
#include <wx/tokenzr.h>
#include <wx/translation.h>

#include <wx/toplevel.h>

#include <wx/aui/framemanager.h>
#include <wx/dcbuffer.h>
#include <wx/menu.h>
#include <wx/utils.h>
#include <wx/control.h>
#include <wx/settings.h>

#include <algorithm>
#include <climits>
#include <memory>
#include <optional>


namespace
{
const int STRIP_HEIGHT = 24;   // DIP
const int TAB_PAD_X    = 10;   // DIP

/// Share of the window kept for the canvas before the panel columns shrink
const double MIN_CANVAS_SHARE = 0.4;

/// Narrowest a column shrinks to (DIP)
const int MIN_COLUMN_WIDTH = 60;


/// wxAuiManager's dock list (protected; reached through a member pointer, which is legal)
struct AUI_DOCKS_ACCESS : public wxAuiManager
{
    static wxAuiDockInfoArray& Get( wxAuiManager& aMgr )
    {
        return aMgr.*( &AUI_DOCKS_ACCESS::m_docks );
    }
};

wxAuiDockInfoArray& auiDocks( wxAuiManager& aMgr )
{
    return AUI_DOCKS_ACCESS::Get( aMgr );
}

// Colours derive from the system theme so the strip matches the surrounding panels
wxColour mixBase( const wxColour& a, const wxColour& b, double t )
{
    return wxColour( a.Red() + ( b.Red() - a.Red() ) * t, a.Green() + ( b.Green() - a.Green() ) * t,
                     a.Blue() + ( b.Blue() - a.Blue() ) * t );
}

wxColour stripBg()   { return mixBase( wxSystemSettings::GetColour( wxSYS_COLOUR_BTNFACE ), *wxBLACK, 0.17 ); }
wxColour tabText()   { return wxSystemSettings::GetColour( wxSYS_COLOUR_BTNTEXT ); }

wxColour mix( const wxColour& a, const wxColour& b, double t )
{
    return wxColour( a.Red() + ( b.Red() - a.Red() ) * t, a.Green() + ( b.Green() - a.Green() ) * t,
                     a.Blue() + ( b.Blue() - a.Blue() ) * t );
}

// Altium's tab boxes: closed tabs a shade darker than the strip, the open one lighter
wxColour tabFill()     { return stripBg(); }
wxColour tabOpenFill() { return mix( wxSystemSettings::GetColour( wxSYS_COLOUR_BTNFACE ), *wxWHITE, 0.07 ); }
wxColour tabEdge()     { return mix( stripBg(), *wxBLACK, 0.40 ); }
wxColour tabTextDim()  { return tabText(); }
}


/**
 * Vertical tabs along a window edge for a collapsed PANEL_TAB_STRIP column.
 */
class SIDE_TAB_BAR : public wxPanel
{
public:
    SIDE_TAB_BAR( wxWindow* aParent, PANEL_TAB_STRIP* aStrip ) :
            wxPanel( aParent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE ),
            m_strip( aStrip )
    {
        SetBackgroundStyle( wxBG_STYLE_PAINT );
        SetMinSize( wxSize( FromDIP( STRIP_HEIGHT ), -1 ) );

        // Moved to a monitor with another scale: keep the bar's size in DIPs
        Bind( wxEVT_DPI_CHANGED,
              [this]( wxDPIChangedEvent& aEvent )
              {
                  aEvent.Skip();
                  CallAfter( [this]() { RefitAuiBar( this, FromDIP( STRIP_HEIGHT ), -1 ); Refresh(); } );
              } );

        Bind( wxEVT_PAINT, &SIDE_TAB_BAR::onPaint, this );
        Bind( wxEVT_LEFT_DOWN, &SIDE_TAB_BAR::onLeftDown, this );
    }

private:
    void onPaint( wxPaintEvent& )
    {
        wxAutoBufferedPaintDC dc( this );
        dc.SetFont( GetFont() );
        dc.SetBackground( wxBrush( stripBg() ) );
        dc.Clear();

        m_rects.clear();
        int y = FromDIP( 4 );
        int w = GetClientSize().x;

        for( size_t ii : m_strip->GetOpenTabs() )
        {
            wxString label = wxGetTranslation( m_strip->GetTabs()[ii].label );
            wxSize          text = dc.GetTextExtent( label );
            wxRect          r( FromDIP( 2 ), y, w - FromDIP( 4 ), text.x + FromDIP( TAB_PAD_X ) * 2 );

            dc.SetPen( wxPen( tabEdge() ) );
            dc.SetBrush( wxBrush( tabFill() ) );
            dc.DrawRoundedRectangle( r, FromDIP( 3 ) );

            // Text runs top to bottom, like Altium's side tabs
            dc.SetTextForeground( tabText() );
            dc.DrawRotatedText( label, r.x + ( r.width + text.y ) / 2, r.y + FromDIP( TAB_PAD_X ),
                                -90 );

            m_rects.emplace_back( ii, r );
            y = r.GetBottom() + FromDIP( 4 );
        }
    }

    void onLeftDown( wxMouseEvent& aEvent )
    {
        for( const auto& [tab, rect] : m_rects )
        {
            if( rect.Contains( aEvent.GetPosition() ) )
            {
                m_strip->Expand( tab );
                return;
            }
        }
    }

    PANEL_TAB_STRIP*                      m_strip;
    std::vector<std::pair<size_t, wxRect>> m_rects;
};


namespace
{
/// The strips of this module (each editor module has its own copy of this code)
std::vector<PANEL_TAB_STRIP*>& liveStrips()
{
    static std::vector<PANEL_TAB_STRIP*> strips;
    return strips;
}
}


bool PANEL_TAB_STRIP::IsPaneOpen( wxAuiManager& aAuiMgr, const wxString& aPaneName )
{
    wxAuiPaneInfo& pane = aAuiMgr.GetPane( aPaneName );

    if( !pane.IsOk() )
        return false;

    if( pane.IsShown() )
        return true;

    for( PANEL_TAB_STRIP* strip : liveStrips() )
    {
        if( &strip->m_auiMgr != &aAuiMgr )
            continue;

        for( size_t ii = 0; ii < strip->m_tabs.size(); ++ii )
        {
            const std::vector<wxString>& panes = strip->m_tabs[ii].panes;

            if( !panes.empty() && panes[0] == aPaneName )
                return strip->m_open.count( ii ) > 0;
        }
    }

    return false;
}


bool PANEL_TAB_STRIP::CloseIfBackground( wxAuiManager& aAuiMgr, const wxString& aPaneName )
{
    for( PANEL_TAB_STRIP* strip : liveStrips() )
    {
        if( &strip->m_auiMgr != &aAuiMgr )
            continue;

        for( size_t ii = 0; ii < strip->m_tabs.size(); ++ii )
        {
            const std::vector<wxString>& panes = strip->m_tabs[ii].panes;

            if( panes.empty() || panes[0] != aPaneName || !strip->m_open.count( ii ) )
                continue;

            // On screen: the caller hides it, and the strip drops the tab
            if( ii == strip->m_active && !strip->m_collapsed )
                return false;

            strip->showTab( ii, false );
            strip->m_open.erase( ii );

            if( ii == strip->m_active && !strip->m_open.empty() )
                strip->m_active = *strip->m_open.begin();

            if( strip->m_collapsed )
            {
                for( size_t jj = 0; jj < strip->m_tabs.size(); ++jj )
                    strip->showTab( jj, false );
            }

            strip->sync();
            strip->saveState();
            return true;
        }
    }

    return false;
}


void PANEL_TAB_STRIP::saveOrApply( wxTopLevelWindow* aWindow, bool aSave )
{
    for( PANEL_TAB_STRIP* strip : liveStrips() )
    {
        if( wxGetTopLevelParent( strip ) != aWindow || strip->m_paneName.IsEmpty() )
            continue;

        wxString key = wxS( "panels." ) + strip->m_paneName;

        if( aSave )
        {
            strip->sampleWidth();
            int width = strip->m_columnWidth;

            wxString active = strip->m_active < strip->m_tabs.size()
                                      ? strip->m_tabs[strip->m_active].label : wxString();

            DOCUMENT_WINDOWS::SetCarryOver( key, wxString::Format( wxS( "%d|%d|" ), width,
                                                                   strip->m_collapsed ? 1 : 0 )
                                                         + active );
            continue;
        }

        wxString value = DOCUMENT_WINDOWS::GetCarryOver( key );

        if( value.IsEmpty() )
            continue;

        long     width = 0, collapsed = 0;
        wxString active = value.AfterFirst( '|' ).AfterFirst( '|' );

        value.BeforeFirst( '|' ).ToLong( &width );
        value.AfterFirst( '|' ).BeforeFirst( '|' ).ToLong( &collapsed );

        // The same panel in front, when this editor has it too (e.g. Properties)
        size_t front = strip->m_active;

        for( size_t ii = 0; ii < strip->m_tabs.size(); ++ii )
        {
            if( strip->m_tabs[ii].label == active && strip->m_open.count( ii ) )
                front = ii;
        }

        if( width > 0 )
        {
            strip->m_columnWidth = (int) width;
            strip->m_fitTarget = 0;
        }

        if( collapsed )
        {
            if( !strip->m_collapsed )
                strip->Collapse();
        }
        else
        {
            strip->m_collapsed = false;
            strip->activate( front, false );
        }
    }
}


PANEL_TAB_STRIP::~PANEL_TAB_STRIP()
{
    saveState();
    std::erase( liveStrips(), this );
}


static std::unique_ptr<wxFileConfig> panelStateFile()
{
    wxFileName fn( PATHS::GetUserSettingsPath(), wxS( "altium_panels.ini" ) );

    return std::make_unique<wxFileConfig>( wxEmptyString, wxEmptyString, fn.GetFullPath(),
                                           wxEmptyString, wxCONFIG_USE_LOCAL_FILE );
}


void PANEL_TAB_STRIP::saveState() const
{
    if( m_stateKey.IsEmpty() )
        return;

    wxString open;

    for( size_t ii : m_open )
    {
        if( ii < m_tabs.size() && !m_tabs[ii].panes.empty() )
            open << m_tabs[ii].panes[0] << wxS( "," );
    }

    // Every tab of the column, so panels added in a later version can be told from closed ones
    wxString known;

    for( const TAB& tab : m_tabs )
    {
        if( !tab.panes.empty() )
            known << tab.panes[0] << wxS( "," );
    }

    wxString active = m_active < m_tabs.size() && !m_tabs[m_active].panes.empty()
                              ? m_tabs[m_active].panes[0] : wxString();

    try
    {
        std::unique_ptr<wxFileConfig> cfg = panelStateFile();
        cfg->SetPath( wxS( "/" ) + m_stateKey );
        cfg->Write( wxS( "open" ), open );
        cfg->Write( wxS( "known" ), known );
        cfg->Write( wxS( "active" ), active );
        cfg->Write( wxS( "width" ), (long) m_columnWidth );
        cfg->Write( wxS( "collapsed" ), m_collapsed );
        cfg->Flush();
    }
    catch( ... )
    {
        // Settings are a convenience; never fail closing a window over them
    }
}


void PANEL_TAB_STRIP::EnsureListed( const wxString& aPaneName )
{
    for( size_t ii = 0; ii < m_tabs.size(); ++ii )
    {
        if( m_tabs[ii].panes.empty() || m_tabs[ii].panes[0] != aPaneName || m_open.count( ii ) )
            continue;

        bool wasEmpty = m_open.empty();
        m_open.insert( ii );

        if( wasEmpty )
            activate( ii, false );
        else
            Refresh();

        return;
    }
}


void PANEL_TAB_STRIP::restoreState()
{
    if( m_stateKey.IsEmpty() )
        return;

    std::unique_ptr<wxFileConfig> cfg = panelStateFile();
    cfg->SetPath( wxS( "/" ) + m_stateKey );

    wxString open;

    if( !cfg->Read( wxS( "open" ), &open ) )
        return;     // first run: every tab listed, like Altium's default layout

    wxString active = cfg->Read( wxS( "active" ), wxEmptyString );
    long     width = cfg->Read( wxS( "width" ), 0L );
    bool     collapsed = cfg->ReadBool( wxS( "collapsed" ), false );

    std::set<wxString> openNames;
    std::set<wxString> knownNames;

    for( const wxString& name : wxStringTokenize( open, wxS( "," ), wxTOKEN_STRTOK ) )
        openNames.insert( name );

    for( const wxString& name : wxStringTokenize( cfg->Read( wxS( "known" ), wxEmptyString ),
                                                  wxS( "," ), wxTOKEN_STRTOK ) )
    {
        knownNames.insert( name );
    }

    std::set<size_t> restored;
    size_t           front = SIZE_MAX;

    for( size_t ii = 0; ii < m_tabs.size(); ++ii )
    {
        if( m_tabs[ii].panes.empty() )
            continue;

        // Closed last session: stays closed.  Not in the saved column at all (a panel added
        // since, or a file from before "known" was written): listed, as on a first run.
        const wxString& name = m_tabs[ii].panes[0];

        if( !openNames.count( name ) && knownNames.count( name ) )
            continue;

        restored.insert( ii );

        if( m_tabs[ii].panes[0] == active )
            front = ii;
    }

    // Panels added since the state was saved (or a stale file) shouldn't leave a column empty
    if( restored.empty() )
        return;

    m_open = std::move( restored );

    if( width > 0 )
        m_columnWidth = (int) width;

    activate( front != SIZE_MAX ? front : *m_open.begin(), false );

    if( collapsed )
        Collapse();
}


PANEL_TAB_STRIP::PANEL_TAB_STRIP( wxWindow* aParent, wxAuiManager& aAuiMgr,
                                  std::vector<TAB> aTabs ) :
        wxPanel( aParent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE ),
        m_auiMgr( aAuiMgr ),
        m_tabs( std::move( aTabs ) ),
        m_active( 0 ),
        m_collapsed( false ),
        m_sideBar( nullptr )
{
    SetBackgroundStyle( wxBG_STYLE_PAINT );
    SetMinSize( wxSize( -1, FromDIP( STRIP_HEIGHT ) ) );

    // Moved to a monitor with another scale: keep the bar's size in DIPs
    Bind( wxEVT_DPI_CHANGED,
          [this]( wxDPIChangedEvent& aEvent )
          {
              aEvent.Skip();
              CallAfter( [this]() { RefitAuiBar( this, -1, FromDIP( STRIP_HEIGHT ) ); Refresh(); } );
          } );

    Bind( wxEVT_PAINT, &PANEL_TAB_STRIP::onPaint, this );
    Bind( wxEVT_LEFT_DOWN, &PANEL_TAB_STRIP::onLeftDown, this );
    Bind( wxEVT_LEFT_UP, &PANEL_TAB_STRIP::onLeftUp, this );
    Bind( wxEVT_MOTION, &PANEL_TAB_STRIP::onMotion, this );
    Bind( wxEVT_MOUSE_CAPTURE_LOST, [this]( wxMouseCaptureLostEvent& ) { m_dragTab.reset(); } );
    Bind( wxEVT_SIZE,
          [this]( wxSizeEvent& aEvent )
          {
              Refresh();   // labels re-fit to the new width
              aEvent.Skip();
          } );
    // Watch from the frame's idle handler: the strip itself is hidden while no tab is open,
    // but a panel can still be opened from a menu.
    aParent->Bind( wxEVT_IDLE, &PANEL_TAB_STRIP::onIdle, this );
    aParent->Bind( wxEVT_AUI_PANE_BUTTON, &PANEL_TAB_STRIP::onPaneButton, this );

    // Column widths follow the document across a switch (see DOCUMENT_WINDOWS::AddSwitchHook)
    static bool hooked = false;

    if( !hooked )
    {
        hooked = true;
        DOCUMENT_WINDOWS::AddSwitchHook( &PANEL_TAB_STRIP::saveOrApply );
    }

    liveStrips().push_back( this );
}


void PANEL_TAB_STRIP::onPaneButton( wxAuiManagerEvent& aEvent )
{
    wxAuiPaneInfo* pane = aEvent.GetPane();
    int            button = aEvent.GetButton();

    if( !pane || ( button != wxAUI_BUTTON_MAXIMIZE_RESTORE && button != wxAUI_BUTTON_PIN ) )
    {
        aEvent.Skip();
        return;
    }

    auto it = std::find_if( m_tabs.begin(), m_tabs.end(),
                            [&]( const TAB& tab )
                            {
                                return !tab.panes.empty() && tab.panes[0] == pane->name;
                            } );

    if( it == m_tabs.end() )
    {
        aEvent.Skip();
        return;
    }

    if( button == wxAUI_BUTTON_PIN )
    {
        // Altium's pin: fold the column into tabs along the window edge
        CallAfter( [this]() { Collapse(); } );
        return;
    }

    // Altium's drop-down: every panel of the group, the one in front ticked
    wxMenu menu;

    for( size_t ii = 0; ii < m_tabs.size(); ++ii )
    {
        if( m_tabs[ii].panes.empty() )
            continue;

        wxMenuItem* item = menu.AppendCheckItem( wxID_HIGHEST + 1 + (int) ii,
                                                       wxGetTranslation( m_tabs[ii].label ) );
        item->Check( ii == m_active && m_open.count( ii ) );
    }

    wxWindow* frame = GetParent();
    wxPoint   pos = frame->ScreenToClient( wxGetMousePosition() );
    int       id = frame->GetPopupMenuSelectionFromUser( menu, pos );

    wxString picked = id > wxID_HIGHEST ? menu.GetLabelText( id ) : wxString();

    if( picked.IsEmpty() )
        return;

    // The tabs can change while the menu runs: find the picked one again by its name
    CallAfter(
            [this, picked]()
            {
                for( size_t ii = 0; ii < m_tabs.size(); ++ii )
                {
                    if( !m_tabs[ii].panes.empty() && wxMenuItem::GetLabelText( wxGetTranslation( m_tabs[ii].label ) ) == picked )
                    {
                        m_open.insert( ii );
                        activate( ii );
                        return;
                    }
                }
            } );
}


void PANEL_TAB_STRIP::Attach( int aDirection, int aLayer, const wxString& aPaneName )
{
    m_paneName = aPaneName;

    // Drop panes the frame doesn't have (e.g. optional panels that are disabled)
    for( TAB& tab : m_tabs )
    {
        std::erase_if( tab.panes,
                       [&]( const wxString& name )
                       {
                           return !m_auiMgr.GetPane( name ).IsOk();
                       } );
    }

    m_auiMgr.AddPane( this, wxAuiPaneInfo().Name( aPaneName ).Direction( aDirection )
                                    .Layer( aLayer ).Row( 0 )
                                    .CaptionVisible( false ).PaneBorder( false )
                                    .Floatable( false ).Movable( false ).Resizable( false )
                                    .CloseButton( false )
                                    .MinSize( -1, GetMinSize().y ).BestSize( -1, GetMinSize().y )
                                    .MaxSize( -1, GetMinSize().y ) );

    // Panes in a dock column share its height by proportion; the strip must not take a share
    m_auiMgr.GetPane( this ).dock_proportion = 0;

    layoutColumn();

    // Vertical tabs shown along the window edge while the column is collapsed
    m_sideBar = new SIDE_TAB_BAR( GetParent(), this );
    m_auiMgr.AddPane( m_sideBar, wxAuiPaneInfo().Name( aPaneName + wxS( "Side" ) )
                                         .Direction( aDirection ).Layer( 10 ).Row( 0 )
                                         .CaptionVisible( false ).PaneBorder( false )
                                         .Floatable( false ).Movable( false ).Resizable( false )
                                         .DockFixed( true ).CloseButton( false )
                                         .MinSize( m_sideBar->GetMinSize().x, -1 )
                                         .BestSize( m_sideBar->GetMinSize().x, -1 ).Hide() );

    // Like Altium, every panel in the column starts out listed (only the active tab's panes
    // are visible, so a saved layout can't tell which background tabs were open).  The tab
    // visible in the restored layout stays in front.
    m_open.clear();
    std::optional<size_t> front;

    for( size_t ii = 0; ii < m_tabs.size(); ++ii )
    {
        if( m_tabs[ii].panes.empty() )
            continue;

        m_open.insert( ii );

        if( !front && tabShown( ii ) )
            front = ii;
    }

    if( !m_open.empty() )
        activate( front.value_or( *m_open.begin() ) );
    else
        sync();

    // Then the tabs as they were left last session
    if( wxWindow* frame = wxGetTopLevelParent( GetParent() ) )
        m_stateKey = frame->GetName() + wxS( "." ) + aPaneName;

    restoreState();

    // Panels already on this side stay where they are; only ones dragged here later join
    trackDocking();
}


void PANEL_TAB_STRIP::layoutColumn()
{
    wxAuiPaneInfo& self = m_auiMgr.GetPane( this );

    if( !self.IsOk() )
        return;

    // One column: every grouped pane in the same dock/layer/row, the strip at the bottom
    int position = 0;

    for( const TAB& tab : m_tabs )
    {
        for( const wxString& name : tab.panes )
        {
            wxAuiPaneInfo& pane = m_auiMgr.GetPane( name );

            pane.Direction( self.dock_direction ).Layer( self.dock_layer ).Row( 0 )
                    .Position( position++ );

            // Altium's caption buttons on the tab's panel: panel list and pin (see
            // WX_AUI_DOCK_ART::DrawPaneButton)
            if( pane.IsOk() && name == tab.panes[0] )
                pane.MaximizeButton( true ).PinButton( true );
        }
    }

    self.Row( 0 ).Position( position );
}


bool PANEL_TAB_STRIP::trackDocking()
{
    wxAuiPaneInfo& self = m_auiMgr.GetPane( this );

    if( !self.IsOk() )
        return false;

    int  direction = self.dock_direction;
    bool changed = false;

    auto inTabs =
            [&]( const wxString& aName )
            {
                for( const TAB& tab : m_tabs )
                {
                    if( std::find( tab.panes.begin(), tab.panes.end(), aName ) != tab.panes.end() )
                        return true;
                }

                return false;
            };

    // A tab whose panel was dragged away (floated, or docked on another side) leaves the strip
    for( size_t ii = m_tabs.size(); ii-- > 0; )
    {
        if( m_tabs[ii].panes.empty() )
            continue;

        wxAuiPaneInfo& pane = m_auiMgr.GetPane( m_tabs[ii].panes[0] );

        if( pane.IsOk() && pane.IsShown()
                && ( pane.IsFloating() || pane.dock_direction != direction ) )
        {
            removeTab( ii );
            changed = true;
        }
    }

    // A panel dropped on this side (into the column or at the window edge) becomes a tab
    std::map<wxString, bool> onSide;
    std::vector<wxString>    adopt;
    wxAuiPaneInfoArray&      panes = m_auiMgr.GetAllPanes();
    bool                     firstCall = !m_tracking;

    for( size_t ii = 0; ii < panes.size(); ++ii )
    {
        wxAuiPaneInfo& pane = panes[ii];

        if( pane.window == this || pane.window == (wxWindow*) m_sideBar || pane.name.IsEmpty()
                || pane.IsToolbar() || !pane.IsMovable() || !pane.HasCaption()
                || pane.dock_direction == wxAUI_DOCK_CENTER )
        {
            continue;
        }

        bool here = pane.IsShown() && pane.IsDocked() && pane.dock_direction == direction;
        onSide[pane.name] = here;

        if( here && !firstCall && !inTabs( pane.name ) )
        {
            auto it = m_onSide.find( pane.name );

            if( it == m_onSide.end() || !it->second )
                adopt.push_back( pane.name );
        }
    }

    // On the first call this records what is already there
    m_onSide = std::move( onSide );
    m_tracking = true;

    for( const wxString& name : adopt )
    {
        wxAuiPaneInfo& pane = m_auiMgr.GetPane( name );
        wxString       label = pane.caption.IsEmpty() ? name : pane.caption;

        m_tabs.push_back( { label, { name } } );
        m_open.insert( m_tabs.size() - 1 );
        m_active = m_tabs.size() - 1;
        changed = true;
    }

    // Tabs dropped at the window edge land in a new dock row or layer: pull them into the column
    for( const TAB& tab : m_tabs )
    {
        for( const wxString& name : tab.panes )
        {
            wxAuiPaneInfo& pane = m_auiMgr.GetPane( name );

            if( pane.IsOk() && pane.IsShown() && pane.IsDocked()
                    && pane.dock_direction == direction
                    && ( pane.dock_layer != self.dock_layer || pane.dock_row != self.dock_row ) )
            {
                changed = true;
            }
        }
    }

    if( !changed )
        return false;

    layoutColumn();

    // wxAUI has already sized the dock for the dropped panel; keep the width the column had
    if( !m_open.empty() )
        activate( m_open.count( m_active ) ? m_active : *m_open.begin(), false );
    else
        sync();

    return true;
}


void PANEL_TAB_STRIP::removeTab( size_t aTab )
{
    // Out of the group the panel gets ordinary caption buttons back
    if( !m_tabs[aTab].panes.empty() )
    {
        wxAuiPaneInfo& pane = m_auiMgr.GetPane( m_tabs[aTab].panes[0] );

        if( pane.IsOk() )
            pane.MaximizeButton( false ).PinButton( false );
    }

    m_tabs.erase( m_tabs.begin() + aTab );

    std::set<size_t> open;

    for( size_t ii : m_open )
    {
        if( ii < aTab )
            open.insert( ii );
        else if( ii > aTab )
            open.insert( ii - 1 );
    }

    m_open = std::move( open );

    if( m_active > aTab )
        m_active--;
    else if( m_active == aTab )
        m_active = m_open.empty() ? 0 : *m_open.begin();

    m_tabRects.clear();
}


bool PANEL_TAB_STRIP::tabShown( size_t aTab ) const
{
    // Only a tab's main pane says whether the tab is open; companion panes (e.g. a selection
    // filter) may be shown by the frame for its own reasons.
    return !m_tabs[aTab].panes.empty() && m_auiMgr.GetPane( m_tabs[aTab].panes[0] ).IsShown();
}


void PANEL_TAB_STRIP::showTab( size_t aTab, bool aShow )
{
    for( const wxString& name : m_tabs[aTab].panes )
    {
        if( wxAuiPaneInfo& pane = m_auiMgr.GetPane( name ); pane.IsOk() )
            pane.Show( aShow );
    }
}


void PANEL_TAB_STRIP::activate( size_t aTab, bool aSampleWidth )
{
    // Switching tabs must not resize the column: each panel has its own preferred size, and
    // wxAUI would otherwise refit the dock to whichever panel is shown.
    if( aSampleWidth )
        sampleWidth();

    m_active = aTab;
    m_collapsed = false;

    // The remembered width, unless the window is too narrow for it (see fitWidth())
    int width = m_columnWidth;

    if( width > 0 )
    {
        int fit = fitWidth();
        m_fitTarget = fit < m_columnWidth ? fit : 0;
        width = fit;
    }

    for( size_t ii = 0; ii < m_tabs.size(); ++ii )
    {
        showTab( ii, ii == aTab );

        if( ii == aTab && width > 0 )
        {
            for( const wxString& name : m_tabs[ii].panes )
            {
                wxAuiPaneInfo& pane = m_auiMgr.GetPane( name );

                if( pane.IsOk() && pane.IsDocked() )
                    pane.BestSize( width, pane.best_size.y );
            }
        }
    }

    sync();

    // If wxAUI still refitted the dock, force the width back
    applyWidth( width );
}


void PANEL_TAB_STRIP::applyWidth( int aWidth )
{
    if( aWidth > 0 && m_active < m_tabs.size() && !m_tabs[m_active].panes.empty()
            && GetSize().x != aWidth )
    {
        wxAuiPaneInfo& pane = m_auiMgr.GetPane( m_tabs[m_active].panes[0] );
        wxAuiPaneInfo& self = m_auiMgr.GetPane( this );

        if( pane.IsOk() && pane.IsDocked() && self.IsOk() && aWidth < GetSize().x )
        {
            // wxAUI keeps a dock's size between layouts, and a pane's minimum size can only
            // make it wider: set the dock's size the way wxAUI's own sash drag does
            for( wxAuiDockInfo& dock : auiDocks( m_auiMgr ) )
            {
                if( dock.dock_direction == self.dock_direction
                        && dock.dock_layer == self.dock_layer && dock.dock_row == self.dock_row )
                {
                    dock.size = aWidth;
                }
            }

            m_auiMgr.Update();
        }
        else if( pane.IsOk() && pane.IsDocked() )
        {
            SetAuiPaneSize( m_auiMgr, pane, aWidth, -1 );
        }
    }

    // What wxAUI really gave (panel minimum sizes can keep it wider): a different width seen
    // later means the user dragged the sash
    m_fitWidth = m_fitTarget > 0 ? GetSize().x : 0;
}


bool PANEL_TAB_STRIP::isSideColumn() const
{
    wxAuiPaneInfo& self = m_auiMgr.GetPane( const_cast<PANEL_TAB_STRIP*>( this ) );

    return self.IsOk() && ( self.dock_direction == wxAUI_DOCK_LEFT
                            || self.dock_direction == wxAUI_DOCK_RIGHT );
}


void PANEL_TAB_STRIP::sampleWidth()
{
    if( m_collapsed || !IsShownOnScreen() )
        return;

    int width = GetSize().x;

    if( width <= 0 )
        return;

    // A width that changed together with the window's is wxAUI's doing (it squeezes docks
    // that don't fit), not a width the user chose: only a change while the window keeps its
    // size (a sash drag) is remembered
    wxWindow* managed = m_auiMgr.GetManagedWindow();
    int       frameWidth = managed ? managed->GetClientSize().x : 0;
    bool      frameChanged = frameWidth != m_sampleFrameWidth;

    m_sampleFrameWidth = frameWidth;

    if( frameChanged && m_columnWidth > 0 )
        return;

    if( m_fitTarget > 0 )
    {
        // Shrunk to fit a narrow window: that width isn't the one to remember
        if( width == m_fitWidth || m_fitWidth == 0 )
            return;

        // The user dragged the sash of a shrunk column: scale the remembered width by the
        // same amount, so the column keeps (about) the dragged width while the window stays
        // narrow and grows back in proportion when it widens
        long long scaled = (long long) m_columnWidth * width / m_fitTarget;

        m_columnWidth = (int) std::clamp<long long>( scaled, 1, std::max( frameWidth, width ) );
        m_fitTarget = width;
        m_fitWidth = width;
        return;
    }

    m_columnWidth = width;
}


int PANEL_TAB_STRIP::fitWidth() const
{
    if( m_columnWidth <= 0 || !isSideColumn() )
        return m_columnWidth;

    wxWindow* managed = m_auiMgr.GetManagedWindow();

    if( !managed )
        return m_columnWidth;

    int frameWidth = managed->GetClientSize().x;

    if( frameWidth <= 0 )
        return m_columnWidth;

    // Altium keeps its panels from squeezing the document away: when the columns don't leave
    // MIN_CANVAS_SHARE of the window to the canvas, all of them shrink in proportion
    long long total = 0;

    for( PANEL_TAB_STRIP* strip : liveStrips() )
    {
        if( &strip->m_auiMgr != &m_auiMgr || strip->m_collapsed || strip->m_open.empty()
                || strip->m_columnWidth <= 0 || !strip->isSideColumn() )
        {
            continue;
        }

        total += strip->m_columnWidth;
    }

    int room = frameWidth - (int) ( frameWidth * MIN_CANVAS_SHARE );

    if( total <= room || total <= 0 )
        return m_columnWidth;

    int width = (int) ( (long long) m_columnWidth * room / total );

    return std::clamp( width, std::min( m_columnWidth, FromDIP( MIN_COLUMN_WIDTH ) ),
                       m_columnWidth );
}


void PANEL_TAB_STRIP::fitToWindow()
{
    if( m_collapsed || m_columnWidth <= 0 || !IsShownOnScreen() || !isSideColumn() )
        return;

    int fit = fitWidth();

    if( fit < m_columnWidth )
    {
        // Only when the target moves: if wxAUI can't reach it (minimum sizes), retrying on
        // every idle would never settle
        if( fit != m_fitTarget )
        {
            m_fitTarget = fit;
            applyWidth( fit );
        }
    }
    else if( m_fitTarget > 0 )
    {
        // Wide enough again: back to the remembered width
        m_fitTarget = 0;
        applyWidth( m_columnWidth );
    }
}


void PANEL_TAB_STRIP::Collapse()
{
    sampleWidth();   // restored by Expand()

    m_collapsed = true;

    for( size_t ii = 0; ii < m_tabs.size(); ++ii )
        showTab( ii, false );

    sync();
}


void PANEL_TAB_STRIP::Expand( size_t aTab )
{
    m_collapsed = false;
    activate( aTab );
}


void PANEL_TAB_STRIP::sync()
{
    // wxAUI renumbers the positions of the shown panes of a (resizable) dock on every update,
    // so a panel coming back from hiding could sort above the strip: restore the column order
    layoutColumn();

    wxAuiPaneInfo& self = m_auiMgr.GetPane( this );

    if( self.IsOk() )
        self.Show( !m_open.empty() && !m_collapsed );

    if( m_sideBar )
    {
        m_auiMgr.GetPane( m_sideBar ).Show( !m_open.empty() && m_collapsed );
        m_sideBar->Refresh();
    }

    m_auiMgr.Update();
    Refresh();
}


void PANEL_TAB_STRIP::onIdle( wxIdleEvent& aEvent )
{
    aEvent.Skip();

    if( m_paneName.IsEmpty() )
        return;

    // Documents parked behind the current one have nothing to track
    if( wxWindow* frame = wxGetTopLevelParent( GetParent() ); frame && !frame->IsShown() )
        return;

    // A panel is being dragged: wait for the drop
    if( wxGetMouseState().LeftIsDown() )
        return;

    if( trackDocking() )
        return;

    // Remember the column width (also after the user drags its sash) for drops and tab switches
    sampleWidth();

    // The window was resized: shrink the column to leave the canvas room, or grow it back
    fitToWindow();

    bool changed = false;

    if( m_collapsed )
    {
        // A pane opened from a menu while collapsed brings the column back
        for( size_t ii = 0; ii < m_tabs.size(); ++ii )
        {
            if( tabShown( ii ) )
            {
                m_open.insert( ii );
                activate( ii );
                return;
            }
        }

        return;
    }

    // A pane opened from a menu or toolbar: its tab joins the strip and becomes active
    for( size_t ii = 0; ii < m_tabs.size(); ++ii )
    {
        if( ( ii != m_active || !m_open.count( ii ) ) && tabShown( ii ) )
        {
            m_open.insert( ii );
            activate( ii );
            return;
        }
    }

    // The active pane was closed (its close button or a menu toggle): drop its tab
    if( m_open.count( m_active ) && !tabShown( m_active ) )
    {
        m_open.erase( m_active );
        changed = true;

        if( !m_open.empty() )
        {
            activate( *m_open.begin() );
            return;
        }
    }

    // Companion panes the frame re-showed under an inactive tab go back into hiding
    for( size_t ii = 0; ii < m_tabs.size(); ++ii )
    {
        if( ii == m_active )
            continue;

        for( const wxString& name : m_tabs[ii].panes )
        {
            wxAuiPaneInfo& pane = m_auiMgr.GetPane( name );

            if( pane.IsShown() && pane.IsDocked() )
            {
                pane.Hide();
                changed = true;
            }
        }
    }

    if( changed )
        sync();
}


void PANEL_TAB_STRIP::onPaint( wxPaintEvent& aEvent )
{
    wxAutoBufferedPaintDC dc( this );
    dc.SetFont( GetFont() );
    dc.SetBackground( wxBrush( stripBg() ) );
    dc.Clear();

    m_tabRects.assign( m_tabs.size(), wxRect() );

    int x = FromDIP( 2 );
    int h = GetClientSize().y;

    // Each tab is as wide as its label.  When the column is too narrow for all of them, padding
    // tightens first; then only the longest labels are shortened, to a common cap, so short
    // labels stay whole.  The open tab keeps its full label whenever the others can still
    // show a few characters.
    int pad = FromDIP( TAB_PAD_X );
    int gap = 0;

    // The whole width is for tabs (the panel's pin button folds the column, as in Altium)
    int available = GetClientSize().x - FromDIP( 4 );

    auto textWidth =
            [&]( size_t aTab ) -> int
            {
                dc.SetFont( GetFont() );
                return dc.GetTextExtent( wxGetTranslation( m_tabs[aTab].label ) ).x;
            };

    std::map<size_t, int> textW;
    int                   naturalText = 0;

    for( size_t ii : m_open )
    {
        textW[ii] = textWidth( ii );
        naturalText += textW[ii];
    }

    int count = (int) m_open.size();

    if( count > 0 && naturalText + count * ( 2 * pad + gap ) > available )
        pad = std::max( FromDIP( 3 ), ( available - naturalText - count * gap ) / ( 2 * count ) );

    // Widest text each tab may show (water-filling: the cap is raised until the space is used)
    auto capFor =
            [&]( const std::vector<int>& aWidths, int aSpace ) -> int
            {
                std::vector<int> sorted = aWidths;
                std::sort( sorted.begin(), sorted.end() );

                int remaining = aSpace;
                int left = (int) sorted.size();

                for( int w : sorted )
                {
                    if( w * left <= remaining )
                    {
                        remaining -= w;
                        left--;
                    }
                    else
                    {
                        return std::max( 0, remaining / left );
                    }
                }

                return INT_MAX;
            };

    int textSpace = available - count * ( 2 * pad + gap );
    int cap = INT_MAX;
    int activeCap = INT_MAX;

    if( count > 0 && naturalText > textSpace )
    {
        std::vector<int> others;

        for( size_t ii : m_open )
        {
            if( ii != m_active || !textW.count( m_active ) )
                others.push_back( textW[ii] );
        }

        int minShown = FromDIP( 24 );   // about three characters plus the ellipsis

        if( textW.count( m_active ) && others.size() < (size_t) count
                && capFor( others, textSpace - textW[m_active] ) >= minShown )
        {
            cap = capFor( others, textSpace - textW[m_active] );
        }
        else
        {
            std::vector<int> all;

            for( size_t ii : m_open )
                all.push_back( textW[ii] );

            cap = activeCap = capFor( all, textSpace );
        }
    }

    for( size_t ii = 0; ii < m_tabs.size(); ++ii )
    {
        if( !m_open.count( ii ) )
            continue;

        bool active = ii == m_active;

        dc.SetFont( GetFont() );

        wxString label = wxGetTranslation( m_tabs[ii].label );
        int      limit = active ? activeCap : cap;
        int      tw = dc.GetTextExtent( label ).x;

        if( tw > limit )
        {
            label = wxControl::Ellipsize( label, dc, wxELLIPSIZE_END, limit );
            tw = limit;
        }

        int w = tw + pad * 2;

        // Altium's panel tabs: boxes hanging from the panel with rounded bottom corners, the
        // open one a shade lighter than the others, every label in the normal weight
        wxRect r( x, 0, w, h - FromDIP( 3 ) );
        int    radius = FromDIP( 3 );

        dc.SetPen( wxPen( tabEdge() ) );
        dc.SetBrush( wxBrush( active ? tabOpenFill() : tabFill() ) );
        dc.DrawRoundedRectangle( r, radius );

        // Square top corners: the tab continues the panel above it
        dc.SetPen( *wxTRANSPARENT_PEN );
        dc.DrawRectangle( r.x + 1, r.y, r.width - 2, radius + 1 );
        dc.SetPen( wxPen( tabEdge() ) );
        dc.DrawLine( r.x, r.y, r.x, r.y + radius + 1 );
        dc.DrawLine( r.GetRight(), r.y, r.GetRight(), r.y + radius + 1 );

        dc.SetTextForeground( active ? tabText() : tabTextDim() );
        dc.DrawText( label, r.x + pad, r.y + ( r.height - dc.GetTextExtent( label ).y ) / 2 );

        m_tabRects[ii] = r;
        x += w + gap;
    }

    m_collapseRect = wxRect();
}


void PANEL_TAB_STRIP::onLeftDown( wxMouseEvent& aEvent )
{
    if( m_collapseRect.Contains( aEvent.GetPosition() ) )
    {
        Collapse();
        return;
    }

    for( size_t ii = 0; ii < m_tabRects.size(); ++ii )
    {
        if( m_tabRects[ii].Contains( aEvent.GetPosition() ) )
        {
            // A tab can be dragged off the strip to float its panel
            m_dragTab = ii;
            m_dragStart = aEvent.GetPosition();

            // Follow the pointer even once it leaves the strip
            if( !HasCapture() )
                CaptureMouse();

            if( ii != m_active )
                activate( ii );

            return;
        }
    }
}


void PANEL_TAB_STRIP::onLeftUp( wxMouseEvent& aEvent )
{
    m_dragTab.reset();

    if( HasCapture() )
        ReleaseMouse();

    aEvent.Skip();
}


void PANEL_TAB_STRIP::onMotion( wxMouseEvent& aEvent )
{
    aEvent.Skip();

    if( !m_dragTab || !aEvent.LeftIsDown() )
    {
        m_dragTab.reset();

        if( HasCapture() )
            ReleaseMouse();

        return;
    }

    wxPoint delta = aEvent.GetPosition() - m_dragStart;
    int     threshold = FromDIP( 8 );

    if( std::abs( delta.x ) < threshold && std::abs( delta.y ) < threshold )
        return;

    size_t tab = *m_dragTab;
    m_dragTab.reset();

    if( HasCapture() )
        ReleaseMouse();

    // Tabs that show several panes together (e.g. layers + selection filter) stay put
    if( tab >= m_tabs.size() || m_tabs[tab].panes.size() != 1 )
        return;

    wxAuiPaneInfo& pane = m_auiMgr.GetPane( m_tabs[tab].panes[0] );

    if( !pane.IsOk() || !pane.IsFloatable() )
        return;

    // Float the panel under the pointer and hand the drag to wxAUI, which shows the docking
    // hints; where it is dropped decides whether it joins a column again
    wxPoint grab( FromDIP( 60 ), FromDIP( 10 ) );
    wxSize  size = pane.window->GetSize();

    if( size.x < FromDIP( 200 ) || size.y < FromDIP( 120 ) )
        size = pane.floating_size;

    pane.Float().Show().FloatingPosition( wxGetMousePosition() - grab ).FloatingSize( size );
    m_auiMgr.Update();

    m_auiMgr.StartPaneDrag( pane.window, grab );
}
