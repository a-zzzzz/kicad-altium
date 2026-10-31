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

#include <board_stackup_manager/layer_stack_manager_frame.h>

#include <base_units.h>
#include <bitmaps.h>
#include <board.h>
#include <board_design_settings.h>
#include <collectors.h>
#include <confirm.h>
#include <eda_units.h>
#include <footprint.h>
#include <layer_pairs.h>
#include <netclass.h>
#include <pad.h>
#include <pcb_draw_panel_gal.h>
#include <pcb_edit_frame.h>
#include <project.h>
#include <project/net_settings.h>
#include <project/project_file.h>
#include <string_utils.h>
#include <tool/actions.h>
#include <tool/tool_base.h>
#include <tool/tool_event.h>
#include <tool/tool_manager.h>
#include <ui_events.h>
#include <widgets/altium_chrome.h>
#include <widgets/altium_layer_tabs.h>
#include <widgets/document_tab_bar.h>

#include <board_stackup_manager/dialog_dielectric_list_manager.h>
#include <board_stackup_manager/dielectric_material.h>
#include <board_stackup_manager/stackup_predefined_prms.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/colordlg.h>
#include <wx/control.h>
#include <wx/dcbuffer.h>
#include <wx/filename.h>
#include <wx/grid.h>
#include <wx/listbox.h>
#include <wx/menu.h>
#include <wx/panel.h>
#include <wx/settings.h>
#include <wx/simplebook.h>
#include <wx/splitter.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/toolbar.h>

#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#endif


namespace
{

enum COLUMNS
{
    COL_NUM = 0,
    COL_NAME,
    COL_MATERIAL,
    COL_TYPE,
    COL_WEIGHT,
    COL_THICKNESS,
    COL_DK,
    COL_DF,
    COL_COUNT,
    COL_COLOR       ///< Not a grid column: the colour is edited in the Properties panel
};


enum IDS
{
    ID_ADD_PAIR = wxID_HIGHEST + 1300,
    ID_INS_DIEL_ABOVE,
    ID_INS_DIEL_BELOW,
    ID_DELETE,
    ID_MOVE_UP,
    ID_MOVE_DOWN,
    ID_UNDO,
    ID_REDO,
    ID_SAVE,
    ID_REVERT,
    ID_IMPEDANCE,
    ID_CHOOSE_MATERIAL,
    ID_ADD_TECH_FIRST,
    ID_ADD_TECH_LAST = ID_ADD_TECH_FIRST + 5,
    ID_VIEW_STACKUP,
    ID_VIEW_IMPEDANCE,
    ID_VIEW_VIA_TYPES,
    ID_CLOSE_DOCUMENT,
    ID_MENU_BUTTON,
    ID_SUBSTACK_ADD,
    ID_SUBSTACK_DELETE
};


// Mask, paste and overlay layers that can be added to / removed from the stack
const PCB_LAYER_ID TECH_LAYERS[] = { F_SilkS, F_Paste, F_Mask, B_Mask, B_Paste, B_SilkS };


wxColour sysColour( wxSystemColour aIndex )
{
    return wxSystemSettings::GetColour( aIndex );
}


wxColour blend( const wxColour& aA, const wxColour& aB, double aFactor )
{
    auto mix = [&]( unsigned char a, unsigned char b )
               {
                   return (unsigned char) ( a + ( b - a ) * aFactor + 0.5 );
               };

    return wxColour( mix( aA.Red(), aB.Red() ), mix( aA.Green(), aB.Green() ),
                     mix( aA.Blue(), aB.Blue() ) );
}


// Altium colours whole rows by the kind of layer: copper orange, dielectrics a khaki grey,
// mask and overlay layers dark green; the text is light on all of them
wxColour rowBackground( BOARD_STACKUP_ITEM_TYPE aType )
{
    switch( aType )
    {
    case BS_ITEM_TYPE_COPPER:     return wxColour( 0xC9, 0x7E, 0x4D );
    case BS_ITEM_TYPE_DIELECTRIC: return wxColour( 0x7A, 0x76, 0x6A );
    default:                      return wxColour( 0x2E, 0x6A, 0x3E );
    }
}


wxColour rowText()
{
    return wxColour( 0xF2, 0xF2, 0xF2 );
}


/// Copper weight in oz/ft^2 from a copper thickness (1 oz = 35 um), rounded to a usual value.
wxString copperWeight( int aThicknessIU )
{
    double oz = aThicknessIU / 1e6 / 0.035;

    if( oz <= 0.0 )
        return wxEmptyString;

    double rounded = std::round( oz * 4.0 ) / 4.0;

    if( rounded <= 0.0 )
        rounded = 0.25;

    wxString text = wxString::FromCDouble( rounded, 2 );

    while( text.Contains( '.' ) && ( text.EndsWith( "0" ) || text.EndsWith( "." ) ) )
        text.RemoveLast();

    return text + wxS( "oz" );
}


wxColour itemColour( const BOARD_STACKUP_ITEM* aItem, int aSub )
{
    BOARD_STACKUP_ITEM_TYPE type = aItem->GetType();

    if( type == BS_ITEM_TYPE_COPPER )
        return wxColour( 220, 180, 30 );

    if( type == BS_ITEM_TYPE_SOLDERPASTE )
        return wxColour( 200, 200, 200 );

    wxString colour = aItem->GetColor( aSub );

    if( colour.StartsWith( wxT( "#" ) ) )
        return KIGFX::COLOR4D( colour ).WithAlpha( 1.0 ).ToColour();

    const std::vector<FAB_LAYER_COLOR>& standard = GetStandardColors( type );

    // Index 0 is "Not specified": use a typical colour for the kind of layer instead
    for( size_t ii = 1; ii < standard.size(); ++ii )
    {
        if( standard[ii].GetName() == colour )
            return standard[ii].GetColor( type ).WithAlpha( 1.0 ).ToColour();
    }

    switch( type )
    {
    case BS_ITEM_TYPE_SOLDERMASK: return wxColour( 60, 150, 80 );
    case BS_ITEM_TYPE_SILKSCREEN: return wxColour( 235, 235, 235 );
    default:                      return wxColour( 109, 116, 75 );
    }
}


DIELECTRIC_SUBSTRATE_LIST::DL_MATERIAL_LIST_TYPE materialListType( BOARD_STACKUP_ITEM_TYPE aType )
{
    switch( aType )
    {
    case BS_ITEM_TYPE_SOLDERMASK: return DIELECTRIC_SUBSTRATE_LIST::DL_MATERIAL_SOLDERMASK;
    case BS_ITEM_TYPE_SILKSCREEN: return DIELECTRIC_SUBSTRATE_LIST::DL_MATERIAL_SILKSCREEN;
    default:                      return DIELECTRIC_SUBSTRATE_LIST::DL_MATERIAL_DIELECTRIC;
    }
}


// Copper layer types, in the order shown in the Type column
const LAYER_T COPPER_TYPES[] = { LT_SIGNAL, LT_POWER, LT_MIXED, LT_JUMPER };

wxString copperTypeName( LAYER_T aType )
{
    switch( aType )
    {
    case LT_POWER:  return _( "Plane" );
    case LT_MIXED:  return _( "Mixed" );
    case LT_JUMPER: return _( "Jumper" );
    default:        return _( "Signal" );
    }
}


wxString materialDisplay( const wxString& aMaterial )
{
    return IsPrmSpecified( aMaterial ) ? aMaterial : wxGetTranslation( NotSpecifiedPrm() );
}


wxString colourDisplay( const wxString& aColour )
{
    if( aColour.StartsWith( wxT( "#" ) ) )
        return aColour;

    return wxGetTranslation( IsPrmSpecified( aColour ) ? aColour : NotSpecifiedPrm() );
}


wxString customColourLabel()
{
    return _( "Custom..." );
}


void copySublayer( BOARD_STACKUP_ITEM* aItem, int aFrom, int aTo )
{
    aItem->SetMaterial( aItem->GetMaterial( aFrom ), aTo );
    aItem->SetThickness( aItem->GetThickness( aFrom ), aTo );
    aItem->SetThicknessLocked( aItem->IsThicknessLocked( aFrom ), aTo );
    aItem->SetEpsilonR( aItem->GetEpsilonR( aFrom ), aTo );
    aItem->SetLossTangent( aItem->GetLossTangent( aFrom ), aTo );
    aItem->SetColor( aItem->GetColor( aFrom ), aTo );
}


bool stackupsEqual( const BOARD_STACKUP& aA, const BOARD_STACKUP& aB )
{
    // BOARD_STACKUP::operator== doesn't compare list or sub-layer counts
    if( aA.GetCount() != aB.GetCount() )
        return false;

    for( int ii = 0; ii < aA.GetCount(); ++ii )
    {
        if( aA.GetList()[ii]->GetSublayersCount() != aB.GetList()[ii]->GetSublayersCount() )
            return false;
    }

    return aA == aB;
}


/// Width of the "..." material button drawn at the right of a Material cell.
int materialButtonWidth( const wxWindow& aWindow )
{
    return aWindow.FromDIP( 22 );
}


/// Altium-style stack cell: the row's layer colour across the whole cell, light text, the
/// selected row outlined instead of filled, and optionally a small "..." button at the right
/// (the material library).
class STACK_CELL_RENDERER : public wxGridCellStringRenderer
{
public:
    explicit STACK_CELL_RENDERER( bool aButton = false ) :
            m_button( aButton )
    {
    }

    void Draw( wxGrid& aGrid, wxGridCellAttr& aAttr, wxDC& aDC, const wxRect& aRect, int aRow,
               int aCol, bool ) override
    {
        wxColour bg = aAttr.GetBackgroundColour();

        aDC.SetPen( *wxTRANSPARENT_PEN );
        aDC.SetBrush( wxBrush( bg ) );
        aDC.DrawRectangle( aRect );

        wxRect text = aRect;
        text.Deflate( aGrid.FromDIP( 6 ), 0 );

        if( m_button )
        {
            int    w = materialButtonWidth( aGrid );
            wxRect button( aRect.GetRight() - w - aGrid.FromDIP( 3 ), aRect.y + aGrid.FromDIP( 4 ),
                           w, aRect.height - aGrid.FromDIP( 8 ) );

            aDC.SetPen( wxPen( wxColour( 0x2A, 0x2A, 0x2A ) ) );
            aDC.SetBrush( wxBrush( wxColour( 0x4A, 0x4A, 0x4A ) ) );
            aDC.DrawRoundedRectangle( button, aGrid.FromDIP( 3 ) );

            aDC.SetFont( aAttr.GetFont() );
            aDC.SetTextForeground( wxColour( 0xDD, 0xDD, 0xDD ) );
            wxString dots = wxS( "\u2026" );
            wxSize   ext = aDC.GetTextExtent( dots );
            aDC.DrawText( dots, button.x + ( button.width - ext.x ) / 2,
                          button.y + ( button.height - ext.y ) / 2 - aGrid.FromDIP( 2 ) );

            text.width -= w + aGrid.FromDIP( 6 );
        }

        aDC.SetFont( aAttr.GetFont() );
        aDC.SetTextForeground( aAttr.GetTextColour() );

        int hAlign, vAlign;
        aAttr.GetAlignment( &hAlign, &vAlign );

        wxString value = wxControl::Ellipsize( aGrid.GetCellValue( aRow, aCol ), aDC,
                                               wxELLIPSIZE_END, std::max( 0, text.width ) );
        wxDCClipper clip( aDC, aRect );
        aDC.DrawLabel( value, text, hAlign | wxALIGN_CENTER_VERTICAL );

        // Altium outlines the selected row in light blue and keeps its colour
        bool selected = false;

        for( int row : aGrid.GetSelectedRows() )
            selected |= row == aRow;

        selected |= aGrid.GetGridCursorRow() == aRow && aGrid.GetSelectedRows().IsEmpty();

        if( selected )
        {
            aDC.SetPen( wxPen( wxColour( 0x7F, 0xB8, 0xE8 ), aGrid.FromDIP( 1 ) ) );
            aDC.DrawLine( aRect.x, aRect.y, aRect.GetRight() + 1, aRect.y );
            aDC.DrawLine( aRect.x, aRect.GetBottom(), aRect.GetRight() + 1, aRect.GetBottom() );

            if( aCol == 0 )
                aDC.DrawLine( aRect.x, aRect.y, aRect.x, aRect.GetBottom() );

            if( aCol == aGrid.GetNumberCols() - 1 )
                aDC.DrawLine( aRect.GetRight(), aRect.y, aRect.GetRight(), aRect.GetBottom() );
        }
    }

    wxSize GetBestSize( wxGrid& aGrid, wxGridCellAttr& aAttr, wxDC& aDC, int aRow,
                        int aCol ) override
    {
        wxSize size = wxGridCellStringRenderer::GetBestSize( aGrid, aAttr, aDC, aRow, aCol );

        if( m_button )
            size.x += materialButtonWidth( aGrid ) + aGrid.FromDIP( 6 );

        return size;
    }

    wxGridCellRenderer* Clone() const override
    {
        return new STACK_CELL_RENDERER( m_button );
    }

private:
    bool m_button;
};


/// Altium-style tab strip under the table: Stackup, Impedance, Via Types.
class VIEW_TABS : public wxPanel
{
public:
    VIEW_TABS( wxWindow* aParent, std::function<void( int )> aOnSelect ) :
            wxPanel( aParent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE ),
            m_selected( 0 ),
            m_onSelect( std::move( aOnSelect ) )
    {
        m_labels = { _( "Stackup" ), _( "Impedance" ), _( "Via Types" ) };

        SetBackgroundStyle( wxBG_STYLE_PAINT );
        SetMinSize( wxSize( -1, FromDIP( 24 ) ) );

        Bind( wxEVT_PAINT, &VIEW_TABS::onPaint, this );
        Bind( wxEVT_LEFT_DOWN,
              [this]( wxMouseEvent& aEvent )
              {
                  std::vector<wxRect> rects = tabRects();

                  for( int ii = 0; ii < (int) rects.size(); ++ii )
                  {
                      if( rects[ii].Contains( aEvent.GetPosition() ) && ii != m_selected )
                          m_onSelect( ii );
                  }
              } );
    }

    void SetSelected( int aIndex )
    {
        m_selected = aIndex;
        Refresh();
    }

private:
    std::vector<wxRect> tabRects()
    {
        wxClientDC          dc( this );
        std::vector<wxRect> rects;
        int                 x = FromDIP( 4 );

        dc.SetFont( GetFont() );

        for( const wxString& label : m_labels )
        {
            int width = dc.GetTextExtent( label ).x + FromDIP( 14 );
            rects.emplace_back( x, 0, width, GetClientSize().y - 2 );
            x += width + FromDIP( 2 );
        }

        return rects;
    }

    void onPaint( wxPaintEvent& )
    {
        // Altium's small document-view tabs at the bottom left: a dark strip, the open view a
        // lighter rounded box, no accent line
        wxAutoBufferedPaintDC dc( this );
        wxColour              face = sysColour( wxSYS_COLOUR_BTNFACE );
        wxColour              strip = blend( face, *wxBLACK, 0.17 );

        dc.SetBackground( wxBrush( strip ) );
        dc.Clear();
        dc.SetFont( GetFont() );

        std::vector<wxRect> rects = tabRects();

        for( int ii = 0; ii < (int) rects.size(); ++ii )
        {
            wxRect tab = rects[ii];
            bool   active = ii == m_selected;
            wxSize text = dc.GetTextExtent( m_labels[ii] );

            tab.Deflate( 0, FromDIP( 2 ) );

            if( active )
            {
                dc.SetPen( wxPen( blend( strip, *wxBLACK, 0.40 ) ) );
                dc.SetBrush( wxBrush( blend( face, *wxWHITE, 0.07 ) ) );
                dc.DrawRoundedRectangle( tab, FromDIP( 3 ) );
            }

            dc.SetTextForeground( sysColour( wxSYS_COLOUR_BTNTEXT ) );
            dc.DrawText( m_labels[ii], tab.x + ( tab.width - text.x ) / 2,
                         tab.y + ( tab.height - text.y ) / 2 );
        }
    }

    std::vector<wxString>      m_labels;
    int                        m_selected;
    std::function<void( int )> m_onSelect;
};


/// Widens the columns so the table fills the grid's width.  Every column gets a share of the
/// spare width in proportion to its base width; the stretchable ones get a triple share, so on
/// a wide window the numeric columns grow too instead of sitting squashed at the far right.
void fitColumns( wxGrid* aGrid, const std::vector<int>& aBase, const std::vector<int>& aStretch )
{
    if( aBase.empty() )
        return;

    int total = 0;

    for( int width : aBase )
        total += width;

    int extra = std::max( 0, aGrid->GetClientSize().x - total - 1 );

    auto weight =
            [&]( int col ) -> long long
            {
                // Narrow columns (colour swatch, layer number) keep their width
                if( aBase[col] < aGrid->FromDIP( 60 ) )
                    return 0;

                bool stretch = std::find( aStretch.begin(), aStretch.end(), col ) != aStretch.end();
                return (long long) aBase[col] * ( stretch ? 3 : 1 );
            };

    long long weights = 0;

    for( int col = 0; col < (int) aBase.size(); ++col )
        weights += weight( col );

    for( int col = 0; col < (int) aBase.size() && col < aGrid->GetNumberCols(); ++col )
    {
        int width = aBase[col];

        if( weights > 0 )
            width += (int) ( extra * weight( col ) / weights );

        if( aGrid->GetColSize( col ) != width )
            aGrid->SetColSize( col, width );
    }
}


// ---------------------------------------------------------------------------------------------
// Impedance: closed-form transmission line approximations
//
// Written for this fork from the published formulas; lengths in mm, impedances in ohms.
//
//  Surface microstrip (IPC-2141A; intended for 0.1 < W/H < 2.0 and 1 < Er < 15):
//      Z0 = 87 / sqrt( Er + 1.41 ) * ln( 5.98 H / ( 0.8 W + T ) )
//      effective Er for the propagation delay: Er_eff = 0.475 Er + 0.67
//  Symmetric stripline (IPC-2141A):
//      Z0 = 60 / sqrt( Er ) * ln( 1.9 B / ( 0.8 W + T ) ),   B = plane-to-plane spacing
//  Offset (asymmetric) stripline: the parallel combination of the two symmetric striplines of
//  spacing 2 H1 + T and 2 H2 + T (Cohn's approximation):
//      Z0 = 2 Z1 Z2 / ( Z1 + Z2 ),   which is the symmetric formula when H1 = H2.
//  Edge-coupled differential pairs (IPC-2141A / National Semiconductor AN-905):
//      microstrip  Zdiff = 2 Z0 ( 1 - 0.48 exp( -0.96 S / H ) )
//      stripline   Zdiff = 2 Z0 ( 1 - 0.347 exp( -2.9 S / B ) )
//  Propagation delay: tpd = sqrt( Er_eff ) / c.
//
// The width for a target impedance is found by bisection (impedance falls as width grows).
// ---------------------------------------------------------------------------------------------

struct LINE_GEOMETRY
{
    bool   stripline = false;
    double er = 4.2;       ///< thickness-weighted Dk of the dielectric(s) to the reference(s)
    double h1 = 0.0;       ///< dielectric height to the (first) reference plane, mm
    double h2 = 0.0;       ///< stripline: dielectric height to the second reference plane, mm
    double t = 0.035;      ///< copper thickness, mm
};


double singleEndedZ( const LINE_GEOMETRY& aGeom, double aWidth )
{
    auto zSym =
            [&]( double aB )
            {
                double arg = 1.9 * aB / ( 0.8 * aWidth + aGeom.t );
                return arg > 1.0 ? 60.0 / std::sqrt( aGeom.er ) * std::log( arg ) : 0.0;
            };

    if( aGeom.stripline )
    {
        double z1 = zSym( 2.0 * aGeom.h1 + aGeom.t );
        double z2 = zSym( 2.0 * aGeom.h2 + aGeom.t );
        return ( z1 + z2 ) > 0.0 ? 2.0 * z1 * z2 / ( z1 + z2 ) : 0.0;
    }

    double arg = 5.98 * aGeom.h1 / ( 0.8 * aWidth + aGeom.t );
    return arg > 1.0 ? 87.0 / std::sqrt( aGeom.er + 1.41 ) * std::log( arg ) : 0.0;
}


double differentialZ( const LINE_GEOMETRY& aGeom, double aWidth, double aGap )
{
    double z0 = singleEndedZ( aGeom, aWidth );

    if( aGeom.stripline )
    {
        double b = aGeom.h1 + aGeom.h2 + aGeom.t;
        return 2.0 * z0 * ( 1.0 - 0.347 * std::exp( -2.9 * aGap / b ) );
    }

    return 2.0 * z0 * ( 1.0 - 0.48 * std::exp( -0.96 * aGap / aGeom.h1 ) );
}


double delayPsPerMm( const LINE_GEOMETRY& aGeom )
{
    double erEff = aGeom.stripline ? aGeom.er : 0.475 * aGeom.er + 0.67;
    return std::sqrt( erEff ) / 299.792458 * 1000.0;     // c = 299.79 mm/ns
}


/// Width (mm) giving aTarget ohms, or a negative number if out of reach.
double solveWidth( const LINE_GEOMETRY& aGeom, double aTarget, bool aDiff, double aGap )
{
    auto z = [&]( double aWidth )
             {
                 return aDiff ? differentialZ( aGeom, aWidth, aGap )
                              : singleEndedZ( aGeom, aWidth );
             };

    double lo = 0.001;
    double hi = std::max( 0.01, 20.0 * std::max( aGeom.h1, aGeom.h2 ) );

    if( aTarget <= 0.0 || z( lo ) < aTarget || z( hi ) > aTarget )
        return -1.0;

    for( int ii = 0; ii < 60; ++ii )
    {
        double mid = 0.5 * ( lo + hi );

        if( z( mid ) > aTarget )
            lo = mid;
        else
            hi = mid;
    }

    return 0.5 * ( lo + hi );
}


enum IMP_COLUMNS
{
    IMP_LAYER = 0,
    IMP_TOP_REF,
    IMP_BOTTOM_REF,
    IMP_WIDTH,
    IMP_GAP,
    IMP_IMPEDANCE,
    IMP_DEVIATION,
    IMP_DELAY,
    IMP_COUNT
};


enum VIA_COLUMNS
{
    VIA_NAME = 0,
    VIA_KIND,
    VIA_FIRST,
    VIA_LAST,
    VIA_ENABLED,
    VIA_COUNT
};


bool sameViaTypes( const std::vector<LAYER_PAIR_INFO>& aA, const std::vector<LAYER_PAIR_INFO>& aB )
{
    if( aA.size() != aB.size() )
        return false;

    for( size_t ii = 0; ii < aA.size(); ++ii )
    {
        const LAYER_PAIR& a = aA[ii].GetLayerPair();
        const LAYER_PAIR& b = aB[ii].GetLayerPair();

        if( a.GetLayerA() != b.GetLayerA() || a.GetLayerB() != b.GetLayerB()
                || aA[ii].IsEnabled() != aB[ii].IsEnabled()
                || aA[ii].GetName() != aB[ii].GetName() )
        {
            return false;
        }
    }

    return true;
}


#ifdef __WXMSW__
/// Colours the native caption like the editors' Altium title bar (Windows 11 DWM attributes).
void styleNativeCaption( wxTopLevelWindow* aWindow )
{
    using SET_ATTRIBUTE = HRESULT( WINAPI* )( HWND, DWORD, LPCVOID, DWORD );

    static HMODULE dwm = ::LoadLibraryW( L"dwmapi.dll" );

    if( !dwm )
        return;

    static SET_ATTRIBUTE setAttribute =
            reinterpret_cast<SET_ATTRIBUTE>(
                    reinterpret_cast<void*>( ::GetProcAddress( dwm, "DwmSetWindowAttribute" ) ) );

    if( !setAttribute )
        return;

    ALTIUM_CHROME_COLOURS colours = ALTIUM_CHROME_COLOURS::Get();
    COLORREF caption = RGB( colours.background.Red(), colours.background.Green(),
                            colours.background.Blue() );
    COLORREF text = RGB( colours.text.Red(), colours.text.Green(), colours.text.Blue() );
    HWND     hwnd = (HWND) aWindow->GetHWND();

    // DWMWA_BORDER_COLOR = 34, DWMWA_CAPTION_COLOR = 35, DWMWA_TEXT_COLOR = 36
    setAttribute( hwnd, 34, &caption, sizeof( caption ) );
    setAttribute( hwnd, 35, &caption, sizeof( caption ) );
    setAttribute( hwnd, 36, &text, sizeof( text ) );
}
#endif

} // namespace


bool LAYER_STACK_MANAGER_FRAME::STATE::operator==( const STATE& aOther ) const
{
    return m_layers == aOther.m_layers && m_names == aOther.m_names
           && m_types == aOther.m_types && stackupsEqual( m_stackup, aOther.m_stackup )
           && m_profiles == aOther.m_profiles && sameViaTypes( m_viaTypes, aOther.m_viaTypes );
}


int LAYER_STACK_MANAGER_FRAME::STATE::CopperCount() const
{
    return (int) ( m_layers & LSET::AllCuMask() ).count();
}


void LAYER_STACK_MANAGER_FRAME::ShowFor( PCB_EDIT_FRAME* aFrame )
{
    for( wxWindow* win : wxTopLevelWindows )
    {
        LAYER_STACK_MANAGER_FRAME* lsm = dynamic_cast<LAYER_STACK_MANAGER_FRAME*>( win );

        if( lsm && lsm->m_frame == aFrame && lsm->m_frameAlive && !lsm->IsBeingDeleted() )
        {
            if( lsm->IsIconized() )
                lsm->Iconize( false );

            lsm->Show();
            lsm->Raise();
            return;
        }
    }

    LAYER_STACK_MANAGER_FRAME* lsm = new LAYER_STACK_MANAGER_FRAME( aFrame );

    // Open in the editor's place, like switching to another document tab
    if( aFrame->IsMaximized() )
        lsm->Maximize();
    else
        lsm->SetSize( aFrame->GetRect() );

    lsm->Show();
    lsm->Raise();
}


LAYER_STACK_MANAGER_FRAME::LAYER_STACK_MANAGER_FRAME( PCB_EDIT_FRAME* aFrame ) :
        // No parent: an owned window would always sit above the editor, which breaks
        // switching between the two with the document tabs
        wxFrame( nullptr, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                 wxDEFAULT_FRAME_STYLE ),
        m_frame( aFrame ),
        m_board( aFrame->GetBoard() ),
        m_frameAlive( true ),
        m_titleBar( nullptr ),
        m_menuStrip( nullptr ),
        m_toolbar( nullptr ),
        m_impedance( nullptr ),
        m_props( nullptr ),
        m_propName( nullptr ),
        m_propComment( nullptr ),
        m_propColour( nullptr ),
        m_propColourLabel( nullptr ),
        m_propStackName( nullptr ),
        m_propLayers( nullptr ),
        m_propDielectrics( nullptr ),
        m_propConductive( nullptr ),
        m_propDielectricThk( nullptr ),
        m_propTotal( nullptr ),
        m_book( nullptr ),
        m_viewTabs( nullptr ),
        m_view( 0 ),
        m_grid( nullptr ),
        m_profileList( nullptr ),
        m_profileName( nullptr ),
        m_profileType( nullptr ),
        m_profileTarget( nullptr ),
        m_profileDelete( nullptr ),
        m_profileRecalc( nullptr ),
        m_impGrid( nullptr ),
        m_profileSel( 0 ),
        m_viaGrid( nullptr ),
        m_viaDelete( nullptr ),
        m_viaInfo( nullptr ),
        m_status( nullptr ),
        m_layerCount( nullptr ),
        m_thickness( nullptr )
{
    SetIcons( aFrame->GetIcons() );
    SetMinSize( FromDIP( wxSize( 640, 360 ) ) );

    m_saved = loadFromBoard();
    m_state = m_saved;

    buildMenuBar();

    wxBoxSizer* frameSizer = new wxBoxSizer( wxVERTICAL );

#ifdef __WXMSW__
    // Altium's title bar and menu row, as in the editor windows
    m_titleBar = new ALTIUM_TITLE_BAR( this );
    m_menuStrip = new ALTIUM_MENU_STRIP( this );
    frameSizer->Add( m_titleBar, 0, wxEXPAND );
    frameSizer->Add( m_menuStrip, 0, wxEXPAND );
#endif

    // The document tabs, as in the editor windows (the tab bar must be a child of the frame)
    frameSizer->Add( new DOCUMENT_TAB_BAR( this ), 0, wxEXPAND );

    wxPanel* root = new wxPanel( this );
    root->SetBackgroundColour( sysColour( wxSYS_COLOUR_BTNFACE ) );
    frameSizer->Add( root, 1, wxEXPAND );

    wxBoxSizer* rootSizer = new wxBoxSizer( wxVERTICAL );

    buildToolbar( root );
    rootSizer->Add( m_toolbar, 0, wxEXPAND );

    // The three views share the toolbar, footer and undo history
    m_book = new wxSimplebook( root );

    wxPanel*    stackupPage = new wxPanel( m_book );
    wxBoxSizer* stackupSizer = new wxBoxSizer( wxVERTICAL );
    buildGrid( stackupPage );
    stackupSizer->Add( m_grid, 1, wxEXPAND );
    stackupPage->SetSizer( stackupSizer );

    m_book->AddPage( stackupPage, _( "Stackup" ) );
    m_book->AddPage( buildImpedancePage( m_book ), _( "Impedance" ) );
    m_book->AddPage( buildViaTypesPage( m_book ), _( "Via Types" ) );

    // Altium's Properties panel for the stack sits beside the table
    wxBoxSizer* body = new wxBoxSizer( wxHORIZONTAL );
    body->Add( m_book, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP( 2 ) );
    body->Add( buildProperties( root ), 0, wxEXPAND | wxLEFT, FromDIP( 4 ) );
    rootSizer->Add( body, 1, wxEXPAND );

    // Footer: messages at the left, layer count and total thickness at the right
    wxBoxSizer* footer = new wxBoxSizer( wxHORIZONTAL );
    m_status = new wxStaticText( root, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                 wxST_ELLIPSIZE_END );
    m_layerCount = new wxStaticText( root, wxID_ANY, wxEmptyString );
    m_thickness = new wxStaticText( root, wxID_ANY, wxEmptyString );
    m_thickness->SetFont( m_thickness->GetFont().Bold() );

    footer->Add( m_status, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP( 8 ) );
    footer->Add( m_layerCount, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP( 16 ) );
    footer->Add( m_thickness, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP( 8 ) );
    rootSizer->Add( footer, 0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP( 4 ) );

    m_viewTabs = new VIEW_TABS( root, [this]( int aView ) { showView( aView ); } );
    rootSizer->Add( m_viewTabs, 0, wxEXPAND );

    root->SetSizer( rootSizer );
    SetSizer( frameSizer );

    // Commands (toolbar tools send wxEVT_MENU too)
    Bind( wxEVT_MENU, [this]( wxCommandEvent& ) { addLayerPair(); }, ID_ADD_PAIR );
    Bind( wxEVT_MENU, [this]( wxCommandEvent& ) { insertDielectric( true ); }, ID_INS_DIEL_ABOVE );
    Bind( wxEVT_MENU, [this]( wxCommandEvent& ) { insertDielectric( false ); }, ID_INS_DIEL_BELOW );
    Bind( wxEVT_MENU, [this]( wxCommandEvent& ) { deleteLayer(); }, ID_DELETE );
    Bind( wxEVT_MENU, [this]( wxCommandEvent& ) { moveSublayer( -1 ); }, ID_MOVE_UP );
    Bind( wxEVT_MENU, [this]( wxCommandEvent& ) { moveSublayer( 1 ); }, ID_MOVE_DOWN );
    Bind( wxEVT_MENU, [this]( wxCommandEvent& ) { undo(); }, ID_UNDO );
    Bind( wxEVT_MENU, [this]( wxCommandEvent& ) { redo(); }, ID_REDO );
    Bind( wxEVT_MENU, [this]( wxCommandEvent& ) { applyToBoard(); }, ID_SAVE );
    Bind( wxEVT_MENU, [this]( wxCommandEvent& ) { revert(); }, ID_REVERT );
    Bind( wxEVT_MENU, [this]( wxCommandEvent& ) { chooseMaterial( selectedRow() ); },
          ID_CHOOSE_MATERIAL );
    Bind( wxEVT_MENU, [this]( wxCommandEvent& ) { showView( 0 ); }, ID_VIEW_STACKUP );
    Bind( wxEVT_MENU, [this]( wxCommandEvent& ) { showView( 1 ); }, ID_VIEW_IMPEDANCE );
    Bind( wxEVT_MENU, [this]( wxCommandEvent& ) { showView( 2 ); }, ID_VIEW_VIA_TYPES );
    Bind( wxEVT_MENU, [this]( wxCommandEvent& ) { Close(); }, ID_CLOSE_DOCUMENT );

    // The menu's Impedance Controlled item mirrors the toolbar's check box
    Bind( wxEVT_MENU,
          [this]( wxCommandEvent& aEvent )
          {
              STATE before = m_state;
              m_state.m_stackup.m_HasDielectricConstrains = aEvent.IsChecked();
              commitChange( before );
          },
          ID_IMPEDANCE );
    Bind( wxEVT_MENU,
          [this]( wxCommandEvent& aEvent )
          {
              addTechLayer( TECH_LAYERS[aEvent.GetId() - ID_ADD_TECH_FIRST] );
          },
          ID_ADD_TECH_FIRST, ID_ADD_TECH_LAST );

    Bind( wxEVT_TOOL_DROPDOWN,
          [this]( wxCommandEvent& )
          {
              wxMenu menu;
              fillAddMenu( menu );
              PopupMenu( &menu );
          },
          ID_ADD_PAIR );

    Bind( wxEVT_CHECKBOX,
          [this]( wxCommandEvent& )
          {
              STATE before = m_state;
              m_state.m_stackup.m_HasDielectricConstrains = m_impedance->GetValue();
              commitChange( before );
          },
          ID_IMPEDANCE );

    wxAcceleratorEntry accels[] = {
        { wxACCEL_CTRL, 'S', ID_SAVE },
        { wxACCEL_CTRL, 'Z', ID_UNDO },
        { wxACCEL_CTRL, 'Y', ID_REDO },
        { wxACCEL_CTRL | wxACCEL_SHIFT, 'Z', ID_REDO },
    };
    SetAcceleratorTable( wxAcceleratorTable( (int) std::size( accels ), accels ) );

    Bind( wxEVT_CLOSE_WINDOW, &LAYER_STACK_MANAGER_FRAME::onClose, this );
    Bind( wxEVT_ACTIVATE, &LAYER_STACK_MANAGER_FRAME::onActivate, this );

    // Follow the editor: its unit changes, its closing, and its destruction
    m_frame->Bind( wxEVT_CLOSE_WINDOW, &LAYER_STACK_MANAGER_FRAME::onPcbClose, this );
    m_frame->Bind( wxEVT_DESTROY, &LAYER_STACK_MANAGER_FRAME::onPcbDestroy, this );
    m_frame->Bind( EDA_EVT_UNITS_CHANGED, &LAYER_STACK_MANAGER_FRAME::onUnitsChanged, this );

    refreshGrid();

    if( !m_rows.empty() )
        selectRow( 0, 0 );

    setStatus( _( "Double-click a cell to edit it. Ctrl+S saves the stackup to the board." ) );

    styleCaption();
}


void LAYER_STACK_MANAGER_FRAME::buildMenuBar()
{
    // Altium's Layer Stack Manager has its own document menus
    wxMenu* fileMenu = new wxMenu;
    fileMenu->Append( ID_SAVE, _( "&Save to PCB\tCtrl+S" ),
                      _( "Write the stackup to the board" ) );
    fileMenu->Append( ID_REVERT, _( "&Revert" ),
                      _( "Discard unsaved changes and reload the board's stackup" ) );
    fileMenu->AppendSeparator();
    fileMenu->Append( ID_CLOSE_DOCUMENT, _( "&Close" ), _( "Close the Layer Stack Manager" ) );

    wxMenu* editMenu = new wxMenu;
    editMenu->Append( ID_UNDO, _( "&Undo\tCtrl+Z" ) );
    editMenu->Append( ID_REDO, _( "&Redo\tCtrl+Y" ) );
    editMenu->AppendSeparator();
    editMenu->Append( ID_ADD_PAIR, _( "&Add Copper Layer Pair" ) );
    editMenu->Append( ID_INS_DIEL_ABOVE, _( "Insert Dielectric A&bove" ) );
    editMenu->Append( ID_INS_DIEL_BELOW, _( "Insert Dielectric Be&low" ) );
    editMenu->Append( ID_DELETE, _( "&Delete Layer" ) );
    editMenu->AppendSeparator();
    editMenu->Append( ID_MOVE_UP, _( "Move &Up" ) );
    editMenu->Append( ID_MOVE_DOWN, _( "Move Do&wn" ) );
    editMenu->AppendSeparator();
    editMenu->Append( ID_CHOOSE_MATERIAL, _( "Choose &Material..." ) );

    wxMenu* viewMenu = new wxMenu;
    viewMenu->AppendRadioItem( ID_VIEW_STACKUP, _( "&Stackup" ) );
    viewMenu->AppendRadioItem( ID_VIEW_IMPEDANCE, _( "&Impedance" ) );
    viewMenu->AppendRadioItem( ID_VIEW_VIA_TYPES, _( "&Via Types" ) );

    wxMenu* toolsMenu = new wxMenu;
    toolsMenu->AppendCheckItem( ID_IMPEDANCE, _( "&Impedance Controlled" ),
                                _( "The board has impedance-controlled layers" ) );

    wxMenuBar* menuBar = new wxMenuBar;
    menuBar->Append( fileMenu, _( "&File" ) );
    menuBar->Append( editMenu, _( "&Edit" ) );
    menuBar->Append( viewMenu, _( "&View" ) );
    menuBar->Append( toolsMenu, _( "&Tools" ) );
    SetMenuBar( menuBar );
}


#ifdef __WXMSW__
WXLRESULT LAYER_STACK_MANAGER_FRAME::MSWWindowProc( WXUINT aMsg, WXWPARAM aWParam,
                                                    WXLPARAM aLParam )
{
    return ALTIUM_CHROME::WindowProc( this, m_titleBar, aMsg, aWParam, aLParam,
                                      [this]( WXUINT msg, WXWPARAM wParam, WXLPARAM lParam )
                                      {
                                          return wxFrame::MSWWindowProc( msg, wParam, lParam );
                                      } );
}
#endif


void LAYER_STACK_MANAGER_FRAME::styleCaption()
{
#ifdef __WXMSW__
    // The client-drawn title bar replaces the native caption (and the menu row the native
    // menu bar), as in the editor windows
    if( m_titleBar )
    {
        ALTIUM_CHROME::HideNativeCaption( this );
        ALTIUM_CHROME::BindMenuKeys( this, m_menuStrip );

        // The title bar draws the active / inactive state and the maximise glyph
        auto refreshTitle =
                [this]( wxEvent& aEvent )
                {
                    m_titleBar->Refresh( false );
                    aEvent.Skip();
                };

        Bind( wxEVT_ACTIVATE, refreshTitle );
        Bind( wxEVT_SIZE, refreshTitle );
    }
    else
    {
        styleNativeCaption( this );
    }
#endif
}


void LAYER_STACK_MANAGER_FRAME::showView( int aView )
{
    if( m_grid->IsCellEditControlShown() )
        m_grid->SaveEditControlValue();

    if( m_impGrid->IsCellEditControlShown() )
        m_impGrid->SaveEditControlValue();

    if( m_viaGrid->IsCellEditControlShown() )
        m_viaGrid->SaveEditControlValue();

    m_view = aView;
    m_book->ChangeSelection( aView );
    static_cast<VIEW_TABS*>( m_viewTabs )->SetSelected( aView );
    refreshViews();
    updateChrome();

    switch( aView )
    {
    case 1:
        setStatus( _( "Impedance profiles: enter a target impedance and the trace width (and gap) "
                      "is calculated for each layer from the stackup." ) );
        break;
    case 2:
        setStatus( _( "Via types: the via spans offered while routing (layer pairs)." ) );
        break;
    default:
        setStatus( _( "Double-click a cell to edit it. Ctrl+S saves the stackup to the board." ) );
        break;
    }
}


LAYER_STACK_MANAGER_FRAME::~LAYER_STACK_MANAGER_FRAME()
{
    if( m_frameAlive )
    {
        m_frame->Unbind( wxEVT_CLOSE_WINDOW, &LAYER_STACK_MANAGER_FRAME::onPcbClose, this );
        m_frame->Unbind( wxEVT_DESTROY, &LAYER_STACK_MANAGER_FRAME::onPcbDestroy, this );
        m_frame->Unbind( EDA_EVT_UNITS_CHANGED, &LAYER_STACK_MANAGER_FRAME::onUnitsChanged, this );
    }
}


void LAYER_STACK_MANAGER_FRAME::buildToolbar( wxWindow* aParent )
{
    // Altium's row: the substack chooser with its add / remove buttons, then Add, Modify and
    // Delete, and a menu button at the far right with the rest of the commands
    wxPanel*    bar = new wxPanel( aParent );
    wxBoxSizer* sizer = new wxBoxSizer( wxHORIZONTAL );
    m_toolbar = bar;

    auto icon = []( BITMAPS aBitmap )
                {
                    return KiBitmapBundleDef( aBitmap, 16 );
                };

    auto button =
            [&]( int aId, const wxString& aLabel, BITMAPS aBitmap, const wxString& aTip ) -> wxButton*
            {
                wxButton* btn = new wxButton( bar, aId, aLabel, wxDefaultPosition, wxDefaultSize,
                                              aLabel.IsEmpty() ? wxBU_EXACTFIT : 0 );

                if( aBitmap != BITMAPS::INVALID_BITMAP )
                    btn->SetBitmap( icon( aBitmap ) );

                btn->SetToolTip( aTip );

                if( aLabel.IsEmpty() )
                    btn->SetMinSize( FromDIP( wxSize( 30, 26 ) ) );
                else
                    btn->SetMinSize( wxSize( FromDIP( 92 ), FromDIP( 26 ) ) );

                m_tools[aId] = btn;
                return btn;
            };

    wxChoice* substack = new wxChoice( bar, wxID_ANY );
    substack->Append( _( "Rigid" ) );
    substack->SetSelection( 0 );
    substack->SetMinSize( wxSize( FromDIP( 150 ), -1 ) );
    substack->SetToolTip( _( "The board's substack (flex substacks aren't supported)" ) );

    sizer->Add( substack, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP( 4 ) );
    sizer->Add( button( ID_SUBSTACK_ADD, wxEmptyString, BITMAPS::small_plus,
                        _( "Add a substack (not supported: boards have a single rigid stack)" ) ),
                0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP( 4 ) );
    sizer->Add( button( ID_SUBSTACK_DELETE, wxEmptyString, BITMAPS::small_trash,
                        _( "Delete the substack" ) ),
                0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP( 20 ) );

    sizer->Add( button( ID_ADD_PAIR, _( "Add" ), BITMAPS::small_plus,
                        _( "Add layers: a pair of copper layers (with their dielectrics), a "
                           "dielectric, or a mask or overlay layer" ) ),
                0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP( 6 ) );
    sizer->Add( button( ID_CHOOSE_MATERIAL, _( "Modify" ), BITMAPS::small_edit,
                        _( "Choose the selected layer's material from the material library" ) ),
                0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP( 6 ) );
    sizer->Add( button( ID_DELETE, _( "Delete" ), BITMAPS::small_trash,
                        _( "Delete the selected layer" ) ),
                0, wxALIGN_CENTER_VERTICAL );

    sizer->AddStretchSpacer();
    sizer->Add( button( ID_MENU_BUTTON, wxEmptyString, BITMAPS::panel_menu,
                        _( "More commands: undo, save to the board, move, impedance control" ) ),
                0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP( 4 ) );

    bar->SetSizer( sizer );

    for( auto& [id, window] : m_tools )
    {
        if( id == ID_ADD_PAIR || id == ID_DELETE )
            continue;

        window->Bind( wxEVT_BUTTON, [this, id = id]( wxCommandEvent& )
                      {
                          wxCommandEvent evt( wxEVT_MENU, id );
                          ProcessWindowEvent( evt );
                      } );
    }

    // "Add" and "Delete" act on the open view (layers, impedance profiles or via types); the
    // menu button offers the remaining commands
    for( int id : { ID_ADD_PAIR, ID_DELETE } )
    {
        wxWindow* btn = m_tools[id];
        btn->Bind( wxEVT_BUTTON,
                   [this, id]( wxCommandEvent& )
                   {
                       if( id == ID_ADD_PAIR )
                           onAddButton();
                       else
                           onDeleteButton();
                   } );
    }

    m_tools[ID_MENU_BUTTON]->Bind( wxEVT_BUTTON,
            [this]( wxCommandEvent& )
            {
                wxWindow* btn = m_tools[ID_MENU_BUTTON];
                wxMenu    menu;

                menu.Append( ID_UNDO, _( "Undo" ) + wxS( "\tCtrl+Z" ) );
                menu.Enable( ID_UNDO, !m_undo.empty() );
                menu.Append( ID_REDO, _( "Redo" ) + wxS( "\tCtrl+Y" ) );
                menu.Enable( ID_REDO, !m_redo.empty() );
                menu.AppendSeparator();
                menu.Append( ID_SAVE, _( "Save to PCB" ) + wxS( "\tCtrl+S" ) );
                menu.Enable( ID_SAVE, isDirty() );
                menu.Append( ID_REVERT, _( "Revert" ) );
                menu.Enable( ID_REVERT, isDirty() );
                menu.AppendSeparator();
                menu.Append( ID_MOVE_UP, _( "Move Up" ) );
                menu.Enable( ID_MOVE_UP, m_view == 0 && canMove( -1 ) );
                menu.Append( ID_MOVE_DOWN, _( "Move Down" ) );
                menu.Enable( ID_MOVE_DOWN, m_view == 0 && canMove( 1 ) );
                menu.AppendSeparator();
                menu.AppendCheckItem( ID_IMPEDANCE, _( "Impedance Controlled" ) );
                menu.Check( ID_IMPEDANCE, m_state.m_stackup.m_HasDielectricConstrains );

                btn->PopupMenu( &menu, btn->GetSize().x - FromDIP( 200 ), btn->GetSize().y );
            } );
}


void LAYER_STACK_MANAGER_FRAME::enableTool( int aId, bool aEnable )
{
    auto it = m_tools.find( aId );

    if( it != m_tools.end() && it->second->IsEnabled() != aEnable )
        it->second->Enable( aEnable );
}


wxWindow* LAYER_STACK_MANAGER_FRAME::buildProperties( wxWindow* aParent )
{
    // Laid out like the editors' Properties panel: a caption, then Layer and Substack sections
    // on slightly lighter blocks
    wxColour face = sysColour( wxSYS_COLOUR_BTNFACE );
    wxColour block = blend( face, sysColour( wxSYS_COLOUR_WINDOWTEXT ), 0.06 );

    wxPanel* panel = new wxPanel( aParent );
    panel->SetMinSize( wxSize( FromDIP( 280 ), -1 ) );
    m_props = panel;

    wxBoxSizer* sizer = new wxBoxSizer( wxVERTICAL );

    wxStaticText* caption = new wxStaticText( panel, wxID_ANY, _( "Properties" ) );
    caption->SetFont( caption->GetFont().Bold() );
    sizer->Add( caption, 0, wxALL, FromDIP( 6 ) );

    wxStaticText* kind = new wxStaticText( panel, wxID_ANY, _( "Layer Stack Manager" ) );
    sizer->Add( kind, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP( 6 ) );

    auto section =
            [&]( const wxString& aTitle ) -> std::pair<wxPanel*, wxFlexGridSizer*>
            {
                wxStaticText* title = new wxStaticText( panel, wxID_ANY,
                                                        wxS( "\u25E2 " ) + aTitle );
                title->SetFont( title->GetFont().Bold() );
                sizer->Add( title, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP( 6 ) );

                wxPanel* body = new wxPanel( panel );
                body->SetBackgroundColour( block );

                wxFlexGridSizer* grid = new wxFlexGridSizer( 2, FromDIP( 6 ), FromDIP( 8 ) );
                grid->AddGrowableCol( 1 );

                wxBoxSizer* outer = new wxBoxSizer( wxVERTICAL );
                outer->Add( grid, 1, wxEXPAND | wxALL, FromDIP( 8 ) );
                body->SetSizer( outer );

                sizer->Add( body, 0, wxEXPAND | wxALL, FromDIP( 4 ) );
                return { body, grid };
            };

    auto label =
            [&]( wxWindow* aBody, wxFlexGridSizer* aGrid, const wxString& aText ) -> wxStaticText*
            {
                wxStaticText* text = new wxStaticText( aBody, wxID_ANY, aText );
                aGrid->Add( text, 0, wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL );
                return text;
            };

    // Layer
    auto [layerBody, layerGrid] = section( _( "Layer" ) );

    label( layerBody, layerGrid, _( "Name" ) );
    m_propName = new wxTextCtrl( layerBody, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                 wxDefaultSize, wxTE_PROCESS_ENTER );
    m_propName->SetToolTip( _( "Copper layer names can be edited; other names are fixed" ) );
    layerGrid->Add( m_propName, 1, wxEXPAND );

    label( layerBody, layerGrid, _( "Comment" ) );
    m_propComment = new wxTextCtrl( layerBody, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                    wxDefaultSize, wxTE_READONLY );
    m_propComment->SetToolTip( _( "KiCad stackups don't store layer comments" ) );
    layerGrid->Add( m_propComment, 1, wxEXPAND );

    m_propColourLabel = label( layerBody, layerGrid, _( "Color" ) );
    m_propColour = new wxChoice( layerBody, wxID_ANY );
    layerGrid->Add( m_propColour, 1, wxEXPAND );

    // Substack
    auto [stackBody, stackGrid] = section( _( "Substack" ) );

    label( stackBody, stackGrid, _( "Stack Name" ) );
    m_propStackName = new wxTextCtrl( stackBody, wxID_ANY, _( "Rigid" ), wxDefaultPosition,
                                      wxDefaultSize, wxTE_READONLY );
    stackGrid->Add( m_propStackName, 1, wxEXPAND );

    label( stackBody, stackGrid, _( "Is Flex" ) );
    wxCheckBox* flex = new wxCheckBox( stackBody, wxID_ANY, wxEmptyString );
    flex->Enable( false );
    stackGrid->Add( flex, 0, wxALIGN_CENTER_VERTICAL );

    auto value =
            [&]( const wxString& aLabel ) -> wxStaticText*
            {
                label( stackBody, stackGrid, aLabel );
                wxStaticText* text = new wxStaticText( stackBody, wxID_ANY, wxEmptyString );
                stackGrid->Add( text, 0, wxALIGN_CENTER_VERTICAL );
                return text;
            };

    m_propLayers = value( _( "Layers" ) );
    m_propDielectrics = value( _( "Dielectrics" ) );
    m_propConductive = value( _( "Conductive Thickness" ) );
    m_propDielectricThk = value( _( "Dielectric Thickness" ) );
    m_propTotal = value( _( "Total Thickness" ) );

    panel->SetSizer( sizer );

    // Name: copper layers can be renamed, as in the Name column
    auto commitName =
            [this]()
            {
                int row = selectedRow();

                if( row < 0 || m_propName->IsEmpty() )
                    return;

                if( m_propName->GetValue() == m_grid->GetCellValue( row, COL_NAME ) )
                    return;

                STATE    before = m_state;
                wxString error;

                if( !applyCellEdit( row, COL_NAME, m_propName->GetValue().Strip( wxString::both ),
                                    error ) )
                {
                    m_state = before;
                    wxBell();
                    setStatus( error, true );
                    refreshProperties();
                    return;
                }

                commitChange( before );
            };

    m_propName->Bind( wxEVT_TEXT_ENTER, [commitName]( wxCommandEvent& ) { commitName(); } );
    m_propName->Bind( wxEVT_KILL_FOCUS,
                      [commitName]( wxFocusEvent& aEvent )
                      {
                          aEvent.Skip();
                          commitName();
                      } );

    m_propColour->Bind( wxEVT_CHOICE,
                        [this]( wxCommandEvent& )
                        {
                            int row = selectedRow();

                            if( row < 0 )
                                return;

                            STATE    before = m_state;
                            wxString error;

                            applyCellEdit( row, COL_COLOR, m_propColour->GetStringSelection(),
                                           error );
                            commitChange( before );
                        } );

    return panel;
}


void LAYER_STACK_MANAGER_FRAME::refreshProperties()
{
    if( !m_props )
        return;

    m_props->Show( m_view == 0 );

    int row = selectedRow();
    const std::vector<BOARD_STACKUP_ITEM*>& list = m_state.m_stackup.GetList();

    // Layer section
    BOARD_STACKUP_ITEM* item = nullptr;
    int                 sub = 0;

    if( row >= 0 && row < (int) m_rows.size() )
    {
        item = list[m_rows[row].m_item];
        sub = m_rows[row].m_sub;
    }

    if( !m_propName->HasFocus() )
        m_propName->ChangeValue( item ? m_grid->GetCellValue( row, COL_NAME ) : wxString() );

    m_propName->SetEditable( item && item->GetType() == BS_ITEM_TYPE_COPPER );

    bool colourEditable = item && item->IsColorEditable();
    m_propColour->Show( colourEditable );
    m_propColourLabel->Show( colourEditable );

    if( colourEditable )
    {
        const std::vector<FAB_LAYER_COLOR>& standard = GetStandardColors( item->GetType() );
        wxArrayString                       choices;

        // The last standard entry is the "user defined" placeholder: offer a picker instead
        for( size_t ii = 0; ii + 1 < standard.size(); ++ii )
            choices.Add( wxGetTranslation( standard[ii].GetName() ) );

        wxString current = colourDisplay( item->GetColor( sub ) );

        if( choices.Index( current ) == wxNOT_FOUND )
            choices.Add( current );

        choices.Add( customColourLabel() );

        if( m_propColour->GetStrings() != choices )
            m_propColour->Set( choices );

        m_propColour->SetStringSelection( current );
    }

    // Substack section: counts and thicknesses of the whole stack
    int    copper = 0;
    int    dielectrics = 0;
    double conductive = 0.0;
    double dielectric = 0.0;

    for( const BOARD_STACKUP_ITEM* layerItem : list )
    {
        if( layerItem->GetType() == BS_ITEM_TYPE_COPPER )
        {
            copper++;
            conductive += layerItem->GetThickness();
        }
        else if( layerItem->GetType() == BS_ITEM_TYPE_DIELECTRIC )
        {
            for( int ii = 0; ii < layerItem->GetSublayersCount(); ++ii )
            {
                dielectrics++;
                dielectric += layerItem->GetThickness( ii );
            }
        }
    }

    m_propLayers->SetLabel( wxString::Format( wxS( "%d" ), copper ) );
    m_propDielectrics->SetLabel( wxString::Format( wxS( "%d" ), dielectrics ) );
    m_propConductive->SetLabel( m_frame->StringFromValue( conductive, true ) );
    m_propDielectricThk->SetLabel( m_frame->StringFromValue( dielectric, true ) );
    m_propTotal->SetLabel( m_frame->StringFromValue(
            m_state.m_stackup.BuildBoardThicknessFromStackup(), true ) );

    m_props->Layout();
    m_props->GetParent()->Layout();
}


void LAYER_STACK_MANAGER_FRAME::buildGrid( wxWindow* aParent )
{
    m_grid = new wxGrid( aParent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE );
    m_grid->CreateGrid( 0, COL_COUNT );
    m_grid->SetRowLabelSize( 0 );
    m_grid->SetColLabelSize( FromDIP( 26 ) );
    m_grid->SetDefaultRowSize( FromDIP( 24 ) );
    m_grid->SetColLabelAlignment( wxALIGN_LEFT, wxALIGN_CENTER );
    m_grid->SetSelectionMode( wxGrid::wxGridSelectRows );
    m_grid->DisableDragRowSize();
    m_grid->SetMargins( 0, 0 );

    // Altium: coloured rows on the panel grey, a dark header with light text, dark cell lines
    wxColour face = sysColour( wxSYS_COLOUR_BTNFACE );
    m_grid->SetBackgroundColour( face );
    m_grid->GetGridWindow()->SetBackgroundColour( face );
    m_grid->SetDefaultCellBackgroundColour( face );
    m_grid->SetDefaultCellTextColour( rowText() );
    m_grid->SetDefaultRenderer( new STACK_CELL_RENDERER() );
    m_grid->SetLabelBackgroundColour( blend( face, *wxBLACK, 0.12 ) );
    m_grid->SetLabelTextColour( sysColour( wxSYS_COLOUR_BTNTEXT ) );
    m_grid->SetGridLineColour( blend( face, *wxBLACK, 0.40 ) );
    m_grid->SetCellHighlightPenWidth( 0 );
    m_grid->SetCellHighlightROPenWidth( 0 );
    m_grid->SetSelectionBackground( face );
    m_grid->SetDefaultCellAlignment( wxALIGN_LEFT, wxALIGN_CENTER );

    m_grid->SetColLabelValue( COL_NUM, wxS( "#" ) );
    m_grid->SetColLabelValue( COL_WEIGHT, _( "Weight" ) );
    m_grid->SetColLabelValue( COL_NAME, _( "Name" ) );
    m_grid->SetColLabelValue( COL_MATERIAL, _( "Material" ) );
    m_grid->SetColLabelValue( COL_TYPE, _( "Type" ) );
    m_grid->SetColLabelValue( COL_THICKNESS, _( "Thickness" ) );
    m_grid->SetColLabelValue( COL_DK, _( "Dk" ) );
    m_grid->SetColLabelValue( COL_DF, _( "Df" ) );

    // The "..." button at the right of a Material cell opens the material library
    m_grid->Bind( wxEVT_GRID_CELL_LEFT_CLICK,
                  [this]( wxGridEvent& aEvent )
                  {
                      int row = aEvent.GetRow();

                      if( aEvent.GetCol() == COL_MATERIAL && row >= 0 && row < (int) m_rows.size()
                              && m_state.m_stackup.GetList()[m_rows[row].m_item]
                                         ->IsMaterialEditable() )
                      {
                          wxRect cell = m_grid->CellToRect( row, COL_MATERIAL );
                          int    x, y;
                          m_grid->CalcScrolledPosition( cell.GetRight(), cell.y, &x, &y );

                          if( aEvent.GetPosition().x > x - materialButtonWidth( *m_grid )
                                                               - FromDIP( 4 ) )
                          {
                              m_grid->SelectRow( row );
                              m_grid->SetGridCursor( row, COL_NAME );
                              CallAfter( [this, row]() { chooseMaterial( row ); } );
                              return;
                          }
                      }

                      aEvent.Skip();
                  } );

    // The selection is drawn as an outline over the row colours: repaint when it moves
    m_grid->Bind( wxEVT_GRID_RANGE_SELECTED,
                  [this]( wxGridRangeSelectEvent& aEvent )
                  {
                      aEvent.Skip();
                      m_grid->ForceRefresh();
                  } );

    m_grid->Bind( wxEVT_GRID_CELL_CHANGED, &LAYER_STACK_MANAGER_FRAME::onCellChanged, this );
    m_grid->Bind( wxEVT_GRID_CELL_RIGHT_CLICK, &LAYER_STACK_MANAGER_FRAME::onCellRightClick,
                  this );
    m_grid->Bind( wxEVT_GRID_SELECT_CELL,
                  [this]( wxGridEvent& aEvent )
                  {
                      aEvent.Skip();
                      CallAfter( [this]()
                                 {
                                     m_grid->ForceRefresh();
                                     updateChrome();
                                 } );
                  } );

    m_grid->Bind( wxEVT_SIZE,
                  [this]( wxSizeEvent& aEvent )
                  {
                      aEvent.Skip();
                      fitColumns( m_grid, m_colBase[m_grid], { COL_NAME, COL_MATERIAL } );
                  } );

    m_grid->Bind( wxEVT_KEY_DOWN,
                  [this]( wxKeyEvent& aEvent )
                  {
                      if( ( aEvent.GetKeyCode() == WXK_DELETE
                            || aEvent.GetKeyCode() == WXK_NUMPAD_DELETE )
                          && !m_grid->IsCellEditControlShown() )
                      {
                          deleteLayer();
                          return;
                      }

                      aEvent.Skip();
                  } );
}


LAYER_STACK_MANAGER_FRAME::STATE LAYER_STACK_MANAGER_FRAME::loadFromBoard() const
{
    STATE                  state;
    BOARD_DESIGN_SETTINGS& bds = m_board->GetDesignSettings();

    // Same normalisation as Board Setup: drop layers that aren't enabled, add missing ones
    state.m_stackup = bds.GetStackupDescriptor();
    bool thicknessConstrained = state.m_stackup.m_HasThicknessConstrains;
    state.m_stackup.SynchronizeWithBoard( &bds );
    state.m_stackup.m_HasThicknessConstrains = thicknessConstrained;

    state.m_layers = m_board->GetEnabledLayers() & BOARD_STACKUP::StackupAllowedBrdLayers();

    for( PCB_LAYER_ID layer : LSET::AllCuMask() )
    {
        state.m_names[layer] = m_board->GetLayerName( layer );
        state.m_types[layer] = m_board->GetLayerType( layer );
    }

    // Impedance profiles are KiCad's tuning profiles (project file); via types are the
    // project's layer pair presets
    if( std::shared_ptr<TUNING_PROFILES> params =
                m_frame->Prj().GetProjectFile().TuningProfileParameters() )
    {
        state.m_profiles = params->GetTuningProfiles();

        for( const TUNING_PROFILE& profile : state.m_profiles )
            state.m_profileOrig.push_back( profile.m_ProfileName );
    }

    if( LAYER_PAIR_SETTINGS* pairs = m_frame->GetLayerPairSettings() )
    {
        for( const LAYER_PAIR_INFO& info : pairs->GetLayerPairs() )
            state.m_viaTypes.push_back( info );
    }

    syncDerived( state );
    return state;
}


void LAYER_STACK_MANAGER_FRAME::rebuildStackup( STATE& aState ) const
{
    // Lay out the stack for the new set of layers (KiCad's default ordering, with dielectric
    // N between copper layers N and N+1), keeping the parameters of the layers that remain.
    int copperCount = aState.CopperCount();

    BOARD_STACKUP full;
    full.BuildDefaultStackupList( nullptr, copperCount );

    BOARD_STACKUP rebuilt( aState.m_stackup );
    rebuilt.RemoveAll();

    for( const BOARD_STACKUP_ITEM* item : full.GetList() )
    {
        const BOARD_STACKUP_ITEM* source = item;

        if( item->GetBrdLayerId() != UNDEFINED_LAYER )
        {
            if( !aState.m_layers.Contains( item->GetBrdLayerId() ) )
                continue;

            for( const BOARD_STACKUP_ITEM* old : aState.m_stackup.GetList() )
            {
                if( old->GetBrdLayerId() == item->GetBrdLayerId() )
                    source = old;
            }
        }
        else
        {
            if( item->GetDielectricLayerId() > copperCount - 1 )
                continue;

            for( const BOARD_STACKUP_ITEM* old : aState.m_stackup.GetList() )
            {
                if( old->GetBrdLayerId() == UNDEFINED_LAYER
                        && old->GetDielectricLayerId() == item->GetDielectricLayerId() )
                {
                    source = old;
                }
            }
        }

        rebuilt.Add( new BOARD_STACKUP_ITEM( *source ) );
    }

    aState.m_stackup = rebuilt;
    syncDerived( aState );
}


wxString LAYER_STACK_MANAGER_FRAME::layerDisplayName( PCB_LAYER_ID aLayer ) const
{
    wxString boardName = IsCopperLayer( aLayer ) ? m_state.m_names.at( aLayer )
                                                 : m_board->GetLayerName( aLayer );

    return ALTIUM_LAYER_TABS::DisplayName( aLayer, boardName );
}


bool LAYER_STACK_MANAGER_FRAME::validateLayerName( PCB_LAYER_ID aLayer, const wxString& aName,
                                                   wxString& aError ) const
{
    // Same rules as Board Setup's layer names
    wxString badChars = wxFileName::GetForbiddenChars( wxPATH_DOS );
    badChars.Append( '%' );

    if( aName.IsEmpty() )
    {
        aError = _( "Layer must have a name." );
        return false;
    }

    if( aName.find_first_of( badChars ) != wxString::npos )
    {
        aError = wxString::Format( _( "%s are forbidden in layer names." ), badChars );
        return false;
    }

    if( aName == wxT( "signal" ) )
    {
        aError = _( "Layer name \"signal\" is reserved." );
        return false;
    }

    LSET enabled = ( m_board->GetEnabledLayers() & ~BOARD_STACKUP::StackupAllowedBrdLayers() )
                   | m_state.m_layers;

    for( PCB_LAYER_ID other : enabled )
    {
        if( other == aLayer )
            continue;

        wxString otherName = IsCopperLayer( other ) ? m_state.m_names.at( other )
                                                    : m_board->GetLayerName( other );

        if( aName == otherName || aName == ALTIUM_LAYER_TABS::DisplayName( other, otherName ) )
        {
            aError = wxString::Format( _( "Layer name '%s' already in use." ), aName );
            return false;
        }
    }

    return true;
}


bool LAYER_STACK_MANAGER_FRAME::applyToBoard()
{
    if( !m_frameAlive )
        return false;

    if( m_frame->GetBoard() != m_board )
    {
        syncFromBoard();
        setStatus( _( "The board was reloaded; the stackup has been read again." ), true );
        return false;
    }

    wxString error;

    for( PCB_LAYER_ID layer : LSET( m_state.m_layers & LSET::AllCuMask() ) )
    {
        if( !validateLayerName( layer, m_state.m_names[layer], error ) )
        {
            DisplayErrorMessage( this, error );
            return false;
        }
    }

    BOARD*                 board = m_board;
    BOARD_DESIGN_SETTINGS& bds = board->GetDesignSettings();
    LSET                   oldEnabled = board->GetEnabledLayers();
    LSET newEnabled = ( oldEnabled & ~BOARD_STACKUP::StackupAllowedBrdLayers() ) | m_state.m_layers;

    // Removed layers that still have items: confirm, as Board Setup does
    std::vector<PCB_LAYER_ID> layersWithItems;
    wxString                  layerList;

    for( PCB_LAYER_ID layer : LSET( oldEnabled & ~newEnabled ) )
    {
        bool hasItems = board->HasItemsOnLayer( layer );

        for( FOOTPRINT* footprint : board->Footprints() )
        {
            for( PAD* pad : footprint->Pads() )
            {
                if( !hasItems && pad->HasExplicitDefinitionForLayer( layer ) )
                    hasItems = true;
            }
        }

        if( !hasItems && !IsCopperLayer( layer ) )
        {
            PCB_LAYER_COLLECTOR collector;
            collector.SetLayerId( layer );
            collector.Collect( board, GENERAL_COLLECTOR::FootprintItems );
            hasItems = collector.GetCount() > 0;
        }

        if( hasItems )
        {
            layersWithItems.push_back( layer );
            layerList << wxS( "    " )
                      << ALTIUM_LAYER_TABS::DisplayName( layer, board->GetLayerName( layer ) )
                      << wxS( "\n" );
        }
    }

    if( !layersWithItems.empty() )
    {
        wxString msg = wxString::Format( _( "These layers are being removed but contain items:\n\n"
                                            "%s\n"
                                            "Board items on them will be deleted and footprint "
                                            "items on them will no longer be accessible.  This "
                                            "can't be undone.\n\n"
                                            "Continue?" ),
                                         layerList );

        if( !IsOK( this, msg ) )
            return false;

        m_frame->GetToolManager()->RunAction( ACTIONS::selectionClear );

        for( PCB_LAYER_ID layer : layersWithItems )
            board->RemoveAllItemsOnLayer( layer );

        // The undo list may point at the deleted items
        m_frame->ClearUndoRedoList();
    }

    if( newEnabled != oldEnabled )
    {
        board->SetEnabledLayers( newEnabled );

        // Newly enabled layers are made visible
        board->SetVisibleLayers( board->GetVisibleLayers() | ( newEnabled & ~oldEnabled ) );

        // Through-hole pads span every inner copper layer (as Board Setup does)
        if( ( ( newEnabled ^ oldEnabled ) & LSET::AllCuMask() ).any() )
        {
            for( FOOTPRINT* footprint : board->Footprints() )
            {
                for( PAD* pad : footprint->Pads() )
                {
                    if( pad->HasHole() && pad->IsOnCopperLayer() )
                        pad->SetLayerSet( pad->GetLayerSet() | LSET::InternalCuMask() );
                }
            }
        }
    }

    for( PCB_LAYER_ID layer : LSET( newEnabled & LSET::AllCuMask() ) )
    {
        if( board->GetLayerName( layer ) != m_state.m_names[layer] )
            board->SetLayerName( layer, m_state.m_names[layer] );

        if( board->GetLayerType( layer ) != m_state.m_types[layer] )
            board->SetLayerType( layer, m_state.m_types[layer] );
    }

    BOARD_STACKUP stackup = m_state.m_stackup;

    for( BOARD_STACKUP_ITEM* item : stackup.GetList() )
    {
        item->SetEnabled( true );

        if( item->GetType() == BS_ITEM_TYPE_COPPER )
            item->SetLayerName( board->GetLayerName( item->GetBrdLayerId() ) );
    }

    bds.GetStackupDescriptor() = stackup;
    bds.SetBoardThickness( stackup.BuildBoardThicknessFromStackup() );
    bds.m_HasStackup = true;

    // Impedance profiles: the project's tuning profiles (stored in the .kicad_pro file)
    std::shared_ptr<TUNING_PROFILES> params =
            m_frame->Prj().GetProjectFile().TuningProfileParameters();

    if( params && !( m_state.m_profiles == m_saved.m_profiles ) )
    {
        params->ClearTuningProfiles();

        for( TUNING_PROFILE profile : m_state.m_profiles )
        {
            profile.m_TrackPropagationEntriesMap.clear();

            for( const DELAY_PROFILE_TRACK_PROPAGATION_ENTRY& entry :
                 profile.m_TrackPropagationEntries )
            {
                profile.m_TrackPropagationEntriesMap[entry.GetSignalLayer()] = entry;
            }

            params->AddTuningProfile( std::move( profile ) );
        }

        // Net classes refer to profiles by name: follow renames
        std::shared_ptr<NET_SETTINGS>& netSettings = m_frame->Prj().GetProjectFile().NetSettings();

        for( size_t ii = 0; netSettings && ii < m_state.m_profiles.size(); ++ii )
        {
            const wxString& oldName = m_state.m_profileOrig[ii];
            const wxString& newName = m_state.m_profiles[ii].m_ProfileName;

            if( oldName.IsEmpty() || oldName == newName )
                continue;

            auto remap = [&]( const std::shared_ptr<NETCLASS>& aNetclass )
                         {
                             if( aNetclass && aNetclass->GetTuningProfile() == oldName )
                                 aNetclass->SetTuningProfile( newName );
                         };

            remap( netSettings->GetDefaultNetclass() );

            for( const auto& [name, netclass] : netSettings->GetNetclasses() )
                remap( netclass );
        }

        if( netSettings )
            netSettings->ClearAllCaches();

        board->SynchronizeTuningProfileProperties();
    }

    // Via types: the layer pair presets (the board editor writes them to the project file)
    if( LAYER_PAIR_SETTINGS* pairs = m_frame->GetLayerPairSettings() )
    {
        if( !sameViaTypes( m_state.m_viaTypes, m_saved.m_viaTypes ) )
            pairs->SetLayerPairs( m_state.m_viaTypes );
    }

    m_frame->OnModify();
    refreshPcbEditor();

    // Undo history is kept: undoing and saving again restores an earlier stackup
    m_saved = loadFromBoard();
    m_state = m_saved;
    refreshGrid();
    setStatus( wxString::Format( _( "Stackup saved to %s." ),
                                 wxFileName( m_frame->GetCurrentFileName() ).GetFullName() ) );
    return true;
}


void LAYER_STACK_MANAGER_FRAME::refreshPcbEditor()
{
    // As after Board Setup: layer widgets, tools and design-rule caches follow the new stackup
    m_frame->UpdateUserInterface();

    TOOL_MANAGER* toolManager = m_frame->GetToolManager();
    toolManager->ResetTools( TOOL_BASE::MODEL_RELOAD );

    TOOL_EVENT toolEvent( TC_COMMAND, TA_MODEL_CHANGE, AS_ACTIVE );
    toolEvent.SetHasPosition( false );
    toolManager->ProcessEvent( toolEvent );

    m_frame->GetCanvas()->Refresh();
}


void LAYER_STACK_MANAGER_FRAME::syncFromBoard()
{
    if( !m_frameAlive )
        return;

    if( m_frame->GetBoard() != m_board )
    {
        // A different board was opened in the editor: start over with it
        m_board = m_frame->GetBoard();
        m_saved = loadFromBoard();
        m_state = m_saved;
        m_undo.clear();
        m_redo.clear();
        refreshGrid();
        return;
    }

    if( isDirty() )
        return;

    // Pick up changes made elsewhere (e.g. Board Setup) while there are no edits here
    STATE fresh = loadFromBoard();

    if( !( fresh == m_saved ) )
    {
        m_saved = fresh;
        m_state = fresh;
        m_undo.clear();
        m_redo.clear();
        refreshGrid();
    }
}


void LAYER_STACK_MANAGER_FRAME::refreshGrid()
{
    int selected = selectedRow();

    m_rows.clear();

    const std::vector<BOARD_STACKUP_ITEM*>& list = m_state.m_stackup.GetList();

    for( int ii = 0; ii < (int) list.size(); ++ii )
    {
        // Altium's stack doesn't list paste layers (they stay in the stackup)
        if( list[ii]->GetType() == BS_ITEM_TYPE_SOLDERPASTE )
            continue;

        for( int sub = 0; sub < list[ii]->GetSublayersCount(); ++sub )
            m_rows.push_back( { ii, sub } );
    }

    {
        wxGridUpdateLocker lock( m_grid );

        if( m_grid->IsCellEditControlShown() )
            m_grid->HideCellEditControl();

        if( m_grid->GetNumberRows() > 0 )
            m_grid->DeleteRows( 0, m_grid->GetNumberRows() );

        m_grid->AppendRows( (int) m_rows.size() );

        int copperIndex = 0;

        for( int row = 0; row < (int) m_rows.size(); ++row )
            fillRow( row, copperIndex );

        m_grid->AutoSizeColumns( false );

        const int minWidths[COL_COUNT] = { 30, 170, 150, 110, 70, 110, 60, 70 };

        std::vector<int>& base = m_colBase[m_grid];
        base.assign( COL_COUNT, 0 );

        for( int col = 0; col < COL_COUNT; ++col )
        {
            int width = std::max( m_grid->GetColSize( col ), FromDIP( minWidths[col] ) );
            base[col] = width + FromDIP( 12 );
        }

        // The table spans the window; Name and Material take the extra width
        fitColumns( m_grid, base, { COL_NAME, COL_MATERIAL } );
    }

    if( selected >= (int) m_rows.size() )
        selected = (int) m_rows.size() - 1;

    if( selected >= 0 )
    {
        m_grid->SelectRow( selected );
        m_grid->SetGridCursor( selected, COL_NAME );
    }

    refreshViews();
    updateChrome();
}


void LAYER_STACK_MANAGER_FRAME::fillRow( int aRow, int& aCopperIndex )
{
    const ROW&              row = m_rows[aRow];
    BOARD_STACKUP_ITEM*     item = m_state.m_stackup.GetList()[row.m_item];
    int                     sub = row.m_sub;
    BOARD_STACKUP_ITEM_TYPE type = item->GetType();
    PCB_LAYER_ID            layer = item->GetBrdLayerId();
    wxColour                background = rowBackground( type );
    wxColour                dimText = blend( rowText(), background, 0.35 );

    for( int col = 0; col < COL_COUNT; ++col )
    {
        m_grid->SetCellBackgroundColour( aRow, col, background );
        m_grid->SetCellTextColour( aRow, col, rowText() );
        m_grid->SetReadOnly( aRow, col, true );
    }

    auto editable =
            [&]( int aCol, const wxString& aValue, wxGridCellEditor* aEditor = nullptr )
            {
                m_grid->SetCellValue( aRow, aCol, aValue );
                m_grid->SetReadOnly( aRow, aCol, false );

                if( aEditor )
                    m_grid->SetCellEditor( aRow, aCol, aEditor );
            };

    auto fixed =
            [&]( int aCol, const wxString& aValue, bool aDim = false )
            {
                m_grid->SetCellValue( aRow, aCol, aValue );

                if( aDim )
                    m_grid->SetCellTextColour( aRow, aCol, dimText );
            };

    // Name and copper number
    switch( type )
    {
    case BS_ITEM_TYPE_COPPER:
        fixed( COL_NUM, wxString::Format( wxS( "%d" ), ++aCopperIndex ) );
        editable( COL_NAME, layerDisplayName( layer ) );
        break;

    case BS_ITEM_TYPE_DIELECTRIC:
        if( sub == 0 )
            fixed( COL_NAME, item->FormatDielectricLayerName() );
        else
            fixed( COL_NAME, wxString::Format( wxS( "%s (%d)" ),
                                               item->FormatDielectricLayerName(), sub + 1 ) );
        break;

    default:
        fixed( COL_NAME, layerDisplayName( layer ) );
        break;
    }

    // Material
    if( item->IsMaterialEditable() )
    {
        DIELECTRIC_SUBSTRATE_LIST materials( materialListType( type ) );
        wxArrayString             choices;

        for( int ii = 0; ii < materials.GetCount(); ++ii )
            choices.Add( materialDisplay( materials.GetSubstrate( ii )->m_Name ) );

        editable( COL_MATERIAL, materialDisplay( item->GetMaterial( sub ) ),
                  new wxGridCellChoiceEditor( choices, true ) );
        m_grid->SetCellRenderer( aRow, COL_MATERIAL, new STACK_CELL_RENDERER( true ) );
    }
    else if( type == BS_ITEM_TYPE_COPPER )
    {
        fixed( COL_MATERIAL, _( "Copper" ), true );
    }

    // Type
    switch( type )
    {
    case BS_ITEM_TYPE_COPPER:
    {
        wxArrayString choices;

        for( LAYER_T layerType : COPPER_TYPES )
            choices.Add( copperTypeName( layerType ) );

        editable( COL_TYPE, copperTypeName( m_state.m_types[layer] ),
                  new wxGridCellChoiceEditor( choices ) );
        break;
    }

    case BS_ITEM_TYPE_DIELECTRIC:
    {
        wxString typeName = item->GetTypeName() == KEY_CORE ? _( "Core" ) : _( "Prepreg" );

        if( sub == 0 )
        {
            wxArrayString choices;
            choices.Add( _( "Core" ) );
            choices.Add( _( "Prepreg" ) );
            editable( COL_TYPE, typeName, new wxGridCellChoiceEditor( choices ) );
        }
        else
        {
            fixed( COL_TYPE, typeName, true );
        }

        break;
    }

    case BS_ITEM_TYPE_SOLDERMASK:  fixed( COL_TYPE, _( "Solder Mask" ) ); break;
    case BS_ITEM_TYPE_SILKSCREEN:  fixed( COL_TYPE, _( "Overlay" ) );     break;
    case BS_ITEM_TYPE_SOLDERPASTE: fixed( COL_TYPE, _( "Paste" ) );       break;
    default:                                                              break;
    }

    if( item->IsThicknessEditable() )
        editable( COL_THICKNESS, m_frame->StringFromValue( item->GetThickness( sub ), true ) );

    // Copper weight, as Altium lists it (editing it sets the thickness)
    if( type == BS_ITEM_TYPE_COPPER )
        editable( COL_WEIGHT, copperWeight( item->GetThickness( sub ) ) );

    if( item->HasEpsilonRValue() )
        editable( COL_DK, item->FormatEpsilonR( sub ) );

    if( item->HasLossTangentValue() )
        editable( COL_DF, item->FormatLossTangent( sub ) );

    m_grid->SetCellAlignment( aRow, COL_NUM, wxALIGN_CENTER, wxALIGN_CENTER );
}


void LAYER_STACK_MANAGER_FRAME::updateChrome()
{
    if( !m_toolbar )
        return;

    bool dirty = isDirty();

    bool stackupView = m_view == 0;

    int  row = selectedRow();
    bool materialEditable = row >= 0 && row < (int) m_rows.size()
                            && m_state.m_stackup.GetList()[m_rows[row].m_item]->IsMaterialEditable();

    bool canDeleteHere = stackupView ? canDelete()
                         : m_view == 1 ? !m_state.m_profiles.empty()
                                       : m_viaGrid && m_viaGrid->GetGridCursorRow() > 0;

    enableTool( ID_ADD_PAIR, true );
    enableTool( ID_CHOOSE_MATERIAL, stackupView && materialEditable );
    enableTool( ID_DELETE, canDeleteHere );

    // Altium's Impedance and Via Types views have only Add and Delete
    if( m_tools.count( ID_CHOOSE_MATERIAL )
            && m_tools[ID_CHOOSE_MATERIAL]->IsShown() != stackupView )
    {
        m_tools[ID_CHOOSE_MATERIAL]->Show( stackupView );
        m_toolbar->Layout();
    }
    enableTool( ID_SUBSTACK_ADD, false );
    enableTool( ID_SUBSTACK_DELETE, false );

    if( m_impedance )
        m_impedance->SetValue( m_state.m_stackup.m_HasDielectricConstrains );

    refreshProperties();

    if( wxMenuBar* menuBar = GetMenuBar() )
    {
        menuBar->Enable( ID_ADD_PAIR, stackupView && m_state.CopperCount() + 2 <= MAX_CU_LAYERS );
        menuBar->Enable( ID_INS_DIEL_ABOVE, stackupView && canInsertDielectric( true ) );
        menuBar->Enable( ID_INS_DIEL_BELOW, stackupView && canInsertDielectric( false ) );
        menuBar->Enable( ID_DELETE, stackupView && canDelete() );
        menuBar->Enable( ID_MOVE_UP, stackupView && canMove( -1 ) );
        menuBar->Enable( ID_MOVE_DOWN, stackupView && canMove( 1 ) );
        menuBar->Enable( ID_CHOOSE_MATERIAL, stackupView && selectedRow() >= 0 );
        menuBar->Enable( ID_UNDO, !m_undo.empty() );
        menuBar->Enable( ID_REDO, !m_redo.empty() );
        menuBar->Enable( ID_SAVE, dirty );
        menuBar->Enable( ID_REVERT, dirty );
        menuBar->Check( ID_IMPEDANCE, m_state.m_stackup.m_HasDielectricConstrains );
        menuBar->Check( m_view == 1 ? ID_VIEW_IMPEDANCE
                                    : m_view == 2 ? ID_VIEW_VIA_TYPES : ID_VIEW_STACKUP,
                        true );
    }

    m_layerCount->SetLabel( wxString::Format( _( "%d copper layers" ), m_state.CopperCount() ) );
    m_thickness->SetLabel( wxString::Format(
            _( "Total Thickness: %s" ),
            m_frame->StringFromValue( m_state.m_stackup.BuildBoardThicknessFromStackup(),
                                      true ) ) );

    wxString fileName = wxFileName( m_frame->GetCurrentFileName() ).GetFullName();

    if( fileName.IsEmpty() )
        fileName = _( "Untitled" );

    wxString title = fileName + wxS( " [Stackup]" );

    if( dirty )
        title += wxS( " *" );

    if( GetTitle() != title )
        SetTitle( title );

    m_status->GetParent()->Layout();
}


void LAYER_STACK_MANAGER_FRAME::setStatus( const wxString& aText, bool aError )
{
    m_status->SetForegroundColour( aError ? wxColour( 210, 50, 50 )
                                          : sysColour( wxSYS_COLOUR_BTNTEXT ) );
    m_status->SetLabel( aText );
    m_status->SetToolTip( aText );
}


int LAYER_STACK_MANAGER_FRAME::selectedRow() const
{
    if( !m_grid || m_grid->GetNumberRows() == 0 )
        return -1;

    wxArrayInt rows = m_grid->GetSelectedRows();

    if( !rows.IsEmpty() )
        return rows[0];

    int row = m_grid->GetGridCursorRow();
    return row < (int) m_rows.size() ? row : -1;
}


void LAYER_STACK_MANAGER_FRAME::selectRow( int aItem, int aSub )
{
    for( int row = 0; row < (int) m_rows.size(); ++row )
    {
        if( m_rows[row].m_item == aItem && m_rows[row].m_sub == aSub )
        {
            m_grid->SelectRow( row );
            m_grid->SetGridCursor( row, COL_NAME );
            m_grid->MakeCellVisible( row, COL_NAME );
            break;
        }
    }

    updateChrome();
}


void LAYER_STACK_MANAGER_FRAME::commitChange( const STATE& aBefore, const wxString& aMessage )
{
    if( !( m_state == aBefore ) )
    {
        m_undo.push_back( aBefore );
        m_redo.clear();
    }

    refreshGrid();
    setStatus( aMessage );
}


bool LAYER_STACK_MANAGER_FRAME::applyCellEdit( int aRow, int aCol, const wxString& aValue,
                                               wxString& aError )
{
    const ROW&              row = m_rows[aRow];
    BOARD_STACKUP_ITEM*     item = m_state.m_stackup.GetList()[row.m_item];
    int                     sub = row.m_sub;
    BOARD_STACKUP_ITEM_TYPE type = item->GetType();
    PCB_LAYER_ID            layer = item->GetBrdLayerId();

    auto parseNumber =
            [&]( double& aResult, const wxString& aWhat ) -> bool
            {
                if( ( aValue.ToDouble( &aResult ) || aValue.ToCDouble( &aResult ) )
                        && aResult >= 0.0 )
                {
                    return true;
                }

                aError = wxString::Format( _( "%s must be a positive number (or 0 if not used)." ),
                                           aWhat );
                return false;
            };

    switch( aCol )
    {
    case COL_NAME:
        if( type != BS_ITEM_TYPE_COPPER || aValue == layerDisplayName( layer ) )
            return true;

        if( !validateLayerName( layer, aValue, aError ) )
            return false;

        m_state.m_names[layer] = aValue;
        return true;

    case COL_MATERIAL:
    {
        wxString material = aValue;

        if( material.IsEmpty() || material == wxGetTranslation( NotSpecifiedPrm() ) )
            material = NotSpecifiedPrm();

        item->SetMaterial( material, sub );

        // A known material brings its Dk and Df (and for dielectrics a natural colour)
        DIELECTRIC_SUBSTRATE_LIST materials( materialListType( type ) );
        DIELECTRIC_SUBSTRATE*     substrate = IsPrmSpecified( material )
                                                      ? materials.GetSubstrate( material )
                                                      : nullptr;

        if( substrate )
        {
            item->SetMaterial( substrate->m_Name, sub );

            if( item->HasEpsilonRValue() )
                item->SetEpsilonR( substrate->m_EpsilonR, sub );

            if( item->HasLossTangentValue() )
                item->SetLossTangent( substrate->m_LossTangent, sub );

            if( type == BS_ITEM_TYPE_DIELECTRIC && !item->GetColor( sub ).StartsWith( "#" ) )
            {
                const wxString& name = substrate->m_Name;

                if( name.IsSameAs( "PTFE" ) || name.IsSameAs( "Teflon" ) )
                    item->SetColor( "PTFE natural", sub );
                else if( name.IsSameAs( "Polyimide" ) || name.IsSameAs( "Kapton" ) )
                    item->SetColor( "Polyimide", sub );
                else if( name.IsSameAs( "Al" ) )
                    item->SetColor( "Aluminum", sub );
                else
                    item->SetColor( "FR4 natural", sub );
            }
        }

        return true;
    }

    case COL_TYPE:
        if( type == BS_ITEM_TYPE_COPPER )
        {
            for( LAYER_T layerType : COPPER_TYPES )
            {
                if( aValue == copperTypeName( layerType ) )
                    m_state.m_types[layer] = layerType;
            }
        }
        else if( type == BS_ITEM_TYPE_DIELECTRIC && sub == 0 )
        {
            item->SetTypeName( aValue == _( "Core" ) ? KEY_CORE : KEY_PREPREG );
        }

        return true;

    case COL_THICKNESS:
    {
        if( !item->IsThicknessEditable() )
            return true;

        int thickness = m_frame->ValueFromString( aValue );

        if( aValue.IsEmpty() || thickness < 0 )
        {
            aError = _( "A layer thickness must be 0 or more." );
            return false;
        }

        item->SetThickness( thickness, sub );
        return true;
    }

    case COL_WEIGHT:
    {
        if( type != BS_ITEM_TYPE_COPPER )
            return true;

        wxString number = aValue.Lower();
        number.Replace( wxS( "oz" ), wxEmptyString );
        number.Trim().Trim( false );

        double oz = 0.0;

        if( !number.ToCDouble( &oz ) && !number.ToDouble( &oz ) )
        {
            aError = _( "The copper weight must be a number of ounces, e.g. 1oz." );
            return false;
        }

        if( oz <= 0.0 )
        {
            aError = _( "The copper weight must be more than 0." );
            return false;
        }

        item->SetThickness( KiROUND( oz * 0.035 * 1e6 ), sub );
        return true;
    }

    case COL_DK:
    {
        double value;

        if( !parseNumber( value, _( "Dk (relative permittivity)" ) ) )
            return false;

        item->SetEpsilonR( value, sub );
        return true;
    }

    case COL_DF:
    {
        double value;

        if( !parseNumber( value, _( "Df (loss tangent)" ) ) )
            return false;

        item->SetLossTangent( value, sub );
        return true;
    }

    case COL_COLOR:
    {
        if( aValue == customColourLabel() )
        {
            CallAfter( [this, aRow]() { pickCustomColour( aRow ); } );
            return true;
        }

        const std::vector<FAB_LAYER_COLOR>& standard = GetStandardColors( type );

        for( const FAB_LAYER_COLOR& colour : standard )
        {
            if( aValue == wxGetTranslation( colour.GetName() ) )
                item->SetColor( colour.GetName(), sub );
        }

        return true;
    }

    default:
        return true;
    }
}


bool LAYER_STACK_MANAGER_FRAME::canInsertDielectric( bool aAbove, int* aItem, int* aPos,
                                                     int* aSource ) const
{
    int row = selectedRow();

    if( row < 0 || row >= (int) m_rows.size() )
        return false;

    const std::vector<BOARD_STACKUP_ITEM*>& list = m_state.m_stackup.GetList();
    int                                     itemIdx = m_rows[row].m_item;
    int                                     sub = m_rows[row].m_sub;
    int                                     target = -1;
    int                                     pos = 0;
    int                                     source = 0;

    if( list[itemIdx]->GetType() == BS_ITEM_TYPE_DIELECTRIC )
    {
        target = itemIdx;
        pos = aAbove ? sub : sub + 1;
        source = sub;
    }
    else if( list[itemIdx]->GetType() == BS_ITEM_TYPE_COPPER )
    {
        // A dielectric next to a copper layer joins the stack of plies on that side
        int neighbour = aAbove ? itemIdx - 1 : itemIdx + 1;

        if( neighbour >= 0 && neighbour < (int) list.size()
                && list[neighbour]->GetType() == BS_ITEM_TYPE_DIELECTRIC )
        {
            target = neighbour;
            pos = aAbove ? list[neighbour]->GetSublayersCount() : 0;
            source = aAbove ? pos - 1 : 0;
        }
    }

    if( target < 0 )
        return false;

    if( aItem )
        *aItem = target;

    if( aPos )
        *aPos = pos;

    if( aSource )
        *aSource = source;

    return true;
}


bool LAYER_STACK_MANAGER_FRAME::canDelete() const
{
    int row = selectedRow();

    if( row < 0 || row >= (int) m_rows.size() )
        return false;

    const BOARD_STACKUP_ITEM* item = m_state.m_stackup.GetList()[m_rows[row].m_item];

    switch( item->GetType() )
    {
    case BS_ITEM_TYPE_DIELECTRIC: return item->GetSublayersCount() > 1;
    case BS_ITEM_TYPE_COPPER:     return m_state.CopperCount() > 2;
    default:                      return true;
    }
}


bool LAYER_STACK_MANAGER_FRAME::canMove( int aDir ) const
{
    int row = selectedRow();

    if( row < 0 || row >= (int) m_rows.size() )
        return false;

    const BOARD_STACKUP_ITEM* item = m_state.m_stackup.GetList()[m_rows[row].m_item];
    int                       target = m_rows[row].m_sub + aDir;

    return item->GetType() == BS_ITEM_TYPE_DIELECTRIC && target >= 0
           && target < item->GetSublayersCount();
}


void LAYER_STACK_MANAGER_FRAME::addLayerPair()
{
    int count = m_state.CopperCount();

    if( count + 2 > MAX_CU_LAYERS )
    {
        setStatus( wxString::Format( _( "A board can have at most %d copper layers." ),
                                     MAX_CU_LAYERS ), true );
        return;
    }

    STATE before = m_state;
    m_state.m_layers = ( m_state.m_layers & ~LSET::AllCuMask() ) | LSET::AllCuMask( count + 2 );
    rebuildStackup( m_state );

    // KiCad numbers inner layers top-down, so new layers go just above the bottom layer
    LSEQ         stack = LSET::AllCuMask( count + 2 ).CuStack();
    PCB_LAYER_ID first = stack[count - 1];
    PCB_LAYER_ID second = stack[count];

    commitChange( before, wxString::Format( _( "Added %s and %s above the bottom layer." ),
                                            layerDisplayName( first ),
                                            layerDisplayName( second ) ) );

    const std::vector<BOARD_STACKUP_ITEM*>& list = m_state.m_stackup.GetList();

    for( int ii = 0; ii < (int) list.size(); ++ii )
    {
        if( list[ii]->GetBrdLayerId() == first )
            selectRow( ii, 0 );
    }
}


void LAYER_STACK_MANAGER_FRAME::addTechLayer( PCB_LAYER_ID aLayer )
{
    if( m_state.m_layers.Contains( aLayer ) )
        return;

    STATE before = m_state;
    m_state.m_layers.set( aLayer );
    rebuildStackup( m_state );
    commitChange( before, wxString::Format( _( "Added %s." ), layerDisplayName( aLayer ) ) );

    const std::vector<BOARD_STACKUP_ITEM*>& list = m_state.m_stackup.GetList();

    for( int ii = 0; ii < (int) list.size(); ++ii )
    {
        if( list[ii]->GetBrdLayerId() == aLayer )
            selectRow( ii, 0 );
    }
}


void LAYER_STACK_MANAGER_FRAME::insertDielectric( bool aAbove )
{
    int target, pos, source;

    if( !canInsertDielectric( aAbove, &target, &pos, &source ) )
    {
        setStatus( _( "Select a dielectric, or a copper layer next to one." ), true );
        return;
    }

    STATE               before = m_state;
    BOARD_STACKUP_ITEM* item = m_state.m_stackup.GetList()[target];

    // The new ply starts as a copy of its neighbour
    item->AddDielectricPrms( pos );
    copySublayer( item, source >= pos ? source + 1 : source, pos );

    commitChange( before );
    selectRow( target, pos );
}


void LAYER_STACK_MANAGER_FRAME::deleteLayer()
{
    int row = selectedRow();

    if( row < 0 || row >= (int) m_rows.size() )
        return;

    STATE               before = m_state;
    BOARD_STACKUP_ITEM* item = m_state.m_stackup.GetList()[m_rows[row].m_item];
    int                 count = m_state.CopperCount();
    wxString            message;

    switch( item->GetType() )
    {
    case BS_ITEM_TYPE_DIELECTRIC:
        if( item->GetSublayersCount() < 2 )
        {
            setStatus( _( "The only dielectric between two copper layers can't be deleted; "
                          "delete a copper layer pair instead." ), true );
            return;
        }

        item->RemoveDielectricPrms( m_rows[row].m_sub );
        break;

    case BS_ITEM_TYPE_COPPER:
    {
        if( count <= 2 )
        {
            setStatus( _( "A board needs at least two copper layers." ), true );
            return;
        }

        // Copper layers come and go in pairs, from the bottom of the inner layers
        LSEQ stack = LSET::AllCuMask( count ).CuStack();
        message = wxString::Format( _( "Removed %s and %s (copper layers are removed in pairs, "
                                       "from the bottom of the inner layers)." ),
                                    layerDisplayName( stack[count - 3] ),
                                    layerDisplayName( stack[count - 2] ) );

        m_state.m_layers = ( m_state.m_layers & ~LSET::AllCuMask() ) | LSET::AllCuMask( count - 2 );
        rebuildStackup( m_state );
        break;
    }

    default:
        message = wxString::Format( _( "Removed %s." ),
                                    layerDisplayName( item->GetBrdLayerId() ) );
        m_state.m_layers.reset( item->GetBrdLayerId() );
        rebuildStackup( m_state );
        break;
    }

    commitChange( before, message );
}


void LAYER_STACK_MANAGER_FRAME::moveSublayer( int aDir )
{
    if( !canMove( aDir ) )
        return;

    int                 row = selectedRow();
    int                 itemIdx = m_rows[row].m_item;
    int                 sub = m_rows[row].m_sub;
    STATE               before = m_state;
    BOARD_STACKUP_ITEM* item = m_state.m_stackup.GetList()[itemIdx];

    // Swap the two plies through a temporary one
    item->AddDielectricPrms( item->GetSublayersCount() );
    int temp = item->GetSublayersCount() - 1;
    copySublayer( item, sub, temp );
    copySublayer( item, sub + aDir, sub );
    copySublayer( item, temp, sub + aDir );
    item->RemoveDielectricPrms( temp );

    commitChange( before );
    selectRow( itemIdx, sub + aDir );
}


void LAYER_STACK_MANAGER_FRAME::chooseMaterial( int aRow )
{
    if( aRow < 0 || aRow >= (int) m_rows.size() )
        return;

    int                 itemIdx = m_rows[aRow].m_item;
    int                 sub = m_rows[aRow].m_sub;
    BOARD_STACKUP_ITEM* item = m_state.m_stackup.GetList()[itemIdx];

    if( !item->IsMaterialEditable() )
        return;

    DIELECTRIC_SUBSTRATE_LIST  materials( materialListType( item->GetType() ) );
    DIALOG_DIELECTRIC_MATERIAL dlg( this, materials );

    if( dlg.ShowModal() != wxID_OK )
        return;

    DIELECTRIC_SUBSTRATE substrate = dlg.GetSelectedSubstrate();

    if( substrate.m_Name.IsEmpty() )
        return;

    STATE before = m_state;
    item = m_state.m_stackup.GetList()[itemIdx];
    item->SetMaterial( substrate.m_Name, sub );

    if( item->HasEpsilonRValue() )
        item->SetEpsilonR( substrate.m_EpsilonR, sub );

    if( item->HasLossTangentValue() )
        item->SetLossTangent( substrate.m_LossTangent, sub );

    commitChange( before );
}


void LAYER_STACK_MANAGER_FRAME::pickCustomColour( int aRow )
{
    if( aRow < 0 || aRow >= (int) m_rows.size() )
        return;

    int                 itemIdx = m_rows[aRow].m_item;
    int                 sub = m_rows[aRow].m_sub;
    BOARD_STACKUP_ITEM* item = m_state.m_stackup.GetList()[itemIdx];

    wxColourData data;
    data.SetColour( itemColour( item, sub ) );

    wxColourDialog dlg( this, &data );

    if( dlg.ShowModal() != wxID_OK )
    {
        refreshGrid();
        return;
    }

    STATE before = m_state;
    item = m_state.m_stackup.GetList()[itemIdx];
    item->SetColor( KIGFX::COLOR4D( dlg.GetColourData().GetColour() ).ToHexString(), sub );
    commitChange( before );
}


void LAYER_STACK_MANAGER_FRAME::undo()
{
    if( m_undo.empty() )
        return;

    m_redo.push_back( m_state );
    m_state = m_undo.back();
    m_undo.pop_back();
    refreshGrid();
    setStatus( wxEmptyString );
}


void LAYER_STACK_MANAGER_FRAME::redo()
{
    if( m_redo.empty() )
        return;

    m_undo.push_back( m_state );
    m_state = m_redo.back();
    m_redo.pop_back();
    refreshGrid();
    setStatus( wxEmptyString );
}


void LAYER_STACK_MANAGER_FRAME::revert()
{
    STATE before = m_state;
    m_state = m_saved;
    commitChange( before, _( "Reverted to the board's stackup." ) );
}


void LAYER_STACK_MANAGER_FRAME::fillAddMenu( wxMenu& aMenu ) const
{
    aMenu.Append( ID_ADD_PAIR, _( "Add Copper Layer Pair" ) );
    aMenu.Enable( ID_ADD_PAIR, m_state.CopperCount() + 2 <= MAX_CU_LAYERS );

    aMenu.Append( ID_INS_DIEL_ABOVE, _( "Insert Dielectric Above" ) );
    aMenu.Enable( ID_INS_DIEL_ABOVE, canInsertDielectric( true ) );
    aMenu.Append( ID_INS_DIEL_BELOW, _( "Insert Dielectric Below" ) );
    aMenu.Enable( ID_INS_DIEL_BELOW, canInsertDielectric( false ) );

    bool separator = false;

    for( int ii = 0; ii < (int) std::size( TECH_LAYERS ); ++ii )
    {
        PCB_LAYER_ID layer = TECH_LAYERS[ii];

        // Paste layers aren't listed (as in Altium), so they aren't offered either
        if( m_state.m_layers.Contains( layer ) || layer == F_Paste || layer == B_Paste )
            continue;

        if( !separator )
        {
            aMenu.AppendSeparator();
            separator = true;
        }

        aMenu.Append( ID_ADD_TECH_FIRST + ii,
                      wxString::Format( _( "Add %s" ),
                                        ALTIUM_LAYER_TABS::DisplayName(
                                                layer, m_board->GetLayerName( layer ) ) ) );
    }
}


void LAYER_STACK_MANAGER_FRAME::onCellChanged( wxGridEvent& aEvent )
{
    int row = aEvent.GetRow();
    int col = aEvent.GetCol();

    if( row < 0 || row >= (int) m_rows.size() )
        return;

    wxString value = m_grid->GetCellValue( row, col ).Strip( wxString::both );
    STATE    before = m_state;
    wxString error;

    if( !applyCellEdit( row, col, value, error ) )
    {
        m_state = before;
        wxBell();
        CallAfter( [this, error]()
                   {
                       refreshGrid();
                       setStatus( error, true );
                   } );
        return;
    }

    // Rebuild the rows after the editor has closed
    CallAfter( [this, before]() { commitChange( before ); } );
}


void LAYER_STACK_MANAGER_FRAME::onCellRightClick( wxGridEvent& aEvent )
{
    int row = aEvent.GetRow();

    if( row >= 0 && row < (int) m_rows.size() )
    {
        m_grid->SelectRow( row );
        m_grid->SetGridCursor( row, aEvent.GetCol() );
    }

    updateChrome();

    wxMenu menu;
    fillAddMenu( menu );
    menu.AppendSeparator();
    menu.Append( ID_DELETE, _( "Delete Layer" ) + wxS( "\tDel" ) );
    menu.Enable( ID_DELETE, canDelete() );
    menu.Append( ID_MOVE_UP, _( "Move Up" ) );
    menu.Enable( ID_MOVE_UP, canMove( -1 ) );
    menu.Append( ID_MOVE_DOWN, _( "Move Down" ) );
    menu.Enable( ID_MOVE_DOWN, canMove( 1 ) );

    if( row >= 0 && row < (int) m_rows.size()
            && m_state.m_stackup.GetList()[m_rows[row].m_item]->IsMaterialEditable() )
    {
        menu.AppendSeparator();
        menu.Append( ID_CHOOSE_MATERIAL, _( "Choose Material..." ) );
    }

    PopupMenu( &menu );
}


void LAYER_STACK_MANAGER_FRAME::onClose( wxCloseEvent& aEvent )
{
    if( m_frameAlive && isDirty() && aEvent.CanVeto() )
    {
        if( !HandleUnsavedChanges( this, _( "Save the changes to the layer stack before closing?" ),
                                   [this]() -> bool
                                   {
                                       return applyToBoard();
                                   } ) )
        {
            aEvent.Veto();
            return;
        }
    }

    if( m_frameAlive && m_frame->IsShown() )
        m_frame->Raise();

    Destroy();
}


void LAYER_STACK_MANAGER_FRAME::onActivate( wxActivateEvent& aEvent )
{
    aEvent.Skip();

    if( aEvent.GetActive() && m_frameAlive && !IsBeingDeleted() )
        CallAfter( [this]() { syncFromBoard(); } );
}


void LAYER_STACK_MANAGER_FRAME::onPcbClose( wxCloseEvent& aEvent )
{
    // Offer to save unsaved stackup edits before the board goes away
    if( m_frameAlive && isDirty() && aEvent.CanVeto() )
    {
        Raise();

        if( !HandleUnsavedChanges( this, _( "Save the changes to the layer stack?" ),
                                   [this]() -> bool
                                   {
                                       return applyToBoard();
                                   } ) )
        {
            aEvent.Veto();
            return;
        }

        m_state = m_saved;     // Discarded (or saved): nothing left to ask about
        updateChrome();
    }

    aEvent.Skip();
}


void LAYER_STACK_MANAGER_FRAME::onPcbDestroy( wxWindowDestroyEvent& aEvent )
{
    aEvent.Skip();

    // Destroy events of the editor's child windows propagate here too
    if( aEvent.GetEventObject() != m_frame )
        return;

    m_frameAlive = false;
    Hide();
    Destroy();
}


void LAYER_STACK_MANAGER_FRAME::onUnitsChanged( wxCommandEvent& aEvent )
{
    aEvent.Skip();

    if( m_frameAlive && !IsBeingDeleted() )
        refreshGrid();
}


// ---------------------------------------------------------------------------------------------
// Impedance and Via Types views
// ---------------------------------------------------------------------------------------------

namespace
{

/// Copper layers in stack order (top to bottom).
std::vector<PCB_LAYER_ID> copperStack( const BOARD_STACKUP& aStackup )
{
    std::vector<PCB_LAYER_ID> layers;

    for( const BOARD_STACKUP_ITEM* item : aStackup.GetList() )
    {
        if( item->GetType() == BS_ITEM_TYPE_COPPER )
            layers.push_back( item->GetBrdLayerId() );
    }

    return layers;
}


int stackIndex( const std::vector<PCB_LAYER_ID>& aStack, PCB_LAYER_ID aLayer )
{
    auto it = std::find( aStack.begin(), aStack.end(), aLayer );
    return it == aStack.end() ? -1 : (int) ( it - aStack.begin() );
}


/// The dielectric(s) between a signal layer and its reference plane(s), from the stackup.
bool lineGeometry( const BOARD_STACKUP& aStackup, PCB_LAYER_ID aSignal, PCB_LAYER_ID aTop,
                   PCB_LAYER_ID aBottom, LINE_GEOMETRY& aGeom, wxString& aError )
{
    const std::vector<BOARD_STACKUP_ITEM*>& list = aStackup.GetList();

    auto itemIndex =
            [&]( PCB_LAYER_ID aLayer )
            {
                for( int ii = 0; ii < (int) list.size(); ++ii )
                {
                    if( list[ii]->GetType() == BS_ITEM_TYPE_COPPER
                            && list[ii]->GetBrdLayerId() == aLayer )
                    {
                        return ii;
                    }
                }

                return -1;
            };

    // Dielectric height (mm) between two stack items, and the thickness-weighted sum of Dk
    auto span =
            [&]( int aFrom, int aTo, double& aHeight, double& aDkSum, double& aDielectric )
            {
                aHeight = aDkSum = aDielectric = 0.0;

                for( int ii = std::min( aFrom, aTo ) + 1; ii < std::max( aFrom, aTo ); ++ii )
                {
                    const BOARD_STACKUP_ITEM* item = list[ii];

                    if( item->GetType() == BS_ITEM_TYPE_DIELECTRIC )
                    {
                        for( int sub = 0; sub < item->GetSublayersCount(); ++sub )
                        {
                            double th = pcbIUScale.IUTomm( item->GetThickness( sub ) );
                            aHeight += th;
                            aDielectric += th;
                            aDkSum += th * item->GetEpsilonR( sub );
                        }
                    }
                    else if( item->GetType() == BS_ITEM_TYPE_COPPER )
                    {
                        aHeight += pcbIUScale.IUTomm( item->GetThickness() );
                    }
                }
            };

    int sig = itemIndex( aSignal );

    if( sig < 0 )
    {
        aError = _( "Layer not in the stack" );
        return false;
    }

    int top = aTop != UNDEFINED_LAYER ? itemIndex( aTop ) : -1;
    int bottom = aBottom != UNDEFINED_LAYER ? itemIndex( aBottom ) : -1;

    if( ( aTop != UNDEFINED_LAYER && ( top < 0 || top >= sig ) )
            || ( aBottom != UNDEFINED_LAYER && ( bottom < 0 || bottom <= sig ) ) )
    {
        aError = _( "Reference must be above (top) / below (bottom) the layer" );
        return false;
    }

    if( top < 0 && bottom < 0 )
    {
        aError = _( "No reference layer" );
        return false;
    }

    aGeom.t = pcbIUScale.IUTomm( list[sig]->GetThickness() );

    double h1 = 0, dk1 = 0, d1 = 0, h2 = 0, dk2 = 0, d2 = 0;

    if( top >= 0 )
        span( top, sig, h1, dk1, d1 );

    if( bottom >= 0 )
        span( sig, bottom, h2, dk2, d2 );

    aGeom.stripline = top >= 0 && bottom >= 0;
    aGeom.h1 = top >= 0 ? h1 : h2;
    aGeom.h2 = aGeom.stripline ? h2 : 0.0;

    double dielectric = d1 + d2;

    if( aGeom.h1 <= 0.0 || ( aGeom.stripline && aGeom.h2 <= 0.0 ) || dielectric <= 0.0 )
    {
        aError = _( "Dielectric thickness is 0" );
        return false;
    }

    aGeom.er = ( dk1 + dk2 ) / dielectric;

    if( aGeom.er < 1.0 )
    {
        aError = _( "Dielectric constant (Dk) not set" );
        return false;
    }

    return true;
}


wxString profileLabel( const TUNING_PROFILE& aProfile )
{
    bool diff = aProfile.m_Type == TUNING_PROFILE::PROFILE_TYPE::DIFFERENTIAL;

    return wxString::Format( wxS( "%s  (%s %g %s)" ), aProfile.m_ProfileName,
                             diff ? _( "Differential" ) : _( "Single-Ended" ),
                             aProfile.m_TargetImpedance, wxString::FromUTF8( "\xce\xa9" ) );
}


void styleTable( wxGrid* aGrid, wxWindow* aDip )
{
    aGrid->SetRowLabelSize( 0 );
    aGrid->SetColLabelSize( aDip->FromDIP( 26 ) );
    aGrid->SetDefaultRowSize( aDip->FromDIP( 24 ) );
    aGrid->SetColLabelAlignment( wxALIGN_LEFT, wxALIGN_CENTER );
    aGrid->SetSelectionMode( wxGrid::wxGridSelectRows );
    aGrid->DisableDragRowSize();
    aGrid->SetMargins( 0, 0 );

    // As the stack table: panel grey around the rows, dark header, dark cell lines
    wxColour face = sysColour( wxSYS_COLOUR_BTNFACE );
    aGrid->SetBackgroundColour( face );
    aGrid->GetGridWindow()->SetBackgroundColour( face );
    aGrid->SetDefaultCellBackgroundColour( face );
    aGrid->SetDefaultCellTextColour( rowText() );
    aGrid->SetDefaultRenderer( new STACK_CELL_RENDERER() );
    aGrid->SetLabelBackgroundColour( blend( face, *wxBLACK, 0.12 ) );
    aGrid->SetLabelTextColour( sysColour( wxSYS_COLOUR_BTNTEXT ) );
    aGrid->SetGridLineColour( blend( face, *wxBLACK, 0.40 ) );
    aGrid->SetCellHighlightPenWidth( 0 );
    aGrid->SetCellHighlightROPenWidth( 0 );
    aGrid->SetSelectionBackground( face );
    aGrid->SetDefaultCellAlignment( wxALIGN_LEFT, wxALIGN_CENTER );

    aGrid->Bind( wxEVT_GRID_SELECT_CELL,
                 [aGrid]( wxGridEvent& aEvent )
                 {
                     aEvent.Skip();
                     aGrid->CallAfter( [aGrid]() { aGrid->ForceRefresh(); } );
                 } );
}


/**
 * Altium's via-type drawing beside the stack table: one column per via type with its span
 * drawn as a plated barrel through the layer rows (pads on the copper rows it joins), a "#"
 * column with the copper layer numbers, and the rows in the stack's colours.
 */
class VIA_DRAWING : public wxPanel
{
public:
    struct ROW
    {
        BOARD_STACKUP_ITEM_TYPE type;
        int                     copper;     ///< 1-based copper number, 0 if not copper
    };

    struct VIA
    {
        wxString label;
        int      firstRow;
        int      lastRow;
    };

    VIA_DRAWING( wxWindow* aParent, std::function<void( int )> aOnSelect ) :
            wxPanel( aParent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE ),
            m_onSelect( std::move( aOnSelect ) )
    {
        SetBackgroundStyle( wxBG_STYLE_PAINT );
        Bind( wxEVT_PAINT, &VIA_DRAWING::onPaint, this );
        Bind( wxEVT_LEFT_DOWN,
              [this]( wxMouseEvent& aEvent )
              {
                  int x = aEvent.GetX() - numWidth();

                  if( x >= 0 && x / colWidth() < (int) m_vias.size() )
                      m_onSelect( x / colWidth() );
              } );
    }

    void Set( std::vector<ROW> aRows, std::vector<VIA> aVias, int aSelected, int aHeader,
              int aRowHeight )
    {
        m_rows = std::move( aRows );
        m_vias = std::move( aVias );
        m_selected = aSelected;
        m_header = aHeader;
        m_rowHeight = aRowHeight;
        Refresh();
    }

private:
    int numWidth() const { return FromDIP( 26 ); }
    int colWidth() const { return FromDIP( 110 ); }

    void onPaint( wxPaintEvent& )
    {
        wxAutoBufferedPaintDC dc( this );
        wxColour              face = sysColour( wxSYS_COLOUR_BTNFACE );
        wxColour              line = blend( face, *wxBLACK, 0.40 );
        wxSize                size = GetClientSize();

        dc.SetBackground( wxBrush( face ) );
        dc.Clear();
        dc.SetFont( GetFont() );

        // Header
        dc.SetPen( *wxTRANSPARENT_PEN );
        dc.SetBrush( wxBrush( blend( face, *wxBLACK, 0.12 ) ) );
        dc.DrawRectangle( 0, 0, size.x, m_header );
        dc.SetTextForeground( sysColour( wxSYS_COLOUR_BTNTEXT ) );

        int textY = ( m_header - dc.GetCharHeight() ) / 2;
        dc.DrawText( wxS( "#" ), FromDIP( 8 ), textY );

        for( size_t ii = 0; ii < m_vias.size(); ++ii )
        {
            int    x = numWidth() + (int) ii * colWidth();
            wxSize ext = dc.GetTextExtent( m_vias[ii].label );
            dc.DrawText( m_vias[ii].label, x + ( colWidth() - ext.x ) / 2, textY );
        }

        // Rows across the whole width, in the stack's colours
        for( size_t row = 0; row < m_rows.size(); ++row )
        {
            int      y = m_header + (int) row * m_rowHeight;
            wxColour bg = rowBackground( m_rows[row].type );

            dc.SetPen( *wxTRANSPARENT_PEN );
            dc.SetBrush( wxBrush( bg ) );
            dc.DrawRectangle( 0, y, size.x, m_rowHeight );

            if( m_rows[row].copper > 0 )
            {
                dc.SetTextForeground( rowText() );
                dc.DrawText( wxString::Format( wxS( "%d" ), m_rows[row].copper ), FromDIP( 4 ),
                             y + ( m_rowHeight - dc.GetCharHeight() ) / 2 );
            }

            // The via columns are shaded darker
            for( size_t ii = 0; ii < m_vias.size(); ++ii )
            {
                int x = numWidth() + (int) ii * colWidth();
                dc.SetBrush( wxBrush( blend( bg, *wxBLACK, 0.22 ) ) );
                dc.DrawRectangle( x + FromDIP( 2 ), y, colWidth() - FromDIP( 4 ), m_rowHeight );
            }

            dc.SetPen( wxPen( line ) );
            dc.DrawLine( 0, y + m_rowHeight - 1, size.x, y + m_rowHeight - 1 );
        }

        // Each via: pads on its copper rows, the plated barrel through its span
        wxColour pad( 0xC4, 0x9C, 0x7C );
        wxColour barrelEdge( 0xA8, 0x74, 0x45 );
        wxColour barrelMid( 0xD0, 0x9A, 0x62 );

        for( size_t ii = 0; ii < m_vias.size(); ++ii )
        {
            const VIA& via = m_vias[ii];
            int        x = numWidth() + (int) ii * colWidth();
            int        cx = x + colWidth() / 2;
            int        barrel = colWidth() / 3;
            int        padW = colWidth() * 2 / 3;

            if( via.firstRow < 0 || via.lastRow < via.firstRow )
                continue;

            for( int row = via.firstRow; row <= via.lastRow && row < (int) m_rows.size(); ++row )
            {
                int y = m_header + row * m_rowHeight;

                if( m_rows[row].type == BS_ITEM_TYPE_COPPER )
                {
                    dc.SetPen( wxPen( blend( pad, *wxBLACK, 0.45 ) ) );
                    dc.SetBrush( wxBrush( pad ) );
                    dc.DrawRectangle( cx - padW / 2, y, padW, m_rowHeight - 1 );
                }
                else
                {
                    // The drilled hole's plating clearance around the barrel
                    dc.SetPen( *wxTRANSPARENT_PEN );
                    dc.SetBrush( wxBrush( pad ) );
                    dc.DrawRectangle( cx - barrel / 2 - FromDIP( 3 ), y, barrel + FromDIP( 6 ),
                                      m_rowHeight );
                }
            }

            int top = m_header + via.firstRow * m_rowHeight;
            int bottom = m_header + ( via.lastRow + 1 ) * m_rowHeight - 1;

            dc.GradientFillLinear( wxRect( cx - barrel / 2, top, barrel / 2, bottom - top ),
                                   barrelEdge, barrelMid, wxEAST );
            dc.GradientFillLinear( wxRect( cx, top, barrel - barrel / 2, bottom - top ),
                                   barrelMid, barrelEdge, wxEAST );
            dc.SetPen( wxPen( blend( barrelEdge, *wxBLACK, 0.4 ) ) );
            dc.SetBrush( *wxTRANSPARENT_BRUSH );
            dc.DrawRectangle( cx - barrel / 2, top, barrel, bottom - top );

            if( (int) ii == m_selected )
            {
                dc.SetPen( wxPen( wxColour( 0x7F, 0xB8, 0xE8 ), FromDIP( 1 ) ) );
                dc.DrawRectangle( x + 1, 0, colWidth() - 2,
                                  m_header + (int) m_rows.size() * m_rowHeight );
            }
        }

        // Column dividers
        dc.SetPen( wxPen( line ) );
        dc.DrawLine( numWidth(), 0, numWidth(), m_header + (int) m_rows.size() * m_rowHeight );

        for( size_t ii = 1; ii <= m_vias.size(); ++ii )
        {
            int x = numWidth() + (int) ii * colWidth();
            dc.DrawLine( x, 0, x, m_header + (int) m_rows.size() * m_rowHeight );
        }
    }

    std::vector<ROW>           m_rows;
    std::vector<VIA>           m_vias;
    int                        m_selected = 0;
    int                        m_header = 26;
    int                        m_rowHeight = 24;
    std::function<void( int )> m_onSelect;
};

} // namespace


wxWindow* LAYER_STACK_MANAGER_FRAME::buildImpedancePage( wxWindow* aParent )
{
    // Altium's Impedance view: the stack at the left (with each signal layer's copper
    // orientation), the selected profile's per-layer results at the right
    wxSplitterWindow* split = new wxSplitterWindow( aParent, wxID_ANY, wxDefaultPosition,
                                                    wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3DSASH );
    split->SetSashGravity( 0.55 );
    split->SetMinimumPaneSize( FromDIP( 200 ) );

    m_impStack = new wxGrid( split, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE );
    m_impStack->CreateGrid( 0, 8 );
    styleTable( m_impStack, this );
    m_impStack->EnableEditing( false );

    const wxString stackLabels[] = { wxS( "#" ), _( "Name" ), _( "Material" ), _( "Type" ),
                                     _( "Weight" ), _( "Thickness" ), _( "Dk" ),
                                     _( "Copper Orientation" ) };

    for( int col = 0; col < 8; ++col )
        m_impStack->SetColLabelValue( col, stackLabels[col] );

    wxPanel*    right = new wxPanel( split );
    wxBoxSizer* rightSizer = new wxBoxSizer( wxVERTICAL );

    // Shown when there are no profiles yet: one centred button, as in Altium
    wxPanel*    empty = new wxPanel( right );
    wxBoxSizer* emptySizer = new wxBoxSizer( wxVERTICAL );
    wxButton*   addFirst = new wxButton( empty, wxID_ANY, _( "Add Impedance Profile" ) );
    addFirst->SetMinSize( wxSize( FromDIP( 190 ), FromDIP( 28 ) ) );
    emptySizer->AddStretchSpacer();
    emptySizer->Add( addFirst, 0, wxALIGN_CENTER_HORIZONTAL );
    emptySizer->AddStretchSpacer();
    empty->SetSizer( emptySizer );
    m_impEmpty = empty;

    // The profiles: a short list and the selected profile's settings, then its table
    wxPanel*    body = new wxPanel( right );
    wxBoxSizer* bodySizer = new wxBoxSizer( wxVERTICAL );
    m_impBody = body;

    wxBoxSizer* top = new wxBoxSizer( wxHORIZONTAL );

    m_profileList = new wxListBox( body, wxID_ANY, wxDefaultPosition, FromDIP( wxSize( 240, 84 ) ) );
    top->Add( m_profileList, 0, wxEXPAND | wxALL, FromDIP( 4 ) );

    wxFlexGridSizer* props = new wxFlexGridSizer( 2, FromDIP( 4 ), FromDIP( 6 ) );
    props->AddGrowableCol( 1 );

    m_profileName = new wxTextCtrl( body, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                    wxDefaultSize, wxTE_PROCESS_ENTER );
    m_profileType = new wxChoice( body, wxID_ANY );
    m_profileType->Append( _( "Single-Ended" ) );
    m_profileType->Append( _( "Differential" ) );
    m_profileTarget = new wxTextCtrl( body, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                      wxDefaultSize, wxTE_PROCESS_ENTER );

    props->Add( new wxStaticText( body, wxID_ANY, _( "Name" ) ), 0, wxALIGN_CENTER_VERTICAL );
    props->Add( m_profileName, 1, wxEXPAND );
    props->Add( new wxStaticText( body, wxID_ANY, _( "Type" ) ), 0, wxALIGN_CENTER_VERTICAL );
    props->Add( m_profileType, 1, wxEXPAND );
    props->Add( new wxStaticText( body, wxID_ANY,
                                  wxString::Format( _( "Target Impedance (%s)" ),
                                                    wxString::FromUTF8( "\xce\xa9" ) ) ),
                0, wxALIGN_CENTER_VERTICAL );
    props->Add( m_profileTarget, 1, wxEXPAND );
    top->Add( props, 1, wxEXPAND | wxALL, FromDIP( 4 ) );

    wxBoxSizer* actions = new wxBoxSizer( wxVERTICAL );
    m_profileRecalc = new wxButton( body, wxID_ANY, _( "Recalculate Widths" ) );
    m_profileRecalc->SetToolTip( _( "Calculate every layer's width (for the gap shown) from the "
                                    "target impedance and the current stackup" ) );
    m_profileDelete = new wxButton( body, wxID_ANY, _( "Delete Profile" ) );
    actions->Add( m_profileRecalc, 0, wxEXPAND | wxBOTTOM, FromDIP( 4 ) );
    actions->Add( m_profileDelete, 0, wxEXPAND );
    top->Add( actions, 0, wxALL, FromDIP( 4 ) );

    bodySizer->Add( top, 0, wxEXPAND );

    m_impGrid = new wxGrid( body, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE );
    m_impGrid->CreateGrid( 0, IMP_COUNT );
    styleTable( m_impGrid, this );

    m_impGrid->SetColLabelValue( IMP_LAYER, _( "Layer" ) );
    m_impGrid->SetColLabelValue( IMP_TOP_REF, _( "Top Ref" ) );
    m_impGrid->SetColLabelValue( IMP_BOTTOM_REF, _( "Bottom Ref" ) );
    m_impGrid->SetColLabelValue( IMP_WIDTH, _( "Width (W1)" ) );
    m_impGrid->SetColLabelValue( IMP_GAP, _( "Trace Gap" ) );
    m_impGrid->SetColLabelValue( IMP_IMPEDANCE, _( "Impedance" ) );
    m_impGrid->SetColLabelValue( IMP_DEVIATION, _( "Deviation" ) );
    m_impGrid->SetColLabelValue( IMP_DELAY, _( "Delay" ) );
    bodySizer->Add( m_impGrid, 1, wxEXPAND );

    wxStaticText* note = new wxStaticText(
            body, wxID_ANY,
            _( "Widths use IPC-2141 closed-form approximations (microstrip with one reference, "
               "stripline between two); confirm with your fabricator.  Profiles are stored in "
               "the project as tuning profiles; assign them to net classes in Board Setup." ) );
    note->SetForegroundColour( sysColour( wxSYS_COLOUR_GRAYTEXT ) );
    bodySizer->Add( note, 0, wxEXPAND | wxALL, FromDIP( 6 ) );
    body->SetSizer( bodySizer );

    rightSizer->Add( empty, 1, wxEXPAND );
    rightSizer->Add( body, 1, wxEXPAND );
    right->SetSizer( rightSizer );

    split->SplitVertically( m_impStack, right, FromDIP( 640 ) );

    note->Bind( wxEVT_SIZE,
                [note, this]( wxSizeEvent& aEvent )
                {
                    aEvent.Skip();
                    note->Wrap( std::max( FromDIP( 200 ), aEvent.GetSize().x ) );
                } );

    addFirst->Bind( wxEVT_BUTTON,
                    [this, addFirst]( wxCommandEvent& )
                    {
                        wxMenu menu;
                        menu.Append( 1, _( "Single-Ended" ) );
                        menu.Append( 2, _( "Differential" ) );
                        int id = addFirst->GetPopupMenuSelectionFromUser(
                                menu, 0, addFirst->GetSize().y );

                        if( id == 1 || id == 2 )
                            addProfile( id == 2 );
                    } );

    m_profileList->Bind( wxEVT_LISTBOX,
                         [this]( wxCommandEvent& )
                         {
                             m_profileSel = m_profileList->GetSelection();
                             refreshImpedance();
                         } );

    m_profileDelete->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { deleteProfile(); } );
    m_profileRecalc->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { recalculateProfile(); } );

    m_profileName->Bind( wxEVT_TEXT_ENTER,
                         [this]( wxCommandEvent& ) { onProfilePropertyChanged( 0 ); } );
    m_profileName->Bind( wxEVT_KILL_FOCUS,
                         [this]( wxFocusEvent& aEvent )
                         {
                             aEvent.Skip();
                             onProfilePropertyChanged( 0 );
                         } );
    m_profileType->Bind( wxEVT_CHOICE,
                         [this]( wxCommandEvent& ) { onProfilePropertyChanged( 1 ); } );
    m_profileTarget->Bind( wxEVT_TEXT_ENTER,
                           [this]( wxCommandEvent& ) { onProfilePropertyChanged( 2 ); } );
    m_profileTarget->Bind( wxEVT_KILL_FOCUS,
                           [this]( wxFocusEvent& aEvent )
                           {
                               aEvent.Skip();
                               onProfilePropertyChanged( 2 );
                           } );

    m_impGrid->Bind( wxEVT_GRID_CELL_CHANGED, &LAYER_STACK_MANAGER_FRAME::onImpedanceCellChanged,
                     this );
    m_impGrid->Bind( wxEVT_SIZE,
                     [this]( wxSizeEvent& aEvent )
                     {
                         aEvent.Skip();
                         fitColumns( m_impGrid, m_colBase[m_impGrid], { IMP_LAYER } );
                     } );
    m_impStack->Bind( wxEVT_SIZE,
                      [this]( wxSizeEvent& aEvent )
                      {
                          aEvent.Skip();
                          fitColumns( m_impStack, m_colBase[m_impStack], { 1, 2 } );
                      } );

    return split;
}


wxWindow* LAYER_STACK_MANAGER_FRAME::buildViaTypesPage( wxWindow* aParent )
{
    // Altium's Via Types view: the stack at the left, a drawing of every via type's span at the
    // right (one column each); the selected type's settings are edited in the table below it
    wxSplitterWindow* split = new wxSplitterWindow( aParent, wxID_ANY, wxDefaultPosition,
                                                    wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3DSASH );
    split->SetSashGravity( 0.5 );
    split->SetMinimumPaneSize( FromDIP( 200 ) );

    m_viaStack = new wxGrid( split, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE );
    m_viaStack->CreateGrid( 0, 4 );
    styleTable( m_viaStack, this );
    m_viaStack->EnableEditing( false );
    m_viaStack->SetColLabelValue( 0, wxS( "#" ) );
    m_viaStack->SetColLabelValue( 1, _( "Name" ) );
    m_viaStack->SetColLabelValue( 2, _( "Type" ) );
    m_viaStack->SetColLabelValue( 3, _( "Thickness" ) );

    wxPanel*    right = new wxPanel( split );
    wxBoxSizer* sizer = new wxBoxSizer( wxVERTICAL );

    m_viaDrawing = new VIA_DRAWING( right,
            [this]( int aColumn )
            {
                if( aColumn >= 0 && aColumn < m_viaGrid->GetNumberRows() )
                {
                    m_viaGrid->SetGridCursor( aColumn, VIA_NAME );
                    m_viaGrid->SelectRow( aColumn );
                    m_viaDelete->Enable( aColumn > 0 );
                    refreshViaTypes();
                    updateChrome();
                }
            } );
    sizer->Add( m_viaDrawing, 1, wxEXPAND );

    m_viaGrid = new wxGrid( right, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE );
    m_viaGrid->CreateGrid( 0, VIA_COUNT );
    styleTable( m_viaGrid, this );

    m_viaGrid->SetColLabelValue( VIA_NAME, _( "Name" ) );
    m_viaGrid->SetColLabelValue( VIA_KIND, _( "Type" ) );
    m_viaGrid->SetColLabelValue( VIA_FIRST, _( "First Layer" ) );
    m_viaGrid->SetColLabelValue( VIA_LAST, _( "Last Layer" ) );
    m_viaGrid->SetColLabelValue( VIA_ENABLED, _( "Enabled" ) );
    m_viaGrid->SetMinSize( wxSize( -1, FromDIP( 150 ) ) );
    sizer->Add( m_viaGrid, 0, wxEXPAND | wxTOP, FromDIP( 4 ) );

    // Kept for the toolbar's Delete (enabled when a blind / buried / micro via is selected)
    m_viaDelete = new wxButton( right, wxID_ANY, _( "Delete Via Type" ) );
    m_viaDelete->Hide();

    m_viaInfo = new wxStaticText( right, wxID_ANY, wxEmptyString );
    m_viaInfo->SetForegroundColour( sysColour( wxSYS_COLOUR_GRAYTEXT ) );
    sizer->Add( m_viaInfo, 0, wxEXPAND | wxALL, FromDIP( 6 ) );

    right->SetSizer( sizer );
    split->SplitVertically( m_viaStack, right, FromDIP( 520 ) );

    m_viaDelete->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { deleteViaType(); } );

    m_viaGrid->Bind( wxEVT_GRID_CELL_CHANGED, &LAYER_STACK_MANAGER_FRAME::onViaCellChanged, this );
    m_viaGrid->Bind( wxEVT_GRID_SELECT_CELL,
                     [this]( wxGridEvent& aEvent )
                     {
                         aEvent.Skip();

                         if( m_refreshingVias )
                             return;

                         // Moving within a row (e.g. to edit First Layer) needs no rebuild: that
                         // would close the cell editor and move the cursor
                         if( aEvent.GetRow() == m_lastViaRow )
                         {
                             m_viaDelete->Enable( aEvent.GetRow() > 0 );
                             return;
                         }

                         CallAfter( [this]()
                                    {
                                        m_viaDelete->Enable( m_viaGrid->GetGridCursorRow() > 0 );
                                        refreshViaTypes();
                                        updateChrome();
                                    } );
                     } );
    m_viaGrid->Bind( wxEVT_SIZE,
                     [this]( wxSizeEvent& aEvent )
                     {
                         aEvent.Skip();
                         fitColumns( m_viaGrid, m_colBase[m_viaGrid], { VIA_NAME } );
                     } );
    m_viaStack->Bind( wxEVT_SIZE,
                      [this]( wxSizeEvent& aEvent )
                      {
                          aEvent.Skip();
                          fitColumns( m_viaStack, m_colBase[m_viaStack], { 1 } );
                      } );

    return split;
}


void LAYER_STACK_MANAGER_FRAME::fillStackView( wxGrid* aGrid, const std::vector<int>& aColumns )
{
    // A read-only copy of the stack table's rows (only the given columns, and for the
    // Impedance view a "Copper Orientation" column at the end, given as -1)
    wxGridUpdateLocker lock( aGrid );

    if( aGrid->GetNumberRows() > 0 )
        aGrid->DeleteRows( 0, aGrid->GetNumberRows() );

    aGrid->AppendRows( (int) m_rows.size() );

    std::vector<PCB_LAYER_ID> stack = copperStack( m_state.m_stackup );

    for( int row = 0; row < (int) m_rows.size(); ++row )
    {
        BOARD_STACKUP_ITEM* item = m_state.m_stackup.GetList()[m_rows[row].m_item];
        wxColour            bg = rowBackground( item->GetType() );

        for( int col = 0; col < (int) aColumns.size(); ++col )
        {
            aGrid->SetCellBackgroundColour( row, col, bg );
            aGrid->SetCellTextColour( row, col, rowText() );
            aGrid->SetReadOnly( row, col, true );

            if( aColumns[col] >= 0 )
            {
                aGrid->SetCellValue( row, col, m_grid->GetCellValue( row, aColumns[col] ) );
            }
            else if( item->GetType() == BS_ITEM_TYPE_COPPER )
            {
                // Which way the copper's foil faces: the bottom layer's faces down
                bool last = !stack.empty() && item->GetBrdLayerId() == stack.back();
                aGrid->SetCellValue( row, col, last ? _( "Below" ) : _( "Above" ) );
            }
        }
    }

    aGrid->AutoSizeColumns( false );

    std::vector<int>& base = m_colBase[aGrid];
    base.assign( aColumns.size(), 0 );

    for( int col = 0; col < (int) aColumns.size(); ++col )
    {
        int minimum = col == 0 ? 30 : col == 1 ? 140 : 90;
        base[col] = std::max( aGrid->GetColSize( col ), FromDIP( minimum ) ) + FromDIP( 12 );
    }

    fitColumns( aGrid, base, { 1 } );
}


void LAYER_STACK_MANAGER_FRAME::onAddButton()
{
    wxWindow* btn = m_tools[ID_ADD_PAIR];
    wxMenu    menu;

    switch( m_view )
    {
    case 1:
        menu.Append( 1, _( "Single-Ended Impedance Profile" ) );
        menu.Append( 2, _( "Differential Impedance Profile" ) );

        if( int id = btn->GetPopupMenuSelectionFromUser( menu, 0, btn->GetSize().y );
            id == 1 || id == 2 )
        {
            addProfile( id == 2 );
        }

        break;

    case 2:
        addViaType();
        break;

    default:
        fillAddMenu( menu );
        btn->PopupMenu( &menu, 0, btn->GetSize().y );
        break;
    }
}


void LAYER_STACK_MANAGER_FRAME::onDeleteButton()
{
    switch( m_view )
    {
    case 1:  deleteProfile();  break;
    case 2:  deleteViaType();  break;
    default: deleteLayer();    break;
    }
}


void LAYER_STACK_MANAGER_FRAME::refreshViews()
{
    if( m_impStack )
    {
        fillStackView( m_impStack, { COL_NUM, COL_NAME, COL_MATERIAL, COL_TYPE, COL_WEIGHT,
                                     COL_THICKNESS, COL_DK, -1 } );
    }

    if( m_viaStack )
        fillStackView( m_viaStack, { COL_NUM, COL_NAME, COL_TYPE, COL_THICKNESS } );

    if( m_impGrid )
        refreshImpedance();

    if( m_viaGrid )
        refreshViaTypes();
}


void LAYER_STACK_MANAGER_FRAME::syncDerived( STATE& aState ) const
{
    std::vector<PCB_LAYER_ID> stack = copperStack( aState.m_stackup );

    while( aState.m_profileOrig.size() < aState.m_profiles.size() )
        aState.m_profileOrig.push_back( wxEmptyString );

    // Each profile has one row per copper layer, in stack order
    for( TUNING_PROFILE& profile : aState.m_profiles )
    {
        bool diff = profile.m_Type == TUNING_PROFILE::PROFILE_TYPE::DIFFERENTIAL;
        std::vector<DELAY_PROFILE_TRACK_PROPAGATION_ENTRY> entries;

        for( int ii = 0; ii < (int) stack.size(); ++ii )
        {
            PCB_LAYER_ID layer = stack[ii];
            auto         existing = std::find_if( profile.m_TrackPropagationEntries.begin(),
                                                  profile.m_TrackPropagationEntries.end(),
                                                  [&]( const auto& aEntry )
                                                  {
                                                      return aEntry.GetSignalLayer() == layer;
                                                  } );

            if( existing != profile.m_TrackPropagationEntries.end() )
            {
                DELAY_PROFILE_TRACK_PROPAGATION_ENTRY entry = *existing;

                // References to layers no longer in the stack fall back to the neighbours
                if( entry.GetTopReferenceLayer() != UNDEFINED_LAYER
                        && stackIndex( stack, entry.GetTopReferenceLayer() ) < 0 )
                {
                    entry.SetTopReferenceLayer( ii > 0 ? stack[ii - 1] : UNDEFINED_LAYER );
                }

                if( entry.GetBottomReferenceLayer() != UNDEFINED_LAYER
                        && stackIndex( stack, entry.GetBottomReferenceLayer() ) < 0 )
                {
                    entry.SetBottomReferenceLayer( ii + 1 < (int) stack.size() ? stack[ii + 1]
                                                                               : UNDEFINED_LAYER );
                }

                entries.push_back( entry );
                continue;
            }

            // New layer: nearest copper layers as references, width from the target
            DELAY_PROFILE_TRACK_PROPAGATION_ENTRY entry;
            entry.SetSignalLayer( layer );
            entry.SetTopReferenceLayer( ii > 0 ? stack[ii - 1] : UNDEFINED_LAYER );
            entry.SetBottomReferenceLayer( ii + 1 < (int) stack.size() ? stack[ii + 1]
                                                                       : UNDEFINED_LAYER );
            entry.SetDiffPairGap( diff ? pcbIUScale.mmToIU( 0.15 ) : 0 );
            entry.SetWidth( pcbIUScale.mmToIU( 0.2 ) );
            entry.SetEnableTimeDomainTuning( profile.m_EnableTimeDomainTuning );
            computeEntry( aState, profile, entry, true );
            entries.push_back( entry );
        }

        profile.m_TrackPropagationEntries = entries;
    }

    // Via types whose layers left the stack go too
    std::vector<LAYER_PAIR_INFO> viaTypes;

    for( const LAYER_PAIR_INFO& info : aState.m_viaTypes )
    {
        if( stackIndex( stack, info.GetLayerPair().GetLayerA() ) >= 0
                && stackIndex( stack, info.GetLayerPair().GetLayerB() ) >= 0 )
        {
            viaTypes.push_back( info );
        }
    }

    aState.m_viaTypes = viaTypes;
}


bool LAYER_STACK_MANAGER_FRAME::evaluateEntry( const STATE& aState, const TUNING_PROFILE& aProfile,
                                               const DELAY_PROFILE_TRACK_PROPAGATION_ENTRY& aEntry,
                                               double& aImpedance, double& aDelayPsPerMm,
                                               wxString& aError ) const
{
    LINE_GEOMETRY geom;

    if( !lineGeometry( aState.m_stackup, aEntry.GetSignalLayer(), aEntry.GetTopReferenceLayer(),
                       aEntry.GetBottomReferenceLayer(), geom, aError ) )
    {
        return false;
    }

    double width = pcbIUScale.IUTomm( aEntry.GetWidth() );

    if( width <= 0.0 )
    {
        aError = _( "Width is 0" );
        return false;
    }

    if( aProfile.m_Type == TUNING_PROFILE::PROFILE_TYPE::DIFFERENTIAL )
        aImpedance = differentialZ( geom, width, pcbIUScale.IUTomm( aEntry.GetDiffPairGap() ) );
    else
        aImpedance = singleEndedZ( geom, width );

    aDelayPsPerMm = delayPsPerMm( geom );

    if( aImpedance <= 0.0 )
    {
        aError = _( "Trace too wide for the dielectric" );
        return false;
    }

    return true;
}


void LAYER_STACK_MANAGER_FRAME::computeEntry( const STATE& aState, const TUNING_PROFILE& aProfile,
                                              DELAY_PROFILE_TRACK_PROPAGATION_ENTRY& aEntry,
                                              bool aSolveWidth ) const
{
    LINE_GEOMETRY geom;
    wxString      error;

    if( !lineGeometry( aState.m_stackup, aEntry.GetSignalLayer(), aEntry.GetTopReferenceLayer(),
                       aEntry.GetBottomReferenceLayer(), geom, error ) )
    {
        return;
    }

    bool diff = aProfile.m_Type == TUNING_PROFILE::PROFILE_TYPE::DIFFERENTIAL;

    if( aSolveWidth )
    {
        double width = solveWidth( geom, aProfile.m_TargetImpedance, diff,
                                   pcbIUScale.IUTomm( aEntry.GetDiffPairGap() ) );

        // Rounded to 0.1 um, as a fabricator would quote it
        if( width > 0.0 )
            aEntry.SetWidth( KiROUND( pcbIUScale.mmToIU( width ) / 100.0 ) * 100 );
    }

    // Propagation delay per length, used by KiCad's time-domain tuning
    aEntry.SetDelay( KiROUND( EDA_UNIT_UTILS::UI::FromUserUnit( pcbIUScale, EDA_UNITS::PS_PER_MM,
                                                                delayPsPerMm( geom ) ) ) );
}


TUNING_PROFILE* LAYER_STACK_MANAGER_FRAME::currentProfile()
{
    if( m_profileSel >= 0 && m_profileSel < (int) m_state.m_profiles.size() )
        return &m_state.m_profiles[m_profileSel];

    return nullptr;
}


void LAYER_STACK_MANAGER_FRAME::refreshProfileList()
{
    wxArrayString labels;

    for( const TUNING_PROFILE& profile : m_state.m_profiles )
        labels.Add( profileLabel( profile ) );

    if( m_profileSel >= (int) labels.size() )
        m_profileSel = (int) labels.size() - 1;

    if( m_profileSel < 0 && !labels.IsEmpty() )
        m_profileSel = 0;

    bool same = m_profileList->GetCount() == labels.size();

    for( unsigned ii = 0; same && ii < labels.size(); ++ii )
        same = m_profileList->GetString( ii ) == labels[ii];

    if( !same )
        m_profileList->Set( labels );

    if( m_profileSel >= 0 && m_profileList->GetSelection() != m_profileSel )
        m_profileList->SetSelection( m_profileSel );
}


void LAYER_STACK_MANAGER_FRAME::refreshImpedance()
{
    refreshProfileList();

    TUNING_PROFILE* profile = currentProfile();
    bool            has = profile != nullptr;
    bool            diff = has && profile->m_Type == TUNING_PROFILE::PROFILE_TYPE::DIFFERENTIAL;

    if( m_impEmpty && m_impEmpty->IsShown() == has )
    {
        m_impEmpty->Show( !has );
        m_impBody->Show( has );
        m_impEmpty->GetParent()->Layout();
    }

    m_profileName->Enable( has );
    m_profileType->Enable( has );
    m_profileTarget->Enable( has );
    m_profileDelete->Enable( has );
    m_profileRecalc->Enable( has );

    if( !m_profileName->HasFocus() )
        m_profileName->ChangeValue( has ? profile->m_ProfileName : wxString() );

    m_profileType->SetSelection( has ? ( diff ? 1 : 0 ) : wxNOT_FOUND );

    if( !m_profileTarget->HasFocus() )
    {
        m_profileTarget->ChangeValue( has ? wxString::Format( wxS( "%g" ),
                                                              profile->m_TargetImpedance )
                                          : wxString() );
    }

    std::vector<PCB_LAYER_ID> stack = copperStack( m_state.m_stackup );
    bool imperial = EDA_UNIT_UTILS::IsImperialUnit( m_frame->GetUserUnits() );
    wxString ohm = wxString::FromUTF8( "\xce\xa9" );
    wxColour dimText = sysColour( wxSYS_COLOUR_GRAYTEXT );

    wxGridUpdateLocker lock( m_impGrid );

    if( m_impGrid->IsCellEditControlShown() )
        m_impGrid->HideCellEditControl();

    if( m_impGrid->GetNumberRows() > 0 )
        m_impGrid->DeleteRows( 0, m_impGrid->GetNumberRows() );

    m_impRows.clear();

    if( has )
    {
        m_impGrid->AppendRows( (int) profile->m_TrackPropagationEntries.size() );

        for( int row = 0; row < (int) profile->m_TrackPropagationEntries.size(); ++row )
        {
            const DELAY_PROFILE_TRACK_PROPAGATION_ENTRY& entry =
                    profile->m_TrackPropagationEntries[row];
            PCB_LAYER_ID layer = entry.GetSignalLayer();
            int          idx = stackIndex( stack, layer );

            m_impRows.push_back( layer );

            for( int col = 0; col < IMP_COUNT; ++col )
            {
                m_impGrid->SetReadOnly( row, col, true );
                m_impGrid->SetCellBackgroundColour( row, col,
                                                    rowBackground( BS_ITEM_TYPE_COPPER ) );
            }

            m_impGrid->SetCellValue( row, IMP_LAYER, layerDisplayName( layer ) );

            // Reference choices: none, or a copper layer on that side
            wxArrayString above, below;
            above.Add( _( "None" ) );
            below.Add( _( "None" ) );

            for( int ii = 0; ii < (int) stack.size(); ++ii )
            {
                if( ii < idx )
                    above.Add( layerDisplayName( stack[ii] ) );
                else if( ii > idx )
                    below.Add( layerDisplayName( stack[ii] ) );
            }

            auto refName = [&]( PCB_LAYER_ID aRef )
                           {
                               return aRef == UNDEFINED_LAYER ? wxString( _( "None" ) )
                                                              : layerDisplayName( aRef );
                           };

            m_impGrid->SetCellValue( row, IMP_TOP_REF, refName( entry.GetTopReferenceLayer() ) );
            m_impGrid->SetCellEditor( row, IMP_TOP_REF, new wxGridCellChoiceEditor( above ) );
            m_impGrid->SetReadOnly( row, IMP_TOP_REF, false );

            m_impGrid->SetCellValue( row, IMP_BOTTOM_REF,
                                     refName( entry.GetBottomReferenceLayer() ) );
            m_impGrid->SetCellEditor( row, IMP_BOTTOM_REF, new wxGridCellChoiceEditor( below ) );
            m_impGrid->SetReadOnly( row, IMP_BOTTOM_REF, false );

            m_impGrid->SetCellValue( row, IMP_WIDTH,
                                     m_frame->StringFromValue( entry.GetWidth(), true ) );
            m_impGrid->SetReadOnly( row, IMP_WIDTH, false );

            if( diff )
            {
                m_impGrid->SetCellValue( row, IMP_GAP,
                                         m_frame->StringFromValue( entry.GetDiffPairGap(), true ) );
                m_impGrid->SetReadOnly( row, IMP_GAP, false );
            }
            else
            {
                m_impGrid->SetCellValue( row, IMP_GAP, wxS( "-" ) );
                m_impGrid->SetCellTextColour( row, IMP_GAP, dimText );
            }

            double   impedance = 0.0;
            double   delay = 0.0;
            wxString error;

            if( evaluateEntry( m_state, *profile, entry, impedance, delay, error ) )
            {
                double deviation = profile->m_TargetImpedance > 0.0
                                           ? ( impedance / profile->m_TargetImpedance - 1.0 ) * 100.0
                                           : 0.0;

                m_impGrid->SetCellValue( row, IMP_IMPEDANCE,
                                         wxString::Format( wxS( "%.2f %s" ), impedance, ohm ) );
                m_impGrid->SetCellValue( row, IMP_DEVIATION,
                                         wxString::Format( wxS( "%+.1f %%" ), deviation ) );

                if( std::abs( deviation ) > 10.0 )
                    m_impGrid->SetCellTextColour( row, IMP_DEVIATION, wxColour( 210, 50, 50 ) );

                m_impGrid->SetCellValue( row, IMP_DELAY,
                                         imperial ? wxString::Format( _( "%.1f ps/in" ),
                                                                      delay * 25.4 )
                                                  : wxString::Format( _( "%.2f ps/mm" ), delay ) );
            }
            else
            {
                m_impGrid->SetCellValue( row, IMP_IMPEDANCE, error );
                m_impGrid->SetCellTextColour( row, IMP_IMPEDANCE, wxColour( 210, 50, 50 ) );
            }
        }
    }

    m_impGrid->AutoSizeColumns( false );

    const int        minWidths[IMP_COUNT] = { 150, 120, 120, 90, 90, 100, 80, 90 };
    std::vector<int>& base = m_colBase[m_impGrid];
    base.assign( IMP_COUNT, 0 );

    for( int col = 0; col < IMP_COUNT; ++col )
        base[col] = std::max( m_impGrid->GetColSize( col ), FromDIP( minWidths[col] ) )
                    + FromDIP( 12 );

    fitColumns( m_impGrid, base, { IMP_LAYER } );
}


void LAYER_STACK_MANAGER_FRAME::addProfile( bool aDifferential )
{
    STATE before = m_state;

    wxString stem = aDifferential ? _( "Differential" ) : _( "Single-Ended" );
    wxString name;

    for( int ii = 1; name.IsEmpty(); ++ii )
    {
        wxString candidate = wxString::Format( wxS( "%s %d" ), stem, ii );

        if( std::none_of( m_state.m_profiles.begin(), m_state.m_profiles.end(),
                          [&]( const TUNING_PROFILE& aProfile )
                          {
                              return aProfile.m_ProfileName == candidate;
                          } ) )
        {
            name = candidate;
        }
    }

    TUNING_PROFILE profile;
    profile.m_ProfileName = name;
    profile.m_Type = aDifferential ? TUNING_PROFILE::PROFILE_TYPE::DIFFERENTIAL
                                   : TUNING_PROFILE::PROFILE_TYPE::SINGLE;
    profile.m_TargetImpedance = aDifferential ? 100.0 : 50.0;
    profile.m_EnableTimeDomainTuning = false;
    profile.m_ViaPropagationDelay = 0;

    m_state.m_profiles.push_back( profile );
    m_state.m_profileOrig.push_back( wxEmptyString );
    syncDerived( m_state );

    m_profileSel = (int) m_state.m_profiles.size() - 1;
    commitChange( before, wxString::Format( _( "Added impedance profile '%s'." ), name ) );
}


void LAYER_STACK_MANAGER_FRAME::deleteProfile()
{
    if( !currentProfile() )
        return;

    STATE    before = m_state;
    wxString name = currentProfile()->m_ProfileName;

    m_state.m_profiles.erase( m_state.m_profiles.begin() + m_profileSel );

    if( m_profileSel < (int) m_state.m_profileOrig.size() )
        m_state.m_profileOrig.erase( m_state.m_profileOrig.begin() + m_profileSel );

    commitChange( before, wxString::Format( _( "Deleted impedance profile '%s'.  Net classes "
                                               "using it no longer have a profile." ),
                                            name ) );
}


void LAYER_STACK_MANAGER_FRAME::recalculateProfile()
{
    TUNING_PROFILE* profile = currentProfile();

    if( !profile )
        return;

    STATE before = m_state;
    profile = currentProfile();

    for( DELAY_PROFILE_TRACK_PROPAGATION_ENTRY& entry : profile->m_TrackPropagationEntries )
        computeEntry( m_state, *profile, entry, true );

    commitChange( before, _( "Widths recalculated from the target impedance." ) );
}


void LAYER_STACK_MANAGER_FRAME::onProfilePropertyChanged( int aWhich )
{
    TUNING_PROFILE* profile = currentProfile();

    if( !profile || IsBeingDeleted() )
        return;

    STATE before = m_state;
    profile = currentProfile();

    switch( aWhich )
    {
    case 0:
    {
        wxString name = m_profileName->GetValue().Strip( wxString::both );

        if( name == profile->m_ProfileName )
            return;

        bool duplicate = std::any_of( m_state.m_profiles.begin(), m_state.m_profiles.end(),
                                      [&]( const TUNING_PROFILE& aOther )
                                      {
                                          return &aOther != profile
                                                 && aOther.m_ProfileName == name;
                                      } );

        if( name.IsEmpty() || duplicate )
        {
            wxBell();
            m_profileName->ChangeValue( profile->m_ProfileName );
            setStatus( name.IsEmpty() ? _( "A profile needs a name." )
                                      : wxString::Format( _( "A profile named '%s' already "
                                                             "exists." ), name ),
                       true );
            return;
        }

        profile->m_ProfileName = name;
        break;
    }

    case 1:
    {
        bool diff = m_profileType->GetSelection() == 1;
        auto type = diff ? TUNING_PROFILE::PROFILE_TYPE::DIFFERENTIAL
                         : TUNING_PROFILE::PROFILE_TYPE::SINGLE;

        if( type == profile->m_Type )
            return;

        profile->m_Type = type;

        // Typical targets for each kind; widths follow
        if( diff && profile->m_TargetImpedance == 50.0 )
            profile->m_TargetImpedance = 100.0;
        else if( !diff && profile->m_TargetImpedance == 100.0 )
            profile->m_TargetImpedance = 50.0;

        for( DELAY_PROFILE_TRACK_PROPAGATION_ENTRY& entry : profile->m_TrackPropagationEntries )
        {
            if( diff && entry.GetDiffPairGap() <= 0 )
                entry.SetDiffPairGap( pcbIUScale.mmToIU( 0.15 ) );

            computeEntry( m_state, *profile, entry, true );
        }

        break;
    }

    case 2:
    {
        double target = 0.0;
        wxString text = m_profileTarget->GetValue().Strip( wxString::both );

        if( !( text.ToDouble( &target ) || text.ToCDouble( &target ) ) || target <= 0.0 )
        {
            wxBell();
            m_profileTarget->ChangeValue( wxString::Format( wxS( "%g" ),
                                                            profile->m_TargetImpedance ) );
            setStatus( _( "The target impedance must be a positive number of ohms." ), true );
            return;
        }

        if( target == profile->m_TargetImpedance )
            return;

        profile->m_TargetImpedance = target;

        for( DELAY_PROFILE_TRACK_PROPAGATION_ENTRY& entry : profile->m_TrackPropagationEntries )
            computeEntry( m_state, *profile, entry, true );

        break;
    }

    default:
        return;
    }

    commitChange( before );
}


void LAYER_STACK_MANAGER_FRAME::onImpedanceCellChanged( wxGridEvent& aEvent )
{
    int             row = aEvent.GetRow();
    int             col = aEvent.GetCol();
    TUNING_PROFILE* profile = currentProfile();

    if( !profile || row < 0 || row >= (int) profile->m_TrackPropagationEntries.size() )
        return;

    STATE    before = m_state;
    wxString value = m_impGrid->GetCellValue( row, col ).Strip( wxString::both );
    wxString error;

    profile = currentProfile();
    DELAY_PROFILE_TRACK_PROPAGATION_ENTRY& entry = profile->m_TrackPropagationEntries[row];

    auto layerFromName =
            [&]( const wxString& aName )
            {
                for( PCB_LAYER_ID layer : copperStack( m_state.m_stackup ) )
                {
                    if( layerDisplayName( layer ) == aName )
                        return layer;
                }

                return UNDEFINED_LAYER;
            };

    switch( col )
    {
    case IMP_TOP_REF:
        entry.SetTopReferenceLayer( layerFromName( value ) );
        computeEntry( m_state, *profile, entry, true );
        break;

    case IMP_BOTTOM_REF:
        entry.SetBottomReferenceLayer( layerFromName( value ) );
        computeEntry( m_state, *profile, entry, true );
        break;

    case IMP_WIDTH:
    {
        int width = m_frame->ValueFromString( value );

        if( value.IsEmpty() || width <= 0 )
            error = _( "The width must be more than 0." );
        else
            entry.SetWidth( width );

        break;
    }

    case IMP_GAP:
    {
        int gap = m_frame->ValueFromString( value );

        if( value.IsEmpty() || gap <= 0 )
        {
            error = _( "The gap must be more than 0." );
        }
        else
        {
            // The width follows the gap, to keep the target impedance
            entry.SetDiffPairGap( gap );
            computeEntry( m_state, *profile, entry, true );
        }

        break;
    }

    default:
        return;
    }

    if( !error.IsEmpty() )
    {
        m_state = before;
        wxBell();
        CallAfter( [this, error]()
                   {
                       refreshGrid();
                       setStatus( error, true );
                   } );
        return;
    }

    CallAfter( [this, before]() { commitChange( before ); } );
}


wxString LAYER_STACK_MANAGER_FRAME::viaTypeKind( PCB_LAYER_ID aFirst, PCB_LAYER_ID aLast ) const
{
    std::vector<PCB_LAYER_ID> stack = copperStack( m_state.m_stackup );
    int                       a = stackIndex( stack, aFirst );
    int                       b = stackIndex( stack, aLast );
    int                       last = (int) stack.size() - 1;

    if( a > b )
        std::swap( a, b );

    if( a == 0 && b == last )
        return _( "Through" );

    if( a == 0 || b == last )
        return b - a == 1 ? _( "Micro-via" ) : _( "Blind" );

    return _( "Buried" );
}


void LAYER_STACK_MANAGER_FRAME::refreshViaTypes()
{
    // Rebuilding the grid moves its cursor, whose select event would refresh this view again
    if( m_refreshingVias )
        return;

    struct GUARD
    {
        bool& flag;
        GUARD( bool& aFlag ) : flag( aFlag ) { flag = true; }
        ~GUARD() { flag = false; }
    } guard( m_refreshingVias );

    std::vector<PCB_LAYER_ID> stack = copperStack( m_state.m_stackup );
    wxArrayString             layerNames;

    for( PCB_LAYER_ID layer : stack )
        layerNames.Add( layerDisplayName( layer ) );

    auto copperNumber = [&]( PCB_LAYER_ID aLayer )
                        {
                            return stackIndex( stack, aLayer ) + 1;
                        };

    wxColour dimText = sysColour( wxSYS_COLOUR_GRAYTEXT );
    int      cursor = m_viaGrid->GetNumberRows() > 0 ? m_viaGrid->GetGridCursorRow() : 0;
    int      cursorCol = m_viaGrid->GetNumberRows() > 0 ? m_viaGrid->GetGridCursorCol() : VIA_NAME;

    wxGridUpdateLocker lock( m_viaGrid );

    if( m_viaGrid->IsCellEditControlShown() )
        m_viaGrid->HideCellEditControl();

    if( m_viaGrid->GetNumberRows() > 0 )
        m_viaGrid->DeleteRows( 0, m_viaGrid->GetNumberRows() );

    m_viaGrid->AppendRows( 1 + (int) m_state.m_viaTypes.size() );

    for( int row = 0; row < m_viaGrid->GetNumberRows(); ++row )
    {
        for( int col = 0; col < VIA_COUNT; ++col )
            m_viaGrid->SetReadOnly( row, col, true );

        m_viaGrid->SetCellRenderer( row, VIA_ENABLED, new wxGridCellBoolRenderer );
        m_viaGrid->SetCellAlignment( row, VIA_ENABLED, wxALIGN_CENTER, wxALIGN_CENTER );
    }

    // Through vias always exist
    m_viaGrid->SetCellValue( 0, VIA_NAME, wxString::Format( wxS( "Thru 1:%d" ),
                                                           (int) stack.size() ) );
    m_viaGrid->SetCellValue( 0, VIA_KIND, _( "Through" ) );
    m_viaGrid->SetCellValue( 0, VIA_FIRST, layerNames.IsEmpty() ? wxString() : layerNames[0] );
    m_viaGrid->SetCellValue( 0, VIA_LAST, layerNames.IsEmpty() ? wxString() : layerNames.Last() );
    m_viaGrid->SetCellValue( 0, VIA_ENABLED, wxS( "1" ) );

    for( int col = 0; col < VIA_COUNT; ++col )
        m_viaGrid->SetCellTextColour( 0, col, dimText );

    for( int ii = 0; ii < (int) m_state.m_viaTypes.size(); ++ii )
    {
        const LAYER_PAIR_INFO& info = m_state.m_viaTypes[ii];
        PCB_LAYER_ID           first = info.GetLayerPair().GetLayerA();
        PCB_LAYER_ID           last = info.GetLayerPair().GetLayerB();
        int                    row = ii + 1;

        if( copperNumber( first ) > copperNumber( last ) )
            std::swap( first, last );

        wxString kind = viaTypeKind( first, last );
        wxString name = info.GetName() ? *info.GetName()
                                       : wxString::Format( wxS( "%s %d:%d" ), kind,
                                                           copperNumber( first ),
                                                           copperNumber( last ) );

        m_viaGrid->SetCellValue( row, VIA_NAME, name );
        m_viaGrid->SetReadOnly( row, VIA_NAME, false );

        if( !info.GetName() )
            m_viaGrid->SetCellTextColour( row, VIA_NAME, dimText );

        m_viaGrid->SetCellValue( row, VIA_KIND, kind );

        m_viaGrid->SetCellValue( row, VIA_FIRST, layerDisplayName( first ) );
        m_viaGrid->SetCellEditor( row, VIA_FIRST, new wxGridCellChoiceEditor( layerNames ) );
        m_viaGrid->SetReadOnly( row, VIA_FIRST, false );

        m_viaGrid->SetCellValue( row, VIA_LAST, layerDisplayName( last ) );
        m_viaGrid->SetCellEditor( row, VIA_LAST, new wxGridCellChoiceEditor( layerNames ) );
        m_viaGrid->SetReadOnly( row, VIA_LAST, false );

        m_viaGrid->SetCellValue( row, VIA_ENABLED, info.IsEnabled() ? wxS( "1" ) : wxS( "" ) );
        m_viaGrid->SetCellEditor( row, VIA_ENABLED, new wxGridCellBoolEditor );
        m_viaGrid->SetReadOnly( row, VIA_ENABLED, false );
    }

    m_viaGrid->AutoSizeColumns( false );

    const int        minWidths[VIA_COUNT] = { 180, 100, 150, 150, 70 };
    std::vector<int>& base = m_colBase[m_viaGrid];
    base.assign( VIA_COUNT, 0 );

    for( int col = 0; col < VIA_COUNT; ++col )
        base[col] = std::max( m_viaGrid->GetColSize( col ), FromDIP( minWidths[col] ) )
                    + FromDIP( 12 );

    fitColumns( m_viaGrid, base, { VIA_NAME } );

    cursor = std::clamp( cursor, 0, m_viaGrid->GetNumberRows() - 1 );

    // Moving the cursor fires a select event, which refreshes this view again: only move it
    // when it isn't already there, and never from inside a refresh
    // Keep the column too, so the First/Last Layer and Enabled cells stay reachable
    cursorCol = std::clamp( cursorCol, 0, VIA_COUNT - 1 );

    if( m_viaGrid->GetGridCursorRow() != cursor || m_viaGrid->GetGridCursorCol() != cursorCol )
        m_viaGrid->SetGridCursor( cursor, cursorCol );

    m_lastViaRow = cursor;
    m_viaDelete->Enable( cursor > 0 );

    // The drawing: the stack rows and every via type's span across them
    if( VIA_DRAWING* drawing = dynamic_cast<VIA_DRAWING*>( m_viaDrawing ) )
    {
        std::vector<VIA_DRAWING::ROW> rows;
        std::map<PCB_LAYER_ID, int>   rowOfLayer;

        for( int row = 0; row < (int) m_rows.size(); ++row )
        {
            BOARD_STACKUP_ITEM* item = m_state.m_stackup.GetList()[m_rows[row].m_item];
            int                 copper = 0;

            if( item->GetType() == BS_ITEM_TYPE_COPPER )
            {
                copper = copperNumber( item->GetBrdLayerId() );
                rowOfLayer[item->GetBrdLayerId()] = row;
            }

            rows.push_back( { item->GetType(), copper } );
        }

        std::vector<VIA_DRAWING::VIA> vias;

        auto span =
                [&]( PCB_LAYER_ID aFirst, PCB_LAYER_ID aLast, const wxString& aLabel )
                {
                    int first = rowOfLayer.count( aFirst ) ? rowOfLayer[aFirst] : -1;
                    int last = rowOfLayer.count( aLast ) ? rowOfLayer[aLast] : -1;

                    if( first > last )
                        std::swap( first, last );

                    vias.push_back( { aLabel, first, last } );
                };

        if( !stack.empty() )
            span( stack.front(), stack.back(), m_viaGrid->GetCellValue( 0, VIA_NAME ) );

        for( int ii = 0; ii < (int) m_state.m_viaTypes.size(); ++ii )
        {
            const LAYER_PAIR& pair = m_state.m_viaTypes[ii].GetLayerPair();
            span( pair.GetLayerA(), pair.GetLayerB(), m_viaGrid->GetCellValue( ii + 1, VIA_NAME ) );
        }

        drawing->Set( std::move( rows ), std::move( vias ), cursor,
                      m_viaStack ? m_viaStack->GetColLabelSize() : FromDIP( 26 ),
                      m_viaStack ? m_viaStack->GetDefaultRowSize() : FromDIP( 24 ) );
    }

    const BOARD_DESIGN_SETTINGS& bds = m_board->GetDesignSettings();

    m_viaInfo->SetLabel( wxString::Format(
            _( "Via types are the board's layer pairs: routing uses them for vias and for "
               "switching layers.  Blind, buried and micro-vias are placed when a via joins "
               "layers of such a span.\n"
               "Board Setup minimums: via %s / hole %s;  micro-via %s / hole %s." ),
            m_frame->StringFromValue( bds.m_ViasMinSize, true ),
            m_frame->StringFromValue( bds.m_MinThroughDrill, true ),
            m_frame->StringFromValue( bds.m_MicroViasMinSize, true ),
            m_frame->StringFromValue( bds.m_MicroViasMinDrill, true ) ) );
}


void LAYER_STACK_MANAGER_FRAME::addViaType()
{
    std::vector<PCB_LAYER_ID> stack = copperStack( m_state.m_stackup );

    if( stack.size() < 3 )
    {
        setStatus( _( "A two-layer board has only through vias." ), true );
        return;
    }

    auto exists = [&]( PCB_LAYER_ID aA, PCB_LAYER_ID aB )
                  {
                      return std::any_of( m_state.m_viaTypes.begin(), m_state.m_viaTypes.end(),
                                          [&]( const LAYER_PAIR_INFO& aInfo )
                                          {
                                              return aInfo.GetLayerPair().HasSameLayers(
                                                      LAYER_PAIR( aA, aB ) );
                                          } );
                  };

    // Micro-vias from the outer layers first, then deeper spans
    std::vector<std::pair<int, int>> candidates;
    int                              last = (int) stack.size() - 1;

    for( int span = 1; span < last; ++span )
    {
        candidates.emplace_back( 0, span );
        candidates.emplace_back( last - span, last );

        for( int a = 1; a + span < last; ++a )
            candidates.emplace_back( a, a + span );
    }

    for( const auto& [a, b] : candidates )
    {
        if( exists( stack[a], stack[b] ) )
            continue;

        STATE before = m_state;
        m_state.m_viaTypes.emplace_back( LAYER_PAIR( stack[a], stack[b] ), true, std::nullopt );
        commitChange( before, wxString::Format( _( "Added a %s via type from %s to %s." ),
                                                viaTypeKind( stack[a], stack[b] ).Lower(),
                                                layerDisplayName( stack[a] ),
                                                layerDisplayName( stack[b] ) ) );

        m_viaGrid->SetGridCursor( m_viaGrid->GetNumberRows() - 1, VIA_NAME );
        m_viaGrid->MakeCellVisible( m_viaGrid->GetNumberRows() - 1, VIA_NAME );
        m_viaDelete->Enable( true );
        return;
    }

    setStatus( _( "Every layer span already has a via type." ), true );
}


void LAYER_STACK_MANAGER_FRAME::deleteViaType()
{
    int row = m_viaGrid->GetGridCursorRow();

    if( row < 1 || row > (int) m_state.m_viaTypes.size() )
        return;

    STATE before = m_state;
    m_state.m_viaTypes.erase( m_state.m_viaTypes.begin() + row - 1 );
    commitChange( before, _( "Deleted the via type." ) );
}


void LAYER_STACK_MANAGER_FRAME::onViaCellChanged( wxGridEvent& aEvent )
{
    int row = aEvent.GetRow();
    int col = aEvent.GetCol();

    if( row < 1 || row > (int) m_state.m_viaTypes.size() )
        return;

    STATE           before = m_state;
    LAYER_PAIR_INFO info = m_state.m_viaTypes[row - 1];
    PCB_LAYER_ID    first = info.GetLayerPair().GetLayerA();
    PCB_LAYER_ID    last = info.GetLayerPair().GetLayerB();
    bool            enabled = info.IsEnabled();
    wxString        value = m_viaGrid->GetCellValue( row, col ).Strip( wxString::both );
    wxString        error;

    auto layerFromName =
            [&]( const wxString& aName )
            {
                for( PCB_LAYER_ID layer : copperStack( m_state.m_stackup ) )
                {
                    if( layerDisplayName( layer ) == aName )
                        return layer;
                }

                return UNDEFINED_LAYER;
            };

    switch( col )
    {
    case VIA_NAME:
    {
        // An empty name goes back to the automatic one
        std::vector<PCB_LAYER_ID> stack = copperStack( m_state.m_stackup );
        int a = stackIndex( stack, first ) + 1;
        int b = stackIndex( stack, last ) + 1;
        wxString automatic = wxString::Format( wxS( "%s %d:%d" ), viaTypeKind( first, last ),
                                               std::min( a, b ), std::max( a, b ) );

        if( value.IsEmpty() || value == automatic )
            info.UnsetName();
        else
            info.SetName( value );

        m_state.m_viaTypes[row - 1] = info;
        break;
    }

    case VIA_FIRST:
    case VIA_LAST:
    {
        PCB_LAYER_ID layer = layerFromName( value );
        std::vector<PCB_LAYER_ID> stack = copperStack( m_state.m_stackup );

        // Keep the pair in stack order: the edited end replaces the upper or lower layer
        if( stackIndex( stack, first ) > stackIndex( stack, last ) )
            std::swap( first, last );

        if( col == VIA_FIRST )
            first = layer;
        else
            last = layer;

        if( layer == UNDEFINED_LAYER || first == last )
        {
            error = _( "A via type needs two different layers." );
            break;
        }

        if( LAYER_PAIR( first, last ).HasSameLayers( LAYER_PAIR( stack.front(), stack.back() ) ) )
        {
            error = _( "Through vias are always available." );
            break;
        }

        for( int ii = 0; ii < (int) m_state.m_viaTypes.size(); ++ii )
        {
            if( ii != row - 1
                    && m_state.m_viaTypes[ii].GetLayerPair().HasSameLayers( LAYER_PAIR( first, last ) ) )
            {
                error = _( "There is already a via type with these layers." );
            }
        }

        if( !error.IsEmpty() )
            break;

        m_state.m_viaTypes[row - 1] = LAYER_PAIR_INFO( LAYER_PAIR( first, last ), enabled,
                                                       info.GetName() );
        break;
    }

    case VIA_ENABLED:
        info.SetEnabled( value == wxS( "1" ) );
        m_state.m_viaTypes[row - 1] = info;
        break;

    default:
        return;
    }

    if( !error.IsEmpty() )
    {
        m_state = before;
        wxBell();
        CallAfter( [this, error]()
                   {
                       refreshGrid();
                       setStatus( error, true );
                   } );
        return;
    }

    CallAfter( [this, before]() { commitChange( before ); } );
}
