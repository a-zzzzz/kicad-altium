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

#ifndef SCH_MESSAGES_PANEL_H
#define SCH_MESSAGES_PANEL_H

#include <widgets/messages_panel.h>
#include <kiid.h>
#include <eda_item.h>
#include <math/box2.h>

class SCH_SHEET_PATH;

class SCH_EDIT_FRAME;

/**
 * Messages panel of the schematic editor: one row per ERC marker on every sheet, plus the
 * report output of the editor's dialogs.  Double-click opens the sheet and selects the marker.
 */
class SCH_MESSAGES_PANEL : public MESSAGES_PANEL
{
public:
    SCH_MESSAGES_PANEL( wxWindow* aParent, SCH_EDIT_FRAME* aFrame );

    /**
     * Altium's compile on save: check the project's nets (nets with a single pin) and replace
     * the previous compiler messages with the results; the Messages panel comes to the front
     * when there are any.  Returns the number of errors.
     */
    static int CompileProject( SCH_EDIT_FRAME* aFrame );

protected:
    void   collectMarkers( std::vector<MESSAGE>& aMessages ) override;
    size_t markerSignature() override;

private:
    void showMarker( const KIID& aMarker );

    /// Details row: select one object of a violation and zoom to it
    void showObject( const KIID& aItem, const KIID& aMarker );

    /// Show \a aSheet, select \a aItems and zoom to \a aBox
    void zoomTo( SCH_SHEET_PATH aSheet, EDA_ITEMS aItems, BOX2I aBox );

    SCH_EDIT_FRAME* m_schFrame;
};

#endif // SCH_MESSAGES_PANEL_H
