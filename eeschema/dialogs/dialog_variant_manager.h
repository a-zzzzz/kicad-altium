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

#ifndef DIALOG_VARIANT_MANAGER_H
#define DIALOG_VARIANT_MANAGER_H

#include <dialog_shim.h>
#include <sch_sheet_path.h>

#include <map>
#include <vector>

class SCH_EDIT_FRAME;
class SCH_SYMBOL;
class wxGrid;
class wxGridEvent;
class wxStaticText;


/**
 * Altium-style Variant Management: components (rows) x variants (columns).
 *
 * Each cell is Fitted, Not Fitted (KiCad DNP for that variant) or Alternate (field overrides
 * for that variant, edited as "parameter variations").  Variants can be added, renamed,
 * removed and described.  Nothing is changed until OK; OK applies everything in one undoable
 * commit.
 */
class DIALOG_VARIANT_MANAGER : public DIALOG_SHIM
{
public:
    DIALOG_VARIANT_MANAGER( SCH_EDIT_FRAME* aParent );

    bool TransferDataFromWindow() override;

private:
    struct UNIT
    {
        SCH_SYMBOL*    symbol;
        SCH_SHEET_PATH path;
    };

    struct COMPONENT
    {
        wxString          designator;
        wxString          comment;
        wxString          footprint;
        wxString          description;
        std::vector<UNIT> units;
    };

    struct CELL
    {
        bool                         dnp = false;
        std::map<wxString, wxString> fields;    ///< overrides: field name -> variant value
        bool                         origDnp = false;
        std::map<wxString, wxString> origFields;
    };

    struct VARIANT_COL
    {
        wxString name;
        wxString origName;      ///< empty for a variant added in this dialog
        wxString description;
        wxString origDescription;
    };

    void loadData();
    void fillGrid();
    void fillRow( int aRow );
    int  variantCol( int aGridCol ) const;   ///< m_variants index, or -1

    void setCell( int aRow, int aVariant, const wxString& aState );
    bool editParameters( int aRow, int aVariant );

    void addVariant();
    void renameVariant( int aVariant );
    void removeVariant( int aVariant );
    void describeVariant( int aVariant );
    bool askVariantName( const wxString& aTitle, wxString& aName, int aIgnore );

    void onCellChanged( wxGridEvent& aEvent );
    void onCellDClick( wxGridEvent& aEvent );
    void onCellRightClick( wxGridEvent& aEvent );
    void onLabelRightClick( wxGridEvent& aEvent );

private:
    SCH_EDIT_FRAME*                m_frame;
    wxGrid*                        m_grid;
    wxStaticText*                  m_info;
    std::vector<COMPONENT>         m_components;
    std::vector<VARIANT_COL>       m_variants;
    std::vector<wxString>          m_removed;     ///< original names of removed variants
    std::vector<std::vector<CELL>> m_cells;       ///< [component][variant]
};

#endif // DIALOG_VARIANT_MANAGER_H
