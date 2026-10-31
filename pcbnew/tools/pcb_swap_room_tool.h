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

#ifndef PCB_SWAP_ROOM_TOOL_H
#define PCB_SWAP_ROOM_TOOL_H

#include <set>
#include <vector>

#include <tool/tool_action.h>
#include <tools/pcb_tool_base.h>

class FOOTPRINT;
class PAD;
class ZONE;
class ZONE_SETTINGS;
class SHAPE_LINE_CHAIN;


/**
 * Actions for the Altium-style Tools > Pin/Part Swapping and Design > Rooms submenus.
 *
 * Kept out of PCB_ACTIONS so this feature lives in its own files.
 */
class PCB_SWAP_ROOM_ACTIONS
{
public:
    // Pin/Part swapping
    static TOOL_ACTION interactivePinSwap;
    static TOOL_ACTION interactivePartSwap;

    // Rooms (placement rule areas)
    static TOOL_ACTION placeRectangularRoom;
    static TOOL_ACTION placePolygonalRoom;
    static TOOL_ACTION roomFromSelectionRect;
    static TOOL_ACTION roomFromSelectionOrthogonal;
    static TOOL_ACTION roomFromSelectionHull;
    static TOOL_ACTION arrangeWithinRoom;
    static TOOL_ACTION moveRoom;
};


/**
 * Pin/part swapping and rooms, mapped onto KiCad's pad/gate net swapping, placement rule
 * areas and the multichannel tool.
 */
class PCB_SWAP_ROOM_TOOL : public PCB_TOOL_BASE
{
public:
    PCB_SWAP_ROOM_TOOL();

    bool Init() override;
    void Reset( RESET_REASON aReason ) override {}

    /// Click pin A, then pin B of the same component: their nets swap.  Repeats until Esc.
    int InteractivePinSwap( const TOOL_EVENT& aEvent );

    /// Click a pin of gate A, then a pin of gate B of the same component: the gates' nets swap.
    int InteractivePartSwap( const TOOL_EVENT& aEvent );

    /// Prompt for the room's settings, then click two corners / click polygon vertices.
    int PlaceRoom( const TOOL_EVENT& aEvent );

    /// Build a room around the selected components.
    int RoomFromSelection( const TOOL_EVENT& aEvent );

    /// Pack the room's components inside it.
    int ArrangeWithinRoom( const TOOL_EVENT& aEvent );

    /// Select a room and the components inside it, and start moving them.
    int MoveRoom( const TOOL_EVENT& aEvent );

private:
    void setTransitions() override;

    /// Shared click-click loop for pin and part swapping.
    int swapLoop( const TOOL_EVENT& aEvent, bool aGateMode );

    /// The copper pad under aPos, preferring pads on the active layer.
    PAD* padAt( const VECTOR2I& aPos ) const;

    /// Index of the footprint unit (gate) holding aPad, or -1.
    static int unitOfPad( const PAD* aPad );

    /// The single placement rule area in the selection, or nullptr.
    ZONE* selectedRoom() const;

    /// The smallest placement rule area under aPos, or nullptr.
    ZONE* roomAt( const VECTOR2I& aPos ) const;

    /// Selected room, or let the user click one.  Returns nullptr on cancel.
    ZONE* pickRoom( const TOOL_EVENT& aEvent, const wxString& aPrompt );

    /// Footprints the room's placement source names (its sheet, class or group).
    std::set<FOOTPRINT*> roomMembers( ZONE* aRoom );

    /// Room preset: a placement rule area with no keepouts.  Fills the source from aMembers.
    void initRoomSettings( ZONE_SETTINGS& aSettings, const std::vector<FOOTPRINT*>& aMembers );

    /// Show the rule area dialog for the room.  False when cancelled.
    bool editRoomSettings( ZONE_SETTINGS& aSettings );

    /// Create and commit a room with the given outline.
    ZONE* commitRoom( const ZONE_SETTINGS& aSettings, const SHAPE_LINE_CHAIN& aOutline );

    /// A room name not used by any other zone.
    wxString uniqueRoomName( const wxString& aBase ) const;
};

#endif // PCB_SWAP_ROOM_TOOL_H
