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

#ifndef SCH_PROJECT_TOOL_H
#define SCH_PROJECT_TOOL_H

#include <sch_edit_frame.h>
#include <tool/tool_action.h>
#include <tools/sch_tool_base.h>


/**
 * Altium-style project commands of the schematic editor (docs/altium-parity/validate.md).
 */
class SCH_PROJECT_ACTIONS
{
public:
    /// Project > Validate PCB Project: ERC plus schematic / PCB parity, into the Messages panel
    static TOOL_ACTION validateProject;

    /// Edit > Smart Paste (Shift+Ctrl+V): KiCad's Paste Special.  A schematic-only action, so
    /// the shortcut doesn't take Shift+Ctrl+V (Distribute Vertically) from the PCB editor.
    static TOOL_ACTION smartPaste;
};


class SCH_PROJECT_TOOL : public SCH_TOOL_BASE<SCH_EDIT_FRAME>
{
public:
    SCH_PROJECT_TOOL();

    void Reset( RESET_REASON aReason ) override;

    int ValidateProject( const TOOL_EVENT& aEvent );
    int SmartPaste( const TOOL_EVENT& aEvent );

    /// Menu label of Validate PCB Project, naming the project as Altium does.
    static wxString ValidateLabel( SCH_EDIT_FRAME* aFrame );

    /// Menu label of Design > Update PCB Document, naming the board as Altium does.
    static wxString UpdatePcbLabel( SCH_EDIT_FRAME* aFrame );

private:
    void setTransitions() override;

    /// Run ERC without its dialog; returns the number of unannotated symbols.
    int runErc();

    wxString m_menuProject;    ///< project the menu labels were built for
};

#endif // SCH_PROJECT_TOOL_H
