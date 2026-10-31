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

#ifndef SCH_OUTPUTS_TOOL_H
#define SCH_OUTPUTS_TOOL_H

#include <sch_edit_frame.h>
#include <tool/tool_action.h>
#include <tools/sch_tool_base.h>

#include <wx/dialog.h>
#include <wx/weakref.h>



/**
 * Altium-style output documents reachable from the schematic editor: Output Job files,
 * the ActiveBOM view and the Variant Manager.  Kept out of SCH_ACTIONS so the feature lives
 * in its own files (docs/altium-parity/outputs.md).
 */
class SCH_OUTPUTS_ACTIONS
{
public:
    static TOOL_ACTION newOutputJob;     ///< File > New > Output Job File
    static TOOL_ACTION openOutputJob;    ///< Project > Output Jobs...
    static TOOL_ACTION activeBom;        ///< Reports > Bill of Materials (ActiveBOM view)
    static TOOL_ACTION variantManager;   ///< Project > Variants...
};


class SCH_OUTPUTS_TOOL : public SCH_TOOL_BASE<SCH_EDIT_FRAME>
{
public:
    SCH_OUTPUTS_TOOL();

    int NewOutputJob( const TOOL_EVENT& aEvent );
    int OpenOutputJob( const TOOL_EVENT& aEvent );
    int ActiveBom( const TOOL_EVENT& aEvent );
    int VariantManager( const TOOL_EVENT& aEvent );

private:
    void setTransitions() override;

    wxWeakRef<wxDialog> m_activeBom;   ///< the open ActiveBOM window, if any
};

#endif // SCH_OUTPUTS_TOOL_H
