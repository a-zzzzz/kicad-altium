/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2013 Jean-Pierre Charras, jp.charras at wanadoo.fr
 * Copyright (C) 2008 Wayne Stambaugh <stambaughw@gmail.com>
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 * Copyright (C) 2019 CERN
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include <advanced_config.h>
#include <api/api_plugin_manager.h>
#include <sch_draw_panel.h>
#include <sch_edit_frame.h>
#include <kiface_base.h>
#include <bitmaps.h>
#include <eeschema_id.h>
#include <pgm_base.h>
#include <python_scripting.h>
#include <tool/action_menu.h>
#include <tool/tool_manager.h>
#include <settings/common_settings.h>
#include <tool/action_toolbar.h>
#include <tool/ui/toolbar_context_menu_registry.h>
#include <widgets/wx_infobar.h>
#include <tools/sch_actions.h>
#include <tools/sch_selection_tool.h>
#include <widgets/hierarchy_pane.h>
#include <widgets/wx_aui_utils.h>
#include <widgets/sch_design_block_pane.h>
#include <widgets/sch_properties_panel.h>
#include <widgets/sch_search_pane.h>
#include <toolbars_sch_editor.h>
#include <wx/choice.h>
#include <wx/stattext.h>


ACTION_TOOLBAR_CONTROL SCH_ACTION_TOOLBAR_CONTROLS::currentVariant( "control.currentVariant",
                                                                    _( "Current variant" ),
                                                                    _( "Selects the current schematic variant" ),
                                                                    { FRAME_SCH } );


std::optional<TOOLBAR_CONFIGURATION> SCH_EDIT_TOOLBAR_SETTINGS::DefaultToolbarConfig( TOOLBAR_LOC aToolbar )
{
    TOOLBAR_CONFIGURATION config;

    // clang-format off
    switch( aToolbar )
    {
    case TOOLBAR_LOC::TOP_AUX:
        return std::nullopt;

    case TOOLBAR_LOC::LEFT:
        config.AppendAction( ACTIONS::toggleGrid )
              .WithContextMenu(
                      []( TOOL_MANAGER* aToolMgr )
                      {
                          SCH_SELECTION_TOOL* selTool = aToolMgr->GetTool<SCH_SELECTION_TOOL>();
                          auto               menu = std::make_unique<ACTION_MENU>( false, selTool );
                          menu->Add( ACTIONS::gridProperties );
                          return menu;
                      } )
              .AppendAction( ACTIONS::toggleGridOverrides )
              .AppendGroup( TOOLBAR_GROUP_CONFIG( _( "Units" ) )
                            .AddAction( ACTIONS::inchesUnits )
                            .AddAction( ACTIONS::milsUnits )
                            .AddAction( ACTIONS::millimetersUnits ) )
              .AppendGroup( TOOLBAR_GROUP_CONFIG( _( "Crosshair modes" ) )
                            .AddAction( ACTIONS::cursorSmallCrosshairs )
                            .AddAction( ACTIONS::cursorFullCrosshairs )
                            .AddAction( ACTIONS::cursor45Crosshairs ) );

        config.AppendSeparator()
              .AppendAction( SCH_ACTIONS::toggleHiddenPins );

        config.AppendSeparator()
              .AppendGroup( TOOLBAR_GROUP_CONFIG( _( "Line modes" ) )
                            .AddAction( SCH_ACTIONS::lineModeFree )
                            .AddAction( SCH_ACTIONS::lineMode90 )
                            .AddAction( SCH_ACTIONS::lineMode45 ) );

        config.AppendSeparator()
              .AppendAction( SCH_ACTIONS::toggleAnnotateAuto );

        config.AppendSeparator()
              .AppendAction( SCH_ACTIONS::showHierarchy )
              .AppendAction( ACTIONS::showProperties );

        if( ADVANCED_CFG::GetCfg().m_DrawBoundingBoxes )
            config.AppendAction( ACTIONS::toggleBoundingBoxes );

        break;

    case TOOLBAR_LOC::RIGHT:
        // Laid out like Altium's Active Bar: one button per tool family, with the rest of
        // the family in the button's drop-down palette.
        config.AppendAction( SCH_ACTIONS::selectionFilterMenu )
              .AppendGroup( TOOLBAR_GROUP_CONFIG( _( "Move objects" ) )
                            .AddAction( SCH_ACTIONS::move )
                            .AddAction( SCH_ACTIONS::drag )
                            .AddAction( SCH_ACTIONS::rotateCCW )
                            .AddAction( SCH_ACTIONS::rotateCW )
                            .AddAction( SCH_ACTIONS::mirrorV )
                            .AddAction( SCH_ACTIONS::mirrorH )
                            .AddAction( SCH_ACTIONS::swap ) )
              .AppendGroup( TOOLBAR_GROUP_CONFIG( _( "Select objects" ) )
                            .AddAction( SCH_ACTIONS::selectInsideArea )
                            .AddAction( ACTIONS::selectSetLasso )
                            .AddAction( SCH_ACTIONS::selectOutsideArea )
                            .AddAction( SCH_ACTIONS::selectTouchingRect )
                            .AddAction( SCH_ACTIONS::selectTouchingLine )
                            .AddAction( ACTIONS::selectAll )
                            .AddAction( SCH_ACTIONS::selectConnection )
                            .AddAction( SCH_ACTIONS::toggleSelection )
                            .AddAction( ACTIONS::selectSetRect ) )
              .AppendGroup( TOOLBAR_GROUP_CONFIG( _( "Align objects" ) )
                            .AddAction( SCH_ACTIONS::alignLeft )
                            .AddAction( SCH_ACTIONS::alignRight )
                            .AddAction( SCH_ACTIONS::alignCenterX )
                            .AddAction( SCH_ACTIONS::alignTop )
                            .AddAction( SCH_ACTIONS::alignBottom )
                            .AddAction( SCH_ACTIONS::alignCenterY )
                            .AddAction( SCH_ACTIONS::alignToGrid ) );

        config.AppendSeparator()
              .AppendAction( SCH_ACTIONS::placeSymbol )
              .AppendGroup( TOOLBAR_GROUP_CONFIG( _( "Wiring tools" ) )
                            .AddAction( SCH_ACTIONS::drawWire )
                            .AddAction( SCH_ACTIONS::drawBus )
                            .AddAction( SCH_ACTIONS::placeBusWireEntry )
                            .AddAction( SCH_ACTIONS::placeJunction ) )
              .AppendGroup( TOOLBAR_GROUP_CONFIG( _( "Net labels and power ports" ) )
                            .AddAction( SCH_ACTIONS::placeLabel )
                            .AddAction( SCH_ACTIONS::placePowerGnd )
                            .AddAction( SCH_ACTIONS::placePowerVcc )
                            .AddAction( SCH_ACTIONS::placePowerPlus12 )
                            .AddAction( SCH_ACTIONS::placePowerPlus5 )
                            .AddAction( SCH_ACTIONS::placePowerMinus5 )
                            .AddAction( SCH_ACTIONS::placePowerSignalGnd )
                            .AddAction( SCH_ACTIONS::placePowerEarth )
                            .AddAction( SCH_ACTIONS::placePower )
                            .AddAction( SCH_ACTIONS::placeGlobalLabel )
                            .AddAction( SCH_ACTIONS::placeClassLabel ) )
              .AppendGroup( TOOLBAR_GROUP_CONFIG( _( "Sheet tools" ) )
                            .AddAction( SCH_ACTIONS::drawSheet )
                            .AddAction( SCH_ACTIONS::placeHierLabel )
                            .AddAction( SCH_ACTIONS::placeSheetPin )
                            .AddAction( SCH_ACTIONS::syncAllSheetsPins ) )
              .AppendGroup( TOOLBAR_GROUP_CONFIG( _( "Directives" ) )
                            .AddAction( SCH_ACTIONS::placeNoConnect )
                            .AddAction( SCH_ACTIONS::drawRuleArea ) );

        config.AppendSeparator()
              .AppendGroup( TOOLBAR_GROUP_CONFIG( _( "Text tools" ) )
                            .AddAction( SCH_ACTIONS::placeSchematicText )
                            .AddAction( SCH_ACTIONS::drawTextBox )
                            .AddAction( SCH_ACTIONS::drawTable ) )
              .AppendGroup( TOOLBAR_GROUP_CONFIG( _( "Graphic tools" ) )
                            .AddAction( SCH_ACTIONS::drawLines )
                            .AddAction( SCH_ACTIONS::drawRectangle )
                            .AddAction( SCH_ACTIONS::drawCircle )
                            .AddAction( SCH_ACTIONS::drawArc )
                            .AddAction( SCH_ACTIONS::drawBezier )
                            .AddAction( SCH_ACTIONS::placeImage ) )
              .AppendAction( ACTIONS::deleteTool );

        break;

    case TOOLBAR_LOC::TOP_MAIN:
        // Sits at the right end of the menu row (the file and undo buttons are in the title
        // bar, as in Altium)
        config.AppendControl( SCH_ACTION_TOOLBAR_CONTROLS::currentVariant );

        // Insert all the IPC plugins here on the toolbar
        // TODO (ISM): Move this to individual actions for each script
        config.AppendControl( ACTION_TOOLBAR_CONTROLS::ipcScripting );

        config.AppendSpacer( -1 )
              .AppendAction( ACTIONS::openPreferences );

        break;
    }

    // clang-format on
    return config;
}


void SCH_EDIT_FRAME::configureToolbars()
{
    SCH_BASE_FRAME::configureToolbars();

    // Variant selection drop down control on main tool bar.
    auto variantSelectionCtrlFactory =
            [this]( ACTION_TOOLBAR* aToolbar )
            {
                std::optional<wxString> currentVariantName = Schematic().GetCurrentVariant();
                wxString tmp = currentVariantName ? *currentVariantName : GetDefaultVariantName();

                m_currentVariantCtrl = new wxChoice( aToolbar, ID_TOOLBAR_SCH_SELECT_VARAIANT, wxDefaultPosition,
                                                     wxDefaultSize, Schematic().GetVariantNamesForUI(), 0,
                                                     wxDefaultValidator, tmp );

                m_currentVariantCtrl->SetToolTip( _( "Select the current variant to display and edit." ) );
                aToolbar->Add( m_currentVariantCtrl );
                UpdateVariantSelectionCtrl( Schematic().GetVariantNamesForUI() );
            };

    RegisterCustomToolbarControlFactory( SCH_ACTION_TOOLBAR_CONTROLS::currentVariant, variantSelectionCtrlFactory );

    // IPC/Scripting plugin control
    // TODO (ISM): Clean this up to make IPC actions just normal tool actions to get rid of this entire
    // control
    auto pluginControlFactory =
            [this]( ACTION_TOOLBAR* aToolbar )
            {
                // Add scripting console and API plugins
                bool scriptingAvailable = SCRIPTING::IsWxAvailable();

#ifdef KICAD_IPC_API
                bool haveApiPlugins = Pgm().GetCommonSettings()->m_Api.enable_server
                                        && !Pgm().GetPluginManager().GetActionsForScope( PluginActionScope() ).empty();
#else
                bool haveApiPlugins = false;
#endif

                if( scriptingAvailable || haveApiPlugins )
                {
                    aToolbar->AddScaledSeparator( aToolbar->GetParent() );

                    if( haveApiPlugins )
                        AddApiPluginTools( aToolbar );
                }
            };

    RegisterCustomToolbarControlFactory( ACTION_TOOLBAR_CONTROLS::ipcScripting, pluginControlFactory );
}


void SCH_EDIT_FRAME::ClearToolbarControl( int aId )
{
    SCH_BASE_FRAME::ClearToolbarControl( aId );

    switch( aId )
    {
    case ID_TOOLBAR_SCH_SELECT_VARAIANT: m_currentVariantCtrl = nullptr; break;
    }
}


void SCH_EDIT_FRAME::UpdateVariantSelectionCtrl( const wxArrayString& aVariantNames )
{
    if( !m_currentVariantCtrl )
        return;

    // Fall back to the default if nothing is currently selected.
    wxString currentSelection = GetDefaultVariantName();
    int selectionIndex = m_currentVariantCtrl->GetSelection();

    if( selectionIndex != wxNOT_FOUND )
        currentSelection = m_currentVariantCtrl->GetString( selectionIndex );

    // Add all variant names, a separator, and "Add New Variant..." at the end
    wxArrayString contents = aVariantNames;
    contents.Add( wxS( "---" ) );
    contents.Add( _( "Add New Design Variant..." ) );

    m_currentVariantCtrl->Set( contents );

    selectionIndex = m_currentVariantCtrl->FindString( currentSelection );

    if( ( selectionIndex == wxNOT_FOUND ) && ( m_currentVariantCtrl->GetCount() != 0 ) )
        selectionIndex = 0;

    m_currentVariantCtrl->SetSelection( selectionIndex );
}


void SCH_EDIT_FRAME::onVariantSelected( wxCommandEvent& aEvent )
{
    if( aEvent.GetId() != ID_TOOLBAR_SCH_SELECT_VARAIANT )
        return;

    wxCHECK( m_currentVariantCtrl, /* void */ );

    int selection = m_currentVariantCtrl->GetSelection();
    int count = m_currentVariantCtrl->GetCount();

    if( selection == wxNOT_FOUND || count == 0 )
        return;

    // "Add New Variant..." is always the last item, separator is second-to-last
    if( selection == count - 1 )
    {
        // Restore previous selection before showing dialog
        int previousSelection = m_currentVariantCtrl->FindString(
                Schematic().GetCurrentVariant().IsEmpty() ? GetDefaultVariantName()
                                                          : Schematic().GetCurrentVariant() );

        if( previousSelection != wxNOT_FOUND )
            m_currentVariantCtrl->SetSelection( previousSelection );

        ShowAddVariantDialog();
        return;
    }

    // Separator line - ignore selection
    if( selection == count - 2 )
    {
        int previousSelection = m_currentVariantCtrl->FindString(
                Schematic().GetCurrentVariant().IsEmpty() ? GetDefaultVariantName()
                                                          : Schematic().GetCurrentVariant() );

        if( previousSelection != wxNOT_FOUND )
            m_currentVariantCtrl->SetSelection( previousSelection );

        return;
    }

    wxString selectedString = m_currentVariantCtrl->GetString( selection );
    wxString selectedVariant;

    if( selectedString != GetDefaultVariantName() )
        selectedVariant = selectedString;

    Schematic().SetCurrentVariant( selectedVariant );
    UpdateProperties();
    HardRedraw();

    // Refresh message panel for current selection
    SCH_SELECTION_TOOL* selTool = m_toolManager->GetTool<SCH_SELECTION_TOOL>();

    if( selTool )
    {
        SCH_SELECTION& sub_sel = selTool->GetSelection();

        if( sub_sel.GetSize() == 1 )
            SetMsgPanel( static_cast<EDA_ITEM*>( sub_sel.Front() ) );
    }
}


bool SCH_EDIT_FRAME::ShowAddVariantDialog( wxWindow* aParent )
{
    if( !aParent )
        aParent = this;

    // Create a dialog with both name and description fields
    wxDialog dlg( aParent, wxID_ANY, _( "New Design Variant" ), wxDefaultPosition, wxDefaultSize,
                  wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER );

    wxBoxSizer* mainSizer = new wxBoxSizer( wxVERTICAL );

    // Name field
    wxStaticText* nameLabel = new wxStaticText( &dlg, wxID_ANY, _( "Variant name:" ) );
    mainSizer->Add( nameLabel, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, 10 );

    mainSizer->AddSpacer( 3 );

    wxTextCtrl* nameCtrl = new wxTextCtrl( &dlg, wxID_ANY );
    mainSizer->Add( nameCtrl, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 10 );

    // Description field
    wxStaticText* descLabel = new wxStaticText( &dlg, wxID_ANY, _( "Description (optional):" ) );
    mainSizer->Add( descLabel, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, 10 );

    mainSizer->AddSpacer( 3 );

    wxTextCtrl* descCtrl = new wxTextCtrl( &dlg, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                           wxSize( 300, 60 ), wxTE_MULTILINE );
    mainSizer->Add( descCtrl, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 10 );

    // Buttons
    wxStdDialogButtonSizer* btnSizer = new wxStdDialogButtonSizer();
    btnSizer->AddButton( new wxButton( &dlg, wxID_OK ) );
    btnSizer->AddButton( new wxButton( &dlg, wxID_CANCEL ) );
    btnSizer->Realize();
    mainSizer->Add( btnSizer, 0, wxALL | wxALIGN_RIGHT, 5 );

    dlg.SetSizer( mainSizer );
    dlg.Fit();
    dlg.Centre();
    nameLabel->SetFocus();

    if( dlg.ShowModal() == wxID_CANCEL )
        return false;

    wxString variantName = nameCtrl->GetValue().Trim().Trim( false );
    wxString variantDesc = descCtrl->GetValue().Trim().Trim( false );

    // Empty strings, reserved names, and duplicate variant names are not allowed.
    if( variantName.IsEmpty() )
    {
        GetInfoBar()->ShowMessageFor( _( "Variant name cannot be empty." ), 10000, wxICON_ERROR );
        return false;
    }

    // Check for reserved name (case-insensitive)
    if( variantName.CmpNoCase( GetDefaultVariantName() ) == 0 )
    {
        GetInfoBar()->ShowMessageFor( wxString::Format( _( "'%s' is a reserved variant name." ),
                                                         GetDefaultVariantName() ),
                                      10000, wxICON_ERROR );
        return false;
    }

    // Check for duplicate variant names (case-insensitive)
    for( const wxString& existingName : Schematic().GetVariantNames() )
    {
        if( existingName.CmpNoCase( variantName ) == 0 )
        {
            GetInfoBar()->ShowMessageFor( wxString::Format( _( "Variant '%s' already exists." ),
                                                            existingName ),
                                          10000, wxICON_ERROR );
            return false;
        }
    }

    // Add variant to the schematic
    Schematic().AddVariant( variantName );

    if( !variantDesc.IsEmpty() )
        Schematic().SetVariantDescription( variantName, variantDesc );

    // Update the variant selector and select the new variant
    UpdateVariantSelectionCtrl( Schematic().GetVariantNamesForUI() );
    SetCurrentVariant( variantName );
    OnModify();
    return true;
}


void SCH_EDIT_FRAME::SetCurrentVariant( const wxString& aVariantName )
{
    if( !m_currentVariantCtrl )
        return;

    wxString name = aVariantName.IsEmpty() ? GetDefaultVariantName() : aVariantName;

    int newSelection = m_currentVariantCtrl->FindString( name );

    if( newSelection == wxNOT_FOUND )
        return;

    int currentSelection = m_currentVariantCtrl->GetSelection();

    wxString selectedString;

    if( currentSelection != wxNOT_FOUND )
        selectedString = m_currentVariantCtrl->GetString( currentSelection );

    if( selectedString != name )
    {
        m_currentVariantCtrl->SetSelection( newSelection );
        Schematic().SetCurrentVariant( aVariantName );

        UpdateProperties();
        HardRedraw();
    }
}
