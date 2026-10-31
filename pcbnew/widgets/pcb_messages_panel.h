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

#ifndef PCB_MESSAGES_PANEL_H
#define PCB_MESSAGES_PANEL_H

#include <widgets/messages_panel.h>
#include <kiid.h>

class PCB_EDIT_FRAME;

/**
 * Messages panel of the PCB editor: one row per DRC / parity marker on the board, plus the
 * report output of the editor's dialogs.  Double-click zooms to and selects the violation.
 */
class PCB_MESSAGES_PANEL : public MESSAGES_PANEL
{
public:
    PCB_MESSAGES_PANEL( wxWindow* aParent, PCB_EDIT_FRAME* aFrame );

protected:
    void   collectMarkers( std::vector<MESSAGE>& aMessages ) override;
    size_t markerSignature() override;

private:
    void showMarker( const KIID& aMarker );

    /// Details row: select one object of a violation and zoom to it
    void showObject( const KIID& aItem );

    PCB_EDIT_FRAME* m_pcbFrame;
};

#endif // PCB_MESSAGES_PANEL_H
