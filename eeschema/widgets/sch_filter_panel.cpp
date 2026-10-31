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

#include "sch_filter_panel.h"

#include <sch_edit_frame.h>
#include <sch_connection.h>
#include <sch_field.h>
#include <sch_label.h>
#include <sch_line.h>
#include <sch_pin.h>
#include <sch_screen.h>
#include <sch_shape.h>
#include <sch_sheet.h>
#include <sch_sheet_pin.h>
#include <sch_symbol.h>
#include <sch_text.h>
#include <sch_textbox.h>
#include <schematic.h>
#include <tool/actions.h>
#include <tool/selection.h>
#include <tool/tool_manager.h>
#include <view/view.h>

using namespace ALTIUM_QUERY;


namespace
{

enum ID
{
    K_PART = 1, K_POWER, K_WIRE, K_BUS, K_BUSENTRY, K_JUNCTION, K_NETLABEL, K_PORT,
    K_OFFSHEET, K_SHEETSYMBOL, K_SHEETENTRY, K_PIN, K_PARAMETER, K_DESIGNATOR, K_COMMENT,
    K_NOERC, K_TEXT, K_TEXTFRAME, K_GRAPHIC, K_IMAGE, K_DIRECTIVE, K_SELECTED, K_LOCKED,
    K_HASNOFOOTPRINT,

    F_INNET = 100, F_INCOMPONENT, F_HASPARAMETER, F_HASFOOTPRINT, F_HASPIN,

    D_DESIGNATOR = 200, D_COMMENT, D_FOOTPRINT, D_LIBREF, D_DESCRIPTION, D_TEXT, D_NAME,
    D_ELECTRICAL, D_X, D_Y, D_LOCKED, D_NET, D_OBJECTKIND, D_PARTCOUNT
};


SCH_SYMBOL* owningSymbol( EDA_ITEM* aItem )
{
    if( aItem->Type() == SCH_SYMBOL_T )
        return static_cast<SCH_SYMBOL*>( aItem );

    if( aItem->GetParent() && aItem->GetParent()->Type() == SCH_SYMBOL_T )
        return static_cast<SCH_SYMBOL*>( aItem->GetParent() );

    return nullptr;
}


bool isLine( EDA_ITEM* aItem, int aKind )
{
    if( aItem->Type() != SCH_LINE_T )
        return false;

    SCH_LINE* line = static_cast<SCH_LINE*>( aItem );
    return aKind == 0 ? line->IsWire() : aKind == 1 ? line->IsBus() : line->IsGraphicLine();
}

} // namespace


// ---- adapter -------------------------------------------------------------------------------

SCH_QUERY_ADAPTER::SCH_QUERY_ADAPTER( SCH_EDIT_FRAME* aFrame ) :
        m_frame( aFrame )
{
}


size_t SCH_QUERY_ADAPTER::propertyBaseType() const
{
    return TYPE_HASH( SCH_ITEM );
}


const std::vector<KEYWORD>& SCH_QUERY_ADAPTER::Keywords() const
{
    static const wxString objects = _( "Object Type Checks" );
    static const wxString member = _( "Membership Checks" );
    static const wxString state = _( "State Checks" );
    static const wxString fields = _( "Object Properties" );

    static const std::vector<KEYWORD> keys = {
        { wxS( "IsPart" ),          KIND::KEYWORD, objects, _( "Parts (components that are not power objects)" ), wxS( "IsPart" ), K_PART },
        { wxS( "IsComponent" ),     KIND::KEYWORD, objects, _( "Same as IsPart" ), wxS( "IsComponent" ), K_PART },
        { wxS( "IsPowerObject" ),   KIND::KEYWORD, objects, _( "Power ports (power symbols)" ), wxS( "IsPowerObject" ), K_POWER },
        { wxS( "IsWire" ),          KIND::KEYWORD, objects, _( "Wires" ), wxS( "IsWire" ), K_WIRE },
        { wxS( "IsBus" ),           KIND::KEYWORD, objects, _( "Buses" ), wxS( "IsBus" ), K_BUS },
        { wxS( "IsBusEntry" ),      KIND::KEYWORD, objects, _( "Bus entries" ), wxS( "IsBusEntry" ), K_BUSENTRY },
        { wxS( "IsJunction" ),      KIND::KEYWORD, objects, _( "Junctions" ), wxS( "IsJunction" ), K_JUNCTION },
        { wxS( "IsNetLabel" ),      KIND::KEYWORD, objects, _( "Net labels" ), wxS( "IsNetLabel" ), K_NETLABEL },
        { wxS( "IsPort" ),          KIND::KEYWORD, objects, _( "Ports (hierarchical labels)" ), wxS( "IsPort" ), K_PORT },
        { wxS( "IsOffSheetConnector" ), KIND::KEYWORD, objects, _( "Off sheet connectors (global labels)" ), wxS( "IsOffSheetConnector" ), K_OFFSHEET },
        { wxS( "IsSheetSymbol" ),   KIND::KEYWORD, objects, _( "Sheet symbols" ), wxS( "IsSheetSymbol" ), K_SHEETSYMBOL },
        { wxS( "IsSheetEntry" ),    KIND::KEYWORD, objects, _( "Sheet entries" ), wxS( "IsSheetEntry" ), K_SHEETENTRY },
        { wxS( "IsPin" ),           KIND::KEYWORD, objects, _( "Component pins" ), wxS( "IsPin" ), K_PIN },
        { wxS( "IsParameter" ),     KIND::KEYWORD, objects, _( "Parameters (fields) of components and sheets" ), wxS( "IsParameter" ), K_PARAMETER },
        { wxS( "IsDesignator" ),    KIND::KEYWORD, objects, _( "Component designators" ), wxS( "IsDesignator" ), K_DESIGNATOR },
        { wxS( "IsComment" ),       KIND::KEYWORD, objects, _( "Component comments (values)" ), wxS( "IsComment" ), K_COMMENT },
        { wxS( "IsNoERC" ),         KIND::KEYWORD, objects, _( "No ERC markers (no connects)" ), wxS( "IsNoERC" ), K_NOERC },
        { wxS( "IsText" ),          KIND::KEYWORD, objects, _( "Text strings" ), wxS( "IsText" ), K_TEXT },
        { wxS( "IsTextFrame" ),     KIND::KEYWORD, objects, _( "Text frames" ), wxS( "IsTextFrame" ), K_TEXTFRAME },
        { wxS( "IsGraphic" ),       KIND::KEYWORD, objects, _( "Drawing objects: lines, arcs, rectangles, polygons..." ), wxS( "IsGraphic" ), K_GRAPHIC },
        { wxS( "IsImage" ),         KIND::KEYWORD, objects, _( "Images" ), wxS( "IsImage" ), K_IMAGE },
        { wxS( "IsDirective" ),     KIND::KEYWORD, objects, _( "Directives (parameter sets, net class labels)" ), wxS( "IsDirective" ), K_DIRECTIVE },

        { wxS( "InNet" ),           KIND::FUNCTION, member, _( "Objects on the named net (wildcards allowed)" ), wxS( "InNet('GND')" ), F_INNET },
        { wxS( "InComponent" ),     KIND::FUNCTION, member, _( "The component with this designator and its pins and parameters" ), wxS( "InComponent('U1')" ), F_INCOMPONENT },
        { wxS( "HasParameter" ),    KIND::FUNCTION, member, _( "Components with the parameter (and value, wildcards allowed)" ), wxS( "HasParameter('Manufacturer','*')" ), F_HASPARAMETER },
        { wxS( "HasFootprint" ),    KIND::FUNCTION, member, _( "Components whose footprint matches (wildcards allowed)" ), wxS( "HasFootprint('*0603*')" ), F_HASFOOTPRINT },
        { wxS( "HasPin" ),          KIND::FUNCTION, member, _( "Components with a pin of this designator or name" ), wxS( "HasPin('VCC')" ), F_HASPIN },
        { wxS( "HasNoFootprint" ),  KIND::KEYWORD,  member, _( "Parts without a footprint" ), wxS( "HasNoFootprint" ), K_HASNOFOOTPRINT },

        { wxS( "IsSelected" ),      KIND::KEYWORD, state, _( "Selected objects" ), wxS( "IsSelected" ), K_SELECTED },
        { wxS( "IsLocked" ),        KIND::KEYWORD, state, _( "Locked objects" ), wxS( "IsLocked" ), K_LOCKED },

        { wxS( "Designator" ),      KIND::FIELD, fields, _( "Component designator, or a pin's designator (number)" ), wxS( "Designator Like 'U*'" ), D_DESIGNATOR },
        { wxS( "Comment" ),         KIND::FIELD, fields, _( "Component comment (value)" ), wxS( "Comment = '10k'" ), D_COMMENT },
        { wxS( "CurrentFootprint" ), KIND::FIELD, fields, _( "Component footprint" ), wxS( "CurrentFootprint Like '*0603*'" ), D_FOOTPRINT },
        { wxS( "Footprint" ),       KIND::FIELD, fields, _( "Component footprint" ), wxS( "Footprint = ''" ), D_FOOTPRINT },
        { wxS( "LibReference" ),    KIND::FIELD, fields, _( "Library symbol name" ), wxS( "LibReference = 'R'" ), D_LIBREF },
        { wxS( "Description" ),     KIND::FIELD, fields, _( "Component description" ), wxS( "Description Like '*resistor*'" ), D_DESCRIPTION },
        { wxS( "Text" ),            KIND::FIELD, fields, _( "Text of a label, port, string or parameter" ), wxS( "Text = 'VBAT'" ), D_TEXT },
        { wxS( "Name" ),            KIND::FIELD, fields, _( "Name of a pin, sheet entry, sheet symbol or parameter" ), wxS( "Name = 'VCC'" ), D_NAME },
        { wxS( "Electrical" ),      KIND::FIELD, fields, _( "Pin electrical type" ), wxS( "Electrical = 'Power input'" ), D_ELECTRICAL },
        { wxS( "Net" ),             KIND::FIELD, fields, _( "Net of a wire, label or pin" ), wxS( "Net = 'GND'" ), D_NET },
        { wxS( "X" ),               KIND::FIELD, fields, _( "X position" ), wxS( "X > 1000mil" ), D_X },
        { wxS( "Y" ),               KIND::FIELD, fields, _( "Y position" ), wxS( "Y < 500mil" ), D_Y },
        { wxS( "Locked" ),          KIND::FIELD, fields, _( "Whether the object is locked" ), wxS( "Locked = True" ), D_LOCKED },
        { wxS( "PartCount" ),       KIND::FIELD, fields, _( "Number of parts (units) of a component" ), wxS( "PartCount > 1" ), D_PARTCOUNT },
        { wxS( "ObjectKind" ),      KIND::FIELD, fields, _( "Kind of object: Part, Wire, Net Label, Port, Pin, Parameter..." ), wxS( "ObjectKind = 'Wire'" ), D_OBJECTKIND },
    };

    return keys;
}


double SCH_QUERY_ADAPTER::ToInternalUnits( double aValue, const wxString& aUnit ) const
{
    wxString unit = aUnit;

    if( unit.IsEmpty() )
    {
        switch( m_frame->GetUserUnits() )
        {
        case EDA_UNITS::MM:   unit = wxS( "mm" );  break;
        case EDA_UNITS::INCH: unit = wxS( "in" );  break;
        default:              unit = wxS( "mil" ); break;
        }
    }

    if( unit == wxS( "mm" ) )
        return schIUScale.mmToIU( aValue );
    else if( unit == wxS( "um" ) )
        return schIUScale.mmToIU( aValue / 1000.0 );
    else if( unit == wxS( "in" ) )
        return schIUScale.MilsToIU( aValue * 1000.0 );
    else if( unit == wxS( "mil" ) )
        return schIUScale.MilsToIU( aValue );

    return aValue;
}


bool SCH_QUERY_ADAPTER::EvalKeyword( EDA_ITEM* aItem, const KEYWORD& aKey )
{
    SCH_ITEM* item = dynamic_cast<SCH_ITEM*>( aItem );

    if( !item )
        return false;

    KICAD_T type = item->Type();

    switch( aKey.id )
    {
    case K_PART:        return type == SCH_SYMBOL_T && !static_cast<SCH_SYMBOL*>( item )->IsPower();
    case K_POWER:       return type == SCH_SYMBOL_T && static_cast<SCH_SYMBOL*>( item )->IsPower();
    case K_WIRE:        return isLine( item, 0 );
    case K_BUS:         return isLine( item, 1 );
    case K_GRAPHIC:     return isLine( item, 2 ) || type == SCH_SHAPE_T;
    case K_BUSENTRY:    return type == SCH_BUS_WIRE_ENTRY_T || type == SCH_BUS_BUS_ENTRY_T;
    case K_JUNCTION:    return type == SCH_JUNCTION_T;
    case K_NETLABEL:    return type == SCH_LABEL_T;
    case K_PORT:        return type == SCH_HIER_LABEL_T;
    case K_OFFSHEET:    return type == SCH_GLOBAL_LABEL_T;
    case K_SHEETSYMBOL: return type == SCH_SHEET_T;
    case K_SHEETENTRY:  return type == SCH_SHEET_PIN_T;
    case K_PIN:         return type == SCH_PIN_T;
    case K_PARAMETER:   return type == SCH_FIELD_T;
    case K_DESIGNATOR:  return type == SCH_FIELD_T
                               && static_cast<SCH_FIELD*>( item )->GetId() == FIELD_T::REFERENCE;
    case K_COMMENT:     return type == SCH_FIELD_T
                               && static_cast<SCH_FIELD*>( item )->GetId() == FIELD_T::VALUE;
    case K_NOERC:       return type == SCH_NO_CONNECT_T;
    case K_TEXT:        return type == SCH_TEXT_T;
    case K_TEXTFRAME:   return type == SCH_TEXTBOX_T;
    case K_IMAGE:       return type == SCH_BITMAP_T;
    case K_DIRECTIVE:   return type == SCH_DIRECTIVE_LABEL_T;
    case K_SELECTED:    return item->IsSelected();
    case K_LOCKED:      return item->IsLocked();
    case K_HASNOFOOTPRINT:
        return type == SCH_SYMBOL_T && !static_cast<SCH_SYMBOL*>( item )->IsPower()
               && static_cast<SCH_SYMBOL*>( item )
                          ->GetFootprintFieldText( true, &m_sheet, false )
                          .IsEmpty();
    default:            return false;
    }
}


namespace
{

wxString netOf( SCH_ITEM* aItem, const SCH_SHEET_PATH& aSheet )
{
    if( SCH_CONNECTION* conn = aItem->Connection( &aSheet ) )
        return conn->Name( true );

    return wxEmptyString;
}

} // namespace


bool SCH_QUERY_ADAPTER::EvalFunction( EDA_ITEM* aItem, const KEYWORD& aKey,
                                      const std::vector<VALUE>& aArgs )
{
    SCH_ITEM* item = dynamic_cast<SCH_ITEM*>( aItem );

    if( !item || aArgs.empty() )
        return false;

    auto argText =
            []( const VALUE& v ) -> wxString
            {
                return v.type == VALUE::STR ? v.str : wxString::FromCDouble( v.num );
            };

    switch( aKey.id )
    {
    case F_INNET:
    {
        std::vector<SCH_ITEM*> probes;

        // A component is on every net its pins are on
        if( item->Type() == SCH_SYMBOL_T )
        {
            for( SCH_PIN* pin : static_cast<SCH_SYMBOL*>( item )->GetPins( &m_sheet ) )
                probes.push_back( pin );
        }
        else
        {
            probes.push_back( item );
        }

        for( SCH_ITEM* probe : probes )
        {
            wxString net = netOf( probe, m_sheet );

            if( net.IsEmpty() )
                continue;

            for( const VALUE& arg : aArgs )
            {
                wxString pattern = argText( arg );

                // Nets of a sheet are shown with their path (/Power/VBAT); Altium names
                // don't have one
                if( LikeMatch( net, pattern ) || LikeMatch( net.AfterLast( '/' ), pattern ) )
                    return true;
            }
        }

        return false;
    }

    case F_INCOMPONENT:
    {
        SCH_SYMBOL* symbol = owningSymbol( item );

        for( const VALUE& arg : aArgs )
        {
            if( symbol && LikeMatch( symbol->GetRef( &m_sheet ), argText( arg ) ) )
                return true;
        }

        return false;
    }

    case F_HASPARAMETER:
    {
        if( item->Type() != SCH_SYMBOL_T && item->Type() != SCH_SHEET_T )
            return false;

        std::vector<SCH_FIELD*> fieldList;

        if( item->Type() == SCH_SYMBOL_T )
        {
            for( SCH_FIELD& field : static_cast<SCH_SYMBOL*>( item )->GetFields() )
                fieldList.push_back( &field );
        }
        else
        {
            for( SCH_FIELD& field : static_cast<SCH_SHEET*>( item )->GetFields() )
                fieldList.push_back( &field );
        }

        for( SCH_FIELD* field : fieldList )
        {
            if( !LikeMatch( field->GetName(), argText( aArgs[0] ) ) )
                continue;

            if( aArgs.size() < 2 || LikeMatch( field->GetShownText( &m_sheet, false ),
                                               argText( aArgs[1] ) ) )
            {
                return true;
            }
        }

        return false;
    }

    case F_HASFOOTPRINT:
        if( item->Type() != SCH_SYMBOL_T )
            return false;

        return LikeMatch( static_cast<SCH_SYMBOL*>( item )->GetFootprintFieldText( true, &m_sheet,
                                                                                    false ),
                          argText( aArgs[0] ) );

    case F_HASPIN:
        if( item->Type() != SCH_SYMBOL_T )
            return false;

        for( SCH_PIN* pin : static_cast<SCH_SYMBOL*>( item )->GetPins( &m_sheet ) )
        {
            if( LikeMatch( pin->GetShownNumber(), argText( aArgs[0] ) )
                    || LikeMatch( pin->GetShownName(), argText( aArgs[0] ) ) )
            {
                return true;
            }
        }

        return false;

    default:
        return false;
    }
}


VALUE SCH_QUERY_ADAPTER::EvalField( EDA_ITEM* aItem, const KEYWORD& aKey )
{
    SCH_ITEM* item = dynamic_cast<SCH_ITEM*>( aItem );

    if( !item )
        return VALUE();

    KICAD_T     type = item->Type();
    SCH_SYMBOL* symbol = owningSymbol( item );

    switch( aKey.id )
    {
    case D_DESIGNATOR:
        if( type == SCH_PIN_T )
            return VALUE::Str( static_cast<SCH_PIN*>( item )->GetShownNumber() );

        return symbol ? VALUE::Str( symbol->GetRef( &m_sheet ) ) : VALUE();

    case D_COMMENT:
        return symbol ? VALUE::Str( symbol->GetValue( true, &m_sheet, false ) ) : VALUE();

    case D_FOOTPRINT:
        return symbol ? VALUE::Str( symbol->GetFootprintFieldText( true, &m_sheet, false ) )
                      : VALUE();

    case D_LIBREF:
        return symbol ? VALUE::Str( symbol->GetLibId().GetUniStringLibItemName() ) : VALUE();

    case D_DESCRIPTION:
        return symbol ? VALUE::Str( symbol->GetDescription() ) : VALUE();

    case D_TEXT:
        if( type == SCH_FIELD_T )
            return VALUE::Str( static_cast<SCH_FIELD*>( item )->GetShownText( &m_sheet, false ) );

        if( SCH_TEXT* text = dynamic_cast<SCH_TEXT*>( item ) )
            return VALUE::Str( text->GetText() );

        if( type == SCH_TEXTBOX_T )
            return VALUE::Str( static_cast<SCH_TEXTBOX*>( item )->GetText() );

        return VALUE();

    case D_NAME:
        if( type == SCH_PIN_T )
            return VALUE::Str( static_cast<SCH_PIN*>( item )->GetShownName() );

        if( type == SCH_SHEET_PIN_T || type == SCH_LABEL_T || type == SCH_GLOBAL_LABEL_T
                || type == SCH_HIER_LABEL_T )
        {
            return VALUE::Str( static_cast<SCH_TEXT*>( item )->GetText() );
        }

        if( type == SCH_SHEET_T )
            return VALUE::Str( static_cast<SCH_SHEET*>( item )->GetName() );

        if( type == SCH_FIELD_T )
            return VALUE::Str( static_cast<SCH_FIELD*>( item )->GetName() );

        return VALUE();

    case D_ELECTRICAL:
        if( type == SCH_PIN_T )
            return VALUE::Str( static_cast<SCH_PIN*>( item )->GetElectricalTypeName() );

        return VALUE();

    case D_NET:
    {
        wxString net = netOf( item, m_sheet );
        return net.IsEmpty() ? VALUE() : VALUE::Str( net.AfterLast( '/' ) );
    }

    case D_X:
    case D_Y:
    {
        VECTOR2I pos = m_frame->GetOriginTransforms().ToDisplayAbs( item->GetPosition() );
        return VALUE::Num( aKey.id == D_X ? pos.x : pos.y, true );
    }

    case D_LOCKED:
        return VALUE::Bool( item->IsLocked() );

    case D_PARTCOUNT:
        if( type == SCH_SYMBOL_T )
            return VALUE::Num( static_cast<SCH_SYMBOL*>( item )->GetUnitCount(), false );

        return VALUE();

    case D_OBJECTKIND:
    {
        static const std::pair<int, const char*> kinds[] = {
            { K_PART, "Part" },             { K_POWER, "Power Object" },
            { K_WIRE, "Wire" },             { K_BUS, "Bus" },
            { K_BUSENTRY, "Bus Entry" },    { K_JUNCTION, "Junction" },
            { K_NETLABEL, "Net Label" },    { K_PORT, "Port" },
            { K_OFFSHEET, "Off Sheet Connector" }, { K_SHEETSYMBOL, "Sheet Symbol" },
            { K_SHEETENTRY, "Sheet Entry" }, { K_PIN, "Pin" },
            { K_PARAMETER, "Parameter" },   { K_NOERC, "No ERC" },
            { K_TEXT, "Text" },             { K_TEXTFRAME, "Text Frame" },
            { K_GRAPHIC, "Graphic" },       { K_IMAGE, "Image" },
            { K_DIRECTIVE, "Directive" }
        };

        for( const auto& [id, name] : kinds )
        {
            KEYWORD probe;
            probe.id = id;

            if( EvalKeyword( item, probe ) )
                return VALUE::Str( wxString::FromUTF8( name ) );
        }

        return VALUE::Str( item->GetFriendlyName() );
    }

    default:
        return VALUE();
    }
}


VALUE SCH_QUERY_ADAPTER::PropertyField( EDA_ITEM* aItem, const wxString& aName )
{
    VALUE val = ADAPTER::PropertyField( aItem, aName );

    if( val.type == VALUE::NONE )
    {
        SCH_SYMBOL* symbol = owningSymbol( aItem );

        if( symbol && symbol != aItem )
            val = ADAPTER::PropertyField( symbol, aName );

        // A component's own parameter by name (Manufacturer, Tolerance, ...)
        if( val.type == VALUE::NONE && symbol )
        {
            for( SCH_FIELD& field : symbol->GetFields() )
            {
                wxString name = field.GetName();
                name.Replace( wxS( " " ), wxEmptyString );

                if( name.CmpNoCase( aName ) == 0 )
                    return VALUE::Str( field.GetShownText( &m_sheet, false ) );
            }
        }
    }

    return val;
}


// ---- panel ---------------------------------------------------------------------------------

SCH_FILTER_PANEL::SCH_FILTER_PANEL( wxWindow* aParent, SCH_EDIT_FRAME* aFrame ) :
        ALTIUM_FILTER_PANEL( aParent, aFrame, MODE::SCH, m_queryAdapter ),
        m_frame( aFrame ),
        m_queryAdapter( aFrame )
{
}


SCH_FILTER_PANEL::~SCH_FILTER_PANEL()
{
}


void SCH_FILTER_PANEL::OnSchematicChanged()
{
    m_masked.clear();
}


void SCH_FILTER_PANEL::clearMask()
{
    if( m_masked.empty() )
        return;

    KIGFX::VIEW* view = m_frame->GetCanvas()->GetView();
    SCH_SCREENS  screens( m_frame->Schematic().Root() );

    // The mask may be on another sheet than the one shown now
    for( SCH_SCREEN* screen = screens.GetFirst(); screen; screen = screens.GetNext() )
    {
        for( SCH_ITEM* item : screen->Items() )
        {
            auto restore =
                    [&]( SCH_ITEM* aItem )
                    {
                        if( m_masked.count( aItem->m_Uuid ) )
                        {
                            aItem->SetForcedTransparency( 0.0 );
                            return true;
                        }

                        return false;
                    };

            bool changed = restore( item );

            item->RunOnChildren(
                    [&]( SCH_ITEM* child )
                    {
                        changed |= restore( child );
                    },
                    RECURSE_MODE::NO_RECURSE );

            if( changed && screen == m_frame->GetScreen() )
                view->Update( item, KIGFX::REPAINT );
        }
    }

    m_masked.clear();
    m_frame->GetCanvas()->Refresh();
}


int SCH_FILTER_PANEL::applyQuery( ALTIUM_QUERY::QUERY& aQuery, const OPTIONS& aOptions,
                                  wxString& aMessage )
{
    SCH_SCREEN* screen = m_frame->GetScreen();

    if( !screen )
        return 0;

    clearMask();

    SCH_SHEET_PATH& current = m_frame->Schematic().CurrentSheet();

    auto collect =
            []( SCH_SCREEN* aScreen, std::vector<SCH_ITEM*>& aOut )
            {
                for( SCH_ITEM* item : aScreen->Items() )
                {
                    aOut.push_back( item );
                    item->RunOnChildren( [&]( SCH_ITEM* child ) { aOut.push_back( child ); },
                                         RECURSE_MODE::NO_RECURSE );
                }
            };

    auto passesLimit =
            [&]( SCH_ITEM* aItem ) -> bool
            {
                if( aOptions.selectedOnly )
                    return aItem->IsSelected();

                if( aOptions.nonSelectedOnly )
                    return !aItem->IsSelected();

                return true;
            };

    // Other sheets (Whole Project) are only counted: KiCad shows one sheet at a time
    int elsewhere = 0;

    if( aOptions.scope == 2 )
    {
        for( const SCH_SHEET_PATH& path : m_frame->Schematic().Hierarchy() )
        {
            if( path == current || !path.LastScreen() )
                continue;

            m_queryAdapter.SetSheet( path );
            std::vector<SCH_ITEM*> items;
            collect( path.LastScreen(), items );

            for( SCH_ITEM* item : items )
            {
                if( aQuery.Matches( item ) )
                    elsewhere++;
            }
        }
    }

    m_queryAdapter.SetSheet( current );

    std::vector<SCH_ITEM*> all;
    collect( screen, all );

    std::set<SCH_ITEM*>    matched;
    std::vector<SCH_ITEM*> matchList;

    for( SCH_ITEM* item : all )
    {
        if( passesLimit( item ) && aQuery.Matches( item ) )
        {
            matched.insert( item );
            matchList.push_back( item );
        }
    }

    KIGFX::VIEW*  view = m_frame->GetCanvas()->GetView();
    TOOL_MANAGER* toolMgr = m_frame->GetToolManager();

    if( aOptions.mask )
    {
        std::set<SCH_ITEM*> touched;

        for( SCH_ITEM* item : all )
        {
            SCH_ITEM* parent = dynamic_cast<SCH_ITEM*>( item->GetParent() );

            if( matched.count( item ) || ( parent && matched.count( parent ) ) )
                continue;

            // A component whose pin or parameter matches stays in view too
            if( item->Type() == SCH_SYMBOL_T )
            {
                bool childMatched = false;

                item->RunOnChildren(
                        [&]( SCH_ITEM* child )
                        {
                            childMatched |= matched.count( child ) > 0;
                        },
                        RECURSE_MODE::NO_RECURSE );

                if( childMatched )
                    continue;
            }

            item->SetForcedTransparency( aOptions.maskLevel );
            m_masked.insert( item->m_Uuid );
            touched.insert( parent ? parent : item );
        }

        for( SCH_ITEM* item : touched )
            view->Update( item, KIGFX::REPAINT );
    }

    if( aOptions.deselect )
    {
        EDA_ITEMS unselect;

        for( EDA_ITEM* sel : m_frame->GetCurrentSelection() )
        {
            SCH_ITEM* item = dynamic_cast<SCH_ITEM*>( sel );

            if( item && !matched.count( item ) )
                unselect.push_back( item );
        }

        if( !unselect.empty() )
            toolMgr->RunAction<EDA_ITEMS*>( ACTIONS::unselectItems, &unselect );
    }

    if( aOptions.select && !matchList.empty() )
    {
        // A matching component is selected whole: leave out its own pins and fields
        EDA_ITEMS select;

        for( SCH_ITEM* item : matchList )
        {
            SCH_ITEM* parent = dynamic_cast<SCH_ITEM*>( item->GetParent() );

            if( !parent || !matched.count( parent ) )
                select.push_back( item );
        }

        toolMgr->RunAction<EDA_ITEMS*>( ACTIONS::selectItems, &select );
    }

    if( aOptions.zoom && !matchList.empty() )
    {
        BOX2I bbox = matchList.front()->GetBoundingBox();

        for( SCH_ITEM* item : matchList )
            bbox.Merge( item->GetBoundingBox() );

        bbox.Normalize();
        int margin = std::max( { bbox.GetWidth(), bbox.GetHeight() } ) / 4
                     + schIUScale.MilsToIU( 300 );
        bbox.Inflate( margin );

        view->SetViewport( BOX2D( bbox.GetOrigin(), bbox.GetSize() ) );
    }

    m_frame->GetCanvas()->Refresh();

    if( elsewhere > 0 )
    {
        aMessage = wxString::Format( _( "%d objects match here, %d on other sheets" ),
                                     (int) matchList.size(), elsewhere );
    }

    return (int) matchList.size();
}
