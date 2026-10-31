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

#include <properties/property_value_text.h>

#include <eda_units.h>
#include <inspectable.h>
#include <properties/color4d_variant.h>
#include <properties/property.h>
#include <properties/std_optional_variants.h>
#include <units_provider.h>

#include <wx/intl.h>


bool PROPERTY_VALUE_TEXT::GetValue( INSPECTABLE* aItem, PROPERTY_BASE* aProperty,
                                    wxVariant& aValue )
{
    if( !aProperty->Available( aItem ) )
        return false;

    const wxAny& any = aItem->Get( aProperty );

    if( any.IsNull() )
    {
        // Unset optional value
        aValue.MakeNull();
        return true;
    }

    if( aProperty->HasChoices() )
    {
        // Enums have no default wxAny conversion; treat them as ints
        int tmp;

        if( any.GetAs<int>( &tmp ) )
        {
            aValue = wxVariant( tmp );
            return true;
        }
    }

    return any.GetAs( &aValue );
}


static bool isDistance( PROPERTY_BASE* aProperty )
{
    return aProperty->Display() == PT_SIZE || aProperty->Display() == PT_COORD;
}


wxString PROPERTY_VALUE_TEXT::Format( const UNITS_PROVIDER* aUnits, PROPERTY_BASE* aProperty,
                                      const wxVariant& aValue )
{
    if( aValue.IsNull() )
        return wxEmptyString;

    // Optional values (overrides): blank when unset, otherwise shown like the plain type
    if( aValue.GetType() == wxS( "std::optional<int>" ) )
    {
        std::optional<int> v = static_cast<STD_OPTIONAL_INT_VARIANT_DATA*>( aValue.GetData() )->Value();
        return v ? Format( aUnits, aProperty, wxVariant( (long) *v ) ) : wxString();
    }

    if( aValue.GetType() == wxS( "std::optional<double>" ) )
    {
        std::optional<double> v =
                static_cast<STD_OPTIONAL_DOUBLE_VARIANT_DATA*>( aValue.GetData() )->Value();
        return v ? Format( aUnits, aProperty, wxVariant( *v ) ) : wxString();
    }

    bool isLong = aValue.GetType() == wxS( "long" );

    if( isLong && aProperty->HasChoices() )
    {
        const wxPGChoices& choices = aProperty->Choices();
        int                idx = choices.Index( (int) aValue.GetLong() );

        if( idx >= 0 )
            return choices.GetLabel( idx );
    }

    if( isLong && isDistance( aProperty ) && aUnits )
        return aUnits->MessageTextFromValue( (double) aValue.GetLong() );

    // Some distances (e.g. a schematic line's length) are doubles
    if( aValue.GetType() == wxS( "double" ) && isDistance( aProperty ) && aUnits )
        return aUnits->MessageTextFromValue( aValue.GetDouble() );

    // A fully transparent colour means "not set": the item uses its default colour
    if( aValue.GetType() == wxS( "COLOR4D" ) )
    {
        const KIGFX::COLOR4D& color =
                static_cast<COLOR4D_VARIANT_DATA*>( aValue.GetData() )->Color();

        if( color == KIGFX::COLOR4D::UNSPECIFIED || color.a == 0.0 )
            return _( "Default" );
    }

    if( aValue.GetType() == wxS( "bool" ) )
        return aValue.GetBool() ? _( "Yes" ) : _( "No" );

    return aValue.MakeString();
}


namespace
{
// Unset optional values (e.g. a clearance override) read as null; distances are still
// editable as distances.
wxString effectiveType( PROPERTY_BASE* aProperty, const wxVariant& aCurrent )
{
    if( aCurrent.IsNull() && isDistance( aProperty ) )
        return wxS( "long" );

    return aCurrent.GetType();
}
}


bool PROPERTY_VALUE_TEXT::IsEditableAsText( PROPERTY_BASE* aProperty, const wxVariant& aCurrent )
{
    wxString type = effectiveType( aProperty, aCurrent );

    return type == wxS( "long" ) || type == wxS( "double" ) || type == wxS( "bool" )
           || type == wxS( "string" ) || type == wxS( "std::optional<int>" )
           || type == wxS( "std::optional<double>" );
}


bool PROPERTY_VALUE_TEXT::Parse( const UNITS_PROVIDER* aUnits, PROPERTY_BASE* aProperty,
                                 const wxVariant& aCurrent, const wxString& aText,
                                 wxVariant& aValue )
{
    wxString text = aText;
    text.Trim().Trim( false );
    wxString type = effectiveType( aProperty, aCurrent );

    if( type == wxS( "std::optional<int>" ) || type == wxS( "std::optional<double>" ) )
    {
        bool isInt = type == wxS( "std::optional<int>" );

        if( text.IsEmpty() )
        {
            if( isInt )
                aValue = wxVariant( new STD_OPTIONAL_INT_VARIANT_DATA( std::nullopt ) );
            else
                aValue = wxVariant( new STD_OPTIONAL_DOUBLE_VARIANT_DATA( std::nullopt ) );

            return true;
        }

        wxVariant plain;

        if( !Parse( aUnits, aProperty, isInt ? wxVariant( 0L ) : wxVariant( 0.0 ), text, plain ) )
            return false;

        if( isInt )
            aValue = wxVariant( new STD_OPTIONAL_INT_VARIANT_DATA( (int) plain.GetLong() ) );
        else
            aValue = wxVariant( new STD_OPTIONAL_DOUBLE_VARIANT_DATA( plain.GetDouble() ) );

        return true;
    }

    if( type == wxS( "string" ) )
    {
        aValue = wxVariant( aText );
        return true;
    }

    if( type == wxS( "bool" ) )
    {
        wxString lower = text.Lower();

        if( lower == _( "yes" ).Lower() || lower == wxS( "true" ) || lower == wxS( "1" ) )
            aValue = wxVariant( true );
        else if( lower == _( "no" ).Lower() || lower == wxS( "false" ) || lower == wxS( "0" ) )
            aValue = wxVariant( false );
        else
            return false;

        return true;
    }

    if( type == wxS( "long" ) )
    {
        if( aProperty->HasChoices() )
        {
            const wxPGChoices& choices = aProperty->Choices();

            for( unsigned ii = 0; ii < choices.GetCount(); ++ii )
            {
                if( choices.GetLabel( ii ).IsSameAs( text, false ) )
                {
                    aValue = wxVariant( (long) choices.GetValue( ii ) );
                    return true;
                }
            }

            return false;
        }

        if( isDistance( aProperty ) && aUnits )
        {
            // Distances accept unit suffixes ("0.25mm", "10mil") and default to user units
            aValue = wxVariant( (long) EDA_UNIT_UTILS::UI::ValueFromString(
                                        aUnits->GetIuScale(), aUnits->GetUserUnits(), text ) );
            return true;
        }

        long number;

        if( !text.ToLong( &number ) )
            return false;

        aValue = wxVariant( number );
        return true;
    }

    if( type == wxS( "double" ) )
    {
        if( isDistance( aProperty ) && aUnits )
        {
            aValue = wxVariant( EDA_UNIT_UTILS::UI::DoubleValueFromString(
                    aUnits->GetIuScale(), aUnits->GetUserUnits(), text ) );
            return true;
        }

        double number;

        if( !text.ToDouble( &number ) )
            return false;

        aValue = wxVariant( number );
        return true;
    }

    return false;
}
