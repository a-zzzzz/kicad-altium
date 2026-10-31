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
#include <widgets/document_tab_bar.h>

#include <bitmaps.h>
#include <document_windows.h>
#include <eda_base_frame.h>

#include <wx/dcbuffer.h>
#include <wx/filename.h>
#include <wx/frame.h>
#include <wx/menu.h>

#include <algorithm>
#include <wx/settings.h>


namespace
{
const int BAR_HEIGHT = 26;   // DIP
const int TAB_PAD_X  = 12;   // DIP
const int CLOSE_SIZE = 14;   // DIP

wxColour mixColour( const wxColour& a, const wxColour& b, double t )
{
    return wxColour( a.Red() + ( b.Red() - a.Red() ) * t, a.Green() + ( b.Green() - a.Green() ) * t,
                     a.Blue() + ( b.Blue() - a.Blue() ) * t );
}

// Altium's document tabs: a dark bar, darker closed tabs, the open document a lighter grey
wxColour face()        { return wxSystemSettings::GetColour( wxSYS_COLOUR_BTNFACE ); }
wxColour barBg()       { return mixColour( face(), *wxBLACK, 0.17 ); }
// Altium: background tabs share the strip colour; only the open one is lighter
wxColour tabFill()     { return barBg(); }
wxColour tabActive()   { return mixColour( face(), *wxWHITE, 0.07 ); }
wxColour tabText()     { return wxSystemSettings::GetColour( wxSYS_COLOUR_BTNTEXT ); }
wxColour tabBorder()   { return mixColour( barBg(), *wxBLACK, 0.40 ); }
}


DOCUMENT_TAB_BAR::DOCUMENT_TAB_BAR( wxFrame* aOwner ) :
        wxPanel( aOwner, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE ),
        m_owner( aOwner ),
        m_hoverClose( -1 )
{
    SetBackgroundStyle( wxBG_STYLE_PAINT );
    SetMinSize( wxSize( -1, FromDIP( BAR_HEIGHT ) ) );

    // Moved to a monitor with another scale: keep the bar's size in DIPs
    Bind( wxEVT_DPI_CHANGED,
          [this]( wxDPIChangedEvent& aEvent )
          {
              aEvent.Skip();
              CallAfter( [this]() { RefitAuiBar( this, -1, FromDIP( BAR_HEIGHT ) ); Refresh(); } );
          } );

    Bind( wxEVT_PAINT, &DOCUMENT_TAB_BAR::onPaint, this );

    // Whether same-kind documents share a tab depends on the width
    Bind( wxEVT_SIZE,
          [this]( wxSizeEvent& aEvent )
          {
              Refresh();
              aEvent.Skip();
          } );
    Bind( wxEVT_LEFT_DOWN, &DOCUMENT_TAB_BAR::onLeftDown, this );
    Bind( wxEVT_MOTION, &DOCUMENT_TAB_BAR::onMotion, this );
    Bind( wxEVT_LEAVE_WINDOW,
          [this]( wxMouseEvent& aEvent )
          {
              if( m_hoverClose != -1 )
              {
                  m_hoverClose = -1;
                  Refresh();
              }

              aEvent.Skip();
          } );

    // Other windows opening, closing or renaming their documents don't notify us; check on idle
    aOwner->Bind( wxEVT_IDLE, &DOCUMENT_TAB_BAR::onIdle, this );

    // A window with document tabs is one of the documents of the single main window
    DOCUMENT_WINDOWS::Register( aOwner );
}


std::vector<DOCUMENT_TAB_BAR::TAB> DOCUMENT_TAB_BAR::currentTabs() const
{
    std::vector<TAB> tabs;

    // Open documents (shown or parked behind the current one), then other KiCad windows
    for( wxTopLevelWindow* frame : DOCUMENT_WINDOWS::TabWindows() )
    {
        wxString label = frame->GetTitle();

        // Label with the document's file name, like Altium's tabs
        if( EDA_BASE_FRAME* edaFrame = dynamic_cast<EDA_BASE_FRAME*>( frame ) )
        {
            wxString fileName = edaFrame->GetCurrentFileName();

            if( !fileName.IsEmpty() )
            {
                label = wxFileName( fileName ).GetFullName();

                // Unsaved changes: Altium marks the tab "Board.PcbDoc *"
                if( edaFrame->IsContentModified() )
                    label += wxS( " *" );
            }
            else if( edaFrame->IsType( FRAME_FOOTPRINT_EDITOR )
                     || edaFrame->IsType( FRAME_SCH_SYMBOL_EDITOR ) )
            {
                // Library editors have no file of their own: label the tab with the library
                // document, as Altium does ("lib:item [Read Only] — Footprint Editor").
                bool     fpEditor = edaFrame->IsType( FRAME_FOOTPRINT_EDITOR );
                wxString item = label.BeforeFirst( wxUniChar( 0x2014 ) ).Trim();
                wxString modified;

                if( item.StartsWith( wxS( "*" ) ) )
                {
                    modified = wxS( "*" );
                    item = item.Mid( 1 );
                }

                if( item.Contains( wxS( ":" ) ) && !item.StartsWith( wxS( "[" ) ) )
                {
                    label = modified + item.BeforeFirst( ':' )
                            + ( fpEditor ? wxS( ".pretty" ) : wxS( ".kicad_sym" ) );
                }
                else
                {
                    label = fpEditor ? _( "Footprint Editor" ) : _( "Symbol Editor" );
                }
            }
        }

        tabs.push_back( { frame, label, {}, {} } );
    }

    return tabs;
}


void DOCUMENT_TAB_BAR::onIdle( wxIdleEvent& aEvent )
{
    aEvent.Skip();

    std::vector<TAB> tabs = currentTabs();
    bool             changed = tabs.size() != m_tabs.size();

    for( size_t ii = 0; !changed && ii < tabs.size(); ++ii )
        changed = tabs[ii].window != m_tabs[ii].window || tabs[ii].label != m_tabs[ii].label;

    if( changed )
    {
        m_tabs = std::move( tabs );
        Refresh();
    }
}


/// Altium-style document-kind icon for a tab's window.
static BITMAPS documentIcon( wxTopLevelWindow* aWindow )
{
    if( EDA_BASE_FRAME* frame = dynamic_cast<EDA_BASE_FRAME*>( aWindow ) )
    {
        if( frame->IsType( FRAME_PCB_EDITOR ) )
            return BITMAPS::doc_pcb;
        else if( frame->IsType( FRAME_SCH ) )
            return BITMAPS::doc_schematic;
        else if( frame->IsType( FRAME_FOOTPRINT_EDITOR ) )
            return BITMAPS::doc_pcb_lib;
        else if( frame->IsType( FRAME_SCH_SYMBOL_EDITOR ) )
            return BITMAPS::doc_sch_lib;
    }

    return BITMAPS::INVALID_BITMAP;
}


/// The kind of document a tab shows, for grouping ("PCB Library Document"); empty: not grouped.
static wxString documentKind( wxTopLevelWindow* aWindow )
{
    if( EDA_BASE_FRAME* frame = dynamic_cast<EDA_BASE_FRAME*>( aWindow ) )
    {
        if( frame->IsType( FRAME_PCB_EDITOR ) )
            return _( "PCB Document" );
        else if( frame->IsType( FRAME_SCH ) )
            return _( "Schematic Document" );
        else if( frame->IsType( FRAME_FOOTPRINT_EDITOR ) )
            return _( "PCB Library Document" );
        else if( frame->IsType( FRAME_SCH_SYMBOL_EDITOR ) )
            return _( "Schematic Library Document" );
        else if( frame->IsType( FRAME_GERBER ) )
            return _( "CAM Document" );
    }

    return wxEmptyString;
}


void DOCUMENT_TAB_BAR::layoutEntries( wxDC& aDC )
{
    int h = GetClientSize().y;
    int pad = FromDIP( TAB_PAD_X );
    int closeSize = FromDIP( CLOSE_SIZE );
    int arrowSize = FromDIP( 12 );

    aDC.SetFont( GetFont() );

    auto entryWidth =
            [&]( const ENTRY& aEntry )
            {
                int iconW = FromDIP( 16 ) + pad / 2;
                int w = pad + iconW + aDC.GetTextExtent( aEntry.label ).x + pad / 2 + closeSize
                        + pad / 2;

                return aEntry.group ? w + arrowSize : w;
            };

    // One tab per document, as long as they fit
    std::vector<ENTRY> entries;

    for( size_t ii = 0; ii < m_tabs.size(); ++ii )
    {
        ENTRY entry;
        entry.members.push_back( (int) ii );
        entry.label = m_tabs[ii].label;
        entry.active = m_tabs[ii].window == m_owner;
        entries.push_back( entry );
    }

    int total = FromDIP( 4 );

    for( const ENTRY& entry : entries )
        total += entryWidth( entry ) + FromDIP( 2 );

    // Too many: documents of one kind share a tab, at the place of the first of them
    if( total > GetClientSize().x )
    {
        std::vector<ENTRY> grouped;

        for( size_t ii = 0; ii < m_tabs.size(); ++ii )
        {
            wxString kind = documentKind( m_tabs[ii].window );
            ENTRY*   into = nullptr;

            if( !kind.IsEmpty() )
            {
                for( ENTRY& entry : grouped )
                {
                    if( documentKind( m_tabs[entry.members.front()].window ) == kind )
                        into = &entry;
                }
            }

            if( into )
            {
                into->members.push_back( (int) ii );
            }
            else
            {
                ENTRY entry;
                entry.members.push_back( (int) ii );
                grouped.push_back( entry );
            }
        }

        for( ENTRY& entry : grouped )
        {
            int shown = entry.members.front();

            for( int member : entry.members )
            {
                if( m_tabs[member].window == m_owner )
                {
                    entry.active = true;
                    shown = member;
                }
            }

            if( entry.members.size() > 1 )
            {
                entry.group = true;

                // "(3) PCB Library Document", or the open one's name when it is in the group
                entry.label = wxString::Format( wxS( "(%d) %s" ), (int) entry.members.size(),
                                                entry.active
                                                        ? m_tabs[shown].label
                                                        : documentKind( m_tabs[shown].window ) );

                // The group's icon and switching follow the document it names
                std::swap( *std::find( entry.members.begin(), entry.members.end(), shown ),
                           entry.members.front() );
            }
            else
            {
                entry.label = m_tabs[shown].label;
            }
        }

        entries = std::move( grouped );
    }

    int x = FromDIP( 4 );

    for( ENTRY& entry : entries )
    {
        int w = entryWidth( entry );

        entry.rect = wxRect( x, FromDIP( 2 ), w, h - FromDIP( 2 ) );
        entry.closeRect = wxRect( entry.rect.GetRight() - pad / 2 - closeSize,
                                  entry.rect.y + ( entry.rect.height - closeSize ) / 2,
                                  closeSize, closeSize );

        if( entry.group )
        {
            // The drop-down arrow sits where the close box is; the close box moves left
            entry.arrowRect = entry.closeRect;
            entry.closeRect.x -= arrowSize + FromDIP( 2 );
        }

        x = entry.rect.GetRight() + FromDIP( 2 );
    }

    m_entries = std::move( entries );
}


void DOCUMENT_TAB_BAR::onPaint( wxPaintEvent& aEvent )
{
    wxAutoBufferedPaintDC dc( this );
    dc.SetBackground( wxBrush( barBg() ) );
    dc.Clear();

    layoutEntries( dc );

    int pad = FromDIP( TAB_PAD_X );
    int closeSize = FromDIP( CLOSE_SIZE );

    for( size_t ii = 0; ii < m_entries.size(); ++ii )
    {
        const ENTRY& entry = m_entries[ii];
        const TAB&   tab = m_tabs[entry.members.front()];

        dc.SetFont( GetFont() );

        wxSize   text = dc.GetTextExtent( entry.label );
        BITMAPS  iconId = documentIcon( tab.window );
        wxBitmap icon;

        if( iconId != BITMAPS::INVALID_BITMAP )
            icon = KiBitmapBundleDef( iconId, 16 ).GetBitmapFor( this );

        int iconW = FromDIP( 16 ) + pad / 2;

        // A box with rounded top corners; the bottom runs into the document below
        int    radius = FromDIP( 3 );
        wxRect box( entry.rect.x, entry.rect.y, entry.rect.width, entry.rect.height + radius + 1 );

        dc.SetClippingRegion( entry.rect );
        dc.SetPen( wxPen( tabBorder() ) );
        dc.SetBrush( wxBrush( entry.active ? tabActive() : tabFill() ) );
        dc.DrawRoundedRectangle( box, radius );
        dc.DestroyClippingRegion();

        if( icon.IsOk() )
        {
            dc.DrawBitmap( icon, entry.rect.x + pad,
                           entry.rect.y + ( entry.rect.height - icon.GetLogicalHeight() ) / 2,
                           true );
        }

        dc.SetTextForeground( tabText() );
        dc.DrawText( entry.label, entry.rect.x + pad + iconW,
                     entry.rect.y + ( entry.rect.height - text.y ) / 2 );

        if( entry.group )
        {
            // A filled down arrow, like Altium's group button
            const wxRect& a = entry.arrowRect;
            int           s = a.width / 3;
            wxPoint       tri[3] = { wxPoint( a.x + a.width / 2 - s, a.y + a.height / 2 - s / 2 ),
                                     wxPoint( a.x + a.width / 2 + s, a.y + a.height / 2 - s / 2 ),
                                     wxPoint( a.x + a.width / 2, a.y + a.height / 2 + s / 2 + 1 ) };

            dc.SetPen( *wxTRANSPARENT_PEN );
            dc.SetBrush( wxBrush( tabText() ) );
            dc.DrawPolygon( 3, tri );
        }

        // Close box: an x on the open document's tab (or under the pointer), as in Altium
        if( !entry.active && (int) ii != m_hoverClose )
            continue;

        if( (int) ii == m_hoverClose )
        {
            dc.SetPen( *wxTRANSPARENT_PEN );
            dc.SetBrush( wxBrush( tabBorder() ) );
            dc.DrawRoundedRectangle( entry.closeRect, FromDIP( 2 ) );
        }

        int inset = closeSize / 4;
        dc.SetPen( wxPen( tabText(), FromDIP( 1 ) ) );
        dc.DrawLine( entry.closeRect.x + inset, entry.closeRect.y + inset,
                     entry.closeRect.GetRight() - inset + 1, entry.closeRect.GetBottom() - inset + 1 );
        dc.DrawLine( entry.closeRect.GetRight() - inset, entry.closeRect.y + inset,
                     entry.closeRect.x + inset - 1, entry.closeRect.GetBottom() - inset + 1 );
    }
}


void DOCUMENT_TAB_BAR::showGroupMenu( const ENTRY& aEntry )
{
    wxMenu menu;

    for( size_t ii = 0; ii < aEntry.members.size(); ++ii )
    {
        const TAB&  tab = m_tabs[aEntry.members[ii]];
        wxMenuItem* item = new wxMenuItem( &menu, wxID_HIGHEST + 1 + (int) ii, tab.label );
        BITMAPS     icon = documentIcon( tab.window );

        if( icon != BITMAPS::INVALID_BITMAP )
            item->SetBitmap( KiBitmapBundleDef( icon, 16 ) );

        menu.Append( item );
    }

    int picked = GetPopupMenuSelectionFromUser(
            menu, wxPoint( aEntry.rect.x, aEntry.rect.GetBottom() ) ) - wxID_HIGHEST - 1;

    if( picked >= 0 && picked < (int) aEntry.members.size() )
    {
        wxTopLevelWindow* window = m_tabs[aEntry.members[picked]].window;

        if( window != m_owner )
            switchTo( window );
    }
}


void DOCUMENT_TAB_BAR::onMotion( wxMouseEvent& aEvent )
{
    int hover = -1;

    for( size_t ii = 0; ii < m_entries.size(); ++ii )
    {
        if( m_entries[ii].closeRect.Contains( aEvent.GetPosition() ) )
            hover = (int) ii;
    }

    if( hover != m_hoverClose )
    {
        m_hoverClose = hover;
        Refresh();
    }

    aEvent.Skip();
}


void DOCUMENT_TAB_BAR::onLeftDown( wxMouseEvent& aEvent )
{
    for( const ENTRY& entry : m_entries )
    {
        const TAB& tab = m_tabs[entry.members.front()];

        if( entry.group && entry.arrowRect.Contains( aEvent.GetPosition() ) )
        {
            showGroupMenu( entry );
            return;
        }

        if( entry.closeRect.Contains( aEvent.GetPosition() ) )
        {
            tab.window->Close();
            return;
        }

        if( entry.rect.Contains( aEvent.GetPosition() ) )
        {
            // A group not holding the open document lists its documents, as in Altium
            if( entry.group && !entry.active )
                showGroupMenu( entry );
            else if( tab.window != m_owner )
                switchTo( tab.window );

            return;
        }
    }
}


void DOCUMENT_TAB_BAR::switchTo( wxTopLevelWindow* aWindow )
{
    // The other document takes this window's place and this one is parked behind it
    DOCUMENT_WINDOWS::SwitchTo( aWindow );
}
