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

#include <widgets/wx_aui_utils.h>
#include <widgets/altium_chrome.h>

#include <eda_base_frame.h>
#include <tool/action_toolbar.h>
#include <tool/tool_action.h>
#include <tool/tool_event.h>
#include <widgets/wx_aui_art_providers.h>

#include <wx/weakref.h>
#include <wx/dcbuffer.h>
#include <wx/frame.h>
#include <wx/menu.h>
#include <wx/settings.h>

#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#endif


static constexpr int TITLE_HEIGHT = 32;
static constexpr int MENU_HEIGHT = 30;
static constexpr int CAPTION_BUTTON_WIDTH = 46;


ALTIUM_CHROME_COLOURS ALTIUM_CHROME_COLOURS::Get()
{
    ALTIUM_CHROME_COLOURS c;

    if( wxSystemSettings::GetAppearance().IsDark() )
    {
        // Altium's dark theme: title bar #4E4E4E over a #404040 menu row
        c.background = wxColour( 0x40, 0x40, 0x40 );
        c.titleBackground = wxColour( 0x4E, 0x4E, 0x4E );
        c.text = wxColour( 0xF0, 0xF0, 0xF0 );
        c.inactiveText = wxColour( 150, 150, 150 );
        c.hover = wxColour( 0x5A, 0x5A, 0x5A );
        c.pressed = wxColour( 0x6A, 0x6A, 0x6A );
    }
    else
    {
        c.background = wxColour( 238, 238, 240 );
        c.titleBackground = c.background;
        c.text = wxColour( 30, 30, 30 );
        c.inactiveText = wxColour( 130, 130, 130 );
        c.hover = wxColour( 214, 214, 218 );
        c.pressed = wxColour( 196, 196, 202 );
    }

    return c;
}


// ---------------------------------------------------------------------------- title bar

ALTIUM_TITLE_BAR::ALTIUM_TITLE_BAR( wxFrame* aFrame,
                                    const std::vector<const TOOL_ACTION*>& aQuickAccess ) :
        wxPanel( aFrame, wxID_ANY ),
        m_frame( aFrame ),
        m_quickAccess( nullptr ),
        m_hover( NONE ),
        m_pressed( NONE )
{
    SetBackgroundStyle( wxBG_STYLE_PAINT );
    SetBackgroundColour( ALTIUM_CHROME_COLOURS::Get().titleBackground );
    SetMinSize( wxSize( -1, FromDIP( TITLE_HEIGHT ) ) );

    // Moved to a monitor with another scale: keep the bar's size in DIPs
    Bind( wxEVT_DPI_CHANGED,
          [this]( wxDPIChangedEvent& aEvent )
          {
              aEvent.Skip();
              CallAfter( [this]() { RefitAuiBar( this, -1, FromDIP( TITLE_HEIGHT ) ); Refresh(); } );
          } );

    // Quick-access buttons are tool actions, so only an EDA_BASE_FRAME (which has a tool
    // manager) gets them.  Toolbars take their tool manager from the frame they're created in.
    EDA_BASE_FRAME* edaFrame = dynamic_cast<EDA_BASE_FRAME*>( aFrame );

    if( edaFrame && !aQuickAccess.empty() )
    {
        m_quickAccess = new ACTION_TOOLBAR( edaFrame, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                            wxAUI_TB_DEFAULT_STYLE | wxAUI_TB_PLAIN_BACKGROUND
                                                    | wxAUI_TB_HORIZONTAL );
        m_quickAccess->Reparent( this );

        if( WX_AUI_TOOLBAR_ART* art =
                    dynamic_cast<WX_AUI_TOOLBAR_ART*>( m_quickAccess->GetArtProvider() ) )
        {
            art->SetBaseColour( ALTIUM_CHROME_COLOURS::Get().titleBackground );
        }

        m_quickAccess->SetBackgroundColour( ALTIUM_CHROME_COLOURS::Get().titleBackground );

        for( const TOOL_ACTION* action : aQuickAccess )
            m_quickAccess->Add( *action );

        m_quickAccess->KiRealize();
    }

    wxBoxSizer* sizer = new wxBoxSizer( wxHORIZONTAL );
    sizer->AddSpacer( FromDIP( 30 ) );     // application icon

    if( m_quickAccess )
        sizer->Add( m_quickAccess, 0, wxALIGN_CENTER_VERTICAL );

    sizer->AddStretchSpacer();
    sizer->AddSpacer( FromDIP( 3 * CAPTION_BUTTON_WIDTH ) );
    SetSizer( sizer );

    Bind( wxEVT_PAINT, &ALTIUM_TITLE_BAR::onPaint, this );
    Bind( wxEVT_MOTION, &ALTIUM_TITLE_BAR::onMouse, this );
    Bind( wxEVT_LEAVE_WINDOW, &ALTIUM_TITLE_BAR::onMouse, this );
    Bind( wxEVT_LEFT_DOWN, &ALTIUM_TITLE_BAR::onMouse, this );
    Bind( wxEVT_LEFT_UP, &ALTIUM_TITLE_BAR::onMouse, this );
    Bind( wxEVT_MOUSE_CAPTURE_LOST, [this]( wxMouseCaptureLostEvent& ) { m_pressed = NONE; } );
    Bind( wxEVT_SIZE,
          [this]( wxSizeEvent& aEvent )
          {
              Refresh();
              aEvent.Skip();
          } );
}


wxRect ALTIUM_TITLE_BAR::buttonRect( int aButton ) const
{
    wxSize size = GetClientSize();
    int    w = FromDIP( CAPTION_BUTTON_WIDTH );

    return wxRect( size.x - ( 3 - aButton ) * w, 0, w, size.y );
}


int ALTIUM_TITLE_BAR::buttonAt( const wxPoint& aPos ) const
{
    for( int button : { MINIMIZE, MAXIMIZE, CLOSE } )
    {
        if( buttonRect( button ).Contains( aPos ) )
            return button;
    }

    return NONE;
}


#ifdef __WXMSW__
WXLRESULT ALTIUM_TITLE_BAR::MSWWindowProc( WXUINT aMsg, WXWPARAM aWParam, WXLPARAM aLParam )
{
    // Everything but the caption buttons belongs to the frame's caption: let the frame's own
    // hit test answer (it returns HTCAPTION / HTTOP...), so dragging, double-click to maximise,
    // the system menu and snapping all behave natively.
    if( aMsg == WM_NCHITTEST )
    {
        POINT pt = { (int) (short) LOWORD( aLParam ), (int) (short) HIWORD( aLParam ) };
        ::ScreenToClient( GetHWND(), &pt );

        if( buttonAt( wxPoint( pt.x, pt.y ) ) == NONE )
            return HTTRANSPARENT;
    }

    return wxPanel::MSWWindowProc( aMsg, aWParam, aLParam );
}
#endif


void ALTIUM_TITLE_BAR::onMouse( wxMouseEvent& aEvent )
{
    int button = aEvent.Leaving() ? NONE : buttonAt( aEvent.GetPosition() );

    if( aEvent.LeftDown() && button != NONE )
    {
        m_pressed = button;
        CaptureMouse();
    }
    else if( aEvent.LeftUp() && m_pressed != NONE )
    {
        int pressed = m_pressed;
        m_pressed = NONE;

        if( HasCapture() )
            ReleaseMouse();

        if( pressed == button )
        {
            switch( button )
            {
            case MINIMIZE: m_frame->Iconize();                             break;
            case MAXIMIZE: m_frame->Maximize( !m_frame->IsMaximized() );   break;
            case CLOSE:
#ifdef __WXMSW__
                // Same as the native close button, so closing the main window closes all
                // documents (DOCUMENT_WINDOWS)
                ::PostMessage( (HWND) m_frame->GetHWND(), WM_SYSCOMMAND, SC_CLOSE, 0 );
#else
                m_frame->Close();
#endif
                return;
            }
        }
    }

    if( button != m_hover || aEvent.LeftDown() || aEvent.LeftUp() )
    {
        m_hover = button;
        Refresh( false );
    }
}


void ALTIUM_TITLE_BAR::onPaint( wxPaintEvent& aEvent )
{
    wxAutoBufferedPaintDC  dc( this );
    ALTIUM_CHROME_COLOURS  colours = ALTIUM_CHROME_COLOURS::Get();
    wxSize                 size = GetClientSize();
    bool                   active = m_frame->IsActive();

    dc.SetBackground( wxBrush( colours.titleBackground ) );
    dc.Clear();

    // Application icon
    wxIcon icon = m_frame->GetIcons().GetIcon( FromDIP( wxSize( 16, 16 ) ),
                                               wxIconBundle::FALLBACK_NEAREST_LARGER );

    if( icon.IsOk() )
    {
        wxBitmap bmp( icon );
        int      s = FromDIP( 16 );
        dc.DrawBitmap( wxBitmap( bmp.ConvertToImage().Rescale( s, s, wxIMAGE_QUALITY_HIGH ) ),
                       FromDIP( 8 ), ( size.y - s ) / 2, true );
    }

    // Title, centred in the window but kept clear of the quick-access buttons and caption
    // buttons
    wxFont font = GetFont();
    dc.SetFont( font );
    dc.SetTextForeground( active ? colours.text : colours.inactiveText );

    int left = ( m_quickAccess ? m_quickAccess->GetRect().GetRight() : FromDIP( 30 ) )
               + FromDIP( 16 );
    int right = buttonRect( MINIMIZE ).x - FromDIP( 16 );

    if( right > left )
    {
        wxString title = wxControl::Ellipsize( m_frame->GetTitle(), dc, wxELLIPSIZE_END,
                                               right - left );
        wxSize   extent = dc.GetTextExtent( title );
        int      x = std::clamp( ( size.x - extent.x ) / 2, left, right - extent.x );

        dc.DrawText( title, x, ( size.y - extent.y ) / 2 );
    }

    // Caption buttons
    for( int button : { MINIMIZE, MAXIMIZE, CLOSE } )
    {
        wxRect   r = buttonRect( button );
        wxColour glyph = active ? colours.text : colours.inactiveText;

        if( button == m_hover )
        {
            wxColour bg = ( button == CLOSE ) ? wxColour( 232, 17, 35 )
                                              : ( m_pressed == button ? colours.pressed
                                                                      : colours.hover );
            dc.SetPen( *wxTRANSPARENT_PEN );
            dc.SetBrush( wxBrush( bg ) );
            dc.DrawRectangle( r );

            if( button == CLOSE )
                glyph = *wxWHITE;
        }

        int    g = FromDIP( 10 );
        wxRect box( r.x + ( r.width - g ) / 2, r.y + ( r.height - g ) / 2, g, g );

        dc.SetPen( wxPen( glyph, std::max( 1, FromDIP( 1 ) ) ) );
        dc.SetBrush( *wxTRANSPARENT_BRUSH );

        switch( button )
        {
        case MINIMIZE:
            dc.DrawLine( box.x, box.y + g / 2, box.GetRight() + 1, box.y + g / 2 );
            break;

        case MAXIMIZE:
            if( m_frame->IsMaximized() )
            {
                int o = FromDIP( 2 );
                dc.DrawRectangle( box.x, box.y + o, g - o, g - o );
                dc.DrawLine( box.x + o, box.y + o, box.x + o, box.y );
                dc.DrawLine( box.x + o, box.y, box.GetRight() + 1, box.y );
                dc.DrawLine( box.GetRight(), box.y, box.GetRight(), box.GetBottom() - o + 1 );
                dc.DrawLine( box.GetRight(), box.GetBottom() - o, box.GetRight() - o + 1,
                             box.GetBottom() - o );
            }
            else
            {
                dc.DrawRectangle( box );
            }

            break;

        case CLOSE:
            dc.DrawLine( box.x, box.y, box.GetRight() + 1, box.GetBottom() + 1 );
            dc.DrawLine( box.GetRight(), box.y, box.x - 1, box.GetBottom() + 1 );
            break;
        }
    }
}


// ---------------------------------------------------------------------------- menu row

/// Title of a menu in the frame's menu bar, with its '&' mnemonic.  Read from the native menu
/// on Windows: wx's cached titles aren't reliable once the bar is no longer shown natively.
static wxString menuTitle( wxMenuBar* aMenuBar, size_t aIndex )
{
#ifdef __WXMSW__
    if( HMENU bar = (HMENU) aMenuBar->GetHMenu() )
    {
        wchar_t buf[256] = {};

        if( ::GetMenuStringW( bar, (UINT) aIndex, buf, 255, MF_BYPOSITION ) > 0 )
            return wxString( buf );
    }
#endif

    return aMenuBar->GetMenuLabel( aIndex );
}

ALTIUM_MENU_STRIP::ALTIUM_MENU_STRIP( wxFrame* aFrame ) :
        wxPanel( aFrame, wxID_ANY ),
        m_frame( aFrame ),
        m_hover( -1 ),
        m_open( -1 ),
        m_next( -1 ),
        m_nextKeyboard( false ),
        m_openMenu( nullptr ),
        m_selMenu( nullptr ),
        m_selIsPopup( false )
{
    SetBackgroundStyle( wxBG_STYLE_PAINT );
    SetBackgroundColour( ALTIUM_CHROME_COLOURS::Get().background );
    SetMinSize( wxSize( -1, FromDIP( MENU_HEIGHT ) ) );

    // Moved to a monitor with another scale: keep the bar's size in DIPs
    Bind( wxEVT_DPI_CHANGED,
          [this]( wxDPIChangedEvent& aEvent )
          {
              aEvent.Skip();
              CallAfter( [this]() { RefitAuiBar( this, -1, FromDIP( MENU_HEIGHT ) ); Refresh(); } );
          } );

    Bind( wxEVT_PAINT, &ALTIUM_MENU_STRIP::onPaint, this );
    Bind( wxEVT_MOTION, &ALTIUM_MENU_STRIP::onMouse, this );
    Bind( wxEVT_LEAVE_WINDOW, &ALTIUM_MENU_STRIP::onMouse, this );
    Bind( wxEVT_LEFT_DOWN, &ALTIUM_MENU_STRIP::onMouse, this );
    Bind( wxEVT_SIZE,
          [this]( wxSizeEvent& aEvent )
          {
              layoutToolbar();
              Refresh( false );
              aEvent.Skip();
          } );

    // The frame rebuilds the toolbar's contents (and so its size) after it is placed here
    Bind( wxEVT_IDLE,
          [this]( wxIdleEvent& aEvent )
          {
              for( wxWindow* child : GetChildren() )
              {
                  if( child->GetBestSize() != m_toolbarBest )
                  {
                      layoutToolbar();
                      break;
                  }
              }

              aEvent.Skip();
          } );
}


void ALTIUM_MENU_STRIP::layoutToolbar()
{
    // Main toolbar hugs the right end of the row, but never covers the menu titles: when the
    // row is too narrow it is clipped from the right (and hidden once there is no room at all).
    layoutTitles();

    int titlesEnd = m_titleRects.empty() ? 0 : m_titleRects.back().GetRight();
    int minX = titlesEnd + FromDIP( 12 );

    for( wxWindow* child : GetChildren() )
    {
        child->InvalidateBestSize();

        wxSize best = child->GetBestSize();
        wxSize size = GetClientSize();
        m_toolbarBest = best;

        int    x = std::max( minX, size.x - best.x - FromDIP( 4 ) );
        int    w = std::min( best.x, size.x - FromDIP( 4 ) - x );

        if( w < FromDIP( 24 ) )
        {
            child->Hide();
            continue;
        }

        child->SetSize( x, ( size.y - best.y ) / 2, w, best.y );
        child->Show();
    }
}


void ALTIUM_MENU_STRIP::SetToolbar( ACTION_TOOLBAR* aToolbar )
{
    aToolbar->Reparent( this );
    aToolbar->SetBackgroundColour( ALTIUM_CHROME_COLOURS::Get().background );

    if( WX_AUI_TOOLBAR_ART* art = dynamic_cast<WX_AUI_TOOLBAR_ART*>( aToolbar->GetArtProvider() ) )
        art->SetBaseColour( ALTIUM_CHROME_COLOURS::Get().background );
    aToolbar->Show();

    layoutToolbar();
}


void ALTIUM_MENU_STRIP::layoutTitles()
{
    m_titleRects.clear();

    wxMenuBar* menuBar = m_frame->GetMenuBar();

    if( !menuBar )
        return;

    wxClientDC dc( this );
    dc.SetFont( GetFont() );

    int x = FromDIP( 4 );
    int pad = FromDIP( 8 );
    int h = GetClientSize().y;

    for( size_t ii = 0; ii < menuBar->GetMenuCount(); ++ii )
    {
        int w = dc.GetTextExtent( wxStripMenuCodes( menuTitle( menuBar, ii ) ) ).x + 2 * pad;
        m_titleRects.emplace_back( x, FromDIP( 3 ), w, h - FromDIP( 6 ) );
        x += w;
    }
}


int ALTIUM_MENU_STRIP::titleAt( const wxPoint& aPos ) const
{
    for( size_t ii = 0; ii < m_titleRects.size(); ++ii )
    {
        if( m_titleRects[ii].Contains( aPos ) )
            return (int) ii;
    }

    return -1;
}


int ALTIUM_MENU_STRIP::FindMnemonic( int aKey ) const
{
    wxMenuBar* menuBar = m_frame->GetMenuBar();

    if( !menuBar )
        return -1;

    wxChar letter = wxTolower( static_cast<wxChar>( aKey ) );

    for( size_t ii = 0; ii < menuBar->GetMenuCount(); ++ii )
    {
        wxString label = menuTitle( menuBar, ii );
        size_t   pos = label.find( '&' );

        while( pos != wxString::npos && pos + 1 < label.length() && label[pos + 1] == '&' )
            pos = label.find( '&', pos + 2 );

        if( pos != wxString::npos && pos + 1 < label.length()
                && wxTolower( label[pos + 1] ) == letter )
        {
            return (int) ii;
        }
    }

    return -1;
}


#ifdef __WXMSW__
// The menu strip whose menu is down (menus are modal, so there is at most one) and the hooks
// that watch its menu loop.
static ALTIUM_MENU_STRIP* s_trackingStrip = nullptr;
static HHOOK              s_msgFilterHook = nullptr;
static HHOOK              s_callWndHook = nullptr;


/// Sees the messages of the menu's modal loop (mouse and keys) before the menu handles them.
static LRESULT CALLBACK menuMsgFilterProc( int aCode, WPARAM aWParam, LPARAM aLParam )
{
    if( aCode == MSGF_MENU && s_trackingStrip )
    {
        if( s_trackingStrip->MenuLoopMessage( reinterpret_cast<MSG*>( aLParam ) ) )
            return TRUE;
    }

    return ::CallNextHookEx( s_msgFilterHook, aCode, aWParam, aLParam );
}


/// Sees WM_MENUSELECT, sent to the owner frame whenever the highlighted item changes.
static LRESULT CALLBACK menuCallWndProc( int aCode, WPARAM aWParam, LPARAM aLParam )
{
    if( aCode == HC_ACTION && s_trackingStrip )
    {
        const CWPSTRUCT* cwp = reinterpret_cast<const CWPSTRUCT*>( aLParam );

        if( cwp->message == WM_MENUSELECT && cwp->lParam )
            s_trackingStrip->MenuSelected( (void*) cwp->lParam, HIWORD( cwp->wParam ) );
    }

    return ::CallNextHookEx( s_callWndHook, aCode, aWParam, aLParam );
}


void ALTIUM_MENU_STRIP::MenuSelected( void* aMenu, unsigned aFlags )
{
    if( aFlags == 0xFFFF )      // menu closing
        return;

    m_selMenu = aMenu;
    m_selIsPopup = ( aFlags & MF_POPUP ) != 0;
}


bool ALTIUM_MENU_STRIP::MenuLoopMessage( void* aMsg )
{
    const MSG* msg = static_cast<const MSG*>( aMsg );
    int        count = (int) m_titleRects.size();

    auto switchTo =
            [&]( int aIndex, bool aKeyboard )
            {
                m_next = aIndex;
                m_nextKeyboard = aKeyboard;
                ::EndMenu();
            };

    switch( msg->message )
    {
    case WM_MOUSEMOVE:
    {
        wxPoint screen( msg->pt.x, msg->pt.y );

        // Menus send synthetic moves when they open; only act on real movement
        if( screen == m_lastMouse )
            return false;

        m_lastMouse = screen;

        if( !GetScreenRect().Contains( screen ) )
            return false;

        int title = titleAt( ScreenToClient( screen ) );

        if( title >= 0 && title != m_open && m_next < 0 )
        {
            switchTo( title, false );
            return true;
        }

        return false;
    }

    case WM_LBUTTONDOWN:
    case WM_NCLBUTTONDOWN:
    {
        // A click on the open menu's own title closes it (and isn't passed on to reopen it)
        wxPoint screen( msg->pt.x, msg->pt.y );

        if( GetScreenRect().Contains( screen ) && titleAt( ScreenToClient( screen ) ) == m_open )
        {
            m_next = -1;
            ::EndMenu();
            return true;
        }

        return false;
    }

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if( count < 2 || m_next >= 0 )
            return false;

        // Left at the top level (the highlighted item is in the dropped-down menu itself)
        // goes to the previous menu; Left inside a submenu closes that submenu as usual.
        if( msg->wParam == VK_LEFT && ( !m_selMenu || m_selMenu == m_openMenu ) )
        {
            switchTo( ( m_open + count - 1 ) % count, true );
            return true;
        }

        // Right goes to the next menu unless the highlighted item opens a submenu
        if( msg->wParam == VK_RIGHT && !m_selIsPopup )
        {
            switchTo( ( m_open + 1 ) % count, true );
            return true;
        }

        return false;

    default:
        return false;
    }
}
#endif


void ALTIUM_MENU_STRIP::OpenMenu( int aIndex, bool aFromKeyboard )
{
#ifdef __WXMSW__
    // Menus are modal: never start a second loop from inside the first
    if( s_trackingStrip )
        return;

    layoutTitles();

    int  index = aIndex;
    bool keyboard = aFromKeyboard;

    while( index >= 0 )
    {
        wxMenuBar* menuBar = m_frame->GetMenuBar();

        if( !menuBar || index >= (int) m_titleRects.size()
                || index >= (int) menuBar->GetMenuCount() )
        {
            break;
        }

        wxMenu* menu = menuBar->GetMenu( index );

        if( !menu || !menu->GetHMenu() )
            break;

        m_open = index;
        m_next = -1;
        m_nextKeyboard = false;
        m_openMenu = menu->GetHMenu();
        m_selMenu = nullptr;
        m_selIsPopup = false;
        m_lastMouse = wxGetMousePosition();
        m_hover = -1;
        Refresh( false );
        Update();

        // The menu still belongs to the frame's (hidden) menu bar, so the frame gets the usual
        // WM_INITMENUPOPUP / WM_COMMAND and wx updates and dispatches exactly as for a menu bar.
        wxPoint pos = ClientToScreen( m_titleRects[index].GetBottomLeft() );
        HWND    owner = (HWND) m_frame->GetHWND();

        // Opened from the keyboard: highlight the first item, as a native menu bar does
        if( keyboard )
            ::PostMessage( owner, WM_KEYDOWN, VK_DOWN, 0 );

        s_trackingStrip = this;
        s_msgFilterHook = ::SetWindowsHookEx( WH_MSGFILTER, menuMsgFilterProc, nullptr,
                                              ::GetCurrentThreadId() );
        s_callWndHook = ::SetWindowsHookEx( WH_CALLWNDPROC, menuCallWndProc, nullptr,
                                            ::GetCurrentThreadId() );

        ::TrackPopupMenuEx( (HMENU) menu->GetHMenu(),
                            TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON, pos.x, pos.y, owner,
                            nullptr );

        if( s_msgFilterHook )
            ::UnhookWindowsHookEx( s_msgFilterHook );

        if( s_callWndHook )
            ::UnhookWindowsHookEx( s_callWndHook );

        s_msgFilterHook = nullptr;
        s_callWndHook = nullptr;
        s_trackingStrip = nullptr;

        index = m_next;
        keyboard = m_nextKeyboard;
        m_next = -1;
    }

    m_open = -1;
    m_openMenu = nullptr;
    m_selMenu = nullptr;
    m_hover = titleAt( ScreenToClient( wxGetMousePosition() ) );
    Refresh( false );
#endif
}


void ALTIUM_MENU_STRIP::onMouse( wxMouseEvent& aEvent )
{
    if( m_titleRects.empty() )
        layoutTitles();

    int title = aEvent.Leaving() ? -1 : titleAt( aEvent.GetPosition() );

    if( aEvent.LeftDown() && title >= 0 )
    {
        OpenMenu( title );
        return;
    }

    if( title != m_hover )
    {
        m_hover = title;
        Refresh( false );
    }
}


void ALTIUM_MENU_STRIP::onPaint( wxPaintEvent& aEvent )
{
    wxAutoBufferedPaintDC dc( this );
    ALTIUM_CHROME_COLOURS colours = ALTIUM_CHROME_COLOURS::Get();

    dc.SetBackground( wxBrush( colours.background ) );
    dc.Clear();

    wxMenuBar* menuBar = m_frame->GetMenuBar();

    if( !menuBar )
        return;

    layoutTitles();
    dc.SetFont( GetFont() );
    dc.SetTextForeground( colours.text );

    for( size_t ii = 0; ii < m_titleRects.size() && ii < menuBar->GetMenuCount(); ++ii )
    {
        const wxRect& r = m_titleRects[ii];

        if( (int) ii == m_open || (int) ii == m_hover )
        {
            dc.SetPen( *wxTRANSPARENT_PEN );
            dc.SetBrush( wxBrush( (int) ii == m_open ? colours.pressed : colours.hover ) );
            dc.DrawRectangle( r );
        }

        wxString label = wxStripMenuCodes( menuTitle( menuBar, ii ) );
        wxSize   extent = dc.GetTextExtent( label );
        dc.DrawText( label, r.x + ( r.width - extent.x ) / 2, r.y + ( r.height - extent.y ) / 2 );
    }
}


#ifdef __WXMSW__
namespace
{
/// GetSystemMetrics() at the window's own DPI (per-monitor aware), falling back to the
/// system DPI on Windows versions without GetSystemMetricsForDpi().
int metricForWindow( HWND aWindow, int aIndex )
{
    using GET_DPI = UINT( WINAPI* )( HWND );
    using GET_METRIC = int( WINAPI* )( int, UINT );

    static HMODULE    user32 = ::GetModuleHandleW( L"user32.dll" );
    static GET_DPI    getDpi = user32 ? reinterpret_cast<GET_DPI>(
                                                ::GetProcAddress( user32, "GetDpiForWindow" ) )
                                      : nullptr;
    static GET_METRIC getMetric = user32 ? reinterpret_cast<GET_METRIC>( ::GetProcAddress(
                                                   user32, "GetSystemMetricsForDpi" ) )
                                         : nullptr;

    if( getDpi && getMetric )
        return getMetric( aIndex, getDpi( aWindow ) );

    return ::GetSystemMetrics( aIndex );
}
} // namespace


void ALTIUM_CHROME::HideNativeCaption( wxFrame* aFrame )
{
    // The frame's wxMenuBar stays attached (wx still routes its commands and updates), but
    // Windows doesn't draw it; the menu row does.
    ::SetMenu( aFrame->GetHWND(), nullptr );

    // Re-run WM_NCCALCSIZE so the native caption goes away
    ::SetWindowPos( aFrame->GetHWND(), nullptr, 0, 0, 0, 0,
                    SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE );
}


WXLRESULT ALTIUM_CHROME::WindowProc( wxFrame* aFrame, wxWindow* aTitleBar, WXUINT aMsg,
                                     WXWPARAM aWParam, WXLPARAM aLParam,
                                     const std::function<WXLRESULT( WXUINT, WXWPARAM,
                                                                    WXLPARAM )>& aDefault )
{
    if( !aTitleBar )
        return aDefault( aMsg, aWParam, aLParam );

    HWND hwnd = aFrame->GetHWND();
    int  frameX = metricForWindow( hwnd, SM_CXFRAME ) + metricForWindow( hwnd, SM_CXPADDEDBORDER );
    int  frameY = metricForWindow( hwnd, SM_CYFRAME ) + metricForWindow( hwnd, SM_CXPADDEDBORDER );

    if( aMsg == WM_SIZE )
    {
        // Laid out while minimised (a tiny window) the title bar and panels can come back
        // with stale positions -- caption buttons off the edge, a gap at one side.  Lay the
        // whole frame out again once it is restored.
        static const wchar_t* MINIMISED_PROP = L"KiAltiumMinimised";

        if( aWParam == SIZE_MINIMIZED )
        {
            ::SetPropW( hwnd, MINIMISED_PROP, (HANDLE) 1 );
        }
        else if( ::GetPropW( hwnd, MINIMISED_PROP ) )
        {
            ::RemovePropW( hwnd, MINIMISED_PROP );

            wxWeakRef<wxFrame> frame( aFrame );

            aFrame->CallAfter(
                    [frame]()
                    {
                        if( !frame )
                            return;

                        ::SetWindowPos( frame->GetHWND(), nullptr, 0, 0, 0, 0,
                                        SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER
                                                | SWP_NOACTIVATE );
                        frame->SendSizeEvent();
                        frame->Layout();
                        frame->Refresh();
                    } );
        }
    }

    if( aMsg == WM_NCCALCSIZE && aWParam )
    {
        // Keep the (invisible) resize borders at the sides and bottom, drop the caption.
        // A maximised window hangs over the screen edges by the border width, so keep
        // that much at the top too.
        NCCALCSIZE_PARAMS* params = reinterpret_cast<NCCALCSIZE_PARAMS*>( aLParam );
        RECT&              r = params->rgrc[0];

        r.left += frameX;
        r.right -= frameX;
        r.bottom -= frameY;

        if( ::IsZoomed( hwnd ) )
            r.top += frameY;

        return 0;
    }

    if( aMsg == WM_NCHITTEST )
    {
        LRESULT hit = aDefault( aMsg, aWParam, aLParam );

        if( hit != HTCLIENT )
            return hit;

        POINT pt = { (int) (short) LOWORD( aLParam ), (int) (short) HIWORD( aLParam ) };
        ::ScreenToClient( hwnd, &pt );

        if( !::IsZoomed( hwnd ) && pt.y < frameY )
        {
            RECT client;
            ::GetClientRect( hwnd, &client );

            if( pt.x < frameX )
                return HTTOPLEFT;
            else if( pt.x >= client.right - frameX )
                return HTTOPRIGHT;

            return HTTOP;
        }

        // The title bar passes its empty space through to here
        if( aTitleBar->GetRect().Contains( pt.x, pt.y ) )
            return HTCAPTION;

        return hit;
    }

    return aDefault( aMsg, aWParam, aLParam );
}


void ALTIUM_CHROME::BindMenuKeys( wxFrame* aFrame, ALTIUM_MENU_STRIP* aStrip,
                                  const std::function<bool( int )>& aIsHotkey )
{
    // Without a native menu bar Windows no longer opens menus from the keyboard
    aFrame->Bind( wxEVT_CHAR_HOOK,
                  [aFrame, aStrip, aIsHotkey]( wxKeyEvent& aEvent )
                  {
                      int key = aEvent.GetKeyCode();

                      if( key == WXK_F10 && !aEvent.HasAnyModifiers()
                              && !( aIsHotkey && aIsHotkey( WXK_F10 ) ) )
                      {
                          aFrame->CallAfter( [aStrip]() { aStrip->OpenMenu( 0, true ); } );
                          return;
                      }

                      if( aEvent.AltDown() && !aEvent.ControlDown() && !aEvent.ShiftDown()
                              && key < 128 && wxIsalpha( key ) )
                      {
                          int index = aStrip->FindMnemonic( key );

                          if( index >= 0
                                  && !( aIsHotkey
                                        && aIsHotkey( static_cast<int>( MD_ALT )
                                                      + (int) wxToupper( key ) ) ) )
                          {
                              aFrame->CallAfter( [aStrip, index]()
                                                 {
                                                     aStrip->OpenMenu( index, true );
                                                 } );
                              return;
                          }
                      }

                      aEvent.Skip();
                  } );
}
#endif
