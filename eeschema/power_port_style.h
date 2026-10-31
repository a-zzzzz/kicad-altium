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

#ifndef POWER_PORT_STYLE_H
#define POWER_PORT_STYLE_H

#include <vector>
#include <wx/string.h>

class SCH_SHEET_PATH;
class SCH_SYMBOL;
class SCHEMATIC;

/**
 * Altium's power port styles (Circle, Arrow, Bar, Wave, Power Ground, Signal Ground, Earth and
 * the GOST variants).  KiCad draws a power port with its library symbol, so a style is a
 * generated power symbol (the same graphics the Altium importer draws), linked as
 * "Altium_Power:<style>".  The net still comes from the Value field.
 */
namespace POWER_PORT_STYLE
{
/// Library nickname of the generated style symbols (not a real library: ERC doesn't look it up)
extern const wxString LIB_NICKNAME;

/// The style names, in Altium's order
const std::vector<wxString>& Names();

/// The style a power port shows (an estimate for KiCad's own power symbols), or -1
int Get( const SCH_SYMBOL* aSymbol );

/// Gives a power port another style, keeping its net name, place and the direction it points
void Apply( SCH_SYMBOL* aSymbol, int aStyle );

/// A new power port as Altium places it: net  aNet in style  aStyle, pointing up for a
/// supply (Bar, Arrow, Circle, Wave) and down for a ground
SCH_SYMBOL* Create( const wxString& aNet, int aStyle, const SCH_SHEET_PATH* aSheet,
                    SCHEMATIC* aSchematic );
}

#endif // POWER_PORT_STYLE_H
