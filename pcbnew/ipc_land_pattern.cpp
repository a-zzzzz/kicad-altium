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

#include "ipc_land_pattern.h"

#include <algorithm>
#include <cmath>

#include <wx/intl.h>


namespace IPC
{

namespace
{
/// IPC rounds land dimensions to 0.05 mm (to the nearest step, as Altium's wizard does).
double round05( double aValue )
{
    return std::round( aValue / 0.05 ) * 0.05;
}


/// Courtyard sizes are rounded up to the 0.01 mm grid.
double ceil01( double aValue )
{
    return std::ceil( aValue / 0.01 - 1e-9 ) * 0.01;
}


double rss( double a, double b, double c )
{
    return std::sqrt( a * a + b * b + c * c );
}


double pick( const OVERRIDE& aOverride, double aCalculated )
{
    return aOverride.calc ? aCalculated : aOverride.value;
}


/// Two digits of a dimension in tenths of a millimetre (chip naming: 2.0 x 1.25 -> "2012").
wxString tenths( double aValue )
{
    return wxString::Format( wxS( "%02d" ), (int) std::floor( aValue * 10.0 + 1e-6 ) );
}


/// A dimension in hundredths of a millimetre (leaded naming: 1.27 -> "127").
wxString hundredths( double aValue )
{
    return wxString::Format( wxS( "%d" ), (int) std::lround( aValue * 100.0 ) );
}


wxString densityLetter( DENSITY aDensity )
{
    switch( aDensity )
    {
    case DENSITY::MOST:  return wxS( "M" );
    case DENSITY::LEAST: return wxS( "L" );
    default:             return wxS( "N" );
    }
}


wxString densityWords( DENSITY aDensity )
{
    switch( aDensity )
    {
    case DENSITY::MOST:  return _( "IPC Low Density" );
    case DENSITY::LEAST: return _( "IPC High Density" );
    default:             return _( "IPC Medium Density" );
    }
}


/// Subtracts the interval [a, b] from each interval of a list.
void subtract( std::vector<std::pair<double, double>>& aList, double a, double b )
{
    std::vector<std::pair<double, double>> out;

    for( const auto& [lo, hi] : aList )
    {
        if( b <= lo || a >= hi )
        {
            out.emplace_back( lo, hi );
            continue;
        }

        if( a > lo )
            out.emplace_back( lo, a );

        if( b < hi )
            out.emplace_back( b, hi );
    }

    aList = std::move( out );
}


/// Adds an axis-aligned silkscreen line, cut where it would come too close to a pad.
void addClippedLine( RESULT& aResult, double x1, double y1, double x2, double y2, double aKeepOut )
{
    bool   horizontal = std::abs( y1 - y2 ) < 1e-9;
    double lo = horizontal ? std::min( x1, x2 ) : std::min( y1, y2 );
    double hi = horizontal ? std::max( x1, x2 ) : std::max( y1, y2 );
    double fixed = horizontal ? y1 : x1;

    std::vector<std::pair<double, double>> parts{ { lo, hi } };

    for( const PAD& pad : aResult.pads )
    {
        double minX = pad.x - pad.sx / 2 - aKeepOut, maxX = pad.x + pad.sx / 2 + aKeepOut;
        double minY = pad.y - pad.sy / 2 - aKeepOut, maxY = pad.y + pad.sy / 2 + aKeepOut;

        if( horizontal && fixed > minY && fixed < maxY )
            subtract( parts, minX, maxX );
        else if( !horizontal && fixed > minX && fixed < maxX )
            subtract( parts, minY, maxY );
    }

    for( const auto& [a, b] : parts )
    {
        if( b - a < 0.2 )
            continue;

        if( horizontal )
            aResult.silk.push_back( { a, fixed, b, fixed } );
        else
            aResult.silk.push_back( { fixed, a, fixed, b } );
    }
}
} // namespace


const std::vector<FAMILY_INFO>& Families()
{
    static const std::vector<FAMILY_INFO> families = {
        { FAMILY::CHIP,   wxS( "CHIP" ),   _( "Chip Components, 2-Pins" ),
          _( "Capacitor, Inductor, Resistor, Diode, LED" ) },
        { FAMILY::DFN,    wxS( "DFN" ),    _( "Dual Flat No-lead" ), wxS( "DFN" ) },
        { FAMILY::MELF,   wxS( "MELF" ),   _( "MELF Components, 2-Pins" ), _( "Diode, Resistor" ) },
        { FAMILY::MOLDED, wxS( "MOLDED" ), _( "Molded Components, 2-Pins" ),
          _( "Capacitor, Inductor, Diode" ) },
        { FAMILY::QFN,    wxS( "QFN" ),    _( "Quad Flat No-Lead" ), wxS( "QFN, LLP" ) },
        { FAMILY::QFP,    wxS( "QFP" ),    _( "Quad Flat Pack" ), wxS( "QFP, LQFP, TQFP" ) },
        { FAMILY::SOIC,   wxS( "SOIC" ),   _( "Small Outline Package, Gullwing Leads" ),
          wxS( "SOIC, SOP, SSOP, TSSOP" ) },
        { FAMILY::SOT23,  wxS( "SOT23" ),  _( "Small Outline Transistor" ),
          wxS( "SOT23-3, SOT23-5, SOT23-6" ) },
    };

    return families;
}


std::vector<wxString> PackageTypes( FAMILY aFamily )
{
    switch( aFamily )
    {
    case FAMILY::CHIP:
        return { _( "Chip Capacitor" ), _( "Chip Resistor" ), _( "Chip Inductor" ),
                 _( "Chip Diode" ), _( "Chip LED" ) };
    case FAMILY::MOLDED:
        return { _( "Molded Capacitor, Polarized" ), _( "Molded Inductor" ), _( "Molded Diode" ) };
    case FAMILY::MELF:
        return { _( "MELF Resistor" ), _( "MELF Diode" ) };
    default:
        return {};
    }
}


bool IsTwoTerminal( FAMILY aFamily )
{
    return aFamily == FAMILY::CHIP || aFamily == FAMILY::MOLDED || aFamily == FAMILY::MELF;
}


bool IsNoLead( FAMILY aFamily )
{
    return aFamily == FAMILY::DFN || aFamily == FAMILY::QFN;
}


bool IsQuad( FAMILY aFamily )
{
    return aFamily == FAMILY::QFP || aFamily == FAMILY::QFN;
}


INPUT Defaults( FAMILY aFamily )
{
    INPUT in;
    in.family = aFamily;

    switch( aFamily )
    {
    case FAMILY::CHIP:      // 0402 capacitor (Altium's starting values)
        in.L = { 0.9, 1.1 };   in.W = { 0.4, 0.6 };   in.T = { 0.1, 0.3 };
        in.height = 0.6;       in.pins = 2;
        break;

    case FAMILY::MOLDED:    // tantalum case A (3216)
        in.L = { 3.0, 3.4 };   in.W = { 1.1, 1.3 };   in.T = { 0.5, 1.1 };
        in.E = { 1.4, 1.8 };   in.height = 1.8;       in.pins = 2;
        in.polarityPin = 1;
        break;

    case FAMILY::MELF:      // MiniMELF (SOD-80)
        in.L = { 3.3, 3.7 };   in.W = { 1.4, 1.6 };   in.T = { 0.25, 0.5 };
        in.height = 1.6;       in.pins = 2;
        break;

    case FAMILY::SOIC:      // SOIC-8, 3.9 mm body
        in.L = { 5.8, 6.2 };   in.W = { 0.31, 0.51 }; in.T = { 0.4, 1.27 };
        in.D = { 4.8, 5.0 };   in.E = { 3.8, 4.0 };
        in.height = 1.75;      in.pitch = 1.27;       in.pins = 8;
        break;

    case FAMILY::SOT23:     // SOT23-3
        in.L = { 2.1, 2.64 };  in.W = { 0.3, 0.5 };   in.T = { 0.2, 0.6 };
        in.D = { 2.8, 3.04 };  in.E = { 1.2, 1.4 };
        in.height = 1.12;      in.pitch = 0.95;       in.pins = 3;
        break;

    case FAMILY::QFP:       // LQFP-64, 10 x 10 mm
        in.L = { 11.8, 12.2 }; in.W = { 0.17, 0.27 }; in.T = { 0.45, 0.75 };
        in.D = { 9.8, 10.2 };
        in.height = 1.6;       in.pitch = 0.5;        in.pins = 64;
        break;

    case FAMILY::DFN:       // DFN-8, 3 x 3 mm
        in.L = { 2.9, 3.1 };   in.W = { 0.25, 0.35 }; in.T = { 0.3, 0.5 };
        in.D = { 2.9, 3.1 };
        in.height = 1.0;       in.pitch = 0.65;       in.pins = 8;
        in.epX = 1.6;          in.epY = 2.4;
        break;

    case FAMILY::QFN:       // QFN-32, 5 x 5 mm
        in.L = { 4.9, 5.1 };   in.W = { 0.18, 0.3 };  in.T = { 0.3, 0.5 };
        in.height = 1.0;       in.pitch = 0.5;        in.pins = 32;
        in.epX = 3.1;          in.epY = 3.1;
        break;
    }

    double toe, heel, side;
    DefaultFillets( aFamily, in.pitch, in.density, toe, heel, side, in.courtyardExcess );
    return in;
}


void DefaultFillets( FAMILY aFamily, double aPitch, DENSITY aDensity, double& aToe,
                     double& aHeel, double& aSide, double& aCourtyard )
{
    // IPC-7351B solder fillet goals, { most (A), nominal (B), least (C) }
    struct GOALS
    {
        double toe[3], heel[3], side[3];
    };

    static const GOALS chip     = { { 0.55, 0.35, 0.15 }, { 0.0, 0.0, 0.0 },    { 0.05, 0.0, -0.05 } };
    static const GOALS molded   = { { 0.25, 0.15, 0.07 }, { 0.8, 0.5, 0.2 },    { 0.01, -0.05, -0.1 } };
    static const GOALS melf     = { { 0.6, 0.4, 0.2 },    { 0.2, 0.1, 0.02 },   { 0.1, 0.05, 0.01 } };
    static const GOALS gull     = { { 0.55, 0.35, 0.15 }, { 0.45, 0.35, 0.25 }, { 0.05, 0.03, 0.01 } };
    static const GOALS gullFine = { { 0.55, 0.35, 0.15 }, { 0.45, 0.35, 0.25 }, { 0.01, -0.02, -0.04 } };
    static const GOALS noLead   = { { 0.4, 0.3, 0.2 },    { 0.0, 0.0, 0.0 },    { -0.04, -0.04, -0.04 } };

    const GOALS* goals = &chip;

    switch( aFamily )
    {
    case FAMILY::CHIP:   goals = &chip;                              break;
    case FAMILY::MOLDED: goals = &molded;                            break;
    case FAMILY::MELF:   goals = &melf;                              break;
    case FAMILY::DFN:
    case FAMILY::QFN:    goals = &noLead;                            break;
    default:             goals = aPitch > 0.625 ? &gull : &gullFine; break;
    }

    int i = (int) aDensity;
    aToe = goals->toe[i];
    aHeel = goals->heel[i];
    aSide = goals->side[i];

    static const double courtyard[3] = { 0.5, 0.25, 0.1 };
    aCourtyard = courtyard[i];
}


RESULT Calculate( const INPUT& aIn )
{
    RESULT r;
    FAMILY fam = aIn.family;

    auto bad =
            [&]( const wxString& aMsg )
            {
                r.ok = false;
                r.error = aMsg;
                return r;
            };

    // --- checks ----------------------------------------------------------------------------
    if( aIn.L.min <= 0 || aIn.L.max < aIn.L.min )
        return bad( _( "The overall length range is not valid." ) );

    if( aIn.W.min <= 0 || aIn.W.max < aIn.W.min )
        return bad( _( "The terminal width range is not valid." ) );

    if( aIn.T.min <= 0 || aIn.T.max < aIn.T.min )
        return bad( _( "The terminal length range is not valid." ) );

    if( !IsTwoTerminal( fam ) )
    {
        if( aIn.pitch <= 0 )
            return bad( _( "The pitch must be greater than zero." ) );

        if( fam == FAMILY::SOT23 && aIn.pins != 3 && aIn.pins != 5 && aIn.pins != 6 )
            return bad( _( "SOT23 packages have 3, 5 or 6 pins." ) );

        if( IsQuad( fam ) && ( aIn.pins < 4 || aIn.pins % 4 ) )
            return bad( _( "Quad packages need a multiple of 4 pins." ) );

        if( ( fam == FAMILY::SOIC || fam == FAMILY::DFN ) && ( aIn.pins < 2 || aIn.pins % 2 ) )
            return bad( _( "Dual packages need an even number of pins." ) );
    }

    // --- component tolerances and heel spacing ---------------------------------------------
    double ct = aIn.T.Tol();

    r.tolL = pick( aIn.tolL, aIn.L.Tol() );
    r.tolW = pick( aIn.tolW, aIn.W.Tol() );
    r.tolS = pick( aIn.tolS, std::sqrt( aIn.L.Tol() * aIn.L.Tol() + 2 * ct * ct ) );

    r.sMin = pick( aIn.sMin, aIn.L.min - 2 * aIn.T.max );
    r.sMax = pick( aIn.sMax, r.sMin + r.tolS );

    // --- fillets and IPC tolerances --------------------------------------------------------
    double toe, heel, side, courtyard;
    DefaultFillets( fam, aIn.pitch, aIn.density, toe, heel, side, courtyard );

    r.toe = pick( aIn.toe, toe );
    r.heel = pick( aIn.heel, heel );
    r.side = pick( aIn.side, side );

    r.fabTol = aIn.ipcDefaults ? 0.1 : aIn.fabTol;
    r.placeTol = aIn.ipcDefaults ? 0.1 : aIn.placeTol;
    r.courtyardExcess = aIn.ipcDefaults ? courtyard : aIn.courtyardExcess;

    double F = r.fabTol, P = r.placeTol;

    // --- land pattern (IPC-7351B) ----------------------------------------------------------
    double Z = round05( aIn.L.min + 2 * r.toe + rss( r.tolL, F, P ) );
    double G = round05( r.sMax - 2 * r.heel - rss( r.tolS, F, P ) );
    double X = round05( aIn.W.min + 2 * r.side + rss( r.tolW, F, P ) );

    r.padX = pick( aIn.padX, X );
    r.padY = pick( aIn.padY, ( Z - G ) / 2 );
    r.padC = pick( aIn.padC, ( Z + G ) / 2 );

    if( r.padX <= 0 || r.padY <= 0 )
        return bad( _( "The calculated pads have no size; check the package dimensions." ) );

    if( r.padC - r.padY < 0.05 )
        return bad( _( "The pads of opposite rows overlap; check the package dimensions." ) );

    if( !IsTwoTerminal( fam ) && r.padX >= aIn.pitch )
        return bad( _( "Neighbouring pads overlap: the pad width is not smaller than the pitch." ) );

    // --- pads ------------------------------------------------------------------------------
    double c = r.padC / 2, e = aIn.pitch;

    auto addPad =
            [&]( int aNumber, double x, double y, bool aAcross )
            {
                PAD pad;
                pad.number = wxString::Format( wxS( "%d" ), aNumber );
                pad.x = x;
                pad.y = y;
                pad.sx = aAcross ? r.padX : r.padY;
                pad.sy = aAcross ? r.padY : r.padX;
                r.pads.push_back( pad );
            };

    if( IsTwoTerminal( fam ) )
    {
        addPad( 1, -c, 0, false );
        addPad( 2, c, 0, false );
    }
    else if( fam == FAMILY::SOT23 )
    {
        if( aIn.pins == 3 )
        {
            addPad( 1, -c, -e, false );
            addPad( 2, -c, e, false );
            addPad( 3, c, 0, false );
        }
        else
        {
            addPad( 1, -c, -e, false );
            addPad( 2, -c, 0, false );
            addPad( 3, -c, e, false );
            addPad( 4, c, e, false );

            if( aIn.pins == 5 )
            {
                addPad( 5, c, -e, false );
            }
            else
            {
                addPad( 5, c, 0, false );
                addPad( 6, c, -e, false );
            }
        }
    }
    else if( IsQuad( fam ) )
    {
        int    n = aIn.pins / 4;
        double first = -( n - 1 ) / 2.0 * e;
        int    pin = 1;

        for( int k = 0; k < n; ++k )
            addPad( pin++, -c, first + k * e, false );    // left, top to bottom

        for( int k = 0; k < n; ++k )
            addPad( pin++, first + k * e, c, true );      // bottom, left to right

        for( int k = 0; k < n; ++k )
            addPad( pin++, c, -first - k * e, false );    // right, bottom to top

        for( int k = 0; k < n; ++k )
            addPad( pin++, -first - k * e, -c, true );    // top, right to left
    }
    else
    {
        int    n = aIn.pins / 2;
        double first = -( n - 1 ) / 2.0 * e;
        int    pin = 1;

        for( int k = 0; k < n; ++k )
            addPad( pin++, -c, first + k * e, false );    // left, top to bottom

        for( int k = 0; k < n; ++k )
            addPad( pin++, c, -first - k * e, false );    // right, bottom to top
    }

    if( IsNoLead( fam ) && aIn.epX > 0 && aIn.epY > 0 )
    {
        PAD ep;
        ep.number = wxString::Format( wxS( "%d" ), aIn.pins + 1 );
        ep.sx = aIn.epX;
        ep.sy = aIn.epY;
        r.pads.push_back( ep );
    }

    // --- body ------------------------------------------------------------------------------
    switch( fam )
    {
    case FAMILY::CHIP:
    case FAMILY::MELF:   r.bodyX = aIn.L.Nom(); r.bodyY = aIn.W.Nom(); break;
    case FAMILY::MOLDED: r.bodyX = aIn.L.Nom(); r.bodyY = aIn.E.Nom(); break;
    case FAMILY::SOIC:
    case FAMILY::SOT23:  r.bodyX = aIn.E.Nom(); r.bodyY = aIn.D.Nom(); break;
    case FAMILY::QFP:    r.bodyX = aIn.D.Nom(); r.bodyY = aIn.D.Nom(); break;
    case FAMILY::DFN:    r.bodyX = aIn.L.Nom(); r.bodyY = aIn.D.Nom(); break;
    case FAMILY::QFN:    r.bodyX = aIn.L.Nom(); r.bodyY = aIn.L.Nom(); break;
    }

    r.bodyHeight = aIn.height;
    r.pin1Chamfer = !IsTwoTerminal( fam );

    // --- silkscreen ------------------------------------------------------------------------
    r.silkClearance = pick( aIn.silkClearance, 0.2 );
    r.silkOffset = pick( aIn.silkOffset, 0.0 );

    double keepOut = r.silkClearance + aIn.silkWidth / 2;
    double sx = r.bodyX / 2 + r.silkOffset + aIn.silkWidth / 2;
    double sy = r.bodyY / 2 + r.silkOffset + aIn.silkWidth / 2;

    addClippedLine( r, -sx, -sy, sx, -sy, keepOut );
    addClippedLine( r, -sx, sy, sx, sy, keepOut );
    addClippedLine( r, -sx, -sy, -sx, sy, keepOut );
    addClippedLine( r, sx, -sy, sx, sy, keepOut );

    double minX = -r.bodyX / 2, maxX = r.bodyX / 2, minY = -r.bodyY / 2, maxY = r.bodyY / 2;

    for( const PAD& pad : r.pads )
    {
        minX = std::min( minX, pad.x - pad.sx / 2 );
        maxX = std::max( maxX, pad.x + pad.sx / 2 );
        minY = std::min( minY, pad.y - pad.sy / 2 );
        maxY = std::max( maxY, pad.y + pad.sy / 2 );
    }

    if( IsTwoTerminal( fam ) && aIn.polarityPin > 0 )
    {
        // A bar beside the polarity pin
        const PAD& pad = r.pads[aIn.polarityPin - 1];
        double     x = pad.x < 0 ? pad.x - pad.sx / 2 - keepOut : pad.x + pad.sx / 2 + keepOut;
        double     y = std::max( pad.sy / 2, r.bodyY / 2 ) + r.silkOffset;

        r.silk.push_back( { x, -y, x, y } );
        minX = std::min( minX, x - aIn.silkWidth / 2 );
        maxX = std::max( maxX, x + aIn.silkWidth / 2 );
    }
    else if( !IsTwoTerminal( fam ) && !r.pads.empty() )
    {
        // A dot beside pin 1
        const PAD& pad = r.pads.front();
        r.hasPin1Dot = true;
        r.pin1X = pad.x - pad.sx / 2 - keepOut - 0.15;
        r.pin1Y = pad.y;
        minX = std::min( minX, r.pin1X - 0.15 );
    }

    // --- courtyard and assembly (centred on the footprint) ---------------------------------
    double extX = std::max( -minX, maxX );
    double extY = std::max( -minY, maxY );

    r.courtV1 = pick( aIn.courtV1, ceil01( 2 * ( extX + r.courtyardExcess ) ) );
    r.courtV2 = pick( aIn.courtV2, ceil01( 2 * ( extY + r.courtyardExcess ) ) );
    r.assyA = pick( aIn.assyA, r.bodyX );
    r.assyB = pick( aIn.assyB, r.bodyY );

    // --- name and description --------------------------------------------------------------
    wxString              type;
    std::vector<wxString> types = PackageTypes( fam );

    if( aIn.packageType >= 0 && aIn.packageType < (int) types.size() )
        type = types[aIn.packageType];

    switch( fam )
    {
    case FAMILY::CHIP:
    {
        static const wxString prefix[] = { wxS( "CAPC" ), wxS( "RESC" ), wxS( "INDC" ),
                                           wxS( "DIOC" ), wxS( "LEDC" ) };
        r.name = prefix[std::clamp( aIn.packageType, 0, 4 )] + tenths( aIn.L.Nom() )
                 + tenths( aIn.W.Nom() ) + wxS( "X" ) + tenths( aIn.height );
        break;
    }

    case FAMILY::MOLDED:
    {
        static const wxString prefix[] = { wxS( "CAPPM" ), wxS( "INDM" ), wxS( "DIOM" ) };
        r.name = prefix[std::clamp( aIn.packageType, 0, 2 )] + tenths( aIn.L.Nom() )
                 + tenths( aIn.E.Nom() ) + wxS( "X" ) + tenths( aIn.height );
        break;
    }

    case FAMILY::MELF:
    {
        static const wxString prefix[] = { wxS( "RESMELF" ), wxS( "DIOMELF" ) };
        r.name = prefix[std::clamp( aIn.packageType, 0, 1 )] + tenths( aIn.L.Nom() )
                 + tenths( aIn.W.Nom() );
        break;
    }

    case FAMILY::SOIC:
        r.name = wxS( "SOIC" ) + hundredths( aIn.pitch ) + wxS( "P" ) + hundredths( aIn.L.Nom() )
                 + wxS( "X" ) + hundredths( aIn.height )
                 + wxString::Format( wxS( "-%d" ), aIn.pins );
        type = _( "Small Outline Package" );
        break;

    case FAMILY::SOT23:
        r.name = wxS( "SOT" ) + hundredths( aIn.pitch ) + wxS( "P" ) + hundredths( aIn.L.Nom() )
                 + wxS( "X" ) + hundredths( aIn.height )
                 + wxString::Format( wxS( "-%d" ), aIn.pins );
        type = _( "Small Outline Transistor" );
        break;

    case FAMILY::QFP:
        r.name = wxS( "QFP" ) + hundredths( aIn.pitch ) + wxS( "P" ) + hundredths( aIn.L.Nom() )
                 + wxS( "X" ) + hundredths( aIn.L.Nom() ) + wxS( "X" ) + hundredths( aIn.height )
                 + wxString::Format( wxS( "-%d" ), aIn.pins );
        type = _( "Quad Flat Pack" );
        break;

    case FAMILY::DFN:
    case FAMILY::QFN:
    {
        bool ep = aIn.epX > 0 && aIn.epY > 0;
        r.name = ( fam == FAMILY::DFN ? wxS( "DFN" ) : wxS( "QFN" ) ) + hundredths( aIn.pitch )
                 + wxS( "P" ) + hundredths( r.bodyY ) + wxS( "X" ) + hundredths( r.bodyX )
                 + wxS( "X" ) + hundredths( aIn.height )
                 + wxString::Format( wxS( "-%d" ), aIn.pins + ( ep ? 1 : 0 ) );
        type = fam == FAMILY::DFN ? _( "Dual Flat No-Lead" ) : _( "Quad Flat No-Lead" );
        break;
    }
    }

    r.name += densityLetter( aIn.density );

    if( !aIn.suggestedName && !aIn.name.IsEmpty() )
        r.name = aIn.name;

    r.description = wxString::Format( _( "%s, %d-Leads, Body %.2fx%.2fmm, %s" ), type,
                                      (int) r.pads.size(), std::max( r.bodyX, r.bodyY ),
                                      std::min( r.bodyX, r.bodyY ), densityWords( aIn.density ) );

    if( !IsTwoTerminal( fam ) )
        r.description += wxString::Format( _( ", Pitch %.2fmm" ), aIn.pitch );

    if( !aIn.suggestedName && !aIn.description.IsEmpty() )
        r.description = aIn.description;

    r.ok = true;
    return r;
}

} // namespace IPC
