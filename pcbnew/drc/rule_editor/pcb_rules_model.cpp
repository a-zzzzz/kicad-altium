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

#include "pcb_rules_model.h"

#include <algorithm>
#include <map>
#include <set>

#include <wx/ffile.h>
#include <wx/regex.h>
#include <wx/tokenzr.h>

#include <base_units.h>
#include <eda_units.h>
#include <board.h>
#include <board_design_settings.h>
#include <netclass.h>
#include <project/net_settings.h>
#include <kiplatform/io.h>
#include <drc/drc_rule.h>
#include <drc/drc_rule_condition.h>
#include <drc/drc_rule_parser.h>
#include <richio.h>


namespace PCB_RULES
{

// ------------------------------------------------------------------------------------------
// Category table
// ------------------------------------------------------------------------------------------

static FIELD len( const wxString& aLabel, const wxString& aKeyword, PART aPart )
{
    return FIELD{ aLabel, aKeyword, aPart, FIELD_KIND::LENGTH, wxEmptyString };
}


static FIELD cnt( const wxString& aLabel, const wxString& aKeyword, PART aPart )
{
    return FIELD{ aLabel, aKeyword, aPart, FIELD_KIND::COUNT, wxEmptyString };
}


static CATEGORY unsupported( const wxString& aGroup, const wxString& aName, const wxString& aNote )
{
    CATEGORY c;
    c.id = aGroup + wxS( "/" ) + aName;
    c.group = aGroup;
    c.name = aName;
    c.defaultRuleName = aName;
    c.supported = false;
    c.binary = false;
    c.note = aNote;
    return c;
}


static CATEGORY cat( const wxString& aId, const wxString& aGroup, const wxString& aName,
                     const wxString& aDefaultName, bool aBinary,
                     const std::vector<wxString>& aKeywords, const std::vector<FIELD>& aFields,
                     const std::vector<wxString>& aImplicit = {}, const wxString& aNote = {} )
{
    CATEGORY c;
    c.id = aId;
    c.group = aGroup;
    c.name = aName;
    c.defaultRuleName = aDefaultName;
    c.supported = true;
    c.binary = aBinary;
    c.keywords = aKeywords;
    c.fields = aFields;
    c.implicitTerms = aImplicit;
    c.note = aNote;
    return c;
}


const std::vector<CATEGORY>& Categories()
{
    static const std::vector<CATEGORY> s_categories = []()
    {
        std::vector<CATEGORY> v;

        const wxString E = _( "Electrical" );
        const wxString R = _( "Routing" );
        const wxString S = _( "SMT" );
        const wxString M = _( "Mask" );
        const wxString P = _( "Plane" );
        const wxString T = _( "Testpoint" );
        const wxString F = _( "Manufacturing" );
        const wxString H = _( "High Speed" );
        const wxString L = _( "Placement" );
        const wxString I = _( "Signal Integrity" );

        const wxString noEquiv = _( "KiCad has no rule type for this; it is listed so the tree "
                                    "matches the familiar layout." );

        // Electrical
        v.push_back( cat( wxS( "clearance" ), E, _( "Clearance" ), wxS( "Clearance" ), true,
                          { wxS( "clearance" ) },
                          { len( _( "Minimum Clearance" ), wxS( "clearance" ), PART::MIN ) },
                          {},
                          _( "Copper-to-copper clearance between objects of different nets. "
                             "Net class clearances also apply; a matching rule overrides them." ) ) );
        v.push_back( unsupported( E, _( "Short-Circuit" ),
                                  _( "KiCad always reports shorts between different nets "
                                     "(DRC: Items shorting two nets); there is no rule to allow "
                                     "them." ) ) );
        v.push_back( unsupported( E, _( "Un-Routed Net" ),
                                  _( "KiCad always reports unrouted connections (DRC: Missing "
                                     "connection between items); severity is set in Board Setup "
                                     "> Violation Severity." ) ) );
        v.push_back( cat( wxS( "hole_clearance" ), E, _( "Hole Clearance" ),
                          wxS( "HoleClearance" ), true, { wxS( "hole_clearance" ) },
                          { len( _( "Minimum Clearance" ), wxS( "hole_clearance" ), PART::MIN ) },
                          {}, _( "Clearance from a drilled hole to copper of another net "
                                 "(KiCad rule type)." ) ) );
        v.push_back( cat( wxS( "creepage" ), E, _( "Creepage" ), wxS( "Creepage" ), true,
                          { wxS( "creepage" ) },
                          { len( _( "Minimum Creepage" ), wxS( "creepage" ), PART::MIN ) },
                          {}, _( "Surface creepage distance between nets (KiCad rule type)." ) ) );

        // Routing
        v.push_back( cat( wxS( "width" ), R, _( "Width" ), wxS( "Width" ), false,
                          { wxS( "track_width" ) },
                          { len( _( "Min Width" ), wxS( "track_width" ), PART::MIN ),
                            len( _( "Preferred Width" ), wxS( "track_width" ), PART::OPT ),
                            len( _( "Max Width" ), wxS( "track_width" ), PART::MAX ) },
                          {},
                          _( "The interactive router uses the preferred width.  Net class track "
                             "widths also apply; a matching rule overrides them." ) ) );
        v.push_back( unsupported( R, _( "Routing Topology" ), noEquiv ) );
        v.push_back( unsupported( R, _( "Routing Priority" ), noEquiv ) );
        v.push_back( unsupported( R, _( "Routing Layers" ),
                                  _( "Use keep-out rule areas, or a Custom rule with "
                                     "(constraint disallow track) and a layer clause." ) ) );
        v.push_back( unsupported( R, _( "Routing Corners" ),
                                  _( "Corner style is chosen while routing (Shift+Space).  A "
                                     "Custom rule can use (constraint track_angle ...)." ) ) );
        v.push_back( cat( wxS( "via_style" ), R, _( "Routing Via Style" ), wxS( "RoutingVias" ),
                          false, { wxS( "via_diameter" ), wxS( "hole_size" ) },
                          { len( _( "Via Diameter Minimum" ), wxS( "via_diameter" ), PART::MIN ),
                            len( _( "Via Diameter Preferred" ), wxS( "via_diameter" ), PART::OPT ),
                            len( _( "Via Diameter Maximum" ), wxS( "via_diameter" ), PART::MAX ),
                            len( _( "Via Hole Size Minimum" ), wxS( "hole_size" ), PART::MIN ),
                            len( _( "Via Hole Size Preferred" ), wxS( "hole_size" ), PART::OPT ),
                            len( _( "Via Hole Size Maximum" ), wxS( "hole_size" ), PART::MAX ) },
                          { wxS( "A.Type == 'Via'" ) },
                          _( "The router places vias with the preferred sizes." ) ) );
        v.push_back( unsupported( R, _( "Fanout Control" ),
                                  _( "Fanout is not automated in KiCad." ) ) );
        v.push_back( cat( wxS( "diff_pairs" ), R, _( "Differential Pairs Routing" ),
                          wxS( "DiffPairsRouting" ), false,
                          { wxS( "diff_pair_gap" ), wxS( "diff_pair_uncoupled" ) },
                          { len( _( "Min Width" ), wxS( "track_width" ), PART::MIN ),
                            len( _( "Preferred Width" ), wxS( "track_width" ), PART::OPT ),
                            len( _( "Max Width" ), wxS( "track_width" ), PART::MAX ),
                            len( _( "Min Gap" ), wxS( "diff_pair_gap" ), PART::MIN ),
                            len( _( "Preferred Gap" ), wxS( "diff_pair_gap" ), PART::OPT ),
                            len( _( "Max Gap" ), wxS( "diff_pair_gap" ), PART::MAX ),
                            len( _( "Max Uncoupled Length" ), wxS( "diff_pair_uncoupled" ),
                                 PART::MAX ) } ) );

        // SMT
        v.push_back( unsupported( S, _( "SMD To Corner" ), noEquiv ) );
        v.push_back( unsupported( S, _( "SMD To Plane" ), noEquiv ) );
        v.push_back( unsupported( S, _( "SMD Neck-Down" ), noEquiv ) );
        v.push_back( unsupported( S, _( "SMD Entry" ), noEquiv ) );

        // Mask
        v.push_back( cat( wxS( "mask_expansion" ), M, _( "Solder Mask Expansion" ),
                          wxS( "SolderMaskExpansion" ), false, { wxS( "solder_mask_expansion" ) },
                          { len( _( "Expansion" ), wxS( "solder_mask_expansion" ), PART::OPT ) },
                          {}, _( "Pad and footprint local settings take precedence." ) ) );
        v.push_back( cat( wxS( "paste_expansion" ), M, _( "Paste Mask Expansion" ),
                          wxS( "PasteMaskExpansion" ), false, { wxS( "solder_paste_abs_margin" ) },
                          { len( _( "Expansion" ), wxS( "solder_paste_abs_margin" ), PART::OPT ) },
                          {}, _( "Absolute paste margin (negative shrinks the paste).  Pad and "
                                 "footprint local settings take precedence." ) ) );

        // Plane
        v.push_back( unsupported( P, _( "Power Plane Connect Style" ),
                                  _( "KiCad planes are copper zones; use Polygon Connect Style." ) ) );
        v.push_back( unsupported( P, _( "Power Plane Clearance" ),
                                  _( "KiCad planes are copper zones; their clearance comes from "
                                     "Clearance rules and the zone's own clearance." ) ) );
        {
            CATEGORY c = cat( wxS( "polygon_connect" ), P, _( "Polygon Connect Style" ),
                              wxS( "PolygonConnect" ), false,
                              { wxS( "zone_connection" ), wxS( "thermal_relief_gap" ),
                                wxS( "thermal_spoke_width" ), wxS( "min_resolved_spokes" ) },
                              { FIELD{ _( "Connect Style" ), wxS( "zone_connection" ), PART::NONE,
                                       FIELD_KIND::CONNECT_STYLE, wxEmptyString },
                                len( _( "Air-Gap Width" ), wxS( "thermal_relief_gap" ), PART::MIN ),
                                len( _( "Conductor Width" ), wxS( "thermal_spoke_width" ),
                                     PART::MIN ),
                                cnt( _( "Minimum Conductors" ), wxS( "min_resolved_spokes" ),
                                     PART::MIN ) },
                              {}, _( "How pads connect to copper zones.  Overrides the zone's "
                                     "own pad connection settings." ) );
            v.push_back( c );
        }

        // Testpoint
        v.push_back( unsupported( T, _( "Fabrication Testpoint Style" ), noEquiv ) );
        v.push_back( unsupported( T, _( "Fabrication Testpoint Usage" ), noEquiv ) );
        v.push_back( unsupported( T, _( "Assembly Testpoint Style" ), noEquiv ) );
        v.push_back( unsupported( T, _( "Assembly Testpoint Usage" ), noEquiv ) );

        // Manufacturing
        v.push_back( cat( wxS( "annular_ring" ), F, _( "Minimum Annular Ring" ),
                          wxS( "MinimumAnnularRing" ), false, { wxS( "annular_width" ) },
                          { len( _( "Minimum Annular Ring" ), wxS( "annular_width" ),
                                 PART::MIN ) } ) );
        v.push_back( unsupported( F, _( "Acute Angle" ), noEquiv ) );
        v.push_back( cat( wxS( "hole_size" ), F, _( "Hole Size" ), wxS( "HoleSize" ), false,
                          { wxS( "hole_size" ) },
                          { len( _( "Minimum" ), wxS( "hole_size" ), PART::MIN ),
                            len( _( "Maximum" ), wxS( "hole_size" ), PART::MAX ) } ) );
        v.push_back( unsupported( F, _( "Layer Pairs" ),
                                  _( "Allowed layer pairs follow the via types defined in the "
                                     "board stackup." ) ) );
        v.push_back( cat( wxS( "hole_to_hole" ), F, _( "Hole To Hole Clearance" ),
                          wxS( "HoleToHoleClearance" ), true, { wxS( "hole_to_hole" ) },
                          { len( _( "Hole To Hole Clearance" ), wxS( "hole_to_hole" ),
                                 PART::MIN ) } ) );
        v.push_back( cat( wxS( "mask_sliver" ), F, _( "Minimum Solder Mask Sliver" ),
                          wxS( "MinimumSolderMaskSliver" ), false, { wxS( "solder_mask_sliver" ) },
                          { len( _( "Minimum Solder Mask Sliver" ), wxS( "solder_mask_sliver" ),
                                 PART::MIN ) } ) );
        v.push_back( cat( wxS( "silk_to_mask" ), F, _( "Silk To Solder Mask Clearance" ),
                          wxS( "SilkToSolderMaskClearance" ), true, { wxS( "silk_clearance" ) },
                          { len( _( "Silkscreen To Mask Clearance" ), wxS( "silk_clearance" ),
                                 PART::MIN ) },
                          { wxS( "(L == 'F.Mask' || L == 'B.Mask')" ) } ) );
        v.push_back( cat( wxS( "silk_to_silk" ), F, _( "Silk To Silk Clearance" ),
                          wxS( "SilkToSilkClearance" ), true, { wxS( "silk_clearance" ) },
                          { len( _( "Silk Text to Silk Clearance" ), wxS( "silk_clearance" ),
                                 PART::MIN ) },
                          { wxS( "(L == 'F.SilkS' || L == 'B.SilkS')" ) } ) );
        v.push_back( unsupported( F, _( "Net Antennae" ),
                                  _( "KiCad reports dangling tracks and vias in DRC instead." ) ) );
        v.push_back( cat( wxS( "board_outline" ), F, _( "Board Outline Clearance" ),
                          wxS( "BoardOutlineClearance" ), false, { wxS( "edge_clearance" ) },
                          { len( _( "Board Outline Clearance" ), wxS( "edge_clearance" ),
                                 PART::MIN ) } ) );
        v.push_back( cat( wxS( "connection_width" ), F, _( "Minimum Connection Width" ),
                          wxS( "MinimumConnectionWidth" ), false, { wxS( "connection_width" ) },
                          { len( _( "Minimum Width" ), wxS( "connection_width" ), PART::MIN ) },
                          {}, _( "Narrowest copper neck allowed (KiCad rule type)." ) ) );
        v.push_back( cat( wxS( "silk_text" ), F, _( "Text Height / Thickness" ),
                          wxS( "TextSize" ), false,
                          { wxS( "text_height" ), wxS( "text_thickness" ) },
                          { len( _( "Minimum Text Height" ), wxS( "text_height" ), PART::MIN ),
                            len( _( "Minimum Text Thickness" ), wxS( "text_thickness" ),
                                 PART::MIN ) },
                          {}, _( "Minimum text size (KiCad rule type)." ) ) );

        // High Speed
        v.push_back( unsupported( H, _( "Parallel Segment" ), noEquiv ) );
        v.push_back( cat( wxS( "length" ), H, _( "Length" ), wxS( "Length" ), false,
                          { wxS( "length" ) },
                          { len( _( "Minimum Length" ), wxS( "length" ), PART::MIN ),
                            len( _( "Maximum Length" ), wxS( "length" ), PART::MAX ) } ) );
        v.push_back( cat( wxS( "matched_lengths" ), H, _( "Matched Lengths" ),
                          wxS( "MatchedLengths" ), false, { wxS( "skew" ) },
                          { len( _( "Tolerance" ), wxS( "skew" ), PART::MAX ),
                            FIELD{ _( "Within differential pairs only" ), wxS( "skew" ),
                                   PART::NONE, FIELD_KIND::FLAG, wxS( "within_diff_pairs" ) } },
                          {}, _( "Maximum length difference between the nets in scope." ) ) );
        v.push_back( unsupported( H, _( "Daisy Chain Stub Length" ), noEquiv ) );
        v.push_back( unsupported( H, _( "Vias Under SMD" ),
                                  _( "KiCad has no via-in-pad rule; a Custom rule with "
                                     "(constraint disallow via) inside a rule area can be used." ) ) );
        v.push_back( cat( wxS( "via_count" ), H, _( "Maximum Via Count" ),
                          wxS( "MaximumViaCount" ), false, { wxS( "via_count" ) },
                          { cnt( _( "Maximum Via Count" ), wxS( "via_count" ), PART::MAX ) } ) );

        // Placement
        v.push_back( unsupported( L, _( "Room Definition" ),
                                  _( "Rooms are rule areas with a placement source; edit them on "
                                     "the board." ) ) );
        v.push_back( cat( wxS( "component_clearance" ), L, _( "Component Clearance" ),
                          wxS( "ComponentClearance" ), true, { wxS( "courtyard_clearance" ) },
                          { len( _( "Minimum Clearance" ), wxS( "courtyard_clearance" ),
                                 PART::MIN ) },
                          {}, _( "Clearance between footprint courtyards." ) ) );
        v.push_back( cat( wxS( "orientations" ), L, _( "Component Orientations" ),
                          wxS( "ComponentOrientations" ), false, { wxS( "assertion" ) },
                          { FIELD{ _( "Allowed Orientations" ), wxS( "assertion" ), PART::NONE,
                                   FIELD_KIND::ORIENTATION, wxEmptyString } },
                          { wxS( "A.Type == 'Footprint'" ) } ) );
        v.push_back( cat( wxS( "permitted_layers" ), L, _( "Permitted Layers" ),
                          wxS( "PermittedLayers" ), false, { wxS( "assertion" ) },
                          { FIELD{ _( "Permitted Layers" ), wxS( "assertion" ), PART::NONE,
                                   FIELD_KIND::SIDE_LAYERS, wxEmptyString } },
                          { wxS( "A.Type == 'Footprint'" ) } ) );
        v.push_back( unsupported( L, _( "Nets to Ignore" ), noEquiv ) );
        v.push_back( unsupported( L, _( "Height" ), noEquiv ) );

        // Signal Integrity
        v.push_back( unsupported( I, _( "Signal Integrity" ),
                                  _( "Signal integrity analysis is not part of KiCad." ) ) );

        // Everything else KiCad can express
        {
            CATEGORY c;
            c.id = wxS( "custom" );
            c.group = _( "Custom" );
            c.name = _( "Custom Rules" );
            c.defaultRuleName = wxS( "CustomRule" );
            c.supported = true;
            c.binary = false;
            c.note = _( "Rules using KiCad constraints that have no category of their own, "
                        "edited as rule text." );
            v.push_back( c );
        }

        return v;
    }();

    return s_categories;
}


int CategoryIndex( const wxString& aId )
{
    const std::vector<CATEGORY>& cats = Categories();

    for( size_t i = 0; i < cats.size(); ++i )
    {
        if( cats[i].id == aId )
            return (int) i;
    }

    return -1;
}


int CustomCategory()
{
    return (int) Categories().size() - 1;
}


std::vector<wxString> Groups()
{
    std::vector<wxString> groups;

    for( const CATEGORY& c : Categories() )
    {
        if( std::find( groups.begin(), groups.end(), c.group ) == groups.end() )
            groups.push_back( c.group );
    }

    return groups;
}


// ------------------------------------------------------------------------------------------
// Small text helpers
// ------------------------------------------------------------------------------------------

wxString FormatMM( long long aIU )
{
    wxString s = wxString::FromCDouble( aIU / pcbIUScale.IU_PER_MM, 6 );

    if( s.Contains( wxS( "." ) ) )
    {
        while( s.EndsWith( wxS( "0" ) ) )
            s.RemoveLast();

        if( s.EndsWith( wxS( "." ) ) )
            s.RemoveLast();
    }

    if( s == wxS( "-0" ) )
        s = wxS( "0" );

    return s + wxS( "mm" );
}


/// Remove all whitespace, and outer parentheses that wrap the whole expression.
static wxString normalize( const wxString& aExpr );


/// Strip one level of parentheses wrapping the whole (trimmed) expression.
static wxString stripOuterParens( const wxString& aExpr )
{
    wxString s = aExpr;
    s.Trim( true ).Trim( false );

    while( s.StartsWith( wxS( "(" ) ) && s.EndsWith( wxS( ")" ) ) )
    {
        // Make sure the first paren closes at the very end
        int   depth = 0;
        bool  wraps = true;
        wxChar quote = 0;

        for( size_t i = 0; i < s.length(); ++i )
        {
            wxChar c = s[i];

            if( quote )
            {
                if( c == quote )
                    quote = 0;

                continue;
            }

            if( c == '\'' || c == '"' )
                quote = c;
            else if( c == '(' )
                depth++;
            else if( c == ')' )
            {
                depth--;

                if( depth == 0 && i != s.length() - 1 )
                {
                    wraps = false;
                    break;
                }
            }
        }

        if( !wraps )
            break;

        s = s.Mid( 1, s.length() - 2 );
        s.Trim( true ).Trim( false );
    }

    return s;
}


static wxString normalize( const wxString& aExpr )
{
    wxString s = stripOuterParens( aExpr );
    wxString out;

    for( wxChar c : s )
    {
        if( c != ' ' && c != '\t' && c != '\n' && c != '\r' )
            out += c;
    }

    return out;
}


/**
 * Split an expression on a top-level operator ("&&" or "||").  Returns the parts, trimmed.
 * aOtherOperatorFound is set when the other logical operator appears at top level.
 */
static std::vector<wxString> splitTopLevel( const wxString& aExpr, const wxString& aOp,
                                            bool* aOtherOperatorFound = nullptr )
{
    std::vector<wxString> parts;
    wxString              other = aOp == wxS( "&&" ) ? wxS( "||" ) : wxS( "&&" );
    int                   depth = 0;
    wxChar                quote = 0;
    size_t                start = 0;

    if( aOtherOperatorFound )
        *aOtherOperatorFound = false;

    for( size_t i = 0; i < aExpr.length(); ++i )
    {
        wxChar c = aExpr[i];

        if( quote )
        {
            if( c == quote )
                quote = 0;

            continue;
        }

        if( c == '\'' || c == '"' )
        {
            quote = c;
        }
        else if( c == '(' )
        {
            depth++;
        }
        else if( c == ')' )
        {
            depth--;
        }
        else if( depth == 0 && i + 1 < aExpr.length() )
        {
            wxString two = aExpr.Mid( i, 2 );

            if( two == aOp )
            {
                wxString part = aExpr.Mid( start, i - start );
                part.Trim( true ).Trim( false );
                parts.push_back( part );
                start = i + 2;
                i++;
            }
            else if( two == other && aOtherOperatorFound )
            {
                *aOtherOperatorFound = true;
            }
        }
    }

    wxString last = aExpr.Mid( start );
    last.Trim( true ).Trim( false );
    parts.push_back( last );

    return parts;
}


static bool hasTopLevelOr( const wxString& aExpr )
{
    bool orFound = false;
    splitTopLevel( aExpr, wxS( "&&" ), &orFound );
    return orFound;
}


// ------------------------------------------------------------------------------------------
// S-expression splitting
// ------------------------------------------------------------------------------------------

struct SEXPR_PARTS
{
    std::vector<wxString> atoms;      ///< tokens directly inside the outer list (unquoted)
    std::vector<wxString> children;   ///< lists directly inside the outer list (raw text)
    std::vector<wxString> comments;   ///< "#" comment lines directly inside the outer list
    bool                  ok = false;
};


/**
 * Split the first list of aText into its direct atoms, child lists and comment lines.
 * Comment lines follow the KiCad lexer: a line whose first non-blank character is '#'.
 */
static SEXPR_PARTS splitSexpr( const wxString& aText )
{
    SEXPR_PARTS parts;
    int         depth = 0;
    bool        lineStart = true;
    size_t      childStart = 0;
    size_t      n = aText.length();
    size_t      i = 0;

    while( i < n )
    {
        wxChar c = aText[i];

        if( lineStart )
        {
            size_t j = i;

            while( j < n && ( aText[j] == ' ' || aText[j] == '\t' ) )
                j++;

            lineStart = false;

            if( j < n && aText[j] == '#' )
            {
                size_t k = aText.find( '\n', j );

                if( k == wxString::npos )
                    k = n;

                if( depth == 1 )
                {
                    wxString line = aText.Mid( j + 1, k - j - 1 );
                    line.Trim( true );

                    if( line.StartsWith( wxS( " " ) ) )
                        line = line.Mid( 1 );

                    parts.comments.push_back( line );
                }

                i = k;
                continue;
            }
        }

        if( c == '\n' )
        {
            lineStart = true;
            i++;
            continue;
        }

        if( c == '"' )
        {
            // quoted string
            size_t   j = i + 1;
            wxString value;

            while( j < n && aText[j] != '"' && aText[j] != '\n' )
            {
                if( aText[j] == '\\' && j + 1 < n )
                {
                    value += aText[j + 1];
                    j += 2;
                    continue;
                }

                value += aText[j];
                j++;
            }

            if( depth == 1 )
                parts.atoms.push_back( value );

            i = ( j < n && aText[j] == '"' ) ? j + 1 : j;
            continue;
        }

        if( c == '(' )
        {
            if( depth == 1 )
                childStart = i;

            depth++;
            i++;
            continue;
        }

        if( c == ')' )
        {
            depth--;

            if( depth == 1 )
                parts.children.push_back( aText.Mid( childStart, i - childStart + 1 ) );

            if( depth == 0 )
            {
                parts.ok = true;
                return parts;
            }

            i++;
            continue;
        }

        if( c == ' ' || c == '\t' || c == '\r' )
        {
            i++;
            continue;
        }

        // bare atom
        size_t j = i;

        while( j < n && aText[j] != ' ' && aText[j] != '\t' && aText[j] != '\r'
               && aText[j] != '\n' && aText[j] != '(' && aText[j] != ')' && aText[j] != '"' )
        {
            j++;
        }

        if( depth == 1 )
            parts.atoms.push_back( aText.Mid( i, j - i ) );

        i = j;
    }

    return parts;
}


static wxString headOf( const wxString& aSexpr )
{
    SEXPR_PARTS parts = splitSexpr( aSexpr );
    return parts.atoms.empty() ? wxString() : parts.atoms[0];
}


static wxString quoteName( const wxString& aName )
{
    wxString escaped = aName;
    escaped.Replace( wxS( "\\" ), wxS( "\\\\" ) );
    escaped.Replace( wxS( "\"" ), wxS( "\\\"" ) );
    return wxS( "\"" ) + escaped + wxS( "\"" );
}


// ------------------------------------------------------------------------------------------
// Constraint clauses
// ------------------------------------------------------------------------------------------

static std::vector<std::shared_ptr<DRC_RULE>> parseRules( const wxString& aText, wxString* aError )
{
    std::vector<std::shared_ptr<DRC_RULE>> rules;

    try
    {
        DRC_RULES_PARSER parser( wxS( "(version 1)\n" ) + aText, wxS( "Rules Editor" ) );
        parser.Parse( rules, nullptr );
    }
    catch( const IO_ERROR& ioe )
    {
        if( aError )
            *aError = ioe.What();

        rules.clear();
    }

    return rules;
}


bool ParseConstraintClause( const wxString& aClause, CLAUSE_VALUES& aValues )
{
    aValues = CLAUSE_VALUES();

    SEXPR_PARTS parts = splitSexpr( aClause );

    if( parts.atoms.size() >= 2 )
        aValues.keyword = parts.atoms[1];

    std::vector<std::shared_ptr<DRC_RULE>> rules =
            parseRules( wxS( "(rule \"x\" " ) + aClause + wxS( ")" ), nullptr );

    if( rules.empty() || rules[0]->m_Constraints.empty() )
        return false;

    const DRC_CONSTRAINT& c = rules[0]->m_Constraints[0];

    if( c.m_Value.HasMin() )
        aValues.min = c.m_Value.Min();

    if( c.m_Value.HasOpt() )
        aValues.opt = c.m_Value.Opt();

    if( c.m_Value.HasMax() )
        aValues.max = c.m_Value.Max();

    aValues.withinDiffPairs = c.GetOption( DRC_CONSTRAINT::OPTIONS::SKEW_WITHIN_DIFF_PAIRS );

    if( c.m_Type == ZONE_CONNECTION_CONSTRAINT )
    {
        switch( c.m_ZoneConnection )
        {
        case ZONE_CONNECTION::FULL:    aValues.zoneConnection = 0; break;
        case ZONE_CONNECTION::THERMAL: aValues.zoneConnection = 1; break;
        case ZONE_CONNECTION::NONE:    aValues.zoneConnection = 2; break;
        default:                       break;
        }
    }

    if( c.m_Type == ASSERTION_CONSTRAINT && c.m_Test )
        aValues.assertion = c.m_Test->GetExpression();

    aValues.ok = true;
    return true;
}


static bool isUnitless( const wxString& aKeyword )
{
    return aKeyword == wxS( "via_count" ) || aKeyword == wxS( "min_resolved_spokes" )
           || aKeyword == wxS( "track_angle" ) || aKeyword == wxS( "via_dangling" )
           || aKeyword == wxS( "bridged_mask" );
}


wxString BuildConstraintClause( const CLAUSE_VALUES& aValues )
{
    const wxString& kw = aValues.keyword;

    if( kw == wxS( "zone_connection" ) )
    {
        if( !aValues.zoneConnection )
            return wxEmptyString;

        static const wxChar* styles[] = { wxS( "solid" ), wxS( "thermal_reliefs" ),
                                          wxS( "none" ) };
        return wxString::Format( wxS( "(constraint zone_connection %s)" ),
                                 styles[std::clamp( *aValues.zoneConnection, 0, 2 )] );
    }

    if( kw == wxS( "assertion" ) )
    {
        if( aValues.assertion.IsEmpty() )
            return wxEmptyString;

        return wxString::Format( wxS( "(constraint assertion \"%s\")" ), aValues.assertion );
    }

    if( kw == wxS( "min_resolved_spokes" ) )
    {
        if( !aValues.min )
            return wxEmptyString;

        return wxString::Format( wxS( "(constraint min_resolved_spokes %d)" ), *aValues.min );
    }

    auto fmt = [&]( int aValue ) -> wxString
    {
        if( isUnitless( kw ) )
            return wxString::Format( wxS( "%d" ), aValue );

        return FormatMM( aValue );
    };

    wxString body;

    if( aValues.min )
        body << wxS( " (min " ) << fmt( *aValues.min ) << wxS( ")" );

    if( aValues.opt )
        body << wxS( " (opt " ) << fmt( *aValues.opt ) << wxS( ")" );

    if( aValues.max )
        body << wxS( " (max " ) << fmt( *aValues.max ) << wxS( ")" );

    if( body.IsEmpty() )
        return wxEmptyString;

    if( aValues.withinDiffPairs )
        body << wxS( " (within_diff_pairs)" );

    return wxS( "(constraint " ) + kw + body + wxS( ")" );
}


// ------------------------------------------------------------------------------------------
// Scopes
// ------------------------------------------------------------------------------------------

wxString SCOPE::ToExpr( wxChar aObj ) const
{
    wxString o = wxString( aObj ) + wxS( "." );

    switch( kind )
    {
    case SCOPE_KIND::ALL:      return wxEmptyString;
    case SCOPE_KIND::NET:      return o + wxS( "NetName == '" ) + net + wxS( "'" );
    case SCOPE_KIND::NETCLASS: return o + wxS( "hasNetclass('" ) + net + wxS( "')" );
    case SCOPE_KIND::LAYER:    return o + wxS( "existsOnLayer('" ) + layer + wxS( "')" );
    case SCOPE_KIND::NET_AND_LAYER:
        return o + wxS( "NetName == '" ) + net + wxS( "' && " ) + o + wxS( "existsOnLayer('" )
               + layer + wxS( "')" );
    case SCOPE_KIND::CUSTOM:   return query;
    }

    return wxEmptyString;
}


wxString SCOPE::Describe( wxChar aObj ) const
{
    if( kind == SCOPE_KIND::ALL )
        return _( "All" );

    return ToExpr( aObj );
}


static bool refersTo( const wxString& aTerm, wxChar aObj )
{
    for( size_t i = 0; i + 1 < aTerm.length(); ++i )
    {
        if( aTerm[i] == aObj && aTerm[i + 1] == '.' )
        {
            if( i == 0 )
                return true;

            wxChar prev = aTerm[i - 1];

            if( !wxIsalnum( prev ) && prev != '_' )
                return true;
        }
    }

    return false;
}


struct SCOPE_ACC
{
    wxString              net;
    wxString              netclass;
    wxString              layer;
    std::vector<wxString> terms;
    bool                  custom = false;
};


static SCOPE finishScope( const SCOPE_ACC& aAcc )
{
    SCOPE s;
    int   kinds = ( aAcc.net.IsEmpty() ? 0 : 1 ) + ( aAcc.netclass.IsEmpty() ? 0 : 1 );

    if( aAcc.custom || kinds > 1 || ( !aAcc.netclass.IsEmpty() && !aAcc.layer.IsEmpty() ) )
    {
        s.kind = SCOPE_KIND::CUSTOM;

        wxString q;

        for( const wxString& t : aAcc.terms )
        {
            if( !q.IsEmpty() )
                q << wxS( " && " );

            q << t;
        }

        s.query = q;
    }
    else if( !aAcc.net.IsEmpty() && !aAcc.layer.IsEmpty() )
    {
        s.kind = SCOPE_KIND::NET_AND_LAYER;
        s.net = aAcc.net;
        s.layer = aAcc.layer;
    }
    else if( !aAcc.net.IsEmpty() )
    {
        s.kind = SCOPE_KIND::NET;
        s.net = aAcc.net;
    }
    else if( !aAcc.netclass.IsEmpty() )
    {
        s.kind = SCOPE_KIND::NETCLASS;
        s.net = aAcc.netclass;
    }
    else if( !aAcc.layer.IsEmpty() )
    {
        s.kind = SCOPE_KIND::LAYER;
        s.layer = aAcc.layer;
    }

    return s;
}


void ParseScopes( const wxString& aCondition, const CATEGORY& aCategory, SCOPE& aFirst,
                  SCOPE& aSecond, std::vector<wxString>& aHiddenTerms )
{
    aFirst = SCOPE();
    aSecond = SCOPE();
    aHiddenTerms.clear();

    wxString cond = aCondition;
    cond.Trim( true ).Trim( false );

    if( cond.IsEmpty() )
        return;

    // The whole condition may be exactly the category's implicit condition
    if( !aCategory.implicitTerms.empty() )
    {
        wxString implicit;

        for( const wxString& t : aCategory.implicitTerms )
        {
            if( !implicit.IsEmpty() )
                implicit << wxS( " && " );

            implicit << t;
        }

        if( normalize( cond ) == normalize( implicit ) )
        {
            aHiddenTerms = aCategory.implicitTerms;
            return;
        }
    }

    bool                  orFound = false;
    std::vector<wxString> terms = splitTopLevel( cond, wxS( "&&" ), &orFound );

    auto fallback = [&]( const wxString& aVisible )
    {
        aFirst = SCOPE();
        aSecond = SCOPE();

        if( !aVisible.IsEmpty() )
        {
            aFirst.kind = SCOPE_KIND::CUSTOM;
            aFirst.query = aVisible;
        }
    };

    if( orFound )
    {
        fallback( cond );
        return;
    }

    std::vector<wxString> visible;

    for( const wxString& term : terms )
    {
        bool hidden = false;

        for( const wxString& implicit : aCategory.implicitTerms )
        {
            if( normalize( term ) == normalize( implicit ) )
            {
                hidden = true;
                break;
            }
        }

        if( hidden )
            aHiddenTerms.push_back( term );
        else if( !term.IsEmpty() )
            visible.push_back( term );
    }

    static wxRegEx reNet( wxS( "^([AB])\\.NetName\\s*==\\s*'([^']*)'$" ), wxRE_ADVANCED );
    static wxRegEx reClass1( wxS( "^([AB])\\.hasNetclass\\('([^']*)'\\)$" ), wxRE_ADVANCED );
    static wxRegEx reClass2( wxS( "^([AB])\\.NetClass\\s*==\\s*'([^']*)'$" ), wxRE_ADVANCED );
    static wxRegEx reLayer1( wxS( "^([AB])\\.existsOnLayer\\('([^']*)'\\)$" ), wxRE_ADVANCED );
    static wxRegEx reLayer2( wxS( "^([AB])\\.Layer\\s*==\\s*'([^']*)'$" ), wxRE_ADVANCED );

    SCOPE_ACC acc[2];

    for( const wxString& rawTerm : visible )
    {
        wxString term = stripOuterParens( rawTerm );
        bool     a = refersTo( term, 'A' );
        bool     b = refersTo( term, 'B' );

        if( a == b || ( b && !aCategory.binary ) )
        {
            // References both objects, or neither (e.g. "L == ...", "AB.isCoupledDiffPair()")
            wxString visibleText;

            for( const wxString& t : visible )
            {
                if( !visibleText.IsEmpty() )
                    visibleText << wxS( " && " );

                visibleText << t;
            }

            fallback( visibleText );
            return;
        }

        SCOPE_ACC& s = acc[a ? 0 : 1];
        s.terms.push_back( rawTerm );

        auto setOnce = [&]( wxString& aSlot, const wxString& aValue )
        {
            if( aSlot.IsEmpty() )
                aSlot = aValue;
            else
                s.custom = true;
        };

        if( reNet.Matches( term ) )
            setOnce( s.net, reNet.GetMatch( term, 2 ) );
        else if( reClass1.Matches( term ) )
            setOnce( s.netclass, reClass1.GetMatch( term, 2 ) );
        else if( reClass2.Matches( term ) )
            setOnce( s.netclass, reClass2.GetMatch( term, 2 ) );
        else if( reLayer1.Matches( term ) )
            setOnce( s.layer, reLayer1.GetMatch( term, 2 ) );
        else if( reLayer2.Matches( term ) )
            setOnce( s.layer, reLayer2.GetMatch( term, 2 ) );
        else
            s.custom = true;
    }

    aFirst = finishScope( acc[0] );
    aSecond = finishScope( acc[1] );
}


wxString BuildCondition( const std::vector<wxString>& aHiddenTerms, const SCOPE& aFirst,
                         const SCOPE& aSecond, bool aBinary )
{
    std::vector<wxString> parts = aHiddenTerms;

    wxString first = aFirst.ToExpr( 'A' );

    if( !first.IsEmpty() )
        parts.push_back( first );

    if( aBinary )
    {
        wxString second = aSecond.ToExpr( 'B' );

        if( !second.IsEmpty() )
            parts.push_back( second );
    }

    wxString cond;

    for( wxString part : parts )
    {
        part.Trim( true ).Trim( false );

        if( part.IsEmpty() )
            continue;

        if( parts.size() > 1 && hasTopLevelOr( part ) )
            part = wxS( "(" ) + part + wxS( ")" );

        if( !cond.IsEmpty() )
            cond << wxS( " && " );

        cond << part;
    }

    return cond;
}


// ------------------------------------------------------------------------------------------
// Rules
// ------------------------------------------------------------------------------------------

wxString RULE::Generate() const
{
    wxString text;
    text << wxS( "(rule " ) << quoteName( name ) << wxS( "\n" );

    if( !comment.IsEmpty() )
    {
        for( const wxString& line : wxSplit( comment, '\n', '\0' ) )
            text << wxS( "\t# " ) << line << wxS( "\n" );
    }

    if( !layerClause.IsEmpty() )
        text << wxS( "\t" ) << layerClause << wxS( "\n" );

    for( const wxString& c : constraints )
        text << wxS( "\t" ) << c << wxS( "\n" );

    if( !condition.IsEmpty() )
        text << wxS( "\t(condition \"" ) << condition << wxS( "\")\n" );

    if( !severityClause.IsEmpty() )
        text << wxS( "\t" ) << severityClause << wxS( "\n" );

    for( const wxString& o : others )
        text << wxS( "\t" ) << o << wxS( "\n" );

    text << wxS( ")" );
    return text;
}


wxString RULE::ToText() const
{
    if( !modified && !originalText.IsEmpty() )
        return originalText;

    return Generate();
}


int RULE::FindClause( const wxString& aKeyword ) const
{
    for( size_t i = 0; i < constraints.size(); ++i )
    {
        SEXPR_PARTS parts = splitSexpr( constraints[i] );

        if( parts.atoms.size() >= 2 && parts.atoms[1] == aKeyword )
            return (int) i;
    }

    return -1;
}


bool RULE::HasKeyword( const wxString& aKeyword ) const
{
    return FindClause( aKeyword ) >= 0;
}


void ParseRule( const wxString& aText, RULE& aRule )
{
    int  uid = aRule.uid;
    bool enabled = aRule.enabled;

    aRule = RULE();
    aRule.uid = uid;
    aRule.enabled = enabled;
    aRule.originalText = aText;

    SEXPR_PARTS parts = splitSexpr( aText );

    if( parts.atoms.size() >= 2 )
        aRule.name = parts.atoms[1];

    aRule.comment = wxJoin( wxArrayString( parts.comments.size(), parts.comments.data() ), '\n',
                            '\0' );

    for( const wxString& child : parts.children )
    {
        SEXPR_PARTS c = splitSexpr( child );
        wxString    head = c.atoms.empty() ? wxString() : c.atoms[0];

        if( head == wxS( "constraint" ) )
            aRule.constraints.push_back( child );
        else if( head == wxS( "condition" ) )
            aRule.condition = c.atoms.size() >= 2 ? c.atoms[1] : wxString();
        else if( head == wxS( "layer" ) )
            aRule.layerClause = child;
        else if( head == wxS( "severity" ) )
            aRule.severityClause = child;
        else
            aRule.others.push_back( child );
    }

    wxString                               error;
    std::vector<std::shared_ptr<DRC_RULE>> rules = parseRules( aText, &error );

    if( rules.empty() )
    {
        aRule.parseError = true;
        aRule.errorMsg = error.IsEmpty() ? wxString( _( "Not a valid rule." ) ) : error;
    }
    else
    {
        aRule.name = rules[0]->m_Name;

        if( rules[0]->m_Condition )
            aRule.condition = rules[0]->m_Condition->GetExpression();
    }

    if( !parts.ok )
    {
        aRule.parseError = true;

        if( aRule.errorMsg.IsEmpty() )
            aRule.errorMsg = _( "Unbalanced parentheses." );
    }
}


wxString CheckRuleSyntax( const wxString& aText )
{
    wxString error;
    std::vector<std::shared_ptr<DRC_RULE>> rules = parseRules( aText, &error );

    if( rules.empty() && error.IsEmpty() )
        error = _( "Not a valid rule." );

    return error;
}


// ------------------------------------------------------------------------------------------
// Document
// ------------------------------------------------------------------------------------------

void RULES_DOC::Clear()
{
    m_items.clear();
    m_nextUid = 1;
}


bool RULES_DOC::LoadFile( const wxString& aPath )
{
    Clear();

    if( !wxFileExists( aPath ) )
        return true;

    wxFFile file( aPath, "rb" );

    if( !file.IsOpened() )
        return false;

    wxString content;

    if( !file.ReadAll( &content, wxConvUTF8 ) )
        return false;

    LoadFromString( content );
    return true;
}


void RULES_DOC::addChunk( const wxString& aChunk, bool aEnabled )
{
    wxString head = headOf( aChunk );

    if( head == wxS( "version" ) )
        return;

    ITEM item;

    if( head == wxS( "rule" ) )
    {
        item.isRule = true;
        item.rule.uid = m_nextUid++;
        item.rule.enabled = aEnabled;
        ParseRule( aChunk, item.rule );
    }
    else if( aEnabled )
    {
        item.raw = aChunk;
    }
    else
    {
        for( const wxString& line : wxSplit( aChunk, '\n', '\0' ) )
            item.raw << wxS( "#~ " ) << line << wxS( "\n" );

        item.raw.Trim( true );
    }

    m_items.push_back( item );
}


/// Scan text into top-level lists and comment lines.
static void scanTopLevel( const wxString& aText,
                          const std::function<void( const wxString&, bool aIsComment )>& aEmit )
{
    int    depth = 0;
    bool   lineStart = true;
    bool   inString = false;
    size_t start = 0;
    size_t n = aText.length();
    size_t i = 0;

    while( i < n )
    {
        wxChar c = aText[i];

        if( lineStart )
        {
            lineStart = false;
            size_t j = i;

            while( j < n && ( aText[j] == ' ' || aText[j] == '\t' ) )
                j++;

            if( j < n && aText[j] == '#' )
            {
                size_t k = aText.find( '\n', j );

                if( k == wxString::npos )
                    k = n;

                if( depth == 0 )
                {
                    wxString line = aText.Mid( j, k - j );
                    line.Trim( true );
                    aEmit( line, true );
                }

                i = k;
                continue;
            }
        }

        if( c == '\n' )
        {
            lineStart = true;
            inString = false;
            i++;
            continue;
        }

        if( inString )
        {
            if( c == '\\' )
                i++;
            else if( c == '"' )
                inString = false;

            i++;
            continue;
        }

        if( c == '"' )
        {
            inString = true;
        }
        else if( c == '(' )
        {
            if( depth == 0 )
                start = i;

            depth++;
        }
        else if( c == ')' && depth > 0 )
        {
            depth--;

            if( depth == 0 )
                aEmit( aText.Mid( start, i - start + 1 ), false );
        }

        i++;
    }

    if( depth > 0 )
        aEmit( aText.Mid( start ), false );
}


void RULES_DOC::flushDisabled( wxString& aBuffer )
{
    if( aBuffer.IsEmpty() )
        return;

    scanTopLevel( aBuffer,
                  [&]( const wxString& aChunk, bool aIsComment )
                  {
                      if( aIsComment )
                      {
                          ITEM item;
                          item.raw = wxS( "#~ " ) + aChunk;
                          m_items.push_back( item );
                      }
                      else
                      {
                          addChunk( aChunk, false );
                      }
                  } );

    aBuffer.clear();
}


void RULES_DOC::LoadFromString( const wxString& aText )
{
    Clear();

    wxString disabled;

    scanTopLevel( aText,
                  [&]( const wxString& aChunk, bool aIsComment )
                  {
                      if( aIsComment && aChunk.StartsWith( wxS( "#~" ) ) )
                      {
                          wxString line = aChunk.Mid( 2 );

                          if( line.StartsWith( wxS( " " ) ) )
                              line = line.Mid( 1 );

                          disabled << line << wxS( "\n" );
                          return;
                      }

                      flushDisabled( disabled );

                      if( aIsComment )
                      {
                          ITEM item;
                          item.raw = aChunk;
                          m_items.push_back( item );
                      }
                      else
                      {
                          addChunk( aChunk, true );
                      }
                  } );

    flushDisabled( disabled );
}


wxString RULES_DOC::Serialize() const
{
    wxString out = wxS( "(version 1)\n" );

    for( const ITEM& item : m_items )
    {
        if( !item.isRule )
        {
            out << item.raw << wxS( "\n" );
            continue;
        }

        wxString text = item.rule.ToText();

        if( item.rule.enabled )
        {
            out << text << wxS( "\n" );
        }
        else
        {
            for( const wxString& line : wxSplit( text, '\n', '\0' ) )
                out << wxS( "#~ " ) << line << wxS( "\n" );
        }
    }

    return out;
}


bool RULES_DOC::SaveFile( const wxString& aPath ) const
{
    std::string utf8 = std::string( Serialize().mb_str( wxConvUTF8 ) );
    return KIPLATFORM::IO::AtomicWriteFile( aPath, utf8.data(), utf8.size() );
}


RULE* RULES_DOC::FindRule( int aUid )
{
    for( ITEM& item : m_items )
    {
        if( item.isRule && item.rule.uid == aUid )
            return &item.rule;
    }

    return nullptr;
}


const RULE* RULES_DOC::FindRule( int aUid ) const
{
    for( const ITEM& item : m_items )
    {
        if( item.isRule && item.rule.uid == aUid )
            return &item.rule;
    }

    return nullptr;
}


RULE* RULES_DOC::FindRuleByName( const wxString& aName )
{
    for( ITEM& item : m_items )
    {
        if( item.isRule && item.rule.name == aName )
            return &item.rule;
    }

    return nullptr;
}


static bool assertionIsOrientation( const wxString& aExpr )
{
    wxString rest = aExpr;
    static wxRegEx re( wxS( "A\\.Orientation\\s*==\\s*(0|90|180|270)\\s*deg" ), wxRE_ADVANCED );

    if( !rest.Contains( wxS( "Orientation" ) ) )
        return false;

    re.ReplaceAll( &rest, wxEmptyString );
    rest.Replace( wxS( "||" ), wxEmptyString );

    for( wxChar c : rest )
    {
        if( c != ' ' && c != '(' && c != ')' && c != '\t' )
            return false;
    }

    return true;
}


static bool assertionIsSideLayers( const wxString& aExpr )
{
    wxString rest = aExpr;
    static wxRegEx re( wxS( "A\\.Layer\\s*==\\s*'(F|B)\\.Cu'" ), wxRE_ADVANCED );

    if( !rest.Contains( wxS( "Layer" ) ) )
        return false;

    re.ReplaceAll( &rest, wxEmptyString );
    rest.Replace( wxS( "||" ), wxEmptyString );

    for( wxChar c : rest )
    {
        if( c != ' ' && c != '(' && c != ')' && c != '\t' )
            return false;
    }

    return true;
}


int RULES_DOC::CategoryOfClause( const RULE& aRule, const wxString& aClause ) const
{
    SEXPR_PARTS parts = splitSexpr( aClause );
    wxString    kw = parts.atoms.size() >= 2 ? parts.atoms[1] : wxString();
    wxString    cond = normalize( aRule.condition );

    auto idx = []( const wxChar* aId )
    {
        return CategoryIndex( aId );
    };

    if( kw == wxS( "clearance" ) )
        return idx( wxS( "clearance" ) );

    if( kw == wxS( "hole_clearance" ) )
        return idx( wxS( "hole_clearance" ) );

    if( kw == wxS( "creepage" ) )
        return idx( wxS( "creepage" ) );

    if( kw == wxS( "track_width" ) )
    {
        if( aRule.HasKeyword( wxS( "diff_pair_gap" ) )
            || aRule.HasKeyword( wxS( "diff_pair_uncoupled" ) ) )
        {
            return idx( wxS( "diff_pairs" ) );
        }

        return idx( wxS( "width" ) );
    }

    if( kw == wxS( "diff_pair_gap" ) || kw == wxS( "diff_pair_uncoupled" ) )
        return idx( wxS( "diff_pairs" ) );

    if( kw == wxS( "via_diameter" ) )
        return idx( wxS( "via_style" ) );

    if( kw == wxS( "hole_size" ) )
    {
        if( aRule.HasKeyword( wxS( "via_diameter" ) ) || cond.Contains( wxS( "A.Type=='Via'" ) ) )
            return idx( wxS( "via_style" ) );

        return idx( wxS( "hole_size" ) );
    }

    if( kw == wxS( "annular_width" ) )
        return idx( wxS( "annular_ring" ) );

    if( kw == wxS( "solder_mask_expansion" ) )
        return idx( wxS( "mask_expansion" ) );

    if( kw == wxS( "solder_paste_abs_margin" ) )
        return idx( wxS( "paste_expansion" ) );

    if( kw == wxS( "zone_connection" ) || kw == wxS( "thermal_relief_gap" )
        || kw == wxS( "thermal_spoke_width" ) || kw == wxS( "min_resolved_spokes" ) )
    {
        return idx( wxS( "polygon_connect" ) );
    }

    if( kw == wxS( "hole_to_hole" ) )
        return idx( wxS( "hole_to_hole" ) );

    if( kw == wxS( "solder_mask_sliver" ) )
        return idx( wxS( "mask_sliver" ) );

    if( kw == wxS( "silk_clearance" ) )
    {
        if( cond.Contains( wxS( "Mask" ) ) )
            return idx( wxS( "silk_to_mask" ) );

        return idx( wxS( "silk_to_silk" ) );
    }

    if( kw == wxS( "edge_clearance" ) )
        return idx( wxS( "board_outline" ) );

    if( kw == wxS( "connection_width" ) )
        return idx( wxS( "connection_width" ) );

    if( kw == wxS( "text_height" ) || kw == wxS( "text_thickness" ) )
        return idx( wxS( "silk_text" ) );

    if( kw == wxS( "length" ) )
        return idx( wxS( "length" ) );

    if( kw == wxS( "skew" ) )
        return idx( wxS( "matched_lengths" ) );

    if( kw == wxS( "via_count" ) )
        return idx( wxS( "via_count" ) );

    if( kw == wxS( "courtyard_clearance" ) )
        return idx( wxS( "component_clearance" ) );

    if( kw == wxS( "assertion" ) )
    {
        wxString expr = parts.atoms.size() >= 3 ? parts.atoms[2] : wxString();

        if( assertionIsOrientation( expr ) )
            return idx( wxS( "orientations" ) );

        if( assertionIsSideLayers( expr ) )
            return idx( wxS( "permitted_layers" ) );
    }

    return CustomCategory();
}


std::vector<int> RULES_DOC::CategoriesOf( const RULE& aRule ) const
{
    std::vector<int> cats;

    if( aRule.parseError || aRule.constraints.empty() )
        return { CustomCategory() };

    for( const wxString& clause : aRule.constraints )
    {
        int c = CategoryOfClause( aRule, clause );

        if( std::find( cats.begin(), cats.end(), c ) == cats.end() )
            cats.push_back( c );
    }

    return cats;
}


bool RULES_DOC::InCategory( const RULE& aRule, int aCat ) const
{
    std::vector<int> cats = CategoriesOf( aRule );
    return std::find( cats.begin(), cats.end(), aCat ) != cats.end();
}


std::vector<RULE*> RULES_DOC::RulesInCategory( int aCat )
{
    std::vector<RULE*> rules;

    for( auto it = m_items.rbegin(); it != m_items.rend(); ++it )
    {
        if( it->isRule && InCategory( it->rule, aCat ) )
            rules.push_back( &it->rule );
    }

    return rules;
}


std::vector<RULE*> RULES_DOC::AllRules()
{
    std::vector<RULE*> rules;

    for( auto it = m_items.rbegin(); it != m_items.rend(); ++it )
    {
        if( it->isRule )
            rules.push_back( &it->rule );
    }

    return rules;
}


int RULES_DOC::Priority( const RULE& aRule, int aCat )
{
    std::vector<RULE*> rules = RulesInCategory( aCat );

    for( size_t i = 0; i < rules.size(); ++i )
    {
        if( rules[i]->uid == aRule.uid )
            return (int) i + 1;
    }

    return 0;
}


bool RULES_DOC::MovePriority( int aCat, int aUid, bool aUp )
{
    std::vector<RULE*> rules = RulesInCategory( aCat );
    int                pos = -1;

    for( size_t i = 0; i < rules.size(); ++i )
    {
        if( rules[i]->uid == aUid )
            pos = (int) i;
    }

    int other = aUp ? pos - 1 : pos + 1;

    if( pos < 0 || other < 0 || other >= (int) rules.size() )
        return false;

    int otherUid = rules[other]->uid;
    int a = -1;
    int b = -1;

    for( size_t i = 0; i < m_items.size(); ++i )
    {
        if( !m_items[i].isRule )
            continue;

        if( m_items[i].rule.uid == aUid )
            a = (int) i;
        else if( m_items[i].rule.uid == otherUid )
            b = (int) i;
    }

    if( a < 0 || b < 0 )
        return false;

    std::swap( m_items[a], m_items[b] );
    return true;
}


int RULES_DOC::AddRule( const RULE& aRule )
{
    ITEM item;
    item.isRule = true;
    item.rule = aRule;
    item.rule.uid = m_nextUid++;
    item.rule.modified = true;
    m_items.push_back( item );
    return item.rule.uid;
}


void RULES_DOC::RemoveFromCategory( int aUid, int aCat )
{
    RULE* rule = FindRule( aUid );

    if( !rule )
        return;

    if( aCat != CustomCategory() )
    {
        std::vector<wxString> kept;

        for( const wxString& clause : rule->constraints )
        {
            if( CategoryOfClause( *rule, clause ) != aCat )
                kept.push_back( clause );
        }

        if( !kept.empty() && kept.size() != rule->constraints.size() )
        {
            rule->constraints = kept;
            rule->modified = true;
            return;
        }
    }

    m_items.erase( std::remove_if( m_items.begin(), m_items.end(),
                                   [&]( const ITEM& aItem )
                                   {
                                       return aItem.isRule && aItem.rule.uid == aUid;
                                   } ),
                   m_items.end() );
}


wxString RULES_DOC::UniqueName( const wxString& aBase ) const
{
    auto exists = [&]( const wxString& aName )
    {
        for( const ITEM& item : m_items )
        {
            if( item.isRule && item.rule.name.CmpNoCase( aName ) == 0 )
                return true;
        }

        return false;
    };

    if( !exists( aBase ) )
        return aBase;

    for( int i = 1;; ++i )
    {
        wxString name = wxString::Format( wxS( "%s_%d" ), aBase, i );

        if( !exists( name ) )
            return name;
    }
}


FIELD_VALUE RULES_DOC::GetFieldValue( const RULE& aRule, int aCat, int aField ) const
{
    FIELD_VALUE    v;
    const CATEGORY& c = Categories()[aCat];

    if( aField < 0 || aField >= (int) c.fields.size() )
        return v;

    const FIELD& f = c.fields[aField];
    int          idx = aRule.FindClause( f.keyword );

    if( idx < 0 || CategoryOfClause( aRule, aRule.constraints[idx] ) != aCat )
        return v;

    CLAUSE_VALUES cv;

    if( !ParseConstraintClause( aRule.constraints[idx], cv ) )
        return v;

    switch( f.kind )
    {
    case FIELD_KIND::LENGTH:
    case FIELD_KIND::COUNT:
    {
        std::optional<int> part;

        if( f.part == PART::MIN )
            part = cv.min;
        else if( f.part == PART::OPT )
            part = cv.opt;
        else if( f.part == PART::MAX )
            part = cv.max;

        if( part )
        {
            v.set = true;
            v.num = *part;
        }

        break;
    }

    case FIELD_KIND::CONNECT_STYLE:
        if( cv.zoneConnection )
        {
            v.set = true;
            v.num = *cv.zoneConnection;
        }

        break;

    case FIELD_KIND::FLAG:
        v.set = true;
        v.num = cv.withinDiffPairs ? 1 : 0;
        break;

    case FIELD_KIND::ORIENTATION:
    {
        v.set = true;
        static const int angles[] = { 0, 90, 180, 270 };

        for( int i = 0; i < 4; ++i )
        {
            wxRegEx re( wxString::Format( wxS( "Orientation\\s*==\\s*%d\\s*deg" ), angles[i] ),
                        wxRE_ADVANCED );

            if( re.Matches( cv.assertion ) )
                v.num |= ( 1LL << i );
        }

        break;
    }

    case FIELD_KIND::SIDE_LAYERS:
        v.set = true;

        if( cv.assertion.Contains( wxS( "'F.Cu'" ) ) )
            v.num |= 1;

        if( cv.assertion.Contains( wxS( "'B.Cu'" ) ) )
            v.num |= 2;

        break;
    }

    return v;
}


void RULES_DOC::SetFieldValue( RULE& aRule, int aCat, int aField, const FIELD_VALUE& aValue )
{
    const CATEGORY& c = Categories()[aCat];

    if( aField < 0 || aField >= (int) c.fields.size() )
        return;

    const FIELD&  f = c.fields[aField];
    int           idx = aRule.FindClause( f.keyword );
    CLAUSE_VALUES cv;

    if( idx >= 0 )
        ParseConstraintClause( aRule.constraints[idx], cv );

    cv.keyword = f.keyword;

    switch( f.kind )
    {
    case FIELD_KIND::LENGTH:
    case FIELD_KIND::COUNT:
    {
        std::optional<int> part;

        if( aValue.set )
            part = (int) aValue.num;

        if( f.part == PART::MIN )
            cv.min = part;
        else if( f.part == PART::OPT )
            cv.opt = part;
        else if( f.part == PART::MAX )
            cv.max = part;

        break;
    }

    case FIELD_KIND::CONNECT_STYLE:
        if( aValue.set )
            cv.zoneConnection = (int) aValue.num;
        else
            cv.zoneConnection.reset();

        break;

    case FIELD_KIND::FLAG:
        cv.withinDiffPairs = aValue.set && aValue.num;
        break;

    case FIELD_KIND::ORIENTATION:
    {
        wxString expr;
        static const int angles[] = { 0, 90, 180, 270 };

        for( int i = 0; i < 4; ++i )
        {
            if( aValue.num & ( 1LL << i ) )
            {
                if( !expr.IsEmpty() )
                    expr << wxS( " || " );

                expr << wxString::Format( wxS( "A.Orientation == %d deg" ), angles[i] );
            }
        }

        cv.assertion = expr;
        break;
    }

    case FIELD_KIND::SIDE_LAYERS:
    {
        wxString expr;

        if( aValue.num & 1 )
            expr << wxS( "A.Layer == 'F.Cu'" );

        if( aValue.num & 2 )
        {
            if( !expr.IsEmpty() )
                expr << wxS( " || " );

            expr << wxS( "A.Layer == 'B.Cu'" );
        }

        cv.assertion = expr;
        break;
    }
    }

    wxString clause = BuildConstraintClause( cv );

    if( clause.IsEmpty() )
    {
        if( idx >= 0 )
            aRule.constraints.erase( aRule.constraints.begin() + idx );
    }
    else if( idx >= 0 )
    {
        aRule.constraints[idx] = clause;
    }
    else
    {
        aRule.constraints.push_back( clause );
    }

    aRule.modified = true;
}


void RULES_DOC::SeedRule( RULE& aRule, int aCat, BOARD* aBoard )
{
    const CATEGORY& c = Categories()[aCat];

    // Implicit terms first so clause categorisation (via hole size, silk) works
    if( !c.implicitTerms.empty() )
        aRule.condition = BuildCondition( c.implicitTerms, SCOPE(), SCOPE(), false );

    BOARD_DESIGN_SETTINGS* bds = aBoard ? &aBoard->GetDesignSettings() : nullptr;
    std::shared_ptr<NETCLASS> nc;

    if( bds && bds->m_NetSettings )
        nc = bds->m_NetSettings->GetDefaultNetclass();

    auto mm = []( double aMM )
    {
        return (long long) pcbIUScale.mmToIU( aMM );
    };

    auto set = [&]( int aField, long long aValue )
    {
        FIELD_VALUE v;
        v.set = true;
        v.num = aValue;
        SetFieldValue( aRule, aCat, aField, v );
    };

    auto bdsOr = [&]( int BOARD_DESIGN_SETTINGS::*aMember, double aFallbackMM ) -> long long
    {
        if( bds && bds->*aMember > 0 )
            return bds->*aMember;

        return mm( aFallbackMM );
    };

    const wxString& id = c.id;

    if( id == wxS( "clearance" ) )
    {
        set( 0, nc && nc->HasClearance() ? nc->GetClearance()
                                         : bdsOr( &BOARD_DESIGN_SETTINGS::m_MinClearance, 0.2 ) );
    }
    else if( id == wxS( "hole_clearance" ) )
    {
        set( 0, bdsOr( &BOARD_DESIGN_SETTINGS::m_HoleClearance, 0.25 ) );
    }
    else if( id == wxS( "creepage" ) )
    {
        set( 0, mm( 1.0 ) );
    }
    else if( id == wxS( "width" ) )
    {
        set( 0, bdsOr( &BOARD_DESIGN_SETTINGS::m_TrackMinWidth, 0.2 ) );
        set( 1, nc && nc->HasTrackWidth() ? nc->GetTrackWidth() : mm( 0.25 ) );
    }
    else if( id == wxS( "via_style" ) )
    {
        set( 0, bdsOr( &BOARD_DESIGN_SETTINGS::m_ViasMinSize, 0.5 ) );
        set( 1, nc && nc->HasViaDiameter() ? nc->GetViaDiameter() : mm( 0.6 ) );
        set( 3, bdsOr( &BOARD_DESIGN_SETTINGS::m_MinThroughDrill, 0.3 ) );
        set( 4, nc && nc->HasViaDrill() ? nc->GetViaDrill() : mm( 0.3 ) );
    }
    else if( id == wxS( "diff_pairs" ) )
    {
        long long w = nc && nc->HasDiffPairWidth() ? nc->GetDiffPairWidth() : mm( 0.2 );
        long long g = nc && nc->HasDiffPairGap() ? nc->GetDiffPairGap() : mm( 0.25 );
        set( 0, w );
        set( 1, w );
        set( 2, w );
        set( 3, g );
        set( 4, g );
        set( 5, g );
    }
    else if( id == wxS( "mask_expansion" ) )
    {
        set( 0, bds ? bds->m_SolderMaskExpansion : 0 );
    }
    else if( id == wxS( "paste_expansion" ) )
    {
        set( 0, bds ? bds->m_SolderPasteMargin : 0 );
    }
    else if( id == wxS( "polygon_connect" ) )
    {
        set( 0, 1 );
        set( 1, mm( 0.5 ) );
        set( 2, mm( 0.5 ) );
    }
    else if( id == wxS( "annular_ring" ) )
    {
        set( 0, bdsOr( &BOARD_DESIGN_SETTINGS::m_ViasMinAnnularWidth, 0.1 ) );
    }
    else if( id == wxS( "hole_size" ) )
    {
        // No maximum by default: mounting holes would otherwise be flagged
        set( 0, bdsOr( &BOARD_DESIGN_SETTINGS::m_MinThroughDrill, 0.3 ) );
    }
    else if( id == wxS( "hole_to_hole" ) )
    {
        set( 0, bdsOr( &BOARD_DESIGN_SETTINGS::m_HoleToHoleMin, 0.25 ) );
    }
    else if( id == wxS( "mask_sliver" ) )
    {
        set( 0, bdsOr( &BOARD_DESIGN_SETTINGS::m_SolderMaskMinWidth, 0.1 ) );
    }
    else if( id == wxS( "silk_to_mask" ) || id == wxS( "silk_to_silk" ) )
    {
        set( 0, bds ? bds->m_SilkClearance : 0 );
    }
    else if( id == wxS( "board_outline" ) )
    {
        set( 0, bdsOr( &BOARD_DESIGN_SETTINGS::m_CopperEdgeClearance, 0.5 ) );
    }
    else if( id == wxS( "connection_width" ) )
    {
        set( 0, bdsOr( &BOARD_DESIGN_SETTINGS::m_MinConn, 0.1 ) );
    }
    else if( id == wxS( "silk_text" ) )
    {
        set( 0, bdsOr( &BOARD_DESIGN_SETTINGS::m_MinSilkTextHeight, 0.8 ) );
        set( 1, bdsOr( &BOARD_DESIGN_SETTINGS::m_MinSilkTextThickness, 0.08 ) );
    }
    else if( id == wxS( "length" ) )
    {
        set( 0, 0 );
        set( 1, mm( 100.0 ) );
    }
    else if( id == wxS( "matched_lengths" ) )
    {
        set( 0, mm( 1.0 ) );
    }
    else if( id == wxS( "via_count" ) )
    {
        set( 0, 1000 );
    }
    else if( id == wxS( "component_clearance" ) )
    {
        set( 0, mm( 0.25 ) );
    }
    else if( id == wxS( "orientations" ) )
    {
        set( 0, 15 );
    }
    else if( id == wxS( "permitted_layers" ) )
    {
        set( 0, 3 );
    }
    else if( id == wxS( "custom" ) )
    {
        // A constraint no other category claims, so the rule lands under Custom Rules
        aRule.constraints.push_back( wxS( "(constraint physical_clearance (min 0mm))" ) );
    }
}


wxString RULES_DOC::Attributes( const RULE& aRule, int aCat, EDA_UNITS aUnits ) const
{
    const CATEGORY& c = Categories()[aCat];
    wxString        out;

    if( aCat == CustomCategory() )
    {
        for( const wxString& clause : aRule.constraints )
        {
            if( CategoryOfClause( aRule, clause ) != aCat )
                continue;

            if( !out.IsEmpty() )
                out << wxS( "   " );

            out << clause;
        }

        if( aRule.parseError )
            out = _( "Syntax error: " ) + aRule.errorMsg;

        return out;
    }

    for( size_t i = 0; i < c.fields.size(); ++i )
    {
        const FIELD& f = c.fields[i];
        FIELD_VALUE  v = GetFieldValue( aRule, aCat, (int) i );

        if( !v.set )
            continue;

        wxString text;

        switch( f.kind )
        {
        case FIELD_KIND::LENGTH:
            text = EDA_UNIT_UTILS::UI::MessageTextFromValue( pcbIUScale, aUnits, v.num );
            break;

        case FIELD_KIND::COUNT:
            text = wxString::Format( wxS( "%lld" ), v.num );
            break;

        case FIELD_KIND::CONNECT_STYLE:
        {
            static const wxString styles[] = { _( "Direct Connect" ), _( "Relief Connect" ),
                                               _( "No Connect" ) };
            text = styles[std::clamp( (int) v.num, 0, 2 )];
            break;
        }

        case FIELD_KIND::FLAG:
            if( !v.num )
                continue;

            text = _( "Yes" );
            break;

        case FIELD_KIND::ORIENTATION:
        {
            static const wxChar* angles[] = { wxS( "0" ), wxS( "90" ), wxS( "180" ),
                                              wxS( "270" ) };

            for( int a = 0; a < 4; ++a )
            {
                if( v.num & ( 1LL << a ) )
                {
                    if( !text.IsEmpty() )
                        text << wxS( ", " );

                    text << angles[a] << wxS( "°" );
                }
            }

            break;
        }

        case FIELD_KIND::SIDE_LAYERS:
            if( v.num & 1 )
                text << _( "Top" );

            if( v.num & 2 )
                text << ( text.IsEmpty() ? wxString() : wxString( wxS( ", " ) ) ) << _( "Bottom" );

            break;
        }

        if( !out.IsEmpty() )
            out << wxS( "   " );

        out << f.label << wxS( " = " ) << text;
    }

    return out;
}


// ------------------------------------------------------------------------------------------
// Board Setup links
// ------------------------------------------------------------------------------------------

const std::vector<BOARD_LINK>& BoardLinks()
{
    using B = BOARD_DESIGN_SETTINGS;

    static const std::vector<BOARD_LINK> s_links = {
        { wxS( "clearance" ), 0, &B::m_MinClearance },
        { wxS( "hole_clearance" ), 0, &B::m_HoleClearance },
        { wxS( "width" ), 0, &B::m_TrackMinWidth },
        { wxS( "via_style" ), 0, &B::m_ViasMinSize },
        { wxS( "mask_expansion" ), 0, &B::m_SolderMaskExpansion },
        { wxS( "paste_expansion" ), 0, &B::m_SolderPasteMargin },
        { wxS( "polygon_connect" ), 3, &B::m_MinResolvedSpokes },
        { wxS( "annular_ring" ), 0, &B::m_ViasMinAnnularWidth },
        { wxS( "hole_size" ), 0, &B::m_MinThroughDrill },
        { wxS( "hole_to_hole" ), 0, &B::m_HoleToHoleMin },
        { wxS( "mask_sliver" ), 0, &B::m_SolderMaskMinWidth },
        { wxS( "silk_to_mask" ), 0, &B::m_SilkClearance },
        { wxS( "silk_to_silk" ), 0, &B::m_SilkClearance },
        { wxS( "board_outline" ), 0, &B::m_CopperEdgeClearance },
        { wxS( "connection_width" ), 0, &B::m_MinConn },
        { wxS( "silk_text" ), 0, &B::m_MinSilkTextHeight },
        { wxS( "silk_text" ), 1, &B::m_MinSilkTextThickness },
    };

    return s_links;
}


bool HasBoardLink( int aCat )
{
    const wxString& id = Categories()[aCat].id;

    for( const BOARD_LINK& link : BoardLinks() )
    {
        if( link.catId == id )
            return true;
    }

    return false;
}


const BOARD_LINK* FindBoardLink( int aCat, int aField )
{
    const wxString& id = Categories()[aCat].id;

    for( const BOARD_LINK& link : BoardLinks() )
    {
        if( link.catId == id && link.field == aField )
            return &link;
    }

    return nullptr;
}

} // namespace PCB_RULES
