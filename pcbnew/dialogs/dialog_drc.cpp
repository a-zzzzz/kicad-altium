/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2018 Jean-Pierre Charras, jp.charras at wanadoo.fr
 * Copyright (C) 2009-2016 Dick Hollenbeck, dick@softplc.com
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
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

#include <confirm.h>
#include <core/throttle.h>
#include <dialog_drc.h>
#include <board_design_settings.h>
#include <kiface_base.h>
#include <macros.h>
#include <pad.h>
#include <drc/drc_item.h>
#include <drc/drc_report.h>
#include <connectivity/connectivity_data.h>
#include <connectivity/connectivity_algo.h>
#include <drawing_sheet/ds_proxy_view_item.h>
#include <pcb_edit_frame.h>
#include <tool/tool_manager.h>
#include <tools/pcb_actions.h>
#include <wildcards_and_files_ext.h>
#include <pcb_marker.h>
#include <pcb_track.h>
#include <pgm_base.h>
#include <wx/app.h>
#include <wx/filedlg.h>
#include <wx/msgdlg.h>
#include <wx/statusbr.h>
#include <wx/wupdlock.h>
#include <widgets/appearance_controls.h>
#include <widgets/ui_common.h>
#include <widgets/std_bitmap_button.h>
#include <widgets/progress_reporter_base.h>
#include <widgets/wx_html_report_box.h>
#include <view/view_controls.h>
#include <dialogs/panel_setup_rules_base.h>
#include <dialogs/dialog_text_entry.h>
#include <tools/drc_tool.h>
#include <tools/zone_filler_tool.h>
#include <tools/board_inspection_tool.h>
#include <kiplatform/ui.h>
#include <widgets/messages_panel.h>
#include <wx/aui/framemanager.h>
#include <wx/dcgraph.h>
#include <wx/dcmemory.h>
#include <wx/imaglist.h>
#include <wx/menu.h>
#include <wx/statline.h>
#include <wx/treectrl.h>
#include <functional>

// wxWidgets spends *far* too long calcuating column widths (most of it, believe it or
// not, in repeatedly creating/destroying a wxDC to do the measurement in).
// Use default column widths instead.
static int DEFAULT_SINGLE_COL_WIDTH = 660;

static BOARD*  g_lastDRCBoard = nullptr;
static bool    g_lastDRCRun = false;
static bool    g_lastFootprintTestsRun = false;

static std::vector<std::pair<wxString, int>> g_lastIgnored;


DIALOG_DRC::DIALOG_DRC( PCB_EDIT_FRAME* aEditorFrame, wxWindow* aParent ) :
        DIALOG_DRC_BASE( aParent ),
        PROGRESS_REPORTER_BASE( 1 ),
        m_running( false ),
        m_drcRun( false ),
        m_footprintTestsRun( false ),
        m_report_all_track_errors( false ),
        m_crossprobe( true ),
        m_scroll_on_crossprobe( true ),
        m_markersTreeModel( nullptr ),
        m_unconnectedTreeModel( nullptr ),
        m_fpWarningsTreeModel( nullptr ),
        m_updateThrottle( std::chrono::milliseconds( 100 ) ),
        m_yieldThrottle( std::chrono::milliseconds( 2000 ) ),
        m_drcStatusBar( nullptr ),
        m_lastTickSeconds( -1 ),
        m_tree( nullptr ),
        m_pages( nullptr ),
        m_rulesList( nullptr ),
        m_rulesTitle( nullptr ),
        m_cbCreateReport( nullptr ),
        m_cbAllTrackErrors( nullptr ),
        m_cbCrossprobe( nullptr ),
        m_cbCenterCrossprobe( nullptr ),
        m_rulesCategory( -1 ),
        m_syncingTree( false )
{
    SetName( DIALOG_DRC_WINDOW_NAME ); // Set a window name to be able to find it
    KIPLATFORM::UI::SetFloatLevel( this );

    m_frame = aEditorFrame;
    m_currentBoard = m_frame->GetBoard();

    m_bMenu->SetBitmap( KiBitmapBundle( BITMAPS::config ) );

    if( PCBNEW_SETTINGS* cfg = m_frame->GetPcbNewSettings() )
    {
        m_report_all_track_errors = cfg->m_DRCDialog.report_all_track_errors;
        m_crossprobe = cfg->m_DRCDialog.crossprobe;
        m_scroll_on_crossprobe = cfg->m_DRCDialog.scroll_on_crossprobe;
    }

    m_messages->SetImmediateMode();

    m_markersProvider = std::make_shared<DRC_ITEMS_PROVIDER>( m_currentBoard,
                                                              MARKER_BASE::MARKER_DRC,
                                                              MARKER_BASE::MARKER_DRAWING_SHEET );

    m_ratsnestProvider = std::make_shared<DRC_ITEMS_PROVIDER>( m_currentBoard,
                                                               MARKER_BASE::MARKER_RATSNEST );

    m_fpWarningsProvider = std::make_shared<DRC_ITEMS_PROVIDER>( m_currentBoard,
                                                                 MARKER_BASE::MARKER_PARITY );

    m_markersTreeModel = new RC_TREE_MODEL( m_frame, m_markerDataView );
    m_markerDataView->AssociateModel( m_markersTreeModel );

    m_unconnectedTreeModel = new RC_TREE_MODEL( m_frame, m_unconnectedDataView );
    m_unconnectedDataView->AssociateModel( m_unconnectedTreeModel );

    m_fpWarningsTreeModel = new RC_TREE_MODEL( m_frame, m_footprintsDataView );
    m_footprintsDataView->AssociateModel( m_fpWarningsTreeModel );

    // Prevent RTL locales from mirroring the text in the data views
    m_markerDataView->SetLayoutDirection( wxLayout_LeftToRight );
    m_unconnectedDataView->SetLayoutDirection( wxLayout_LeftToRight );
    m_footprintsDataView->SetLayoutDirection( wxLayout_LeftToRight );
    m_ignoredList->SetLayoutDirection( wxLayout_LeftToRight );

    m_ignoredList->InsertColumn( 0, wxEmptyString, wxLIST_FORMAT_LEFT, DEFAULT_SINGLE_COL_WIDTH );

    if( m_currentBoard == g_lastDRCBoard )
    {
        m_drcRun = g_lastDRCRun;
        m_footprintTestsRun = g_lastFootprintTestsRun;

        for( const auto& [ str, code ] : g_lastIgnored )
        {
            wxListItem listItem;
            listItem.SetId( m_ignoredList->GetItemCount() );
            listItem.SetText( str );
            listItem.SetData( code );

            m_ignoredList->InsertItem( listItem );
        }
    }

    m_Notebook->SetSelection( 0 );

    if( Kiface().IsSingle() )
        m_cbTestFootprints->Hide();

    SetupStandardButtons( { { wxID_OK,     _( "Run Design Rule Check..." ) },
                            { wxID_CANCEL, _( "Close" )   } } );

    m_markersTitleTemplate     = m_Notebook->GetPageText( 0 );
    m_unconnectedTitleTemplate = m_Notebook->GetPageText( 1 );
    m_footprintsTitleTemplate  = m_Notebook->GetPageText( 2 );
    m_ignoredTitleTemplate     = m_Notebook->GetPageText( 3 );

    // DPI fix
    bSizerViolationsBox->SetMinSize( FromDIP( bSizerViolationsBox->GetMinSize() ) );

    m_drcStatusBar = new wxStatusBar( this, wxID_ANY, wxSTB_DEFAULT_STYLE );
    m_drcStatusBar->SetFieldsCount( 2 );

    int statusBarWidths[2] = { FromDIP( 12 ), -1 };
    m_drcStatusBar->SetStatusWidths( 2, statusBarWidths );

    if( wxSizer* mainSizer = GetSizer() )
        mainSizer->Add( m_drcStatusBar, 0, wxEXPAND );

    buildAltiumLayout();

    Layout(); // adding the units above expanded Clearance text, now resize.

    SetFocus();

    finishDialogSettings();
}


DIALOG_DRC::~DIALOG_DRC()
{
    if( PCBNEW_SETTINGS* cfg = m_frame->GetPcbNewSettings() )
    {
        cfg->m_DRCDialog.report_all_track_errors = m_report_all_track_errors;
        cfg->m_DRCDialog.crossprobe = m_crossprobe;
        cfg->m_DRCDialog.scroll_on_crossprobe = m_scroll_on_crossprobe;
    }

    m_frame->ClearFocus();

    g_lastDRCBoard = m_currentBoard;
    g_lastDRCRun = m_drcRun;
    g_lastFootprintTestsRun = m_footprintTestsRun;

    g_lastIgnored.clear();

    for( int ii = 0; ii < m_ignoredList->GetItemCount(); ++ii )
        g_lastIgnored.push_back( { m_ignoredList->GetItemText( ii ), m_ignoredList->GetItemData( ii ) } );

    m_markersTreeModel->DecRef();
    m_unconnectedTreeModel->DecRef();
    m_fpWarningsTreeModel->DecRef();
}


bool DIALOG_DRC::TransferDataToWindow()
{
    UpdateData();
    return true;
}


void DIALOG_DRC::OnActivateDlg( wxActivateEvent& aEvent )
{
    if( m_currentBoard != m_frame->GetBoard() )
    {
        // If m_currentBoard is not the current board, (for instance because a new board was loaded),
        // close the dialog, because many pointers are now invalid in lists
        SetReturnCode( wxID_CANCEL );
        Close();

        DRC_TOOL* drcTool = m_frame->GetToolManager()->GetTool<DRC_TOOL>();
        drcTool->DestroyDRCDialog();

        return;
    }

    // Let DIALOG_SHIM re-establish keyboard focus so ESC keeps closing the dialog.
    aEvent.Skip();
}


// PROGRESS_REPORTER calls

bool DIALOG_DRC::updateUI()
{
    if( m_maxProgress != 0 )
    {
        double cur = std::clamp( (double) m_progress.load() / m_maxProgress, 0.0, 1.0 );

        int newValue = KiROUND( cur * 1000.0 );
        m_gauge->SetValue( newValue );
    }

    if( m_running && m_drcStatusBar )
    {
        int elapsed = static_cast<int>(
                std::chrono::duration_cast<std::chrono::seconds>( std::chrono::steady_clock::now() - m_drcStartTime )
                        .count() );

        if( elapsed != m_lastTickSeconds )
        {
            m_lastTickSeconds = elapsed;

            wxString tick;

            if( elapsed >= 60 )
                tick = wxString::Format( wxT( "%1$d min %2$d s" ), elapsed / 60, elapsed % 60 );
            else
                tick = wxString::Format( wxT( "%d s" ), elapsed );

            m_drcStatusBar->SetStatusText( wxString::Format( _( "Elapsed: %s" ), tick ), 1 );
        }
    }

    // Repaint the dialog at ~10Hz using Update() which processes only pending expose/
    // draw events without entering the full platform event loop.
    if( m_updateThrottle.Ready() )
        Update();

    // Yield to the event loop infrequently so the cancel button remains functional.
    // On some Linux systems with glycin-enabled gdk-pixbuf (2.44+), entering the GTK event
    // loop triggers heavyweight sandbox process spawning that can add hundreds of milliseconds
    // per call, so we keep this interval long.
    if( m_yieldThrottle.Ready() )
        Pgm().App().SafeYieldFor( this, wxEVT_CATEGORY_NATIVE_EVENTS );

    return !m_cancelled;
}


void DIALOG_DRC::AdvancePhase( const wxString& aMessage )
{
    PROGRESS_REPORTER_BASE::AdvancePhase( aMessage );
    SetCurrentProgress( 0.0 );

    m_messages->Report( aMessage );
}


int DIALOG_DRC::getSeverities()
{
    int severities = 0;

    if( m_showErrors->GetValue() )
        severities |= RPT_SEVERITY_ERROR;

    if( m_showWarnings->GetValue() )
        severities |= RPT_SEVERITY_WARNING;

    if( m_showExclusions->GetValue() )
        severities |= RPT_SEVERITY_EXCLUSION;

    return severities;
}


void DIALOG_DRC::OnMenu( wxCommandEvent& event )
{
    // Build a pop menu:
    wxMenu menu;

    menu.Append( 4205, _( "Report All Errors for Each Track" ),
                 _( "If unchecked, only the first error will be reported for each track" ),
                 wxITEM_CHECK );
    menu.Check( 4205, m_report_all_track_errors );

    menu.AppendSeparator();

    menu.Append( 4206, _( "Cross-probe Selected Items" ),
                 _( "Highlight corresponding items on canvas when selected in the DRC list" ),
                 wxITEM_CHECK );
    menu.Check( 4206, m_crossprobe );

    menu.Append( 4207, _( "Center on Cross-probe" ),
                 _( "When cross-probing, scroll the canvas so that the item is visible" ),
                 wxITEM_CHECK );
    menu.Check( 4207, m_scroll_on_crossprobe );

    // menu_id is the selected submenu id from the popup menu or wxID_NONE
    int menu_id = m_bMenu->GetPopupMenuSelectionFromUser( menu );

    if( menu_id == 0 || menu_id == 4205 )
    {
        m_report_all_track_errors = !m_report_all_track_errors;
    }
    else if( menu_id == 2 || menu_id == 4206 )
    {
        m_crossprobe = !m_crossprobe;
    }
    else if( menu_id == 3 || menu_id == 4207 )
    {
        m_scroll_on_crossprobe = !m_scroll_on_crossprobe;
    }
}


void DIALOG_DRC::OnErrorLinkClicked( wxHtmlLinkEvent& event )
{
    m_frame->ShowBoardSetupDialog( _( "Custom Rules" ), this );
}


void DIALOG_DRC::OnCharHook( wxKeyEvent& aEvt )
{
    if( int hotkey = aEvt.GetKeyCode() )
    {
        if( aEvt.ControlDown() )
            hotkey |= MD_CTRL;
        if( aEvt.ShiftDown() )
            hotkey |= MD_SHIFT;
        if( aEvt.AltDown() )
            hotkey |= MD_ALT;

        if( hotkey == ACTIONS::excludeMarker.GetHotKey() )
        {
            ExcludeMarker();
            return;
        }
    }

    DIALOG_SHIM::OnCharHook( aEvt );
}


void DIALOG_DRC::OnRunDRCClick( wxCommandEvent& aEvent )
{
    TOOL_MANAGER*     toolMgr              = m_frame->GetToolManager();
    DRC_TOOL*         drcTool              = toolMgr->GetTool<DRC_TOOL>();
    ZONE_FILLER_TOOL* zoneFillerTool       = toolMgr->GetTool<ZONE_FILLER_TOOL>();
    bool              refillZones          = m_cbRefillZones->GetValue();
    bool              testFootprints       = m_cbTestFootprints->GetValue();

    if( zoneFillerTool->IsBusy() )
    {
        wxBell();
        return;
    }

    // The progress and the results are on the Violations page
    selectPage( PAGE_VIOLATIONS );

    m_footprintTestsRun = false;
    m_cancelled = false;

    m_frame->GetBoard()->RecordDRCExclusions();
    deleteAllMarkers( true );

    // This is not the time to have stale or buggy rules.  Ensure they're up-to-date
    // and that they at least parse.
    try
    {
        drcTool->GetDRCEngine()->InitEngine( m_frame->GetDesignRulesPath() );
    }
    catch( PARSE_ERROR& )
    {
        m_runningResultsBook->ChangeSelection( 0 );   // Display the "Tests Running..." tab
        m_DeleteCurrentMarkerButton->Enable( false );
        m_DeleteAllMarkersButton->Enable( false );
        m_saveReport->Enable( false );

        m_messages->Clear();
        m_messages->Report( _( "DRC incomplete: could not compile custom design rules." )
                            + wxS( "&nbsp;&nbsp;" )
                            + wxS( "<a href='$CUSTOM_RULES'>" ) + _( "Show design rules." ) + wxT( "</a>" ) );
        m_messages->Flush();

        Raise();

        // set float level again, it can be lost due to window events during test run
        KIPLATFORM::UI::SetFloatLevel( this );
        return;
    }

    std::vector<std::reference_wrapper<RC_ITEM>> violations = DRC_ITEM::GetItemsWithSeverities();
    m_ignoredList->DeleteAllItems();

    for( std::reference_wrapper<RC_ITEM>& item : violations )
    {
        if( bds().GetSeverity( item.get().GetErrorCode() ) == RPT_SEVERITY_IGNORE )
        {
            wxListItem listItem;
            listItem.SetId( m_ignoredList->GetItemCount() );
            listItem.SetText( wxT( " • " ) + item.get().GetErrorText( true ) );
            listItem.SetData( item.get().GetErrorCode() );

            m_ignoredList->InsertItem( listItem );
        }
    }

    m_ignoredList->SetColumnWidth( 0, m_ignoredList->GetParent()->GetClientSize().x - 20 );

    Raise();

    m_runningResultsBook->ChangeSelection( 0 );   // Display the "Tests Running..." tab
    m_messages->Clear();
    Update();                                     // Repaint only, don't enter the full event loop

    m_running = true;
    m_sdbSizerCancel->SetLabel( _( "Cancel" ) );
    m_sdbSizerOK->Enable( false );
    m_DeleteCurrentMarkerButton->Enable( false );
    m_DeleteAllMarkersButton->Enable( false );
    m_saveReport->Enable( false );

    m_drcStartTime = std::chrono::steady_clock::now();
    m_lastTickSeconds = -1;

    if( m_drcStatusBar )
        m_drcStatusBar->SetStatusText( _( "Elapsed: 0 s" ), 1 );

    {
    wxBusyCursor dummy;
    drcTool->RunTests( this, refillZones, m_report_all_track_errors, testFootprints );
    }

    double elapsedMs =
            std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - m_drcStartTime ).count();

    auto formatElapsed =
            [&]() -> wxString
            {
                int totalSeconds = KiROUND( elapsedMs / 1000.0 );

                if( totalSeconds >= 60 )
                    return wxString::Format( _( "%1$d min %2$d s" ), totalSeconds / 60, totalSeconds % 60 );

                return wxString::Format( _( "%.2f s" ), elapsedMs / 1000.0 );
            };

    if( m_cancelled )
    {
        m_messages->Report( _( "-------- DRC canceled by user.<br><br>" ) );

        if( m_drcStatusBar )
            m_drcStatusBar->SetStatusText( wxString::Format( _( "Canceled after %s" ), formatElapsed() ), 1 );
    }
    else
    {
        m_messages->Report( _( "Done.<br><br>" ) );

        if( m_drcStatusBar )
            m_drcStatusBar->SetStatusText( wxString::Format( _( "Completed in %s" ), formatElapsed() ), 1 );
    }

    Raise();
    Update();                                     // Repaint only, don't enter the full event loop

    m_running = false;
    m_sdbSizerCancel->SetLabel( _( "Close" ) );
    m_sdbSizerOK->Enable( true );
    m_DeleteCurrentMarkerButton->Enable( true );
    m_DeleteAllMarkersButton->Enable( true );
    m_saveReport->Enable( true );

    if( !m_cancelled )
    {
        m_sdbSizerCancel->SetDefault();
        // wxWidgets has a tendency to keep both buttons highlighted without the following:
        m_sdbSizerOK->Enable( false );

        wxMilliSleep( 500 );
        m_runningResultsBook->ChangeSelection( 1 );
        KIPLATFORM::UI::ForceFocus( m_Notebook );

        // now re-enable m_sdbSizerOK button
        m_sdbSizerOK->Enable( true );

        // Altium: the report file is written, the violations go to the Messages panel and
        // the Design Rule Checker closes
        if( m_cbCreateReport && m_cbCreateReport->GetValue() )
            writeAltiumReport();

        showMessagesPanel();

        CallAfter(
                [this]()
                {
                    wxCommandEvent dummy;
                    OnCancelClick( dummy );
                } );
    }

    // set float level again, it can be lost due to window events during test run
    KIPLATFORM::UI::SetFloatLevel( this );
    refreshEditor();
}


void DIALOG_DRC::UpdateData()
{
    int severities = getSeverities();

    m_markersTreeModel->Update( m_markersProvider, severities );
    m_unconnectedTreeModel->Update( m_ratsnestProvider, severities );
    m_fpWarningsTreeModel->Update( m_fpWarningsProvider, severities );

    updateDisplayedCounts();
}


void DIALOG_DRC::OnDRCItemSelected( wxDataViewEvent& aEvent )
{
    if( !m_crossprobe )
    {
        aEvent.Skip();
        return;
    }

    BOARD*        board = m_frame->GetBoard();
    RC_TREE_NODE* node = RC_TREE_MODEL::ToNode( aEvent.GetItem() );

    if( !node )
    {
        // list is being freed; don't do anything with null ptrs

        aEvent.Skip();
        return;
    }

    std::shared_ptr<RC_ITEM> rc_item = node->m_RcItem;

    // The tree keeps its RC_ITEMs alive independently of the board, so rc_item->GetParent()
    // can dangle once the owning marker is deleted (board edited, DRC re-run, undo) while this
    // modeless dialog stays open.  Recover the still-live marker by matching the shared RC_ITEM
    // against the board's current markers instead of trusting the raw back-pointer.
    PCB_MARKER* parentMarker = nullptr;

    for( PCB_MARKER* marker : board->Markers() )
    {
        if( marker->GetRCItem() == rc_item )
        {
            parentMarker = marker;
            break;
        }
    }

    if( rc_item->GetErrorCode() == DRCE_UNRESOLVED_VARIABLE && parentMarker
            && parentMarker->GetMarkerType() == MARKER_BASE::MARKER_DRAWING_SHEET )
    {
        m_frame->FocusOnLocation( parentMarker->GetPos(), m_scroll_on_crossprobe );

        aEvent.Skip();
        return;
    }

    const KIID& itemID = RC_TREE_MODEL::ToUUID( aEvent.GetItem() );
    BOARD_ITEM* item = board->ResolveItem( itemID, true );

    if( !item )
    {
        // nothing to highlight / focus on
        aEvent.Skip();
        return;
    }

    PCB_LAYER_ID principalLayer;
    LSET         violationLayers;
    BOARD_ITEM*  a = board->ResolveItem( rc_item->GetMainItemID(), true );
    BOARD_ITEM*  b = board->ResolveItem( rc_item->GetAuxItemID(), true );

    DRC_ITEM::GetViolationLayers( board, rc_item, parentMarker, principalLayer, violationLayers );

    WINDOW_THAWER thawer( m_frame );

    if( principalLayer > UNDEFINED_LAYER && ( violationLayers & board->GetVisibleLayers() ).none() )
        m_frame->GetAppearancePanel()->SetLayerVisible( principalLayer, true );

    if( principalLayer > UNDEFINED_LAYER && board->GetVisibleLayers().test( principalLayer ) )
        m_frame->SetActiveLayer( principalLayer );

    if( rc_item->GetErrorCode() == DRCE_UNCONNECTED_ITEMS )
    {
        if( !m_frame->GetPcbNewSettings()->m_Display.m_ShowGlobalRatsnest )
            m_frame->GetToolManager()->RunAction( PCB_ACTIONS::showRatsnest );

        if( item->Type() == PCB_ZONE_T )
        {
            m_frame->FocusOnItem( item, principalLayer, m_scroll_on_crossprobe );

            m_frame->GetBoard()->GetConnectivity()->RunOnUnconnectedEdges(
                    [&]( CN_EDGE& edge )
                    {
                        // Connectivity was valid when DRC was run, but this is a modeless dialog
                        // so it might not be now.
                        if( !edge.GetSourceNode() || edge.GetSourceNode()->Dirty() )
                            return true;

                        if( !edge.GetTargetNode() || edge.GetTargetNode()->Dirty() )
                            return true;

                        if( edge.GetSourceNode()->Parent() == a
                            && edge.GetTargetNode()->Parent() == b )
                        {
                            VECTOR2I focusPos;

                            if( item == a && item == b )
                            {
                                focusPos = ( node->m_Type == RC_TREE_NODE::MAIN_ITEM ) ? edge.GetSourcePos()
                                                                                       : edge.GetTargetPos();
                            }
                            else
                            {
                                focusPos = ( item == edge.GetSourceNode()->Parent() ) ? edge.GetSourcePos()
                                                                                      : edge.GetTargetPos();
                            }

                            m_frame->FocusOnLocation( focusPos, m_scroll_on_crossprobe );
                            m_frame->RefreshCanvas();

                            return false;
                        }

                        return true;
                    } );
        }
        else
        {
            m_frame->FocusOnItem( item, principalLayer, m_scroll_on_crossprobe );
        }
    }
    else if( rc_item->GetErrorCode() == DRCE_DIFF_PAIR_UNCOUPLED_LENGTH_TOO_LONG )
    {
        BOARD_CONNECTED_ITEM*    track = dynamic_cast<PCB_TRACK*>( item );
        std::vector<BOARD_ITEM*> items;

        if( track )
        {
            int net = track->GetNetCode();

            wxASSERT( net > 0 );    // Without a net how can it be a diff-pair?

            for( const KIID& id : rc_item->GetIDs() )
            {
                auto* candidate = dynamic_cast<BOARD_CONNECTED_ITEM*>( board->ResolveItem( id, true ) );

                if( candidate && candidate->GetNetCode() == net )
                    items.push_back( candidate );
            }
        }
        else
        {
            items.push_back( item );
        }

        m_frame->FocusOnItems( items, principalLayer, m_scroll_on_crossprobe );
    }
    else
    {
        m_frame->FocusOnItem( item, principalLayer, m_scroll_on_crossprobe );
    }

    aEvent.Skip();
}


void DIALOG_DRC::OnDRCItemDClick( wxDataViewEvent& aEvent )
{
    if( aEvent.GetItem().IsOk() )
    {
        // turn control over to m_frame, hide this DIALOG_DRC window,
        // no destruction so we can preserve listbox cursor
        if( !IsModal() )
            Show( false );
    }

    // Do not skip aEvent here: this is not useful, and Pcbnew crashes if skipped (at least on MSW)
}


void DIALOG_DRC::OnDRCItemRClick( wxDataViewEvent& aEvent )
{
    TOOL_MANAGER*          toolMgr = m_frame->GetToolManager();
    BOARD_INSPECTION_TOOL* inspectionTool = toolMgr->GetTool<BOARD_INSPECTION_TOOL>();
    DRC_TOOL*              drcTool = toolMgr->GetTool<DRC_TOOL>();
    RC_TREE_NODE*          node = RC_TREE_MODEL::ToNode( aEvent.GetItem() );

    if( !node )
        return;

    std::shared_ptr<RC_ITEM>           rcItem = node->m_RcItem;
    DRC_ITEM*                          drcItem = static_cast<DRC_ITEM*>( rcItem.get() );
    std::shared_ptr<CONNECTIVITY_DATA> conn = m_currentBoard->GetConnectivity();
    wxString                           listName;
    wxMenu                             menu;

    switch( bds().m_DRCSeverities[ rcItem->GetErrorCode() ] )
    {
    case RPT_SEVERITY_ERROR:   listName = _( "errors" );      break;
    case RPT_SEVERITY_WARNING: listName = _( "warnings" );    break;
    default:                   listName = _( "appropriate" ); break;
    }

    enum MENU_IDS
    {
        ID_EDIT_EXCLUSION_COMMENT = 4467,
        ID_REMOVE_EXCLUSION,
        ID_REMOVE_EXCLUSION_ALL,
        ID_ADD_EXCLUSION,
        ID_ADD_EXCLUSION_WITH_COMMENT,
        ID_ADD_EXCLUSION_ALL,
        ID_INSPECT_VIOLATION,
        ID_FIX_VIOLATION,
        ID_SET_SEVERITY_TO_ERROR,
        ID_SET_SEVERITY_TO_WARNING,
        ID_SET_SEVERITY_TO_IGNORE,
        ID_EDIT_SEVERITIES
    };

    if( rcItem->GetParent()->IsExcluded() )
    {
        menu.Append( ID_REMOVE_EXCLUSION,
                     _( "Remove exclusion for this violation" ),
                     wxString::Format( _( "It will be placed back in the %s list" ), listName ) );

        menu.Append( ID_EDIT_EXCLUSION_COMMENT,
                     _( "Edit exclusion comment..." ) );

        if( drcItem->GetViolatingRule() && !drcItem->GetViolatingRule()->IsImplicit() )
        {
            menu.Append( ID_REMOVE_EXCLUSION_ALL,
                         wxString::Format( _( "Remove all exclusions for violations of rule '%s'" ),
                                           drcItem->GetViolatingRule()->m_Name ),
                         wxString::Format( _( "They will be placed back in the %s list" ), listName ) );
        }
    }
    else
    {
        menu.Append( ID_ADD_EXCLUSION,
                     _( "Exclude this violation" ),
                     wxString::Format( _( "It will be excluded from the %s list" ), listName ) );

        menu.Append( ID_ADD_EXCLUSION_WITH_COMMENT,
                     _( "Exclude with comment..." ),
                     wxString::Format( _( "It will be excluded from the %s list" ), listName ) );

        if( drcItem->GetViolatingRule() && !drcItem->GetViolatingRule()->IsImplicit() )
        {
            menu.Append( ID_ADD_EXCLUSION_ALL,
                         wxString::Format( _( "Exclude all violations of rule '%s'..." ),
                                           drcItem->GetViolatingRule()->m_Name ),
                         wxString::Format( _( "They will be excluded from the %s list" ), listName ) );
        }
    }

    menu.AppendSeparator();

    wxString inspectDRCErrorMenuText = inspectionTool->InspectDRCErrorMenuText( rcItem );
    wxString fixDRCErrorMenuText = drcTool->FixDRCErrorMenuText( rcItem );

    if( !inspectDRCErrorMenuText.IsEmpty() || !fixDRCErrorMenuText.IsEmpty() )
    {
        if( !inspectDRCErrorMenuText.IsEmpty() )
            menu.Append( ID_INSPECT_VIOLATION, inspectDRCErrorMenuText );

        if( !fixDRCErrorMenuText.IsEmpty() )
            menu.Append( ID_FIX_VIOLATION, fixDRCErrorMenuText );

        menu.AppendSeparator();
    }

    if( bds().m_DRCSeverities[ rcItem->GetErrorCode() ] == RPT_SEVERITY_WARNING )
    {
        menu.Append( ID_SET_SEVERITY_TO_ERROR,
                     wxString::Format( _( "Change severity to Error for all '%s' violations" ),
                                       rcItem->GetErrorText( true ) ),
                     _( "Violation severities can also be edited in Board Setup" ) );
    }
    else
    {
        menu.Append( ID_SET_SEVERITY_TO_WARNING,
                     wxString::Format( _( "Change severity to Warning for all '%s' violations" ),
                                       rcItem->GetErrorText( true ) ),
                     _( "Violation severities can also be edited in Board Setup" ) );
    }

    menu.Append( ID_SET_SEVERITY_TO_IGNORE,
                 wxString::Format( _( "Ignore all '%s' violations" ), rcItem->GetErrorText( true ) ),
                 _( "Violations will not be checked or reported" ) );

    menu.AppendSeparator();

    menu.Append( ID_EDIT_SEVERITIES,
                 _( "Edit violation severities..." ),
                 _( "Open the Board Setup dialog" ) );

    bool modified = false;
    int  command = GetPopupMenuSelectionFromUser( menu );

    switch( command )
    {
    case ID_EDIT_EXCLUSION_COMMENT:
        if( PCB_MARKER* marker = dynamic_cast<PCB_MARKER*>( node->m_RcItem->GetParent() ) )
        {
            WX_TEXT_ENTRY_DIALOG dlg( this, wxEmptyString, _( "Exclusion Comment" ), marker->GetComment(), true );

            if( dlg.ShowModal() == wxID_CANCEL )
                break;

            marker->SetExcluded( true, dlg.GetValue() );

            wxString serialized = marker->SerializeToString();
            bds().m_DrcExclusions.insert( serialized );
            bds().m_DrcExclusionComments[serialized] = dlg.GetValue();

            // Update view
            static_cast<RC_TREE_MODEL*>( aEvent.GetModel() )->ValueChanged( node );
            modified = true;
        }

        break;

    case ID_REMOVE_EXCLUSION:
        if( PCB_MARKER* marker = dynamic_cast<PCB_MARKER*>( rcItem->GetParent() ) )
        {
            marker->SetExcluded( false );

            wxString serialized = marker->SerializeToString();
            bds().m_DrcExclusions.erase( serialized );
            bds().m_DrcExclusionComments.erase( serialized );

            if( rcItem->GetErrorCode() == DRCE_UNCONNECTED_ITEMS )
            {
                m_frame->GetBoard()->UpdateRatsnestExclusions();
                m_frame->GetCanvas()->RedrawRatsnest();
            }
            else
            {
                m_frame->GetCanvas()->GetView()->Update( marker );
            }

            // Update view
            static_cast<RC_TREE_MODEL*>( aEvent.GetModel() )->ValueChanged( node );
            modified = true;
        }

        break;

    case ID_ADD_EXCLUSION:
    case ID_ADD_EXCLUSION_WITH_COMMENT:
        if( PCB_MARKER* marker = dynamic_cast<PCB_MARKER*>( rcItem->GetParent() ) )
        {
            wxString comment;

            if( command == ID_ADD_EXCLUSION_WITH_COMMENT )
            {
                WX_TEXT_ENTRY_DIALOG dlg( this, wxEmptyString, _( "Exclusion Comment" ), wxEmptyString, true );

                if( dlg.ShowModal() == wxID_CANCEL )
                    break;

                comment = dlg.GetValue();
            }

            marker->SetExcluded( true, comment );

            wxString serialized = marker->SerializeToString();
            bds().m_DrcExclusions.insert( serialized );
            bds().m_DrcExclusionComments[serialized] = comment;

            if( rcItem->GetErrorCode() == DRCE_UNCONNECTED_ITEMS )
            {
                m_frame->GetBoard()->UpdateRatsnestExclusions();
                m_frame->GetCanvas()->RedrawRatsnest();
            }
            else
            {
                m_frame->GetCanvas()->GetView()->Update( marker );
            }

            // Update view
            if( m_showExclusions->GetValue() )
                static_cast<RC_TREE_MODEL*>( aEvent.GetModel() )->ValueChanged( node );
            else
                static_cast<RC_TREE_MODEL*>( aEvent.GetModel() )->DeleteCurrentItem( false );

            modified = true;
        }

        break;

    case ID_REMOVE_EXCLUSION_ALL:
        for( PCB_MARKER* marker : m_frame->GetBoard()->Markers() )
        {
            DRC_ITEM* candidateDrcItem = static_cast<DRC_ITEM*>( marker->GetRCItem().get() );

            if( candidateDrcItem->GetViolatingRule() == drcItem->GetViolatingRule() )
            {
                marker->SetExcluded( false );

                wxString serialized = marker->SerializeToString();
                bds().m_DrcExclusions.erase( serialized );
                bds().m_DrcExclusionComments.erase( serialized );
            }
        }

        // Rebuild model and view
        static_cast<RC_TREE_MODEL*>( aEvent.GetModel() )->Update( m_markersProvider, getSeverities() );
        modified = true;
        break;

    case ID_ADD_EXCLUSION_ALL:
        for( PCB_MARKER* marker : m_frame->GetBoard()->Markers() )
        {
            DRC_ITEM* candidateDrcItem = static_cast<DRC_ITEM*>( marker->GetRCItem().get() );

            if( candidateDrcItem->GetViolatingRule() == drcItem->GetViolatingRule() )
            {
                marker->SetExcluded( true );

                wxString serialized = marker->SerializeToString();
                bds().m_DrcExclusions.insert( serialized );
            }
        }

        // Rebuild model and view
        static_cast<RC_TREE_MODEL*>( aEvent.GetModel() )->Update( m_markersProvider, getSeverities() );
        modified = true;
        break;

    case ID_INSPECT_VIOLATION:
        inspectionTool->InspectDRCError( node->m_RcItem );
        break;

    case ID_FIX_VIOLATION:
        drcTool->FixDRCError( node->m_RcItem );
        break;

    case ID_SET_SEVERITY_TO_ERROR:
        bds().m_DRCSeverities[ rcItem->GetErrorCode() ] = RPT_SEVERITY_ERROR;

        for( PCB_MARKER* marker : m_frame->GetBoard()->Markers() )
        {
            if( marker->GetRCItem()->GetErrorCode() == rcItem->GetErrorCode() )
                m_frame->GetCanvas()->GetView()->Update( marker );
        }

        // Rebuild model and view
        static_cast<RC_TREE_MODEL*>( aEvent.GetModel() )->Update( m_markersProvider, getSeverities() );
        modified = true;
        break;

    case ID_SET_SEVERITY_TO_WARNING:
        bds().m_DRCSeverities[ rcItem->GetErrorCode() ] = RPT_SEVERITY_WARNING;

        for( PCB_MARKER* marker : m_frame->GetBoard()->Markers() )
        {
            if( marker->GetRCItem()->GetErrorCode() == rcItem->GetErrorCode() )
                m_frame->GetCanvas()->GetView()->Update( marker );
        }

        // Rebuild model and view
        static_cast<RC_TREE_MODEL*>( aEvent.GetModel() )->Update( m_markersProvider, getSeverities() );
        modified = true;
        break;

    case ID_SET_SEVERITY_TO_IGNORE:
    {
        bds().m_DRCSeverities[ rcItem->GetErrorCode() ] = RPT_SEVERITY_IGNORE;

        wxListItem listItem;
        listItem.SetId( m_ignoredList->GetItemCount() );
        listItem.SetText( wxT( " • " ) + rcItem->GetErrorText( true ) );
        listItem.SetData( rcItem->GetErrorCode() );

        m_ignoredList->InsertItem( listItem );

        BOARD* board = m_frame->GetBoard();

        std::vector<BOARD_ITEM*> toRemove;

        for( PCB_MARKER* marker : board->Markers() )
        {
            if( marker->GetRCItem()->GetErrorCode() == rcItem->GetErrorCode() )
            {
                m_frame->GetCanvas()->GetView()->Remove( marker );
                toRemove.emplace_back( marker );
            }
        }

        for( BOARD_ITEM* marker : toRemove )
            board->Remove( marker, REMOVE_MODE::BULK );

        board->FinalizeBulkRemove( toRemove );

        if( rcItem->GetErrorCode() == DRCE_UNCONNECTED_ITEMS )
            m_frame->GetCanvas()->RedrawRatsnest();

        // Rebuild model and view
        static_cast<RC_TREE_MODEL*>( aEvent.GetModel() )->Update( m_markersProvider, getSeverities() );
        modified = true;
        break;
    }

    case ID_EDIT_SEVERITIES:
        m_frame->ShowBoardSetupDialog( _( "Violation Severity" ), this );
        break;
    }

    if( modified )
    {
        updateDisplayedCounts();
        refreshEditor();
        m_frame->OnModify();
    }
}


void DIALOG_DRC::OnIgnoredItemRClick( wxListEvent& event )
{
    int    errorCode = (int) event.m_item.GetData();
    wxMenu menu;

    menu.Append( RPT_SEVERITY_ERROR,   _( "Error" ),   wxEmptyString, wxITEM_RADIO );
    menu.Append( RPT_SEVERITY_WARNING, _( "Warning" ), wxEmptyString, wxITEM_RADIO );
    menu.Append( RPT_SEVERITY_IGNORE,  _( "Ignore" ),  wxEmptyString, wxITEM_RADIO );

    menu.Check( bds().GetSeverity( errorCode ), true );

    int severity = GetPopupMenuSelectionFromUser( menu );

    if( severity > 0 )
    {
        if( bds().m_DRCSeverities[ errorCode ] != severity )
        {
            bds().m_DRCSeverities[ errorCode ] = (SEVERITY) severity;

            updateDisplayedCounts();
            refreshEditor();
            m_frame->OnModify();
        }
    }
}


void DIALOG_DRC::OnEditViolationSeverities( wxHyperlinkEvent& aEvent )
{
    m_frame->ShowBoardSetupDialog( _( "Violation Severity" ), this );
}


void DIALOG_DRC::OnSeverity( wxCommandEvent& aEvent )
{
    if( aEvent.GetEventObject() == m_showAll )
    {
        m_showErrors->SetValue( true );
        m_showWarnings->SetValue( aEvent.IsChecked() );
        m_showExclusions->SetValue( aEvent.IsChecked() );
    }

    UpdateData();
}


void DIALOG_DRC::OnSaveReport( wxCommandEvent& aEvent )
{
    wxFileName fn( "DRC." + FILEEXT::ReportFileExtension );

    wxFileDialog dlg( this, _( "Save Report File" ), Prj().GetProjectPath(), fn.GetFullName(),
                      FILEEXT::ReportFileWildcard() + wxS( "|" ) + FILEEXT::JsonFileWildcard(),
                      wxFD_SAVE | wxFD_OVERWRITE_PROMPT );

    KIPLATFORM::UI::AllowNetworkFileSystems( &dlg );

    if( dlg.ShowModal() != wxID_OK )
        return;

    fn = dlg.GetPath();

    if( fn.GetExt().IsEmpty() )
        fn.SetExt( FILEEXT::ReportFileExtension );

    if( !fn.IsAbsolute() )
    {
        wxString prj_path = Prj().GetProjectPath();
        fn.MakeAbsolute( prj_path );
    }

    DRC_REPORT reportWriter( m_frame->GetBoard(), GetUserUnits(), m_markersProvider,
                             m_ratsnestProvider, m_fpWarningsProvider );

    bool success = false;
    if( fn.GetExt() == FILEEXT::JsonFileExtension )
        success = reportWriter.WriteJsonReport( fn.GetFullPath() );
    else
        success = reportWriter.WriteTextReport( fn.GetFullPath() );

    if( success )
        m_messages->Report( wxString::Format( _( "Report file '%s' created<br>" ), fn.GetFullPath() ) );
    else
        DisplayError( this, wxString::Format( _( "Failed to create file '%s'." ), fn.GetFullPath() ) );
}


void DIALOG_DRC::OnClose( wxCloseEvent& aEvent )
{
    wxCommandEvent dummy;
    OnCancelClick( dummy );
}


void DIALOG_DRC::OnCancelClick( wxCommandEvent& aEvent )
{
    if( m_running )
    {
        m_cancelled = true;
        return;
    }

    m_frame->ClearFocus();

    SetReturnCode( wxID_CANCEL );

    // The dialog can be modal or not modal.
    // Leave the DRC caller destroy (or not) the dialog
    DRC_TOOL* drcTool = m_frame->GetToolManager()->GetTool<DRC_TOOL>();
    drcTool->DestroyDRCDialog();
}


void DIALOG_DRC::OnChangingNotebookPage( wxNotebookEvent& aEvent )
{
    m_markerDataView->UnselectAll();
    m_unconnectedDataView->UnselectAll();
    m_footprintsDataView->UnselectAll();

    aEvent.Skip();
}


void DIALOG_DRC::refreshEditor()
{
    WINDOW_THAWER thawer( m_frame );

    m_frame->GetCanvas()->Refresh();
}


void DIALOG_DRC::PrevMarker()
{
    selectPage( PAGE_VIOLATIONS );

    if( m_Notebook->IsShown() )
    {
        switch( m_Notebook->GetSelection() )
        {
        case 0: m_markersTreeModel->PrevMarker();      break;
        case 1: m_unconnectedTreeModel->PrevMarker();  break;
        case 2: m_fpWarningsTreeModel->PrevMarker();   break;
        case 3:                                        break;
        }
    }
}


void DIALOG_DRC::NextMarker()
{
    selectPage( PAGE_VIOLATIONS );

    if( m_Notebook->IsShown() )
    {
        switch( m_Notebook->GetSelection() )
        {
        case 0: m_markersTreeModel->NextMarker();      break;
        case 1: m_unconnectedTreeModel->NextMarker();  break;
        case 2: m_fpWarningsTreeModel->NextMarker();   break;
        case 3:                                        break;
        }
    }
}


void DIALOG_DRC::SelectMarker( const PCB_MARKER* aMarker )
{
    selectPage( PAGE_VIOLATIONS );

    if( m_Notebook->IsShown() )
    {
        enum MARKER_BASE::MARKER_T markerType = aMarker->GetMarkerType();

        if( markerType == MARKER_BASE::MARKER_DRC )
            m_Notebook->SetSelection( 0 );
        else if( markerType == MARKER_BASE::MARKER_PARITY )
            m_Notebook->SetSelection( 2 );

        m_markersTreeModel->SelectMarker( aMarker );

        CallAfter(
                [this, aMarker]
                {
                    m_markersTreeModel->CenterMarker( aMarker );
                } );
    }
}


void DIALOG_DRC::ExcludeMarker()
{
    if( !m_Notebook->IsShown() || m_Notebook->GetSelection() != 0 )
        return;

    RC_TREE_NODE* node = RC_TREE_MODEL::ToNode( m_markerDataView->GetCurrentItem() );

    if( node && node->m_RcItem )
    {
        PCB_MARKER* marker = dynamic_cast<PCB_MARKER*>( node->m_RcItem->GetParent() );

        if( marker && marker->GetSeverity() != RPT_SEVERITY_EXCLUSION )
        {
            marker->SetExcluded( true );
            bds().m_DrcExclusions.insert( marker->SerializeToString() );
            m_frame->GetCanvas()->GetView()->Update( marker );

            // Update view
            if( m_showExclusions->GetValue() )
                m_markersTreeModel->ValueChanged( node );
            else
                m_markersTreeModel->DeleteCurrentItem( false );

            updateDisplayedCounts();
            refreshEditor();
            m_frame->OnModify();
        }
    }
}


void DIALOG_DRC::deleteAllMarkers( bool aIncludeExclusions )
{
    // Clear current selection list to avoid selection of deleted items
    Freeze();
    m_frame->GetToolManager()->RunAction( ACTIONS::selectionClear );

    m_markersTreeModel->DeleteItems( false, aIncludeExclusions, false );
    m_unconnectedTreeModel->DeleteItems( false, aIncludeExclusions, false );
    m_fpWarningsTreeModel->DeleteItems( false, aIncludeExclusions, false );

    m_frame->GetBoard()->DeleteMARKERs( true, aIncludeExclusions );
    Thaw();
}


void DIALOG_DRC::OnDeleteOneClick( wxCommandEvent& aEvent )
{
    if( m_Notebook->GetSelection() == 0 )
    {
        // Clear the selection.  It may be the selected DRC marker.
        m_frame->GetToolManager()->RunAction( ACTIONS::selectionClear );

        m_markersTreeModel->DeleteCurrentItem( true );

        // redraw the pcb
        refreshEditor();
    }
    else if( m_Notebook->GetSelection() == 1 )
    {
        m_unconnectedTreeModel->DeleteCurrentItem( true );
    }
    else if( m_Notebook->GetSelection() == 2 )
    {
        m_fpWarningsTreeModel->DeleteCurrentItem( true );
    }

    updateDisplayedCounts();
}


void DIALOG_DRC::OnDeleteAllClick( wxCommandEvent& aEvent )
{
    static bool s_includeExclusions = false;

    int  numExcluded = 0;

    if( m_markersProvider )
        numExcluded += m_markersProvider->GetCount( RPT_SEVERITY_EXCLUSION );

    if( m_ratsnestProvider )
        numExcluded += m_ratsnestProvider->GetCount( RPT_SEVERITY_EXCLUSION );

    if( m_fpWarningsProvider )
        numExcluded += m_fpWarningsProvider->GetCount( RPT_SEVERITY_EXCLUSION );

    if( numExcluded > 0 )
    {
        wxMessageDialog dlg( this, _( "Delete exclusions too?" ), _( "Delete All Markers" ),
                             wxYES_NO | wxCANCEL | wxCENTER | wxICON_QUESTION );
        dlg.SetYesNoLabels( _( "Errors and Warnings Only" ),
                            _( "Errors, Warnings and Exclusions" ) );

        int ret = dlg.ShowModal();

        if( ret == wxID_CANCEL )
            return;
        else if( ret == wxID_NO )
            s_includeExclusions = true;
    }

    deleteAllMarkers( s_includeExclusions );
    m_drcRun = false;

    refreshEditor();
    updateDisplayedCounts();
}


void DIALOG_DRC::updateDisplayedCounts()
{
    DRC_TOOL*   drcTool = m_frame->GetToolManager()->GetTool<DRC_TOOL>();
    DRC_ENGINE* drcEngine = drcTool->GetDRCEngine().get();

    // Collect counts:

    int numMarkers = 0;
    int numUnconnected = 0;
    int numFootprints = 0;

    int numErrors = 0;
    int numWarnings = 0;
    int numExcluded = 0;

    if( m_markersProvider )
    {
        numMarkers += m_markersProvider->GetCount();
        numErrors += m_markersProvider->GetCount( RPT_SEVERITY_ERROR );
        numWarnings += m_markersProvider->GetCount( RPT_SEVERITY_WARNING );
        numExcluded += m_markersProvider->GetCount( RPT_SEVERITY_EXCLUSION );
    }

    if( m_ratsnestProvider )
    {
        numUnconnected += m_ratsnestProvider->GetCount();
        numErrors += m_ratsnestProvider->GetCount( RPT_SEVERITY_ERROR );
        numWarnings += m_ratsnestProvider->GetCount( RPT_SEVERITY_WARNING );
        numExcluded += m_ratsnestProvider->GetCount( RPT_SEVERITY_EXCLUSION );
    }

    if( m_footprintTestsRun && m_fpWarningsProvider )
    {
        numFootprints += m_fpWarningsProvider->GetCount();
        numErrors += m_fpWarningsProvider->GetCount( RPT_SEVERITY_ERROR );
        numWarnings += m_fpWarningsProvider->GetCount( RPT_SEVERITY_WARNING );
        numExcluded += m_fpWarningsProvider->GetCount( RPT_SEVERITY_EXCLUSION );
    }

    bool errorsOverflowed = false;
    bool warningsOverflowed = false;
    bool markersOverflowed = false;
    bool unconnectedOverflowed = false;
    bool footprintsOverflowed = false;

    for( int ii = DRCE_FIRST; ii <= DRCE_LAST; ++ii )
    {
        const SEVERITY severity = bds().GetSeverity( ii );

        if( drcEngine->IsErrorLimitExceeded( ii ) )
        {
            if( severity == RPT_SEVERITY_ERROR )
                errorsOverflowed = true;
            else if( severity == RPT_SEVERITY_WARNING )
                warningsOverflowed = true;

            if( ii == DRCE_UNCONNECTED_ITEMS )
            {
                if( m_showWarnings->GetValue() && severity == RPT_SEVERITY_WARNING )
                    unconnectedOverflowed = true;
                else if( m_showErrors->GetValue() && severity == RPT_SEVERITY_ERROR )
                    unconnectedOverflowed = true;
            }
            else if(    ii == DRCE_MISSING_FOOTPRINT
                     || ii == DRCE_DUPLICATE_FOOTPRINT
                     || ii == DRCE_EXTRA_FOOTPRINT
                     || ii == DRCE_NET_CONFLICT
                     || ii == DRCE_SCHEMATIC_PARITY
                     || ii == DRCE_FOOTPRINT_FILTERS
                     || ii == DRCE_SCHEMATIC_FIELDS_PARITY )
            {
                if( m_showWarnings->GetValue() && severity == RPT_SEVERITY_WARNING )
                    footprintsOverflowed = true;
                else if( m_showErrors->GetValue() && severity == RPT_SEVERITY_ERROR )
                    footprintsOverflowed = true;
            }
            else
            {
                if( m_showWarnings->GetValue() && severity == RPT_SEVERITY_WARNING )
                    markersOverflowed = true;
                else if( m_showErrors->GetValue() && severity == RPT_SEVERITY_ERROR )
                    markersOverflowed = true;
            }
        }
    }

    wxString msg;
    wxString num;

    // Update tab headers:

    if( m_drcRun )
    {
        num.Printf( markersOverflowed ? wxT( "%d+" ) : wxT( "%d" ), numMarkers );
        msg.Printf( m_markersTitleTemplate, num );
    }
    else
    {
        msg = m_markersTitleTemplate;
        msg.Replace( wxT( "(%s)" ), wxEmptyString );
    }

    m_Notebook->SetPageText( 0, msg );

    if( m_drcRun )
    {
        num.Printf( unconnectedOverflowed ? wxT( "%d+" ) : wxT( "%d" ), numUnconnected );
        msg.sprintf( m_unconnectedTitleTemplate, num );
    }
    else
    {
        msg = m_unconnectedTitleTemplate;
        msg.Replace( wxT( "(%s)" ), wxEmptyString );
    }

    m_Notebook->SetPageText( 1, msg );

    if( m_footprintTestsRun )
    {
        num.Printf( footprintsOverflowed ? wxT( "%d+" ) : wxT( "%d" ), numFootprints );
        msg.sprintf( m_footprintsTitleTemplate, num );
    }
    else if( m_drcRun )
    {
        msg = m_footprintsTitleTemplate;
        msg.Replace( wxT( "%s" ), _( "not run" ) );
    }
    else
    {
        msg = m_footprintsTitleTemplate;
        msg.Replace( wxT( "(%s)" ), wxEmptyString );
    }

    m_Notebook->SetPageText( 2, msg );

    if( m_drcRun )
    {
        num.Printf( wxT( "%d" ), m_ignoredList->GetItemCount() );
        msg.sprintf( m_ignoredTitleTemplate, num );
    }
    else
    {
        msg = m_ignoredTitleTemplate;
        msg.Replace( wxT( "(%s)" ), wxEmptyString );
    }

    m_Notebook->SetPageText( 3, msg );

    // Update badges:

    if( !m_drcRun && numErrors == 0 )
        numErrors = -1;

    if( !m_drcRun && numWarnings == 0 )
        numWarnings = -1;

    m_errorsBadge->SetMaximumNumber( numErrors );
    m_errorsBadge->UpdateNumber( errorsOverflowed ? numErrors + 1 : numErrors, RPT_SEVERITY_ERROR );

    m_warningsBadge->SetMaximumNumber( numWarnings );
    m_warningsBadge->UpdateNumber( warningsOverflowed ? numWarnings + 1 : numWarnings, RPT_SEVERITY_WARNING );

    m_exclusionsBadge->SetMaximumNumber( numExcluded );
    m_exclusionsBadge->UpdateNumber( numExcluded, RPT_SEVERITY_EXCLUSION );
}


namespace
{
/// Which page (and rule category) a node of the Design Rule Checker tree shows
struct DRC_TREE_DATA : public wxTreeItemData
{
    DRC_TREE_DATA( int aPage, int aCategory ) : page( aPage ), category( aCategory ) {}

    int page;
    int category;
};


enum RULE_COLUMN
{
    RULE_COL_NAME = 0,
    RULE_COL_CATEGORY,
    RULE_COL_BATCH,
    RULE_COL_SEVERITY
};


/// A check box image for the rules list (native boxes stay light in dark mode)
wxBitmap ruleCheckBitmap( wxWindow* aWindow, bool aChecked )
{
    int      size = aWindow->FromDIP( 16 );
    wxBitmap bmp( size, size, 32 );

    {
        wxMemoryDC dc( bmp );
        dc.SetBackground( wxBrush( wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOW ) ) );
        dc.Clear();

        wxGCDC   gc( dc );
        wxColour fg = wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOWTEXT );
        bool     dark = wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOW ).GetLuminance() < 0.5;
        int      box = aWindow->FromDIP( 12 );
        int      o = ( size - box ) / 2;

        gc.SetPen( wxPen( dark ? wxColour( 0xB0, 0xB0, 0xB0 ) : wxColour( 0x60, 0x60, 0x60 ) ) );
        gc.SetBrush( *wxTRANSPARENT_BRUSH );
        gc.DrawRectangle( o, o, box, box );

        if( aChecked )
        {
            gc.SetPen( wxPen( fg, std::max( 2, box / 6 ) ) );
            wxPoint pts[3] = { wxPoint( o + box * 2 / 10, o + box / 2 ),
                               wxPoint( o + box * 4 / 10, o + box * 7 / 10 ),
                               wxPoint( o + box * 8 / 10, o + box * 3 / 10 ) };
            gc.DrawLines( 3, pts );
        }
    }

    return bmp;
}


wxString severityName( SEVERITY aSeverity )
{
    switch( aSeverity )
    {
    case RPT_SEVERITY_ERROR:   return _( "Error" );
    case RPT_SEVERITY_WARNING: return _( "Warning" );
    case RPT_SEVERITY_IGNORE:  return _( "Ignore" );
    default:                   return wxEmptyString;
    }
}
} // namespace


void DIALOG_DRC::buildAltiumLayout()
{
    DIALOG_DRC_BASE::SetTitle( wxString::Format( _( "Design Rule Checker [%s]" ),
                                EDA_UNIT_UTILS::GetLabel( m_frame->GetUserUnits() ) ) );

    // Take the generated controls out of their sizers (child sizers are deleted, windows kept)
    // and lay them out again: a tree at the left, the selected page at the right
    GetSizer()->Clear( false );

    int margin = FromDIP( 8 );

    auto pageTitle =
            [&]( wxWindow* aPage, wxSizer* aSizer, const wxString& aTitle ) -> wxStaticText*
            {
                wxStaticText* title = new wxStaticText( aPage, wxID_ANY, aTitle );
                title->SetFont( title->GetFont().Bold() );
                aSizer->Add( title, 0, wxLEFT | wxRIGHT | wxTOP, margin );
                aSizer->Add( new wxStaticLine( aPage ), 0, wxEXPAND | wxALL, margin );
                return title;
            };

    m_tree = new wxTreeCtrl( this, wxID_ANY, wxDefaultPosition, FromDIP( wxSize( 220, -1 ) ),
                             wxTR_HAS_BUTTONS | wxTR_HIDE_ROOT | wxTR_SINGLE | wxTR_NO_LINES
                                     | wxTR_FULL_ROW_HIGHLIGHT | wxBORDER_SIMPLE );
    m_tree->SetBackgroundColour( wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOW ) );
    m_pages = new wxSimplebook( this, wxID_ANY );

    // Report Options
    wxPanel*    report = new wxPanel( m_pages, wxID_ANY );
    wxBoxSizer* reportSizer = new wxBoxSizer( wxVERTICAL );
    pageTitle( report, reportSizer, _( "DRC Report Options" ) );

    auto option =
            [&]( wxWindow* aCheck )
            {
                reportSizer->Add( aCheck, 0, wxLEFT | wxRIGHT | wxBOTTOM, margin );
            };

    m_cbCreateReport = new wxCheckBox( report, wxID_ANY, _( "Create Report File" ) );
    m_cbCreateReport->SetValue( true );
    m_cbCreateReport->SetToolTip( _( "Write the violations to a report file next to the board" ) );
    option( m_cbCreateReport );

    m_cbRefillZones->Reparent( report );
    option( m_cbRefillZones );

    m_cbTestFootprints->Reparent( report );
    option( m_cbTestFootprints );

    m_cbAllTrackErrors = new wxCheckBox( report, wxID_ANY, _( "Report All Errors for Each Track" ) );
    m_cbAllTrackErrors->SetToolTip( _( "If unchecked, only the first error will be reported for each track" ) );
    m_cbAllTrackErrors->SetValue( m_report_all_track_errors );
    option( m_cbAllTrackErrors );

    reportSizer->AddSpacer( margin );
    pageTitle( report, reportSizer, _( "Violations List" ) );

    m_cbCrossprobe = new wxCheckBox( report, wxID_ANY, _( "Cross-probe Selected Items" ) );
    m_cbCrossprobe->SetToolTip( _( "Highlight corresponding items on canvas when selected in the DRC list" ) );
    m_cbCrossprobe->SetValue( m_crossprobe );
    option( m_cbCrossprobe );

    m_cbCenterCrossprobe = new wxCheckBox( report, wxID_ANY, _( "Center on Cross-probe" ) );
    m_cbCenterCrossprobe->SetToolTip( _( "When cross-probing, scroll the canvas so that the item is visible" ) );
    m_cbCenterCrossprobe->SetValue( m_scroll_on_crossprobe );
    option( m_cbCenterCrossprobe );

    // These follow the PCB editor settings, not the dialog's saved control state
    for( wxCheckBox* cb : { m_cbAllTrackErrors, m_cbCrossprobe, m_cbCenterCrossprobe } )
        OptOut( cb );

    m_cbAllTrackErrors->Bind( wxEVT_CHECKBOX,
                              [this]( wxCommandEvent& )
                              {
                                  m_report_all_track_errors = m_cbAllTrackErrors->GetValue();
                              } );
    m_cbCrossprobe->Bind( wxEVT_CHECKBOX,
                          [this]( wxCommandEvent& )
                          {
                              m_crossprobe = m_cbCrossprobe->GetValue();
                          } );
    m_cbCenterCrossprobe->Bind( wxEVT_CHECKBOX,
                                [this]( wxCommandEvent& )
                                {
                                    m_scroll_on_crossprobe = m_cbCenterCrossprobe->GetValue();
                                } );

    report->SetSizer( reportSizer );
    m_pages->AddPage( report, wxEmptyString );

    m_bMenu->Hide();    // its options are on the Report Options page

    // Rules To Check
    wxPanel*    rules = new wxPanel( m_pages, wxID_ANY );
    wxBoxSizer* rulesSizer = new wxBoxSizer( wxVERTICAL );
    m_rulesTitle = pageTitle( rules, rulesSizer, _( "Rules To Check" ) );

    m_rulesList = new wxListCtrl( rules, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                  wxLC_REPORT | wxBORDER_SIMPLE );
    m_rulesList->SetBackgroundColour( wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOW ) );
    m_rulesList->InsertColumn( RULE_COL_NAME, _( "Rule" ), wxLIST_FORMAT_LEFT, FromDIP( 340 ) );
    m_rulesList->InsertColumn( RULE_COL_CATEGORY, _( "Category" ), wxLIST_FORMAT_LEFT, FromDIP( 170 ) );
    m_rulesList->InsertColumn( RULE_COL_BATCH, _( "Batch" ), wxLIST_FORMAT_CENTER, FromDIP( 56 ) );
    m_rulesList->InsertColumn( RULE_COL_SEVERITY, _( "Severity" ), wxLIST_FORMAT_LEFT, FromDIP( 90 ) );

    wxImageList* images = new wxImageList( FromDIP( 16 ), FromDIP( 16 ), false, 2 );
    images->Add( ruleCheckBitmap( this, false ) );
    images->Add( ruleCheckBitmap( this, true ) );
    m_rulesList->AssignImageList( images, wxIMAGE_LIST_SMALL );

    rulesSizer->Add( m_rulesList, 1, wxEXPAND | wxLEFT | wxRIGHT, margin );

    wxStaticText* rulesHint = new wxStaticText( rules, wxID_ANY,
            _( "Click Batch to include or skip a check, Severity (or right-click) to change it.  "
               "The settings are saved with the board." ) );
    rulesHint->SetForegroundColour( wxSystemSettings::GetColour( wxSYS_COLOUR_GRAYTEXT ) );
    rulesSizer->Add( rulesHint, 0, wxALL, margin );

    rules->SetSizer( rulesSizer );
    m_pages->AddPage( rules, wxEmptyString );

    m_rulesList->Bind( wxEVT_LEFT_DOWN, &DIALOG_DRC::onRulesClick, this );
    m_rulesList->Bind( wxEVT_LIST_ITEM_RIGHT_CLICK, &DIALOG_DRC::onRulesMenu, this );

    // Violations: KiCad's result lists (Altium lists them in the Messages panel)
    wxPanel*    violations = new wxPanel( m_pages, wxID_ANY );
    wxBoxSizer* violationsSizer = new wxBoxSizer( wxVERTICAL );
    pageTitle( violations, violationsSizer, _( "Violations" ) );

    m_runningResultsBook->Reparent( violations );
    violationsSizer->Add( m_runningResultsBook, 1, wxEXPAND );

    wxBoxSizer* severityRow = new wxBoxSizer( wxHORIZONTAL );

    for( wxWindow* win : std::initializer_list<wxWindow*>{ m_showLabel, m_showAll, m_showErrors,
                                                           m_errorsBadge, m_showWarnings,
                                                           m_warningsBadge, m_showExclusions,
                                                           m_exclusionsBadge } )
    {
        win->Reparent( violations );
        severityRow->Add( win, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP( 6 ) );
    }

    severityRow->AddStretchSpacer();

    for( wxWindow* win : std::initializer_list<wxWindow*>{ m_DeleteCurrentMarkerButton,
                                                           m_DeleteAllMarkersButton, m_saveReport } )
    {
        win->Reparent( violations );
        severityRow->Add( win, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP( 6 ) );
    }

    violationsSizer->Add( severityRow, 0, wxEXPAND | wxALL, margin );

    if( m_drcStatusBar )
    {
        m_drcStatusBar->Reparent( violations );
        violationsSizer->Add( m_drcStatusBar, 0, wxEXPAND );
    }

    violations->SetSizer( violationsSizer );
    m_pages->AddPage( violations, wxEmptyString );

    // The rules, grouped by KiCad's check categories
    m_rules.clear();
    m_ruleCategories.clear();

    for( const RC_ITEM& item : DRC_ITEM::GetItemsWithSeverities() )
    {
        if( item.GetErrorCode() == 0 )
        {
            m_ruleCategories.push_back( item.GetErrorText( true ) );
            continue;
        }

        m_rules.emplace_back( item.GetErrorCode(), std::max( 0, (int) m_ruleCategories.size() - 1 ) );
    }

    // Tree
    wxTreeItemId root = m_tree->AddRoot( wxS( "root" ) );
    wxTreeItemId reportNode = m_tree->AppendItem( root, _( "Report Options" ), -1, -1,
                                                  new DRC_TREE_DATA( PAGE_REPORT_OPTIONS, -1 ) );
    wxTreeItemId rulesNode = m_tree->AppendItem( root, _( "Rules To Check" ), -1, -1,
                                                 new DRC_TREE_DATA( PAGE_RULES, -1 ) );

    for( int c = 0; c < (int) m_ruleCategories.size(); ++c )
    {
        if( !m_ruleCategories[c].IsEmpty() )
        {
            m_tree->AppendItem( rulesNode, m_ruleCategories[c], -1, -1,
                                new DRC_TREE_DATA( PAGE_RULES, c ) );
        }
    }

    m_tree->AppendItem( root, _( "Violations" ), -1, -1, new DRC_TREE_DATA( PAGE_VIOLATIONS, -1 ) );
    m_tree->Expand( rulesNode );
    m_tree->SelectItem( reportNode );
    m_tree->Bind( wxEVT_TREE_SEL_CHANGED, &DIALOG_DRC::onTreeSelection, this );

    // Dialog: tree | page, then Run Design Rule Check... and Close
    wxBoxSizer* top = new wxBoxSizer( wxVERTICAL );
    wxBoxSizer* body = new wxBoxSizer( wxHORIZONTAL );
    body->Add( m_tree, 0, wxEXPAND | wxLEFT | wxTOP | wxBOTTOM, margin );
    body->Add( m_pages, 1, wxEXPAND | wxALL, margin );
    top->Add( body, 1, wxEXPAND );

    wxBoxSizer*             buttons = new wxBoxSizer( wxHORIZONTAL );
    wxStdDialogButtonSizer* sdb = new wxStdDialogButtonSizer();
    sdb->AddButton( m_sdbSizerCancel );
    sdb->Realize();

    buttons->Add( m_sdbSizerOK, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP( 5 ) );
    buttons->AddStretchSpacer();
    buttons->Add( sdb, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP( 5 ) );
    top->Add( buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP( 6 ) );

    SetSizer( top );
    top->SetSizeHints( this );

    m_pages->ChangeSelection( PAGE_REPORT_OPTIONS );
    fillRulesList();
}


void DIALOG_DRC::selectPage( int aPage, int aCategory )
{
    if( !m_pages )
        return;

    m_pages->ChangeSelection( aPage );

    if( aPage == PAGE_RULES )
    {
        m_rulesCategory = aCategory;

        if( m_rulesTitle )
        {
            if( aCategory >= 0 && aCategory < (int) m_ruleCategories.size() )
            {
                m_rulesTitle->SetLabel( wxString::Format( _( "Rules To Check - %s" ),
                                                          m_ruleCategories[aCategory] ) );
            }
            else
            {
                m_rulesTitle->SetLabel( _( "Rules To Check" ) );
            }
        }

        fillRulesList();
    }

    // Keep the tree in step when the page is changed from code (e.g. running the checks)
    if( m_tree && !m_syncingTree )
    {
        std::function<wxTreeItemId( wxTreeItemId )> find =
                [&]( wxTreeItemId aParent ) -> wxTreeItemId
                {
                    wxTreeItemIdValue cookie;

                    for( wxTreeItemId it = m_tree->GetFirstChild( aParent, cookie ); it.IsOk();
                         it = m_tree->GetNextChild( aParent, cookie ) )
                    {
                        auto* d = static_cast<DRC_TREE_DATA*>( m_tree->GetItemData( it ) );

                        if( d && d->page == aPage && ( aPage != PAGE_RULES || d->category == aCategory ) )
                            return it;

                        if( wxTreeItemId sub = find( it ); sub.IsOk() )
                            return sub;
                    }

                    return wxTreeItemId();
                };

        wxTreeItemId item = find( m_tree->GetRootItem() );

        if( item.IsOk() && item != m_tree->GetSelection() )
        {
            m_syncingTree = true;
            m_tree->SelectItem( item );
            m_syncingTree = false;
        }
    }
}


void DIALOG_DRC::onTreeSelection( wxTreeEvent& aEvent )
{
    if( m_syncingTree )
        return;

    wxTreeItemId item = aEvent.GetItem();

    if( !item.IsOk() )
        return;

    if( auto* data = static_cast<DRC_TREE_DATA*>( m_tree->GetItemData( item ) ) )
    {
        m_syncingTree = true;
        selectPage( data->page, data->category );
        m_syncingTree = false;
    }
}


void DIALOG_DRC::fillRulesList()
{
    if( !m_rulesList )
        return;

    m_rulesList->Freeze();
    m_rulesList->DeleteAllItems();
    m_shownRules.clear();

    wxColour dim = wxSystemSettings::GetColour( wxSYS_COLOUR_GRAYTEXT );
    wxColour fg = wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOWTEXT );

    for( int idx = 0; idx < (int) m_rules.size(); ++idx )
    {
        auto [code, category] = m_rules[idx];

        if( m_rulesCategory >= 0 && category != m_rulesCategory )
            continue;

        std::shared_ptr<DRC_ITEM> item = DRC_ITEM::Create( code );

        if( !item )
            continue;

        SEVERITY severity = bds().GetSeverity( code );
        long     row = m_rulesList->InsertItem( m_rulesList->GetItemCount(),
                                                item->GetErrorText( true ), -1 );

        m_rulesList->SetItem( row, RULE_COL_CATEGORY,
                              category < (int) m_ruleCategories.size() ? m_ruleCategories[category]
                                                                       : wxString() );
        m_rulesList->SetItem( row, RULE_COL_BATCH, wxEmptyString );
        m_rulesList->SetItemColumnImage( row, RULE_COL_BATCH, severity == RPT_SEVERITY_IGNORE ? 0 : 1 );
        m_rulesList->SetItem( row, RULE_COL_SEVERITY, severityName( severity ) );
        m_rulesList->SetItemTextColour( row, severity == RPT_SEVERITY_IGNORE ? dim : fg );
        m_shownRules.push_back( idx );
    }

    m_rulesList->Thaw();
}


void DIALOG_DRC::setRuleSeverity( const std::vector<long>& aRows, SEVERITY aSeverity )
{
    bool changed = false;

    wxColour dim = wxSystemSettings::GetColour( wxSYS_COLOUR_GRAYTEXT );
    wxColour fg = wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOWTEXT );

    for( long row : aRows )
    {
        if( row < 0 || row >= (long) m_shownRules.size() )
            continue;

        int code = m_rules[m_shownRules[row]].first;

        if( bds().GetSeverity( code ) == aSeverity )
            continue;

        bds().m_DRCSeverities[code] = aSeverity;
        changed = true;

        m_rulesList->SetItemColumnImage( row, RULE_COL_BATCH, aSeverity == RPT_SEVERITY_IGNORE ? 0 : 1 );
        m_rulesList->SetItem( row, RULE_COL_SEVERITY, severityName( aSeverity ) );
        m_rulesList->SetItemTextColour( row, aSeverity == RPT_SEVERITY_IGNORE ? dim : fg );
    }

    if( changed )
        m_frame->OnModify();
}


void DIALOG_DRC::onRulesClick( wxMouseEvent& aEvent )
{
    aEvent.Skip();   // keep the list's own selection handling

    int  flags = 0;
    long column = -1;
    long row = m_rulesList->HitTest( aEvent.GetPosition(), flags, &column );

    if( row < 0 || row >= (long) m_shownRules.size() )
        return;

    int      code = m_rules[m_shownRules[row]].first;
    SEVERITY current = bds().GetSeverity( code );

    if( column == RULE_COL_BATCH )
    {
        setRuleSeverity( { row }, current == RPT_SEVERITY_IGNORE ? RPT_SEVERITY_ERROR
                                                                 : RPT_SEVERITY_IGNORE );
    }
    else if( column == RULE_COL_SEVERITY )
    {
        CallAfter(
                [this, row]()
                {
                    if( row >= (long) m_shownRules.size() )
                        return;

                    wxMenu menu;
                    menu.AppendRadioItem( 1, _( "Error" ) );
                    menu.AppendRadioItem( 2, _( "Warning" ) );
                    menu.AppendRadioItem( 3, _( "Ignore" ) );

                    SEVERITY sev = bds().GetSeverity( m_rules[m_shownRules[row]].first );
                    menu.Check( sev == RPT_SEVERITY_ERROR ? 1 : sev == RPT_SEVERITY_WARNING ? 2 : 3, true );

                    switch( m_rulesList->GetPopupMenuSelectionFromUser( menu ) )
                    {
                    case 1: setRuleSeverity( { row }, RPT_SEVERITY_ERROR );   break;
                    case 2: setRuleSeverity( { row }, RPT_SEVERITY_WARNING ); break;
                    case 3: setRuleSeverity( { row }, RPT_SEVERITY_IGNORE );  break;
                    default: break;
                    }
                } );
    }
}


void DIALOG_DRC::onRulesMenu( wxListEvent& aEvent )
{
    std::vector<long> rows;

    for( long row = m_rulesList->GetNextItem( -1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED ); row >= 0;
         row = m_rulesList->GetNextItem( row, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED ) )
    {
        rows.push_back( row );
    }

    if( rows.empty() )
        return;

    wxMenu menu;
    menu.Append( 1, _( "Error" ) );
    menu.Append( 2, _( "Warning" ) );
    menu.Append( 3, _( "Ignore" ) );

    switch( m_rulesList->GetPopupMenuSelectionFromUser( menu ) )
    {
    case 1: setRuleSeverity( rows, RPT_SEVERITY_ERROR );   break;
    case 2: setRuleSeverity( rows, RPT_SEVERITY_WARNING ); break;
    case 3: setRuleSeverity( rows, RPT_SEVERITY_IGNORE );  break;
    default: break;
    }
}


void DIALOG_DRC::writeAltiumReport()
{
    wxFileName fn( m_frame->GetBoard()->GetFileName() );

    if( !fn.IsOk() || fn.GetName().IsEmpty() )
        return;

    fn.SetName( fn.GetName() + wxS( "-DRC" ) );
    fn.SetExt( FILEEXT::ReportFileExtension );

    DRC_REPORT reportWriter( m_frame->GetBoard(), GetUserUnits(), m_markersProvider,
                             m_ratsnestProvider, m_fpWarningsProvider );

    if( reportWriter.WriteTextReport( fn.GetFullPath() ) )
        m_messages->Report( wxString::Format( _( "Report file '%s' created<br>" ), fn.GetFullPath() ) );
    else
        m_messages->Report( wxString::Format( _( "Failed to create file '%s'.<br>" ), fn.GetFullPath() ) );
}


void DIALOG_DRC::showMessagesPanel()
{
    wxAuiManager* mgr = wxAuiManager::GetManager( m_frame );

    if( !mgr )
        return;

    wxAuiPaneInfo& pane = mgr->GetPane( MESSAGES_PANEL::PaneName() );

    if( !pane.IsOk() )      // GetPane() returns a shared null pane: never write to it
        return;

    if( !pane.IsShown() )
    {
        pane.Show();
        mgr->Update();
    }

    if( MESSAGES_PANEL* panel = dynamic_cast<MESSAGES_PANEL*>( pane.window ) )
        panel->RefreshMarkers();
}
