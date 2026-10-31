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
#include <algorithm>
#include <wx/control.h>
#include <wx/dc.h>
#include <wx/propgrid/propgrid.h>
#include <properties/pg_cell_renderer.h>
#include <properties/pg_properties.h>
#include <widgets/color_swatch.h>


PG_CELL_RENDERER::PG_CELL_RENDERER() :
        wxPGDefaultRenderer()
{}


static wxColour mixColours( const wxColour& a, const wxColour& b, double t )
{
    return wxColour( a.Red() + ( b.Red() - a.Red() ) * t, a.Green() + ( b.Green() - a.Green() ) * t,
                     a.Blue() + ( b.Blue() - a.Blue() ) * t );
}


bool PG_CELL_RENDERER::Render( wxDC &aDC, const wxRect &aRect, const wxPropertyGrid *aGrid,
                               wxPGProperty *aProperty, int aColumn, int aItem, int aFlags ) const
{
    wxPGCell cell = aGrid->GetUnspecifiedValueAppearance();

    // The Properties panel draws Altium-style rows: muted right-aligned labels and values in
    // input-style boxes.  Other property grids (e.g. the simulator) keep the stock look.
    const bool panelStyle = aGrid->GetName() == PANEL_GRID_NAME && !aProperty->IsCategory();

    if( panelStyle && aColumn > 0 )
    {
        bool isColor = dynamic_cast<PGPROPERTY_COLOR4D*>( aProperty ) != nullptr;
        bool isBool = aProperty->GetValueType() == wxS( "bool" );
        bool readOnly = aProperty->HasFlag( wxPG_PROP_READONLY );

        if( isColor || isBool || readOnly )
            return renderValue( aDC, aRect, aGrid, aProperty, aColumn, aItem, aFlags );

        // Editable values sit in Altium's dark input boxes (a drop-down arrow for choices)
        wxColour bg = aGrid->GetCellBackgroundColour();
        wxColour fg = aGrid->GetCellTextColour();
        wxRect   box = aRect;

        box.Deflate( 1, 2 );
        box.width -= 2;

        aDC.SetPen( wxPen( mixColours( bg, *wxBLACK, 0.55 ) ) );
        aDC.SetBrush( wxBrush( ( aFlags & Selected ) ? aGrid->GetSelectionBackgroundColour()
                                                     : mixColours( bg, *wxBLACK, 0.30 ) ) );
        aDC.DrawRoundedRectangle( box, 2 );

        bool ret = renderValue( aDC, aRect, aGrid, aProperty, aColumn, aItem,
                                aFlags | DontUseCellBgCol );

        if( aProperty->GetChoices().IsOk() && aProperty->GetChoices().GetCount() > 0 )
        {
            int a = std::max( 3, box.height / 6 );
            int cx = box.GetRight() - a - 4;
            int cy = box.y + box.height / 2;
            wxPoint tri[3] = { { cx - a, cy - a / 2 }, { cx + a, cy - a / 2 }, { cx, cy + a / 2 + 1 } };

            // Keep the arrow clear of long values
            if( !( aFlags & Selected ) )
            {
                aDC.SetPen( *wxTRANSPARENT_PEN );
                aDC.SetBrush( wxBrush( mixColours( bg, *wxBLACK, 0.30 ) ) );
                aDC.DrawRectangle( cx - a - 4, box.y + 1, box.GetRight() - ( cx - a - 4 ), box.height - 2 );
            }

            aDC.SetPen( wxPen( fg ) );
            aDC.SetBrush( wxBrush( fg ) );
            aDC.DrawPolygon( 3, tri );
        }

        return ret;
    }

    if( aColumn > 0 )
        return renderValue( aDC, aRect, aGrid, aProperty, aColumn, aItem, aFlags );

    wxString text;
    int      preDrawFlags = aFlags;

    aProperty->GetDisplayInfo( aColumn, aItem, aFlags, &text, &cell );

    text = wxControl::Ellipsize( text, aDC, wxELLIPSIZE_END, aRect.GetWidth() );

    int imageWidth = PreDrawCell( aDC, aRect, aGrid, cell, preDrawFlags );

    int imageOffset = aProperty->GetImageOffset( imageWidth );

    if( panelStyle )
    {
        if( !( aFlags & Selected ) )
            aDC.SetTextForeground( aGrid->GetCellTextColour() );

        int textWidth = aDC.GetTextExtent( text ).x;
        imageOffset = std::max( imageOffset, aRect.GetWidth() - textWidth - 10 );
    }

    DrawEditorValue( aDC, aRect, imageOffset, text, aProperty, nullptr );

    PostDrawCell( aDC, aGrid, cell, preDrawFlags );

    return !text.IsEmpty();
}


bool PG_CELL_RENDERER::renderValue( wxDC &aDC, const wxRect &aRect, const wxPropertyGrid *aGrid,
                                    wxPGProperty *aProperty, int aColumn, int aItem,
                                    int aFlags ) const
{
    if( PGPROPERTY_COLOR4D* colorProp = dynamic_cast<PGPROPERTY_COLOR4D*>( aProperty ) )
    {
        wxAny av = colorProp->GetValue().GetAny();
        KIGFX::COLOR4D color = av.IsNull() ? KIGFX::COLOR4D::UNSPECIFIED
                                           : av.As<KIGFX::COLOR4D>();

        if( !color.m_text )
        {
            wxSize swatchSize = aGrid->ConvertDialogToPixels( wxSize( 24, 16 ) );
            int offset = ( aRect.GetHeight() - swatchSize.GetHeight() ) / 2;
            wxRect swatch( aRect.GetPosition() + wxPoint( offset, offset ), swatchSize );

            aDC.Clear();    // Ensure the "old" background is erased.
            COLOR_SWATCH::RenderToDC( &aDC, color, colorProp->GetBackgroundColor(), swatch,
                                      aGrid->ConvertDialogToPixels( CHECKERBOARD_SIZE_DU ),
                                      aGrid->GetBackgroundColour() );

            return true;
        }
    }

    if( aProperty->HasFlag( wxPG_PROP_READONLY ) && !( aFlags & Selected ) )
    {
        wxColour disabledFg = aGrid->GetCellDisabledTextColour();

        if( disabledFg.IsOk() )
        {
            aDC.SetTextForeground( disabledFg );
            return wxPGDefaultRenderer::Render( aDC, aRect, aGrid, aProperty, aColumn, aItem,
                                                aFlags | DontUseCellFgCol );
        }
    }

    // Default behavior for value column
    return wxPGDefaultRenderer::Render( aDC, aRect, aGrid, aProperty, aColumn, aItem, aFlags );
}
