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

#ifndef WINDOW_MENU_H
#define WINDOW_MENU_H

#include <wx/menu.h>
#include <vector>

class wxFrame;
class wxTopLevelWindow;

/**
 * Altium-style Window menu: arrange the open KiCad editor windows (tile horizontally,
 * tile vertically, cascade), switch between them, and close the current one.
 *
 * The list of windows is rebuilt every time the menu opens, so it always reflects the
 * editors that are currently open in this process.
 */
class WINDOW_MENU : public wxMenu
{
public:
    WINDOW_MENU( wxFrame* aOwner );
    ~WINDOW_MENU() override;

    enum class ARRANGE
    {
        GRID,         ///< Altium's Tile: rows and columns
        HORIZONTAL,   ///< full-width strips stacked top to bottom
        VERTICAL,     ///< full-height columns side by side
        CASCADE
    };

    /// Arrange the open documents on \a aOwner's display, leaving \a aOwner in front.
    static void Arrange( wxFrame* aOwner, ARRANGE aMode );

private:
    void rebuild();
    void onMenuOpen( wxMenuEvent& aEvent );
    void onCommand( wxCommandEvent& aEvent );

    /// Visible top-level KiCad frames, in creation order.
    static std::vector<wxTopLevelWindow*> openWindows();

    wxFrame*                       m_owner;
    std::vector<wxTopLevelWindow*> m_listed;   ///< windows behind the list items
    int                            m_firstId;  ///< first of a reserved block of item IDs
};

#endif // WINDOW_MENU_H
