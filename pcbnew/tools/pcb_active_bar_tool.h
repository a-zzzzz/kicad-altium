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

#ifndef PCB_ACTIVE_BAR_TOOL_H
#define PCB_ACTIVE_BAR_TOOL_H

#include <tools/pcb_tool_base.h>

class BOARD_ITEM;


/**
 * Commands behind the Altium-style Active Bar that KiCad has no equivalent for: the selection
 * filter drop-down, the area/line/type selection modes, toggle selection and align to grid.
 */
class PCB_ACTIVE_BAR_TOOL : public PCB_TOOL_BASE
{
public:
    PCB_ACTIVE_BAR_TOOL();

    /// Drop-down of object kinds that may be selected (mirrors the selection filter panel).
    int SelectionFilterMenu( const TOOL_EVENT& aEvent );

    /// Inside Area / Outside Area / Touching Rectangle: click two corners.
    /// Touching Line: click two points.
    int SelectArea( const TOOL_EVENT& aEvent );

    /// All on Layer / Free Objects / All Locked / Off Grid Pads.
    int SelectByKind( const TOOL_EVENT& aEvent );

    /// Each click flips the selection state of the object under the cursor.
    int ToggleSelection( const TOOL_EVENT& aEvent );

    /// Moves the selected objects so their reference points sit on the grid.
    int AlignToGrid( const TOOL_EVENT& aEvent );

    /// Increase / Decrease Horizontal / Vertical Spacing: one grid step per gap.
    int AdjustSpacing( const TOOL_EVENT& aEvent );

private:
    void setTransitions() override;

    /// Replaces the selection with the selectable items among aItems.
    void selectItems( const std::vector<BOARD_ITEM*>& aItems );

    /// Removes aItems from the selection (the DeSelect variants).
    void deselectItems( const std::vector<BOARD_ITEM*>& aItems );

    /// Every top-level item on the board, plus pads when aWithPads.
    std::vector<BOARD_ITEM*> allItems( bool aWithPads ) const;
};

#endif // PCB_ACTIVE_BAR_TOOL_H
