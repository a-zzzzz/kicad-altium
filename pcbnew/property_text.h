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

#ifndef PROPERTY_TEXT_H
#define PROPERTY_TEXT_H

#include <wx/string.h>
#include <wx/variant.h>

class BOARD_ITEM;
class PCB_BASE_FRAME;
class PROPERTY_BASE;

/**
 * Read, show and parse board item properties as text, the way the Properties panel presents
 * them.  Shared by the Altium-style Find Similar dialog and PCB List panel.
 */
namespace PROPERTY_TEXT
{
/// Read a property as a variant (enums as ints).  False if unavailable for this item.
bool GetValue( BOARD_ITEM* aItem, PROPERTY_BASE* aProperty, wxVariant& aValue );

/// Human-readable text: choice labels, net names, distances in user units, Yes/No.
wxString Format( PCB_BASE_FRAME* aFrame, PROPERTY_BASE* aProperty, const wxVariant& aValue );

/**
 * Parse user text into a value of the same type as \a aCurrent (the item's current value).
 *
 * @return false if the text can't be understood or the type isn't editable as text.
 */
bool Parse( PCB_BASE_FRAME* aFrame, PROPERTY_BASE* aProperty, const wxVariant& aCurrent,
            const wxString& aText, wxVariant& aValue );

/// True if Parse() can handle values like \a aCurrent.
bool IsEditableAsText( PROPERTY_BASE* aProperty, const wxVariant& aCurrent );
}

#endif // PROPERTY_TEXT_H
