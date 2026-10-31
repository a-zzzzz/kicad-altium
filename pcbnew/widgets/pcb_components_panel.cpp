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

#include "pcb_components_panel.h"

#include <board.h>
#include <board_commit.h>
#include <board_design_settings.h>
#include <footprint.h>
#include <footprint_library_adapter.h>
#include <fp_tree_model_adapter.h>
#include <pad.h>
#include <pcb_edit_frame.h>
#include <pcbnew_settings.h>
#include <project_pcb.h>
#include <tool/actions.h>
#include <tool/tool_manager.h>
#include <tools/board_editor_control.h>
#include <tools/pcb_actions.h>
#include <view/view_controls.h>
#include <widgets/footprint_preview_widget.h>
#include <confirm.h>
#include <dialogs/dialog_exchange_footprints.h>
#include <tools/pcb_selection_tool.h>

#include <wx/sizer.h>


PCB_COMPONENTS_PANEL::PCB_COMPONENTS_PANEL( wxWindow* aParent, PCB_EDIT_FRAME* aFrame ) :
        COMPONENTS_PANEL( aParent, aFrame, wxS( "footprints" ) ),
        m_pcbFrame( aFrame ),
        m_footprintPreview( nullptr )
{
}


void PCB_COMPONENTS_PANEL::createPreviews( wxWindow* aParent, wxBoxSizer* aSizer )
{
    m_footprintPreview = new FOOTPRINT_PREVIEW_WIDGET( aParent, m_pcbFrame->Kiway() );
    m_footprintPreview->SetUserUnits( m_pcbFrame->GetUserUnits() );
    aSizer->Add( m_footprintPreview, 3, wxEXPAND | wxALL, 4 );
}


std::vector<wxString> PCB_COMPONENTS_PANEL::libraryNames()
{
    FOOTPRINT_LIBRARY_ADAPTER* libs = PROJECT_PCB::FootprintLibAdapter( &m_pcbFrame->Prj() );

    // As the footprint chooser does: the tree is built from fully loaded libraries
    libs->AsyncLoad();
    libs->BlockUntilLoaded();

    std::vector<wxString> names;

    for( const wxString& name : libs->GetLibraryNames() )
    {
        if( libs->HasLibrary( name, true ) )
            names.push_back( name );
    }

    return names;
}


wxObjectDataPtr<LIB_TREE_MODEL_ADAPTER> PCB_COMPONENTS_PANEL::createAdapter( const wxString& aLibrary )
{
    FOOTPRINT_LIBRARY_ADAPTER* libs = PROJECT_PCB::FootprintLibAdapter( &m_pcbFrame->Prj() );
    wxObjectDataPtr<LIB_TREE_MODEL_ADAPTER> adapter = FP_TREE_MODEL_ADAPTER::Create( m_pcbFrame, libs );

    if( aLibrary.IsEmpty() )
    {
        static_cast<FP_TREE_MODEL_ADAPTER*>( adapter.get() )->AddLibraries( m_pcbFrame );
    }
    else
    {
        std::vector<LIB_TREE_ITEM*> items;

        for( FOOTPRINT* fp : libs->GetFootprints( aLibrary, true ) )
            items.push_back( fp );

        adapter->DoAddLibrary( aLibrary, libs->GetLibraryDescription( aLibrary ).value_or( wxString() ),
                               items, false, true );
        adapter->AssignIntrinsicRanks();
    }

    return adapter;
}


void PCB_COMPONENTS_PANEL::showPreview( const LIB_ID& aLibId, int aUnit )
{
    if( !m_footprintPreview->IsInitialized() )
        return;

    if( aLibId.IsValid() )
    {
        m_footprintPreview->ClearStatus();
        m_footprintPreview->DisplayFootprint( aLibId );
    }
    else
    {
        m_footprintPreview->SetStatusText( _( "Select a component" ) );
    }
}


void PCB_COMPONENTS_PANEL::place( const LIB_ID& aLibId, int aUnit, bool aRepeat )
{
    TOOL_MANAGER* toolMgr = m_pcbFrame->GetToolManager();

    if( toolMgr->GetTool<BOARD_EDITOR_CONTROL>()->PlacingFootprint() )
        return;

    FOOTPRINT* footprint = nullptr;

    try
    {
        footprint = PROJECT_PCB::FootprintLibAdapter( &m_pcbFrame->Prj() )->LoadFootprint( aLibId, false );
    }
    catch( const IO_ERROR& ioe )
    {
        DisplayErrorMessage( m_pcbFrame, _( "Error loading footprint." ), ioe.What() );
    }

    if( !footprint )
        return;

    // Same preparation as adding a footprint from the footprint viewer
    BOARD*           board = m_pcbFrame->GetBoard();
    PCBNEW_SETTINGS* cfg = m_pcbFrame->GetPcbNewSettings();
    BOARD_COMMIT     commit( m_pcbFrame );

    toolMgr->RunAction( ACTIONS::selectionClear );

    footprint->SetParent( board );
    footprint->SetLink( niluuid );
    footprint->SetFlags( IS_NEW );

    BOARD_DESIGN_SETTINGS& bds = board->GetDesignSettings();
    footprint->ApplyDefaultSettings( *board, bds.m_StyleFPFields, bds.m_StyleFPText,
                                     bds.m_StyleFPShapes, bds.m_StyleFPDimensions,
                                     bds.m_StyleFPBarcodes );

    for( PAD* pad : footprint->Pads() )
    {
        pad->SetLocalRatsnestVisible( cfg->m_Display.m_ShowGlobalRatsnest );
        pad->SetNetCode( 0 );
    }

    if( footprint->IsFlipped() )
        footprint->Flip( footprint->GetPosition(), cfg->m_FlipDirection );

    KIGFX::VIEW_CONTROLS* viewControls = m_pcbFrame->GetCanvas()->GetViewControls();
    VECTOR2D              cursorPos = viewControls->GetCursorPosition();

    commit.Add( footprint );
    viewControls->SetCrossHairCursorPosition( VECTOR2D( 0, 0 ), false );
    m_pcbFrame->PlaceFootprint( footprint );
    footprint->SetPosition( VECTOR2I( 0, 0 ) );
    viewControls->SetCrossHairCursorPosition( cursorPos, false );
    commit.Push( _( "Place Footprint" ) );

    // Like Altium, keep placing copies until Esc after a double-click
    PCB_EDIT_FRAME* frame = m_pcbFrame;

    // (a drag places just one: its source supplies no more copies)
    toolMgr->GetTool<BOARD_EDITOR_CONTROL>()->SetFootprintRepeatSource(
            [frame, aLibId, aRepeat]() -> FOOTPRINT*
            {
                if( !aRepeat )
                    return nullptr;

                try
                {
                    return PROJECT_PCB::FootprintLibAdapter( &frame->Prj() )->LoadFootprint( aLibId, false );
                }
                catch( const IO_ERROR& )
                {
                    return nullptr;
                }
            } );

    // The part follows the cursor on the canvas until it is clicked down
    m_pcbFrame->GetCanvas()->SetFocus();
    toolMgr->PostAction( PCB_ACTIONS::placeFootprint, footprint );
    footprint->ClearFlags();
}


/// The component of the first selected item (a pad or text stands for its component).
static FOOTPRINT* selectedFootprint( PCB_EDIT_FRAME* aFrame )
{
    PCB_SELECTION_TOOL* selTool = aFrame->GetToolManager()->GetTool<PCB_SELECTION_TOOL>();

    for( EDA_ITEM* item : selTool->GetSelection() )
    {
        if( item->Type() == PCB_FOOTPRINT_T )
            return static_cast<FOOTPRINT*>( item );

        if( item->IsBOARD_ITEM() )
        {
            if( FOOTPRINT* parent = static_cast<BOARD_ITEM*>( item )->GetParentFootprint() )
                return parent;
        }
    }

    return nullptr;
}


bool PCB_COMPONENTS_PANEL::canReplace()
{
    return selectedFootprint( m_pcbFrame ) != nullptr;
}


void PCB_COMPONENTS_PANEL::replace( const LIB_ID& aLibId, int aUnit )
{
    FOOTPRINT* footprint = selectedFootprint( m_pcbFrame );

    if( !footprint )
        return;

    // The dialog changes the selected footprints: make that the component itself
    if( !footprint->IsSelected() )
    {
        m_pcbFrame->GetToolManager()->RunAction( ACTIONS::selectionClear );
        m_pcbFrame->GetToolManager()->RunAction<EDA_ITEM*>( ACTIONS::selectItem, footprint );
    }

    // KiCad's Change Footprints, set to the selected footprints and the picked one
    DIALOG_EXCHANGE_FOOTPRINTS dlg( m_pcbFrame, footprint, false, true );
    dlg.SetNewLibId( aLibId );
    dlg.ShowQuasiModal();
}


void PCB_COMPONENTS_PANEL::manageLibraries()
{
    m_pcbFrame->GetToolManager()->RunAction( ACTIONS::showFootprintLibTable );
    RefreshLibraries();
}
