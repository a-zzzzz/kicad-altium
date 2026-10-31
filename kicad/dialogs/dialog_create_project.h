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

#ifndef DIALOG_CREATE_PROJECT_H
#define DIALOG_CREATE_PROJECT_H

#include <dialog_shim.h>

class wxListBox;
class wxTextCtrl;
class wxTreeCtrl;


/**
 * Altium's Create Project dialog (File > New > Project...): locations at the left (only
 * Local Projects applies), the project type in the middle (PCB > <Empty>, or a KiCad
 * template) and the project's name and folder at the right.  The project is created in
 * <Folder>\<Name>\<Name>.kicad_pro, as Altium does.
 */
class DIALOG_CREATE_PROJECT : public DIALOG_SHIM
{
public:
    DIALOG_CREATE_PROJECT( wxWindow* aParent, const wxString& aDefaultFolder );

    wxString GetProjectName() const;
    wxString GetFolder() const;

    /// The new project's file: Folder/Name/Name.kicad_pro.
    wxString GetProjectFile() const;

    /// True when a KiCad project template was picked rather than an empty project.
    bool UseTemplate() const { return m_useTemplate; }

    bool TransferDataFromWindow() override;

private:
    wxTreeCtrl* m_types;
    wxTextCtrl* m_name;
    wxTextCtrl* m_folder;
    bool        m_useTemplate;
};

#endif // DIALOG_CREATE_PROJECT_H
