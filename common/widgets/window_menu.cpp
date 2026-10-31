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

#include <widgets/window_menu.h>

#include <document_windows.h>

#include <algorithm>
#include <cmath>

#include <wx/display.h>
#include <wx/frame.h>
#include <wx/intl.h>
#include <wx/toplevel.h>
#include <wx/windowid.h>


namespace
{
// Fixed items first, then one item per open window
enum ITEM_OFFSET
{
    TILE = 0,
    TILE_HORIZONTALLY,
    TILE_VERTICALLY,
    CLOSE_WINDOW,
    CLOSE_ALL,
    FIRST_WINDOW
};

const int MAX_LISTED_WINDOWS = 32;
const int ID_COUNT = FIRST_WINDOW + MAX_LISTED_WINDOWS;
}


WINDOW_MENU::WINDOW_MENU( wxFrame* aOwner ) :
        wxMenu(),
        m_owner( aOwner ),
        m_firstId( wxIdManager::ReserveId( ID_COUNT ) )
{
    Bind( wxEVT_MENU_OPEN, &WINDOW_MENU::onMenuOpen, this );
    Bind( wxEVT_MENU, &WINDOW_MENU::onCommand, this, m_firstId, m_firstId + ID_COUNT - 1 );

    rebuild();
}


WINDOW_MENU::~WINDOW_MENU()
{
    wxIdManager::UnreserveId( m_firstId, ID_COUNT );
}


std::vector<wxTopLevelWindow*> WINDOW_MENU::openWindows()
{
    // The same list as the document tabs: open documents (also those parked behind the
    // current one), then other KiCad windows on screen
    return DOCUMENT_WINDOWS::TabWindows();
}


void WINDOW_MENU::rebuild()
{
    while( GetMenuItemCount() > 0 )
        Destroy( FindItemByPosition( 0 ) );

    m_listed = openWindows();

    if( m_listed.size() > MAX_LISTED_WINDOWS )
        m_listed.resize( MAX_LISTED_WINDOWS );

    bool canArrange = m_listed.size() > 1;

    // Altium's Window menu: arrangements, closing, then the open documents
    Append( m_firstId + TILE, _( "&Tile" ),
            _( "Arrange the open editor windows in a grid" ) )->Enable( canArrange );
    Append( m_firstId + TILE_HORIZONTALLY, _( "Tile &Horizontally" ),
            _( "Stack the open editor windows top to bottom" ) )->Enable( canArrange );
    Append( m_firstId + TILE_VERTICALLY, _( "Tile &Vertically" ),
            _( "Place the open editor windows side by side" ) )->Enable( canArrange );

    AppendSeparator();
    Append( m_firstId + CLOSE_WINDOW, _( "Close &Documents" ), _( "Close the current document" ) );
    Append( m_firstId + CLOSE_ALL, _( "Close A&ll" ), _( "Close every open document" ) );

    AppendSeparator();

    for( size_t ii = 0; ii < m_listed.size(); ++ii )
    {
        wxString label = m_listed[ii]->GetTitle();

        // Escape mnemonics so a '&' in a file name isn't swallowed
        label.Replace( wxS( "&" ), wxS( "&&" ) );

        if( ii < 9 )
            label = wxString::Format( wxS( "&%d %s" ), int( ii + 1 ), label );

        wxMenuItem* item = AppendCheckItem( m_firstId + FIRST_WINDOW + int( ii ), label );
        item->Check( m_listed[ii] == m_owner );
    }
}


void WINDOW_MENU::onMenuOpen( wxMenuEvent& aEvent )
{
    if( aEvent.GetMenu() == this )
        rebuild();

    aEvent.Skip();
}


void WINDOW_MENU::onCommand( wxCommandEvent& aEvent )
{
    int offset = aEvent.GetId() - m_firstId;

    switch( offset )
    {
    case TILE:              Arrange( m_owner, ARRANGE::GRID );       return;
    case TILE_HORIZONTALLY: Arrange( m_owner, ARRANGE::HORIZONTAL ); return;
    case TILE_VERTICALLY:   Arrange( m_owner, ARRANGE::VERTICAL );   return;
    case CLOSE_WINDOW:      m_owner->Close();                        return;
    case CLOSE_ALL:         DOCUMENT_WINDOWS::CloseAll();            return;
    default:                break;
    }

    size_t idx = size_t( offset - FIRST_WINDOW );

    // The window may have closed since the menu was built
    std::vector<wxTopLevelWindow*> current = openWindows();

    if( idx < m_listed.size()
        && std::find( current.begin(), current.end(), m_listed[idx] ) != current.end() )
    {
        DOCUMENT_WINDOWS::SwitchTo( m_listed[idx] );
    }
}


void WINDOW_MENU::Arrange( wxFrame* aOwner, ARRANGE aMode )
{
    // Tiling shows every document at once; switching tabs gathers them into one again
    DOCUMENT_WINDOWS::ShowAll();

    std::vector<wxTopLevelWindow*> windows = openWindows();

    if( windows.size() < 2 )
        return;

    int    displayIdx = wxDisplay::GetFromWindow( aOwner );
    wxRect area = wxDisplay( displayIdx == wxNOT_FOUND ? 0 : displayIdx ).GetClientArea();
    int    count = int( windows.size() );

    // Grid: as square as possible, columns first
    int cols = std::max( 1, int( std::ceil( std::sqrt( double( count ) ) ) ) );
    int rows = ( count + cols - 1 ) / cols;

    for( int ii = 0; ii < count; ++ii )
    {
        wxTopLevelWindow* win = windows[ii];
        wxRect            rect;

        switch( aMode )
        {
        case ARRANGE::GRID:
        {
            int w = area.width / cols;
            int h = area.height / rows;
            rect = wxRect( area.x + ( ii % cols ) * w, area.y + ( ii / cols ) * h, w, h );
            break;
        }

        case ARRANGE::HORIZONTAL:
        {
            int h = area.height / count;
            rect = wxRect( area.x, area.y + ii * h, area.width, h );
            break;
        }

        case ARRANGE::VERTICAL:
        {
            int w = area.width / count;
            rect = wxRect( area.x + ii * w, area.y, w, area.height );
            break;
        }

        case ARRANGE::CASCADE:
        {
            int step = aOwner->FromDIP( 32 );
            rect = wxRect( area.x + ii * step, area.y + ii * step,
                           area.width * 3 / 4, area.height * 3 / 4 );
            break;
        }
        }

        if( win->IsIconized() )
            win->Iconize( false );

        if( win->IsMaximized() )
            win->Maximize( false );

        win->SetSize( rect );
        win->Raise();
    }

    // Leave the window the command came from in front
    aOwner->Raise();
}
