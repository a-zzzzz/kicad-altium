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

#ifndef PCB_PANEL_UTILS_H
#define PCB_PANEL_UTILS_H

#include <pcb_base_frame.h>
#include <pcb_draw_panel_gal.h>
#include <view/view.h>
#include <math/vector2wx.h>
#include <base_units.h>

#include <algorithm>
#include <cmath>

/**
 * Zoom the board view so the box fills the canvas with a small margin around it (the
 * Messages and PCB panels' "zoom to object").
 */
inline void PcbZoomToBox( PCB_BASE_FRAME* aFrame, BOX2I aBox )
{
    if( !aFrame || !aFrame->GetCanvas() )
        return;

    KIGFX::VIEW* view = aFrame->GetCanvas()->GetView();

    aBox.Normalize();
    aBox.Inflate( std::max( aBox.GetWidth(), aBox.GetHeight() ) / 5 + pcbIUScale.mmToIU( 1 ) );

    // Show some board around small objects (a pad, a reference text) rather than filling the
    // window with them
    const int minSize = pcbIUScale.mmToIU( 15 );

    if( aBox.GetWidth() < minSize || aBox.GetHeight() < minSize )
    {
        VECTOR2I centre = aBox.Centre();
        int      w = std::max( (int) aBox.GetWidth(), minSize );
        int      h = std::max( (int) aBox.GetHeight(), minSize );

        aBox = BOX2I( VECTOR2I( centre.x - w / 2, centre.y - h / 2 ), VECTOR2I( w, h ) );
    }

    VECTOR2D screen = view->ToWorld( ToVECTOR2D( aFrame->GetCanvas()->GetClientSize() ), false );

    if( std::fabs( screen.x ) > 1 && std::fabs( screen.y ) > 1 )
    {
        double ratio = std::max( std::fabs( aBox.GetWidth() / screen.x ),
                                 std::fabs( aBox.GetHeight() / screen.y ) );

        if( ratio > 0 )
            view->SetScale( view->GetScale() / ratio );
    }

    view->SetCenter( aBox.Centre() );
    aFrame->GetCanvas()->ForceRefresh();
}

#endif // PCB_PANEL_UTILS_H
