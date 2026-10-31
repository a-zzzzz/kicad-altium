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

#ifndef ALTIUM_LAYER_TABS_H
#define ALTIUM_LAYER_TABS_H

#include <layer_ids.h>
#include <lset.h>
#include <wx/panel.h>
#include <vector>

class PCB_BASE_FRAME;

/**
 * A strip of colored layer tabs docked under the PCB canvas, one tab per enabled and
 * visible layer.  Clicking a tab makes that layer active; the mouse wheel scrolls the
 * strip when it overflows.
 */
class ALTIUM_LAYER_TABS : public wxPanel
{
public:
    ALTIUM_LAYER_TABS( PCB_BASE_FRAME* aFrame );

    /// Rebuild the tab list from the board's enabled and visible layers.
    void OnBoardChanged();

    /// Repaint after the active layer or layer colors change.
    void OnLayerChanged();

    /// Altium-style display name for a layer that still has its default KiCad name.
    static wxString DisplayName( PCB_LAYER_ID aLayer, const wxString& aBoardName );

private:
    struct TAB
    {
        PCB_LAYER_ID layer;
        wxString     label;
        wxRect       rect;
    };

    void onPaint( wxPaintEvent& aEvent );
    void onLeftDown( wxMouseEvent& aEvent );
    void onMouseWheel( wxMouseEvent& aEvent );
    void onSize( wxSizeEvent& aEvent );
    void onUpdateUI( wxUpdateUIEvent& aEvent );

    void layoutTabs( wxDC& aDC );
    void showLayerSetMenu();
    int  contentWidth() const;

    PCB_BASE_FRAME*  m_frame;
    std::vector<TAB> m_tabs;
    wxRect           m_swatchRect;     ///< active-layer colour square at the far left
    wxRect           m_layerSetRect;   ///< "LS" layer set button
    int              m_scrollOffset;
    LSET             m_shownLayers;   ///< layers m_tabs was built from
    PCB_LAYER_ID     m_paintedActive; ///< active layer at last paint
};

#endif // ALTIUM_LAYER_TABS_H
