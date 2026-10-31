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

#ifndef PCB_COMPONENTS_PANEL_H
#define PCB_COMPONENTS_PANEL_H

#include <widgets/components_panel.h>

class FOOTPRINT_PREVIEW_WIDGET;
class PCB_EDIT_FRAME;


/// Components panel of the PCB editor: footprint libraries and a footprint preview.
class PCB_COMPONENTS_PANEL : public COMPONENTS_PANEL
{
public:
    PCB_COMPONENTS_PANEL( wxWindow* aParent, PCB_EDIT_FRAME* aFrame );

protected:
    void createPreviews( wxWindow* aParent, wxBoxSizer* aSizer ) override;
    std::vector<wxString> libraryNames() override;
    wxObjectDataPtr<LIB_TREE_MODEL_ADAPTER> createAdapter( const wxString& aLibrary ) override;
    void showPreview( const LIB_ID& aLibId, int aUnit ) override;
    void place( const LIB_ID& aLibId, int aUnit, bool aRepeat ) override;
    void manageLibraries() override;
    bool canReplace() override;
    void replace( const LIB_ID& aLibId, int aUnit ) override;

private:
    PCB_EDIT_FRAME*           m_pcbFrame;
    FOOTPRINT_PREVIEW_WIDGET* m_footprintPreview;
};

#endif // PCB_COMPONENTS_PANEL_H
