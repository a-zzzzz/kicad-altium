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

#ifndef DIALOG_BOARD_LEVEL_ANNOTATE_H
#define DIALOG_BOARD_LEVEL_ANNOTATE_H

#include <dialog_shim.h>
#include <sch_sheet_path.h>

#include <vector>

class SCH_EDIT_FRAME;
class SCH_SYMBOL;
class wxGrid;
class wxGridEvent;


/**
 * Altium's Board Level Annotate (Tools > Annotation, Ctrl+L): the PCB designators of the
 * components, built from their schematic designators with a naming scheme.
 *
 * In Altium a sheet used several times (a multi-channel design) keeps one logical designator
 * per component and each channel gets its own PCB designator, e.g. R1_CH2 with the scheme
 * $Component_$RoomName.  KiCad keeps a designator per sheet use, which is what goes to the
 * PCB: this dialog proposes those per-use designators from the first use's ones with the
 * scheme.  Components of sheets used once keep their designator, as in Altium.
 *
 * Left, the Filter Options (sheets, channel names, enabled, scope); right, the Proposed Change
 * List; Annotate fills it (undesignated, all or selected rows), Accept Changes applies it as
 * an Engineering Change Order.  Update the PCB afterwards to rename its footprints.
 */
class DIALOG_BOARD_LEVEL_ANNOTATE : public DIALOG_SHIM
{
public:
    DIALOG_BOARD_LEVEL_ANNOTATE( SCH_EDIT_FRAME* aParent );

private:
    struct CHANNEL
    {
        SCH_SHEET_PATH path;
        wxString       file;
        wxString       name;            ///< channel name: the sheet's name (root: the project)
        bool           enabled = true;
        int            scope = 0;       ///< 0 All, 1 Ignore Selected Parts, 2 Only Selected
        int            index = 1;       ///< 1-based use of its sheet file
        bool           multi = false;   ///< the sheet file is used more than once
    };

    struct ROW
    {
        SCH_SYMBOL*    symbol = nullptr;
        std::vector<SCH_SYMBOL*> units;  ///< every placed part of the package (Altium: one PCB component)
        SCH_SHEET_PATH sheet;
        int            channel = 0;
        wxString       base;            ///< the first use's designator (Altium's logical one)
        wxString       prefix;
        wxString       localIndex;
        wxString       current;         ///< this use's designator
        wxString       proposed;
        bool           apply = true;
        bool           selected = false;
    };

    enum ANNOTATE_MODE { ANNOTATE_UNDESIGNATED = 0, ANNOTATE_ALL_ROWS, ANNOTATE_SELECTED };

    void     collect();
    void     fillChannels();
    void     fillRows();
    wxString roomName( const CHANNEL& aChannel ) const;
    wxString designatorFor( const ROW& aRow ) const;
    bool     inScope( const ROW& aRow ) const;
    void     annotate( ANNOTATE_MODE aMode );
    void     showOptions();
    void     acceptChanges();

    SCH_EDIT_FRAME*      m_frame;
    std::vector<CHANNEL> m_channels;
    std::vector<ROW>     m_rows;
    wxGrid*              m_channelsGrid;
    wxGrid*              m_rowsGrid;
    bool                 m_filling;
};


#endif // DIALOG_BOARD_LEVEL_ANNOTATE_H
