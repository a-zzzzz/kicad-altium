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

/**
 * @file pcb_rules_model.h
 *
 * Data model behind the "PCB Rules and Constraints Editor" (Design > Rules...).
 *
 * The editor presents KiCad's custom design rules (the project's .kicad_dru file) as a tree of
 * rule categories, each holding named rules with a priority, an enabled flag, one or two
 * scopes and a set of constraint values.  This file holds everything that does not need a GUI:
 *
 *  - the category table (which KiCad constraint keywords each category edits),
 *  - a loss-less reader/writer for .kicad_dru files (rules that are not edited are written back
 *    verbatim; top-level comments and unknown items are preserved; disabled rules are kept as
 *    "#~ " comment lines so KiCad ignores them),
 *  - scope <-> condition-expression conversion,
 *  - field <-> constraint-clause conversion.
 *
 * Priority follows KiCad's semantics: a rule later in the file overrides an earlier one, so the
 * highest priority rule (priority 1) of a category is the last one of that category in the file.
 */

#ifndef PCB_RULES_MODEL_H
#define PCB_RULES_MODEL_H

#include <optional>
#include <vector>
#include <wx/string.h>

// Complete type needed: BOARD_LINK holds pointers to its members (MSVC member pointer size
// depends on the class being complete).
#include <board_design_settings.h>

class BOARD;
enum class EDA_UNITS;


namespace PCB_RULES
{

enum class FIELD_KIND
{
    LENGTH,         ///< a distance, one part (min/opt/max) of a constraint
    COUNT,          ///< a unitless integer, one part of a constraint
    CONNECT_STYLE,  ///< zone_connection: 0 = solid, 1 = thermal reliefs, 2 = none
    FLAG,           ///< an option token on a constraint (e.g. within_diff_pairs)
    ORIENTATION,    ///< assertion on A.Orientation; bit mask 1=0deg 2=90 4=180 8=270
    SIDE_LAYERS     ///< assertion on A.Layer; bit mask 1=top 2=bottom
};

enum class PART
{
    NONE,
    MIN,
    OPT,
    MAX
};

struct FIELD
{
    wxString   label;
    wxString   keyword;   ///< KiCad constraint keyword this field edits
    PART       part;
    FIELD_KIND kind;
    wxString   token;     ///< FLAG: the option token
};

struct CATEGORY
{
    wxString              id;
    wxString              group;            ///< tree group (Electrical, Routing, ...)
    wxString              name;             ///< category shown in the tree
    wxString              defaultRuleName;  ///< base name for new rules
    bool                  supported;        ///< false: shown for orientation only
    bool                  binary;           ///< rule has a second-object scope
    std::vector<wxString> keywords;         ///< constraint keywords edited by this category
    std::vector<wxString> implicitTerms;    ///< condition terms implied by the category
    std::vector<FIELD>    fields;
    wxString              note;
};

/// All categories in tree order.  The last one is the "Custom" (other KiCad constraints) one.
const std::vector<CATEGORY>& Categories();
int                          CategoryIndex( const wxString& aId );
int                          CustomCategory();
std::vector<wxString>        Groups();


struct FIELD_VALUE
{
    bool      set = false;
    long long num = 0;
};


/// Values of one parsed constraint clause.
struct CLAUSE_VALUES
{
    wxString           keyword;
    std::optional<int> min;
    std::optional<int> opt;
    std::optional<int> max;
    bool               withinDiffPairs = false;
    std::optional<int> zoneConnection;   ///< 0 solid, 1 thermal, 2 none
    wxString           assertion;
    bool               ok = false;
};

bool     ParseConstraintClause( const wxString& aClause, CLAUSE_VALUES& aValues );
wxString BuildConstraintClause( const CLAUSE_VALUES& aValues );
wxString FormatMM( long long aIU );


enum class SCOPE_KIND
{
    ALL = 0,
    NET,
    NETCLASS,
    LAYER,
    NET_AND_LAYER,
    CUSTOM
};

struct SCOPE
{
    SCOPE_KIND kind = SCOPE_KIND::ALL;
    wxString   net;        ///< net name, or net class name for NETCLASS
    wxString   layer;      ///< canonical layer name
    wxString   query;      ///< CUSTOM expression

    wxString ToExpr( wxChar aObj ) const;
    wxString Describe( wxChar aObj ) const;
};


struct RULE
{
    int                   uid = 0;
    wxString              name;
    wxString              comment;
    bool                  enabled = true;
    wxString              layerClause;     ///< "(layer outer)" etc, or empty
    wxString              condition;       ///< condition expression (no quotes)
    wxString              severityClause;  ///< "(severity warning)" etc, or empty
    std::vector<wxString> constraints;     ///< "(constraint ...)" clause texts
    std::vector<wxString> others;          ///< any other clause, kept verbatim
    wxString              originalText;    ///< text as read; written back while not modified
    bool                  modified = false;
    bool                  parseError = false;
    wxString              errorMsg;

    /// The rule as .kicad_dru text (without the "#~ " prefix of disabled rules).
    wxString ToText() const;

    /// Regenerate the text from the parts.
    wxString Generate() const;

    bool HasKeyword( const wxString& aKeyword ) const;
    int  FindClause( const wxString& aKeyword ) const;
};

/// Parse one "(rule ...)" text.  On a syntax error aRule.parseError is set, but whatever could be
/// split out is still filled in.
void ParseRule( const wxString& aText, RULE& aRule );

/// Check a complete rule text with KiCad's own rule parser.  Returns an empty string when valid.
wxString CheckRuleSyntax( const wxString& aText );


/// Split a condition into the category's hidden (implicit) terms and up to two scopes.
void ParseScopes( const wxString& aCondition, const CATEGORY& aCategory, SCOPE& aFirst,
                  SCOPE& aSecond, std::vector<wxString>& aHiddenTerms );

wxString BuildCondition( const std::vector<wxString>& aHiddenTerms, const SCOPE& aFirst,
                         const SCOPE& aSecond, bool aBinary );


class RULES_DOC
{
public:
    struct ITEM
    {
        bool     isRule = false;
        RULE     rule;
        wxString raw;
    };

    void     Clear();
    bool     LoadFile( const wxString& aPath );
    void     LoadFromString( const wxString& aText );
    wxString Serialize() const;
    bool     SaveFile( const wxString& aPath ) const;

    RULE*       FindRule( int aUid );
    const RULE* FindRule( int aUid ) const;
    RULE*       FindRuleByName( const wxString& aName );

    /// The category a constraint clause of aRule belongs to.
    int              CategoryOfClause( const RULE& aRule, const wxString& aClause ) const;
    std::vector<int> CategoriesOf( const RULE& aRule ) const;
    bool             InCategory( const RULE& aRule, int aCat ) const;

    /// Rules of a category, highest priority (priority 1) first.
    std::vector<RULE*> RulesInCategory( int aCat );
    std::vector<RULE*> AllRules();
    int                Priority( const RULE& aRule, int aCat );

    /// Raise (aUp) or lower the priority of a rule within a category.
    bool MovePriority( int aCat, int aUid, bool aUp );

    /// Add a new rule (highest priority in its category).  Returns its uid.
    int  AddRule( const RULE& aRule );

    /// Remove a rule from a category: its clauses for that category are dropped, and the whole
    /// rule is deleted when it has no constraints left (or it is a custom rule).
    void RemoveFromCategory( int aUid, int aCat );

    wxString UniqueName( const wxString& aBase ) const;

    FIELD_VALUE GetFieldValue( const RULE& aRule, int aCat, int aField ) const;
    void        SetFieldValue( RULE& aRule, int aCat, int aField, const FIELD_VALUE& aValue );

    /// Fill a new rule's constraints with sensible starting values for its category.
    void SeedRule( RULE& aRule, int aCat, BOARD* aBoard );

    wxString Attributes( const RULE& aRule, int aCat, EDA_UNITS aUnits ) const;

    std::vector<ITEM> m_items;

private:
    void addChunk( const wxString& aChunk, bool aEnabled );
    void flushDisabled( wxString& aBuffer );

    int m_nextUid = 1;
};


/// Board-wide minimums of Board Setup shown as each category's lowest priority rule.
struct BOARD_LINK
{
    wxString                   catId;
    int                        field;
    int BOARD_DESIGN_SETTINGS::*member;
};

const std::vector<BOARD_LINK>& BoardLinks();
bool                           HasBoardLink( int aCat );
const BOARD_LINK*              FindBoardLink( int aCat, int aField );

} // namespace PCB_RULES

#endif // PCB_RULES_MODEL_H
