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

#include <altium_query.h>

#include <cmath>
#include <optional>
#include <eda_item.h>
#include <inspectable.h>
#include <geometry/eda_angle.h>
#include <properties/property.h>
#include <properties/property_mgr.h>
#include <wx/intl.h>


namespace ALTIUM_QUERY
{

namespace
{

wxString normalise( const wxString& aName )
{
    wxString out;

    for( wxUniChar c : aName )
    {
        if( c != ' ' && c != '_' && c != '-' )
            out += c;
    }

    return out.Lower();
}

} // namespace


bool VALUE::AsBool() const
{
    switch( type )
    {
    case BOOL: return b;
    case NUM:  return num != 0.0;
    case STR:  return !str.IsEmpty() && str.CmpNoCase( wxS( "false" ) ) != 0;
    default:   return false;
    }
}


bool LikeMatch( const wxString& aText, const wxString& aPattern )
{
    return aText.Lower().Matches( aPattern.Lower() );
}


bool StringEquals( const wxString& aA, const wxString& aB )
{
    return aA.CmpNoCase( aB ) == 0;
}


const std::vector<wxString>& Operators()
{
    static const std::vector<wxString> ops = { wxS( "And" ), wxS( "Or" ), wxS( "Not" ),
                                               wxS( "Xor" ), wxS( "Like" ), wxS( "Div" ),
                                               wxS( "Mod" ), wxS( "True" ), wxS( "False" ),
                                               wxS( "All" ) };
    return ops;
}


// ---- properties as fields ----------------------------------------------------------------------

bool ADAPTER::IsPropertyName( const wxString& aName )
{
    if( m_propertyNames.empty() )
    {
        PROPERTY_MANAGER& propMgr = PROPERTY_MANAGER::Instance();

        for( const PROPERTY_MANAGER::CLASS_INFO& cls : propMgr.GetAllClasses() )
        {
            if( !propMgr.IsOfType( cls.type, propertyBaseType() ) )
                continue;

            for( PROPERTY_BASE* prop : propMgr.GetProperties( cls.type ) )
                m_propertyNames[normalise( prop->Name() )] = true;
        }

        m_propertyNames[wxS( " " )] = true;   // computed once even if there are none
    }

    return m_propertyNames.count( normalise( aName ) ) > 0;
}


VALUE ADAPTER::readProperty( INSPECTABLE* aObject, size_t aTypeHash, const wxString& aName )
{
    PROPERTY_MANAGER& propMgr = PROPERTY_MANAGER::Instance();
    wxString          wanted = normalise( aName );
    PROPERTY_BASE*    prop = nullptr;

    for( PROPERTY_BASE* candidate : propMgr.GetProperties( aTypeHash ) )
    {
        if( normalise( candidate->Name() ) == wanted )
        {
            prop = candidate;
            break;
        }
    }

    if( !prop || !prop->Available( aObject ) )
        return VALUE();

    bool  distance = prop->Display() == PT_SIZE || prop->Display() == PT_COORD;
    wxAny any = aObject->Get( prop );

    // Read through wxAny: INSPECTABLE::Get<T> is only instantiated in the editors
    if( any.CheckType<int>() )
        return VALUE::Num( any.As<int>(), distance );

    if( any.CheckType<long long>() )
        return VALUE::Num( (double) any.As<long long>(), distance );

    if( any.CheckType<std::optional<int>>() )
    {
        std::optional<int> val = any.As<std::optional<int>>();
        return val ? VALUE::Num( *val, distance ) : VALUE();
    }

    if( any.CheckType<double>() )
        return VALUE::Num( any.As<double>(), false );

    if( any.CheckType<std::optional<double>>() )
    {
        std::optional<double> val = any.As<std::optional<double>>();
        return val ? VALUE::Num( *val, false ) : VALUE();
    }

    if( any.CheckType<bool>() )
        return VALUE::Bool( any.As<bool>() );

    if( any.CheckType<EDA_ANGLE>() )
        return VALUE::Num( any.As<EDA_ANGLE>().AsDegrees(), false );

    wxString str;

    if( any.GetAs<wxString>( &str ) )
        return VALUE::Str( str );

    return VALUE();
}


VALUE ADAPTER::PropertyField( EDA_ITEM* aItem, const wxString& aName )
{
    return readProperty( aItem, TYPE_HASH( *aItem ), aName );
}


// ---- syntax tree ----------------------------------------------------------------------------

struct QUERY::NODE
{
    enum OP { LITERAL, KEY, FUNC, FIELD, PROPERTY, NOT, AND, OR, XOR, CMP, NEG };

    OP                                 op = LITERAL;
    VALUE                              literal;
    const KEYWORD*                     key = nullptr;
    wxString                           property;
    wxString                           cmp;          // = <> < > <= >= like
    std::vector<std::unique_ptr<NODE>> kids;
};


namespace
{

struct TOKEN
{
    enum TYPE { END, IDENT, NUMBER, STRING, OP, LPAREN, RPAREN, COMMA };

    TYPE     type = END;
    wxString text;
    double   num = 0.0;
    wxString unit;
    int      pos = 0;
};


class PARSER
{
public:
    PARSER( const wxString& aText, ADAPTER& aAdapter ) :
            m_text( aText ),
            m_adapter( aAdapter )
    {}

    bool Tokenise()
    {
        size_t i = 0;
        size_t n = m_text.length();

        while( i < n )
        {
            wxUniChar c = m_text[i];

            if( wxIsspace( c ) )
            {
                i++;
                continue;
            }

            TOKEN tok;
            tok.pos = (int) i;

            if( c == '\'' || c == '"' )
            {
                wxUniChar quote = c;
                size_t    j = i + 1;
                wxString  str;

                // A doubled quote stands for itself, as in Altium
                while( j < n )
                {
                    if( m_text[j] == quote )
                    {
                        if( j + 1 < n && m_text[j + 1] == quote )
                        {
                            str += quote;
                            j += 2;
                            continue;
                        }

                        break;
                    }

                    str += m_text[j++];
                }

                if( j >= n )
                    return fail( _( "Missing closing quote" ), (int) i );

                tok.type = TOKEN::STRING;
                tok.text = str;
                i = j + 1;
            }
            else if( wxIsdigit( c ) || ( c == '.' && i + 1 < n && wxIsdigit( m_text[i + 1] ) ) )
            {
                size_t j = i;

                while( j < n && ( wxIsdigit( m_text[j] ) || m_text[j] == '.' ) )
                    j++;

                // Exponent (1e3) only when digits follow, so "1mm" keeps its unit
                if( j + 1 < n && ( m_text[j] == 'e' || m_text[j] == 'E' )
                        && wxIsdigit( m_text[j + 1] ) )
                {
                    j++;

                    while( j < n && wxIsdigit( m_text[j] ) )
                        j++;
                }

                m_text.Mid( i, j - i ).ToCDouble( &tok.num );

                size_t k = j;

                while( k < n && wxIsalpha( m_text[k] ) )
                    k++;

                tok.unit = m_text.Mid( j, k - j ).Lower();

                if( !tok.unit.IsEmpty() && tok.unit != wxS( "mil" ) && tok.unit != wxS( "mils" )
                        && tok.unit != wxS( "mm" ) && tok.unit != wxS( "in" )
                        && tok.unit != wxS( "um" ) && tok.unit != wxS( "deg" ) )
                {
                    return fail( wxString::Format( _( "Unknown unit '%s'" ), tok.unit ), (int) j );
                }

                if( tok.unit == wxS( "mils" ) )
                    tok.unit = wxS( "mil" );

                tok.type = TOKEN::NUMBER;
                tok.text = m_text.Mid( i, k - i );
                i = k;
            }
            else if( wxIsalpha( c ) || c == '_' )
            {
                size_t j = i;

                while( j < n && ( wxIsalnum( m_text[j] ) || m_text[j] == '_' ) )
                    j++;

                tok.text = m_text.Mid( i, j - i );
                i = j;

                wxString lower = tok.text.Lower();

                if( lower == wxS( "and" ) || lower == wxS( "or" ) || lower == wxS( "not" )
                        || lower == wxS( "xor" ) || lower == wxS( "like" ) )
                {
                    tok.type = TOKEN::OP;
                    tok.text = lower;
                }
                else
                {
                    tok.type = TOKEN::IDENT;
                }
            }
            else if( c == '(' )
            {
                tok.type = TOKEN::LPAREN;
                i++;
            }
            else if( c == ')' )
            {
                tok.type = TOKEN::RPAREN;
                i++;
            }
            else if( c == ',' )
            {
                tok.type = TOKEN::COMMA;
                i++;
            }
            else
            {
                static const wxString twoChar[] = { wxS( "<>" ), wxS( "<=" ), wxS( ">=" ),
                                                    wxS( "==" ), wxS( "!=" ), wxS( "&&" ),
                                                    wxS( "||" ) };
                wxString two = m_text.Mid( i, 2 );
                bool     found = false;

                for( const wxString& op : twoChar )
                {
                    if( two == op )
                    {
                        tok.type = TOKEN::OP;
                        tok.text = op;
                        i += 2;
                        found = true;
                        break;
                    }
                }

                if( !found )
                {
                    if( c == '=' || c == '<' || c == '>' || c == '!' || c == '-' )
                    {
                        tok.type = TOKEN::OP;
                        tok.text = wxString( c );
                        i++;
                    }
                    else
                    {
                        return fail( wxString::Format( _( "Unexpected character '%s'" ),
                                                       wxString( c ) ),
                                     (int) i );
                    }
                }

                // Spellings Altium also accepts
                if( tok.text == wxS( "&&" ) )
                    tok.text = wxS( "and" );
                else if( tok.text == wxS( "||" ) )
                    tok.text = wxS( "or" );
                else if( tok.text == wxS( "!" ) )
                    tok.text = wxS( "not" );
                else if( tok.text == wxS( "==" ) )
                    tok.text = wxS( "=" );
                else if( tok.text == wxS( "!=" ) )
                    tok.text = wxS( "<>" );
            }

            m_tokens.push_back( tok );
        }

        TOKEN end;
        end.pos = (int) n;
        m_tokens.push_back( end );
        return true;
    }

    std::unique_ptr<QUERY::NODE> Parse()
    {
        std::unique_ptr<QUERY::NODE> root = parseOr();

        if( root && peek().type != TOKEN::END )
        {
            if( peek().type == TOKEN::RPAREN )
                fail( _( "Unmatched ')'" ), peek().pos );
            else
                fail( wxString::Format( _( "Expected And/Or before '%s'" ), describe( peek() ) ),
                      peek().pos );

            return nullptr;
        }

        return root;
    }

    wxString m_error;
    int      m_errorPos = -1;

private:
    using NODEP = std::unique_ptr<QUERY::NODE>;

    const TOKEN& peek() const { return m_tokens[m_pos]; }
    const TOKEN& next()       { return m_tokens[m_pos < m_tokens.size() - 1 ? m_pos++ : m_pos]; }

    bool isOp( const wxString& aOp ) const
    {
        return peek().type == TOKEN::OP && peek().text == aOp;
    }

    bool fail( const wxString& aMsg, int aPos )
    {
        if( m_error.IsEmpty() )
        {
            m_error = aMsg;
            m_errorPos = aPos;
        }

        return false;
    }

    static wxString describe( const TOKEN& aTok )
    {
        switch( aTok.type )
        {
        case TOKEN::END:    return _( "end of query" );
        case TOKEN::LPAREN: return wxS( "(" );
        case TOKEN::RPAREN: return wxS( ")" );
        case TOKEN::COMMA:  return wxS( "," );
        case TOKEN::STRING: return wxS( "'" ) + aTok.text + wxS( "'" );
        default:            return aTok.text;
        }
    }

    NODEP binary( QUERY::NODE::OP aOp, NODEP aLeft, NODEP aRight )
    {
        if( !aLeft || !aRight )
            return nullptr;

        NODEP node = std::make_unique<QUERY::NODE>();
        node->op = aOp;
        node->kids.push_back( std::move( aLeft ) );
        node->kids.push_back( std::move( aRight ) );
        return node;
    }

    NODEP parseOr()
    {
        NODEP left = parseXor();

        while( left && isOp( wxS( "or" ) ) )
        {
            next();
            left = binary( QUERY::NODE::OR, std::move( left ), parseXor() );
        }

        return left;
    }

    NODEP parseXor()
    {
        NODEP left = parseAnd();

        while( left && isOp( wxS( "xor" ) ) )
        {
            next();
            left = binary( QUERY::NODE::XOR, std::move( left ), parseAnd() );
        }

        return left;
    }

    NODEP parseAnd()
    {
        NODEP left = parseNot();

        while( left && isOp( wxS( "and" ) ) )
        {
            next();
            left = binary( QUERY::NODE::AND, std::move( left ), parseNot() );
        }

        return left;
    }

    NODEP parseNot()
    {
        if( isOp( wxS( "not" ) ) )
        {
            next();
            NODEP operand = parseNot();

            if( !operand )
                return nullptr;

            NODEP node = std::make_unique<QUERY::NODE>();
            node->op = QUERY::NODE::NOT;
            node->kids.push_back( std::move( operand ) );
            return node;
        }

        return parseCompare();
    }

    NODEP parseCompare()
    {
        NODEP left = parsePrimary();

        if( !left )
            return nullptr;

        static const wxString cmps[] = { wxS( "=" ), wxS( "<>" ), wxS( "<" ), wxS( ">" ),
                                         wxS( "<=" ), wxS( ">=" ), wxS( "like" ) };

        for( const wxString& cmp : cmps )
        {
            if( isOp( cmp ) )
            {
                next();
                NODEP right = parsePrimary();

                if( !right )
                    return nullptr;

                NODEP node = std::make_unique<QUERY::NODE>();
                node->op = QUERY::NODE::CMP;
                node->cmp = cmp;
                node->kids.push_back( std::move( left ) );
                node->kids.push_back( std::move( right ) );
                return node;
            }
        }

        return left;
    }

    const KEYWORD* findKeyword( const wxString& aName ) const
    {
        for( const KEYWORD& key : m_adapter.Keywords() )
        {
            if( key.name.CmpNoCase( aName ) == 0 )
                return &key;
        }

        return nullptr;
    }

    NODEP parsePrimary()
    {
        const TOKEN& tok = peek();
        NODEP        node = std::make_unique<QUERY::NODE>();

        switch( tok.type )
        {
        case TOKEN::LPAREN:
        {
            int open = tok.pos;
            next();
            NODEP inner = parseOr();

            if( !inner )
                return nullptr;

            if( peek().type != TOKEN::RPAREN )
            {
                fail( _( "Missing ')'" ), open );
                return nullptr;
            }

            next();
            return inner;
        }

        case TOKEN::NUMBER:
            node->literal = VALUE::Num( tok.num, false );
            node->literal.unit = tok.unit;
            node->literal.literal = true;
            next();
            return node;

        case TOKEN::STRING:
            node->literal = VALUE::Str( tok.text );
            node->literal.literal = true;
            next();
            return node;

        case TOKEN::OP:
            if( tok.text == wxS( "-" ) )
            {
                next();

                if( peek().type != TOKEN::NUMBER )
                {
                    fail( _( "Expected a number after '-'" ), peek().pos );
                    return nullptr;
                }

                node->literal = VALUE::Num( -peek().num, false );
                node->literal.unit = peek().unit;
                node->literal.literal = true;
                next();
                return node;
            }

            fail( wxString::Format( _( "Unexpected '%s'" ), describe( tok ) ), tok.pos );
            return nullptr;

        case TOKEN::IDENT:
        {
            wxString name = tok.text;
            int      pos = tok.pos;
            next();

            if( !name.CmpNoCase( wxS( "true" ) ) || !name.CmpNoCase( wxS( "all" ) ) )
            {
                node->literal = VALUE::Bool( true );
                return node;
            }

            if( !name.CmpNoCase( wxS( "false" ) ) )
            {
                node->literal = VALUE::Bool( false );
                return node;
            }

            const KEYWORD* key = findKeyword( name );

            if( peek().type == TOKEN::LPAREN )
            {
                if( !key || key->kind != KIND::FUNCTION )
                {
                    fail( wxString::Format( key ? _( "'%s' takes no arguments" )
                                                : _( "Unknown function '%s'" ),
                                            name ),
                          pos );
                    return nullptr;
                }

                next();
                node->op = QUERY::NODE::FUNC;
                node->key = key;

                if( peek().type != TOKEN::RPAREN )
                {
                    while( true )
                    {
                        NODEP arg = parsePrimary();

                        if( !arg )
                            return nullptr;

                        node->kids.push_back( std::move( arg ) );

                        if( peek().type == TOKEN::COMMA )
                        {
                            next();
                            continue;
                        }

                        break;
                    }
                }

                if( peek().type != TOKEN::RPAREN )
                {
                    fail( wxString::Format( _( "Missing ')' after the arguments of '%s'" ), name ),
                          peek().pos );
                    return nullptr;
                }

                next();
                return node;
            }

            if( key && key->kind == KIND::FUNCTION )
            {
                fail( wxString::Format( _( "'%s' needs arguments, e.g. %s" ), name, key->example ),
                      pos );
                return nullptr;
            }

            if( key )
            {
                node->op = key->kind == KIND::KEYWORD ? QUERY::NODE::KEY : QUERY::NODE::FIELD;
                node->key = key;
                return node;
            }

            if( m_adapter.IsPropertyName( name ) )
            {
                node->op = QUERY::NODE::PROPERTY;
                node->property = name;
                return node;
            }

            fail( wxString::Format( _( "Unknown keyword '%s'" ), name ), pos );
            return nullptr;
        }

        default:
            fail( wxString::Format( _( "Unexpected %s" ), describe( tok ) ), tok.pos );
            return nullptr;
        }
    }

    wxString           m_text;
    ADAPTER&           m_adapter;
    std::vector<TOKEN> m_tokens;
    size_t             m_pos = 0;
};


VALUE evaluate( QUERY::NODE* aNode, EDA_ITEM* aItem, ADAPTER& aAdapter );


bool compare( const VALUE& aLeft, const wxString& aCmp, const VALUE& aRight, ADAPTER& aAdapter )
{
    if( aLeft.type == VALUE::NONE || aRight.type == VALUE::NONE )
        return false;

    if( aCmp == wxS( "like" ) )
    {
        wxString text = aLeft.type == VALUE::STR ? aLeft.str : wxString::FromCDouble( aLeft.num );
        wxString pattern = aRight.type == VALUE::STR ? aRight.str
                                                     : wxString::FromCDouble( aRight.num );
        return LikeMatch( text, pattern );
    }

    auto asNumber =
            []( const VALUE& v, double& out ) -> bool
            {
                if( v.type == VALUE::NUM )
                {
                    out = v.num;
                    return true;
                }

                if( v.type == VALUE::BOOL )
                {
                    out = v.b ? 1 : 0;
                    return true;
                }

                if( v.type == VALUE::STR )
                    return v.str.ToCDouble( &out );

                return false;
            };

    // Numbers: a literal next to a length takes its unit (or the editor's units) into account
    if( aLeft.type == VALUE::NUM || aRight.type == VALUE::NUM
            || ( aLeft.type == VALUE::BOOL && aRight.type == VALUE::BOOL ) )
    {
        double l = 0, r = 0;

        if( asNumber( aLeft, l ) && asNumber( aRight, r ) )
        {
            if( aLeft.distance && aRight.literal && aRight.type == VALUE::NUM )
                r = aAdapter.ToInternalUnits( r, aRight.unit );
            else if( aRight.distance && aLeft.literal && aLeft.type == VALUE::NUM )
                l = aAdapter.ToInternalUnits( l, aLeft.unit );

            // Lengths are integers in internal units; allow for rounding of typed values
            double eps = ( aLeft.distance || aRight.distance ) ? 1.0 : 1e-9;

            if( aCmp == wxS( "=" ) )  return std::fabs( l - r ) < eps;
            if( aCmp == wxS( "<>" ) ) return std::fabs( l - r ) >= eps;
            if( aCmp == wxS( "<" ) )  return l < r - eps;
            if( aCmp == wxS( ">" ) )  return l > r + eps;
            if( aCmp == wxS( "<=" ) ) return l <= r + eps;
            if( aCmp == wxS( ">=" ) ) return l >= r - eps;
        }
    }

    if( aLeft.type == VALUE::BOOL || aRight.type == VALUE::BOOL )
    {
        bool l = aLeft.AsBool();
        bool r = aRight.AsBool();

        if( aRight.type == VALUE::STR )
            r = !aRight.str.CmpNoCase( wxS( "true" ) );

        if( aLeft.type == VALUE::STR )
            l = !aLeft.str.CmpNoCase( wxS( "true" ) );

        if( aCmp == wxS( "=" ) )  return l == r;
        if( aCmp == wxS( "<>" ) ) return l != r;
        return false;
    }

    wxString l = aLeft.type == VALUE::STR ? aLeft.str : wxString::FromCDouble( aLeft.num );
    wxString r = aRight.type == VALUE::STR ? aRight.str : wxString::FromCDouble( aRight.num );
    int      c = l.CmpNoCase( r );

    if( aCmp == wxS( "=" ) )  return c == 0;
    if( aCmp == wxS( "<>" ) ) return c != 0;
    if( aCmp == wxS( "<" ) )  return c < 0;
    if( aCmp == wxS( ">" ) )  return c > 0;
    if( aCmp == wxS( "<=" ) ) return c <= 0;
    if( aCmp == wxS( ">=" ) ) return c >= 0;
    return false;
}


VALUE evaluate( QUERY::NODE* aNode, EDA_ITEM* aItem, ADAPTER& aAdapter )
{
    switch( aNode->op )
    {
    case QUERY::NODE::LITERAL:
        return aNode->literal;

    case QUERY::NODE::KEY:
        return VALUE::Bool( aAdapter.EvalKeyword( aItem, *aNode->key ) );

    case QUERY::NODE::FIELD:
        return aAdapter.EvalField( aItem, *aNode->key );

    case QUERY::NODE::PROPERTY:
        return aAdapter.PropertyField( aItem, aNode->property );

    case QUERY::NODE::FUNC:
    {
        std::vector<VALUE> args;

        for( std::unique_ptr<QUERY::NODE>& kid : aNode->kids )
            args.push_back( evaluate( kid.get(), aItem, aAdapter ) );

        return VALUE::Bool( aAdapter.EvalFunction( aItem, *aNode->key, args ) );
    }

    case QUERY::NODE::NOT:
        return VALUE::Bool( !evaluate( aNode->kids[0].get(), aItem, aAdapter ).AsBool() );

    case QUERY::NODE::AND:
        return VALUE::Bool( evaluate( aNode->kids[0].get(), aItem, aAdapter ).AsBool()
                            && evaluate( aNode->kids[1].get(), aItem, aAdapter ).AsBool() );

    case QUERY::NODE::OR:
        return VALUE::Bool( evaluate( aNode->kids[0].get(), aItem, aAdapter ).AsBool()
                            || evaluate( aNode->kids[1].get(), aItem, aAdapter ).AsBool() );

    case QUERY::NODE::XOR:
        return VALUE::Bool( evaluate( aNode->kids[0].get(), aItem, aAdapter ).AsBool()
                            != evaluate( aNode->kids[1].get(), aItem, aAdapter ).AsBool() );

    case QUERY::NODE::CMP:
        return VALUE::Bool( compare( evaluate( aNode->kids[0].get(), aItem, aAdapter ), aNode->cmp,
                                     evaluate( aNode->kids[1].get(), aItem, aAdapter ),
                                     aAdapter ) );

    default:
        return VALUE();
    }
}

} // namespace


QUERY::QUERY() = default;
QUERY::~QUERY() = default;


bool QUERY::Compile( const wxString& aText, ADAPTER& aAdapter )
{
    m_root.reset();
    m_adapter = &aAdapter;
    m_error.clear();
    m_errorPos = -1;

    if( aText.Strip( wxString::both ).IsEmpty() )
        return true;

    PARSER parser( aText, aAdapter );

    if( parser.Tokenise() )
        m_root = parser.Parse();

    if( !m_root )
    {
        m_error = parser.m_error.IsEmpty() ? _( "Invalid query" ) : parser.m_error;
        m_errorPos = parser.m_errorPos;
        return false;
    }

    return true;
}


bool QUERY::Matches( EDA_ITEM* aItem )
{
    if( !m_root || !m_adapter || !aItem )
        return false;

    return evaluate( m_root.get(), aItem, *m_adapter ).AsBool();
}

} // namespace ALTIUM_QUERY
