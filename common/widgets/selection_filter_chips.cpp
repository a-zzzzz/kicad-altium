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

#include <widgets/selection_filter_chips.h>

#include <wx/checkbox.h>
#include <wx/dcbuffer.h>
#include <wx/intl.h>
#include <wx/scrolwin.h>
#include <wx/settings.h>

#include <algorithm>


namespace
{
wxColour mix( const wxColour& a, const wxColour& b, double t )
{
    return wxColour( a.Red() + ( b.Red() - a.Red() ) * t, a.Green() + ( b.Green() - a.Green() ) * t,
                     a.Blue() + ( b.Blue() - a.Blue() ) * t );
}
}


SELECTION_FILTER_CHIPS::SELECTION_FILTER_CHIPS( wxWindow* aParent,
                                                std::function<void( wxCheckBox* )> aToggle,
                                                std::function<void( wxCheckBox*, wxMouseEvent& )> aRightClick,
                                                std::function<double( wxCheckBox* )> aFlash ) :
        wxPanel( aParent ),
        m_toggle( std::move( aToggle ) ),
        m_rightClick( std::move( aRightClick ) ),
        m_flash( std::move( aFlash ) )
{
    SetBackgroundStyle( wxBG_STYLE_PAINT );

    Bind( wxEVT_PAINT, &SELECTION_FILTER_CHIPS::onPaint, this );
    Bind( wxEVT_LEFT_DOWN, &SELECTION_FILTER_CHIPS::onLeftDown, this );
    Bind( wxEVT_LEFT_DCLICK, &SELECTION_FILTER_CHIPS::onLeftDown, this );
    Bind( wxEVT_RIGHT_DOWN,
          [this]( wxMouseEvent& aEvent )
          {
              int hit = hitTest( aEvent.GetPosition() );

              if( hit >= 0 && !m_chips[hit].allButton && m_rightClick )
                  m_rightClick( m_chips[hit].box, aEvent );
          } );
    Bind( wxEVT_MOTION,
          [this]( wxMouseEvent& aEvent )
          {
              int hit = hitTest( aEvent.GetPosition() );

              if( hit != m_hover )
              {
                  m_hover = hit;
                  SetToolTip( hit >= 0 ? m_chips[hit].box->GetToolTipText() : wxString() );
                  Refresh();
              }
          } );
    Bind( wxEVT_LEAVE_WINDOW,
          [this]( wxMouseEvent& )
          {
              m_hover = -1;
              Refresh();
          } );
    Bind( wxEVT_SIZE,
          [this]( wxSizeEvent& aEvent )
          {
              updateHeight();
              Refresh();
              aEvent.Skip();
          } );
}


void SELECTION_FILTER_CHIPS::SetChips( std::vector<CHIP> aChips )
{
    m_chips = std::move( aChips );
    m_height = -1;
    updateHeight();
    Refresh();
}


wxSize SELECTION_FILTER_CHIPS::DoGetBestClientSize() const
{
    int width = GetClientSize().x > 0 ? GetClientSize().x : FromDIP( 260 );
    return wxSize( minWidth(), layout( std::max( width, minWidth() ) ).second );
}


int SELECTION_FILTER_CHIPS::chipHeight() const
{
    return GetCharHeight() + FromDIP( 10 );
}


wxString SELECTION_FILTER_CHIPS::labelOf( const CHIP& aChip ) const
{
    if( aChip.allButton )
        return aChip.box->GetValue() ? _( "All - On" ) : _( "All - Off" );

    return aChip.label;
}


int SELECTION_FILTER_CHIPS::chipWidth( const CHIP& aChip ) const
{
    int w = GetTextExtent( labelOf( aChip ) ).x + FromDIP( 20 );
    return aChip.allButton ? std::max( w, FromDIP( 90 ) ) : w;
}


int SELECTION_FILTER_CHIPS::minWidth() const
{
    int w = 0;

    for( const CHIP& chip : m_chips )
        w = std::max( w, chipWidth( chip ) );

    return w + 2 * FromDIP( 6 );
}


std::pair<std::vector<wxRect>, int> SELECTION_FILTER_CHIPS::layout( int aWidth ) const
{
    std::vector<wxRect> rects;
    int                 pad = m_drawBlock ? FromDIP( 6 ) : 0;
    int                 gap = FromDIP( 4 );
    int                 x = pad;
    int                 y = pad;
    int                 h = chipHeight();

    for( const CHIP& chip : m_chips )
    {
        int w = chipWidth( chip );

        if( x > pad && x + w > aWidth - pad )
        {
            x = pad;
            y += h + gap;
        }

        rects.emplace_back( x, y, w, h );

        if( chip.allButton || chip.rowBreak )
        {
            x = pad;
            y += h + gap;
        }
        else
        {
            x += w + gap;
        }
    }

    int bottom = rects.empty() ? 0 : rects.back().GetBottom() + 1;
    return { rects, bottom + pad };
}


void SELECTION_FILTER_CHIPS::updateHeight()
{
    int height = layout( std::max( GetClientSize().x, minWidth() ) ).second;

    if( height == m_height )
        return;

    // A re-layout can change our width (e.g. a scrollbar coming and going), which can change
    // the height again: never let that ping-pong.  Keep the taller of two alternating heights.
    if( height == m_prevHeight )
    {
        height = std::max( height, m_height );

        if( height == m_height )
            return;
    }

    m_prevHeight = m_height;
    m_height = height;
    SetMinSize( wxSize( minWidth(), height ) );
    InvalidateBestSize();

    if( m_layoutPending )
        return;

    m_layoutPending = true;

    // The buttons usually sit in a panel inside the Properties panel: let both re-layout
    CallAfter(
            [this]()
            {
                m_layoutPending = false;

                for( wxWindow* w = GetParent(); w && !w->IsTopLevel(); w = w->GetParent() )
                {
                    w->InvalidateBestSize();

                    if( wxScrolledWindow* scroll = dynamic_cast<wxScrolledWindow*>( w ) )
                        scroll->FitInside();

                    w->Layout();
                    w->Refresh();

                    if( w->GetParent() && w->GetParent()->IsTopLevel() )
                        break;
                }
            } );
}


int SELECTION_FILTER_CHIPS::hitTest( const wxPoint& aPos ) const
{
    std::vector<wxRect> rects = layout( GetClientSize().x ).first;

    for( size_t ii = 0; ii < rects.size(); ++ii )
    {
        if( rects[ii].Contains( aPos ) )
            return (int) ii;
    }

    return -1;
}


void SELECTION_FILTER_CHIPS::onLeftDown( wxMouseEvent& aEvent )
{
    int hit = hitTest( aEvent.GetPosition() );

    if( hit >= 0 )
    {
        m_toggle( m_chips[hit].box );
        Refresh();
    }
}


void SELECTION_FILTER_CHIPS::onPaint( wxPaintEvent& aEvent )
{
    wxAutoBufferedPaintDC dc( this );
    wxColour              bg = GetParent()->GetBackgroundColour();
    wxColour              fg = wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOWTEXT );

    dc.SetBackground( wxBrush( bg ) );
    dc.Clear();

    // Altium draws a section's controls on a slightly lighter, rounded panel
    if( m_drawBlock )
    {
        dc.SetPen( *wxTRANSPARENT_PEN );
        dc.SetBrush( wxBrush( mix( bg, fg, 0.06 ) ) );
        dc.DrawRoundedRectangle( wxRect( wxPoint( 0, 0 ), GetClientSize() ), FromDIP( 3 ) );
    }

    dc.SetFont( GetFont() );

    std::vector<wxRect> rects = layout( GetClientSize().x ).first;
    const wxColour      onFill( 0x5A, 0x7C, 0xA0 );     // steel blue, as Altium's "on" buttons

    for( size_t ii = 0; ii < rects.size() && ii < m_chips.size(); ++ii )
    {
        const CHIP& chip = m_chips[ii];
        bool        on = chip.box->GetValue() && !chip.allButton;
        wxColour    fill = on ? onFill : mix( bg, fg, chip.allButton ? 0.14 : 0.10 );
        wxColour    text = on ? wxColour( 0xF2, 0xF2, 0xF2 ) : fg;

        if( (int) ii == m_hover )
            fill = mix( fill, *wxWHITE, 0.12 );

        if( double flash = m_flash ? m_flash( chip.box ) : 0.0; flash > 0 )
            fill = mix( fill, wxSystemSettings::GetColour( wxSYS_COLOUR_HIGHLIGHT ), flash );

        dc.SetPen( wxPen( mix( fill, *wxBLACK, 0.55 ) ) );
        dc.SetBrush( wxBrush( fill ) );
        dc.DrawRoundedRectangle( rects[ii], FromDIP( 3 ) );

        wxString label = labelOf( chip );
        wxSize   ext = dc.GetTextExtent( label );

        dc.SetTextForeground( text );
        dc.DrawText( label, rects[ii].x + ( rects[ii].width - ext.x ) / 2,
                     rects[ii].y + ( rects[ii].height - ext.y ) / 2 );
    }
}
