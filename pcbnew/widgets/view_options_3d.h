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

#ifndef VIEW_OPTIONS_3D_H
#define VIEW_OPTIONS_3D_H

#include <vector>

#include <wx/scrolwin.h>

#include <widgets/properties_form.h>

class PCB_EDIT_FRAME;
class PANEL_3D_LAYOUT;
class EDA_3D_VIEWER_SETTINGS;
class VIEW_OPTIONS_3D_SLIDER;

/**
 * Altium's View Configuration > View Options while the PCB editor shows the board in 3D:
 * General Settings (3D on/off, single layer mode, projection, 3D bodies) and 3D Settings (board
 * thickness scale, realistic / by-layer colours and a transparency per material).  It replaces
 * the 2D View Options page of the panel while in 3D (APPEARANCE_CONTROLS::Set3DMode).
 */
class VIEW_OPTIONS_3D : public wxScrolledWindow, public PROPERTIES_FORM_HOST
{
public:
    VIEW_OPTIONS_3D( wxWindow* aParent, PCB_EDIT_FRAME* aFrame );

    /// The 3D view the options act on (null while there is none)
    void SetLayoutPanel( PANEL_3D_LAYOUT* aPanel );

    /// Re-read every value (after the 3D view or the settings changed elsewhere)
    void Sync();

    // PROPERTIES_FORM_HOST: the forms here only have custom rows
    wxPropertyGrid* FormGrid() override { return nullptr; }
    void FormApply( const std::vector<std::pair<wxString, wxVariant>>& ) override {}
    void FormLayoutChanged() override;

private:
    wxWindow* addSection( const wxString& aTitle );
    EDA_3D_VIEWER_SETTINGS* cfg() const;

    /// Rebuild the 3D view with the changed settings
    void apply();

    PCB_EDIT_FRAME*                      m_frame;
    PANEL_3D_LAYOUT*                     m_layoutPanel = nullptr;
    wxBoxSizer*                          m_sizer;
    std::vector<PROPERTIES_FORM*>        m_forms;
    VIEW_OPTIONS_3D_SLIDER*              m_thickness = nullptr;
    std::vector<VIEW_OPTIONS_3D_SLIDER*> m_transparency;
    std::vector<wxWindow*>               m_swatches;
};

#endif // VIEW_OPTIONS_3D_H
