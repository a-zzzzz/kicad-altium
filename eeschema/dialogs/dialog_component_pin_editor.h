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

#ifndef DIALOG_COMPONENT_PIN_EDITOR_H
#define DIALOG_COMPONENT_PIN_EDITOR_H

#include <dialog_shim.h>
#include <math/vector2d.h>
#include <pin_type.h>

#include <vector>

class LIB_SYMBOL;
class SCH_PIN;
class SYMBOL_EDIT_FRAME;
class ALTIUM_SUB_TABS;
class wxChoice;
class WX_GRID;
class wxGridEvent;
class wxSearchCtrl;
class wxSimplebook;
class wxSpinCtrl;
class wxStaticText;
class wxTextCtrl;
class wxBitmapToggleButton;


/**
 * Altium's Component Pin Editor: every pin of the component in a table (Designator, Name,
 * Description, Type, Owner part, Show, Number, Name, Pin/Pkg Length, Propagation Delay), Add /
 * Remove, and the picked pin's properties at the right (Location, Rotation, Designator, Name,
 * Functions, Electrical Type, ...) with a preview.  Works on copies; OK puts them in the symbol.
 *
 * Location and Rotation follow Altium: the location is the end at the body, and the pin
 * extends from it in the rotation's direction (0 Degrees = to the right), Y upwards.
 */
class DIALOG_COMPONENT_PIN_EDITOR : public DIALOG_SHIM
{
public:
    DIALOG_COMPONENT_PIN_EDITOR( SYMBOL_EDIT_FRAME* aFrame, LIB_SYMBOL* aSymbol );
    ~DIALOG_COMPONENT_PIN_EDITOR() override;

    bool TransferDataFromWindow() override;

    /// Opens the editor on the symbol being edited, as one undoable change.
    static void Run( SYMBOL_EDIT_FRAME* aFrame );

private:
    void buildTable();
    void fillRow( int aRow );
    void showPin( int aRow );
    void applyForm();
    void onCellChanged( wxGridEvent& aEvent );
    void onSelect( wxGridEvent& aEvent );
    void onAdd();
    void onRemove();
    void paintPreview();

    int  pickedRow() const;

    // Altium's view of a pin: body end and direction
    VECTOR2I        bodyEnd( const SCH_PIN* aPin ) const;
    int             altiumRotation( const SCH_PIN* aPin ) const;
    void            placePin( SCH_PIN* aPin, const VECTOR2I& aBodyEnd, int aRotation ) const;

    SYMBOL_EDIT_FRAME*     m_frame;
    LIB_SYMBOL*            m_symbol;
    std::vector<SCH_PIN*>  m_pins;
    std::vector<int>       m_shown;        ///< table rows -> m_pins (search filter)
    bool                   m_showNumbers;
    bool                   m_showNames;
    bool                   m_loading = false;
    SCH_PIN*               m_formPin = nullptr;  ///< the pin the form at the right shows

    WX_GRID*               m_grid;
    wxSearchCtrl*          m_search;
    ALTIUM_SUB_TABS*       m_tabs;
    wxSimplebook*          m_book;
    wxTextCtrl*            m_x;
    wxTextCtrl*            m_y;
    wxChoice*              m_rotation;
    wxTextCtrl*            m_designator;
    wxTextCtrl*            m_name;
    wxTextCtrl*            m_functions;
    wxChoice*              m_type;
    wxTextCtrl*            m_length;
    wxSpinCtrl*            m_part;
    wxWindow*              m_preview;
    wxStaticText*          m_status;
    wxWindow*              m_form;
};

#endif // DIALOG_COMPONENT_PIN_EDITOR_H
