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

#include <set>
#include <kiid.h>
#include <sch_sheet_path.h>
#include <widgets/altium_filter_panel.h>

class SCH_EDIT_FRAME;
class SCH_ITEM;


/// Altium's schematic query language over schematic objects.
class SCH_QUERY_ADAPTER : public ALTIUM_QUERY::ADAPTER
{
public:
    SCH_QUERY_ADAPTER( SCH_EDIT_FRAME* aFrame );

    const std::vector<ALTIUM_QUERY::KEYWORD>& Keywords() const override;

    bool  EvalKeyword( EDA_ITEM* aItem, const ALTIUM_QUERY::KEYWORD& aKey ) override;
    bool  EvalFunction( EDA_ITEM* aItem, const ALTIUM_QUERY::KEYWORD& aKey,
                        const std::vector<ALTIUM_QUERY::VALUE>& aArgs ) override;
    ALTIUM_QUERY::VALUE EvalField( EDA_ITEM* aItem, const ALTIUM_QUERY::KEYWORD& aKey ) override;
    ALTIUM_QUERY::VALUE PropertyField( EDA_ITEM* aItem, const wxString& aName ) override;

    double ToInternalUnits( double aValue, const wxString& aUnit ) const override;

    /// The sheet the objects being tested are on (for references, values and nets).
    void SetSheet( const SCH_SHEET_PATH& aSheet ) { m_sheet = aSheet; }

protected:
    size_t propertyBaseType() const override;

private:
    SCH_EDIT_FRAME* m_frame;
    SCH_SHEET_PATH  m_sheet;
};


class SCH_FILTER_PANEL : public ALTIUM_FILTER_PANEL
{
public:
    SCH_FILTER_PANEL( wxWindow* aParent, SCH_EDIT_FRAME* aFrame );
    ~SCH_FILTER_PANEL() override;

    /// The schematic was replaced: forget the mask.
    void OnSchematicChanged();

protected:
    int  applyQuery( ALTIUM_QUERY::QUERY& aQuery, const OPTIONS& aOptions,
                     wxString& aMessage ) override;
    void clearMask() override;

private:
    SCH_EDIT_FRAME*   m_frame;
    SCH_QUERY_ADAPTER m_queryAdapter;
    std::set<KIID>    m_masked;
};
