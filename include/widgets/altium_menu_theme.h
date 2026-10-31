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

#ifndef ALTIUM_MENU_THEME_H
#define ALTIUM_MENU_THEME_H

/**
 * Altium-coloured popup menus on Windows.
 *
 * Windows draws popup menus (menu bar drop-downs, submenus, context menus) with its own dark
 * theme, ignoring the application's colours.  This paints every popup menu of the GUI thread
 * itself, in Altium's greys: while a menu is open its items are owner-drawn (text, shortcut,
 * icon, check mark, state), and when it closes they are restored, so wxWidgets keeps updating
 * labels and states on ordinary menu items.
 */
namespace ALTIUM_MENU_THEME
{
/// Install once per process (later calls, also from other modules, do nothing).
void Install();
}

#endif // ALTIUM_MENU_THEME_H
