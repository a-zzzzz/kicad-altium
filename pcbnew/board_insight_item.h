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

#ifndef BOARD_INSIGHT_ITEM_H
#define BOARD_INSIGHT_ITEM_H

#include <eda_item.h>
#include <gal/color4d.h>
#include <wx/arrstr.h>
#include <vector>

/**
 * Heads-up readout drawn in the top-left corner of the PCB canvas (Altium's Board Insight
 * display, see docs/altium-parity/board-insight.md).  The owner sets the cursor position
 * and the text lines; the item only draws them.
 */
class BOARD_INSIGHT_ITEM : public EDA_ITEM
{
public:
    BOARD_INSIGHT_ITEM();

    void SetCursor( const VECTOR2I& aPos ) { m_cursor = aPos; }
    /// The text lines, each in its own colour (COLOR4D::UNSPECIFIED: the readout's colour)
    void SetLines( const wxArrayString& aLines,
                   const std::vector<KIGFX::COLOR4D>& aColours = {} )
    {
        m_lines = aLines;
        m_colours = aColours;
    }

    /// Screen pixels to keep clear at the top of the view (e.g. under the Active Bar).
    void SetTopOffset( int aPixels ) { m_topOffset = aPixels; }

    const BOX2I ViewBBox() const override;
    std::vector<int> ViewGetLayers() const override;
    void ViewDraw( int aLayer, KIGFX::VIEW* aView ) const override;

    wxString GetClass() const override { return wxT( "BOARD_INSIGHT_ITEM" ); }

#if defined( DEBUG )
    void Show( int x, std::ostream& st ) const override {}
#endif

private:
    VECTOR2I      m_cursor;
    wxArrayString               m_lines;
    std::vector<KIGFX::COLOR4D> m_colours;
    int                         m_topOffset = 0;
};

#endif // BOARD_INSIGHT_ITEM_H
