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
#include <layer_ids.h>
#include <widgets/altium_filter_panel.h>

class BOARD;
class BOARD_ITEM;
class PCB_EDIT_FRAME;


/// Altium's PCB query language over board items.
class PCB_QUERY_ADAPTER : public ALTIUM_QUERY::ADAPTER
{
public:
    PCB_QUERY_ADAPTER( PCB_EDIT_FRAME* aFrame );

    const std::vector<ALTIUM_QUERY::KEYWORD>& Keywords() const override;

    bool  EvalKeyword( EDA_ITEM* aItem, const ALTIUM_QUERY::KEYWORD& aKey ) override;
    bool  EvalFunction( EDA_ITEM* aItem, const ALTIUM_QUERY::KEYWORD& aKey,
                        const std::vector<ALTIUM_QUERY::VALUE>& aArgs ) override;
    ALTIUM_QUERY::VALUE EvalField( EDA_ITEM* aItem, const ALTIUM_QUERY::KEYWORD& aKey ) override;
    ALTIUM_QUERY::VALUE PropertyField( EDA_ITEM* aItem, const wxString& aName ) override;

    double ToInternalUnits( double aValue, const wxString& aUnit ) const override;

    /// Altium's name for a board layer (Top Layer, Mid-Layer 1, Top Overlay, ...).
    wxString LayerName( PCB_LAYER_ID aLayer ) const;

    /// The layers a name means (Altium's, KiCad's or the board's own name), or none.
    LSET ResolveLayers( const wxString& aName ) const;

protected:
    size_t propertyBaseType() const override;

private:
    PCB_EDIT_FRAME* m_frame;
};


class PCB_FILTER_PANEL : public ALTIUM_FILTER_PANEL
{
public:
    PCB_FILTER_PANEL( wxWindow* aParent, PCB_EDIT_FRAME* aFrame );
    ~PCB_FILTER_PANEL() override;

    /// The board was replaced: forget the mask and list its layers.
    void OnBoardChanged();

protected:
    int  applyQuery( ALTIUM_QUERY::QUERY& aQuery, const OPTIONS& aOptions,
                     wxString& aMessage ) override;
    void clearMask() override;

    std::vector<wxString> builderLayers() const override;

private:
    PCB_EDIT_FRAME*   m_frame;
    PCB_QUERY_ADAPTER m_queryAdapter;
    std::set<KIID>    m_masked;
};
