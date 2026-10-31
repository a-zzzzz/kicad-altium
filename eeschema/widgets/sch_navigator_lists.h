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

#ifndef SCH_NAVIGATOR_LISTS_H
#define SCH_NAVIGATOR_LISTS_H

#include <wx/panel.h>
#include <wx/treelist.h>
#include <kiid.h>
#include <sch_sheet_path.h>

#include <functional>
#include <set>

class SCH_EDIT_FRAME;
class SCH_ITEM;
class wxSplitterWindow;


/**
 * The lower part of Altium's schematic Navigator panel: the compiled design's component
 * instances (Instance | Comment | Type), its nets and buses (Net / Bus | Scope) and the pins of
 * the picked component or net (Component Pins / Net Pins | Electrical Type | Type).
 *
 * Picking a row jumps to the sheet that holds it, zooms to it, highlights it (Altium's "Mask
 * Objects") and optionally selects it, following the options of the right-click menu.
 */
class SCH_NAVIGATOR_LISTS : public wxPanel
{
public:
    SCH_NAVIGATOR_LISTS( wxWindow* aParent, SCH_EDIT_FRAME* aFrame );
    ~SCH_NAVIGATOR_LISTS() override;

    /// The design changed: rebuild the lists the next time they are on screen.
    void MarkDirty() { m_dirty = true; }

    /// Show a net as picked (when it is highlighted on the canvas).
    void ShowNet( const wxString& aNetName );

    /// Altium's right-click menu of the Navigator's lists (also used by the documents tree).
    void ShowContextMenu( wxWindow* aWindow, const wxString& aJumpLabel,
                          std::function<void()> aJump );

    /// Picked-object behaviour, shared by all the Navigator's lists.
    struct OPTIONS
    {
        bool zoom = true;       ///< Zoom on Objects
        bool mask = true;       ///< Mask Objects (highlight the net, dim the rest)
        bool select = false;    ///< Select Objects
    };

    static OPTIONS& Options();

    /// Item data of a row.
    struct ROW : public wxClientData
    {
        enum KIND { COMPONENT, NET, PIN, ITEM, FIELD, FOLDER };

        KIND           kind = FOLDER;
        SCH_SHEET_PATH sheet;
        KIID           id;            ///< the symbol, pin or item
        wxString       ref;           ///< COMPONENT: its reference
        wxString       net;           ///< NET: the net's full name
    };

private:
    void rebuild();
    void buildComponents();
    void buildNets();
    void fillPins( const ROW& aRow );

    void onIdle( wxIdleEvent& aEvent );
    void onPicked( wxTreeListCtrl* aList, wxTreeListItem aItem );
    void onContextMenu( wxTreeListCtrl* aList, wxTreeListEvent& aEvent );

    void jumpTo( const ROW& aRow );
    void jumpToComponent( const wxString& aRef );
    void jumpToNet( const wxString& aNet );
    void jumpToItem( const SCH_SHEET_PATH& aSheet, const KIID& aId );

    wxTreeListCtrl* makeList( wxWindow* aParent, const std::vector<wxString>& aColumns );
    std::set<wxString> expandedKeys( wxTreeListCtrl* aList ) const;
    void restoreExpanded( wxTreeListCtrl* aList, const std::set<wxString>& aKeys );
    static wxString rowKey( const ROW* aRow );

private:
    SCH_EDIT_FRAME*   m_frame;
    wxSplitterWindow* m_split1;
    wxSplitterWindow* m_split2;
    wxTreeListCtrl*   m_components;
    wxTreeListCtrl*   m_nets;
    wxTreeListCtrl*   m_pins;
    bool              m_dirty;
    bool              m_building;
    bool              m_placed;       ///< sashes set to even thirds
    wxString          m_pinsFor;      ///< key of the row the pins list shows
};

#endif // SCH_NAVIGATOR_LISTS_H
