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

#include "view_options_3d.h"

#include <algorithm>
#include <functional>

#include <wx/dcbuffer.h>
#include <wx/settings.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <3d_viewer/eda_3d_viewer_settings.h>
#include <3d_viewer/panel_3d_layout.h>
#include <pcb_edit_frame.h>
#include <pgm_base.h>
#include <settings/settings_manager.h>
#include <tool/tool_manager.h>
#include <tools/pcb_actions.h>


namespace
{
wxColour panelBg()
{
    return wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOW );
}

wxColour textColour()
{
    return wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOWTEXT );
}

/// The slightly lighter box a section's rows sit in
wxColour bodyBg()
{
    return PROPERTIES_FORM::Mix( panelBg(), textColour(), 0.04 );
}
} // namespace


/**
 * Altium's thin slider: a dark track and a small light thumb.  Values run 0..1; the callback
 * fires on release (aFinal) and while dragging.
 */
class VIEW_OPTIONS_3D_SLIDER : public wxWindow
{
public:
    VIEW_OPTIONS_3D_SLIDER( wxWindow* aParent, std::function<void( double, bool )> aOnChange ) :
            wxWindow( aParent, wxID_ANY ),
            m_onChange( std::move( aOnChange ) )
    {
        SetBackgroundStyle( wxBG_STYLE_PAINT );
        SetMinSize( wxSize( FromDIP( 60 ), FromDIP( 18 ) ) );

        Bind( wxEVT_PAINT, [this]( wxPaintEvent& ) { paint(); } );
        Bind( wxEVT_LEFT_DOWN,
              [this]( wxMouseEvent& aEvent )
              {
                  CaptureMouse();
                  setFromX( aEvent.GetX(), false );
              } );
        Bind( wxEVT_MOTION,
              [this]( wxMouseEvent& aEvent )
              {
                  if( HasCapture() && aEvent.LeftIsDown() )
                      setFromX( aEvent.GetX(), false );
              } );
        Bind( wxEVT_LEFT_UP,
              [this]( wxMouseEvent& aEvent )
              {
                  if( HasCapture() )
                  {
                      ReleaseMouse();
                      setFromX( aEvent.GetX(), true );
                  }
              } );
        Bind( wxEVT_MOUSE_CAPTURE_LOST, []( wxMouseCaptureLostEvent& ) {} );
    }

    void   SetValue( double aValue ) { m_value = std::clamp( aValue, 0.0, 1.0 ); Refresh(); }
    double GetValue() const { return m_value; }

private:
    int trackLeft() const { return FromDIP( 4 ); }
    int trackRight() const { return GetClientSize().x - FromDIP( 4 ); }

    void setFromX( int aX, bool aFinal )
    {
        int    span = std::max( 1, trackRight() - trackLeft() );
        double value = std::clamp( double( aX - trackLeft() ) / span, 0.0, 1.0 );

        if( value != m_value || aFinal )
        {
            m_value = value;
            Refresh();
            m_onChange( m_value, aFinal );
        }
    }

    void paint()
    {
        wxAutoBufferedPaintDC dc( this );
        wxSize                size = GetClientSize();

        dc.SetBackground( wxBrush( GetParent()->GetBackgroundColour() ) );
        dc.Clear();

        int cy = size.y / 2;
        int th = FromDIP( 6 );

        dc.SetPen( *wxTRANSPARENT_PEN );
        dc.SetBrush( wxBrush( PROPERTIES_FORM::Mix( panelBg(), *wxBLACK, 0.35 ) ) );
        dc.DrawRoundedRectangle( trackLeft(), cy - th / 2, trackRight() - trackLeft(), th, th / 2 );

        int x = trackLeft() + int( m_value * ( trackRight() - trackLeft() ) );
        dc.SetBrush( wxBrush( PROPERTIES_FORM::Mix( textColour(), panelBg(), 0.1 ) ) );
        dc.DrawRoundedRectangle( x - FromDIP( 2 ), cy - FromDIP( 6 ), FromDIP( 4 ), FromDIP( 12 ),
                                 FromDIP( 1 ) );
    }

    std::function<void( double, bool )> m_onChange;
    double                              m_value = 0.0;
};


/**
 * A collapsible section title, as in Altium's panels: a small triangle and the bold title.
 */
class VIEW_OPTIONS_3D_TITLE : public wxWindow
{
public:
    VIEW_OPTIONS_3D_TITLE( wxWindow* aParent, const wxString& aTitle, std::function<void( bool )> aToggle ) :
            wxWindow( aParent, wxID_ANY ),
            m_title( aTitle ),
            m_toggle( std::move( aToggle ) )
    {
        SetBackgroundStyle( wxBG_STYLE_PAINT );
        wxFont font = GetFont();
        font.MakeBold();
        SetFont( font );
        SetMinSize( wxSize( -1, GetCharHeight() + FromDIP( 12 ) ) );
        SetCursor( wxCursor( wxCURSOR_HAND ) );

        Bind( wxEVT_PAINT,
              [this]( wxPaintEvent& )
              {
                  wxAutoBufferedPaintDC dc( this );
                  wxSize                size = GetClientSize();

                  dc.SetBackground( wxBrush( GetParent()->GetBackgroundColour() ) );
                  dc.Clear();
                  dc.SetFont( GetFont() );

                  int s = FromDIP( 4 );
                  int cx = FromDIP( 8 );
                  int cy = size.y / 2;

                  dc.SetPen( *wxTRANSPARENT_PEN );
                  dc.SetBrush( wxBrush( textColour() ) );

                  wxPoint tri[3];

                  if( m_expanded )
                  {
                      tri[0] = wxPoint( cx + s, cy - s );
                      tri[1] = wxPoint( cx + s, cy + s );
                      tri[2] = wxPoint( cx - s, cy + s );
                  }
                  else
                  {
                      tri[0] = wxPoint( cx - s / 2, cy - s );
                      tri[1] = wxPoint( cx + s, cy );
                      tri[2] = wxPoint( cx - s / 2, cy + s );
                  }

                  dc.DrawPolygon( 3, tri );
                  dc.SetTextForeground( textColour() );
                  dc.DrawText( m_title, FromDIP( 18 ),
                               ( size.y - dc.GetTextExtent( m_title ).y ) / 2 );
              } );

        Bind( wxEVT_LEFT_DOWN,
              [this]( wxMouseEvent& )
              {
                  m_expanded = !m_expanded;
                  Refresh();
                  m_toggle( m_expanded );
              } );
    }

private:
    wxString                    m_title;
    std::function<void( bool )> m_toggle;
    bool                        m_expanded = true;
};


VIEW_OPTIONS_3D::VIEW_OPTIONS_3D( wxWindow* aParent, PCB_EDIT_FRAME* aFrame ) :
        wxScrolledWindow( aParent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL ),
        m_frame( aFrame )
{
    SetBackgroundColour( panelBg() );
    SetScrollRate( 0, FromDIP( 8 ) );

    m_sizer = new wxBoxSizer( wxVERTICAL );
    SetSizer( m_sizer );

    using S = EDA_3D_VIEWER_SETTINGS;

    // General Settings
    {
        wxWindow*        body = addSection( _( "General Settings" ) );
        PROPERTIES_FORM* form = new PROPERTIES_FORM( body, this );
        body->GetSizer()->Add( form, 0, wxEXPAND );
        m_forms.push_back( form );

        form->AddCustomSegmented( { _( "Off" ), _( "On" ) },
                [this]() { return m_frame->Is3DLayoutMode() ? 1 : 0; },
                [this]( int aIndex )
                {
                    const TOOL_ACTION* action = aIndex ? &PCB_ACTIONS::layoutMode3D
                                                       : &PCB_ACTIONS::layoutMode2D;

                    // The switch swaps this page out: do it after the click
                    m_frame->CallAfter( [this, action]()
                                        {
                                            m_frame->GetToolManager()->RunAction( *action );
                                        } );
                },
                {}, _( "3D" ) );

        // Single layer mode only applies to the 2D view here
        form->AddCustomSegmented( { _( "Off" ), _( "On" ) }, []() { return 0; },
                                  []( int ) {}, { false, false }, _( "Single Layer Mode" ) );

        form->AddSeparator();

        form->AddCustomSegmented( { _( "Orthographic" ), _( "Perspective" ) },
                [this]() { return !m_layoutPanel || m_layoutPanel->IsPerspective() ? 1 : 0; },
                [this]( int aIndex )
                {
                    if( m_layoutPanel )
                        m_layoutPanel->SetPerspective( aIndex == 1 );
                },
                {}, _( "Projection" ) );

        form->AddCustomSegmented( { _( "Off" ), _( "On" ) },
                [this]()
                {
                    S* c = cfg();
                    return c && ( c->m_Render.show_footprints_normal
                                  || c->m_Render.show_footprints_insert ) ? 1 : 0;
                },
                [this]( int aIndex )
                {
                    if( S* c = cfg() )
                    {
                        c->m_Render.show_footprints_normal = aIndex == 1;
                        c->m_Render.show_footprints_insert = aIndex == 1;
                        c->m_Render.show_footprints_virtual = aIndex == 1;
                    }

                    if( m_layoutPanel )
                        m_layoutPanel->RefreshView();
                },
                {}, _( "Show 3D Bodies" ) );
    }

    // 3D Settings
    {
        wxWindow*   body = addSection( _( "3D Settings" ) );
        wxBoxSizer* bodySizer = static_cast<wxBoxSizer*>( body->GetSizer() );

        wxBoxSizer*   thicknessRow = new wxBoxSizer( wxHORIZONTAL );
        wxStaticText* thicknessLabel = new wxStaticText( body, wxID_ANY, _( "Board thickness (Scale)" ) );
        thicknessLabel->SetForegroundColour( textColour() );
        thicknessRow->Add( thicknessLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP( 8 ) );

        // Altium's slider starts at the real thickness; the far end is ten times it
        m_thickness = new VIEW_OPTIONS_3D_SLIDER( body,
                [this]( double aValue, bool aFinal )
                {
                    if( S* c = cfg() )
                        c->m_LayoutView.board_thickness_scale = 1.0 + aValue * 9.0;

                    if( aFinal )
                        apply();
                } );
        thicknessRow->Add( m_thickness, 1, wxALIGN_CENTER_VERTICAL );
        bodySizer->Add( thicknessRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP( 6 ) );

        PROPERTIES_FORM* form = new PROPERTIES_FORM( body, this );
        bodySizer->Add( form, 0, wxEXPAND );
        m_forms.push_back( form );

        form->AddCustomSegmented( { _( "Realistic" ), _( "By Layer" ) },
                [this]()
                {
                    S* c = cfg();
                    return c && c->m_LayoutView.colors_by_layer ? 1 : 0;
                },
                [this]( int aIndex )
                {
                    if( S* c = cfg() )
                        c->m_LayoutView.colors_by_layer = aIndex == 1;

                    apply();
                },
                {}, _( "Colors" ) );

        // Material table: swatch, name and transparency, as Altium lists them
        wxFlexGridSizer* table = new wxFlexGridSizer( 2, FromDIP( 2 ), FromDIP( 8 ) );
        table->AddGrowableCol( 1, 1 );

        wxStaticText* nameHead = new wxStaticText( body, wxID_ANY, _( "Name" ) );
        wxStaticText* transHead = new wxStaticText( body, wxID_ANY, _( "Transparency" ) );
        nameHead->SetForegroundColour( textColour() );
        transHead->SetForegroundColour( textColour() );
        table->Add( nameHead, 0, wxALIGN_CENTER_VERTICAL );
        table->Add( transHead, 0, wxALIGN_CENTER_VERTICAL );

        const wxString names[S::LM_COUNT] = { _( "Copper" ), _( "Core" ), _( "Top Silkscreen" ),
                                              _( "Bottom Silkscreen" ), _( "Top Solder Mask" ),
                                              _( "Bottom Solder Mask" ), _( "Top Paste Mask" ),
                                              _( "Bottom Paste Mask" ) };

        for( int ii = 0; ii < S::LM_COUNT; ++ii )
        {
            wxBoxSizer* nameRow = new wxBoxSizer( wxHORIZONTAL );
            wxWindow*   swatch = new wxWindow( body, wxID_ANY, wxDefaultPosition,
                                               FromDIP( wxSize( 12, 12 ) ) );
            swatch->SetMinSize( FromDIP( wxSize( 12, 12 ) ) );
            m_swatches.push_back( swatch );

            wxStaticText* name = new wxStaticText( body, wxID_ANY, names[ii] );
            name->SetForegroundColour( textColour() );

            nameRow->Add( swatch, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP( 6 ) );
            nameRow->Add( name, 0, wxALIGN_CENTER_VERTICAL );
            table->Add( nameRow, 0, wxALIGN_CENTER_VERTICAL );

            VIEW_OPTIONS_3D_SLIDER* slider = new VIEW_OPTIONS_3D_SLIDER( body,
                    [this, ii]( double aValue, bool aFinal )
                    {
                        if( S* c = cfg() )
                        {
                            std::vector<double>& t = c->m_LayoutView.transparency;

                            // Fill unset entries with their defaults before changing one
                            for( int jj = (int) t.size(); jj < S::LM_COUNT; ++jj )
                                t.push_back( c->LayoutTransparency( jj ) );

                            t[ii] = aValue;
                        }

                        if( aFinal )
                            apply();
                    } );
            m_transparency.push_back( slider );
            table->Add( slider, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL );
        }

        bodySizer->Add( table, 0, wxEXPAND | wxALL, FromDIP( 6 ) );
    }

    Sync();
}


wxWindow* VIEW_OPTIONS_3D::addSection( const wxString& aTitle )
{
    wxPanel* body = new wxPanel( this );
    body->SetBackgroundColour( bodyBg() );
    body->SetSizer( new wxBoxSizer( wxVERTICAL ) );

    VIEW_OPTIONS_3D_TITLE* title = new VIEW_OPTIONS_3D_TITLE( this, aTitle,
            [this, body]( bool aExpanded )
            {
                body->Show( aExpanded );
                FormLayoutChanged();
            } );

    m_sizer->Add( title, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP( 4 ) );
    m_sizer->Add( body, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP( 6 ) );
    return body;
}


EDA_3D_VIEWER_SETTINGS* VIEW_OPTIONS_3D::cfg() const
{
    return GetAppSettings<EDA_3D_VIEWER_SETTINGS>( "3d_viewer" );
}


void VIEW_OPTIONS_3D::SetLayoutPanel( PANEL_3D_LAYOUT* aPanel )
{
    m_layoutPanel = aPanel;
    Sync();
}


void VIEW_OPTIONS_3D::Sync()
{
    using S = EDA_3D_VIEWER_SETTINGS;

    for( PROPERTIES_FORM* form : m_forms )
        form->Sync();

    if( S* c = cfg() )
    {
        m_thickness->SetValue( ( c->m_LayoutView.board_thickness_scale - 1.0 ) / 9.0 );

        for( int ii = 0; ii < S::LM_COUNT; ++ii )
            m_transparency[ii]->SetValue( c->LayoutTransparency( ii ) );
    }

    for( int ii = 0; ii < (int) m_swatches.size(); ++ii )
    {
        if( m_layoutPanel )
            m_swatches[ii]->SetBackgroundColour( m_layoutPanel->GetMaterialColor( ii ).ToColour() );

        m_swatches[ii]->Refresh();
    }

    FormLayoutChanged();
}


void VIEW_OPTIONS_3D::FormLayoutChanged()
{
    Layout();
    FitInside();
    Refresh();
}


void VIEW_OPTIONS_3D::apply()
{
    if( m_layoutPanel )
        m_layoutPanel->ApplyViewSettings();

    Sync();
}
