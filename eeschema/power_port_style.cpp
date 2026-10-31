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

#include "power_port_style.h"

#include <memory>

#include <lib_symbol.h>
#include <schematic.h>
#include <sch_field.h>
#include <sch_pin.h>
#include <sch_symbol.h>
#include <sch_io/altium/altium_parser_sch.h>
#include <transform.h>


// The Altium importer's power port graphics (sch_io_altium.cpp)
VECTOR2I HelperGeneratePowerPortGraphics( LIB_SYMBOL* aKsymbol, ASCH_POWER_PORT_STYLE aStyle,
                                          REPORTER* aReporter );


namespace POWER_PORT_STYLE
{
const wxString LIB_NICKNAME = wxS( "Altium_Power" );


const std::vector<wxString>& Names()
{
    // Same order as ASCH_POWER_PORT_STYLE
    static const std::vector<wxString> names = {
        _( "Circle" ), _( "Arrow" ), _( "Bar" ), _( "Wave" ), _( "Power Ground" ),
        _( "Signal Ground" ), _( "Earth" ), _( "GOST Arrow" ), _( "GOST Power Ground" ),
        _( "GOST Earth" ), _( "GOST Bar" ) };

    return names;
}


namespace
{
/// Untranslated names used as library item names
const std::vector<wxString>& itemNames()
{
    static const std::vector<wxString> names = {
        wxS( "Circle" ), wxS( "Arrow" ), wxS( "Bar" ), wxS( "Wave" ), wxS( "Power_Ground" ),
        wxS( "Signal_Ground" ), wxS( "Earth" ), wxS( "GOST_Arrow" ), wxS( "GOST_Power_Ground" ),
        wxS( "GOST_Earth" ), wxS( "GOST_Bar" ) };

    return names;
}


/// Direction from the pin (the port's hot spot) to the body of a library symbol
VECTOR2I bodyDirection( const LIB_SYMBOL* aSymbol )
{
    if( !aSymbol )
        return VECTOR2I( 0, 0 );

    BOX2I box = aSymbol->GetBodyBoundingBox( 0, 0, false, false );
    return box.GetCenter();
}
}


int Get( const SCH_SYMBOL* aSymbol )
{
    if( !aSymbol )
        return -1;

    const LIB_ID& id = aSymbol->GetLibId();
    wxString      item = id.GetUniStringLibItemName();

    if( id.GetUniStringLibNickname() == LIB_NICKNAME )
    {
        const std::vector<wxString>& names = itemNames();

        for( size_t ii = 0; ii < names.size(); ++ii )
        {
            if( names[ii] == item )
                return (int) ii;
        }

        return -1;
    }

    // KiCad's power library: the nearest Altium style
    wxString upper = item.Upper();

    if( upper.StartsWith( wxS( "EARTH" ) ) )
        return (int) ASCH_POWER_PORT_STYLE::EARTH;

    if( upper == wxS( "GNDREF" ) )
        return (int) ASCH_POWER_PORT_STYLE::SIGNAL_GROUND;

    if( upper.StartsWith( wxS( "GND" ) ) || upper.StartsWith( wxS( "VSS" ) ) )
        return (int) ASCH_POWER_PORT_STYLE::POWER_GROUND;

    if( upper == wxS( "+VDC" ) || upper == wxS( "-VDC" ) )
        return (int) ASCH_POWER_PORT_STYLE::CIRCLE;

    return (int) ASCH_POWER_PORT_STYLE::ARROW;
}


namespace
{
/// The generated library symbol of a style;  aValuePos receives where its name goes
LIB_SYMBOL* makeLibSymbol( const wxString& net, int aStyle, VECTOR2I& aValuePos )
{
    LIB_ID      libId( LIB_NICKNAME, itemNames()[aStyle] );
    LIB_SYMBOL* libSymbol = new LIB_SYMBOL( itemNames()[aStyle] );
    libSymbol->SetGlobalPower();
    libSymbol->GetReferenceField().SetText( wxS( "#PWR" ) );
    libSymbol->GetReferenceField().SetVisible( false );
    libSymbol->GetValueField().SetText( net );
    libSymbol->GetValueField().SetVisible( true );
    libSymbol->SetDescription( wxString::Format( _( "Power port (%s style)" ), Names()[aStyle] ) );
    libSymbol->SetKeyWords( wxS( "power-flag" ) );
    libSymbol->SetLibId( libId );

    SCH_PIN* pin = new SCH_PIN( libSymbol );
    pin->SetName( net );
    pin->SetNumber( wxS( "1" ) );
    pin->SetPosition( { 0, 0 } );
    pin->SetLength( 0 );
    pin->SetType( ELECTRICAL_PINTYPE::PT_POWER_IN );
    pin->SetVisible( false );
    libSymbol->AddDrawItem( pin, false );

    aValuePos = HelperGeneratePowerPortGraphics( libSymbol, (ASCH_POWER_PORT_STYLE) aStyle, nullptr );
    libSymbol->GetValueField().SetPosition( aValuePos );
    return libSymbol;
}
}


void Apply( SCH_SYMBOL* aSymbol, int aStyle )
{
    if( !aSymbol || aStyle < 0 || aStyle >= (int) itemNames().size() )
        return;

    wxString    net = aSymbol->GetField( FIELD_T::VALUE )->GetText();
    LIB_ID      libId( LIB_NICKNAME, itemNames()[aStyle] );
    VECTOR2I    valuePos;
    LIB_SYMBOL* libSymbol = makeLibSymbol( net, aStyle, valuePos );

    // Keep the port pointing the way it did (Altium keeps the orientation when the style
    // changes; the generated glyphs point down, KiCad's supply symbols up)
    VECTOR2I oldDir = bodyDirection( aSymbol->GetLibSymbolRef().get() );
    VECTOR2I newDir = bodyDirection( libSymbol );
    bool     flip = (int64_t) oldDir.x * newDir.x + (int64_t) oldDir.y * newDir.y < 0;

    aSymbol->SetLibId( libId );
    aSymbol->SetLibSymbol( libSymbol );

    if( flip )
    {
        aSymbol->SetOrientation( SYM_ROTATE_CLOCKWISE );
        aSymbol->SetOrientation( SYM_ROTATE_CLOCKWISE );
    }

    // The name sits where the new glyph wants it, on whichever side the port now points
    SCH_FIELD* value = aSymbol->GetField( FIELD_T::VALUE );
    value->SetPosition( aSymbol->GetPosition()
                        + aSymbol->GetTransform().TransformCoordinate( valuePos ) );
}


SCH_SYMBOL* Create( const wxString& aNet, int aStyle, const SCH_SHEET_PATH* aSheet,
                    SCHEMATIC* aSchematic )
{
    if( aStyle < 0 || aStyle >= (int) itemNames().size() )
        aStyle = (int) ASCH_POWER_PORT_STYLE::BAR;

    VECTOR2I                    valuePos;
    std::unique_ptr<LIB_SYMBOL> libSymbol( makeLibSymbol( aNet, aStyle, valuePos ) );

    SCH_SYMBOL* symbol = new SCH_SYMBOL( *libSymbol, libSymbol->GetLibId(), aSheet, 1, 0,
                                         VECTOR2I( 0, 0 ), aSchematic );
    symbol->GetField( FIELD_T::VALUE )->SetText( aNet );

    // The generated glyphs point down (a ground); a supply points up
    bool ground = aStyle == (int) ASCH_POWER_PORT_STYLE::POWER_GROUND
                  || aStyle == (int) ASCH_POWER_PORT_STYLE::SIGNAL_GROUND
                  || aStyle == (int) ASCH_POWER_PORT_STYLE::EARTH
                  || aStyle == (int) ASCH_POWER_PORT_STYLE::GOST_POWER_GROUND
                  || aStyle == (int) ASCH_POWER_PORT_STYLE::GOST_EARTH;

    if( !ground )
    {
        symbol->SetOrientation( SYM_ROTATE_CLOCKWISE );
        symbol->SetOrientation( SYM_ROTATE_CLOCKWISE );
    }

    SCH_FIELD* value = symbol->GetField( FIELD_T::VALUE );
    value->SetPosition( symbol->GetPosition() + symbol->GetTransform().TransformCoordinate( valuePos ) );
    return symbol;
}
}
