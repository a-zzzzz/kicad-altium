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

#ifndef PCB_OUTPUTS_TOOL_H
#define PCB_OUTPUTS_TOOL_H

#include <tool/tool_action.h>
#include <tools/pcb_tool_base.h>


/**
 * Altium-style output documents from the PCB editor: Output Job files (project manager),
 * and the schematic's ActiveBOM and Variant Manager (variants are defined in the schematic).
 */
class PCB_OUTPUTS_ACTIONS
{
public:
    static TOOL_ACTION newOutputJob;
    static TOOL_ACTION openOutputJob;
    static TOOL_ACTION activeBom;
    static TOOL_ACTION variantManager;
};


class PCB_OUTPUTS_TOOL : public PCB_TOOL_BASE
{
public:
    PCB_OUTPUTS_TOOL();

    void Reset( RESET_REASON aReason ) override {}

    int NewOutputJob( const TOOL_EVENT& aEvent );
    int OpenOutputJob( const TOOL_EVENT& aEvent );

    /// ActiveBOM / Variant Manager: run the schematic editor's action.
    int RunInSchematic( const TOOL_EVENT& aEvent );

private:
    void setTransitions() override;
};

#endif // PCB_OUTPUTS_TOOL_H
