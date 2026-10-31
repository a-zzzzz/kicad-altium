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

#include <tools/sch_parity_tool.h>

#include <dialogs/dialog_find_similar_objects.h>
#include <sch_screen.h>
#include <schematic.h>
#include <tool/actions.h>
#include <tool/action_manager.h>
#include <tool/tool_manager.h>
#include <tools/sch_actions.h>
#include <tools/sch_selection_tool.h>
#include <widgets/sch_list_panel.h>

#include <unordered_map>


SCH_PARITY_TOOL::SCH_PARITY_TOOL() :
        SCH_TOOL_BASE<SCH_EDIT_FRAME>( "eeschema.ParityTool" ),
        m_boundFrame( nullptr )
{
}


SCH_PARITY_TOOL::~SCH_PARITY_TOOL()
{
    if( m_boundFrame )
        m_boundFrame->Unbind( EDA_EVT_SCHEMATIC_CHANGED, &SCH_PARITY_TOOL::onSchematicChanged, this );
}


bool SCH_PARITY_TOOL::Init()
{
    SCH_TOOL_BASE::Init();

    // Right-click menu: Find Similar Objects next to Properties
    if( m_selectionTool )
    {
        CONDITIONAL_MENU& selToolMenu = m_selectionTool->GetToolMenu().GetMenu();
        selToolMenu.AddItem( SCH_ACTIONS::findSimilar, SELECTION_CONDITIONS::Count( 1 ), 200 );
    }

    // Check mark for View > Panels > SCH List
    m_toolMgr->GetActionManager()->SetConditions(
            SCH_ACTIONS::showSchList,
            ACTION_CONDITIONS().Check(
                    [this]( const SELECTION& )
                    {
                        return m_frame && m_frame->IsSchListShown();
                    } ) );

    // Memories don't survive opening or reloading the schematic
    m_boundFrame = m_frame;
    m_boundFrame->Bind( EDA_EVT_SCHEMATIC_CHANGED, &SCH_PARITY_TOOL::onSchematicChanged, this );

    return true;
}


void SCH_PARITY_TOOL::Reset( RESET_REASON aReason )
{
    SCH_TOOL_BASE::Reset( aReason );

    // Sheet changes and reloads come through here: list the new sheet's objects
    if( aReason == MODEL_RELOAD || aReason == SUPERMODEL_RELOAD )
    {
        if( SCH_LIST_PANEL* list = m_frame ? m_frame->GetSchListPanel() : nullptr )
            list->ScheduleRebuild();
    }
}


void SCH_PARITY_TOOL::onSchematicChanged( wxCommandEvent& aEvent )
{
    m_selectionMemory.clear();

    if( SCH_LIST_PANEL* list = m_frame ? m_frame->GetSchListPanel() : nullptr )
        list->ScheduleRebuild();

    aEvent.Skip();
}


int SCH_PARITY_TOOL::ToggleSchList( const TOOL_EVENT& aEvent )
{
    m_frame->ShowSchList( !m_frame->IsSchListShown() );
    return 0;
}


int SCH_PARITY_TOOL::SelectionChanged( const TOOL_EVENT& aEvent )
{
    if( SCH_LIST_PANEL* list = m_frame->GetSchListPanel() )
        list->OnSelectionChanged();

    return 0;
}


int SCH_PARITY_TOOL::ModelChanged( const TOOL_EVENT& aEvent )
{
    if( SCH_LIST_PANEL* list = m_frame->GetSchListPanel() )
        list->ScheduleRebuild();

    return 0;
}


int SCH_PARITY_TOOL::FindSimilar( const TOOL_EVENT& aEvent )
{
    // Reference object: the selection, or the object under the cursor
    SCH_SELECTION& selection = m_selectionTool->RequestSelection();

    if( selection.Empty() || !selection.Front()->IsSCH_ITEM() )
        return 0;

    EDA_ITEM* reference = selection.Front();

    // Like Altium's default scope ("Current Document"): objects of the current sheet
    DIALOG_FIND_SIMILAR_OBJECTS dlg( m_frame, reference,
                                     SCH_LIST_PANEL::SheetItems( m_frame->GetScreen(), true ) );

    if( dlg.ShowModal() != wxID_OK )
        return 0;

    EDA_ITEMS matches( dlg.GetMatches().begin(), dlg.GetMatches().end() );

    if( dlg.ClearExistingSelection() )
        m_toolMgr->RunAction( ACTIONS::selectionClear );

    if( !matches.empty() )
        m_toolMgr->RunAction<EDA_ITEMS*>( ACTIONS::selectItems, &matches );

    if( dlg.ZoomToMatches() && !matches.empty() )
        m_toolMgr->RunAction( ACTIONS::zoomFitSelection );

    m_frame->ShowInfoBarMsg( wxString::Format( wxPLURAL( "Found %d similar object.",
                                                         "Found %d similar objects.",
                                                         (int) matches.size() ),
                                               (int) matches.size() ) );
    return 0;
}


int SCH_PARITY_TOOL::SelectionMemory( const TOOL_EVENT& aEvent )
{
    int code = aEvent.Parameter<int>();
    int mode = code / 10;
    int slot = code % 10;

    if( slot < 1 || slot > 8 || !m_frame->GetScreen() )
        return 0;

    SLOTS& memory = m_selectionMemory[m_frame->GetCurrentSheet().Path().AsString()];

    if( mode == 0 )
    {
        memory[slot].clear();

        for( EDA_ITEM* item : m_selectionTool->GetSelection() )
        {
            if( item->IsSCH_ITEM() )
                memory[slot].push_back( item->m_Uuid );
        }

        int count = (int) memory[slot].size();

        m_frame->ShowInfoBarMsg( wxString::Format( wxPLURAL( "Stored %d object in memory %d.",
                                                             "Stored %d objects in memory %d.",
                                                             count ),
                                                   count, slot ) );
        return 0;
    }

    if( memory[slot].empty() )
        return 0;

    // Recall: objects deleted since they were stored no longer resolve and are skipped
    std::unordered_map<KIID, EDA_ITEM*> byId;

    for( EDA_ITEM* item : SCH_LIST_PANEL::SheetItems( m_frame->GetScreen(), true ) )
        byId[item->m_Uuid] = item;

    EDA_ITEMS items;

    for( const KIID& id : memory[slot] )
    {
        auto it = byId.find( id );

        if( it != byId.end() )
            items.push_back( it->second );
    }

    if( items.empty() )
        return 0;

    if( mode == 1 )
        m_toolMgr->RunAction( ACTIONS::selectionClear );

    m_toolMgr->RunAction<EDA_ITEMS*>( ACTIONS::selectItems, &items );
    return 0;
}


void SCH_PARITY_TOOL::setTransitions()
{
    Go( &SCH_PARITY_TOOL::FindSimilar,   SCH_ACTIONS::findSimilar.MakeEvent() );
    Go( &SCH_PARITY_TOOL::ToggleSchList, SCH_ACTIONS::showSchList.MakeEvent() );

    const TOOL_ACTION* memoryActions[] = {
        &SCH_ACTIONS::selectionMemoryStore1, &SCH_ACTIONS::selectionMemoryRecall1,
        &SCH_ACTIONS::selectionMemoryRecallAdd1,
        &SCH_ACTIONS::selectionMemoryStore2, &SCH_ACTIONS::selectionMemoryRecall2,
        &SCH_ACTIONS::selectionMemoryRecallAdd2,
        &SCH_ACTIONS::selectionMemoryStore3, &SCH_ACTIONS::selectionMemoryRecall3,
        &SCH_ACTIONS::selectionMemoryRecallAdd3,
        &SCH_ACTIONS::selectionMemoryStore4, &SCH_ACTIONS::selectionMemoryRecall4,
        &SCH_ACTIONS::selectionMemoryRecallAdd4,
        &SCH_ACTIONS::selectionMemoryStore5, &SCH_ACTIONS::selectionMemoryRecall5,
        &SCH_ACTIONS::selectionMemoryRecallAdd5,
        &SCH_ACTIONS::selectionMemoryStore6, &SCH_ACTIONS::selectionMemoryRecall6,
        &SCH_ACTIONS::selectionMemoryRecallAdd6,
        &SCH_ACTIONS::selectionMemoryStore7, &SCH_ACTIONS::selectionMemoryRecall7,
        &SCH_ACTIONS::selectionMemoryRecallAdd7,
        &SCH_ACTIONS::selectionMemoryStore8, &SCH_ACTIONS::selectionMemoryRecall8,
        &SCH_ACTIONS::selectionMemoryRecallAdd8
    };

    for( const TOOL_ACTION* action : memoryActions )
        Go( &SCH_PARITY_TOOL::SelectionMemory, action->MakeEvent() );

    Go( &SCH_PARITY_TOOL::SelectionChanged, EVENTS::PointSelectedEvent );
    Go( &SCH_PARITY_TOOL::SelectionChanged, EVENTS::SelectedEvent );
    Go( &SCH_PARITY_TOOL::SelectionChanged, EVENTS::UnselectedEvent );
    Go( &SCH_PARITY_TOOL::SelectionChanged, EVENTS::ClearedEvent );
    Go( &SCH_PARITY_TOOL::ModelChanged,     EVENTS::SelectedItemsModified );
    Go( &SCH_PARITY_TOOL::ModelChanged,     TOOL_EVENT( TC_MESSAGE, TA_UNDO_REDO_POST, AS_GLOBAL ) );
    Go( &SCH_PARITY_TOOL::ModelChanged,     TOOL_EVENT( TC_MESSAGE, TA_MODEL_CHANGE, AS_GLOBAL ) );
}
