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

#ifndef PROPERTY_LIST_PANEL_H
#define PROPERTY_LIST_PANEL_H

#include <wx/panel.h>
#include <wx/variant.h>
#include <vector>

class EDA_DRAW_FRAME;
class EDA_ITEM;
class PROPERTY_BASE;
class wxChoice;
class wxGrid;
class wxGridEvent;
class wxGridRangeSelectEvent;

/**
 * Altium-style object list (PCB List / SCH List), editor-neutral: a spreadsheet of objects with
 * one column per property.  Shows the selection or every object, optionally one kind only;
 * editing a cell applies the value to all selected rows in one undoable step
 * (docs/altium-parity/list-panel.md).  Editors supply the objects and the commit.
 */
class PROPERTY_LIST_PANEL : public wxPanel
{
public:
    /// @param aListName shown in undo descriptions, e.g. "PCB List".
    PROPERTY_LIST_PANEL( wxWindow* aParent, EDA_DRAW_FRAME* aFrame, const wxString& aListName );

    /// Reload rows and values (after edits, model changes or a selection change).
    void Rebuild();

    /// Rebuild once the current event is done (coalesces bursts of change notifications).
    void ScheduleRebuild();

    /// Call when the canvas selection changed.
    void OnSelectionChanged();

protected:
    /// False while there is no document to list.
    virtual bool hasModel() const = 0;

    virtual std::vector<EDA_ITEM*> selectedItems() const = 0;
    virtual std::vector<EDA_ITEM*> allItems() const = 0;

    /// Value text; the default shows choices, distances in user units and Yes/No.
    virtual wxString formatValue( PROPERTY_BASE* aProperty, const wxVariant& aValue ) const;

    /// Parse typed text; false if it isn't a valid value.
    virtual bool parseValue( PROPERTY_BASE* aProperty, const wxVariant& aCurrent,
                             const wxString& aText, wxVariant& aValue ) const;

    /// True if this cell may be edited.  Default: writeable and editable as text.
    virtual bool isEditable( EDA_ITEM* aItem, PROPERTY_BASE* aProperty,
                             const wxVariant& aCurrent ) const;

    /// Edits are made between beginEdit() and endEdit(), one setValue() per object.
    virtual void beginEdit() = 0;
    virtual bool setValue( EDA_ITEM* aItem, PROPERTY_BASE* aProperty, const wxVariant& aValue ) = 0;
    virtual void endEdit( const wxString& aDescription ) = 0;

    EDA_DRAW_FRAME* m_frame;

private:
    void onCellChanged( wxGridEvent& aEvent );
    void onRangeSelected( wxGridRangeSelectEvent& aEvent );

    bool showingSelection() const;
    std::vector<int> selectedRows() const;

    wxString                 m_listName;
    wxChoice*                m_viewChoice;
    wxChoice*                m_kindChoice;
    wxGrid*                  m_grid;

    std::vector<EDA_ITEM*>   m_rows;
    std::vector<wxString>    m_columns;     ///< property names; column 0 is the object kind
    bool                     m_updating;    ///< ignore grid events while filling the grid
    bool                     m_rebuildPending;
};

#endif // PROPERTY_LIST_PANEL_H
