/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#ifndef PANEL_3D_LAYOUT_H
#define PANEL_3D_LAYOUT_H

#include <functional>

#include <wx/panel.h>

#include <tool/tools_holder.h>
#include "3d_canvas/board_adapter.h"
#include "3d_rendering/track_ball.h"

class EDA_3D_CANVAS;
class PCB_BASE_FRAME;
class TOOL_ACTION;
class wxKeyEvent;

/**
 * The board in 3D, hosted in the PCB editor's own window in place of the 2D canvas
 * (Altium-style 3D layout mode, docs/altium-parity/3d-layout-mode.md).
 *
 * Owns a 3D canvas with the usual 3D viewer mouse/keyboard navigation (EDA_3D_CONTROLLER);
 * it shows the editor's board and is rebuilt from it by ReloadBoard().
 */
class PANEL_3D_LAYOUT : public wxPanel, public TOOLS_HOLDER
{
public:
    PANEL_3D_LAYOUT( wxWindow* aParent, PCB_BASE_FRAME* aFrame );
    ~PANEL_3D_LAYOUT() override;

    /// Rebuild the 3D scene from the editor's board as it is now
    void ReloadBoard();

    /// Rebuild the 3D scene once the current event is done (coalesces repeated requests)
    void ScheduleReload();

    /**
     * Run a view command (zoom in / out / fit, redraw) on the 3D view.
     * @return false if the 3D view has no such command.
     */
    bool RunViewAction( const TOOL_ACTION& aAction );

    /// Show the 3D view's status (camera, zoom) in the editor's status bar message field
    void UpdateStatus();

    /// Give the 3D canvas the keyboard focus
    void FocusCanvas();

    /**
     * Apply the View Configuration's 3D options (EDA_3D_VIEWER_SETTINGS::m_LayoutView: board
     * thickness scale, realistic or by-layer colours, material transparency) and rebuild the
     * 3D view.
     */
    void ApplyViewSettings();

    /// Redraw without rebuilding (visibility of 3D bodies, projection)
    void RefreshView();

    /// Altium's View > Flip Board in 3D: look at the board from the other side
    void FlipView();

    bool IsPerspective() const;
    void SetPerspective( bool aPerspective );

    /// The colour a material row of the View Configuration shows
    KIGFX::COLOR4D GetMaterialColor( int aMaterial ) const;

    /**
     * Called with the hotkey code (key | MD_* modifiers) of every key pressed in the 3D view
     * before 3D navigation sees it.  Return true to consume the key.
     */
    void SetKeyHandler( std::function<bool( int aHotKey )> aHandler ) { m_keyHandler = aHandler; }

    wxWindow*      GetToolCanvas() const override;
    BOARD_ADAPTER& GetAdapter() { return m_boardAdapter; }
    CAMERA&        GetCurrentCamera() { return m_currentCamera; }

    /// Menu letters typed in the 3D view open the editor's menus, as in 2D
    bool OpenMenuForMnemonic( int aKey ) override;

private:
    void onCanvasCharHook( wxKeyEvent& aEvent );

    /// Layer colours, transparency and thickness scale for the adapter, before a rebuild
    void updateAdapterSettings();
    void onCanvasMouse( wxMouseEvent& aEvent );

    PCB_BASE_FRAME*                    m_frame;
    EDA_3D_CANVAS*                     m_canvas;
    BOARD_ADAPTER                      m_boardAdapter;
    CAMERA&                            m_currentCamera;
    TRACK_BALL                         m_trackBallCamera;
    std::function<bool( int aHotKey )> m_keyHandler;
    bool                               m_reloadPending;
    bool                               m_statusPending;
};

#endif // PANEL_3D_LAYOUT_H
