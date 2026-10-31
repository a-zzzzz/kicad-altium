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

#include <widgets/altium_sub_tabs.h>

#include <wx/bookctrl.h>
#include <wx/dcbuffer.h>
#include <wx/settings.h>


namespace
{
wxColour mix( const wxColour& a, const wxColour& b, double t )
{
    return wxColour( a.Red() + ( b.Red() - a.Red() ) * t, a.Green() + ( b.Green() - a.Green() ) * t,
                     a.Blue() + ( b.Blue() - a.Blue() ) * t );
}

// Same shades as the panel, document and layer tab strips
wxColour face()       { return wxSystemSettings::GetColour( wxSYS_COLOUR_BTNFACE ); }
wxColour tabFill()    { return mix( face(), *wxBLACK, 0.17 ); }
wxColour activeFill() { return mix( face(), *wxWHITE, 0.07 ); }
wxColour border()     { return mix( tabFill(), *wxBLACK, 0.40 ); }
}


ALTIUM_SUB_TABS::ALTIUM_SUB_TABS( wxWindow* aParent, std::function<void( int )> aOnSelect ) :
        wxWindow( aParent, wxID_ANY ),
        m_onSelect( std::move( aOnSelect ) )
{
    SetBackgroundStyle( wxBG_STYLE_PAINT );
    SetMinSize( wxSize( -1, GetCharHeight() + FromDIP( 12 ) ) );

    Bind( wxEVT_PAINT, [this]( wxPaintEvent& ) { onPaint(); } );

    Bind( wxEVT_LEFT_DOWN,
          [this]( wxMouseEvent& aEvent )
          {
              wxClientDC          dc( this );
              std::vector<wxRect> tabs = tabRects( dc );

              for( size_t ii = 0; ii < tabs.size(); ++ii )
              {
                  if( tabs[ii].Contains( aEvent.GetPosition() ) && (int) ii != m_active )
                  {
                      m_active = (int) ii;
                      Refresh();

                      if( m_book && (int) ii < (int) m_book->GetPageCount() )
                          m_book->SetSelection( ii );   // sends the page-changed events

                      if( m_onSelect )
                          m_onSelect( m_active );

                      return;
                  }
              }
          } );
}


void ALTIUM_SUB_TABS::SetTabs( const std::vector<wxString>& aLabels, int aActive )
{
    m_labels = aLabels;
    m_active = aActive;
    InvalidateBestSize();
    Refresh();
}


void ALTIUM_SUB_TABS::SetActive( int aActive )
{
    if( aActive != m_active )
    {
        m_active = aActive;
        Refresh();
    }
}


void ALTIUM_SUB_TABS::AttachBook( wxBookCtrlBase* aBook )
{
    m_book = aBook;
    SyncFromBook();

    // Selection changed in code (or by the book itself)
    aBook->Bind( wxEVT_BOOKCTRL_PAGE_CHANGED,
                 [this]( wxBookCtrlEvent& aEvent )
                 {
                     SyncFromBook();
                     aEvent.Skip();
                 } );
}


void ALTIUM_SUB_TABS::SyncFromBook()
{
    if( !m_book )
        return;

    std::vector<wxString> labels;

    for( size_t ii = 0; ii < m_book->GetPageCount(); ++ii )
        labels.push_back( m_book->GetPageText( ii ) );

    int sel = m_book->GetSelection();

    if( labels != m_labels || sel != m_active )
        SetTabs( labels, sel );
}


std::vector<wxRect> ALTIUM_SUB_TABS::tabRects( wxDC& aDC ) const
{
    std::vector<wxRect> rects;
    int                 x = FromDIP( 4 );

    aDC.SetFont( GetFont() );

    for( const wxString& label : m_labels )
    {
        // "&&" in a page text is a literal ampersand
        wxString text = label;
        text.Replace( wxS( "&&" ), wxS( "&" ) );

        int w = aDC.GetTextExtent( text ).x + FromDIP( 20 );
        rects.emplace_back( x, FromDIP( 2 ), w, GetClientSize().y - FromDIP( 2 ) );
        x += w - 1;   // neighbouring tabs share a border
    }

    return rects;
}


void ALTIUM_SUB_TABS::onPaint()
{
    wxAutoBufferedPaintDC dc( this );
    wxColour              bg = GetParent()->GetBackgroundColour();
    wxColour              fg = wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOWTEXT );
    wxSize                size = GetClientSize();

    dc.SetBackground( wxBrush( bg ) );
    dc.Clear();
    dc.SetFont( GetFont() );

    // Line under all the tabs
    dc.SetPen( wxPen( border() ) );
    dc.DrawLine( 0, size.y - 1, size.x, size.y - 1 );

    std::vector<wxRect> tabs = tabRects( dc );

    for( size_t ii = 0; ii < tabs.size(); ++ii )
    {
        const wxRect& r = tabs[ii];

        dc.SetPen( wxPen( border() ) );
        dc.SetBrush( wxBrush( (int) ii == m_active ? activeFill() : tabFill() ) );
        dc.DrawRectangle( r );

        wxString text = m_labels[ii];
        text.Replace( wxS( "&&" ), wxS( "&" ) );

        dc.SetTextForeground( fg );
        wxSize ext = dc.GetTextExtent( text );
        dc.DrawText( text, r.x + ( r.width - ext.x ) / 2, r.y + ( r.height - ext.y ) / 2 );
    }
}
