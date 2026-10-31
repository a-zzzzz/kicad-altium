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

#include "pcb_filter_panel.h"

#include <board.h>
#include <board_connected_item.h>
#include <footprint.h>
#include <netclass.h>
#include <pad.h>
#include <pcb_edit_frame.h>
#include <pcb_field.h>
#include <pcb_shape.h>
#include <pcb_text.h>
#include <pcb_textbox.h>
#include <pcb_track.h>
#include <zone.h>
#include <tool/actions.h>
#include <tool/tool_manager.h>
#include <tool/selection.h>
#include <view/view.h>
#include <widgets/altium_layer_tabs.h>
#include <widgets/pcb_panel_utils.h>

using namespace ALTIUM_QUERY;


namespace
{

enum ID
{
    // object kinds
    K_COMPONENT = 1, K_PAD, K_VIA, K_TRACK, K_ARC, K_FILL, K_REGION, K_POLYGON, K_TEXT,
    K_DIMENSION, K_KEEPOUT, K_DESIGNATOR, K_COMMENT, K_SMDPAD, K_THRUHOLEPAD, K_FREE,
    K_ANYNET, K_ANYCOMPONENT, K_SELECTED, K_LOCKED, K_ONTOP, K_ONBOTTOM, K_ONMULTI,
    K_ONSIGNAL, K_ONELECTRICAL, K_ONCOMPONENTLAYER, K_ONMECHANICAL, K_BOARDOUTLINE,

    // functions
    F_INNET = 100, F_INNETCLASS, F_INCOMPONENT, F_HASFOOTPRINT, F_ONLAYER, F_INNETS,

    // fields
    D_NET = 200, D_NETCLASS, D_DESIGNATOR, D_COMMENT, D_FOOTPRINT, D_NAME, D_LAYER, D_WIDTH,
    D_DIAMETER, D_HOLESIZE, D_XSIZE, D_YSIZE, D_X, D_Y, D_ROTATION, D_LOCKED, D_TEXT,
    D_LENGTH, D_OBJECTKIND
};


BOARD_CONNECTED_ITEM* connected( EDA_ITEM* aItem )
{
    if( BOARD_ITEM* item = dynamic_cast<BOARD_ITEM*>( aItem ) )
    {
        if( item->IsConnected() )
            return static_cast<BOARD_CONNECTED_ITEM*>( item );
    }

    return nullptr;
}


FOOTPRINT* owningFootprint( EDA_ITEM* aItem )
{
    if( aItem->Type() == PCB_FOOTPRINT_T )
        return static_cast<FOOTPRINT*>( aItem );

    if( BOARD_ITEM* item = dynamic_cast<BOARD_ITEM*>( aItem ) )
        return item->GetParentFootprint();

    return nullptr;
}


bool isShape( EDA_ITEM* aItem, std::initializer_list<SHAPE_T> aShapes )
{
    if( aItem->Type() != PCB_SHAPE_T )
        return false;

    SHAPE_T shape = static_cast<PCB_SHAPE*>( aItem )->GetShape();

    for( SHAPE_T s : aShapes )
    {
        if( shape == s )
            return true;
    }

    return false;
}


wxString itemText( EDA_ITEM* aItem )
{
    switch( aItem->Type() )
    {
    case PCB_TEXT_T:
    case PCB_FIELD_T:    return static_cast<PCB_TEXT*>( aItem )->GetText();
    case PCB_TEXTBOX_T:  return static_cast<PCB_TEXTBOX*>( aItem )->GetText();
    default:             return wxEmptyString;
    }
}


wxString squash( const wxString& aName )
{
    wxString out;

    for( wxUniChar c : aName )
    {
        if( c != ' ' && c != '-' && c != '_' && c != '.' )
            out += c;
    }

    return out.Lower();
}

} // namespace


// ---- adapter -------------------------------------------------------------------------------

PCB_QUERY_ADAPTER::PCB_QUERY_ADAPTER( PCB_EDIT_FRAME* aFrame ) :
        m_frame( aFrame )
{
}


size_t PCB_QUERY_ADAPTER::propertyBaseType() const
{
    return TYPE_HASH( BOARD_ITEM );
}


const std::vector<KEYWORD>& PCB_QUERY_ADAPTER::Keywords() const
{
    static const wxString objects = _( "Object Type Checks" );
    static const wxString member = _( "Membership Checks" );
    static const wxString layers = _( "Layer Checks" );
    static const wxString state = _( "State Checks" );
    static const wxString fields = _( "Object Properties" );

    static const std::vector<KEYWORD> keys = {
        { wxS( "IsComponent" ),   KIND::KEYWORD, objects, _( "Components (footprints)" ), wxS( "IsComponent" ), K_COMPONENT },
        { wxS( "IsPad" ),         KIND::KEYWORD, objects, _( "Pads" ), wxS( "IsPad" ), K_PAD },
        { wxS( "IsVia" ),         KIND::KEYWORD, objects, _( "Vias" ), wxS( "IsVia" ), K_VIA },
        { wxS( "IsTrack" ),       KIND::KEYWORD, objects, _( "Tracks: straight copper tracks and lines on any layer" ), wxS( "IsTrack" ), K_TRACK },
        { wxS( "IsArc" ),         KIND::KEYWORD, objects, _( "Arcs and circles" ), wxS( "IsArc" ), K_ARC },
        { wxS( "IsFill" ),        KIND::KEYWORD, objects, _( "Fills (filled rectangles)" ), wxS( "IsFill" ), K_FILL },
        { wxS( "IsRegion" ),      KIND::KEYWORD, objects, _( "Regions (filled polygons)" ), wxS( "IsRegion" ), K_REGION },
        { wxS( "IsPolygon" ),     KIND::KEYWORD, objects, _( "Polygon pours (copper zones)" ), wxS( "IsPolygon" ), K_POLYGON },
        { wxS( "IsText" ),        KIND::KEYWORD, objects, _( "Strings, designators, comments and text frames" ), wxS( "IsText" ), K_TEXT },
        { wxS( "IsDimension" ),   KIND::KEYWORD, objects, _( "Dimensions" ), wxS( "IsDimension" ), K_DIMENSION },
        { wxS( "IsKeepOut" ),     KIND::KEYWORD, objects, _( "Keepouts (rule areas)" ), wxS( "IsKeepOut" ), K_KEEPOUT },
        { wxS( "IsDesignator" ),  KIND::KEYWORD, objects, _( "Component designator strings" ), wxS( "IsDesignator" ), K_DESIGNATOR },
        { wxS( "IsComment" ),     KIND::KEYWORD, objects, _( "Component comment (value) strings" ), wxS( "IsComment" ), K_COMMENT },
        { wxS( "IsSMDPad" ),      KIND::KEYWORD, objects, _( "Surface-mount pads" ), wxS( "IsSMDPad" ), K_SMDPAD },
        { wxS( "IsThruHolePad" ), KIND::KEYWORD, objects, _( "Through-hole pads" ), wxS( "IsThruHolePad" ), K_THRUHOLEPAD },
        { wxS( "IsBoardOutline" ), KIND::KEYWORD, objects, _( "Board outline (Edge.Cuts) primitives" ), wxS( "IsBoardOutline" ), K_BOARDOUTLINE },

        { wxS( "InNet" ),         KIND::FUNCTION, member, _( "Objects on the named net (wildcards allowed)" ), wxS( "InNet('GND')" ), F_INNET },
        { wxS( "InNets" ),        KIND::FUNCTION, member, _( "Objects on any of the named nets" ), wxS( "InNets('GND','VCC')" ), F_INNETS },
        { wxS( "InAnyNet" ),      KIND::KEYWORD,  member, _( "Objects on any net" ), wxS( "InAnyNet" ), K_ANYNET },
        { wxS( "InNetClass" ),    KIND::FUNCTION, member, _( "Objects on a net of the net class" ), wxS( "InNetClass('Power')" ), F_INNETCLASS },
        { wxS( "InComponent" ),   KIND::FUNCTION, member, _( "The component with this designator and its primitives" ), wxS( "InComponent('U1')" ), F_INCOMPONENT },
        { wxS( "InAnyComponent" ), KIND::KEYWORD, member, _( "Primitives that belong to a component" ), wxS( "InAnyComponent" ), K_ANYCOMPONENT },
        { wxS( "IsFree" ),        KIND::KEYWORD,  member, _( "Free primitives (not part of a component)" ), wxS( "IsFree" ), K_FREE },
        { wxS( "HasFootprint" ),  KIND::FUNCTION, member, _( "Components with this footprint (wildcards allowed)" ), wxS( "HasFootprint('SOIC*')" ), F_HASFOOTPRINT },

        { wxS( "OnLayer" ),       KIND::FUNCTION, layers, _( "Objects on the layer" ), wxS( "OnLayer('Top Layer')" ), F_ONLAYER },
        { wxS( "OnTopLayer" ),    KIND::KEYWORD,  layers, _( "Objects on the top side" ), wxS( "OnTopLayer" ), K_ONTOP },
        { wxS( "OnBottomLayer" ), KIND::KEYWORD,  layers, _( "Objects on the bottom side" ), wxS( "OnBottomLayer" ), K_ONBOTTOM },
        { wxS( "OnMultiLayer" ),  KIND::KEYWORD,  layers, _( "Through-hole pads and vias (Multi-Layer)" ), wxS( "OnMultiLayer" ), K_ONMULTI },
        { wxS( "OnSignalLayer" ), KIND::KEYWORD,  layers, _( "Objects on a copper layer" ), wxS( "OnSignalLayer" ), K_ONSIGNAL },
        { wxS( "OnElectricalLayer" ), KIND::KEYWORD, layers, _( "Objects on a copper layer (signal or plane)" ), wxS( "OnElectricalLayer" ), K_ONELECTRICAL },
        { wxS( "OnComponentLayer" ), KIND::KEYWORD, layers, _( "Objects on the top or bottom copper, overlay, solder or paste layers" ), wxS( "OnComponentLayer" ), K_ONCOMPONENTLAYER },
        { wxS( "OnMechanical" ),  KIND::KEYWORD,  layers, _( "Objects on a mechanical (user) layer" ), wxS( "OnMechanical" ), K_ONMECHANICAL },

        { wxS( "IsSelected" ),    KIND::KEYWORD,  state, _( "Selected objects" ), wxS( "IsSelected" ), K_SELECTED },
        { wxS( "IsLocked" ),      KIND::KEYWORD,  state, _( "Locked objects" ), wxS( "IsLocked" ), K_LOCKED },

        { wxS( "Net" ),           KIND::FIELD, fields, _( "Net name" ), wxS( "Net = 'GND'" ), D_NET },
        { wxS( "NetClass" ),      KIND::FIELD, fields, _( "Net class name" ), wxS( "NetClass = 'Default'" ), D_NETCLASS },
        { wxS( "Designator" ),    KIND::FIELD, fields, _( "Component designator (of the component a primitive belongs to)" ), wxS( "Designator Like 'R*'" ), D_DESIGNATOR },
        { wxS( "Comment" ),       KIND::FIELD, fields, _( "Component comment (value)" ), wxS( "Comment = '10k'" ), D_COMMENT },
        { wxS( "Footprint" ),     KIND::FIELD, fields, _( "Component footprint name" ), wxS( "Footprint Like '0603*'" ), D_FOOTPRINT },
        { wxS( "Name" ),          KIND::FIELD, fields, _( "Pad designator, component designator or text" ), wxS( "Name = '1'" ), D_NAME },
        { wxS( "Layer" ),         KIND::FIELD, fields, _( "Layer name (Altium's: Top Layer, Mid-Layer 1, Top Overlay, Multi-Layer...)" ), wxS( "Layer = 'Top Layer'" ), D_LAYER },
        { wxS( "Width" ),         KIND::FIELD, fields, _( "Track, arc or line width" ), wxS( "Width > 10mil" ), D_WIDTH },
        { wxS( "Diameter" ),      KIND::FIELD, fields, _( "Via diameter" ), wxS( "Diameter = 0.6mm" ), D_DIAMETER },
        { wxS( "HoleSize" ),      KIND::FIELD, fields, _( "Via or pad hole size" ), wxS( "HoleSize < 0.3mm" ), D_HOLESIZE },
        { wxS( "XSize" ),         KIND::FIELD, fields, _( "Pad X size" ), wxS( "XSize > 1mm" ), D_XSIZE },
        { wxS( "YSize" ),         KIND::FIELD, fields, _( "Pad Y size" ), wxS( "YSize > 1mm" ), D_YSIZE },
        { wxS( "X" ),             KIND::FIELD, fields, _( "X position" ), wxS( "X > 1000mil" ), D_X },
        { wxS( "Y" ),             KIND::FIELD, fields, _( "Y position" ), wxS( "Y < 500mil" ), D_Y },
        { wxS( "Rotation" ),      KIND::FIELD, fields, _( "Rotation in degrees" ), wxS( "Rotation = 90" ), D_ROTATION },
        { wxS( "Locked" ),        KIND::FIELD, fields, _( "Whether the object is locked" ), wxS( "Locked = True" ), D_LOCKED },
        { wxS( "Text" ),          KIND::FIELD, fields, _( "Text of a string" ), wxS( "Text Like '*REV*'" ), D_TEXT },
        { wxS( "Length" ),        KIND::FIELD, fields, _( "Length of a track or arc" ), wxS( "Length > 1in" ), D_LENGTH },
        { wxS( "ObjectKind" ),    KIND::FIELD, fields, _( "Kind of object: Component, Pad, Via, Track, Arc, Fill, Region, Polygon, Text, Dimension" ), wxS( "ObjectKind = 'Pad'" ), D_OBJECTKIND },
    };

    return keys;
}


double PCB_QUERY_ADAPTER::ToInternalUnits( double aValue, const wxString& aUnit ) const
{
    wxString unit = aUnit;

    if( unit.IsEmpty() )
    {
        switch( m_frame->GetUserUnits() )
        {
        case EDA_UNITS::MM:   unit = wxS( "mm" );  break;
        case EDA_UNITS::INCH: unit = wxS( "in" );  break;
        case EDA_UNITS::UM:   unit = wxS( "um" );  break;
        default:              unit = wxS( "mil" ); break;
        }
    }

    if( unit == wxS( "mm" ) )
        return pcbIUScale.mmToIU( aValue );
    else if( unit == wxS( "um" ) )
        return pcbIUScale.mmToIU( aValue / 1000.0 );
    else if( unit == wxS( "in" ) )
        return pcbIUScale.MilsToIU( aValue * 1000.0 );
    else if( unit == wxS( "mil" ) )
        return pcbIUScale.MilsToIU( aValue );

    return aValue;
}


wxString PCB_QUERY_ADAPTER::LayerName( PCB_LAYER_ID aLayer ) const
{
    BOARD* board = m_frame->GetBoard();
    return ALTIUM_LAYER_TABS::DisplayName( aLayer, board ? board->GetLayerName( aLayer )
                                                         : LSET::Name( aLayer ) );
}


LSET PCB_QUERY_ADAPTER::ResolveLayers( const wxString& aName ) const
{
    BOARD*   board = m_frame->GetBoard();
    wxString wanted = squash( aName );

    if( wanted == wxS( "multilayer" ) )
        return LSET::AllCuMask();

    if( !board )
        return LSET();

    for( PCB_LAYER_ID layer : board->GetEnabledLayers().Seq() )
    {
        if( squash( LayerName( layer ) ) == wanted || squash( board->GetLayerName( layer ) ) == wanted
                || squash( LSET::Name( layer ) ) == wanted
                || squash( BOARD::GetStandardLayerName( layer ) ) == wanted )
        {
            return LSET( { layer } );
        }
    }

    // Altium's own names for layers the board calls something else
    static const std::map<wxString, PCB_LAYER_ID> altium = {
        { wxS( "toplayer" ), F_Cu },           { wxS( "bottomlayer" ), B_Cu },
        { wxS( "topoverlay" ), F_SilkS },      { wxS( "bottomoverlay" ), B_SilkS },
        { wxS( "topsolder" ), F_Mask },        { wxS( "bottomsolder" ), B_Mask },
        { wxS( "toppaste" ), F_Paste },        { wxS( "bottompaste" ), B_Paste },
        { wxS( "boardoutline" ), Edge_Cuts },  { wxS( "keepoutlayer" ), Margin },
    };

    auto it = altium.find( wanted );

    if( it != altium.end() )
        return LSET( { it->second } );

    if( wanted.StartsWith( wxS( "midlayer" ) ) )
    {
        long n = 0;

        if( wanted.Mid( 8 ).ToLong( &n ) && n >= 1 && n <= 30 )
            return LSET( { ToLAYER_ID( In1_Cu + 2 * ( n - 1 ) ) } );
    }

    return LSET();
}


bool PCB_QUERY_ADAPTER::EvalKeyword( EDA_ITEM* aItem, const KEYWORD& aKey )
{
    BOARD_ITEM* item = dynamic_cast<BOARD_ITEM*>( aItem );

    if( !item )
        return false;

    KICAD_T type = item->Type();

    switch( aKey.id )
    {
    case K_COMPONENT:    return type == PCB_FOOTPRINT_T;
    case K_PAD:          return type == PCB_PAD_T;
    case K_VIA:          return type == PCB_VIA_T;
    case K_TRACK:        return type == PCB_TRACE_T || isShape( item, { SHAPE_T::SEGMENT } );
    case K_ARC:          return type == PCB_ARC_T || isShape( item, { SHAPE_T::ARC, SHAPE_T::CIRCLE } );
    case K_FILL:         return isShape( item, { SHAPE_T::RECTANGLE } )
                                && static_cast<PCB_SHAPE*>( item )->IsSolidFill();
    case K_REGION:       return isShape( item, { SHAPE_T::POLY } )
                                && static_cast<PCB_SHAPE*>( item )->IsSolidFill();
    case K_POLYGON:      return type == PCB_ZONE_T && !static_cast<ZONE*>( item )->GetIsRuleArea();
    case K_TEXT:         return type == PCB_TEXT_T || type == PCB_FIELD_T || type == PCB_TEXTBOX_T;
    case K_DIMENSION:    return BaseType( type ) == PCB_DIMENSION_T;
    case K_KEEPOUT:      return type == PCB_ZONE_T && static_cast<ZONE*>( item )->GetIsRuleArea();
    case K_DESIGNATOR:   return type == PCB_FIELD_T && static_cast<PCB_FIELD*>( item )->IsReference();
    case K_COMMENT:      return type == PCB_FIELD_T && static_cast<PCB_FIELD*>( item )->IsValue();
    case K_SMDPAD:       return type == PCB_PAD_T
                                && static_cast<PAD*>( item )->GetAttribute() == PAD_ATTRIB::SMD;
    case K_THRUHOLEPAD:  return type == PCB_PAD_T
                                && ( static_cast<PAD*>( item )->GetAttribute() == PAD_ATTRIB::PTH
                                     || static_cast<PAD*>( item )->GetAttribute() == PAD_ATTRIB::NPTH );
    case K_BOARDOUTLINE: return item->GetLayer() == Edge_Cuts && type != PCB_FOOTPRINT_T;
    case K_FREE:         return type != PCB_FOOTPRINT_T && !item->GetParentFootprint();
    case K_ANYCOMPONENT: return type != PCB_FOOTPRINT_T && item->GetParentFootprint();
    case K_ANYNET:
    {
        BOARD_CONNECTED_ITEM* con = connected( item );
        return con && con->GetNetCode() > 0;
    }
    case K_SELECTED:     return item->IsSelected();
    case K_LOCKED:       return item->IsLocked();
    case K_ONTOP:        return item->IsOnLayer( F_Cu ) || item->IsOnLayer( F_SilkS )
                                || item->IsOnLayer( F_Mask ) || item->IsOnLayer( F_Paste )
                                || item->IsOnLayer( F_CrtYd ) || item->IsOnLayer( F_Fab );
    case K_ONBOTTOM:     return item->IsOnLayer( B_Cu ) || item->IsOnLayer( B_SilkS )
                                || item->IsOnLayer( B_Mask ) || item->IsOnLayer( B_Paste )
                                || item->IsOnLayer( B_CrtYd ) || item->IsOnLayer( B_Fab );
    case K_ONMULTI:      return ( type == PCB_VIA_T || type == PCB_PAD_T )
                                && ( item->GetLayerSet() & LSET::AllCuMask() ).count() > 1;
    case K_ONSIGNAL:
    case K_ONELECTRICAL: return type != PCB_FOOTPRINT_T
                                && ( item->GetLayerSet() & LSET::AllCuMask() ).any();
    case K_ONCOMPONENTLAYER:
        return type != PCB_FOOTPRINT_T
               && ( item->GetLayerSet()
                    & LSET( { F_Cu, B_Cu, F_SilkS, B_SilkS, F_Mask, B_Mask, F_Paste, B_Paste } ) )
                          .any();
    case K_ONMECHANICAL: return type != PCB_FOOTPRINT_T
                                && ( item->GetLayerSet() & LSET::UserMask() ).any();
    default:             return false;
    }
}


bool PCB_QUERY_ADAPTER::EvalFunction( EDA_ITEM* aItem, const KEYWORD& aKey,
                                      const std::vector<VALUE>& aArgs )
{
    BOARD_ITEM* item = dynamic_cast<BOARD_ITEM*>( aItem );

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
    case F_INNETS:
    {
        BOARD_CONNECTED_ITEM* con = connected( item );

        if( !con || con->GetNetCode() <= 0 )
            return false;

        for( const VALUE& arg : aArgs )
        {
            if( LikeMatch( con->GetNetname(), argText( arg ) ) )
                return true;
        }

        return false;
    }

    case F_INNETCLASS:
    {
        BOARD_CONNECTED_ITEM* con = connected( item );

        if( !con || con->GetNetCode() <= 0 || !con->GetEffectiveNetClass() )
            return false;

        return LikeMatch( con->GetEffectiveNetClass()->GetName(), argText( aArgs[0] ) )
               || con->GetEffectiveNetClass()->ContainsNetclassWithName( argText( aArgs[0] ) );
    }

    case F_INCOMPONENT:
    {
        FOOTPRINT* fp = owningFootprint( item );

        for( const VALUE& arg : aArgs )
        {
            if( fp && LikeMatch( fp->GetReference(), argText( arg ) ) )
                return true;
        }

        return false;
    }

    case F_HASFOOTPRINT:
    {
        if( item->Type() != PCB_FOOTPRINT_T )
            return false;

        const LIB_ID& id = static_cast<FOOTPRINT*>( item )->GetFPID();

        for( const VALUE& arg : aArgs )
        {
            if( LikeMatch( id.GetUniStringLibItemName(), argText( arg ) )
                    || LikeMatch( id.Format().wx_str(), argText( arg ) ) )
            {
                return true;
            }
        }

        return false;
    }

    case F_ONLAYER:
    {
        if( item->Type() == PCB_FOOTPRINT_T )
        {
            // A component is on the side it is placed on
            for( const VALUE& arg : aArgs )
            {
                LSET layers = ResolveLayers( argText( arg ) );

                if( ( layers & LSET( { item->GetLayer() } ) ).any() )
                    return true;
            }

            return false;
        }

        for( const VALUE& arg : aArgs )
        {
            wxString name = argText( arg );

            if( squash( name ) == wxS( "multilayer" ) )
            {
                if( ( item->GetLayerSet() & LSET::AllCuMask() ).count() > 1 )
                    return true;

                continue;
            }

            if( ( item->GetLayerSet() & ResolveLayers( name ) ).any() )
                return true;
        }

        return false;
    }

    default:
        return false;
    }
}


VALUE PCB_QUERY_ADAPTER::EvalField( EDA_ITEM* aItem, const KEYWORD& aKey )
{
    BOARD_ITEM* item = dynamic_cast<BOARD_ITEM*>( aItem );

    if( !item )
        return VALUE();

    KICAD_T    type = item->Type();
    FOOTPRINT* fp = owningFootprint( item );

    switch( aKey.id )
    {
    case D_NET:
        if( BOARD_CONNECTED_ITEM* con = connected( item ) )
            return VALUE::Str( con->GetNetCode() > 0 ? con->GetNetname() : wxString( wxS( "No Net" ) ) );

        return VALUE();

    case D_NETCLASS:
        if( BOARD_CONNECTED_ITEM* con = connected( item ) )
        {
            if( con->GetEffectiveNetClass() )
                return VALUE::Str( con->GetEffectiveNetClass()->GetName() );
        }

        return VALUE();

    case D_DESIGNATOR:
        return fp ? VALUE::Str( fp->GetReference() ) : VALUE();

    case D_COMMENT:
        return fp ? VALUE::Str( fp->GetValue() ) : VALUE();

    case D_FOOTPRINT:
        return fp ? VALUE::Str( fp->GetFPID().GetUniStringLibItemName() ) : VALUE();

    case D_NAME:
        if( type == PCB_PAD_T )
            return VALUE::Str( static_cast<PAD*>( item )->GetNumber() );

        if( type == PCB_FOOTPRINT_T )
            return VALUE::Str( static_cast<FOOTPRINT*>( item )->GetReference() );

        if( type == PCB_ZONE_T )
            return VALUE::Str( static_cast<ZONE*>( item )->GetZoneName() );

        if( !itemText( item ).IsEmpty() )
            return VALUE::Str( itemText( item ) );

        return VALUE();

    case D_LAYER:
        if( ( type == PCB_VIA_T || type == PCB_PAD_T )
                && ( item->GetLayerSet() & LSET::AllCuMask() ).count() > 1 )
        {
            return VALUE::Str( wxS( "Multi-Layer" ) );
        }

        return VALUE::Str( LayerName( item->GetLayer() ) );

    case D_WIDTH:
        if( type == PCB_TRACE_T || type == PCB_ARC_T )
            return VALUE::Num( static_cast<PCB_TRACK*>( item )->GetWidth(), true );

        if( type == PCB_SHAPE_T )
            return VALUE::Num( static_cast<PCB_SHAPE*>( item )->GetWidth(), true );

        return VALUE();

    case D_DIAMETER:
        if( type == PCB_VIA_T )
            return VALUE::Num( static_cast<PCB_VIA*>( item )->GetWidth( F_Cu ), true );

        return VALUE();

    case D_HOLESIZE:
        if( type == PCB_VIA_T )
            return VALUE::Num( static_cast<PCB_VIA*>( item )->GetDrillValue(), true );

        if( type == PCB_PAD_T && static_cast<PAD*>( item )->HasHole() )
            return VALUE::Num( static_cast<PAD*>( item )->GetDrillSizeX(), true );

        return VALUE();

    case D_XSIZE:
    case D_YSIZE:
        if( type == PCB_PAD_T )
        {
            VECTOR2I size = static_cast<PAD*>( item )->GetSize( PADSTACK::ALL_LAYERS );
            return VALUE::Num( aKey.id == D_XSIZE ? size.x : size.y, true );
        }

        return VALUE();

    case D_X:
    case D_Y:
    {
        VECTOR2I pos = m_frame->GetOriginTransforms().ToDisplayAbs( item->GetPosition() );
        return VALUE::Num( aKey.id == D_X ? pos.x : pos.y, true );
    }

    case D_ROTATION:
        if( type == PCB_FOOTPRINT_T )
            return VALUE::Num( static_cast<FOOTPRINT*>( item )->GetOrientation().AsDegrees(), false );

        if( type == PCB_PAD_T )
            return VALUE::Num( static_cast<PAD*>( item )->GetOrientation().AsDegrees(), false );

        if( type == PCB_TEXT_T || type == PCB_FIELD_T )
            return VALUE::Num( static_cast<PCB_TEXT*>( item )->GetTextAngle().AsDegrees(), false );

        return VALUE();

    case D_LOCKED:
        return VALUE::Bool( item->IsLocked() );

    case D_TEXT:
        if( type == PCB_TEXT_T || type == PCB_FIELD_T || type == PCB_TEXTBOX_T )
            return VALUE::Str( itemText( item ) );

        return VALUE();

    case D_LENGTH:
        if( type == PCB_TRACE_T || type == PCB_ARC_T )
            return VALUE::Num( static_cast<PCB_TRACK*>( item )->GetLength(), true );

        return VALUE();

    case D_OBJECTKIND:
    {
        static const std::pair<int, const char*> kinds[] = {
            { K_COMPONENT, "Component" }, { K_PAD, "Pad" },         { K_VIA, "Via" },
            { K_TRACK, "Track" },         { K_ARC, "Arc" },         { K_FILL, "Fill" },
            { K_REGION, "Region" },       { K_POLYGON, "Polygon" }, { K_TEXT, "Text" },
            { K_DIMENSION, "Dimension" }, { K_KEEPOUT, "Keepout" }
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


VALUE PCB_QUERY_ADAPTER::PropertyField( EDA_ITEM* aItem, const wxString& aName )
{
    VALUE val = ADAPTER::PropertyField( aItem, aName );

    // A primitive answers for its component's properties (Reference, Value, ...)
    if( val.type == VALUE::NONE )
    {
        if( BOARD_ITEM* item = dynamic_cast<BOARD_ITEM*>( aItem ) )
        {
            if( FOOTPRINT* fp = item->GetParentFootprint() )
                val = ADAPTER::PropertyField( fp, aName );
        }
    }

    return val;
}


// ---- panel ---------------------------------------------------------------------------------

PCB_FILTER_PANEL::PCB_FILTER_PANEL( wxWindow* aParent, PCB_EDIT_FRAME* aFrame ) :
        ALTIUM_FILTER_PANEL( aParent, aFrame, MODE::PCB, m_queryAdapter ),
        m_frame( aFrame ),
        m_queryAdapter( aFrame )
{
    RebuildLayerList();
}


PCB_FILTER_PANEL::~PCB_FILTER_PANEL()
{
}


void PCB_FILTER_PANEL::OnBoardChanged()
{
    m_masked.clear();
    RebuildLayerList();
}


std::vector<wxString> PCB_FILTER_PANEL::builderLayers() const
{
    std::vector<wxString> names;
    BOARD*                board = m_frame->GetBoard();

    if( !board )
        return names;

    // Altium's order: paste, overlay, solder, copper top to bottom, then the bottom side
    const PCB_LAYER_ID topSide[] = { F_Paste, F_SilkS, F_Mask };
    const PCB_LAYER_ID bottomSide[] = { B_Mask, B_SilkS, B_Paste };
    LSET               enabled = board->GetEnabledLayers();

    for( PCB_LAYER_ID layer : topSide )
    {
        if( enabled.Contains( layer ) )
            names.push_back( m_queryAdapter.LayerName( layer ) );
    }

    for( PCB_LAYER_ID layer : enabled.CuStack() )
        names.push_back( m_queryAdapter.LayerName( layer ) );

    for( PCB_LAYER_ID layer : bottomSide )
    {
        if( enabled.Contains( layer ) )
            names.push_back( m_queryAdapter.LayerName( layer ) );
    }

    for( PCB_LAYER_ID layer : enabled.UIOrder() )
    {
        wxString name = m_queryAdapter.LayerName( layer );

        if( std::find( names.begin(), names.end(), name ) == names.end() )
            names.push_back( name );
    }

    return names;
}


void PCB_FILTER_PANEL::clearMask()
{
    BOARD* board = m_frame->GetBoard();

    if( !board || m_masked.empty() )
    {
        m_masked.clear();
        return;
    }

    KIGFX::VIEW* view = m_frame->GetCanvas()->GetView();

    for( const KIID& id : m_masked )
    {
        if( BOARD_ITEM* item = board->ResolveItem( id, true ) )
        {
            item->SetForcedTransparency( 0.0 );
            view->Update( item, KIGFX::REPAINT );
        }
    }

    m_masked.clear();
    m_frame->GetCanvas()->Refresh();
}


int PCB_FILTER_PANEL::applyQuery( ALTIUM_QUERY::QUERY& aQuery, const OPTIONS& aOptions,
                                  wxString& aMessage )
{
    BOARD* board = m_frame->GetBoard();

    if( !board )
        return 0;

    clearMask();

    // Every object the panel can see: components and their primitives, free primitives
    std::vector<BOARD_ITEM*> all;

    for( FOOTPRINT* fp : board->Footprints() )
    {
        all.push_back( fp );
        fp->RunOnChildren( [&]( BOARD_ITEM* child ) { all.push_back( child ); },
                           RECURSE_MODE::NO_RECURSE );
    }

    for( PCB_TRACK* track : board->Tracks() )
        all.push_back( track );

    for( BOARD_ITEM* drawing : board->Drawings() )
        all.push_back( drawing );

    for( ZONE* zone : board->Zones() )
        all.push_back( zone );

    std::set<BOARD_ITEM*> candidates;

    if( aOptions.selectedOnly )
    {
        for( EDA_ITEM* sel : m_frame->GetCurrentSelection() )
        {
            if( BOARD_ITEM* item = dynamic_cast<BOARD_ITEM*>( sel ) )
            {
                candidates.insert( item );

                if( item->Type() == PCB_FOOTPRINT_T )
                {
                    static_cast<FOOTPRINT*>( item )->RunOnChildren(
                            [&]( BOARD_ITEM* child ) { candidates.insert( child ); },
                            RECURSE_MODE::NO_RECURSE );
                }
            }
        }
    }

    std::set<BOARD_ITEM*>    matched;
    std::vector<BOARD_ITEM*> matchList;

    for( BOARD_ITEM* item : all )
    {
        if( aOptions.selectedOnly && !candidates.count( item ) )
            continue;

        if( aQuery.Matches( item ) )
        {
            matched.insert( item );
            matchList.push_back( item );
        }
    }

    KIGFX::VIEW*  view = m_frame->GetCanvas()->GetView();
    TOOL_MANAGER* toolMgr = m_frame->GetToolManager();

    // Mask: everything not matching (nor part of a matching component) fades out
    if( aOptions.mask )
    {
        for( BOARD_ITEM* item : all )
        {
            FOOTPRINT* parent = item->GetParentFootprint();

            if( matched.count( item ) || ( parent && matched.count( parent ) ) )
                continue;

            item->SetForcedTransparency( aOptions.maskLevel );
            view->Update( item, KIGFX::REPAINT );
            m_masked.insert( item->m_Uuid );
        }
    }

    if( aOptions.deselect )
    {
        EDA_ITEMS unselect;

        for( EDA_ITEM* sel : m_frame->GetCurrentSelection() )
        {
            BOARD_ITEM* item = dynamic_cast<BOARD_ITEM*>( sel );

            if( item && !matched.count( item ) )
                unselect.push_back( item );
        }

        if( !unselect.empty() )
            toolMgr->RunAction<EDA_ITEMS*>( ACTIONS::unselectItems, &unselect );
    }

    if( aOptions.select && !matchList.empty() )
    {
        // Selecting a component selects it whole: leave out its own primitives
        EDA_ITEMS select;

        for( BOARD_ITEM* item : matchList )
        {
            FOOTPRINT* parent = item->GetParentFootprint();

            if( !parent || !matched.count( parent ) )
                select.push_back( item );
        }

        toolMgr->RunAction<EDA_ITEMS*>( ACTIONS::selectItems, &select );
    }

    if( aOptions.zoom && !matchList.empty() )
    {
        BOX2I bbox = matchList.front()->GetBoundingBox();

        for( BOARD_ITEM* item : matchList )
            bbox.Merge( item->GetBoundingBox() );

        PcbZoomToBox( m_frame, bbox );
    }

    m_frame->GetCanvas()->Refresh();
    return (int) matchList.size();
}
