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

#ifndef IPC_LAND_PATTERN_H
#define IPC_LAND_PATTERN_H

#include <vector>
#include <wx/string.h>

/**
 * IPC-7351B land pattern calculation for the IPC Compliant Footprint Wizard.
 *
 * All values are in millimetres, in footprint coordinates (x right, y down, origin at the
 * footprint centre).  The calculator is independent of the UI and of KiCad's board objects.
 */
namespace IPC
{

enum class FAMILY
{
    CHIP,       ///< chip capacitor / resistor / inductor / diode / LED, 2 terminals
    MOLDED,     ///< molded body (tantalum capacitor, inductor, diode), 2 terminals
    MELF,       ///< metal electrode leadless face, 2 terminals
    SOIC,       ///< small outline, gull-wing leads on two sides
    SOT23,      ///< small outline transistor, 3 / 5 / 6 gull-wing leads
    QFP,        ///< quad flat pack, gull-wing leads on four sides
    DFN,        ///< dual flat no-lead
    QFN         ///< quad flat no-lead
};

enum class DENSITY { MOST = 0, NOMINAL, LEAST };   ///< IPC level A / B / C

struct RANGE
{
    double min = 0.0;
    double max = 0.0;
    double Nom() const { return ( min + max ) / 2.0; }
    double Tol() const { return max - min; }
};

/// Optional user value: used only when 'calc' is false.
struct OVERRIDE
{
    bool   calc = true;
    double value = 0.0;
};

struct INPUT
{
    FAMILY  family = FAMILY::CHIP;
    int     packageType = 0;    ///< index into PackageTypes( family )
    int     polarityPin = 0;    ///< 0 none, 1 pin 1, 2 pin 2 (two-terminal parts)

    // Package dimensions (meaning depends on the family, see DimensionLabels())
    RANGE   L;          ///< overall length / lead span, toe to toe
    RANGE   W;          ///< terminal / lead width (body width for chips and MELFs)
    RANGE   T;          ///< terminal / lead length
    RANGE   D;          ///< body length (along the rows of leads)
    RANGE   E;          ///< body width (across the rows)
    double  height = 0.6;
    double  pitch = 1.27;
    int     pins = 2;
    double  epX = 0.0;  ///< exposed pad size (no-lead parts; 0 for none)
    double  epY = 0.0;

    // Heel spacing
    OVERRIDE sMin, sMax;

    // Solder fillets
    DENSITY  density = DENSITY::NOMINAL;
    OVERRIDE toe, heel, side;

    // Component tolerances
    OVERRIDE tolL, tolS, tolW;

    // IPC tolerances
    bool   ipcDefaults = true;
    double fabTol = 0.1;
    double placeTol = 0.1;
    double courtyardExcess = 0.25;

    // Footprint dimensions
    OVERRIDE padX, padY, padC;
    bool     roundedPads = true;

    // Silkscreen
    double   silkWidth = 0.12;
    OVERRIDE silkClearance;     ///< R1: pad to silk gap
    OVERRIDE silkOffset;        ///< R2: outline outside the body edge

    // Courtyard, assembly, body
    bool     addCourtyard = true;
    OVERRIDE courtV1, courtV2;
    double   courtLineWidth = 0.05;
    bool     addAssembly = true;
    OVERRIDE assyA, assyB;
    double   assyLineWidth = 0.1;
    bool     addBody = true;

    // Description
    bool     suggestedName = true;
    wxString name;
    wxString description;
};

struct PAD
{
    wxString number;
    double   x = 0.0, y = 0.0;     ///< centre
    double   sx = 0.0, sy = 0.0;   ///< size along x / y
};

struct SEGMENT
{
    double x1, y1, x2, y2;
};

struct RESULT
{
    bool     ok = false;
    wxString error;

    // Calculated intermediate values (shown on the wizard pages)
    double sMin = 0, sMax = 0;
    double tolL = 0, tolS = 0, tolW = 0;
    double toe = 0, heel = 0, side = 0;
    double fabTol = 0, placeTol = 0, courtyardExcess = 0;
    double padX = 0, padY = 0, padC = 0;     ///< pad width, pad length, row spacing
    double silkClearance = 0, silkOffset = 0;
    double courtV1 = 0, courtV2 = 0;          ///< courtyard size x / y
    double assyA = 0, assyB = 0;              ///< assembly outline size x / y

    std::vector<PAD>     pads;
    std::vector<SEGMENT> silk;
    bool                 hasPin1Dot = false;
    double               pin1X = 0, pin1Y = 0;
    double               bodyX = 0, bodyY = 0;    ///< body outline size (3D model, fab)
    double               bodyHeight = 0;
    bool                 pin1Chamfer = false;      ///< assembly outline gets a pin 1 chamfer

    wxString name;
    wxString description;
};

struct FAMILY_INFO
{
    FAMILY   family;
    wxString name;
    wxString description;
    wxString packages;
};

const std::vector<FAMILY_INFO>& Families();
std::vector<wxString>           PackageTypes( FAMILY aFamily );
bool                            IsTwoTerminal( FAMILY aFamily );
bool                            IsNoLead( FAMILY aFamily );
bool                            IsQuad( FAMILY aFamily );

/// Default package values for a family (the wizard starts from these).
INPUT                           Defaults( FAMILY aFamily );

/// Default (toe, heel, side) fillets and courtyard excess for a family, pitch and density.
void DefaultFillets( FAMILY aFamily, double aPitch, DENSITY aDensity, double& aToe,
                     double& aHeel, double& aSide, double& aCourtyard );

RESULT Calculate( const INPUT& aInput );

} // namespace IPC

#endif // IPC_LAND_PATTERN_H
