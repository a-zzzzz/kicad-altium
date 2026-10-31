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

#ifndef SCH_LIST_PANEL_H
#define SCH_LIST_PANEL_H

#include <widgets/property_list_panel.h>
#include <memory>

class PROPERTY_COMMIT_HANDLER;
class SCH_COMMIT;
class SCH_EDIT_FRAME;
class SCH_SCREEN;

/**
 * Altium-style SCH List: the common object list over the objects of the current sheet, edited
 * through schematic commits (docs/altium-parity/list-panel.md).
 */
class SCH_LIST_PANEL : public PROPERTY_LIST_PANEL
{
public:
    SCH_LIST_PANEL( SCH_EDIT_FRAME* aFrame );
    ~SCH_LIST_PANEL();

    /**
     * Objects of a sheet: top-level items and their fields, sheet pins and (optionally)
     * symbol pins.  Shared with Find Similar and selection memory.
     */
    static std::vector<EDA_ITEM*> SheetItems( SCH_SCREEN* aScreen, bool aWithSymbolPins );

protected:
    bool hasModel() const override;
    std::vector<EDA_ITEM*> selectedItems() const override;
    std::vector<EDA_ITEM*> allItems() const override;

    bool isEditable( EDA_ITEM* aItem, PROPERTY_BASE* aProperty,
                     const wxVariant& aCurrent ) const override;

    void beginEdit() override;
    bool setValue( EDA_ITEM* aItem, PROPERTY_BASE* aProperty, const wxVariant& aValue ) override;
    void endEdit( const wxString& aDescription ) override;

private:
    SCH_EDIT_FRAME*                          m_schFrame;
    std::unique_ptr<SCH_COMMIT>              m_commit;
    std::unique_ptr<PROPERTY_COMMIT_HANDLER> m_commitHandler;
    std::vector<EDA_ITEM*>                   m_edited;
};

#endif // SCH_LIST_PANEL_H
