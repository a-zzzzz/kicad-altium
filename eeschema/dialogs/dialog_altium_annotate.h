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

#ifndef DIALOG_ALTIUM_ANNOTATE_H
#define DIALOG_ALTIUM_ANNOTATE_H

#include <dialog_shim.h>
#include <sch_sheet_path.h>
#include <math/vector2d.h>

#include <vector>

class SCH_EDIT_FRAME;
class SCH_SYMBOL;
class wxButton;
class wxChoice;
class wxGrid;
class wxGridEvent;
class wxStaticText;
class wxTextCtrl;


/**
 * Altium's Annotate dialog (Tools > Annotation > Annotate Schematics).
 *
 * Left, the Schematic Annotation Configuration: order of processing (with a picture of it),
 * matching options for multi-part packages, which location of a part orders it, sub-part
 * replacement, and the sheets to annotate (scope, order, start index, suffix).  Right, the
 * Proposed Change List: Update Changes List fills the proposed designators, Reset All / Reset
 * Duplicates clear them, and Accept Changes applies them after listing them as an Engineering
 * Change Order.  Nothing changes on the sheets until the changes are accepted (one undo step).
 *
 * A sheet used several times is listed once per use: each use has its own designators.
 */
class DIALOG_ALTIUM_ANNOTATE : public DIALOG_SHIM
{
public:
    DIALOG_ALTIUM_ANNOTATE( SCH_EDIT_FRAME* aParent );

    enum ORDER { UP_THEN_ACROSS = 0, ACROSS_THEN_UP, DOWN_THEN_ACROSS, ACROSS_THEN_DOWN };

private:
    /// One part of a placed component (one unit of a multi-part one) on one sheet instance.
    struct PART
    {
        SCH_SYMBOL*    symbol = nullptr;
        SCH_SHEET_PATH sheet;
        int            sheetRow = 0;        ///< index into m_sheets
        wxString       prefix;              ///< designator letters, e.g. "R"
        wxString       current;             ///< current designator, e.g. "R1" or "R?"
        int            currentUnit = 1;
        wxString       proposed;
        int            proposedUnit = 1;
        int            unitCount = 1;
        bool           lockDesignator = false;
        bool           lockSub = false;
        bool           selected = false;    ///< selected on the sheets when the dialog opened
        VECTOR2I       partPos;
        VECTOR2I       designatorPos;
        wxString       libId;
        std::vector<wxString> paramValues;  ///< by m_params index
    };

    struct SHEET
    {
        SCH_SHEET_PATH path;
        wxString       name;                ///< file name, or the instance path when compiled
        bool           enabled = true;
        int            scope = 0;           ///< 0 All, 1 Ignore Selected Parts, 2 Only Selected
        int            order = 0;
        bool           useStartIndex = false;
        long           startIndex = 1;
        wxString       suffix;
    };

    struct PARAM
    {
        wxString name;                      ///< field name ("Comment" is the Value field)
        bool     use = false;
        bool     strictly = true;
    };

    void collect();
    void fillSheetsGrid();
    void fillParamsGrid();
    void fillChangesGrid();
    void updateSummary();

    bool processes( const PART& aPart ) const;
    void updateChangesList();
    void updateProposed();
    void resetProposed( bool aDuplicatesOnly );
    void acceptChanges();

    void onSheetsClick( wxGridEvent& aEvent );
    void onSheetsChanged( wxGridEvent& aEvent );
    void onParamsClick( wxGridEvent& aEvent );
    void onChangesClick( wxGridEvent& aEvent );

    SCH_EDIT_FRAME*    m_frame;
    std::vector<PART>  m_parts;
    std::vector<SHEET> m_sheets;
    std::vector<PARAM> m_params;

    wxChoice*          m_order;
    wxWindow*          m_orderPicture;
    wxChoice*          m_completePackages;
    wxChoice*          m_processLocation;
    wxChoice*          m_replaceSubParts;
    wxGrid*            m_paramsGrid;
    wxGrid*            m_sheetsGrid;
    wxGrid*            m_changesGrid;
    wxStaticText*      m_summary;
    wxButton*          m_acceptButton;
    bool               m_filling;
};


#endif // DIALOG_ALTIUM_ANNOTATE_H
