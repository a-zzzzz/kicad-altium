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

#ifndef PCB_PROJECT_TOOL_H
#define PCB_PROJECT_TOOL_H

#include <tool/tool_action.h>
#include <tools/pcb_tool_base.h>

class PCB_EDIT_FRAME;


/**
 * Altium-style project commands of the PCB editor (docs/altium-parity/validate.md).
 */
class PCB_PROJECT_ACTIONS
{
public:
    /// Project > Validate PCB Project (runs the schematic editor's validation)
    static TOOL_ACTION validateProject;

    /// Schematic / PCB parity into the Messages panel; run by the schematic's validation
    static TOOL_ACTION checkParity;
};


class PCB_PROJECT_TOOL : public PCB_TOOL_BASE
{
public:
    PCB_PROJECT_TOOL();

    void Reset( RESET_REASON aReason ) override;

    int ValidateProject( const TOOL_EVENT& aEvent );
    int CheckParity( const TOOL_EVENT& aEvent );

    /// Menu label of Validate PCB Project, naming the project as Altium does.
    static wxString ValidateLabel( PCB_EDIT_FRAME* aFrame );

private:
    void setTransitions() override;

    wxString m_menuProject;    ///< project the menu labels were built for
};

#endif // PCB_PROJECT_TOOL_H
