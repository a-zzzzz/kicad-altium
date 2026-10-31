/*
 * This program source code file is part of KICAD, a free EDA CAD application.
 *
 * Copyright (C) 2020-2023 CERN
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 * @author Maciej Suminski <maciej.suminski@cern.ch>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "pcb_properties_panel.h"
#include <zone.h>

#include <font/fontconfig.h>
#include <font/kicad_font_name.h>
#include <frame_type.h>
#include <pgm_base.h>
#include <pcb_base_edit_frame.h>
#include <pcbnew_settings.h>
#include <tool/tool_manager.h>
#include <tools/pcb_actions.h>
#include <tools/pcb_selection_tool.h>
#include <router/pns_router.h>
#include <router/router_tool.h>
#include <properties/property_mgr.h>
#include <properties/pg_editors.h>
#include <board_commit.h>
#include <board_connected_item.h>
#include <board.h>
#include <board_design_settings.h>
#include <properties/pg_properties.h>
#include <properties/property.h>
#include <pcb_shape.h>
#include <pcb_text.h>
#include <pcb_track.h>
#include <pcb_generator.h>
#include <generators/pcb_tuning_pattern.h>
#include <pad.h>
#include <footprint.h>
#include <pcb_field.h>
#include <template_fieldnames.h>
#include <settings/color_settings.h>
#include <string_utils.h>
#include <widgets/net_selector.h>
#include <widgets/ui_common.h>
#include <widgets/selection_filter_chips.h>
#include <widgets/altium_layer_tabs.h>
#include <hotkeys_basic.h>
#include <gal/gal_display_options.h>
#include <settings/app_settings.h>
#include <wx/checkbox.h>
#include <wx/dcbuffer.h>
#include <wx/stattext.h>
#include <functional>

static const wxString MISSING_FIELD_SENTINEL = wxS( "\uE000" );

class PCB_FOOTPRINT_FIELD_PROPERTY : public PROPERTY_BASE
{
public:
    PCB_FOOTPRINT_FIELD_PROPERTY( const wxString& aName ) :
            PROPERTY_BASE( aName ),
            m_name( aName )
    {
    }

    size_t OwnerHash() const override { return TYPE_HASH( FOOTPRINT ); }
    size_t BaseHash() const override { return TYPE_HASH( FOOTPRINT ); }
    size_t TypeHash() const override { return TYPE_HASH( wxString ); }

    bool Writeable( INSPECTABLE* aObject ) const override
    {
        return PROPERTY_BASE::Writeable( aObject );
    }

    void setter( void* obj, wxAny& v ) override
    {
        wxString value;

        if( !v.GetAs( &value ) )
            return;

        FOOTPRINT* footprint = reinterpret_cast<FOOTPRINT*>( obj );
        PCB_FIELD* field = footprint->GetField( m_name );

        wxString variantName;

        if( footprint->GetBoard() )
            variantName = footprint->GetBoard()->GetCurrentVariant();

        if( !variantName.IsEmpty() )
        {
            // Store the value as a variant override
            FOOTPRINT_VARIANT* variant = footprint->AddVariant( variantName );

            if( variant )
                variant->SetFieldValue( m_name, value );
        }
        else
        {
            // Set the base field value
            if( !field )
            {
                PCB_FIELD* newField = new PCB_FIELD( footprint, FIELD_T::USER, m_name );
                newField->SetText( value );
                footprint->Add( newField );
            }
            else
            {
                field->SetText( value );
            }
        }
    }

    wxAny getter( const void* obj ) const override
    {
        const FOOTPRINT* footprint = reinterpret_cast<const FOOTPRINT*>( obj );
        PCB_FIELD* field = footprint->GetField( m_name );

        if( field )
        {
            wxString variantName;

            if( footprint->GetBoard() )
                variantName = footprint->GetBoard()->GetCurrentVariant();

            wxString text;

            if( !variantName.IsEmpty() )
                text = footprint->GetFieldValueForVariant( variantName, m_name );
            else
                text = field->GetText();

            return wxAny( text );
        }
        else
        {
            return wxAny( MISSING_FIELD_SENTINEL );
        }
    }

private:
    wxString m_name;
};

std::set<wxString> PCB_PROPERTIES_PANEL::m_currentFieldNames;


class PG_NET_SELECTOR_EDITOR : public wxPGEditor
{
public:
    static const wxString EDITOR_NAME;

    PG_NET_SELECTOR_EDITOR() = default;

    wxString GetName() const override { return EDITOR_NAME; }

    wxPGWindowList CreateControls( wxPropertyGrid* aGrid, wxPGProperty* aProperty, const wxPoint& aPos,
                                   const wxSize& aSize ) const override
    {
        NET_SELECTOR* editor = new NET_SELECTOR( aGrid->GetPanel(), wxID_ANY, aPos, aSize, 0 );

        // wxPropertyGrid registers editors globally and the same PG_NET_SELECTOR_EDITOR
        // instance is reused by every PCB_PROPERTIES_PANEL (board editor, footprint editor).
        // Resolve the owning panel -- and therefore the live frame and board -- from the
        // grid at use time instead of caching frame state on the editor.  This avoids
        // cross-frame state corruption and nullptr derefs when one panel is destroyed while
        // another is still live.
        if( PCB_PROPERTIES_PANEL* panel = dynamic_cast<PCB_PROPERTIES_PANEL*>( aGrid->GetParent() ) )
        {
            if( PCB_BASE_EDIT_FRAME* frame = panel->GetFrame() )
            {
                if( BOARD* board = frame->GetBoard() )
                    editor->SetNetInfo( &board->GetNetInfo() );
            }
        }

        editor->SetIndeterminateString( INDETERMINATE_STATE );
        UpdateControl( aProperty, editor );

        editor->Bind( FILTERED_ITEM_SELECTED,
                      [=]( wxCommandEvent& aEvt )
                      {
                          auto& choices = const_cast<wxPGChoices&>( aProperty->GetChoices() );
                          wxString netname = editor->GetSelectedNetname();

                          if( choices.Index( netname ) == wxNOT_FOUND )
                              choices.Add( netname, editor->GetSelectedNetcode() );

                          wxVariant val( editor->GetSelectedNetcode() );
                          aGrid->ChangePropertyValue( aProperty, val );
                      } );

        return editor;
    }

    void UpdateControl( wxPGProperty* aProperty, wxWindow* aCtrl ) const override
    {
        if( NET_SELECTOR* editor = dynamic_cast<NET_SELECTOR*>( aCtrl ) )
        {
            if( aProperty->IsValueUnspecified() )
                editor->SetIndeterminate();
            else
                editor->SetSelectedNetcode( (int) aProperty->GetValue().GetLong() );
        }
    }

    bool GetValueFromControl( wxVariant& aVariant, wxPGProperty* aProperty, wxWindow* aCtrl ) const override
    {
        NET_SELECTOR* editor = dynamic_cast<NET_SELECTOR*>( aCtrl );

        if( !editor )
            return false;

        aVariant = static_cast<long>( editor->GetSelectedNetcode() );
        return true;
    }

    bool OnEvent( wxPropertyGrid* aGrid, wxPGProperty* aProperty, wxWindow* aWindow, wxEvent& aEvent ) const override
    {
        return false;
    }
};


const wxString PG_NET_SELECTOR_EDITOR::EDITOR_NAME = wxS( "PG_NET_SELECTOR_EDITOR" );



namespace
{
wxColour snapMix( const wxColour& a, const wxColour& b, double t )
{
    return wxColour( a.Red() + ( b.Red() - a.Red() ) * t, a.Green() + ( b.Green() - a.Green() ) * t,
                     a.Blue() + ( b.Blue() - a.Blue() ) * t );
}


/**
 * Altium's segmented "Snapping" selector: All Layers | Current Layer | Off.
 */
class SNAP_SEGMENTS : public wxPanel
{
public:
    SNAP_SEGMENTS( wxWindow* aParent, std::function<int()> aGet, std::function<void( int )> aSet ) :
            wxPanel( aParent ),
            m_get( std::move( aGet ) ),
            m_set( std::move( aSet ) )
    {
        m_labels = { _( "All Layers" ), _( "Current Layer" ), _( "Off" ) };

        SetBackgroundStyle( wxBG_STYLE_PAINT );
        SetMinSize( wxSize( FromDIP( 150 ), GetCharHeight() + FromDIP( 12 ) ) );

        Bind( wxEVT_PAINT, &SNAP_SEGMENTS::onPaint, this );
        Bind( wxEVT_LEFT_DOWN,
              [this]( wxMouseEvent& aEvent )
              {
                  int seg = aEvent.GetX() * 3 / std::max( 1, GetClientSize().x );
                  m_set( std::clamp( seg, 0, 2 ) );
                  Refresh();
              } );
    }

private:
    void onPaint( wxPaintEvent& )
    {
        wxAutoBufferedPaintDC dc( this );
        wxColour              bg = GetParent()->GetBackgroundColour();
        wxColour              fg = wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOWTEXT );
        wxSize                size = GetClientSize();
        int                   current = m_get();

        dc.SetBackground( wxBrush( bg ) );
        dc.Clear();
        dc.SetFont( GetFont() );

        wxColour off = snapMix( bg, *wxBLACK, 0.25 );
        wxColour on( 0x5A, 0x7C, 0xA0 );
        wxColour edge = snapMix( bg, *wxBLACK, 0.55 );

        dc.SetPen( wxPen( edge ) );
        dc.SetBrush( wxBrush( off ) );
        dc.DrawRoundedRectangle( wxRect( wxPoint( 0, 0 ), size ), FromDIP( 3 ) );

        for( int ii = 0; ii < 3; ++ii )
        {
            int    x0 = size.x * ii / 3;
            int    x1 = size.x * ( ii + 1 ) / 3;
            wxRect seg( x0, 0, x1 - x0, size.y );

            if( ii == current )
            {
                dc.SetPen( *wxTRANSPARENT_PEN );
                dc.SetBrush( wxBrush( on ) );
                dc.DrawRectangle( seg.Deflate( 1 ) );
            }

            if( ii > 0 )
            {
                dc.SetPen( wxPen( edge ) );
                dc.DrawLine( x0, 1, x0, size.y - 1 );
            }

            wxSize ext = dc.GetTextExtent( m_labels[ii] );
            dc.SetTextForeground( ii == current ? wxColour( 0xF2, 0xF2, 0xF2 ) : fg );
            dc.DrawText( m_labels[ii], seg.x + ( seg.width - ext.x ) / 2,
                         seg.y + ( seg.height - ext.y ) / 2 );
        }
    }

    std::vector<wxString>     m_labels;
    std::function<int()>      m_get;
    std::function<void( int )> m_set;
};


/**
 * Altium's "Objects for snapping" list: an On/Off check box and the object kind per row.  A row
 * without a check box is an Altium snap KiCad doesn't have: shown, greyed, not clickable.
 */
class SNAP_OBJECT_LIST : public wxPanel
{
public:
    SNAP_OBJECT_LIST( wxWindow* aParent, std::vector<std::pair<wxCheckBox*, wxString>> aRows,
                      std::function<void( wxCheckBox* )> aToggle ) :
            wxPanel( aParent ),
            m_rows( std::move( aRows ) ),
            m_toggle( std::move( aToggle ) )
    {
        SetBackgroundStyle( wxBG_STYLE_PAINT );
        SetMinSize( wxSize( FromDIP( 150 ), rowHeight() * ( (int) m_rows.size() + 1 ) + 2 ) );

        Bind( wxEVT_PAINT, &SNAP_OBJECT_LIST::onPaint, this );
        Bind( wxEVT_LEFT_DOWN,
              [this]( wxMouseEvent& aEvent )
              {
                  int row = aEvent.GetY() / rowHeight() - 1;

                  if( row >= 0 && row < (int) m_rows.size() && m_rows[row].first )
                  {
                      m_selected = row;
                      m_toggle( m_rows[row].first );
                      Refresh();
                  }
              } );
    }

private:
    int rowHeight() const { return GetCharHeight() + FromDIP( 9 ); }

    void onPaint( wxPaintEvent& )
    {
        wxAutoBufferedPaintDC dc( this );
        wxColour              bg = GetParent()->GetBackgroundColour();
        wxColour              fg = wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOWTEXT );
        wxSize                size = GetClientSize();
        int                   rh = rowHeight();
        int                   checkCol = FromDIP( 64 );

        dc.SetBackground( wxBrush( snapMix( bg, *wxBLACK, 0.25 ) ) );
        dc.Clear();
        dc.SetFont( GetFont() );

        // Header row
        dc.SetPen( *wxTRANSPARENT_PEN );
        dc.SetBrush( wxBrush( snapMix( bg, *wxBLACK, 0.10 ) ) );
        dc.DrawRectangle( 0, 0, size.x, rh );
        dc.SetTextForeground( fg );
        dc.DrawText( _( "On/Off" ), FromDIP( 6 ), ( rh - GetCharHeight() ) / 2 );
        dc.DrawText( _( "Objects" ), checkCol + FromDIP( 6 ), ( rh - GetCharHeight() ) / 2 );
        dc.SetPen( wxPen( snapMix( bg, fg, 0.25 ) ) );
        dc.DrawLine( checkCol, FromDIP( 4 ), checkCol, rh - FromDIP( 4 ) );

        for( size_t ii = 0; ii < m_rows.size(); ++ii )
        {
            int  y = rh * ( (int) ii + 1 );
            bool enabled = m_rows[ii].first != nullptr;
            bool on = enabled && m_rows[ii].first->GetValue();
            wxColour text = enabled ? fg : snapMix( bg, fg, 0.45 );

            if( (int) ii == m_selected )
            {
                dc.SetPen( *wxTRANSPARENT_PEN );
                dc.SetBrush( wxBrush( wxColour( 0x5A, 0x7C, 0xA0 ) ) );
                dc.DrawRectangle( 0, y, size.x, rh );
            }

            // Row divider
            dc.SetPen( wxPen( snapMix( bg, *wxBLACK, 0.40 ) ) );
            dc.DrawLine( 0, y + rh - 1, size.x, y + rh - 1 );

            // Check box
            int    box = FromDIP( 14 );
            wxRect r( ( checkCol - box ) / 2, y + ( rh - box ) / 2, box, box );

            dc.SetPen( wxPen( snapMix( bg, fg, enabled ? 0.35 : 0.18 ) ) );
            dc.SetBrush( wxBrush( snapMix( bg, *wxBLACK, 0.15 ) ) );
            dc.DrawRoundedRectangle( r, FromDIP( 2 ) );

            if( on )
            {
                dc.SetPen( wxPen( fg, FromDIP( 2 ) ) );
                dc.DrawLine( r.x + box / 5, r.y + box / 2, r.x + box * 2 / 5, r.y + box * 3 / 4 );
                dc.DrawLine( r.x + box * 2 / 5, r.y + box * 3 / 4, r.x + box * 4 / 5, r.y + box / 4 );
            }

            dc.SetTextForeground( text );
            dc.DrawText( m_rows[ii].second, checkCol + FromDIP( 6 ), y + ( rh - GetCharHeight() ) / 2 );
        }

        dc.SetPen( wxPen( snapMix( bg, *wxBLACK, 0.55 ) ) );
        dc.SetBrush( *wxTRANSPARENT_BRUSH );
        dc.DrawRectangle( wxRect( size ) );
    }

    std::vector<std::pair<wxCheckBox*, wxString>> m_rows;
    std::function<void( wxCheckBox* )>            m_toggle;
    int                                           m_selected = -1;
};


/**
 * Altium's Snap Options section for the Properties panel: Grids, the Snapping mode (All Layers /
 * Current Layer / Off, KiCad's magnetic items) and the objects the cursor snaps to.
 */
class PCB_SNAP_OPTIONS : public wxPanel
{
public:
    PCB_SNAP_OPTIONS( wxWindow* aParent, PCB_BASE_EDIT_FRAME* aFrame ) :
            wxPanel( aParent ),
            m_frame( aFrame )
    {
        SetBackgroundStyle( wxBG_STYLE_PAINT );

        wxColour bg = aParent->GetBackgroundColour();
        wxColour fg = wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOWTEXT );

        // The section's block colour; children draw on it
        SetBackgroundColour( snapMix( bg, fg, 0.06 ) );

        Bind( wxEVT_PAINT,
              [this]( wxPaintEvent& )
              {
                  wxAutoBufferedPaintDC dc( this );
                  dc.SetBackground( wxBrush( GetParent()->GetBackgroundColour() ) );
                  dc.Clear();
                  dc.SetPen( *wxTRANSPARENT_PEN );
                  dc.SetBrush( wxBrush( GetBackgroundColour() ) );
                  dc.DrawRoundedRectangle( wxRect( wxPoint( 0, 0 ), GetClientSize() ), FromDIP( 3 ) );
              } );

        // Hidden check boxes are the buttons' model
        for( wxCheckBox** box : { &m_grids, &m_pads, &m_tracks, &m_graphics } )
        {
            *box = new wxCheckBox( this, wxID_ANY, wxEmptyString );
            ( *box )->Hide();
        }

        m_grids->SetToolTip( _( "Snap the cursor to the grid" ) );
        m_pads->SetToolTip( _( "Snap to pad centers" ) );
        m_tracks->SetToolTip( _( "Snap to track and arc vertices" ) );
        m_graphics->SetToolTip( _( "Snap to board graphics (board shape, lines, arcs)" ) );

        auto toggle = [this]( wxCheckBox* aBox ) { onToggle( aBox ); };

        m_gridChips = new SELECTION_FILTER_CHIPS( this, toggle );
        m_gridChips->SetDrawBlock( false );
        m_gridChips->SetChips( { { m_grids, _( "Grids" ) } } );

        // Altium's rows; those KiCad's magnetic items don't cover are greyed
        m_objectList = new SNAP_OBJECT_LIST( this,
                                             { { m_tracks,   _( "Track/Arcs Vertices" ) },
                                               { nullptr,    _( "Track/Arcs Lines" ) },
                                               { nullptr,    _( "Arc Centers" ) },
                                               { nullptr,    _( "Intersections" ) },
                                               { m_pads,     _( "Pad Centers" ) },
                                               { nullptr,    _( "Pad Vertices" ) },
                                               { nullptr,    _( "Pad Edges" ) },
                                               { nullptr,    _( "Via Centers" ) },
                                               { nullptr,    _( "Regions/Polygons/Fills" ) },
                                               { m_graphics, _( "Board Shape" ) },
                                               { nullptr,    _( "Footprint Origins" ) },
                                               { nullptr,    _( "3D Body Snap Points" ) } },
                                             toggle );

        wxStaticText* snapping = new wxStaticText( this, wxID_ANY, _( "Snapping" ) );
        snapping->SetFont( snapping->GetFont().Bold() );
        m_hotkey = new wxStaticText( this, wxID_ANY, wxEmptyString );
        m_hotkey->SetForegroundColour( fg );

        wxStaticText* objects = new wxStaticText( this, wxID_ANY, _( "Objects for snapping" ) );
        objects->SetFont( objects->GetFont().Bold() );

        m_segments = new SNAP_SEGMENTS( this, [this]() { return snappingMode(); },
                                        [this]( int aMode ) { setSnappingMode( aMode ); } );

        wxBoxSizer* snapRow = new wxBoxSizer( wxHORIZONTAL );
        snapRow->Add( snapping, 0, wxALIGN_CENTER_VERTICAL );
        snapRow->AddStretchSpacer();
        snapRow->Add( m_hotkey, 0, wxALIGN_CENTER_VERTICAL );

        int pad = FromDIP( 6 );
        wxBoxSizer* sizer = new wxBoxSizer( wxVERTICAL );
        sizer->Add( m_gridChips, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad );
        sizer->Add( snapRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad );
        sizer->Add( m_segments, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad );
        sizer->Add( objects, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad );
        sizer->Add( m_objectList, 0, wxEXPAND | wxALL, pad );
        SetSizer( sizer );

        sync();

        // The settings also change from menus, hotkeys and dialogs: follow them while on show
        Bind( wxEVT_IDLE,
              [this]( wxIdleEvent& aEvent )
              {
                  if( IsShownOnScreen() )
                      sync();

                  aEvent.Skip();
              } );
    }

private:
    MAGNETIC_SETTINGS* magnetic() const { return m_frame->GetMagneticItemsSettings(); }

    int snappingMode() const
    {
        MAGNETIC_SETTINGS* mag = magnetic();

        if( !mag )
            return 2;

        if( mag->pads == MAGNETIC_OPTIONS::NO_EFFECT && mag->tracks == MAGNETIC_OPTIONS::NO_EFFECT
                && !mag->graphics )
        {
            return 2;
        }

        return mag->allLayers ? 0 : 1;
    }

    void setSnappingMode( int aMode )
    {
        MAGNETIC_SETTINGS* mag = magnetic();

        if( !mag )
            return;

        if( aMode == 2 )
        {
            if( snappingMode() != 2 )
            {
                m_savedPads = mag->pads;
                m_savedTracks = mag->tracks;
                m_savedGraphics = mag->graphics;
            }

            mag->pads = MAGNETIC_OPTIONS::NO_EFFECT;
            mag->tracks = MAGNETIC_OPTIONS::NO_EFFECT;
            mag->graphics = false;
        }
        else
        {
            // Back on from Off: the objects that were on before
            if( snappingMode() == 2 )
            {
                mag->pads = m_savedPads;
                mag->tracks = m_savedTracks;
                mag->graphics = m_savedGraphics;
            }

            mag->allLayers = aMode == 0;
        }

        sync();
    }

    void onToggle( wxCheckBox* aBox )
    {
        MAGNETIC_SETTINGS* mag = magnetic();

        if( aBox == m_grids )
        {
            // Altium's Grids button: snap always, or never
            GAL_DISPLAY_OPTIONS_IMPL& options = m_frame->GetGalDisplayOptions();
            bool on = options.m_gridSnapping == KIGFX::GRID_SNAPPING::NEVER;

            options.m_gridSnapping = on ? KIGFX::GRID_SNAPPING::ALWAYS : KIGFX::GRID_SNAPPING::NEVER;
            options.NotifyChanged();

            if( APP_SETTINGS_BASE* cfg = m_frame->config() )
            {
                if( WINDOW_SETTINGS* window = m_frame->GetWindowSettings( cfg ) )
                    window->grid.snap = static_cast<int>( options.m_gridSnapping );
            }
        }
        else if( mag && aBox == m_pads )
        {
            if( mag->pads == MAGNETIC_OPTIONS::NO_EFFECT )
            {
                mag->pads = m_savedPads == MAGNETIC_OPTIONS::NO_EFFECT ? MAGNETIC_OPTIONS::CAPTURE_ALWAYS
                                                                        : m_savedPads;
            }
            else
            {
                m_savedPads = mag->pads;
                mag->pads = MAGNETIC_OPTIONS::NO_EFFECT;
            }
        }
        else if( mag && aBox == m_tracks )
        {
            if( mag->tracks == MAGNETIC_OPTIONS::NO_EFFECT )
            {
                mag->tracks = m_savedTracks == MAGNETIC_OPTIONS::NO_EFFECT
                                      ? MAGNETIC_OPTIONS::CAPTURE_CURSOR_IN_TRACK_TOOL
                                      : m_savedTracks;
            }
            else
            {
                m_savedTracks = mag->tracks;
                mag->tracks = MAGNETIC_OPTIONS::NO_EFFECT;
            }
        }
        else if( mag && aBox == m_graphics )
        {
            mag->graphics = !mag->graphics;
        }

        sync();
    }

    void sync()
    {
        MAGNETIC_SETTINGS* mag = magnetic();
        bool               changed = false;

        auto set =
                [&]( wxCheckBox* aBox, bool aValue )
                {
                    if( aBox->GetValue() != aValue )
                    {
                        aBox->SetValue( aValue );
                        changed = true;
                    }
                };

        set( m_grids, m_frame->GetGalDisplayOptions().m_gridSnapping != KIGFX::GRID_SNAPPING::NEVER );
        set( m_pads, mag && mag->pads != MAGNETIC_OPTIONS::NO_EFFECT );
        set( m_tracks, mag && mag->tracks != MAGNETIC_OPTIONS::NO_EFFECT );
        set( m_graphics, mag && mag->graphics );

        if( int mode = snappingMode(); mode != m_lastMode )
        {
            m_lastMode = mode;
            changed = true;
        }

        wxString hotkey = KeyNameFromKeyCode( PCB_ACTIONS::magneticSnapToggle.GetHotKey() );

        if( m_hotkey->GetLabel() != hotkey )
        {
            m_hotkey->SetLabel( hotkey );
            Layout();
        }

        if( changed )
        {
            m_gridChips->Refresh();
            m_objectList->Refresh();
            m_segments->Refresh();
        }
    }

    PCB_BASE_EDIT_FRAME*    m_frame;
    wxCheckBox*             m_grids = nullptr;
    wxCheckBox*             m_pads = nullptr;
    wxCheckBox*             m_tracks = nullptr;
    wxCheckBox*             m_graphics = nullptr;
    SELECTION_FILTER_CHIPS* m_gridChips = nullptr;
    SNAP_OBJECT_LIST*       m_objectList = nullptr;
    SNAP_SEGMENTS*          m_segments = nullptr;
    wxStaticText*           m_hotkey = nullptr;
    int                     m_lastMode = -1;
    MAGNETIC_OPTIONS        m_savedPads = MAGNETIC_OPTIONS::CAPTURE_ALWAYS;
    MAGNETIC_OPTIONS        m_savedTracks = MAGNETIC_OPTIONS::CAPTURE_CURSOR_IN_TRACK_TOOL;
    bool                    m_savedGraphics = true;
};
} // namespace


PCB_PROPERTIES_PANEL::PCB_PROPERTIES_PANEL( wxWindow* aParent, PCB_BASE_EDIT_FRAME* aFrame ) :
        PROPERTIES_PANEL( aParent, aFrame ),
        m_frame( aFrame ),
        m_propMgr( PROPERTY_MANAGER::Instance() )
{
    m_propMgr.Rebuild();
    bool found = false;

    wxASSERT( wxPGGlobalVars );

    wxString editorKey = PG_UNIT_EDITOR::BuildEditorName( m_frame );

    auto it = wxPGGlobalVars->m_mapEditorClasses.find( editorKey );

    if( it != wxPGGlobalVars->m_mapEditorClasses.end() )
    {
        m_unitEditorInstance = static_cast<PG_UNIT_EDITOR*>( it->second );
        m_unitEditorInstance->UpdateFrame( m_frame );
        found = true;
    }

    if( !found )
    {
        PG_UNIT_EDITOR* new_editor = new PG_UNIT_EDITOR( m_frame );
        m_unitEditorInstance = static_cast<PG_UNIT_EDITOR*>( wxPropertyGrid::RegisterEditorClass( new_editor ) );
    }

    it = wxPGGlobalVars->m_mapEditorClasses.find( PG_CHECKBOX_EDITOR::EDITOR_NAME );

    if( it == wxPGGlobalVars->m_mapEditorClasses.end() )
    {
        PG_CHECKBOX_EDITOR* cbEditor = new PG_CHECKBOX_EDITOR();
        m_checkboxEditorInstance = static_cast<PG_CHECKBOX_EDITOR*>( wxPropertyGrid::RegisterEditorClass( cbEditor ) );
    }
    else
    {
        m_checkboxEditorInstance = static_cast<PG_CHECKBOX_EDITOR*>( it->second );
    }

    it = wxPGGlobalVars->m_mapEditorClasses.find( PG_RATIO_EDITOR::EDITOR_NAME );

    if( it == wxPGGlobalVars->m_mapEditorClasses.end() )
    {
        PG_RATIO_EDITOR* ratioEditor = new PG_RATIO_EDITOR();
        m_ratioEditorInstance = static_cast<PG_RATIO_EDITOR*>( wxPropertyGrid::RegisterEditorClass( ratioEditor ) );
    }
    else
    {
        m_ratioEditorInstance = static_cast<PG_RATIO_EDITOR*>( it->second );
    }

    it = wxPGGlobalVars->m_mapEditorClasses.find( PG_NET_SELECTOR_EDITOR::EDITOR_NAME );

    if( it == wxPGGlobalVars->m_mapEditorClasses.end() )
    {
        PG_NET_SELECTOR_EDITOR* netEditor = new PG_NET_SELECTOR_EDITOR();
        m_netSelectorEditorInstance = static_cast<PG_NET_SELECTOR_EDITOR*>( wxPropertyGrid::RegisterEditorClass( netEditor ) );
    }
    else
    {
        m_netSelectorEditorInstance = static_cast<PG_NET_SELECTOR_EDITOR*>( it->second );
    }

    it = wxPGGlobalVars->m_mapEditorClasses.find( PG_FPID_EDITOR::BuildEditorName( m_frame ) );

    if( it != wxPGGlobalVars->m_mapEditorClasses.end() )
    {
        m_fpEditorInstance = static_cast<PG_FPID_EDITOR*>( it->second );
        m_fpEditorInstance->UpdateFrame( m_frame );
    }
    else
    {
        PG_FPID_EDITOR* fpEditor = new PG_FPID_EDITOR( m_frame,
                                                       []()
                                                       {
                                                           return "";
                                                       });
        m_fpEditorInstance = static_cast<PG_FPID_EDITOR*>( wxPropertyGrid::RegisterEditorClass( fpEditor ) );
    }

    it = wxPGGlobalVars->m_mapEditorClasses.find( PG_URL_EDITOR::BuildEditorName( m_frame ) );

    if( it != wxPGGlobalVars->m_mapEditorClasses.end() )
    {
        m_urlEditorInstance = static_cast<PG_URL_EDITOR*>( it->second );
        m_urlEditorInstance->UpdateFrame( m_frame );
    }
    else
    {
        PG_URL_EDITOR* urlEditor = new PG_URL_EDITOR( m_frame );
        m_urlEditorInstance = static_cast<PG_URL_EDITOR*>( wxPropertyGrid::RegisterEditorClass( urlEditor ) );
    }

    // Altium's Snap Options section (above the grid, while nothing is selected)
    m_snapOptionsWidget = true;
    SetDocumentWidget( new PCB_SNAP_OPTIONS( this, m_frame ), _( "Snap Options" ) );

    // Altium's per-object sections, in the board editor and the footprint editor (PcbLib)
    if( m_frame->IsType( FRAME_PCB_EDITOR ) )
        buildBoardInformation();

    if( m_frame->IsType( FRAME_PCB_EDITOR ) || m_frame->IsType( FRAME_FOOTPRINT_EDITOR ) )
        buildAltiumLayouts();
}


PCB_PROPERTIES_PANEL::~PCB_PROPERTIES_PANEL()
{
    m_unitEditorInstance->UpdateFrame( nullptr );
    m_fpEditorInstance->UpdateFrame( nullptr );
    m_urlEditorInstance->UpdateFrame( nullptr );

    // Note: the shared PG_NET_SELECTOR_EDITOR does not cache frame state; it resolves the
    // owning panel from the property grid on each CreateControls call, so no teardown is
    // needed here.
}


const SELECTION& PCB_PROPERTIES_PANEL::getSelection( SELECTION& aFallbackSelection )
{
    PCB_SELECTION_TOOL* selectionTool = m_frame->GetToolManager()->GetTool<PCB_SELECTION_TOOL>();
    const SELECTION& selection = selectionTool->GetSelection();

    if( selection.Empty() && m_frame->IsType( FRAME_FOOTPRINT_EDITOR ) )
    {
        if( BOARD* board = m_frame->GetBoard() )
        {
            if( FOOTPRINT* footprint = board->GetFirstFootprint() )
            {
                aFallbackSelection.Clear();
                aFallbackSelection.Add( footprint );
                return aFallbackSelection;
            }
        }
    }

    return selection;
}


const SELECTION& PCB_PROPERTIES_PANEL::shownSelection( SELECTION& aFallbackSelection )
{
    // Altium's PcbLib shows Library Options (General | Footprint) while nothing is selected;
    // the footprint's own values are on the Footprint tab
    if( m_frame->IsType( FRAME_FOOTPRINT_EDITOR ) )
        return m_frame->GetToolManager()->GetTool<PCB_SELECTION_TOOL>()->GetSelection();

    return getSelection( aFallbackSelection );
}


EDA_ITEM* PCB_PROPERTIES_PANEL::getFrontItem()
{
    SELECTION fallbackSelection;
    const SELECTION& selection = getSelection( fallbackSelection );

    return selection.Empty() ? nullptr : selection.Front();
}


void PCB_PROPERTIES_PANEL::UpdateData()
{
    BOARD* board = m_frame->GetBoard();

    if( !board )
        return;

    SELECTION fallbackSelection;
    const SELECTION& selection = shownSelection( fallbackSelection );

    // TODO perhaps it could be called less often? use PROPERTIES_TOOL and catch MODEL_RELOAD?
    updateLists( board );

    // Will actually just be updatePropertyValues() if selection hasn't changed
    rebuildProperties( selection );
}


void PCB_PROPERTIES_PANEL::AfterCommit()
{
    if( !m_frame->GetBoard() )
        return;

    SELECTION fallbackSelection;
    const SELECTION& selection = shownSelection( fallbackSelection );

    rebuildProperties( selection );
}


void PCB_PROPERTIES_PANEL::rebuildProperties( const SELECTION& aSelection )
{
    m_currentFieldNames.clear();

    for( EDA_ITEM* item : aSelection )
    {
        if( item->Type() != PCB_FOOTPRINT_T )
            continue;

        FOOTPRINT* footprint = static_cast<FOOTPRINT*>( item );

        for( PCB_FIELD* field : footprint->GetFields() )
        {
            wxCHECK2( field, continue );

            m_currentFieldNames.insert( field->GetCanonicalName() );
        }
    }

    const wxString groupFields = _HKI( "Fields" );

    // Make sure value comes immediately after reference.  (Reference is invariant, so was added by
    // FOOTPRINT_DESC().  We *could* still add it here, but then the whole Fields section comes at
    // the end, which isn't ideal.)
    if( !m_propMgr.GetProperty( TYPE_HASH( FOOTPRINT ), _HKI( "Value" ) ) )
        m_propMgr.AddProperty( new PCB_FOOTPRINT_FIELD_PROPERTY( _HKI( "Value" ) ), groupFields );

    for( const wxString& name : m_currentFieldNames )
    {
        if( !m_propMgr.GetProperty( TYPE_HASH( FOOTPRINT ), name ) )
        {
            m_propMgr.AddProperty( new PCB_FOOTPRINT_FIELD_PROPERTY( name ), groupFields )
                    .SetAvailableFunc(
                            [name]( INSPECTABLE* )
                            {
                                return PCB_PROPERTIES_PANEL::m_currentFieldNames.count( name );
                            } );
        }
    }

    PROPERTIES_PANEL::rebuildProperties( aSelection );
}


wxString PCB_PROPERTIES_PANEL::documentCaption() const
{
    if( activeRouter() )
        return _( "Interactive Routing" );

    return m_frame->IsType( FRAME_PCB_EDITOR ) ? _( "Board" ) : _( "Library Options" );
}


wxString PCB_PROPERTIES_PANEL::itemTypeName( EDA_ITEM* aItem ) const
{
    // Altium's object names: a placed footprint is a component, a copper zone a polygon pour
    switch( aItem->Type() )
    {
    case PCB_FOOTPRINT_T:
        if( m_frame->IsType( FRAME_PCB_EDITOR ) )
            return _( "Component" );

        break;

    case PCB_ZONE_T:
        if( static_cast<ZONE*>( aItem )->GetIsRuleArea() )
            return _( "Keepout" );

        return _( "Polygon Pour" );

    case PCB_TRACE_T:     return _( "Track" );
    case PCB_ARC_T:       return _( "Arc" );
    case PCB_VIA_T:       return _( "Via" );
    case PCB_PAD_T:       return _( "Pad" );
    case PCB_TEXT_T:
    case PCB_TEXTBOX_T:   return _( "Text" );    // Altium's text in String or Frame mode

    // A component's designator, comment and other texts are its parameters
    case PCB_FIELD_T:
        return m_frame->IsType( FRAME_PCB_EDITOR ) ? _( "Parameter" ) : _( "Text" );

    // Altium's primitives: a line is a track, a rectangle a fill, a polygon a region
    case PCB_SHAPE_T:
        switch( static_cast<PCB_SHAPE*>( aItem )->GetShape() )
        {
        case SHAPE_T::SEGMENT:   return _( "Track" );
        case SHAPE_T::ARC:
        case SHAPE_T::CIRCLE:    return _( "Arc" );
        case SHAPE_T::RECTANGLE: return _( "Fill" );
        case SHAPE_T::POLY:      return _( "Region" );
        default:                 break;
        }

        break;

    case PCB_DIM_ALIGNED_T:
    case PCB_DIM_ORTHOGONAL_T: return _( "Linear Dimension" );
    case PCB_DIM_RADIAL_T:     return _( "Radial Dimension" );
    case PCB_DIM_LEADER_T:     return _( "Leader" );
    case PCB_DIM_CENTER_T:     return _( "Center Dimension" );

    default:
        break;
    }

    return PROPERTIES_PANEL::itemTypeName( aItem );
}


bool PCB_PROPERTIES_PANEL::documentValueChanged( const wxString& aName, const wxVariant& aValue )
{
    // Snap Options: KiCad's magnetic items, applied immediately
    if( MAGNETIC_SETTINGS* magnetic = m_frame->GetMagneticItemsSettings() )
    {
        if( aName == wxS( "doc.snapPads" ) )
        {
            magnetic->pads = static_cast<MAGNETIC_OPTIONS>( std::clamp( aValue.GetLong(), 0L, 2L ) );
            return true;
        }
        else if( aName == wxS( "doc.snapTracks" ) )
        {
            magnetic->tracks = static_cast<MAGNETIC_OPTIONS>( std::clamp( aValue.GetLong(), 0L, 2L ) );
            return true;
        }
        else if( aName == wxS( "doc.snapGraphics" ) )
        {
            magnetic->graphics = aValue.GetBool();
            return true;
        }
        else if( aName == wxS( "doc.snapAllLayers" ) )
        {
            magnetic->allLayers = aValue.GetBool();
            return true;
        }
    }

    return PROPERTIES_PANEL::documentValueChanged( aName, aValue );
}


void PCB_PROPERTIES_PANEL::appendDocumentProperties()
{
    BOARD* board = m_frame->GetBoard();

    // Altium's Snap Options: the objects the cursor snaps to (KiCad's magnetic items); shown
    // as the Snap Options section above the grid
    if( MAGNETIC_SETTINGS* magnetic = m_snapOptionsWidget ? nullptr : m_frame->GetMagneticItemsSettings() )
    {
        wxPGProperty* snap = docCategory( wxS( "doc.snap" ), _( "Snap Options" ) );

        wxArrayString modes;
        modes.Add( _( "Never" ) );
        modes.Add( _( "When Creating Tracks" ) );
        modes.Add( _( "Always" ) );

        docChoice( snap, wxS( "doc.snapPads" ), _( "Snap to Pads" ), modes,
                   static_cast<int>( magnetic->pads ) );
        docChoice( snap, wxS( "doc.snapTracks" ), _( "Snap to Tracks" ), modes,
                   static_cast<int>( magnetic->tracks ) );
        docBool( snap, wxS( "doc.snapGraphics" ), _( "Snap to Board Graphics" ), magnetic->graphics );
        docBool( snap, wxS( "doc.snapAllLayers" ), _( "Snap On All Layers" ), magnetic->allLayers );
    }

    // The footprint editor's Library Options: its Footprint tab shows the footprint's values
    if( wxString layout = documentLayout(); !layout.IsEmpty() )
    {
        for( LAYOUT_FORM& entry : m_layoutForms )
        {
            if( entry.layout == layout )
                entry.form->Sync();
        }
    }

    if( !board || !m_frame->IsType( FRAME_PCB_EDITOR ) )
        return;

    appendRoutingProperties();

    // Altium's Board Information section (above the grid), and the Parameters and Health
    // Check tabs
    updateBoardInformation();
    appendBoardParameters();
    appendHealthCheck();
}


wxPGProperty* PCB_PROPERTIES_PANEL::createPGProperty( const PROPERTY_BASE* aProperty ) const
{
    if( aProperty->TypeHash() == TYPE_HASH( PCB_LAYER_ID ) )
    {
        wxASSERT( aProperty->HasChoices() );

        const wxPGChoices& canonicalLayers = aProperty->Choices();
        wxArrayString      boardLayerNames;
        wxArrayInt         boardLayerIDs;

        for( int ii = 0; ii < (int) aProperty->Choices().GetCount(); ++ii )
        {
            int layer = canonicalLayers.GetValue( ii );

            // Altium's layer names (Top Layer, Top Overlay ...) for layers not renamed by the user
            boardLayerNames.push_back( ALTIUM_LAYER_TABS::DisplayName(
                    ToLAYER_ID( layer ), m_frame->GetBoard()->GetLayerName( ToLAYER_ID( layer ) ) ) );
            boardLayerIDs.push_back( canonicalLayers.GetValue( ii ) );
        }

        auto ret = new PGPROPERTY_COLORENUM( new wxPGChoices( boardLayerNames, boardLayerIDs ) );

        ret->SetColorFunc(
                [&]( int aValue ) -> wxColour
                {
                    return m_frame->GetColorSettings()->GetColor( ToLAYER_ID( aValue ) ).ToColour();
                } );

        ret->SetLabel( wxGetTranslation( aProperty->Name() ) );
        ret->SetName( aProperty->Name() );
        ret->SetHelpString( wxGetTranslation( aProperty->Name() ) );
        ret->SetClientData( const_cast<PROPERTY_BASE*>( aProperty ) );

        return ret;
    }

    wxPGProperty* prop = PGPropertyFactory( aProperty, m_frame );

    if( aProperty->Name() == GetCanonicalFieldName( FIELD_T::FOOTPRINT ) )
        prop->SetEditor( PG_FPID_EDITOR::BuildEditorName( m_frame ) );
    else if( aProperty->Name() == GetCanonicalFieldName( FIELD_T::DATASHEET ) )
        prop->SetEditor( PG_URL_EDITOR::BuildEditorName( m_frame ) );

    return prop;
}


PROPERTY_BASE* PCB_PROPERTIES_PANEL::getPropertyFromEvent( const wxPropertyGridEvent& aEvent ) const
{
    EDA_ITEM* item = const_cast<PCB_PROPERTIES_PANEL*>( this )->getFrontItem();

    if( !item || !item->IsBOARD_ITEM() )
        return nullptr;

    BOARD_ITEM* firstItem = static_cast<BOARD_ITEM*>( item );

    wxCHECK_MSG( firstItem, nullptr, wxT( "getPropertyFromEvent for a property with nothing selected!") );

    PROPERTY_BASE* property = m_propMgr.GetProperty( TYPE_HASH( *firstItem ), aEvent.GetPropertyName() );
    wxCHECK_MSG( property, nullptr, wxT( "getPropertyFromEvent for a property not found on the selected item!" ) );

    return property;
}


void PCB_PROPERTIES_PANEL::valueChanging( wxPropertyGridEvent& aEvent )
{
    if( m_SuppressGridChangeEvents > 0 )
        return;

    EDA_ITEM* item = getFrontItem();

    PROPERTY_BASE* property = getPropertyFromEvent( aEvent );
    wxCHECK( property, /* void */ );
    wxCHECK( item, /* void */ );

    wxVariant newValue = aEvent.GetPropertyValue();

    if( VALIDATOR_RESULT validationFailure = property->Validate( newValue.GetAny(), item ) )
    {
        wxString errorMsg = wxString::Format( wxS( "%s: %s" ), wxGetTranslation( property->Name() ),
                                              validationFailure->get()->Format( m_frame ) );
        m_frame->ShowInfoBarError( errorMsg );
        aEvent.Veto();
        return;
    }

    aEvent.Skip();
}


void PCB_PROPERTIES_PANEL::valueChanged( wxPropertyGridEvent& aEvent )
{
    if( m_SuppressGridChangeEvents > 0 )
        return;

    wxCHECK( getPropertyFromEvent( aEvent ), /* void */ );

    applyPropertyValues( { { aEvent.GetPropertyName(), aEvent.GetPropertyValue() } } );

    aEvent.Skip();
}


void PCB_PROPERTIES_PANEL::applyPropertyValues( const std::vector<std::pair<wxString, wxVariant>>& aValues )
{
    SELECTION fallbackSelection;
    const SELECTION& selection = getSelection( fallbackSelection );

    if( aValues.empty() || selection.Empty() )
        return;

    // The grid validates while a value changes; values from the Altium sections are checked here
    if( EDA_ITEM* front = selection.Front() )
    {
        for( const auto& [name, value] : aValues )
        {
            PROPERTY_BASE* property = m_propMgr.GetProperty( TYPE_HASH( *front ), name );

            if( !property )
                continue;

            if( VALIDATOR_RESULT failure = property->Validate( value.GetAny(), front ) )
            {
                m_frame->ShowInfoBarError( wxString::Format( wxS( "%s: %s" ),
                                                             wxGetTranslation( property->Name() ),
                                                             failure->get()->Format( m_frame ) ) );
                AfterCommit();
                return;
            }
        }
    }

    BOARD_COMMIT changes( m_frame );
    bool         changedPlacedItem = false;
    bool         changedLock = false;

    PROPERTY_COMMIT_HANDLER handler( &changes );

    for( const auto& [propName, newValue] : aValues )
    {
        changedLock |= propName == _HKI( "Locked" );

        for( EDA_ITEM* edaItem : selection )
        {
            if( !edaItem->IsBOARD_ITEM() )
                continue;

            BOARD_ITEM*    item = static_cast<BOARD_ITEM*>( edaItem );
            PROPERTY_BASE* property = m_propMgr.GetProperty( TYPE_HASH( *item ), propName );

            if( !property )
                continue;

            // An item still being placed (Altium's Tab during placement) isn't on the board yet.
            // Change it directly; the placement's own commit records it.
            if( item->IsNew() )
            {
                item->Set( property, newValue );
                changedPlacedItem = true;
                continue;
            }

            if( item->Type() == PCB_TABLECELL_T )
                changes.Modify( item->GetParent(), nullptr, RECURSE_MODE::NO_RECURSE );
            else if( item->Type() == PCB_GENERATOR_T )
                changes.Modify( item, nullptr, RECURSE_MODE::RECURSE );
            else
                changes.Modify( item, nullptr, RECURSE_MODE::NO_RECURSE );

            // In the PCB Editor, we generally restrict pad movement to the footprint (like dragging)
            if( item->Type() == PCB_PAD_T && m_frame
                    && m_frame->IsType( FRAME_PCB_EDITOR )
                    && !m_frame->GetPcbNewSettings()->m_AllowFreePads
                    && ( propName == _HKI( "Position X" ) || propName == _HKI( "Position Y" ) ) )
            {
                PAD* pad = static_cast<PAD*>( item );
                FOOTPRINT* fp = pad->GetParentFootprint();

                if( fp )
                {
                    VECTOR2I oldPos = pad->GetPosition();
                    VECTOR2I newPos = oldPos;

                    if( propName == _HKI( "Position X" ) )
                        newPos.x = (int) newValue.GetLong();
                    else
                        newPos.y = (int) newValue.GetLong();

                    VECTOR2I delta = newPos - oldPos;

                    if( delta.x != 0 || delta.y != 0 )
                    {
                        changes.Modify( fp );
                        fp->Move( delta );
                    }
                }

                continue;
            }

            // Handle variant-aware boolean properties for footprints
            if( item->Type() == PCB_FOOTPRINT_T )
            {
                FOOTPRINT* footprint = static_cast<FOOTPRINT*>( item );
                wxString   variantName;

                if( footprint->GetBoard() )
                    variantName = footprint->GetBoard()->GetCurrentVariant();

                if( !variantName.IsEmpty()
                    && ( propName == _HKI( "Do not Populate" )
                         || propName == _HKI( "Exclude From Bill of Materials" )
                         || propName == _HKI( "Exclude From Position Files" ) ) )
                {
                    FOOTPRINT_VARIANT* variant = footprint->GetVariant( variantName );

                    if( !variant )
                        variant = footprint->AddVariant( variantName );

                    if( variant )
                    {
                        bool boolValue = newValue.GetBool();

                        if( propName == _HKI( "Do not Populate" ) )
                            variant->SetDNP( boolValue );
                        else if( propName == _HKI( "Exclude From Bill of Materials" ) )
                            variant->SetExcludedFromBOM( boolValue );
                        else if( propName == _HKI( "Exclude From Position Files" ) )
                            variant->SetExcludedFromPosFiles( boolValue );

                        continue;
                    }
                }
            }

            item->Set( property, newValue );
        }
    }

    if( !changes.Empty() )
        changes.Push( _( "Edit Properties" ) );

    if( changedPlacedItem )
        m_frame->GetToolManager()->PostAction( ACTIONS::refreshPreview );

    m_frame->Refresh();

    // Perform grid updates as necessary based on value change
    AfterCommit();

    // PointEditor may need to update if locked/unlocked
    if( changedLock )
        m_frame->GetToolManager()->ProcessEvent( EVENTS::SelectedEvent );
}


void PCB_PROPERTIES_PANEL::updateLists( const BOARD* aBoard )
{
    wxPGChoices layersAll;
    wxPGChoices layersCu;
    wxPGChoices nets;
    wxPGChoices fonts;

    // Regenerate all layers
    for( PCB_LAYER_ID layer : aBoard->GetEnabledLayers().UIOrder() )
        layersAll.Add( LSET::Name( layer ), layer );

    for( PCB_LAYER_ID layer : LSET( aBoard->GetEnabledLayers() & LSET::AllCuMask() ).UIOrder() )
        layersCu.Add( LSET::Name( layer ), layer );

    m_propMgr.GetProperty( TYPE_HASH( BOARD_ITEM ), _HKI( "Layer" ) )->SetChoices( layersAll );
    m_propMgr.GetProperty( TYPE_HASH( PCB_SHAPE ), _HKI( "Layer" ) )->SetChoices( layersAll );

    // Copper only properties
    m_propMgr.GetProperty( TYPE_HASH( BOARD_CONNECTED_ITEM ), _HKI( "Layer" ) )->SetChoices( layersCu );
    m_propMgr.GetProperty( TYPE_HASH( PAD ), _HKI( "Bottom Backdrill Must-Cut" ) )->SetChoices( layersCu );
    m_propMgr.GetProperty( TYPE_HASH( PAD ), _HKI( "Top Backdrill Must-Cut" ) )->SetChoices( layersCu );
    m_propMgr.GetProperty( TYPE_HASH( PCB_VIA ), _HKI( "Layer Top" ) )->SetChoices( layersCu );
    m_propMgr.GetProperty( TYPE_HASH( PCB_VIA ), _HKI( "Layer Bottom" ) )->SetChoices( layersCu );
    m_propMgr.GetProperty( TYPE_HASH( PCB_VIA ), _HKI( "Bottom Backdrill Must-Cut" ) )->SetChoices( layersCu );
    m_propMgr.GetProperty( TYPE_HASH( PCB_VIA ), _HKI( "Top Backdrill Must-Cut" ) )->SetChoices( layersCu );
    m_propMgr.GetProperty( TYPE_HASH( PCB_TUNING_PATTERN ), _HKI( "Layer" ) )->SetChoices( layersCu );

    // Regenerate nets

    std::vector<std::pair<wxString, int>> netNames;
    netNames.reserve( aBoard->GetNetInfo().NetsByNetcode().size() );

    for( const auto& [ netCode, netInfo ] : aBoard->GetNetInfo().NetsByNetcode() )
        netNames.emplace_back( netCode == 0 ? _( "No Net" ) : UnescapeString( netInfo->GetNetname() ),
                               netCode );

    std::sort( netNames.begin(), netNames.end(),
               []( const auto& a, const auto& b )
               {
                   // "No Net" first, as in Altium's net lists
                   if( ( a.second == 0 ) != ( b.second == 0 ) )
                       return a.second == 0;

                   return a.first.CmpNoCase( b.first ) < 0;
               } );

    for( const auto& [ netName, netCode ] : netNames )
        nets.Add( netName, netCode );

    auto netProperty = m_propMgr.GetProperty( TYPE_HASH( BOARD_CONNECTED_ITEM ), _HKI( "Net" ) );
    netProperty->SetChoices( nets );

    auto tuningNet = m_propMgr.GetProperty( TYPE_HASH( PCB_TUNING_PATTERN ), _HKI( "Net" ) );
    tuningNet->SetChoices( nets );
}


bool PCB_PROPERTIES_PANEL::getItemValue( EDA_ITEM* aItem, PROPERTY_BASE* aProperty, wxVariant& aValue )
{
    // For FOOTPRINT variant-aware boolean properties, return variant-specific values
    if( aItem->Type() == PCB_FOOTPRINT_T )
    {
        FOOTPRINT*      footprint = static_cast<FOOTPRINT*>( aItem );
        const wxString& propName = aProperty->Name();
        wxString        variantName;

        if( footprint->GetBoard() )
            variantName = footprint->GetBoard()->GetCurrentVariant();

        if( propName == _HKI( "Do not Populate" ) )
        {
            aValue = wxVariant( footprint->GetDNPForVariant( variantName ) );
            return true;
        }
        else if( propName == _HKI( "Exclude From Bill of Materials" ) )
        {
            aValue = wxVariant( footprint->GetExcludedFromBOMForVariant( variantName ) );
            return true;
        }
        else if( propName == _HKI( "Exclude From Position Files" ) )
        {
            aValue = wxVariant( footprint->GetExcludedFromPosFilesForVariant( variantName ) );
            return true;
        }
    }

    // On the board a component's texts show what they print ("D1", not "${REFERENCE}"), as
    // Altium's designator does.  The footprint editor keeps the variables.
    if( m_frame->IsType( FRAME_PCB_EDITOR )
            && ( aItem->Type() == PCB_TEXT_T || aItem->Type() == PCB_FIELD_T )
            && aProperty->Name() == _HKI( "Text" ) )
    {
        PCB_TEXT* text = static_cast<PCB_TEXT*>( aItem );

        if( text->GetText().Contains( wxS( "${" ) ) )
        {
            aValue = wxVariant( text->GetShownText( true ) );
            return true;
        }
    }

    return PROPERTIES_PANEL::getItemValue( aItem, aProperty, aValue );
}


ROUTER_TOOL* PCB_PROPERTIES_PANEL::activeRouter() const
{
    if( !m_frame->IsType( FRAME_PCB_EDITOR ) )
        return nullptr;

    ROUTER_TOOL* router = m_frame->GetToolManager()->GetTool<ROUTER_TOOL>();

    if( router && router->IsToolActive() && router->RoutingInProgress()
            && router->Router()->Mode() == PNS::PNS_MODE_ROUTE_SINGLE )
    {
        return router;
    }

    return nullptr;
}


void PCB_PROPERTIES_PANEL::appendRoutingProperties()
{
    // Altium's Tab while routing: the panel is the Interactive Routing page (its sections are
    // the "routing" layout, pcb_properties_layouts.cpp); the board's rows and sections are
    // hidden meanwhile (categoryTab, documentLayout)
    bool routing = activeRouter() != nullptr;

    if( routing )
        showDocumentWidget( false );

    if( routing != m_routingShown )
    {
        m_routingShown = routing;
        applyFilter();
        fitGridHeight();
    }

    // The base panel picked the sections before it entered document mode; show this page's
    updateSelectionSections();
}
