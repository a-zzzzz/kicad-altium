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

#ifndef SCH_ACTIVE_BAR_TOOL_H
#define SCH_ACTIVE_BAR_TOOL_H

#include <sch_edit_frame.h>
#include <tools/sch_tool_base.h>


/**
 * Commands behind the Altium-style Active Bar in the schematic editor that KiCad has no
 * equivalent for: the selection filter drop-down, area/line selection modes, toggle selection
 * and the power port presets.
 */
class SCH_ACTIVE_BAR_TOOL : public SCH_TOOL_BASE<SCH_EDIT_FRAME>
{
public:
    SCH_ACTIVE_BAR_TOOL();

    int SelectionFilterMenu( const TOOL_EVENT& aEvent );

    /// Inside Area / Outside Area / Touching Rectangle (two corners), Touching Line (two points).
    int SelectArea( const TOOL_EVENT& aEvent );

    /// Each click flips the selection state of the object under the cursor.
    int ToggleSelection( const TOOL_EVENT& aEvent );

    /// Places a specific symbol from the "power" library (GND, VCC, +5V, ...).
    int PlacePowerPort( const TOOL_EVENT& aEvent );

private:
    void setTransitions() override;
};

#endif // SCH_ACTIVE_BAR_TOOL_H
