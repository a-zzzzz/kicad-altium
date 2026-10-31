/*
 * KiRouter - a push-and-(sometimes-)shove PCB router
 *
 * Copyright (C) 2013-2017 CERN
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 * Author: Tomasz Wlostowski <tomasz.wlostowski@cern.ch>
 * Author: Maciej Suminski <maciej.suminski@cern.ch>
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

#ifndef __ROUTER_TOOL_H
#define __ROUTER_TOOL_H

#include "pns_tool_base.h"

class PCB_SELECTION_TOOL;

class ROUTER_TOOL : public PNS::TOOL_BASE
{
public:
    ROUTER_TOOL();
    ~ROUTER_TOOL();

    bool Init() override;
    void Reset( RESET_REASON aReason ) override;

    int MainLoop( const TOOL_EVENT& aEvent );
    int RouteSelected( const TOOL_EVENT& aEvent );

    int InlineBreakTrack( const TOOL_EVENT& aEvent );
    bool CanInlineDrag( int aDragMode );
    int InlineDrag( const TOOL_EVENT& aEvent );

    int SelectCopperLayerPair( const TOOL_EVENT& aEvent );
    int DpDimensionsDialog( const TOOL_EVENT& aEvent );
    int SettingsDialog( const TOOL_EVENT& aEvent );
    int ChangeRouterMode( const TOOL_EVENT& aEvent );
    int CycleRouterMode( const TOOL_EVENT& aEvent );
    int CustomTrackWidthDialog( const TOOL_EVENT& aEvent );

    PNS::PNS_MODE GetRouterMode();

    /**
     * @brief Returns whether routing is currently active.
     *
     * @return True if actively routing, false if not routing or
     *         tool is activated and idle.
     */
    bool RoutingInProgress();

    void setTransitions() override;

    // A filter for narrowing a collection representing a simple corner
    // or a non-fanout-via to a single PCB_TRACK item.
    static void NeighboringSegmentFilter( const VECTOR2I& aPt, GENERAL_COLLECTOR& aCollector,
                                          PCB_SELECTION_TOOL* aSelTool );

    void UpdateMessagePanel();

    /// A track width or via size the route in progress can take (Altium's 3 / 4 keys and the
    /// Properties panel's Interactive Routing page)
    struct SIZE_CHOICE
    {
        wxString label;          ///< "Preferred  0.25 mm", ...
        int      size = 0;       ///< track width, or via diameter
        int      drill = 0;      ///< via hole (vias only)
    };

    /**
     * The track widths for the route in progress: the width rule's minimum, preferred and
     * maximum (aRule) and the board's preset widths (aPresets), without repeats.
     */
    std::vector<SIZE_CHOICE> RouteWidthChoices( bool aRule, bool aPresets );

    /// The via sizes (diameter and hole) for the route in progress, as RouteWidthChoices()
    std::vector<SIZE_CHOICE> RouteViaChoices( bool aRule, bool aPresets );

    /**
     * Route the track in progress with these sizes from now on (KiCad's custom track / via
     * size, as the track & via size dialog sets it) and redraw the route.
     */
    void SetRouteSizes( int aWidth, int aViaDiameter, int aViaDrill );

    /**
     * Re-route the track in progress to where it ends now, with the current sizes and router
     * settings, and redraw it.  Unlike the size-changed event, it doesn't follow the mouse
     * (which may be over the Properties panel).
     */
    void RefreshRoute();

    /// The routed net's length so far: its placed tracks plus the route in progress
    double RoutedNetLength();

    /// Tab paused the route in progress (a click in the canvas resumes it)
    bool IsRoutingPaused() const { return m_routingPaused; }

    /**
     * Altium's heads-up / status bar summary of the route in progress, e.g.
     * "Track 45:HugNPush Obstacles [Width From: Rule Preferred] Gloss: Strong", and the length
     * line "Net Length(VCC) = 1.25 mm".  Empty when not routing.
     */
    wxArrayString RoutingSummary();

private:
    void performRouting( VECTOR2D aStartPosition );
    void performDragging( int aMode = PNS::DM_ANY );
    void breakTrack();
    void restoreSelection( const PCB_SELECTION& aOriginalSelection );

    void handleCommonEvents( TOOL_EVENT& evt );
    int handleLayerSwitch( const TOOL_EVENT& aEvent, bool aForceVia );
    int handlePnSCornerModeChange( const TOOL_EVENT& aEvent );

    /// Altium-style Shift+W / Shift+V: pop up the track width or via size choices at the cursor
    void showSizePopup( bool aVias );

    /// Brief on-canvas feedback for a cycling hotkey (router mode, corner style)
    void showCycleFeedback( const wxString& aTitle, const wxArrayString& aItems, int aSelection );

    // Returns the board layer ID for the start layer of the router
    PCB_LAYER_ID getStartLayer( const PNS::ITEM* aItem );

    void switchLayerOnViaPlacement();
    void updateSizesAfterRouterEvent( int targetLayer, const VECTOR2I& aPos );

    int onLayerCommand( const TOOL_EVENT& aEvent );
    int onViaCommand( const TOOL_EVENT& aEvent );
    int onTrackViaSizeChanged( const TOOL_EVENT& aEvent );

    /// Altium's 3 while routing: step the track width through the width rule's minimum,
    /// preferred and maximum values (falling back to the board's preset widths).
    int onCycleTrackWidth( const TOOL_EVENT& aEvent );

    /// Altium's 4 while routing: step the via size through the via rule's minimum, preferred
    /// and maximum values (falling back to the board's preset via sizes).
    int onCycleViaSize( const TOOL_EVENT& aEvent );

    bool prepareInteractive( VECTOR2D aStartPosition );
    bool finishInteractive();
    void saveRouterDebugLog();

private:
    std::shared_ptr<ACTION_MENU> m_diffPairMenu;
    std::shared_ptr<ACTION_MENU> m_trackViaMenu;
    std::shared_ptr<ACTION_MENU> m_widthPopup;           // Shift+W while routing
    std::shared_ptr<ACTION_MENU> m_viaPopup;             // Shift+V while routing

    // Both of these are in board layer ID format and must be converted to PNS layer ID format
    // when used with the PNS interface.
    PCB_LAYER_ID                 m_lastTargetLayer;
    PCB_LAYER_ID                 m_originalActiveLayer;

    bool                         m_inRouterTool;         // Re-entrancy guard
    bool                         m_inRouteSelected;

    bool                         m_startWithVia;         // User pressed V before routing started
    bool                         m_routingPaused = false;  // Tab paused the route (Altium)
};

#endif
