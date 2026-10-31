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

#ifndef PCB_ALTIUM_CONTEXT_MENU_H
#define PCB_ALTIUM_CONTEXT_MENU_H

class ACTION_MENU;
class PCB_BASE_FRAME;
class SELECTION;
class TOOL_INTERACTIVE;

/**
 * Altium's canvas right-click menu for the PCB editor (and, minus the board-only parts, the
 * footprint editor): Find Similar Objects, Clear Filter, Place / Component / References / Room /
 * Polygon / Net / Pad Actions / Align submenus, Cross Probe, clipboard, rule inspection,
 * Preferences and Properties.  Entries that don't apply to the selection are greyed out, as
 * Altium does.  The caller owns the returned menu.
 */
ACTION_MENU* BuildAltiumPcbContextMenu( TOOL_INTERACTIVE* aTool, PCB_BASE_FRAME* aFrame,
                                        const SELECTION& aSelection );

#endif // PCB_ALTIUM_CONTEXT_MENU_H
