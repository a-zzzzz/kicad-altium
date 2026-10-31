/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#pragma once

#include <map>
#include <set>
#include <vector>

#include <dialog_shim.h>
#include <reporter.h>

class NETLIST;
class PCB_EDIT_FRAME;
class ECO_LIST;
class ECO_HEADER;
class wxButton;
class wxCheckBox;


/**
 * Altium-style "Engineering Change Order" on top of KiCad's Update PCB from Schematic.
 *
 * The changes found by a dry run of BOARD_NETLIST_UPDATER are listed as a "Modifications"
 * table grouped by change type (Add Components, Add Nets, ...), with a "Status" area
 * (Check | Done | Message).  Validate Changes repeats the dry run with the ticked changes and
 * fills Check; Execute Changes applies them and fills Done.
 */
class DIALOG_UPDATE_PCB : public DIALOG_SHIM
{
public:
    DIALOG_UPDATE_PCB( PCB_EDIT_FRAME* aParent, NETLIST* aNetlist );
    ~DIALOG_UPDATE_PCB();

    /// One change of the list (a row under a group).
    struct ECO_ROW
    {
        int      group = 0;
        wxString key;          ///< normalised updater message (identifies the change)
        wxString text;         ///< updater message as shown in reports
        wxString action;       ///< Add / Remove / Change ...
        wxString object;       ///< Affected Object
        wxString ref;          ///< component reference for per-row exclusion, if any
        bool     enabled = true;
        bool     hidden = false; ///< bookkeeping KiCad does along with a listed change (not shown)
        int      check = 0;    ///< STATUS_*
        int      done = 0;     ///< STATUS_*
        wxString message;
    };

    enum STATUS
    {
        STATUS_NONE = 0,
        STATUS_OK,
        STATUS_WARNING,
        STATUS_ERROR
    };

    /// How the Enable box of a group is honoured
    enum ENABLE_MODE
    {
        ENABLE_FIXED,        ///< always applied (no KiCad switch for it)
        ENABLE_PER_ROW,      ///< each row can be left out
        ENABLE_GROUP         ///< the whole group is switched by a KiCad option
    };

    struct ECO_GROUP
    {
        wxString         title;
        ENABLE_MODE      mode = ENABLE_FIXED;
        wxCheckBox*      option = nullptr;   ///< the KiCad option behind an ENABLE_GROUP group
        std::vector<int> rows;
        bool             expanded = true;
    };

    const std::vector<ECO_GROUP>& Groups() const { return m_groups; }
    const std::vector<ECO_ROW>&   Rows() const { return m_rows; }
    const wxString&               DocumentName() const { return m_document; }

    /// Toggle a row (or, with aRow < 0, every row of aGroup) from the list.
    void ToggleEnable( int aGroup, int aRow );
    void SetGroupExpanded( int aGroup, bool aExpanded );
    void SetAllEnabled( bool aEnable );

private:
    bool TransferDataToWindow() override;

    /// Rebuild the list from a dry run with every change type switched on.
    void populate();

    /// Altium's project compile before an ECO: posts its errors to Messages; returns how many.
    int compileProject();

    /// Run the updater.  aAll: list every possible change (ignore the Enable boxes).
    void runUpdater( bool aDryRun, bool aAll, std::vector<std::pair<wxString, SEVERITY>>& aLines );

    /// Map a run's messages onto the rows' Check (aDone false) or Done (aDone true) column.
    void applyStatus( const std::vector<std::pair<wxString, SEVERITY>>& aLines, bool aDone );

    int  groupFor( const wxString& aKey, SEVERITY aSeverity, wxString& aAction, wxString& aObject,
                   wxString& aRef );
    int  addRow( const wxString& aText, SEVERITY aSeverity, bool aAfterExecute = false );
    void syncGroupOptions();
    void refreshList();

    void onValidate( wxCommandEvent& aEvent );
    void onExecute( wxCommandEvent& aEvent );
    void onReport( wxCommandEvent& aEvent );
    void onOnlyErrors( wxCommandEvent& aEvent );
    void onOptions( wxCommandEvent& aEvent );

    wxString reportText() const;

private:
    PCB_EDIT_FRAME* m_frame;
    NETLIST*        m_netlist;
    bool            m_initialized;
    bool            m_runDragCommand;
    bool            m_executed;
    wxString        m_document;

    std::vector<ECO_GROUP> m_groups;
    std::vector<ECO_ROW>   m_rows;
    std::map<wxString, int> m_rowByKey;
    std::set<wxString>      m_addedRefs;    ///< components the ECO adds

    ECO_HEADER* m_header;
    ECO_LIST*   m_list;
    wxButton*   m_validateBtn;
    wxButton*   m_executeBtn;
    wxButton*   m_reportBtn;
    wxButton*   m_optionsBtn;
    wxCheckBox* m_onlyErrors;
    wxButton*   m_closeBtn;

    // KiCad's update options.  They are not shown as such (the change groups and the Options
    // menu drive them) but keep their values between sessions as dialog control state.
    wxWindow*   m_optionHolder;
    wxCheckBox* m_cbRelinkFootprints;
    wxCheckBox* m_cbTransferGroups;
    wxCheckBox* m_cbApplyDesignBlockLayouts;
    wxCheckBox* m_cbUpdateFootprints;
    wxCheckBox* m_cbDeleteExtraFootprints;
    wxCheckBox* m_cbOverrideLocks;
    wxCheckBox* m_cbUpdateFields;
    wxCheckBox* m_cbRemoveExtraFields;
};
