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

#include <property_text.h>

#include <board.h>
#include <board_item.h>
#include <netinfo.h>
#include <pcb_base_frame.h>
#include <properties/property.h>
#include <properties/property_value_text.h>

// The editor-neutral parts live in common (PROPERTY_VALUE_TEXT); the board adds nets.

bool PROPERTY_TEXT::GetValue( BOARD_ITEM* aItem, PROPERTY_BASE* aProperty, wxVariant& aValue )
{
    return PROPERTY_VALUE_TEXT::GetValue( aItem, aProperty, aValue );
}


wxString PROPERTY_TEXT::Format( PCB_BASE_FRAME* aFrame, PROPERTY_BASE* aProperty,
                                const wxVariant& aValue )
{
    if( !aValue.IsNull() && aValue.GetType() == wxS( "long" ) && !aProperty->HasChoices()
        && aProperty->Display() == PT_NET )
    {
        if( NETINFO_ITEM* net = aFrame->GetBoard()->FindNet( (int) aValue.GetLong() ) )
            return net->GetNetname();
    }

    return PROPERTY_VALUE_TEXT::Format( aFrame, aProperty, aValue );
}


bool PROPERTY_TEXT::IsEditableAsText( PROPERTY_BASE* aProperty, const wxVariant& aCurrent )
{
    return PROPERTY_VALUE_TEXT::IsEditableAsText( aProperty, aCurrent );
}


bool PROPERTY_TEXT::Parse( PCB_BASE_FRAME* aFrame, PROPERTY_BASE* aProperty,
                           const wxVariant& aCurrent, const wxString& aText, wxVariant& aValue )
{
    if( aCurrent.GetType() == wxS( "long" ) && !aProperty->HasChoices()
        && aProperty->Display() == PT_NET )
    {
        wxString text = aText;
        text.Trim().Trim( false );

        NETINFO_ITEM* net = aFrame->GetBoard()->FindNet( text );

        if( !net )
            return false;

        aValue = wxVariant( (long) net->GetNetCode() );
        return true;
    }

    return PROPERTY_VALUE_TEXT::Parse( aFrame, aProperty, aCurrent, aText, aValue );
}
