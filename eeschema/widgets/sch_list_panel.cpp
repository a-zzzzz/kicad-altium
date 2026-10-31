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

#include <widgets/sch_list_panel.h>

#include <properties/property.h>
#include <properties/property_mgr.h>
#include <sch_commit.h>
#include <sch_edit_frame.h>
#include <sch_field.h>
#include <sch_screen.h>
#include <sch_sheet.h>
#include <sch_symbol.h>
#include <schematic.h>
#include <tool/tool_manager.h>
#include <tools/sch_selection_tool.h>


SCH_LIST_PANEL::SCH_LIST_PANEL( SCH_EDIT_FRAME* aFrame ) :
        PROPERTY_LIST_PANEL( aFrame, aFrame, _( "SCH List" ) ),
        m_schFrame( aFrame )
{
}


SCH_LIST_PANEL::~SCH_LIST_PANEL() = default;


std::vector<EDA_ITEM*> SCH_LIST_PANEL::SheetItems( SCH_SCREEN* aScreen, bool aWithSymbolPins )
{
    std::vector<EDA_ITEM*> items;

    if( !aScreen )
        return items;

    for( SCH_ITEM* item : aScreen->Items() )
    {
        items.push_back( item );

        item->RunOnChildren(
                [&]( SCH_ITEM* aChild )
                {
                    if( aChild->Type() == SCH_PIN_T && !aWithSymbolPins )
                        return;

                    items.push_back( aChild );
                },
                RECURSE_MODE::NO_RECURSE );
    }

    return items;
}


bool SCH_LIST_PANEL::hasModel() const
{
    return m_schFrame->GetScreen() != nullptr;
}


std::vector<EDA_ITEM*> SCH_LIST_PANEL::selectedItems() const
{
    std::vector<EDA_ITEM*> items;

    for( EDA_ITEM* item : m_schFrame->GetToolManager()->GetTool<SCH_SELECTION_TOOL>()->GetSelection() )
    {
        if( item->IsSCH_ITEM() )
            items.push_back( item );
    }

    return items;
}


std::vector<EDA_ITEM*> SCH_LIST_PANEL::allItems() const
{
    // Symbol pins are left out: every symbol has many and they're mostly read-only here
    return SheetItems( m_schFrame->GetScreen(), false );
}


bool SCH_LIST_PANEL::isEditable( EDA_ITEM* aItem, PROPERTY_BASE* aProperty,
                                 const wxVariant& aCurrent ) const
{
    // Renaming a sheet's file needs the file handling of the sheet dialog / Properties panel
    if( aItem->Type() == SCH_FIELD_T && static_cast<SCH_FIELD*>( aItem )->GetId() == FIELD_T::SHEET_FILENAME )
        return false;

    // Objects still being placed aren't on the sheet yet
    if( aItem->IsNew() )
        return false;

    return PROPERTY_LIST_PANEL::isEditable( aItem, aProperty, aCurrent );
}


void SCH_LIST_PANEL::beginEdit()
{
    m_commit = std::make_unique<SCH_COMMIT>( m_schFrame );
    m_commitHandler = std::make_unique<PROPERTY_COMMIT_HANDLER>( m_commit.get() );
    m_edited.clear();
}


bool SCH_LIST_PANEL::setValue( EDA_ITEM* aItem, PROPERTY_BASE* aProperty, const wxVariant& aValue )
{
    if( !aItem->IsSCH_ITEM() )
        return false;

    SCH_ITEM*       item = static_cast<SCH_ITEM*>( aItem );
    SCH_SCREEN*     screen = m_schFrame->GetScreen();
    SCH_SHEET_PATH& sheet = m_schFrame->GetCurrentSheet();
    wxString        variant = m_schFrame->Schematic().GetCurrentVariant();
    bool            isText = aProperty->Name() == wxT( "Text" );

    // Same rules as the Properties panel: references go through the symbol (instance data),
    // field text honours the active variant
    if( item->Type() == SCH_FIELD_T && isText )
    {
        SCH_FIELD*  field = static_cast<SCH_FIELD*>( item );
        SCH_SYMBOL* symbol = dynamic_cast<SCH_SYMBOL*>( item->GetParentSymbol() );

        if( symbol && field->GetId() == FIELD_T::REFERENCE )
        {
            m_commit->Modify( symbol, screen, RECURSE_MODE::NO_RECURSE );
            symbol->SetRefProp( aValue.GetString() );
            symbol->SyncOtherUnits( sheet, *m_commit, aProperty );
            m_edited.push_back( symbol );
            return true;
        }

        if( symbol && !variant.IsEmpty() )
        {
            m_commit->Modify( symbol, screen, RECURSE_MODE::NO_RECURSE );
            field->SetText( aValue.GetString(), &sheet, variant );
            symbol->SyncOtherUnits( sheet, *m_commit, nullptr, variant );
            m_edited.push_back( symbol );
            return true;
        }
    }

    if( item->Type() == SCH_TABLECELL_T )
        m_commit->Modify( item->GetParent(), screen, RECURSE_MODE::NO_RECURSE );
    else
        m_commit->Modify( item, screen, RECURSE_MODE::NO_RECURSE );

    item->Set( aProperty, aValue );

    if( SCH_SYMBOL* symbol = dynamic_cast<SCH_SYMBOL*>( item ) )
        symbol->SyncOtherUnits( sheet, *m_commit, aProperty, variant );

    m_edited.push_back( item );
    return true;
}


void SCH_LIST_PANEL::endEdit( const wxString& aDescription )
{
    if( m_commit && !m_commit->Empty() )
        m_commit->Push( aDescription );

    m_commitHandler.reset();
    m_commit.reset();

    // Field displays need a repaint
    for( EDA_ITEM* item : m_edited )
        m_schFrame->UpdateItem( item );

    m_edited.clear();
}
