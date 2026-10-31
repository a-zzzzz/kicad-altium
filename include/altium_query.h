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

#pragma once

#include <map>
#include <memory>
#include <vector>
#include <wx/string.h>

class EDA_ITEM;
class INSPECTABLE;


/**
 * Altium's query language, as typed in the PCB Filter and SCH Filter panels:
 *
 *     IsPad And (InNet('GND') Or Net Like 'VCC*') And Not OnLayer('Bottom Layer')
 *     IsTrack And Width > 10mil
 *
 * Keywords, functions and fields are case-insensitive and come from an ADAPTER that knows the
 * editor's objects.  Any property of an object (as shown in the Properties panel) can also be
 * used as a field, with its spaces left out (HoleSize, PositionX, ...).
 */
namespace ALTIUM_QUERY
{

struct VALUE
{
    enum TYPE { NONE, BOOL, NUM, STR };

    TYPE     type = NONE;
    bool     b = false;
    double   num = 0.0;      ///< a field's distance is in internal units; a literal's in its unit
    wxString str;
    wxString unit;           ///< literal numbers only: "mil", "mm", "in" or "" (current units)
    bool     literal = false;
    bool     distance = false;   ///< a field holding a length in internal units

    static VALUE Bool( bool aVal )             { VALUE v; v.type = BOOL; v.b = aVal; return v; }
    static VALUE Num( double aVal, bool aDist ) { VALUE v; v.type = NUM; v.num = aVal; v.distance = aDist; return v; }
    static VALUE Str( const wxString& aVal )   { VALUE v; v.type = STR; v.str = aVal; return v; }

    bool AsBool() const;
};


enum class KIND
{
    KEYWORD,    ///< IsPad: true or false for an object
    FUNCTION,   ///< InNet('GND'): a test with arguments
    FIELD       ///< Net: a value compared with = <> < > Like
};


struct KEYWORD
{
    wxString name;          ///< as Altium spells it, e.g. "InNet"
    KIND     kind;
    wxString category;      ///< Helper dialog grouping
    wxString description;
    wxString example;       ///< inserted by the Helper dialog, e.g. "InNet('GND')"
    int      id = 0;        ///< the adapter's own identifier
};


class ADAPTER
{
public:
    virtual ~ADAPTER() = default;

    /// The keywords, functions and fields this editor understands.
    virtual const std::vector<KEYWORD>& Keywords() const = 0;

    virtual bool  EvalKeyword( EDA_ITEM* aItem, const KEYWORD& aKey ) = 0;
    virtual bool  EvalFunction( EDA_ITEM* aItem, const KEYWORD& aKey,
                                const std::vector<VALUE>& aArgs ) = 0;
    virtual VALUE EvalField( EDA_ITEM* aItem, const KEYWORD& aKey ) = 0;

    /**
     * A property of the object (or of its parent, for the parent footprint's or symbol's
     * values) used as a field.  aName is the property name with its spaces removed.
     */
    virtual VALUE PropertyField( EDA_ITEM* aItem, const wxString& aName );

    /// Internal units for a literal number written with aUnit ("" = the editor's units).
    virtual double ToInternalUnits( double aValue, const wxString& aUnit ) const = 0;

    /// Whether aName (no spaces, any case) is a property of some object of this editor.
    bool IsPropertyName( const wxString& aName );

protected:
    /// Reads a property of an inspectable object into a VALUE (NONE when it has none).
    static VALUE readProperty( INSPECTABLE* aObject, size_t aTypeHash, const wxString& aName );

    /// The base types whose classes' properties count as fields (e.g. BOARD_ITEM).
    virtual size_t propertyBaseType() const = 0;

private:
    std::map<wxString, bool> m_propertyNames;   ///< lower-case, no spaces
};


class QUERY
{
public:
    QUERY();
    ~QUERY();

    /**
     * Parses aText.  On failure returns false and fills the error message and the character
     * position where it was found.
     */
    bool Compile( const wxString& aText, ADAPTER& aAdapter );

    const wxString& GetError() const { return m_error; }
    int             GetErrorPos() const { return m_errorPos; }

    /// Whether an object matches the compiled query (an empty query matches nothing).
    bool Matches( EDA_ITEM* aItem );

    bool IsEmpty() const { return !m_root; }

    struct NODE;

private:
    std::unique_ptr<NODE> m_root;
    ADAPTER*              m_adapter = nullptr;
    wxString              m_error;
    int                   m_errorPos = -1;
};


/// Wildcard match as Altium's Like: * and ?, case-insensitive.
bool LikeMatch( const wxString& aText, const wxString& aPattern );

/// Equality for strings: case-insensitive, with * and ? wildcards in either operand.
bool StringEquals( const wxString& aA, const wxString& aB );

/// Keywords of the language itself (And, Or, Not, Like, ...), for the Helper and colouring.
const std::vector<wxString>& Operators();

} // namespace ALTIUM_QUERY
