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

#ifndef PANEL_FRAME_THEME_H
#define PANEL_FRAME_THEME_H

class wxAuiManager;

/**
 * Altium's docked-panel look, applied to every captioned panel of a frame: the panel body in
 * the chrome grey (#404040) with its contents inset by a margin, lists, trees and search
 * boxes framed by a 1 px near-black (#222222) line, and search boxes darker than the lists.
 *
 * Panels are processed once each (later calls pick up panels created since); does nothing
 * outside dark mode.
 */
void ApplyAltiumPanelFrames( wxAuiManager& aAuiMgr );

#endif // PANEL_FRAME_THEME_H
