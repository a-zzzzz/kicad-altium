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

#include <board_insight_item.h>

#include <font/font.h>
#include <gal/graphics_abstraction_layer.h>
#include <gal/painter.h>
#include <layer_ids.h>
#include <preview_items/preview_utils.h>
#include <view/view.h>


BOARD_INSIGHT_ITEM::BOARD_INSIGHT_ITEM() :
        EDA_ITEM( NOT_USED )
{
}


const BOX2I BOARD_INSIGHT_ITEM::ViewBBox() const
{
    // Screen-sized text whose extent depends on the zoom; always redraw it
    BOX2I bbox;
    bbox.SetMaximum();
    return bbox;
}


std::vector<int> BOARD_INSIGHT_ITEM::ViewGetLayers() const
{
    return { LAYER_SELECT_OVERLAY, LAYER_GP_OVERLAY };
}


void BOARD_INSIGHT_ITEM::ViewDraw( int aLayer, KIGFX::VIEW* aView ) const
{
    if( m_lines.IsEmpty() )
        return;

    KIGFX::GAL* gal = aView->GetGAL();

    // Same shadow-layer choice as the other cursor readouts (Cairo draws overlays reversed)
    int  shadowLayer = gal->IsCairoEngine() ? LAYER_SELECT_OVERLAY : LAYER_GP_OVERLAY;
    bool drawingDropShadows = aLayer == shadowLayer;

    KIGFX::GAL_SCOPED_ATTRS scopedAttrs( *gal, KIGFX::GAL_SCOPED_ATTRS::ALL_ATTRS );
    gal->SetLayerDepth( gal->GetMinDepth() );

    // Pinned to the top-left corner of the view, as Altium's heads-up display sits by default,
    // so it never covers what is under the cursor (the quadrant vector points from the text
    // towards the anchor: the text hangs below and to the right of it)
    VECTOR2D anchor = aView->ToWorld( VECTOR2D( 0.0, 8.0 + m_topOffset ) );

    // As KIGFX::PREVIEW::DrawTextNextToCursor() lays the lines out (left-aligned below the
    // anchor), but each line may have its own colour (Altium's layer name is yellow)
    KIFONT::FONT*             font = KIFONT::FONT::GetFont();
    KIGFX::PREVIEW::TEXT_DIMS dims = KIGFX::PREVIEW::GetConstantGlyphHeight( gal );
    TEXT_ATTRIBUTES           attrs;
    bool                      flipped = gal->IsFlippedX();
    KIGFX::COLOR4D            normal = aView->GetPainter()->GetSettings()->GetLayerColor( LAYER_AUX_ITEMS );
    VECTOR2D                  pos = anchor;

    attrs.m_Halign = flipped ? GR_TEXT_H_ALIGN_RIGHT : GR_TEXT_H_ALIGN_LEFT;
    attrs.m_Mirrored = flipped;
    attrs.m_Size = dims.GlyphSize;
    attrs.m_StrokeWidth = drawingDropShadows ? dims.StrokeWidth + 2 * dims.ShadowWidth
                                             : dims.StrokeWidth;

    pos.x += ( flipped ? -15.0 : 15.0 ) / gal->GetWorldScale();

    if( drawingDropShadows )
        pos.x += ( flipped ? 1 : -1 ) * dims.ShadowWidth * 1.2;

    gal->SetIsFill( false );
    gal->SetIsStroke( true );

    for( size_t ii = 0; ii < m_lines.size(); ++ii )
    {
        KIGFX::COLOR4D colour = ii < m_colours.size() && m_colours[ii] != KIGFX::COLOR4D::UNSPECIFIED
                                        ? m_colours[ii] : normal;

        gal->SetStrokeColor( drawingDropShadows ? KIGFX::PREVIEW::GetShadowColor( colour ) : colour );

        pos.y += dims.LinePitch;
        font->Draw( gal, m_lines[ii], pos, attrs, KIFONT::METRICS::Default() );
    }
}
