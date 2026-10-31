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

#ifndef DIALOG_ALTIUM_ECO_H
#define DIALOG_ALTIUM_ECO_H

#include <dialog_shim.h>

#include <functional>
#include <vector>

class wxCheckBox;
class wxHyperlinkCtrl;
class wxListCtrl;
class wxStaticText;


/**
 * Altium's Engineering Change Order dialog.
 *
 * The modifications are listed under one heading per group ("Add Components(2)", "Annotate
 * Component(2)", ...), each with an Enable box, as Action | Affected Object | In/To |
 * Affected Document, and the Status columns Check | Done | Message.  Validate Changes fills
 * Check, Execute Changes applies the enabled ones and fills Done, Report Changes... saves the
 * list, Only Show Errors hides the rest.  Ticking a heading enables or disables its group.
 *
 * Use:
 * @code
 *   std::vector<DIALOG_ALTIUM_ECO::ITEM> items;
 *   items.push_back( { _( "Add Components" ), _( "Add" ), wxS( "R1" ), _( "To" ), wxS( "x.kicad_pcb" ) } );
 *   DIALOG_ALTIUM_ECO dlg( parent, std::move( items ),
 *           []( std::vector<DIALOG_ALTIUM_ECO::ITEM>& aItems, const std::vector<int>& aEnabled )
 *           {
 *               for( int ii : aEnabled )
 *                   ...apply aItems[ii]...;   // set aItems[ii].failed / .message on errors
 *           } );
 *   dlg.ShowModal();
 *   if( dlg.WasExecuted() ) ...
 * @endcode
 */
class DIALOG_ALTIUM_ECO : public DIALOG_SHIM
{
public:
    struct ITEM
    {
        wxString group;                 ///< heading, e.g. "Add Components"
        wxString action;                ///< e.g. "Add", "Modify", "Remove"
        wxString object;                ///< e.g. "R1", "D? -> D1", "GND"
        wxString preposition;           ///< "In", "To" or "From" (empty: "In")
        wxString document;              ///< e.g. "Blinky.kicad_pcb"
        bool     enabled = true;
        bool     checked = false;       ///< set by Validate Changes
        bool     done = false;          ///< set by Execute Changes
        bool     failed = false;        ///< set by a handler for an item it can't apply
        wxString message;               ///< set by a handler (shown in Message)
    };

    /// Applies (or, for validation, checks) the enabled items, given by index.
    using HANDLER = std::function<void( std::vector<ITEM>& aItems,
                                        const std::vector<int>& aEnabled )>;

    /**
     * @param aExecute applies the enabled items.  Items it doesn't mark failed count as done.
     * @param aValidate optional check of the enabled items (mark problems as failed + message);
     *                  without it every enabled item passes.
     */
    DIALOG_ALTIUM_ECO( wxWindow* aParent, std::vector<ITEM> aItems, HANDLER aExecute,
                       HANDLER aValidate = nullptr );

    /// A red warning line above the buttons, with an optional link (as Altium's "Errors
    /// occurred during compilation of the project! Click here to review them").
    void SetWarning( const wxString& aText, const wxString& aLinkText = wxEmptyString,
                     std::function<void()> aOnLink = nullptr );

    /// True once Execute Changes has run.
    bool WasExecuted() const { return m_executed; }

    const std::vector<ITEM>& GetItems() const { return m_items; }

private:
    void fill();
    void validate();
    void execute();
    void report();

    std::vector<ITEM>      m_items;
    std::vector<wxString>  m_groups;     ///< in order of first appearance
    HANDLER                m_execute;
    HANDLER                m_validate;
    std::function<void()>  m_onLink;
    wxListCtrl*            m_list;
    wxCheckBox*            m_onlyErrors;
    wxStaticText*          m_warning;
    wxHyperlinkCtrl*       m_link;
    std::vector<int>       m_rowItem;    ///< list row -> item index, or -(group index + 1)
    bool                   m_executed;
    bool                   m_filling;
};


#endif // DIALOG_ALTIUM_ECO_H
