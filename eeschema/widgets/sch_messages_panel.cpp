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

#include <widgets/sch_messages_panel.h>

#include <base_units.h>
#include <erc/erc_item.h>
#include <sch_draw_panel.h>
#include <sch_edit_frame.h>
#include <sch_marker.h>
#include <sch_label.h>
#include <sch_pin.h>
#include <sch_sheet.h>
#include <sch_sheet_pin.h>
#include <sch_symbol.h>
#include <sch_screen.h>
#include <sch_sheet_path.h>
#include <schematic.h>
#include <connection_graph.h>
#include <messages_store.h>
#include <string_utils.h>
#include <wildcards_and_files_ext.h>
#include <project.h>
#include <bitmaps.h>
#include <tool/actions.h>
#include <tool/tool_manager.h>
#include <tools/sch_actions.h>
#include <view/view.h>
#include <math/vector2wx.h>

#include <wx/filename.h>

#include <cmath>
#include <set>


namespace
{
/// An object as Altium's Messages Details name it: "Pin U4-7", "Port BT_RF", ...
wxString objectName( SCH_ITEM* aItem, const SCH_SHEET_PATH& aSheet, UNITS_PROVIDER* aUnits )
{
    switch( aItem->Type() )
    {
    case SCH_PIN_T:
    {
        SCH_PIN*    pin = static_cast<SCH_PIN*>( aItem );
        SCH_SYMBOL* symbol = dynamic_cast<SCH_SYMBOL*>( pin->GetParentSymbol() );
        wxString    ref = symbol ? symbol->GetRef( &aSheet, false ) : wxString( wxS( "?" ) );

        return wxString::Format( _( "Pin %s-%s" ), ref, pin->GetShownNumber() );
    }

    case SCH_SYMBOL_T:
        return wxString::Format( _( "Component %s" ),
                                 static_cast<SCH_SYMBOL*>( aItem )->GetRef( &aSheet, true ) );

    case SCH_GLOBAL_LABEL_T:
    case SCH_HIER_LABEL_T:
        return wxString::Format( _( "Port %s" ),
                                 static_cast<SCH_LABEL_BASE*>( aItem )->GetShownText( &aSheet, false ) );

    case SCH_LABEL_T:
        return wxString::Format( _( "Net Label %s" ),
                                 static_cast<SCH_LABEL_BASE*>( aItem )->GetShownText( &aSheet, false ) );

    case SCH_SHEET_PIN_T:
        return wxString::Format( _( "Sheet Entry %s" ),
                                 static_cast<SCH_SHEET_PIN*>( aItem )->GetShownText( &aSheet, false ) );

    case SCH_SHEET_T:
        return wxString::Format( _( "Sheet Symbol %s" ),
                                 static_cast<SCH_SHEET*>( aItem )->GetShownName( false ) );

    default:
        return aItem->GetItemDescription( aUnits, false );
    }
}
}


SCH_MESSAGES_PANEL::SCH_MESSAGES_PANEL( wxWindow* aParent, SCH_EDIT_FRAME* aFrame ) :
        MESSAGES_PANEL( aParent, aFrame ),
        m_schFrame( aFrame )
{
}


size_t SCH_MESSAGES_PANEL::markerSignature()
{
    SCHEMATIC& schematic = m_schFrame->Schematic();

    if( !schematic.IsValid() || !schematic.HasHierarchy() )
        return 0;

    size_t                sig = 17;
    std::set<SCH_SCREEN*> screens;
    SCH_SHEET_LIST        hierarchy = schematic.Hierarchy();

    for( const SCH_SHEET_PATH& path : hierarchy )
    {
        SCH_SCREEN* screen = path.LastScreen();

        if( !screen || !screens.insert( screen ).second )
            continue;

        for( SCH_ITEM* item : screen->Items().OfType( SCH_MARKER_T ) )
        {
            SCH_MARKER* marker = static_cast<SCH_MARKER*>( item );
            sig = sig * 31 + marker->m_Uuid.Hash();
            sig = sig * 31 + ( marker->IsExcluded() ? 1 : 0 ) + size_t( marker->GetSeverity() ) * 2;
        }
    }

    return sig;
}


void SCH_MESSAGES_PANEL::collectMarkers( std::vector<MESSAGE>& aMessages )
{
    SCHEMATIC& schematic = m_schFrame->Schematic();

    if( !schematic.IsValid() || !schematic.HasHierarchy() )
        return;

    std::set<KIID> seen;
    SCH_SHEET_LIST hierarchy = schematic.Hierarchy();

    for( const SCH_SHEET_PATH& path : hierarchy )
    {
        SCH_SCREEN* screen = path.LastScreen();

        if( !screen )
            continue;

        wxString document = wxFileName( screen->GetFileName() ).GetFullName();

        for( SCH_ITEM* item : screen->Items().OfType( SCH_MARKER_T ) )
        {
            SCH_MARKER* marker = static_cast<SCH_MARKER*>( item );
            SEVERITY    severity = marker->GetSeverity();

            if( marker->IsExcluded() || severity == RPT_SEVERITY_IGNORE
                || severity == RPT_SEVERITY_EXCLUSION )
            {
                continue;
            }

            std::shared_ptr<ERC_ITEM> ercItem =
                    std::static_pointer_cast<ERC_ITEM>( marker->GetRCItem() );

            if( !ercItem )
                continue;

            // Markers of a sheet used several times belong to one instance, or to all of them
            if( ercItem->IsSheetSpecific() && ercItem->GetSpecificSheetPath() != path )
                continue;

            if( !seen.insert( marker->m_Uuid ).second )
                continue;

            MESSAGE msg;
            msg.severity = severity;
            msg.document = document;
            msg.source = _( "Compiler" );     // as Altium labels its validation results
            msg.key = marker->m_Uuid.AsString();
            msg.text = ercItem->GetErrorMessage( true );

            SCH_ITEM* a = schematic.ResolveItem( ercItem->GetMainItemID(), nullptr, true );
            SCH_ITEM* b = schematic.ResolveItem( ercItem->GetAuxItemID(), nullptr, true );

            if( a && a != marker )
            {
                msg.text << wxS( ": " ) << a->GetItemDescription( m_schFrame, true );

                if( b && b != marker )
                    msg.text << wxS( " " ) << _( "and" ) << wxS( " " )
                             << b->GetItemDescription( m_schFrame, true );
            }

            if( hierarchy.size() > 1 )
                msg.document << wxS( " (" ) << path.PathHumanReadable() << wxS( ")" );

            KIID id = marker->m_Uuid;
            msg.activate = [this, id]() { showMarker( id ); };

            // Details: the objects the violation is about
            for( const KIID& itemId : ercItem->GetIDs() )
            {
                if( itemId == niluuid )
                    continue;

                SCH_SHEET_PATH itemSheet;
                SCH_ITEM*      obj = schematic.ResolveItem( itemId, &itemSheet, true );

                if( !obj || obj == marker )
                    continue;

                PROJECT_MESSAGE_DETAIL detail;
                detail.text = objectName( obj, itemSheet.empty() ? path : itemSheet, m_schFrame );
                detail.icon = obj->GetMenuImage();
                detail.activate = [this, itemId, id]() { showObject( itemId, id ); };
                msg.details.push_back( std::move( detail ) );
            }

            aMessages.push_back( std::move( msg ) );
        }
    }
}


int SCH_MESSAGES_PANEL::CompileProject( SCH_EDIT_FRAME* aFrame )
{
    SCHEMATIC& schematic = aFrame->Schematic();

    if( !schematic.IsValid() || !schematic.ConnectionGraph() )
        return 0;

    // Pins of real parts (power ports are net names in Altium, not pins), per net
    std::map<wxString, std::vector<std::pair<SCH_PIN*, SCH_SHEET_PATH>>> pinsByNet;

    for( const auto& [key, subgraphs] : schematic.ConnectionGraph()->GetNetMap() )
    {
        for( CONNECTION_SUBGRAPH* subgraph : subgraphs )
        {
            for( SCH_ITEM* item : subgraph->GetItems() )
            {
                if( item->Type() != SCH_PIN_T )
                    continue;

                SCH_PIN*    pin = static_cast<SCH_PIN*>( item );
                SCH_SYMBOL* symbol = dynamic_cast<SCH_SYMBOL*>( pin->GetParentSymbol() );

                if( !symbol || symbol->IsPower() )
                    continue;

                pinsByNet[key.Name].emplace_back( pin, subgraph->GetSheet() );
            }
        }
    }

    wxString document = wxFileName( aFrame->Prj().GetProjectPath(), aFrame->Prj().GetProjectName(),
                                    FILEEXT::KiCadSchematicFileExtension ).GetFullName();
    std::vector<PROJECT_MESSAGE> results;

    for( const auto& [netName, pins] : pinsByNet )
    {
        if( pins.size() != 1 || netName.IsEmpty() || netName.StartsWith( wxS( "unconnected-" ) ) )
            continue;

        SCH_PIN*    pin = pins.front().first;
        SCH_SYMBOL* symbol = static_cast<SCH_SYMBOL*>( pin->GetParentSymbol() );
        wxString    pinName = symbol->GetRef( &pins.front().second, false ) + wxS( "-" )
                              + pin->GetShownNumber();
        wxString    shownNet = UnescapeString( netName );

        // A net named after a global label or power port: just the name, as Altium shows it
        if( shownNet.StartsWith( wxS( "/" ) ) )
            shownNet = shownNet.AfterLast( '/' );

        PROJECT_MESSAGE msg;
        msg.severity = RPT_SEVERITY_ERROR;
        msg.document = document;
        msg.source = _( "Compiler" );
        msg.text = wxString::Format( _( "Net %s has only one pin (Pin %s)" ), shownNet, pinName );
        msg.key = wxS( "compiler:single-pin:" ) + netName;
        msg.owner = aFrame;

        PROJECT_MESSAGE_DETAIL detail;
        detail.text = wxS( "Pin " ) + pinName;
        detail.icon = BITMAPS::pin;
        msg.details.push_back( std::move( detail ) );

        results.push_back( std::move( msg ) );
    }

    int errors = (int) results.size();

    MESSAGES_STORE::SetCompilerMessages( std::move( results ) );

    // As in Altium, the compile results come to the front
    if( errors > 0 )
        MESSAGES_PANEL::ShowPane( aFrame );

    return errors;
}


void SCH_MESSAGES_PANEL::showMarker( const KIID& aMarker )
{
    SCHEMATIC&     schematic = m_schFrame->Schematic();
    SCH_SHEET_PATH sheet;
    SCH_ITEM*      item = schematic.ResolveItem( aMarker, &sheet, true );

    if( !item || item->Type() != SCH_MARKER_T )
        return;

    SCH_MARKER*               marker = static_cast<SCH_MARKER*>( item );
    std::shared_ptr<ERC_ITEM> ercItem = std::static_pointer_cast<ERC_ITEM>( marker->GetRCItem() );

    if( ercItem && ercItem->IsSheetSpecific() )
        sheet = ercItem->GetSpecificSheetPath();

    // Zoom to the marker and the item it reports
    BOX2I bbox = marker->GetBoundingBox();

    if( ercItem )
    {
        if( SCH_ITEM* mainItem = schematic.ResolveItem( ercItem->GetMainItemID(), nullptr, true ) )
        {
            if( mainItem != marker && sheet.LastScreen()
                    && sheet.LastScreen()->Items().contains( mainItem ) )
                bbox.Merge( mainItem->GetBoundingBox() );
        }
    }

    zoomTo( sheet, { marker }, bbox );
}


void SCH_MESSAGES_PANEL::showObject( const KIID& aItem, const KIID& aMarker )
{
    SCHEMATIC&     schematic = m_schFrame->Schematic();
    SCH_SHEET_PATH sheet;
    SCH_ITEM*      item = schematic.ResolveItem( aItem, &sheet, true );

    if( !item )
        return;

    // An object of a sheet used several times: the instance the marker is about
    if( SCH_ITEM* mk = schematic.ResolveItem( aMarker, nullptr, true );
            mk && mk->Type() == SCH_MARKER_T )
    {
        std::shared_ptr<ERC_ITEM> ercItem = std::static_pointer_cast<ERC_ITEM>(
                static_cast<SCH_MARKER*>( mk )->GetRCItem() );

        if( ercItem && ercItem->IsSheetSpecific()
                && ercItem->GetSpecificSheetPath().LastScreen() == sheet.LastScreen() )
        {
            sheet = ercItem->GetSpecificSheetPath();
        }
    }

    // Pins are selected as themselves (as Altium highlights the pin), zoomed with their symbol
    // nearby
    zoomTo( sheet, { item }, item->GetBoundingBox() );
}


void SCH_MESSAGES_PANEL::zoomTo( SCH_SHEET_PATH aSheet, EDA_ITEMS aItems, BOX2I bbox )
{
    TOOL_MANAGER* toolMgr = m_schFrame->GetToolManager();

    if( !aSheet.empty() && aSheet != m_schFrame->GetCurrentSheet() )
    {
        toolMgr->RunAction<SCH_SHEET_PATH*>( SCH_ACTIONS::changeSheet, &aSheet );
        m_schFrame->RedrawScreen( m_schFrame->GetScreen()->m_ScrollCenter, false );
    }

    toolMgr->RunAction( ACTIONS::selectionClear );
    toolMgr->RunAction<EDA_ITEMS*>( ACTIONS::selectItems, &aItems );

    KIGFX::VIEW* view = m_schFrame->GetCanvas()->GetView();

    bbox.Normalize();
    bbox.Inflate( std::max( bbox.GetWidth(), bbox.GetHeight() ) / 2 + schIUScale.MilsToIU( 400 ) );

    VECTOR2D screen = view->ToWorld( ToVECTOR2D( m_schFrame->GetCanvas()->GetClientSize() ), false );

    if( std::fabs( screen.x ) > 1 && std::fabs( screen.y ) > 1 )
    {
        double ratio = std::max( std::fabs( bbox.GetWidth() / screen.x ),
                                 std::fabs( bbox.GetHeight() / screen.y ) );

        if( ratio > 0 )
            view->SetScale( view->GetScale() / ratio );
    }

    view->SetCenter( bbox.Centre() );
    m_schFrame->GetCanvas()->ForceRefresh();
    m_schFrame->GetCanvas()->SetFocus();
}
