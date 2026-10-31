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

#include "sch_components_panel.h"

#include <eeschema_settings.h>
#include <lib_symbol.h>
#include <libraries/symbol_library_adapter.h>
#include <pgm_base.h>
#include <project_sch.h>
#include <sch_edit_frame.h>
#include <dialogs/dialog_change_symbols.h>
#include <tools/sch_selection_tool.h>
#include <sch_symbol.h>
#include <settings/common_settings.h>
#include <settings/settings_manager.h>
#include <symbol_tree_model_adapter.h>
#include <tool/actions.h>
#include <tool/tool_manager.h>
#include <tools/sch_actions.h>
#include <widgets/footprint_preview_widget.h>
#include <widgets/symbol_preview_widget.h>

#include <wx/sizer.h>


SCH_COMPONENTS_PANEL::SCH_COMPONENTS_PANEL( wxWindow* aParent, SCH_EDIT_FRAME* aFrame ) :
        COMPONENTS_PANEL( aParent, aFrame, wxS( "symbols" ) ),
        m_schFrame( aFrame ),
        m_symbolPreview( nullptr ),
        m_footprintPreview( nullptr )
{
}


void SCH_COMPONENTS_PANEL::createPreviews( wxWindow* aParent, wxBoxSizer* aSizer )
{
    EDA_DRAW_PANEL_GAL::GAL_TYPE backend = EDA_DRAW_PANEL_GAL::GAL_TYPE::GAL_TYPE_OPENGL;

    if( m_schFrame->GetCanvas() )
        backend = m_schFrame->GetCanvas()->GetBackend();

    // Symbol above footprint: the panel is a narrow side column
    m_symbolPreview = new SYMBOL_PREVIEW_WIDGET( aParent, &m_schFrame->Kiway(), true, backend );
    m_symbolPreview->SetLayoutDirection( wxLayout_LeftToRight );
    aSizer->Add( m_symbolPreview, 3, wxEXPAND | wxALL, 4 );

    m_footprintPreview = new FOOTPRINT_PREVIEW_WIDGET( aParent, m_schFrame->Kiway() );
    m_footprintPreview->SetUserUnits( m_schFrame->GetUserUnits() );
    aSizer->Add( m_footprintPreview, 3, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4 );
}


std::vector<wxString> SCH_COMPONENTS_PANEL::libraryNames()
{
    SYMBOL_LIBRARY_ADAPTER* libs = PROJECT_SCH::SymbolLibAdapter( &m_schFrame->Prj() );
    std::vector<wxString>   names;

    for( const wxString& name : libs->GetLibraryNames() )
    {
        std::optional<LIBRARY_TABLE_ROW*> row = libs->GetRow( name );

        if( libs->HasLibrary( name, true ) && row.has_value() && !row.value()->Hidden() )
            names.push_back( name );
    }

    return names;
}


wxObjectDataPtr<LIB_TREE_MODEL_ADAPTER> SCH_COMPONENTS_PANEL::createAdapter( const wxString& aLibrary )
{
    // Same settings the symbol chooser makes sure of before loading libraries
    GetAppSettings<EESCHEMA_SETTINGS>( "eeschema" );

    SYMBOL_LIBRARY_ADAPTER* libs = PROJECT_SCH::SymbolLibAdapter( &m_schFrame->Prj() );
    wxObjectDataPtr<LIB_TREE_MODEL_ADAPTER> adapter =
            SYMBOL_TREE_MODEL_ADAPTER::Create( m_schFrame, libs );
    SYMBOL_TREE_MODEL_ADAPTER* symbolAdapter = static_cast<SYMBOL_TREE_MODEL_ADAPTER*>( adapter.get() );

    if( aLibrary.IsEmpty() )
    {
        symbolAdapter->AddLibraries( m_schFrame );
    }
    else
    {
        symbolAdapter->AddLibrary( aLibrary, false );
        symbolAdapter->AssignIntrinsicRanks();
    }

    // Altium's list: Design Item ID | Description | Comment
    std::vector<wxString> shown = adapter->GetShownColumns();

    if( std::find( shown.begin(), shown.end(), wxS( "Value" ) ) == shown.end() )
    {
        if( std::find( shown.begin(), shown.end(), wxS( "Description" ) ) == shown.end() )
            shown.insert( shown.begin(), wxS( "Description" ) );

        shown.push_back( wxS( "Value" ) );
        adapter->SetShownColumns( shown );
    }

    return adapter;
}


void SCH_COMPONENTS_PANEL::showPreview( const LIB_ID& aLibId, int aUnit )
{
    if( !aLibId.IsValid() )
    {
        m_symbolPreview->SetStatusText( _( "Select a component" ) );

        if( m_footprintPreview->IsInitialized() )
            m_footprintPreview->SetStatusText( wxEmptyString );

        return;
    }

    m_symbolPreview->DisplaySymbol( aLibId, aUnit );

    if( !m_footprintPreview->IsInitialized() )
        return;

    LIB_SYMBOL* symbol = m_schFrame->GetLibSymbol( aLibId );
    SCH_FIELD*  fpField = symbol ? symbol->GetField( FIELD_T::FOOTPRINT ) : nullptr;
    wxString    fpName = fpField ? fpField->GetFullText() : wxString();
    LIB_ID      fpId;

    if( fpName.IsEmpty() )
    {
        m_footprintPreview->SetStatusText( _( "No footprint specified" ) );
    }
    else if( fpId.Parse( fpName ) == -1 && fpId.IsValid() )
    {
        m_footprintPreview->ClearStatus();
        m_footprintPreview->DisplayFootprint( fpId );
    }
    else
    {
        m_footprintPreview->SetStatusText( _( "Invalid footprint specified" ) );
    }
}


void SCH_COMPONENTS_PANEL::place( const LIB_ID& aLibId, int aUnit, bool aRepeat )
{
    LIB_SYMBOL* libSymbol = m_schFrame->GetLibSymbol( aLibId );

    if( !libSymbol )
        return;

    SCH_SYMBOL* symbol = new SCH_SYMBOL( *libSymbol, aLibId, &m_schFrame->GetCurrentSheet(),
                                         std::max( 1, aUnit ) );
    symbol->SetParent( m_schFrame->GetScreen() );

    if( EESCHEMA_SETTINGS* cfg = m_schFrame->eeconfig(); cfg && cfg->m_AutoplaceFields.enable )
        symbol->AutoplaceFields( nullptr, AUTOPLACE_AUTO );

    // The part follows the cursor on the canvas until it is clicked down
    m_schFrame->GetCanvas()->SetFocus();
    m_schFrame->GetToolManager()->PostAction( SCH_ACTIONS::placeSymbol,
                                              SCH_ACTIONS::PLACE_SYMBOL_PARAMS{ symbol, true, aRepeat } );
}


/// The symbol of the first selected item (a field or pin stands for its symbol).
static SCH_SYMBOL* selectedSymbol( SCH_EDIT_FRAME* aFrame )
{
    SCH_SELECTION_TOOL* selTool = aFrame->GetToolManager()->GetTool<SCH_SELECTION_TOOL>();

    for( EDA_ITEM* item : selTool->GetSelection() )
    {
        if( item->Type() == SCH_SYMBOL_T )
            return static_cast<SCH_SYMBOL*>( item );

        if( item->GetParent() && item->GetParent()->Type() == SCH_SYMBOL_T )
            return static_cast<SCH_SYMBOL*>( item->GetParent() );
    }

    return nullptr;
}


bool SCH_COMPONENTS_PANEL::canReplace()
{
    return selectedSymbol( m_schFrame ) != nullptr;
}


void SCH_COMPONENTS_PANEL::replace( const LIB_ID& aLibId, int aUnit )
{
    SCH_SYMBOL* symbol = selectedSymbol( m_schFrame );

    if( !symbol )
        return;

    // The dialog changes the selected symbols: make that the symbol itself
    if( !symbol->IsSelected() )
    {
        m_schFrame->GetToolManager()->RunAction( ACTIONS::selectionClear );
        m_schFrame->GetToolManager()->RunAction<EDA_ITEM*>( ACTIONS::selectItem, symbol );
    }

    // KiCad's Change Symbols, set to the selected symbols and the picked part
    DIALOG_CHANGE_SYMBOLS dlg( m_schFrame, symbol, DIALOG_CHANGE_SYMBOLS::MODE::CHANGE );
    dlg.SetNewLibId( aLibId );
    dlg.ShowQuasiModal();
}


void SCH_COMPONENTS_PANEL::manageLibraries()
{
    m_schFrame->GetToolManager()->RunAction( ACTIONS::showSymbolLibTable );
    RefreshLibraries();
}
