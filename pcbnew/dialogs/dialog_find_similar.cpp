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

#include <dialogs/dialog_find_similar.h>

#include <board.h>
#include <footprint.h>
#include <pad.h>
#include <pcb_base_frame.h>
#include <pcb_track.h>
#include <property_text.h>
#include <zone.h>


DIALOG_FIND_SIMILAR::DIALOG_FIND_SIMILAR( PCB_BASE_FRAME* aFrame, BOARD_ITEM* aReference ) :
        DIALOG_FIND_SIMILAR_OBJECTS( aFrame, aReference, boardItems( aFrame ),
                                     [aFrame]( PROPERTY_BASE* aProperty, const wxVariant& aValue )
                                     {
                                         return PROPERTY_TEXT::Format( aFrame, aProperty, aValue );
                                     } )
{
}


std::vector<EDA_ITEM*> DIALOG_FIND_SIMILAR::boardItems( PCB_BASE_FRAME* aFrame )
{
    std::vector<EDA_ITEM*> items;
    BOARD*                 board = aFrame->GetBoard();

    for( PCB_TRACK* track : board->Tracks() )
        items.push_back( track );

    for( FOOTPRINT* fp : board->Footprints() )
    {
        items.push_back( fp );

        for( PAD* pad : fp->Pads() )
            items.push_back( pad );

        for( BOARD_ITEM* item : fp->GraphicalItems() )
            items.push_back( item );

        for( ZONE* zone : fp->Zones() )
            items.push_back( zone );
    }

    for( BOARD_ITEM* item : board->Drawings() )
        items.push_back( item );

    for( ZONE* zone : board->Zones() )
        items.push_back( zone );

    return items;
}
