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

#ifndef DIALOG_ANNOTATE_COMPILED_SHEETS_H
#define DIALOG_ANNOTATE_COMPILED_SHEETS_H

#include <dialog_shim.h>
#include <sch_sheet_path.h>

#include <vector>

class SCH_EDIT_FRAME;
class wxButton;
class wxGrid;


/**
 * Altium's Annotate Compiled Sheets: the sheet numbers of the compiled design, one row per
 * use of a sheet (Document | Hierarchy Path | Sheet Number).  Annotate Sheet numbers them in
 * the listed order (Move Up / Move Down change it); numbers can also be typed.  OK applies
 * them as KiCad page numbers (one undo step).
 */
class DIALOG_ANNOTATE_COMPILED_SHEETS : public DIALOG_SHIM
{
public:
    DIALOG_ANNOTATE_COMPILED_SHEETS( SCH_EDIT_FRAME* aParent );

    bool TransferDataFromWindow() override;

private:
    struct SHEET
    {
        SCH_SHEET_PATH path;
        wxString       document;
        wxString       hierarchy;
        wxString       number;
    };

    void fill();
    void move( int aBy );
    void updateButtons();

    SCH_EDIT_FRAME*    m_frame;
    std::vector<SHEET> m_sheets;
    wxGrid*            m_grid;
    wxButton*          m_up;
    wxButton*          m_down;
};


#endif // DIALOG_ANNOTATE_COMPILED_SHEETS_H
