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

#include <widgets/altium_layer_tabs.h>

#include <board.h>
#include <lset.h>
#include <pcb_base_edit_frame.h>
#include <widgets/appearance_controls.h>
#include <settings/color_settings.h>

#include <algorithm>

#include <wx/dcbuffer.h>
#include <wx/menu.h>
#include <wx/settings.h>


namespace
{
const int TAB_PAD_X   = 8;   // DIP; padding either side of swatch + label
const int SWATCH_SIZE = 9;   // DIP
const int TAB_GAP     = 1;   // DIP
const int BAR_HEIGHT  = 22;  // DIP

// Follow the system theme so the bar matches the panels around it in light and dark mode
wxColour mix( const wxColour& a, const wxColour& b, double t )
{
    return wxColour( a.Red() + ( b.Red() - a.Red() ) * t, a.Green() + ( b.Green() - a.Green() ) * t,
                     a.Blue() + ( b.Blue() - a.Blue() ) * t );
}

// Altium: other layers' tabs share the strip colour, the current layer's tab is lighter, and
// the LS button lighter still (as the panel and document tab strips)
wxColour face()        { return wxSystemSettings::GetColour( wxSYS_COLOUR_BTNFACE ); }
wxColour barBg()       { return mix( face(), *wxBLACK, 0.17 ); }
wxColour tabBg()       { return barBg(); }
wxColour tabActiveBg() { return mix( face(), *wxWHITE, 0.07 ); }
wxColour buttonBg()    { return mix( face(), *wxWHITE, 0.13 ); }
wxColour tabBorder()   { return mix( barBg(), *wxBLACK, 0.40 ); }
wxColour tabText()     { return wxSystemSettings::GetColour( wxSYS_COLOUR_BTNTEXT ); }
}


ALTIUM_LAYER_TABS::ALTIUM_LAYER_TABS( PCB_BASE_FRAME* aFrame ) :
        wxPanel( aFrame, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE ),
        m_frame( aFrame ),
        m_scrollOffset( 0 ),
        m_paintedActive( UNDEFINED_LAYER )
{
    SetBackgroundStyle( wxBG_STYLE_PAINT );
    SetMinSize( wxSize( -1, FromDIP( BAR_HEIGHT ) ) );

    Bind( wxEVT_PAINT, &ALTIUM_LAYER_TABS::onPaint, this );
    Bind( wxEVT_LEFT_DOWN, &ALTIUM_LAYER_TABS::onLeftDown, this );
    Bind( wxEVT_MOUSEWHEEL, &ALTIUM_LAYER_TABS::onMouseWheel, this );
    Bind( wxEVT_SIZE, &ALTIUM_LAYER_TABS::onSize, this );
    Bind( wxEVT_UPDATE_UI, &ALTIUM_LAYER_TABS::onUpdateUI, this );
}


wxString ALTIUM_LAYER_TABS::DisplayName( PCB_LAYER_ID aLayer, const wxString& aBoardName )
{
    // Only translate layers the user hasn't renamed.  Boards store either the canonical
    // name (F.SilkS) or the display name (F.Silkscreen) for untouched layers.
    if( aBoardName != LSET::Name( aLayer ) && aBoardName != BOARD::GetStandardLayerName( aLayer ) )
        return aBoardName;

    if( IsInnerCopperLayer( aLayer ) )
        return wxString::Format( wxS( "Mid-Layer %d" ), ( aLayer - In1_Cu ) / 2 + 1 );

    switch( aLayer )
    {
    case F_Cu:      return wxS( "Top Layer" );
    case B_Cu:      return wxS( "Bottom Layer" );
    case F_SilkS:   return wxS( "Top Overlay" );
    case B_SilkS:   return wxS( "Bottom Overlay" );
    case F_Paste:   return wxS( "Top Paste" );
    case B_Paste:   return wxS( "Bottom Paste" );
    case F_Mask:    return wxS( "Top Solder" );
    case B_Mask:    return wxS( "Bottom Solder" );
    case Edge_Cuts: return wxS( "Board Outline" );
    case F_CrtYd:   return wxS( "Top Courtyard" );
    case B_CrtYd:   return wxS( "Bottom Courtyard" );
    case F_Fab:     return wxS( "Top Assembly" );
    case B_Fab:     return wxS( "Bottom Assembly" );
    case F_Adhes:   return wxS( "Top Glue" );
    case B_Adhes:   return wxS( "Bottom Glue" );
    case Dwgs_User: return wxS( "Mechanical 1" );
    case Cmts_User: return wxS( "Mechanical 2" );
    case Eco1_User: return wxS( "Mechanical 3" );
    case Eco2_User: return wxS( "Mechanical 4" );
    case Margin:    return wxS( "Keep-Out Margin" );
    default:        return aBoardName;
    }
}


void ALTIUM_LAYER_TABS::OnBoardChanged()
{
    m_tabs.clear();
    m_shownLayers.reset();

    BOARD* board = m_frame->GetBoard();

    if( board )
    {
        int copperIndex = 0;

        for( PCB_LAYER_ID layer : board->GetEnabledLayers().UIOrder() )
        {
            // Copper layers are numbered in stackup order, visible or not, as in Altium
            wxString label = DisplayName( layer, board->GetLayerName( layer ) );

            if( IsCopperLayer( layer ) )
                label = wxString::Format( wxS( "[%d] %s" ), ++copperIndex, label );

            if( !board->IsLayerVisible( layer ) )
                continue;

            m_shownLayers.set( layer );
            m_tabs.push_back( { layer, label, {} } );
        }
    }

    m_scrollOffset = 0;
    Refresh();
}


void ALTIUM_LAYER_TABS::OnLayerChanged()
{
    Refresh();
}


void ALTIUM_LAYER_TABS::layoutTabs( wxDC& aDC )
{
    int h = GetClientSize().y;

    // Fixed controls at the far left, as in Altium: active layer colour, then "LS"
    m_swatchRect = wxRect( FromDIP( 4 ), FromDIP( 4 ), FromDIP( 22 ), h - FromDIP( 7 ) );
    m_layerSetRect = wxRect( m_swatchRect.GetRight() + FromDIP( 3 ), FromDIP( 2 ),
                             aDC.GetTextExtent( wxS( "LS" ) ).x + FromDIP( TAB_PAD_X ) * 2,
                             h - FromDIP( 2 ) );

    int x = m_layerSetRect.GetRight() + FromDIP( 6 ) - m_scrollOffset;

    for( TAB& tab : m_tabs )
    {
        int w = FromDIP( TAB_PAD_X ) * 3 + FromDIP( SWATCH_SIZE )
                + aDC.GetTextExtent( tab.label ).x;
        tab.rect = wxRect( x, FromDIP( 2 ), w, h - FromDIP( 2 ) );
        x += w + FromDIP( TAB_GAP );
    }
}


int ALTIUM_LAYER_TABS::contentWidth() const
{
    if( m_tabs.empty() )
        return 0;

    return m_tabs.back().rect.GetRight() + m_scrollOffset + FromDIP( 4 );
}


void ALTIUM_LAYER_TABS::onPaint( wxPaintEvent& aEvent )
{
    wxAutoBufferedPaintDC dc( this );
    dc.SetFont( GetFont() );

    dc.SetBackground( wxBrush( barBg() ) );
    dc.Clear();

    layoutTabs( dc );

    COLOR_SETTINGS* colors = m_frame->GetColorSettings();
    PCB_LAYER_ID    active = m_frame->GetActiveLayer();

    m_paintedActive = active;

    for( const TAB& tab : m_tabs )
    {
        bool isActive = tab.layer == active;

        dc.SetPen( wxPen( tabBorder() ) );
        dc.SetBrush( wxBrush( isActive ? tabActiveBg() : tabBg() ) );

        // Active tab sits flush with the canvas above it; others are inset.
        wxRect r = tab.rect;

        if( !isActive )
            r.Deflate( 0, FromDIP( 1 ) );

        dc.DrawRectangle( r );

        wxColour swatch = colors->GetColor( tab.layer ).WithAlpha( 1.0 ).ToColour();
        int      sz = FromDIP( SWATCH_SIZE );
        int      sy = r.y + ( r.height - sz ) / 2;

        dc.SetPen( wxPen( tabBorder() ) );
        dc.SetBrush( wxBrush( swatch ) );
        dc.DrawRectangle( r.x + FromDIP( TAB_PAD_X ), sy, sz, sz );

        wxFont font = GetFont();

        if( isActive )
            font.MakeBold();

        dc.SetFont( font );
        dc.SetTextForeground( tabText() );

        int ty = r.y + ( r.height - dc.GetTextExtent( tab.label ).y ) / 2;
        dc.DrawText( tab.label, r.x + FromDIP( TAB_PAD_X ) * 2 + sz, ty );
        dc.SetFont( GetFont() );
    }

    // Fixed controls drawn over the scrolled tabs
    dc.SetPen( *wxTRANSPARENT_PEN );
    dc.SetBrush( wxBrush( barBg() ) );
    dc.DrawRectangle( 0, 0, m_layerSetRect.GetRight() + FromDIP( 6 ), GetClientSize().y );

    dc.SetPen( wxPen( tabBorder() ) );
    dc.SetBrush( wxBrush( colors->GetColor( active ).WithAlpha( 1.0 ).ToColour() ) );
    dc.DrawRectangle( m_swatchRect );

    dc.SetBrush( wxBrush( buttonBg() ) );
    dc.DrawRectangle( m_layerSetRect );
    dc.SetTextForeground( tabText() );
    wxSize lsSize = dc.GetTextExtent( wxS( "LS" ) );
    dc.DrawText( wxS( "LS" ), m_layerSetRect.x + ( m_layerSetRect.width - lsSize.x ) / 2,
                 m_layerSetRect.y + ( m_layerSetRect.height - lsSize.y ) / 2 );
}


void ALTIUM_LAYER_TABS::showLayerSetMenu()
{
    // Altium's layer sets map to KiCad's layer presets
    PCB_BASE_EDIT_FRAME* editFrame = dynamic_cast<PCB_BASE_EDIT_FRAME*>( m_frame );

    if( !editFrame || !editFrame->GetAppearancePanel() )
        return;

    APPEARANCE_CONTROLS* appearance = editFrame->GetAppearancePanel();
    const wxArrayString& presets = appearance->GetLayerPresetsMRU();
    wxMenu               menu;

    for( size_t ii = 0; ii < presets.size(); ++ii )
        menu.Append( wxID_HIGHEST + int( ii ), presets[ii] );

    if( presets.empty() )
        return;

    int picked = GetPopupMenuSelectionFromUser( menu, m_layerSetRect.GetBottomLeft() );
    int idx = picked - wxID_HIGHEST;

    if( picked != wxID_NONE && idx >= 0 && idx < int( presets.size() ) )
        appearance->ApplyLayerPreset( wxString( presets[idx] ) );
}


void ALTIUM_LAYER_TABS::onLeftDown( wxMouseEvent& aEvent )
{
    if( m_layerSetRect.Contains( aEvent.GetPosition() ) )
    {
        showLayerSetMenu();
        return;
    }

    if( m_swatchRect.Contains( aEvent.GetPosition() ) )
        return;

    for( const TAB& tab : m_tabs )
    {
        if( tab.rect.Contains( aEvent.GetPosition() ) )
        {
            m_frame->SetActiveLayer( tab.layer );
            return;
        }
    }
}


void ALTIUM_LAYER_TABS::onMouseWheel( wxMouseEvent& aEvent )
{
    int maxOffset = std::max( 0, contentWidth() - GetClientSize().x );
    int step = FromDIP( 40 ) * ( aEvent.GetWheelRotation() > 0 ? -1 : 1 );

    m_scrollOffset = std::clamp( m_scrollOffset + step, 0, maxOffset );
    Refresh();
}


void ALTIUM_LAYER_TABS::onSize( wxSizeEvent& aEvent )
{
    Refresh();
    aEvent.Skip();
}


void ALTIUM_LAYER_TABS::onUpdateUI( wxUpdateUIEvent& aEvent )
{
    // Layer visibility can be changed from many places (appearance panel, presets,
    // hotkeys); polling here keeps the strip in sync without hooking each of them.
    BOARD* board = m_frame->GetBoard();

    if( !board )
        return;

    LSET shown;

    for( PCB_LAYER_ID layer : board->GetEnabledLayers().Seq() )
    {
        if( board->IsLayerVisible( layer ) )
            shown.set( layer );
    }

    if( shown != m_shownLayers )
    {
        int offset = m_scrollOffset;
        OnBoardChanged();
        m_scrollOffset = offset;
    }
    else if( m_frame->GetActiveLayer() != m_paintedActive )
    {
        Refresh();
    }
}
