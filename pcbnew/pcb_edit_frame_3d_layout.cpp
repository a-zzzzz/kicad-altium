/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
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
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

/**
 * Altium-style 3D layout mode (docs/altium-parity/3d-layout-mode.md): the centre pane
 * ("DrawFrame") shows the board in 3D in place of the 2D canvas.  The pane keeps its name and
 * place, only its window is swapped, so the saved AUI layout is the same in either mode.
 */

#include <pcb_edit_frame.h>
#include <pcb_draw_panel_gal.h>
#include <3d_viewer/panel_3d_layout.h>
#include <tool/actions.h>
#include <tool/tool_manager.h>
#include <tools/pcb_actions.h>
#include <widgets/appearance_controls.h>


void PCB_EDIT_FRAME::Set3DLayoutMode( bool a3D )
{
    if( a3D == m_layout3DMode )
        return;

    wxAuiPaneInfo& pane = m_auimgr.GetPane( wxS( "DrawFrame" ) );

    if( !pane.IsOk() )
        return;

    if( a3D )
    {
        if( !m_layout3DPanel )
        {
            m_layout3DPanel = new PANEL_3D_LAYOUT( this, this );
            m_layout3DPanel->Hide();

            // Keys the 3D view hands back to the editor: 2D mode, save, undo and redo
            m_layout3DPanel->SetKeyHandler(
                    [this]( int aHotKey )
                    {
                        // Ctrl+F (View > Flip Board) flips the 3D view, as in Altium
                        if( aHotKey == PCB_ACTIONS::flipBoard.GetHotKey()
                                || aHotKey == PCB_ACTIONS::flipBoard.GetHotKeyAlt() )
                        {
                            CallAfter( [this]()
                                       {
                                           if( m_layout3DPanel )
                                               m_layout3DPanel->FlipView();
                                       } );
                            return true;
                        }

                        for( const TOOL_ACTION* action : { &PCB_ACTIONS::layoutMode2D,
                                                           &ACTIONS::save, &ACTIONS::undo,
                                                           &ACTIONS::redo } )
                        {
                            if( aHotKey == action->GetHotKey()
                                    || aHotKey == action->GetHotKeyAlt() )
                            {
                                CallAfter( [this, action]()
                                           {
                                               m_toolManager->RunAction( *action );
                                           } );
                                return true;
                            }
                        }

                        return false;
                    } );
        }

        // Show the board as it is now
        m_layout3DPanel->ReloadBoard();

        // The Active Bar is a child of the 2D canvas, so it goes with it
        GetCanvas()->Hide();
        pane.Window( m_layout3DPanel );
        m_layout3DPanel->Show();
        m_layout3DMode = true;
        m_auimgr.Update();

        m_layout3DPanel->FocusCanvas();
        m_layout3DPanel->UpdateStatus();

        // View Configuration shows Altium's 3D options
        if( m_appearancePanel )
            m_appearancePanel->Set3DMode( true, m_layout3DPanel );
    }
    else
    {
        m_layout3DPanel->Hide();
        SetStatusText( wxEmptyString, 0 );
        pane.Window( GetCanvas() );
        GetCanvas()->Show();
        m_layout3DMode = false;
        m_auimgr.Update();

        GetCanvas()->ForceRefresh();
        GetCanvas()->SetFocus();

        if( m_appearancePanel )
            m_appearancePanel->Set3DMode( false, nullptr );
    }
}


void PCB_EDIT_FRAME::PushTool( const TOOL_EVENT& aEvent )
{
    if( m_layout3DMode )
        Set3DLayoutMode( false );

    PCB_BASE_EDIT_FRAME::PushTool( aEvent );
}


bool PCB_EDIT_FRAME::Route3DLayoutViewAction( const TOOL_EVENT& aEvent )
{
    if( !m_layout3DMode || !m_layout3DPanel )
        return false;

    // View > Flip Board flips the 3D view (the 2D view keeps its own side)
    if( aEvent.IsAction( &PCB_ACTIONS::flipBoard ) )
    {
        m_layout3DPanel->FlipView();
        return true;
    }

    for( const TOOL_ACTION* action : { &ACTIONS::zoomIn, &ACTIONS::zoomOut, &ACTIONS::zoomInCenter,
                                       &ACTIONS::zoomOutCenter, &ACTIONS::zoomFitScreen,
                                       &ACTIONS::zoomFitObjects, &ACTIONS::zoomFitSelection,
                                       &ACTIONS::zoomRedraw } )
    {
        if( aEvent.IsAction( action ) )
            return m_layout3DPanel->RunViewAction( *action );
    }

    return false;
}


void PCB_EDIT_FRAME::Update3DView( bool aMarkDirty, bool aRefresh, const wxString* aTitle )
{
    PCB_BASE_FRAME::Update3DView( aMarkDirty, aRefresh, aTitle );

    // Edits, undo / redo and reloads all come through here (OnModify, HardRedraw)
    if( m_layout3DMode && m_layout3DPanel && aMarkDirty )
        m_layout3DPanel->ScheduleReload();
}
