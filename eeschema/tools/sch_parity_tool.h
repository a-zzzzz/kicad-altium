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

#ifndef SCH_PARITY_TOOL_H
#define SCH_PARITY_TOOL_H

#include <array>
#include <map>
#include <vector>

#include <kiid.h>
#include <sch_edit_frame.h>
#include <tools/sch_tool_base.h>

class wxCommandEvent;

/**
 * Altium-parity commands of the schematic editor that work on the selection as a whole:
 * Find Similar Objects (find-similar.md), selection memory (selection-memory.md) and the
 * SCH List panel (list-panel.md), including keeping that panel in step with the selection
 * and the sheet.
 */
class SCH_PARITY_TOOL : public SCH_TOOL_BASE<SCH_EDIT_FRAME>
{
public:
    SCH_PARITY_TOOL();
    ~SCH_PARITY_TOOL() override;

    bool Init() override;
    void Reset( RESET_REASON aReason ) override;

    int FindSimilar( const TOOL_EVENT& aEvent );
    int SelectionMemory( const TOOL_EVENT& aEvent );
    int ToggleSchList( const TOOL_EVENT& aEvent );

    /// Selection changed, an edit was made or undone: refresh the SCH List.
    int SelectionChanged( const TOOL_EVENT& aEvent );
    int ModelChanged( const TOOL_EVENT& aEvent );

private:
    void setTransitions() override;

    void onSchematicChanged( wxCommandEvent& aEvent );

    using SLOTS = std::array<std::vector<KIID>, 9>;    ///< slots 1..8; 0 unused

    /// Memories per sheet instance (sheet path), like Altium's per-document memories.
    std::map<wxString, SLOTS> m_selectionMemory;
    SCH_EDIT_FRAME*           m_boundFrame;
};

#endif // SCH_PARITY_TOOL_H
