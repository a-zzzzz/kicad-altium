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

#include <widgets/pcb_messages_panel.h>
#include <widgets/pcb_panel_utils.h>

#include <board.h>
#include <footprint.h>
#include <pad.h>
#include <pcb_edit_frame.h>
#include <pcb_marker.h>
#include <rc_item.h>
#include <tool/actions.h>
#include <tool/tool_manager.h>

#include <wx/filename.h>


namespace
{
/// An object as Altium's Messages Details name it: "Pad U4-7", "Component U4", ...
wxString objectName( BOARD_ITEM* aItem, UNITS_PROVIDER* aUnits )
{
    switch( aItem->Type() )
    {
    case PCB_PAD_T:
    {
        PAD*       pad = static_cast<PAD*>( aItem );
        FOOTPRINT* fp = pad->GetParentFootprint();

        return wxString::Format( _( "Pad %s-%s" ), fp ? fp->GetReference() : wxString( wxS( "?" ) ),
                                 pad->GetNumber() );
    }

    case PCB_FOOTPRINT_T:
        return wxString::Format( _( "Component %s" ),
                                 static_cast<FOOTPRINT*>( aItem )->GetReference() );

    default:
        return aItem->GetItemDescription( aUnits, false );
    }
}
}


PCB_MESSAGES_PANEL::PCB_MESSAGES_PANEL( wxWindow* aParent, PCB_EDIT_FRAME* aFrame ) :
        MESSAGES_PANEL( aParent, aFrame ),
        m_pcbFrame( aFrame )
{
}


size_t PCB_MESSAGES_PANEL::markerSignature()
{
    BOARD* board = m_pcbFrame->GetBoard();

    if( !board )
        return 0;

    size_t sig = std::hash<const void*>()( board ) ^ ( board->Markers().size() * 0x9E3779B9 );

    for( PCB_MARKER* marker : board->Markers() )
    {
        sig = sig * 31 + marker->m_Uuid.Hash();
        sig = sig * 31 + ( marker->IsExcluded() ? 1 : 0 ) + size_t( marker->GetSeverity() ) * 2;
    }

    return sig;
}


void PCB_MESSAGES_PANEL::collectMarkers( std::vector<MESSAGE>& aMessages )
{
    BOARD* board = m_pcbFrame->GetBoard();

    if( !board )
        return;

    wxString document = wxFileName( board->GetFileName() ).GetFullName();

    for( PCB_MARKER* marker : board->Markers() )
    {
        SEVERITY severity = marker->GetSeverity();

        // Excluded (waived) violations and ignored checks aren't messages
        if( marker->IsExcluded() || severity == RPT_SEVERITY_IGNORE
            || severity == RPT_SEVERITY_EXCLUSION )
        {
            continue;
        }

        std::shared_ptr<RC_ITEM> rcItem = marker->GetRCItem();

        if( !rcItem )
            continue;

        MESSAGE msg;
        msg.severity = severity;
        msg.document = document;
        msg.key = marker->m_Uuid.AsString();

        switch( marker->GetMarkerType() )
        {
        case MARKER_BASE::MARKER_PARITY: msg.source = _( "Schematic Parity" ); break;
        default:                         msg.source = _( "DRC" );              break;
        }

        msg.text = rcItem->GetErrorMessage( true );

        BOARD_ITEM* a = board->ResolveItem( rcItem->GetMainItemID(), true );
        BOARD_ITEM* b = board->ResolveItem( rcItem->GetAuxItemID(), true );

        if( a && a != marker )
        {
            msg.text << wxS( ": " ) << a->GetItemDescription( m_pcbFrame, true );

            if( b && b != marker )
                msg.text << wxS( " " ) << _( "and" ) << wxS( " " )
                         << b->GetItemDescription( m_pcbFrame, true );
        }

        KIID id = marker->m_Uuid;
        msg.activate = [this, id]() { showMarker( id ); };

        // Details: the objects the violation is about
        for( const KIID& itemId : rcItem->GetIDs() )
        {
            if( itemId == niluuid )
                continue;

            BOARD_ITEM* obj = board->ResolveItem( itemId, true );

            if( !obj || obj == marker )
                continue;

            PROJECT_MESSAGE_DETAIL detail;
            detail.text = objectName( obj, m_pcbFrame );
            detail.icon = obj->GetMenuImage();
            detail.activate = [this, itemId]() { showObject( itemId ); };
            msg.details.push_back( std::move( detail ) );
        }

        aMessages.push_back( std::move( msg ) );
    }
}


void PCB_MESSAGES_PANEL::showMarker( const KIID& aMarker )
{
    BOARD* board = m_pcbFrame->GetBoard();

    if( !board )
        return;

    PCB_MARKER* marker = nullptr;

    for( PCB_MARKER* candidate : board->Markers() )
    {
        if( candidate->m_Uuid == aMarker )
        {
            marker = candidate;
            break;
        }
    }

    if( !marker )
        return;

    std::shared_ptr<RC_ITEM> rcItem = marker->GetRCItem();
    EDA_ITEMS                items;
    BOX2I                    bbox = marker->GetBoundingBox();

    if( rcItem )
    {
        for( const KIID& id : { rcItem->GetMainItemID(), rcItem->GetAuxItemID() } )
        {
            if( id == niluuid )
                continue;

            if( BOARD_ITEM* item = board->ResolveItem( id, true ) )
            {
                if( item == marker )
                    continue;

                items.push_back( item );
                bbox.Merge( item->GetBoundingBox() );
            }
        }
    }

    if( items.empty() )
        items.push_back( marker );

    TOOL_MANAGER* toolMgr = m_pcbFrame->GetToolManager();
    toolMgr->RunAction( ACTIONS::selectionClear );
    toolMgr->RunAction<EDA_ITEMS*>( ACTIONS::selectItems, &items );

    PcbZoomToBox( m_pcbFrame, bbox );
    m_pcbFrame->GetCanvas()->SetFocus();
}


void PCB_MESSAGES_PANEL::showObject( const KIID& aItem )
{
    BOARD* board = m_pcbFrame->GetBoard();

    if( !board )
        return;

    BOARD_ITEM* item = board->ResolveItem( aItem, true );

    if( !item )
        return;

    EDA_ITEMS items = { item };

    TOOL_MANAGER* toolMgr = m_pcbFrame->GetToolManager();
    toolMgr->RunAction( ACTIONS::selectionClear );
    toolMgr->RunAction<EDA_ITEMS*>( ACTIONS::selectItems, &items );

    PcbZoomToBox( m_pcbFrame, item->GetBoundingBox() );
    m_pcbFrame->GetCanvas()->SetFocus();
}
