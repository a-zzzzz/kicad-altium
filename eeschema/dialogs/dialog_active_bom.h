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

#ifndef DIALOG_ACTIVE_BOM_H
#define DIALOG_ACTIVE_BOM_H

#include <dialog_shim.h>
#include <settings/bom_settings.h>

#include <vector>

class SCH_EDIT_FRAME;
class wxChoice;
class wxGrid;
class wxGridEvent;
class wxSearchCtrl;
class wxStaticText;


/**
 * Altium ActiveBOM-like view of the schematic's bill of materials.
 *
 * Line items are grouped with the KiCad BOM preset chosen at the top (the project's current
 * BOM settings or a built-in preset); a Status column flags missing footprint / value /
 * manufacturer part data.  Fields are edited in KiCad's Symbol Fields Table and exported with
 * KiCad's BOM export.  Modeless; rebuilt whenever it is activated.
 */
class DIALOG_ACTIVE_BOM : public DIALOG_SHIM
{
public:
    DIALOG_ACTIVE_BOM( SCH_EDIT_FRAME* aParent );

    /// Re-read the schematic and refill the grid.
    void Rebuild();

private:
    struct LINE
    {
        wxString              name;          ///< value / comment
        wxString              description;
        std::vector<wxString> designators;
        wxString              footprint;
        wxString              manufacturer;
        wxString              mpn;
        wxString              supplier;
        wxString              spn;
        int                   quantity = 0;
        bool                  notFitted = false;
        int                   severity = 0;  ///< 0 OK, 1 info, 2 warning, 3 error
        wxString              status;
        std::vector<wxString> symbolPaths;   ///< full KIID paths, for cross-probing
    };

    BOM_PRESET currentPreset() const;
    wxString   currentVariant() const;
    void       fillVariants();
    void       fillGrid();
    void       crossProbe( int aRow );

    void onGridSelect( wxGridEvent& aEvent );
    void onActivate( wxActivateEvent& aEvent );

private:
    SCH_EDIT_FRAME*         m_frame;
    wxChoice*               m_variantChoice;
    wxChoice*               m_presetChoice;
    wxSearchCtrl*           m_filter;
    wxGrid*                 m_grid;
    wxStaticText*           m_summary;
    std::vector<BOM_PRESET> m_presets;
    wxArrayString           m_variantNames;   ///< parallel to m_variantChoice ("" = none)
    std::vector<LINE>       m_lines;
    std::vector<int>        m_shown;          ///< grid row -> m_lines index
    bool                    m_filling;        ///< no cross-probing while the grid is refilled
};

#endif // DIALOG_ACTIVE_BOM_H
