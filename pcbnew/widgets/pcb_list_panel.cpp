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

#include <widgets/pcb_list_panel.h>

#include <board.h>
#include <board_commit.h>
#include <footprint.h>
#include <pad.h>
#include <pcb_edit_frame.h>
#include <pcb_track.h>
#include <property_text.h>
#include <tool/tool_manager.h>
#include <tools/pcb_selection_tool.h>
#include <zone.h>


PCB_LIST_PANEL::PCB_LIST_PANEL( PCB_EDIT_FRAME* aFrame ) :
        PROPERTY_LIST_PANEL( aFrame, aFrame, _( "PCB List" ) ),
        m_pcbFrame( aFrame )
{
}


PCB_LIST_PANEL::~PCB_LIST_PANEL() = default;


bool PCB_LIST_PANEL::hasModel() const
{
    return m_pcbFrame->GetBoard() != nullptr;
}


std::vector<EDA_ITEM*> PCB_LIST_PANEL::selectedItems() const
{
    std::vector<EDA_ITEM*> items;

    for( EDA_ITEM* item : m_pcbFrame->GetToolManager()->GetTool<PCB_SELECTION_TOOL>()->GetSelection() )
    {
        if( item->IsBOARD_ITEM() )
            items.push_back( item );
    }

    return items;
}


std::vector<EDA_ITEM*> PCB_LIST_PANEL::allItems() const
{
    std::vector<EDA_ITEM*> items;
    BOARD*                 board = m_pcbFrame->GetBoard();

    for( PCB_TRACK* track : board->Tracks() )
        items.push_back( track );

    for( FOOTPRINT* fp : board->Footprints() )
    {
        items.push_back( fp );

        for( PAD* pad : fp->Pads() )
            items.push_back( pad );

        for( BOARD_ITEM* item : fp->GraphicalItems() )
            items.push_back( item );
    }

    for( BOARD_ITEM* item : board->Drawings() )
        items.push_back( item );

    for( ZONE* zone : board->Zones() )
        items.push_back( zone );

    return items;
}


wxString PCB_LIST_PANEL::formatValue( PROPERTY_BASE* aProperty, const wxVariant& aValue ) const
{
    return PROPERTY_TEXT::Format( m_pcbFrame, aProperty, aValue );
}


bool PCB_LIST_PANEL::parseValue( PROPERTY_BASE* aProperty, const wxVariant& aCurrent,
                                 const wxString& aText, wxVariant& aValue ) const
{
    return PROPERTY_TEXT::Parse( m_pcbFrame, aProperty, aCurrent, aText, aValue );
}


void PCB_LIST_PANEL::beginEdit()
{
    m_commit = std::make_unique<BOARD_COMMIT>( m_pcbFrame );
}


bool PCB_LIST_PANEL::setValue( EDA_ITEM* aItem, PROPERTY_BASE* aProperty, const wxVariant& aValue )
{
    m_commit->Modify( aItem );
    aItem->Set( aProperty, aValue );
    return true;
}


void PCB_LIST_PANEL::endEdit( const wxString& aDescription )
{
    if( m_commit && !m_commit->Empty() )
        m_commit->Push( aDescription );

    m_commit.reset();
}
