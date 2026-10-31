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

#ifndef PCB_LIST_PANEL_H
#define PCB_LIST_PANEL_H

#include <widgets/property_list_panel.h>
#include <memory>

class BOARD_COMMIT;
class PCB_EDIT_FRAME;

/**
 * Altium-style PCB List: the common object list over board objects, with board value text
 * (nets by name) and board commits (docs/altium-parity/list-panel.md).
 */
class PCB_LIST_PANEL : public PROPERTY_LIST_PANEL
{
public:
    PCB_LIST_PANEL( PCB_EDIT_FRAME* aFrame );
    ~PCB_LIST_PANEL();

protected:
    bool hasModel() const override;
    std::vector<EDA_ITEM*> selectedItems() const override;
    std::vector<EDA_ITEM*> allItems() const override;

    wxString formatValue( PROPERTY_BASE* aProperty, const wxVariant& aValue ) const override;
    bool parseValue( PROPERTY_BASE* aProperty, const wxVariant& aCurrent, const wxString& aText,
                     wxVariant& aValue ) const override;

    void beginEdit() override;
    bool setValue( EDA_ITEM* aItem, PROPERTY_BASE* aProperty, const wxVariant& aValue ) override;
    void endEdit( const wxString& aDescription ) override;

private:
    PCB_EDIT_FRAME*               m_pcbFrame;
    std::unique_ptr<BOARD_COMMIT> m_commit;
};

#endif // PCB_LIST_PANEL_H
