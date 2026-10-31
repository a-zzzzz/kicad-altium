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

#ifndef DIALOG_NUMBER_SCHEMATIC_SHEETS_H
#define DIALOG_NUMBER_SCHEMATIC_SHEETS_H

#include <dialog_shim.h>
#include <sch_sheet_path.h>

#include <vector>

class SCH_EDIT_FRAME;
class wxButton;
class wxCheckBox;
class wxGrid;

/**
 * Altium's Tools > Annotation > Number Schematic Sheets: the project's sheets with their sheet
 * number, document number and sheet total; automatic or manual numbering in display order or
 * by hierarchy (depth or breadth first), increasing or decreasing.
 *
 * The sheet number is KiCad's page number.  The document number is kept in each sheet's title
 * block (Comment 9, shown in title blocks as ${COMMENT9}).
 */
class DIALOG_NUMBER_SCHEMATIC_SHEETS : public DIALOG_SHIM
{
public:
    DIALOG_NUMBER_SCHEMATIC_SHEETS( SCH_EDIT_FRAME* aParent );

    bool TransferDataFromWindow() override;

private:
    enum SCHEME { DISPLAY_ORDER = 0, DEPTH_FIRST, BREADTH_FIRST };

    struct SHEET
    {
        SCH_SHEET_PATH path;
        wxString       document;
        wxString       number;
        wxString       docNumber;
    };

    void fill();
    void move( int aBy );
    void updateControls();

    /// The rows in the order a scheme numbers them
    std::vector<size_t> order( SCHEME aScheme ) const;

    void autoNumber( bool aDocument );
    void showSchemeMenu( wxButton* aAnchor, bool aDocument );

    SCH_EDIT_FRAME*    m_frame;
    std::vector<SHEET> m_sheets;
    wxGrid*            m_grid;
    wxCheckBox*        m_automatic;
    wxButton*          m_autoSheet;
    wxButton*          m_autoSheetMore;
    wxButton*          m_up;
    wxButton*          m_down;

    SCHEME             m_sheetScheme = DISPLAY_ORDER;
    bool               m_sheetIncreasing = true;
    SCHEME             m_docScheme = DISPLAY_ORDER;
    bool               m_docIncreasing = true;
};

#endif // DIALOG_NUMBER_SCHEMATIC_SHEETS_H
