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

#include <dialogs/dialog_ipc_footprint_wizard.h>

#include <base_units.h>
#include <footprint.h>
#include <footprint_edit_frame.h>
#include <footprint_library_adapter.h>
#include <pad.h>
#include <pcb_shape.h>
#include <pcb_text.h>
#include <project_pcb.h>
#include <pgm_base.h>
#include <libraries/library_manager.h>
#include <libraries/library_table.h>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dcbuffer.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/listctrl.h>
#include <wx/radiobut.h>
#include <wx/settings.h>
#include <wx/simplebook.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <algorithm>
#include <cmath>


using namespace IPC;

namespace
{
enum PAGE
{
    PG_INTRO = 0,
    PG_TYPE,
    PG_DIMENSIONS,
    PG_HEEL,
    PG_FILLETS,
    PG_COMPONENT_TOL,
    PG_IPC_TOL,
    PG_FOOTPRINT,
    PG_SILK,
    PG_MECHANICAL,
    PG_DESCRIPTION,
    PG_DESTINATION,
    PG_COUNT
};


wxString familyTitle( FAMILY aFamily )
{
    switch( aFamily )
    {
    case FAMILY::CHIP:   return _( "Chip Component" );
    case FAMILY::MOLDED: return _( "Molded Component" );
    case FAMILY::MELF:   return _( "MELF Component" );
    case FAMILY::SOIC:   return _( "SOIC Component" );
    case FAMILY::SOT23:  return _( "SOT23 Component" );
    case FAMILY::QFP:    return _( "QFP Component" );
    case FAMILY::DFN:    return _( "DFN Component" );
    case FAMILY::QFN:    return _( "QFN Component" );
    }

    return wxEmptyString;
}


/// Footprint libraries that can be written to: a folder on disk the user may write to (the
/// libraries are loaded in the background, so the library adapter may not know yet).
std::vector<wxString> writableLibraries( PROJECT& aProject )
{
    std::vector<wxString> names;

    for( LIBRARY_TABLE_ROW* row : Pgm().GetLibraryManager().Rows( LIBRARY_TABLE_TYPE::FOOTPRINT ) )
    {
        if( !row || row->Disabled() || row->Hidden() )
            continue;

        wxString path = LIBRARY_MANAGER::ExpandURI( row->URI(), aProject );

        if( wxFileName::DirExists( path ) && wxFileName::IsDirWritable( path ) )
            names.push_back( row->Nickname() );
    }

    std::sort( names.begin(), names.end(),
               []( const wxString& a, const wxString& b ) { return a.CmpNoCase( b ) < 0; } );
    names.erase( std::unique( names.begin(), names.end() ), names.end() );
    return names;
}


wxColour mix( const wxColour& a, const wxColour& b, double t )
{
    return wxColour( a.Red() + ( b.Red() - a.Red() ) * t, a.Green() + ( b.Green() - a.Green() ) * t,
                     a.Blue() + ( b.Blue() - a.Blue() ) * t );
}
} // namespace


/**
 * The land pattern as it will be built: pads, silkscreen, courtyard and assembly outline.
 */
class IPC_PREVIEW : public wxWindow
{
public:
    IPC_PREVIEW( wxWindow* aParent ) :
            wxWindow( aParent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_SIMPLE )
    {
        SetBackgroundStyle( wxBG_STYLE_PAINT );
        SetMinSize( FromDIP( wxSize( 300, 380 ) ) );
        Bind( wxEVT_PAINT, [this]( wxPaintEvent& ) { paint(); } );
        Bind( wxEVT_SIZE, [this]( wxSizeEvent& aEvent ) { Refresh(); aEvent.Skip(); } );
    }

    void Set( const RESULT& aResult, bool aCourtyard, bool aAssembly )
    {
        m_result = aResult;
        m_courtyard = aCourtyard;
        m_assembly = aAssembly;
        Refresh();
    }

private:
    void paint()
    {
        wxAutoBufferedPaintDC dc( this );
        wxSize                size = GetClientSize();

        dc.SetBackground( wxBrush( wxColour( 0x1C, 0x1C, 0x1C ) ) );
        dc.Clear();

        if( !m_result.ok )
        {
            dc.SetTextForeground( wxColour( 0xE0, 0x60, 0x60 ) );
            dc.DrawLabel( _( "No valid footprint" ), wxRect( size ), wxALIGN_CENTER );
            return;
        }

        double w = std::max( m_result.courtV1, 0.5 ), h = std::max( m_result.courtV2, 0.5 );
        double scale = std::min( ( size.x - 30 ) / w, ( size.y - 30 ) / h );
        wxPoint centre( size.x / 2, size.y / 2 );

        auto pt = [&]( double x, double y )
                  {
                      return wxPoint( centre.x + KiROUND( x * scale ), centre.y + KiROUND( y * scale ) );
                  };

        auto rect = [&]( double cx, double cy, double sx, double sy )
                    {
                        wxPoint a = pt( cx - sx / 2, cy - sy / 2 );
                        wxPoint b = pt( cx + sx / 2, cy + sy / 2 );
                        return wxRect( a, b );
                    };

        // Courtyard
        if( m_courtyard )
        {
            dc.SetPen( wxPen( wxColour( 0xFF, 0x26, 0xE2 ), 1 ) );
            dc.SetBrush( *wxTRANSPARENT_BRUSH );
            dc.DrawRectangle( rect( 0, 0, m_result.courtV1, m_result.courtV2 ) );
        }

        // Assembly outline
        if( m_assembly )
        {
            dc.SetPen( wxPen( wxColour( 0xAF, 0xAF, 0xAF ), 1 ) );
            dc.SetBrush( *wxTRANSPARENT_BRUSH );
            dc.DrawRectangle( rect( 0, 0, m_result.assyA, m_result.assyB ) );
        }

        // Pads
        dc.SetPen( *wxTRANSPARENT_PEN );
        dc.SetBrush( wxBrush( wxColour( 0xC8, 0x34, 0x34 ) ) );

        for( const IPC::PAD& pad : m_result.pads )
            dc.DrawRectangle( rect( pad.x, pad.y, pad.sx, pad.sy ) );

        // Pad numbers
        dc.SetTextForeground( *wxWHITE );
        wxFont font = GetFont();
        font.SetPointSize( std::max( 6, std::min( 11, KiROUND( m_result.padX * scale / 3 ) ) ) );
        dc.SetFont( font );

        for( const IPC::PAD& pad : m_result.pads )
            dc.DrawLabel( pad.number, rect( pad.x, pad.y, pad.sx, pad.sy ), wxALIGN_CENTER );

        // Silkscreen
        dc.SetPen( wxPen( wxColour( 0xF2, 0xED, 0xA1 ), 2 ) );

        for( const SEGMENT& seg : m_result.silk )
            dc.DrawLine( pt( seg.x1, seg.y1 ), pt( seg.x2, seg.y2 ) );

        if( m_result.hasPin1Dot )
        {
            dc.SetBrush( wxBrush( wxColour( 0xF2, 0xED, 0xA1 ) ) );
            dc.DrawCircle( pt( m_result.pin1X, m_result.pin1Y ), 3 );
        }
    }

    RESULT m_result;
    bool   m_courtyard = true;
    bool   m_assembly = true;
};


/**
 * Dimension drawings for the wizard pages (top and side views with the labelled values).
 */
class IPC_DIAGRAM : public wxWindow
{
public:
    enum KIND { PACKAGE, HEEL, FILLETS, PADS, SILK, MECHANICAL, PICTURE };

    IPC_DIAGRAM( wxWindow* aParent, KIND aKind ) :
            wxWindow( aParent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_SIMPLE ),
            m_kind( aKind )
    {
        SetBackgroundStyle( wxBG_STYLE_PAINT );
        SetMinSize( FromDIP( aKind == FILLETS ? wxSize( 440, 150 )
                                              : aKind == PICTURE ? wxSize( 300, 260 )
                                                                 : wxSize( 300, 380 ) ) );
        Bind( wxEVT_PAINT, [this]( wxPaintEvent& ) { paint(); } );
        Bind( wxEVT_SIZE, [this]( wxSizeEvent& aEvent ) { Refresh(); aEvent.Skip(); } );
    }

    void SetFamily( FAMILY aFamily )
    {
        m_family = aFamily;
        Refresh();
    }

private:
    // Drawing in a 100 x 100 unit box, scaled to the window
    struct PEN_CTX
    {
        wxDC*  dc;
        double scale;
        int    ox, oy;
    };

    wxPoint p( const PEN_CTX& c, double x, double y ) const
    {
        return wxPoint( c.ox + KiROUND( x * c.scale ), c.oy + KiROUND( y * c.scale ) );
    }

    void box( const PEN_CTX& c, double x, double y, double w, double h, const wxColour* fill )
    {
        c.dc->SetBrush( fill ? wxBrush( *fill ) : *wxTRANSPARENT_BRUSH );
        c.dc->DrawRectangle( wxRect( p( c, x, y ), p( c, x + w, y + h ) ) );
    }

    void arrowH( const PEN_CTX& c, double x1, double x2, double y, const wxString& aLabel )
    {
        wxPoint a = p( c, x1, y ), b = p( c, x2, y );
        c.dc->DrawLine( a, b );
        int s = std::max( 3, KiROUND( 1.5 * c.scale ) );
        c.dc->DrawLine( a, a + wxPoint( s, -s ) );
        c.dc->DrawLine( a, a + wxPoint( s, s ) );
        c.dc->DrawLine( b, b + wxPoint( -s, -s ) );
        c.dc->DrawLine( b, b + wxPoint( -s, s ) );
        wxSize ext = c.dc->GetTextExtent( aLabel );
        c.dc->DrawText( aLabel, ( a.x + b.x - ext.x ) / 2, a.y - ext.y - 1 );
    }

    void arrowV( const PEN_CTX& c, double x, double y1, double y2, const wxString& aLabel )
    {
        wxPoint a = p( c, x, y1 ), b = p( c, x, y2 );
        c.dc->DrawLine( a, b );
        int s = std::max( 3, KiROUND( 1.5 * c.scale ) );
        c.dc->DrawLine( a, a + wxPoint( -s, s ) );
        c.dc->DrawLine( a, a + wxPoint( s, s ) );
        c.dc->DrawLine( b, b + wxPoint( -s, -s ) );
        c.dc->DrawLine( b, b + wxPoint( s, -s ) );
        wxSize ext = c.dc->GetTextExtent( aLabel );
        c.dc->DrawText( aLabel, a.x + 4, ( a.y + b.y - ext.y ) / 2 );
    }

    void title( const PEN_CTX& c, double y, const wxString& aText )
    {
        wxSize ext = c.dc->GetTextExtent( aText );
        wxPoint at = p( c, 50, y );
        c.dc->DrawText( aText, at.x - ext.x / 2, at.y );
    }

    void paint()
    {
        wxAutoBufferedPaintDC dc( this );
        wxColour bg = mix( wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOW ), *wxBLACK, 0.05 );
        wxColour fg = wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOWTEXT );
        wxColour lead = mix( bg, fg, 0.55 );
        wxColour body = mix( bg, fg, 0.25 );
        wxColour pad = wxColour( 0xC8, 0x34, 0x34 );

        dc.SetBackground( wxBrush( bg ) );
        dc.Clear();
        dc.SetPen( wxPen( fg, 1 ) );
        dc.SetTextForeground( fg );
        dc.SetFont( GetFont() );

        wxSize  size = GetClientSize();
        double  unitsW = m_kind == FILLETS ? 150.0 : 100.0;
        double  scale = std::min( size.x / unitsW, size.y / 100.0 );
        PEN_CTX c{ &dc, scale, KiROUND( ( size.x - unitsW * scale ) / 2 ),
                   KiROUND( ( size.y - 100 * scale ) / 2 ) };

        bool two = IsTwoTerminal( m_family );
        bool quad = IsQuad( m_family );
        bool noLead = IsNoLead( m_family );

        switch( m_kind )
        {
        case PICTURE:
        case PACKAGE:
        {
            title( c, two ? 4 : -2, _( "Top View" ) );

            if( two )
            {
                box( c, 25, 22, 50, 22, &body );
                box( c, 25, 22, 8, 22, &lead );
                box( c, 67, 22, 8, 22, &lead );

                if( m_kind == PACKAGE )
                {
                    arrowH( c, 25, 75, 17, wxS( "L" ) );
                    arrowV( c, 80, 22, 44, wxS( "W" ) );
                    arrowH( c, 25, 33, 52, wxS( "T" ) );
                }

                title( c, 60, _( "Side View" ) );
                box( c, 25, 72, 50, 14, &body );
                box( c, 25, 72, 8, 14, &lead );
                box( c, 67, 72, 8, 14, &lead );

                if( m_kind == PACKAGE )
                    arrowV( c, 80, 72, 86, wxS( "A" ) );
            }
            else if( quad )
            {
                box( c, 30, 22, 40, 40, &body );

                for( int k = 0; k < 4; ++k )
                {
                    double o = 26 + k * 10;
                    box( c, noLead ? 30 : 22, o, 8, 4, &lead );            // left
                    box( c, noLead ? 62 : 70, o, 8, 4, &lead );            // right
                    box( c, o, noLead ? 22 : 14, 4, 8, &lead );            // top
                    box( c, o, noLead ? 54 : 62, 4, 8, &lead );            // bottom
                }

                if( noLead )
                {
                    wxPen dash( fg, 1, wxPENSTYLE_SHORT_DASH );
                    dc.SetPen( dash );
                    box( c, 40, 32, 20, 20, nullptr );
                    dc.SetPen( wxPen( fg, 1 ) );
                }

                if( m_kind == PACKAGE )
                {
                    arrowH( c, noLead ? 30 : 22, noLead ? 70 : 78, 76, noLead ? wxS( "D" ) : wxS( "L" ) );
                    arrowV( c, 84, 26, 36, wxS( "e" ) );

                    if( !noLead )
                        arrowH( c, 30, 70, 84, wxS( "D" ) );
                    else
                        arrowH( c, 30, 38, 84, wxS( "T" ) );
                }
            }
            else
            {
                double inset = noLead ? 0 : 8;
                box( c, 30, 18, 40, 44, &body );

                for( int k = 0; k < 4; ++k )
                {
                    double o = 22 + k * 10;
                    box( c, 30 - inset, o, 8, 4, &lead );
                    box( c, 62 + inset, o, 8, 4, &lead );
                }

                if( noLead )
                {
                    wxPen dash( fg, 1, wxPENSTYLE_SHORT_DASH );
                    dc.SetPen( dash );
                    box( c, 42, 28, 16, 24, nullptr );
                    dc.SetPen( wxPen( fg, 1 ) );
                }

                if( m_kind == PACKAGE )
                {
                    arrowH( c, 30 - inset, 70 + inset, 12, noLead ? wxS( "E (L)" ) : wxS( "L" ) );
                    arrowV( c, 82 + inset, 18, 62, wxS( "D" ) );
                    arrowV( c, 14, 22, 32, wxS( "e" ) );
                    arrowH( c, 30 - inset, 38 - inset, 70, wxS( "T" ) );

                    if( !noLead )
                        arrowH( c, 30, 70, 78, wxS( "E" ) );
                }
            }

            if( m_kind == PACKAGE && !two )
            {
                title( c, 86, _( "Side View" ) );
                box( c, 34, 92, 32, 6, &body );
                arrowV( c, 72, 92, 98, wxS( "A" ) );
            }

            break;
        }

        case HEEL:
        {
            title( c, 20, _( "Side View" ) );

            if( noLead || two )
            {
                box( c, 25, 40, 50, 16, &body );
                box( c, 25, 40, 10, 16, &lead );
                box( c, 65, 40, 10, 16, &lead );
                arrowH( c, 35, 65, 66, wxS( "S" ) );
            }
            else
            {
                box( c, 35, 34, 30, 16, &body );
                dc.SetPen( wxPen( lead, std::max( 2, KiROUND( 2 * scale ) ) ) );
                dc.DrawLine( p( c, 35, 42 ), p( c, 28, 42 ) );
                dc.DrawLine( p( c, 28, 42 ), p( c, 26, 56 ) );
                dc.DrawLine( p( c, 26, 56 ), p( c, 16, 56 ) );
                dc.DrawLine( p( c, 65, 42 ), p( c, 72, 42 ) );
                dc.DrawLine( p( c, 72, 42 ), p( c, 74, 56 ) );
                dc.DrawLine( p( c, 74, 56 ), p( c, 84, 56 ) );
                dc.SetPen( wxPen( fg, 1 ) );
                arrowH( c, 26, 74, 66, wxS( "S" ) );
            }

            break;
        }

        case FILLETS:
        {
            const wxString names[3] = { _( "Toe Fillet" ), _( "Heel Fillet" ), _( "Side Fillet" ) };
            const wxString labels[3] = { wxS( "JT min" ), wxS( "JH min" ), wxS( "JS min" ) };

            for( int k = 0; k < 3; ++k )
            {
                double x = 5 + k * 50;
                box( c, x, 2, 40, 96, nullptr );
                wxSize ext = dc.GetTextExtent( names[k] );
                wxPoint at = p( c, x + 20, 6 );
                dc.DrawText( names[k], at.x - ext.x / 2, at.y );

                // Pad on the board, lead above it
                box( c, x + 6, 80, 28, 4, &pad );
                box( c, x + 14, 48, 12, 32, &lead );
                arrowH( c, k == 1 ? x + 26 : x + 6, k == 1 ? x + 34 : x + 14, 40, labels[k] );
            }

            break;
        }

        case PADS:
        {
            title( c, 6, _( "Top View" ) );

            if( two )
            {
                box( c, 20, 34, 22, 26, &pad );
                box( c, 58, 34, 22, 26, &pad );
                arrowH( c, 31, 69, 26, wxS( "C" ) );
                arrowV( c, 84, 34, 60, wxS( "X" ) );
                arrowH( c, 20, 42, 70, wxS( "Y" ) );
            }
            else
            {
                for( int k = 0; k < 4; ++k )
                {
                    box( c, 18, 24 + k * 12, 18, 6, &pad );
                    box( c, 64, 24 + k * 12, 18, 6, &pad );
                }

                arrowH( c, 27, 73, 18, wxS( "C" ) );
                arrowV( c, 88, 24, 30, wxS( "X" ) );
                arrowV( c, 10, 24, 36, wxS( "e" ) );
                arrowH( c, 18, 36, 76, wxS( "Y" ) );
            }

            break;
        }

        case SILK:
        {
            title( c, 6, _( "Top View" ) );
            box( c, 20, 34, 16, 26, &pad );
            box( c, 64, 34, 16, 26, &pad );
            dc.SetPen( wxPen( wxColour( 0xC0, 0xB0, 0x30 ), 2 ) );
            dc.DrawLine( p( c, 42, 30 ), p( c, 58, 30 ) );
            dc.DrawLine( p( c, 42, 64 ), p( c, 58, 64 ) );
            dc.SetPen( wxPen( fg, 1 ) );
            arrowH( c, 36, 42, 22, wxS( "R1" ) );
            arrowV( c, 50, 30, 34, wxS( "R2" ) );
            break;
        }

        case MECHANICAL:
        {
            title( c, 6, _( "Top View" ) );
            wxPen court( wxColour( 0xD0, 0x30, 0xC0 ), 1 );
            dc.SetPen( court );
            box( c, 18, 22, 64, 32, nullptr );
            dc.SetPen( wxPen( fg, 1 ) );
            box( c, 24, 28, 14, 20, &pad );
            box( c, 62, 28, 14, 20, &pad );
            arrowH( c, 18, 82, 16, wxS( "V1" ) );
            arrowV( c, 86, 22, 54, wxS( "V2" ) );

            box( c, 30, 68, 40, 18, nullptr );
            arrowH( c, 30, 70, 64, wxS( "A" ) );
            arrowV( c, 74, 68, 86, wxS( "B" ) );
            break;
        }
        }
    }

    KIND   m_kind;
    FAMILY m_family = FAMILY::CHIP;
};


DIALOG_IPC_FOOTPRINT_WIZARD::DIALOG_IPC_FOOTPRINT_WIZARD( FOOTPRINT_EDIT_FRAME* aFrame,
                                                          const wxString& aCurrentLibrary ) :
        DIALOG_SHIM( aFrame, wxID_ANY, _( "IPC® Compliant Footprint Wizard" ), wxDefaultPosition,
                     wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER ),
        m_frame( aFrame ),
        m_currentLibrary( aCurrentLibrary )
{
    m_input = Defaults( FAMILY::CHIP );

    wxBoxSizer* main = new wxBoxSizer( wxVERTICAL );

    m_book = new wxSimplebook( this, wxID_ANY );
    main->Add( m_book, 1, wxEXPAND | wxALL, 8 );

    buildIntro();
    buildTypePage();
    buildDimensionsPage();
    buildHeelPage();
    buildFilletsPage();
    buildComponentTolerancesPage();
    buildIpcTolerancesPage();
    buildFootprintPage();
    buildSilkPage();
    buildMechanicalPage();
    buildDescriptionPage();
    buildDestinationPage();

    for( wxPanel* page : m_pages )
        m_book->AddPage( page, wxEmptyString );

    // Footer: error text, then Cancel / Back / Next / Finish as in Altium
    wxBoxSizer* footer = new wxBoxSizer( wxHORIZONTAL );
    m_error = new wxStaticText( this, wxID_ANY, wxEmptyString );
    m_error->SetForegroundColour( wxColour( 0xE0, 0x60, 0x60 ) );
    footer->Add( m_error, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, 4 );

    m_cancel = new wxButton( this, wxID_CANCEL, _( "Cancel" ) );
    m_back = new wxButton( this, wxID_ANY, _( "&Back" ) );
    m_next = new wxButton( this, wxID_ANY, _( "&Next" ) );
    m_finish = new wxButton( this, wxID_OK, _( "Finish" ) );

    for( wxButton* button : { m_cancel, m_back, m_next, m_finish } )
    {
        button->SetMinSize( FromDIP( wxSize( 100, -1 ) ) );
        footer->Add( button, 0, wxLEFT, 6 );
    }

    main->Add( footer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8 );
    SetSizer( main );

    m_back->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { showPage( m_book->GetSelection() - 1 ); } );
    m_next->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { showPage( m_book->GetSelection() + 1 ); } );
    m_finish->Bind( wxEVT_BUTTON,
                    [this]( wxCommandEvent& aEvent )
                    {
                        readInput();
                        recalculate();

                        if( !m_result.ok )
                        {
                            wxBell();
                            return;
                        }

                        if( m_destExisting->GetValue() && m_libraries->GetSelection() < 0 )
                        {
                            showPage( PG_DESTINATION );
                            m_error->SetLabel( _( "Choose the library to store the footprint in." ) );
                            return;
                        }

                        aEvent.Skip();
                    } );

    // Any edit recalculates (so the preview and the calculated fields follow)
    Bind( wxEVT_TEXT,
          [this]( wxCommandEvent& aEvent )
          {
              aEvent.Skip();

              if( !m_filling )
              {
                  readInput();
                  recalculate();
              }
          } );

    auto onToggle =
            [this]( wxCommandEvent& aEvent )
            {
                aEvent.Skip();

                if( !m_filling )
                {
                    updateEnables();
                    readInput();
                    recalculate();
                }
            };

    Bind( wxEVT_CHECKBOX, onToggle );
    Bind( wxEVT_CHOICE, onToggle );
    Bind( wxEVT_RADIOBUTTON, onToggle );

    applyFamily( FAMILY::CHIP );
    showPage( PG_INTRO );

    SetInitialFocus( m_next );
    finishDialogSettings();
    SetMinSize( FromDIP( wxSize( 1060, 700 ) ) );
    SetSize( FromDIP( wxSize( 1060, 700 ) ) );
    Centre();
}


wxPanel* DIALOG_IPC_FOOTPRINT_WIZARD::makePage( const wxString& aTitle, const wxString& aSubtitle,
                                                bool aPreview )
{
    wxPanel*    page = new wxPanel( m_book );
    wxBoxSizer* sizer = new wxBoxSizer( wxVERTICAL );

    // Header band with the page title and its explanation
    wxPanel*    header = new wxPanel( page );
    wxBoxSizer* headerSizer = new wxBoxSizer( wxVERTICAL );
    header->SetBackgroundColour( mix( wxSystemSettings::GetColour( wxSYS_COLOUR_BTNFACE ), *wxBLACK, 0.2 ) );

    wxStaticText* title = new wxStaticText( header, wxID_ANY, aTitle );
    wxFont        font = title->GetFont();
    font.SetWeight( wxFONTWEIGHT_BOLD );
    font.SetPointSize( font.GetPointSize() + 2 );
    title->SetFont( font );
    headerSizer->Add( title, 0, wxLEFT | wxRIGHT | wxTOP, 12 );

    wxStaticText* subtitle = new wxStaticText( header, wxID_ANY, aSubtitle );
    headerSizer->Add( subtitle, 0, wxALL, 12 );
    header->SetSizer( headerSizer );
    sizer->Add( header, 0, wxEXPAND );

    wxBoxSizer* body = new wxBoxSizer( wxHORIZONTAL );
    wxPanel*    content = new wxPanel( page );
    content->SetName( wxS( "content" ) );
    body->Add( content, 1, wxEXPAND | wxALL, 8 );

    if( aPreview )
    {
        wxBoxSizer* right = new wxBoxSizer( wxVERTICAL );
        right->Add( new wxStaticText( page, wxID_ANY, _( "Preview" ) ), 0, wxALIGN_CENTER | wxBOTTOM, 4 );
        IPC_PREVIEW* preview = new IPC_PREVIEW( page );
        right->Add( preview, 1, wxEXPAND );
        body->Add( right, 0, wxEXPAND | wxALL, 8 );
        m_previews.push_back( preview );
    }

    sizer->Add( body, 1, wxEXPAND );
    page->SetSizer( sizer );

    m_titles.push_back( title );
    m_pages.push_back( page );
    return page;
}


static wxPanel* contentOf( wxPanel* aPage )
{
    return static_cast<wxPanel*>( aPage->FindWindow( wxS( "content" ) ) );
}


wxTextCtrl* DIALOG_IPC_FOOTPRINT_WIZARD::numberCtrl( wxWindow* aParent )
{
    wxTextCtrl* ctrl = new wxTextCtrl( aParent, wxID_ANY );
    ctrl->SetMinSize( FromDIP( wxSize( 100, -1 ) ) );
    return ctrl;
}


DIALOG_IPC_FOOTPRINT_WIZARD::RANGE_ROW
DIALOG_IPC_FOOTPRINT_WIZARD::addRange( wxWindow* aParent, wxFlexGridSizer* aGrid,
                                       const wxString& aLabel )
{
    RANGE_ROW row;
    row.label = new wxStaticText( aParent, wxID_ANY, aLabel );
    row.min = numberCtrl( aParent );
    row.max = numberCtrl( aParent );
    aGrid->Add( row.label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12 );
    aGrid->Add( row.min, 0, wxALIGN_CENTER_VERTICAL );
    aGrid->Add( row.max, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 6 );
    return row;
}


DIALOG_IPC_FOOTPRINT_WIZARD::VALUE_ROW
DIALOG_IPC_FOOTPRINT_WIZARD::addValue( wxWindow* aParent, wxFlexGridSizer* aGrid,
                                       const wxString& aLabel )
{
    VALUE_ROW row;
    row.label = new wxStaticText( aParent, wxID_ANY, aLabel );
    row.value = numberCtrl( aParent );
    aGrid->Add( row.label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12 );
    aGrid->Add( row.value, 0, wxALIGN_CENTER_VERTICAL );
    return row;
}


void DIALOG_IPC_FOOTPRINT_WIZARD::buildIntro()
{
    wxPanel*    page = new wxPanel( m_book );
    wxBoxSizer* sizer = new wxBoxSizer( wxVERTICAL );

    wxStaticText* title = new wxStaticText( page, wxID_ANY, _( "IPC® Compliant Footprint Wizard" ) );
    wxFont        font = title->GetFont();
    font.SetWeight( wxFONTWEIGHT_BOLD );
    font.SetPointSize( font.GetPointSize() * 2 );
    title->SetFont( font );

    sizer->AddSpacer( FromDIP( 120 ) );
    sizer->Add( title, 0, wxLEFT, FromDIP( 40 ) );
    sizer->AddSpacer( FromDIP( 40 ) );
    sizer->Add( new wxStaticText( page, wxID_ANY,
                                  _( "This wizard will help you to draw footprints that follow the "
                                     "IPC® Compliant Footprint standard (IPC-7351B)." ) ),
                0, wxLEFT, FromDIP( 40 ) );
    page->SetSizer( sizer );

    m_titles.push_back( title );
    m_pages.push_back( page );
}


void DIALOG_IPC_FOOTPRINT_WIZARD::buildTypePage()
{
    wxPanel* page = makePage( _( "Select Component Type" ), wxEmptyString, false );
    wxPanel* content = contentOf( page );

    wxBoxSizer* sizer = new wxBoxSizer( wxHORIZONTAL );
    wxBoxSizer* left = new wxBoxSizer( wxVERTICAL );
    left->Add( new wxStaticText( content, wxID_ANY,
                                 _( "Select the family of components you wish to create." ) ),
               0, wxBOTTOM, 12 );
    left->Add( new wxStaticText( content, wxID_ANY, _( "Component Types" ) ), 0, wxBOTTOM, 4 );

    m_families = new wxListCtrl( content, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                 wxLC_REPORT | wxLC_SINGLE_SEL );
    m_families->AppendColumn( _( "Name" ), wxLIST_FORMAT_LEFT, FromDIP( 90 ) );
    m_families->AppendColumn( _( "Description" ), wxLIST_FORMAT_LEFT, FromDIP( 330 ) );
    m_families->AppendColumn( _( "Included Packages" ), wxLIST_FORMAT_LEFT, FromDIP( 230 ) );

    for( const FAMILY_INFO& info : Families() )
    {
        long row = m_families->InsertItem( m_families->GetItemCount(), info.name );
        m_families->SetItem( row, 1, info.description );
        m_families->SetItem( row, 2, info.packages );
    }

    left->Add( m_families, 1, wxEXPAND );
    left->Add( new wxStaticText( content, wxID_ANY,
                                 _( "NOTE: All wizard measurement dimensions are required to be "
                                    "entered as metric (mm) units." ) ),
               0, wxTOP, 12 );
    sizer->Add( left, 1, wxEXPAND | wxRIGHT, 12 );

    wxBoxSizer* right = new wxBoxSizer( wxVERTICAL );
    m_familyText = new wxStaticText( content, wxID_ANY, wxEmptyString );
    right->Add( m_familyText, 0, wxBOTTOM, 12 );
    IPC_DIAGRAM* picture = new IPC_DIAGRAM( content, IPC_DIAGRAM::PICTURE );
    right->Add( picture, 1, wxEXPAND );
    m_diagrams.push_back( picture );
    sizer->Add( right, 0, wxEXPAND );

    content->SetSizer( sizer );

    m_families->Bind( wxEVT_LIST_ITEM_SELECTED,
                      [this]( wxListEvent& aEvent )
                      {
                          const FAMILY_INFO& info = Families()[aEvent.GetIndex()];

                          if( info.family != m_input.family )
                              applyFamily( info.family );
                      } );
}


void DIALOG_IPC_FOOTPRINT_WIZARD::buildDimensionsPage()
{
    wxPanel* page = makePage( wxEmptyString, _( "Enter the required package values." ), true );
    wxPanel* content = contentOf( page );

    wxBoxSizer*      sizer = new wxBoxSizer( wxHORIZONTAL );
    wxBoxSizer*      left = new wxBoxSizer( wxVERTICAL );
    wxFlexGridSizer* grid = new wxFlexGridSizer( 3, 6, 6 );

    grid->Add( 0, 0 );
    grid->Add( new wxStaticText( content, wxID_ANY, _( "Minimum" ) ) );
    grid->Add( new wxStaticText( content, wxID_ANY, _( "Maximum" ) ), 0, wxLEFT, 6 );

    m_rowL = addRange( content, grid, wxEmptyString );
    m_rowW = addRange( content, grid, wxEmptyString );
    m_rowT = addRange( content, grid, wxEmptyString );
    m_rowD = addRange( content, grid, wxEmptyString );
    m_rowE = addRange( content, grid, wxEmptyString );

    auto single =
            [&]( const wxString& aLabel )
            {
                VALUE_ROW row = addValue( content, grid, aLabel );
                grid->Add( 0, 0 );
                return row;
            };

    m_rowHeight = single( _( "Maximum Height (A)" ) );
    m_rowPitch = single( _( "Pitch (e)" ) );
    m_rowPins = single( _( "Number of Pins (N)" ) );
    m_rowEpX = single( _( "Exposed Pad Width (E2)" ) );
    m_rowEpY = single( _( "Exposed Pad Length (D2)" ) );
    left->Add( grid, 0 );

    wxBoxSizer* typeRow = new wxBoxSizer( wxHORIZONTAL );
    m_typeLabel = new wxStaticText( content, wxID_ANY, _( "Package Type" ) );
    m_packageType = new wxChoice( content, wxID_ANY );
    typeRow->Add( m_typeLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12 );
    typeRow->Add( m_packageType, 1 );
    left->Add( typeRow, 0, wxEXPAND | wxTOP, 24 );

    m_polarityLabel = new wxStaticText( content, wxID_ANY, _( "Polarity Pin Location" ) );
    left->Add( m_polarityLabel, 0, wxTOP, 24 );
    const wxString polarity[3] = { _( "None" ), _( "Pin 1" ), _( "Pin 2" ) };

    for( int k = 0; k < 3; ++k )
    {
        m_polarity[k] = new wxRadioButton( content, wxID_ANY, polarity[k], wxDefaultPosition,
                                           wxDefaultSize, k == 0 ? wxRB_GROUP : 0 );
        left->Add( m_polarity[k], 0, wxLEFT | wxTOP, 8 );
    }

    sizer->Add( left, 1, wxEXPAND | wxRIGHT, 12 );

    IPC_DIAGRAM* diagram = new IPC_DIAGRAM( content, IPC_DIAGRAM::PACKAGE );
    sizer->Add( diagram, 0, wxEXPAND );
    m_diagrams.push_back( diagram );

    content->SetSizer( sizer );
}


void DIALOG_IPC_FOOTPRINT_WIZARD::buildHeelPage()
{
    wxPanel* page = makePage( wxEmptyString, _( "Enter the heel spacing values." ), true );
    wxPanel* content = contentOf( page );

    wxBoxSizer* sizer = new wxBoxSizer( wxHORIZONTAL );
    wxBoxSizer* left = new wxBoxSizer( wxVERTICAL );

    wxStaticText* text = new wxStaticText( content, wxID_ANY,
            _( "The minimum heel spacing is calculated by subtracting twice the maximum terminal "
               "length from the minimum overall length.\n\nThe maximum heel spacing is calculated "
               "by adding the tolerance on the inner distance between the heels of the opposing "
               "rows of leads to the minimum heel spacing." ) );
    text->Wrap( FromDIP( 420 ) );
    left->Add( text, 0, wxBOTTOM, 16 );

    m_heelCalc = new wxCheckBox( content, wxID_ANY, _( "Use calculated values" ) );
    left->Add( m_heelCalc, 0, wxBOTTOM, 12 );

    wxFlexGridSizer* grid = new wxFlexGridSizer( 2, 6, 6 );
    m_sMin = addValue( content, grid, _( "S Minimum" ) );
    m_sMax = addValue( content, grid, _( "S Maximum" ) );
    left->Add( grid, 0, wxLEFT, 24 );
    sizer->Add( left, 1, wxEXPAND | wxRIGHT, 12 );

    IPC_DIAGRAM* diagram = new IPC_DIAGRAM( content, IPC_DIAGRAM::HEEL );
    sizer->Add( diagram, 0, wxEXPAND );
    m_diagrams.push_back( diagram );
    content->SetSizer( sizer );
}


void DIALOG_IPC_FOOTPRINT_WIZARD::buildFilletsPage()
{
    wxPanel* page = makePage( wxEmptyString, _( "Enter the required fillet values." ), true );
    wxPanel* content = contentOf( page );

    wxBoxSizer* sizer = new wxBoxSizer( wxVERTICAL );

    wxStaticText* text = new wxStaticText( content, wxID_ANY,
            _( "Solder fillet refers to the shape of the solder joint between the component lead "
               "and the PCB pad.  A solder joint may be described by three fillets: toe, heel, and "
               "side.  Minimum values for solder fillets have been determined by IPC®, based on "
               "industry empirical knowledge and reliability testing; they may be adjusted to suit "
               "specific conditions." ) );
    text->Wrap( FromDIP( 600 ) );
    sizer->Add( text, 0, wxBOTTOM, 12 );

    m_filletDefaults = new wxCheckBox( content, wxID_ANY, _( "Use default values" ) );
    sizer->Add( m_filletDefaults, 0, wxBOTTOM, 8 );

    wxBoxSizer* density = new wxBoxSizer( wxHORIZONTAL );
    density->Add( new wxStaticText( content, wxID_ANY, _( "Board density Level" ) ), 0,
                  wxALIGN_CENTER_VERTICAL | wxRIGHT, 12 );
    m_density = new wxChoice( content, wxID_ANY );
    m_density->Append( _( "Level A - Low density" ) );
    m_density->Append( _( "Level B - Medium density" ) );
    m_density->Append( _( "Level C - High density" ) );
    density->Add( m_density, 0 );
    sizer->Add( density, 0, wxBOTTOM, 12 );

    wxFlexGridSizer* grid = new wxFlexGridSizer( 6, 6, 6 );
    m_toe = addValue( content, grid, _( "Toe Fillet (JT min)" ) );
    m_heel = addValue( content, grid, _( "Heel Fillet (JH min)" ) );
    m_side = addValue( content, grid, _( "Side Fillet (JS min)" ) );
    sizer->Add( grid, 0, wxBOTTOM, 12 );

    IPC_DIAGRAM* diagram = new IPC_DIAGRAM( content, IPC_DIAGRAM::FILLETS );
    sizer->Add( diagram, 1, wxEXPAND );
    m_diagrams.push_back( diagram );
    content->SetSizer( sizer );
}


void DIALOG_IPC_FOOTPRINT_WIZARD::buildComponentTolerancesPage()
{
    wxPanel* page = makePage( wxEmptyString, _( "Enter the required package values." ), true );
    wxPanel* content = contentOf( page );

    wxBoxSizer* sizer = new wxBoxSizer( wxVERTICAL );
    wxStaticText* text = new wxStaticText( content, wxID_ANY,
            _( "Component manufacturers usually specify the minimum and maximum value for each "
               "package dimension.  Component tolerance ranges are derived by subtracting the "
               "minimum value from the maximum.  These ranges may be adjusted based upon "
               "experience from suppliers." ) );
    text->Wrap( FromDIP( 600 ) );
    sizer->Add( text, 0, wxBOTTOM, 16 );

    m_tolCalc = new wxCheckBox( content, wxID_ANY, _( "Use calculated component tolerances" ) );
    sizer->Add( m_tolCalc, 0, wxBOTTOM, 12 );

    wxFlexGridSizer* grid = new wxFlexGridSizer( 2, 6, 6 );
    m_tolL = addValue( content, grid, _( "Tolerance on the overall width of the component, including leads" ) );
    m_tolS = addValue( content, grid, _( "Tolerance on the inner distance between the heels of the opposing rows of leads" ) );
    m_tolW = addValue( content, grid, _( "Tolerance on the width of the component leads" ) );
    sizer->Add( grid, 0, wxLEFT, 24 );
    content->SetSizer( sizer );
}


void DIALOG_IPC_FOOTPRINT_WIZARD::buildIpcTolerancesPage()
{
    wxPanel* page = makePage( wxEmptyString, _( "Enter the required tolerance values." ), true );
    wxPanel* content = contentOf( page );

    wxBoxSizer* sizer = new wxBoxSizer( wxVERTICAL );
    wxStaticText* text = new wxStaticText( content, wxID_ANY,
            _( "IPC® specifies certain tolerances for a number of standardized surface-mount "
               "package types.  You can modify here the tolerances related to fabrication and "
               "placement.  Such modification may result in the creation of non IPC® compliant "
               "PCB footprints." ) );
    text->Wrap( FromDIP( 600 ) );
    sizer->Add( text, 0, wxBOTTOM, 16 );

    m_ipcDefaults = new wxCheckBox( content, wxID_ANY, _( "Use Default Values" ) );
    sizer->Add( m_ipcDefaults, 0, wxBOTTOM, 12 );

    wxFlexGridSizer* grid = new wxFlexGridSizer( 2, 10, 6 );
    m_fab = addValue( content, grid, _( "Fabrication Tolerance Assumption (F)" ) );
    m_place = addValue( content, grid, _( "Placement Tolerance Assumption (P)" ) );
    m_courtExcess = addValue( content, grid, _( "Courtyard Excess" ) );
    sizer->Add( grid, 0, wxLEFT, 24 );
    content->SetSizer( sizer );
}


void DIALOG_IPC_FOOTPRINT_WIZARD::buildFootprintPage()
{
    wxPanel* page = makePage( wxEmptyString,
                              _( "The footprint dimensions can now be inferred from the package "
                                 "dimensions.\nYou can review and modify them here." ),
                              true );
    wxPanel* content = contentOf( page );

    wxBoxSizer* sizer = new wxBoxSizer( wxHORIZONTAL );
    wxBoxSizer* left = new wxBoxSizer( wxVERTICAL );

    m_padSummary = new wxStaticText( content, wxID_ANY, wxEmptyString );
    left->Add( m_padSummary, 0, wxBOTTOM, 16 );

    m_padCalc = new wxCheckBox( content, wxID_ANY, _( "Use calculated footprint values" ) );
    left->Add( m_padCalc, 0, wxBOTTOM, 12 );

    wxFlexGridSizer* grid = new wxFlexGridSizer( 2, 6, 6 );
    m_padX = addValue( content, grid, _( "Pad Width (X)" ) );
    m_padY = addValue( content, grid, _( "Pad Length (Y)" ) );
    m_padC = addValue( content, grid, _( "Pad Spacing (C)" ) );
    left->Add( grid, 0, wxLEFT, 24 );

    wxBoxSizer* shape = new wxBoxSizer( wxHORIZONTAL );
    shape->Add( new wxStaticText( content, wxID_ANY, _( "Pad Shape" ) ), 0,
                wxALIGN_CENTER_VERTICAL | wxRIGHT, 12 );
    m_padShape = new wxChoice( content, wxID_ANY );
    m_padShape->Append( _( "Rectangular" ) );
    m_padShape->Append( _( "Rounded Rectangular" ) );
    shape->Add( m_padShape, 0 );
    left->Add( shape, 0, wxTOP | wxLEFT, 24 );
    sizer->Add( left, 1, wxEXPAND | wxRIGHT, 12 );

    IPC_DIAGRAM* diagram = new IPC_DIAGRAM( content, IPC_DIAGRAM::PADS );
    sizer->Add( diagram, 0, wxEXPAND );
    m_diagrams.push_back( diagram );
    content->SetSizer( sizer );
}


void DIALOG_IPC_FOOTPRINT_WIZARD::buildSilkPage()
{
    wxPanel* page = makePage( wxEmptyString,
                              _( "The silkscreen dimensions can now be inferred from the package "
                                 "dimensions.\nYou can review and modify them here." ),
                              true );
    wxPanel* content = contentOf( page );

    wxBoxSizer* sizer = new wxBoxSizer( wxHORIZONTAL );
    wxBoxSizer* left = new wxBoxSizer( wxVERTICAL );

    wxStaticText* text = new wxStaticText( content, wxID_ANY,
            _( "The recommended silkscreen dimensions have been calculated based on the above "
               "selection of package and dimensions.  On this page you can further refine the "
               "silkscreen aspect by defining the line width and by modifying the calculated "
               "silkscreen dimensions." ) );
    text->Wrap( FromDIP( 420 ) );
    left->Add( text, 0, wxBOTTOM, 16 );

    wxFlexGridSizer* widthGrid = new wxFlexGridSizer( 2, 6, 6 );
    m_silkWidth = addValue( content, widthGrid, _( "Silkscreen Line Width" ) );
    left->Add( widthGrid, 0, wxBOTTOM, 12 );

    m_silkCalc = new wxCheckBox( content, wxID_ANY, _( "Use calculated silkscreen dimensions" ) );
    left->Add( m_silkCalc, 0, wxBOTTOM, 8 );

    wxFlexGridSizer* grid = new wxFlexGridSizer( 2, 6, 6 );
    m_silkR1 = addValue( content, grid, _( "Clearance to Pads (R1)" ) );
    m_silkR2 = addValue( content, grid, _( "Offset from Body (R2)" ) );
    left->Add( grid, 0, wxLEFT, 24 );
    sizer->Add( left, 1, wxEXPAND | wxRIGHT, 12 );

    IPC_DIAGRAM* diagram = new IPC_DIAGRAM( content, IPC_DIAGRAM::SILK );
    sizer->Add( diagram, 0, wxEXPAND );
    m_diagrams.push_back( diagram );
    content->SetSizer( sizer );
}


void DIALOG_IPC_FOOTPRINT_WIZARD::buildMechanicalPage()
{
    wxPanel* page = makePage( wxEmptyString,
                              _( "The mechanical dimensions can now be inferred from the package "
                                 "dimensions.\nYou can review and modify them here." ),
                              true );
    wxPanel* content = contentOf( page );

    wxBoxSizer* sizer = new wxBoxSizer( wxHORIZONTAL );
    wxBoxSizer* left = new wxBoxSizer( wxVERTICAL );

    auto group =
            [&]( wxCheckBox*& aAdd, wxCheckBox*& aCalc, const wxString& aAddText,
                 VALUE_ROW& a, const wxString& aLabelA, VALUE_ROW& b, const wxString& aLabelB,
                 VALUE_ROW& aWidth, const wxString& aLayer )
            {
                aAdd = new wxCheckBox( content, wxID_ANY, aAddText );
                left->Add( aAdd, 0, wxTOP, 12 );
                aCalc = new wxCheckBox( content, wxID_ANY, _( "Use calculated values" ) );
                left->Add( aCalc, 0, wxLEFT | wxTOP, 16 );

                wxFlexGridSizer* grid = new wxFlexGridSizer( 4, 6, 6 );
                a = addValue( content, grid, aLabelA );
                aWidth = addValue( content, grid, _( "Line Width" ) );
                b = addValue( content, grid, aLabelB );
                grid->Add( new wxStaticText( content, wxID_ANY, _( "Layer" ) ), 0,
                           wxALIGN_CENTER_VERTICAL | wxRIGHT, 12 );
                grid->Add( new wxStaticText( content, wxID_ANY, aLayer ), 0, wxALIGN_CENTER_VERTICAL );
                left->Add( grid, 0, wxLEFT | wxTOP, 32 );
            };

    wxStaticText* text = new wxStaticText( content, wxID_ANY,
            _( "Choose whether to add courtyard and assembly information to the footprint, using "
               "the IPC® calculated values or your own.  You can also add a component body: a 3D "
               "model with the volume of the package." ) );
    text->Wrap( FromDIP( 520 ) );
    left->Add( text, 0, wxBOTTOM, 4 );

    group( m_addCourt, m_courtCalc, _( "Add Courtyard Information" ), m_courtV1, wxS( "V1" ),
           m_courtV2, wxS( "V2" ), m_courtWidth, wxS( "F.Courtyard" ) );
    group( m_addAssy, m_assyCalc, _( "Add Assembly Information" ), m_assyA, wxS( "A" ), m_assyB,
           wxS( "B" ), m_assyWidth, wxS( "F.Fab" ) );

    m_addBody = new wxCheckBox( content, wxID_ANY, _( "Add Component Body Information" ) );
    left->Add( m_addBody, 0, wxTOP, 12 );
    m_bodyInfo = new wxStaticText( content, wxID_ANY, wxEmptyString );
    left->Add( m_bodyInfo, 0, wxLEFT | wxTOP, 32 );
    sizer->Add( left, 1, wxEXPAND | wxRIGHT, 12 );

    IPC_DIAGRAM* diagram = new IPC_DIAGRAM( content, IPC_DIAGRAM::MECHANICAL );
    sizer->Add( diagram, 0, wxEXPAND );
    m_diagrams.push_back( diagram );
    content->SetSizer( sizer );
}


void DIALOG_IPC_FOOTPRINT_WIZARD::buildDescriptionPage()
{
    wxPanel* page = makePage( wxEmptyString,
                              _( "The footprint values can now be inferred from the package "
                                 "dimensions.\nYou can review and modify them here." ),
                              false );
    wxPanel* content = contentOf( page );

    wxBoxSizer* sizer = new wxBoxSizer( wxVERTICAL );
    m_nameSuggested = new wxCheckBox( content, wxID_ANY, _( "Use suggested values" ) );
    sizer->Add( m_nameSuggested, 0, wxBOTTOM, 16 );

    wxFlexGridSizer* grid = new wxFlexGridSizer( 2, 10, 12 );
    grid->AddGrowableCol( 1 );
    grid->Add( new wxStaticText( content, wxID_ANY, _( "Name" ) ), 0, wxALIGN_CENTER_VERTICAL );
    m_name = new wxTextCtrl( content, wxID_ANY );
    grid->Add( m_name, 1, wxEXPAND );
    grid->Add( new wxStaticText( content, wxID_ANY, _( "Description" ) ), 0, wxALIGN_CENTER_VERTICAL );
    m_description = new wxTextCtrl( content, wxID_ANY );
    grid->Add( m_description, 1, wxEXPAND );
    sizer->Add( grid, 0, wxEXPAND | wxLEFT, 24 );
    content->SetSizer( sizer );
}


void DIALOG_IPC_FOOTPRINT_WIZARD::buildDestinationPage()
{
    wxPanel* page = makePage( _( "Footprint Destination" ),
                              _( "Select where to store the finished footprint." ), false );
    wxPanel* content = contentOf( page );

    wxFlexGridSizer* grid = new wxFlexGridSizer( 2, 14, 12 );
    grid->AddGrowableCol( 1 );

    m_destExisting = new wxRadioButton( content, wxID_ANY, _( "Existing Library" ),
                                        wxDefaultPosition, wxDefaultSize, wxRB_GROUP );
    m_libraries = new wxChoice( content, wxID_ANY );
    grid->Add( m_destExisting, 0, wxALIGN_CENTER_VERTICAL );
    grid->Add( m_libraries, 1, wxEXPAND );

    m_destNew = new wxRadioButton( content, wxID_ANY, _( "New Library" ) );
    grid->Add( m_destNew, 0, wxALIGN_CENTER_VERTICAL );
    grid->Add( new wxStaticText( content, wxID_ANY,
                                 _( "You will be asked where to create it after Finish." ) ),
               0, wxALIGN_CENTER_VERTICAL );

    m_destCurrent = new wxRadioButton( content, wxID_ANY, _( "Current Library" ) );
    m_currentLabel = new wxStaticText( content, wxID_ANY, m_currentLibrary );
    grid->Add( m_destCurrent, 0, wxALIGN_CENTER_VERTICAL );
    grid->Add( m_currentLabel, 0, wxALIGN_CENTER_VERTICAL );

    wxBoxSizer* sizer = new wxBoxSizer( wxVERTICAL );
    sizer->Add( grid, 0, wxEXPAND | wxRIGHT, 120 );

    m_make3D = new wxCheckBox( content, wxID_ANY, _( "Produce 3D model (VRML box of the package body)" ) );
    sizer->Add( m_make3D, 0, wxTOP, 24 );
    content->SetSizer( sizer );

    // Libraries the footprint can be written to
    std::vector<wxString> names = writableLibraries( m_frame->Prj() );

    for( const wxString& name : names )
        m_libraries->Append( name );

    if( !m_currentLibrary.IsEmpty()
        && std::find( names.begin(), names.end(), m_currentLibrary ) != names.end() )
    {
        m_destCurrent->SetValue( true );
        m_libraries->SetStringSelection( m_currentLibrary );
    }
    else
    {
        m_destCurrent->Enable( false );
        m_currentLabel->SetLabel( _( "(no writable library open)" ) );
        m_destExisting->SetValue( true );
    }

    m_make3D->SetValue( true );
}


void DIALOG_IPC_FOOTPRINT_WIZARD::applyFamily( FAMILY aFamily )
{
    m_input = Defaults( aFamily );
    m_filling = true;

    // Select the family's row (when changed in code)
    const std::vector<FAMILY_INFO>& families = Families();

    for( size_t ii = 0; ii < families.size(); ++ii )
    {
        if( families[ii].family == aFamily
            && !( m_families->GetItemState( ii, wxLIST_STATE_SELECTED ) & wxLIST_STATE_SELECTED ) )
        {
            m_families->SetItemState( ii, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                                      wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED );
        }

        if( families[ii].family == aFamily )
        {
            m_familyText->SetLabel( wxString::Format( _( "The selected component is %s.\n"
                                                         "This will allow you to generate %s packages." ),
                                                      families[ii].name, families[ii].packages ) );
        }
    }

    // Which package values the family needs, and what they are called
    bool two = IsTwoTerminal( aFamily );
    bool noLead = IsNoLead( aFamily );

    auto range =
            [&]( RANGE_ROW& aRow, const wxString& aLabel, const RANGE& aValue )
            {
                bool show = !aLabel.IsEmpty();
                aRow.label->Show( show );
                aRow.min->Show( show );
                aRow.max->Show( show );
                aRow.label->SetLabel( aLabel );
                setNumber( aRow.min, aValue.min );
                setNumber( aRow.max, aValue.max );
            };

    switch( aFamily )
    {
    case FAMILY::CHIP:
        range( m_rowL, _( "Body Length Range (L)" ), m_input.L );
        range( m_rowW, _( "Body Width Range (W)" ), m_input.W );
        range( m_rowT, _( "Bandwidth Range (T)" ), m_input.T );
        range( m_rowD, wxEmptyString, m_input.D );
        range( m_rowE, wxEmptyString, m_input.E );
        break;

    case FAMILY::MOLDED:
        range( m_rowL, _( "Overall Length Range (L)" ), m_input.L );
        range( m_rowW, _( "Terminal Width Range (W)" ), m_input.W );
        range( m_rowT, _( "Terminal Length Range (T)" ), m_input.T );
        range( m_rowD, wxEmptyString, m_input.D );
        range( m_rowE, _( "Body Width Range (H)" ), m_input.E );
        break;

    case FAMILY::MELF:
        range( m_rowL, _( "Body Length Range (L)" ), m_input.L );
        range( m_rowW, _( "Body Diameter Range (W)" ), m_input.W );
        range( m_rowT, _( "Terminal Length Range (T)" ), m_input.T );
        range( m_rowD, wxEmptyString, m_input.D );
        range( m_rowE, wxEmptyString, m_input.E );
        break;

    case FAMILY::SOIC:
    case FAMILY::SOT23:
        range( m_rowL, _( "Lead Span Range (L)" ), m_input.L );
        range( m_rowW, _( "Lead Width Range (b)" ), m_input.W );
        range( m_rowT, _( "Lead Length Range (T)" ), m_input.T );
        range( m_rowD, _( "Body Length Range (D)" ), m_input.D );
        range( m_rowE, _( "Body Width Range (E)" ), m_input.E );
        break;

    case FAMILY::QFP:
        range( m_rowL, _( "Lead Span Range (L)" ), m_input.L );
        range( m_rowW, _( "Lead Width Range (b)" ), m_input.W );
        range( m_rowT, _( "Lead Length Range (T)" ), m_input.T );
        range( m_rowD, _( "Body Size Range (D)" ), m_input.D );
        range( m_rowE, wxEmptyString, m_input.E );
        break;

    case FAMILY::DFN:
        range( m_rowL, _( "Body Width Range (E)" ), m_input.L );
        range( m_rowW, _( "Terminal Width Range (b)" ), m_input.W );
        range( m_rowT, _( "Terminal Length Range (L)" ), m_input.T );
        range( m_rowD, _( "Body Length Range (D)" ), m_input.D );
        range( m_rowE, wxEmptyString, m_input.E );
        break;

    case FAMILY::QFN:
        range( m_rowL, _( "Body Size Range (D)" ), m_input.L );
        range( m_rowW, _( "Terminal Width Range (b)" ), m_input.W );
        range( m_rowT, _( "Terminal Length Range (L)" ), m_input.T );
        range( m_rowD, wxEmptyString, m_input.D );
        range( m_rowE, wxEmptyString, m_input.E );
        break;
    }

    auto value =
            [&]( VALUE_ROW& aRow, bool aShow, double aValue )
            {
                aRow.label->Show( aShow );
                aRow.value->Show( aShow );
                setNumber( aRow.value, aValue );
            };

    value( m_rowHeight, true, m_input.height );
    value( m_rowPitch, !two, m_input.pitch );
    m_rowPins.label->Show( !two );
    m_rowPins.value->Show( !two );
    m_rowPins.value->ChangeValue( wxString::Format( wxS( "%d" ), m_input.pins ) );
    value( m_rowEpX, noLead, m_input.epX );
    value( m_rowEpY, noLead, m_input.epY );

    std::vector<wxString> types = PackageTypes( aFamily );
    m_packageType->Clear();

    for( const wxString& type : types )
        m_packageType->Append( type );

    m_packageType->SetSelection( types.empty() ? wxNOT_FOUND : 0 );
    m_typeLabel->Show( !types.empty() );
    m_packageType->Show( !types.empty() );

    m_polarityLabel->Show( two );

    for( int k = 0; k < 3; ++k )
    {
        m_polarity[k]->Show( two );
        m_polarity[k]->SetValue( k == m_input.polarityPin );
    }

    // Calculated / default values everywhere
    for( wxCheckBox* box : { m_heelCalc, m_filletDefaults, m_tolCalc, m_ipcDefaults, m_padCalc,
                             m_silkCalc, m_courtCalc, m_assyCalc, m_nameSuggested } )
    {
        box->SetValue( true );
    }

    m_addCourt->SetValue( true );
    m_addAssy->SetValue( true );
    m_addBody->SetValue( true );
    m_density->SetSelection( (int) m_input.density );
    m_padShape->SetSelection( 1 );
    setNumber( m_silkWidth.value, m_input.silkWidth );
    setNumber( m_courtWidth.value, m_input.courtLineWidth );
    setNumber( m_assyWidth.value, m_input.assyLineWidth );

    for( IPC_DIAGRAM* diagram : m_diagrams )
        diagram->SetFamily( aFamily );

    m_filling = false;

    updateTitles();
    updateEnables();
    readInput();
    recalculate();

    for( wxPanel* page : m_pages )
        page->Layout();
}


void DIALOG_IPC_FOOTPRINT_WIZARD::updateTitles()
{
    wxString name = familyTitle( m_input.family );
    const wxString titles[PG_COUNT] = {
        wxEmptyString,
        wxEmptyString,
        wxString::Format( _( "%s Package Dimensions" ), name ),
        wxString::Format( _( "%s Package Heel Spacing" ), name ),
        wxString::Format( _( "%s Solder Fillets" ), name ),
        wxString::Format( _( "%s Component Tolerances" ), name ),
        wxString::Format( _( "%s IPC Tolerances" ), name ),
        wxString::Format( _( "%s Footprint Dimensions" ), name ),
        wxString::Format( _( "%s Silkscreen Dimensions" ), name ),
        wxString::Format( _( "%s Courtyard, Assembly and Component Body Information" ), name ),
        wxString::Format( _( "%s Footprint Description" ), name ),
        wxEmptyString
    };

    for( int ii = PG_DIMENSIONS; ii < PG_DESTINATION; ++ii )
        m_titles[ii]->SetLabel( titles[ii] );
}


void DIALOG_IPC_FOOTPRINT_WIZARD::updateEnables()
{
    auto enable =
            [&]( std::initializer_list<VALUE_ROW*> aRows, bool aEnable )
            {
                for( VALUE_ROW* row : aRows )
                    row->value->Enable( aEnable );
            };

    enable( { &m_sMin, &m_sMax }, !m_heelCalc->GetValue() );
    enable( { &m_toe, &m_heel, &m_side }, !m_filletDefaults->GetValue() );
    enable( { &m_tolL, &m_tolS, &m_tolW }, !m_tolCalc->GetValue() );
    enable( { &m_fab, &m_place, &m_courtExcess }, !m_ipcDefaults->GetValue() );
    enable( { &m_padX, &m_padY, &m_padC }, !m_padCalc->GetValue() );
    enable( { &m_silkR1, &m_silkR2 }, !m_silkCalc->GetValue() );

    m_courtCalc->Enable( m_addCourt->GetValue() );
    enable( { &m_courtWidth }, m_addCourt->GetValue() );
    enable( { &m_courtV1, &m_courtV2 }, m_addCourt->GetValue() && !m_courtCalc->GetValue() );
    m_assyCalc->Enable( m_addAssy->GetValue() );
    enable( { &m_assyWidth }, m_addAssy->GetValue() );
    enable( { &m_assyA, &m_assyB }, m_addAssy->GetValue() && !m_assyCalc->GetValue() );

    m_name->Enable( !m_nameSuggested->GetValue() );
    m_description->Enable( !m_nameSuggested->GetValue() );
    m_libraries->Enable( m_destExisting->GetValue() );
}


double DIALOG_IPC_FOOTPRINT_WIZARD::number( wxTextCtrl* aCtrl ) const
{
    wxString text = aCtrl->GetValue().Strip( wxString::both ).Lower();
    text.Replace( wxS( "mm" ), wxEmptyString );
    text.Replace( wxS( "," ), wxS( "." ) );

    double value = 0;

    if( !text.Strip( wxString::both ).ToCDouble( &value ) )
        return 0;

    return value;
}


void DIALOG_IPC_FOOTPRINT_WIZARD::setNumber( wxTextCtrl* aCtrl, double aValue )
{
    // Show as Altium does: "0.9mm", without trailing zeros
    wxString text = wxString::FromCDouble( std::round( aValue * 10000.0 ) / 10000.0 );
    aCtrl->ChangeValue( text + wxS( "mm" ) );
}


void DIALOG_IPC_FOOTPRINT_WIZARD::readInput()
{
    IPC::INPUT& in = m_input;

    auto range = [&]( RANGE_ROW& aRow, RANGE& aRange )
                 {
                     aRange.min = number( aRow.min );
                     aRange.max = number( aRow.max );
                 };

    range( m_rowL, in.L );
    range( m_rowW, in.W );
    range( m_rowT, in.T );
    range( m_rowD, in.D );
    range( m_rowE, in.E );

    in.height = number( m_rowHeight.value );
    in.pitch = number( m_rowPitch.value );

    long pins = 0;
    m_rowPins.value->GetValue().ToLong( &pins );
    in.pins = IsTwoTerminal( in.family ) ? 2 : (int) pins;

    in.epX = number( m_rowEpX.value );
    in.epY = number( m_rowEpY.value );
    in.packageType = std::max( 0, m_packageType->GetSelection() );

    for( int k = 0; k < 3; ++k )
    {
        if( m_polarity[k]->GetValue() )
            in.polarityPin = k;
    }

    auto ovr = [&]( OVERRIDE& aOverride, wxCheckBox* aCalc, VALUE_ROW& aRow )
               {
                   aOverride.calc = aCalc->GetValue();
                   aOverride.value = number( aRow.value );
               };

    ovr( in.sMin, m_heelCalc, m_sMin );
    ovr( in.sMax, m_heelCalc, m_sMax );
    ovr( in.toe, m_filletDefaults, m_toe );
    ovr( in.heel, m_filletDefaults, m_heel );
    ovr( in.side, m_filletDefaults, m_side );
    in.density = (DENSITY) std::max( 0, m_density->GetSelection() );
    ovr( in.tolL, m_tolCalc, m_tolL );
    ovr( in.tolS, m_tolCalc, m_tolS );
    ovr( in.tolW, m_tolCalc, m_tolW );

    in.ipcDefaults = m_ipcDefaults->GetValue();
    in.fabTol = number( m_fab.value );
    in.placeTol = number( m_place.value );
    in.courtyardExcess = number( m_courtExcess.value );

    ovr( in.padX, m_padCalc, m_padX );
    ovr( in.padY, m_padCalc, m_padY );
    ovr( in.padC, m_padCalc, m_padC );
    in.roundedPads = m_padShape->GetSelection() == 1;

    in.silkWidth = number( m_silkWidth.value );
    ovr( in.silkClearance, m_silkCalc, m_silkR1 );
    ovr( in.silkOffset, m_silkCalc, m_silkR2 );

    in.addCourtyard = m_addCourt->GetValue();
    ovr( in.courtV1, m_courtCalc, m_courtV1 );
    ovr( in.courtV2, m_courtCalc, m_courtV2 );
    in.courtLineWidth = number( m_courtWidth.value );
    in.addAssembly = m_addAssy->GetValue();
    ovr( in.assyA, m_assyCalc, m_assyA );
    ovr( in.assyB, m_assyCalc, m_assyB );
    in.assyLineWidth = number( m_assyWidth.value );
    in.addBody = m_addBody->GetValue();

    in.suggestedName = m_nameSuggested->GetValue();
    in.name = m_name->GetValue();
    in.description = m_description->GetValue();
}


void DIALOG_IPC_FOOTPRINT_WIZARD::recalculate()
{
    m_result = Calculate( m_input );

    m_filling = true;

    // Calculated values into the fields that follow them
    auto show = [&]( wxCheckBox* aCalc, std::initializer_list<std::pair<VALUE_ROW*, double>> aRows )
                {
                    if( !aCalc->GetValue() )
                        return;

                    for( const auto& [row, value] : aRows )
                        setNumber( row->value, value );
                };

    show( m_heelCalc, { { &m_sMin, m_result.sMin }, { &m_sMax, m_result.sMax } } );
    show( m_filletDefaults, { { &m_toe, m_result.toe }, { &m_heel, m_result.heel },
                              { &m_side, m_result.side } } );
    show( m_tolCalc, { { &m_tolL, m_result.tolL }, { &m_tolS, m_result.tolS },
                       { &m_tolW, m_result.tolW } } );
    show( m_ipcDefaults, { { &m_fab, m_result.fabTol }, { &m_place, m_result.placeTol },
                           { &m_courtExcess, m_result.courtyardExcess } } );

    if( m_result.ok )
    {
        show( m_padCalc, { { &m_padX, m_result.padX }, { &m_padY, m_result.padY },
                           { &m_padC, m_result.padC } } );
        show( m_silkCalc, { { &m_silkR1, m_result.silkClearance }, { &m_silkR2, m_result.silkOffset } } );
        show( m_courtCalc, { { &m_courtV1, m_result.courtV1 }, { &m_courtV2, m_result.courtV2 } } );
        show( m_assyCalc, { { &m_assyA, m_result.assyA }, { &m_assyB, m_result.assyB } } );

        if( m_nameSuggested->GetValue() )
        {
            m_name->ChangeValue( m_result.name );
            m_description->ChangeValue( m_result.description );
        }

        m_padSummary->SetLabel( wxString::Format( _( "The footprint has %d leads and a pad spacing of "
                                                     "%gmm.  You can modify here the calculated "
                                                     "dimensions of the footprint." ),
                                                  (int) m_result.pads.size(), m_result.padC ) );
        m_bodyInfo->SetLabel( wxString::Format( _( "Body %.2f x %.2f mm, height %.2f mm" ),
                                                m_result.bodyX, m_result.bodyY,
                                                m_result.bodyHeight ) );
    }

    m_filling = false;

    m_error->SetLabel( m_result.ok ? wxString() : m_result.error );
    m_finish->Enable( m_result.ok && m_book->GetSelection() >= PG_DIMENSIONS );

    for( IPC_PREVIEW* preview : m_previews )
        preview->Set( m_result, m_input.addCourtyard, m_input.addAssembly );
}


void DIALOG_IPC_FOOTPRINT_WIZARD::showPage( int aPage )
{
    aPage = std::clamp( aPage, 0, PG_COUNT - 1 );
    m_book->SetSelection( aPage );

    m_back->Enable( aPage > PG_INTRO );
    m_next->Enable( aPage < PG_DESTINATION );
    m_finish->Enable( m_result.ok && aPage >= PG_DIMENSIONS );

    if( aPage == PG_TYPE )
        m_families->SetFocus();

    m_pages[aPage]->Layout();
}


DIALOG_IPC_FOOTPRINT_WIZARD::DESTINATION DIALOG_IPC_FOOTPRINT_WIZARD::GetDestination() const
{
    if( m_destNew->GetValue() )
        return DESTINATION::NEW;

    if( m_destCurrent->GetValue() )
        return DESTINATION::CURRENT;

    return DESTINATION::EXISTING;
}


wxString DIALOG_IPC_FOOTPRINT_WIZARD::GetLibrary() const
{
    if( m_destCurrent->GetValue() )
        return m_currentLibrary;

    return m_libraries->GetStringSelection();
}


bool DIALOG_IPC_FOOTPRINT_WIZARD::Want3DModel() const
{
    return m_make3D->GetValue() && m_input.addBody;
}


FOOTPRINT* DIALOG_IPC_FOOTPRINT_WIZARD::BuildFootprint( const wxString& aLibrary )
{
    if( !m_result.ok )
        return nullptr;

    const RESULT& r = m_result;
    auto mm = []( double aValue ) { return pcbIUScale.mmToIU( aValue ); };
    auto pt = [&]( double x, double y ) { return VECTOR2I( mm( x ), mm( y ) ); };

    FOOTPRINT* fp = m_frame->CreateNewFootprint( r.name, aLibrary );
    fp->SetAttributes( FP_SMD );
    fp->SetLibDescription( r.description );
    fp->SetKeywords( wxS( "IPC-7351 " ) + Families()[0].name.Lower() );

    for( const FAMILY_INFO& info : Families() )
    {
        if( info.family == m_input.family )
            fp->SetKeywords( wxS( "IPC-7351 " ) + info.name + wxS( " " ) + info.packages );
    }

    // Pads
    for( const IPC::PAD& ipcPad : r.pads )
    {
        ::PAD* pad = new ::PAD( fp );
        pad->SetAttribute( PAD_ATTRIB::SMD );
        pad->SetLayerSet( ::PAD::SMDMask() );
        pad->SetNumber( ipcPad.number );
        pad->SetSize( PADSTACK::ALL_LAYERS, VECTOR2I( mm( ipcPad.sx ), mm( ipcPad.sy ) ) );
        pad->SetPosition( pt( ipcPad.x, ipcPad.y ) );

        if( m_input.roundedPads )
        {
            // IPC-7351C: 25% corner radius, at most 0.25 mm
            double smaller = std::min( ipcPad.sx, ipcPad.sy );
            pad->SetShape( PADSTACK::ALL_LAYERS, PAD_SHAPE::ROUNDRECT );
            pad->SetRoundRectRadiusRatio( PADSTACK::ALL_LAYERS,
                                          std::min( 0.25, 0.25 / std::max( smaller, 0.01 ) ) );
        }
        else
        {
            pad->SetShape( PADSTACK::ALL_LAYERS, PAD_SHAPE::RECTANGLE );
        }

        fp->Add( pad );
    }

    auto addLine =
            [&]( PCB_LAYER_ID aLayer, double aWidth, double x1, double y1, double x2, double y2 )
            {
                PCB_SHAPE* shape = new PCB_SHAPE( fp, SHAPE_T::SEGMENT );
                shape->SetLayer( aLayer );
                shape->SetStroke( STROKE_PARAMS( mm( aWidth ), LINE_STYLE::SOLID ) );
                shape->SetStart( pt( x1, y1 ) );
                shape->SetEnd( pt( x2, y2 ) );
                fp->Add( shape );
            };

    auto addRect =
            [&]( PCB_LAYER_ID aLayer, double aWidth, double sx, double sy )
            {
                PCB_SHAPE* shape = new PCB_SHAPE( fp, SHAPE_T::RECTANGLE );
                shape->SetLayer( aLayer );
                shape->SetStroke( STROKE_PARAMS( mm( aWidth ), LINE_STYLE::SOLID ) );
                shape->SetStart( pt( -sx / 2, -sy / 2 ) );
                shape->SetEnd( pt( sx / 2, sy / 2 ) );
                fp->Add( shape );
            };

    // Silkscreen
    for( const SEGMENT& seg : r.silk )
        addLine( F_SilkS, m_input.silkWidth, seg.x1, seg.y1, seg.x2, seg.y2 );

    if( r.hasPin1Dot )
    {
        PCB_SHAPE* dot = new PCB_SHAPE( fp, SHAPE_T::CIRCLE );
        dot->SetLayer( F_SilkS );
        dot->SetStroke( STROKE_PARAMS( mm( 0.1 ), LINE_STYLE::SOLID ) );
        dot->SetFillMode( FILL_T::FILLED_SHAPE );
        dot->SetCenter( pt( r.pin1X, r.pin1Y ) );
        dot->SetEnd( pt( r.pin1X + 0.075, r.pin1Y ) );
        fp->Add( dot );
    }

    // Courtyard
    if( m_input.addCourtyard )
        addRect( F_CrtYd, m_input.courtLineWidth, r.courtV1, r.courtV2 );

    // Assembly outline, with a chamfer at pin 1, and the designator on it
    if( m_input.addAssembly )
    {
        double a = r.assyA / 2, b = r.assyB / 2;
        double w = m_input.assyLineWidth;

        if( r.pin1Chamfer )
        {
            double ch = std::min( 1.0, 0.25 * std::min( r.assyA, r.assyB ) );
            addLine( F_Fab, w, -a + ch, -b, a, -b );
            addLine( F_Fab, w, a, -b, a, b );
            addLine( F_Fab, w, a, b, -a, b );
            addLine( F_Fab, w, -a, b, -a, -b + ch );
            addLine( F_Fab, w, -a, -b + ch, -a + ch, -b );
        }
        else
        {
            addRect( F_Fab, w, r.assyA, r.assyB );
        }

    }

    // Altium's wizard footprint has no designator or comment strings of its own: drop the
    // ${REFERENCE} assembly text the new-footprint defaults bring
    std::vector<BOARD_ITEM*> extraTexts;

    for( BOARD_ITEM* item : fp->GraphicalItems() )
    {
        if( item->Type() == PCB_TEXT_T
            && static_cast<PCB_TEXT*>( item )->GetText().Contains( wxS( "${REFERENCE}" ) ) )
        {
            extraTexts.push_back( item );
        }
    }

    for( BOARD_ITEM* item : extraTexts )
    {
        fp->Remove( item );
        delete item;
    }

    // The designator (the placed component's silkscreen name) sized to the part, above the
    // courtyard; the comment hidden, as on a placed Altium component
    double edge = std::max( r.courtV2, r.bodyY ) / 2;
    double textSize = std::clamp( std::min( r.courtV1, r.courtV2 ) * 0.5, 0.5, 1.0 );

    for( PCB_FIELD* field : { &fp->Reference(), &fp->Value() } )
    {
        field->SetTextSize( VECTOR2I( mm( textSize ), mm( textSize ) ) );
        field->SetTextThickness( mm( textSize * 0.15 ) );
    }

    fp->Reference().SetPosition( pt( 0, -edge - textSize * 0.8 ) );
    fp->Value().SetPosition( pt( 0, edge + textSize * 0.8 ) );
    fp->Value().SetText( fp->GetFPID().GetUniStringLibItemName() );
    fp->Value().SetVisible( false );

    return fp;
}


bool DIALOG_IPC_FOOTPRINT_WIZARD::Write3DModel( const wxString& aPath, const RESULT& aResult,
                                                FAMILY aFamily )
{
    wxFFile file( aPath, wxS( "w" ) );

    if( !file.IsOpened() )
        return false;

    // VRML units used by KiCad are 0.1 inch (2.54 mm)
    auto u = []( double aMM ) { return aMM / 2.54; };

    auto boxShape =
            [&]( double cx, double cy, double cz, double sx, double sy, double sz,
                 const char* aColour )
            {
                return wxString::Format( wxS( "Transform { translation %g %g %g children [ "
                                              "Shape { appearance Appearance { material Material "
                                              "{ diffuseColor %s } } geometry Box { size %g %g %g } } ] }\n" ),
                                         u( cx ), u( -cy ), u( cz ), aColour, u( sx ), u( sy ), u( sz ) );
            };

    wxString vrml = wxS( "#VRML V2.0 utf8\n# Package body generated by the IPC Compliant "
                         "Footprint Wizard\n" );

    double h = std::max( aResult.bodyHeight, 0.1 );
    bool   two = IsTwoTerminal( aFamily );

    if( two )
    {
        // Body between the terminals, metal end caps over the pads
        double cap = std::min( aResult.padY, aResult.bodyX / 3 );
        vrml += boxShape( 0, 0, h / 2, aResult.bodyX - 2 * cap, aResult.bodyY, h,
                          aFamily == FAMILY::CHIP ? "0.45 0.42 0.38" : "0.15 0.15 0.15" );
        vrml += boxShape( -( aResult.bodyX - cap ) / 2, 0, h / 2, cap, aResult.bodyY, h,
                          "0.82 0.82 0.80" );
        vrml += boxShape( ( aResult.bodyX - cap ) / 2, 0, h / 2, cap, aResult.bodyY, h,
                          "0.82 0.82 0.80" );
    }
    else
    {
        double standoff = IsNoLead( aFamily ) ? 0.0 : 0.1;
        vrml += boxShape( 0, 0, standoff + ( h - standoff ) / 2, aResult.bodyX, aResult.bodyY,
                          h - standoff, "0.15 0.15 0.15" );

        // Leads as thin plates over the pads
        for( const IPC::PAD& pad : aResult.pads )
        {
            vrml += boxShape( pad.x, pad.y, 0.1, pad.sx * 0.8, pad.sy * 0.8, 0.2, "0.82 0.82 0.80" );
        }
    }

    return file.Write( vrml );
}
