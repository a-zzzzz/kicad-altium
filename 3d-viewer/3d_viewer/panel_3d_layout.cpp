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

#include <3d_rendering/opengl/render_3d_opengl.h> // Must be included before any GL header

#include "panel_3d_layout.h"

#include <wx/sizer.h>

#include <3d_canvas/eda_3d_canvas.h>
#include <common_ogl/ogl_attr_list.h>
#include <eda_3d_viewer_settings.h>
#include <class_draw_panel_gal.h>
#include <pcb_base_frame.h>
#include <pgm_base.h>
#include <project_pcb.h>
#include <settings/common_settings.h>
#include <settings/settings_manager.h>
#include <tool/actions.h>
#include <tool/tool_dispatcher.h>
#include <tool/tool_event.h>
#include <tool/tool_manager.h>
#include <tools/eda_3d_actions.h>
#include <tools/eda_3d_controller.h>
#include <settings/color_settings.h>
#include <layer_ids.h>
#include <algorithm>


PANEL_3D_LAYOUT::PANEL_3D_LAYOUT( wxWindow* aParent, PCB_BASE_FRAME* aFrame ) :
        wxPanel( aParent, wxID_ANY ),
        m_frame( aFrame ),
        m_canvas( nullptr ),
        m_currentCamera( m_trackBallCamera ),
        m_trackBallCamera( 2 * RANGE_SCALE_3D ),
        m_reloadPending( false ),
        m_statusPending( false )
{
    EDA_3D_VIEWER_SETTINGS* cfg = GetAppSettings<EDA_3D_VIEWER_SETTINGS>( "3d_viewer" );
    ANTIALIASING_MODE       aaMode = ANTIALIASING_MODE::AA_NONE;

    if( cfg )
        aaMode = static_cast<ANTIALIASING_MODE>( cfg->m_Render.opengl_AA_mode );

    m_canvas = new EDA_3D_CANVAS( this, OGL_ATT_LIST::GetAttributesList( aaMode, true ),
                                  m_boardAdapter, m_currentCamera,
                                  PROJECT_PCB::Get3DCacheManager( &aFrame->Prj() ) );

    // Same settings as the 3D viewer window, so the two look and navigate alike
    m_boardAdapter.m_MousewheelPanning =
            Pgm().GetCommonSettings()->m_Input.scroll_modifier_zoom != 0;

    if( cfg )
    {
        m_boardAdapter.m_Cfg = cfg;
        m_canvas->SetAnimationEnabled( cfg->m_Camera.animation_enabled );
        m_canvas->SetMovingSpeedMultiplier( cfg->m_Camera.moving_speed_multiplier );
        m_canvas->SetProjectionMode( cfg->m_Camera.projection_mode );
    }

    m_canvas->SetVcSettings( EDA_DRAW_PANEL_GAL::GetVcSettings() );
    m_canvas->SetInfoBar( aFrame->GetInfoBar() );

    m_boardAdapter.SetBoard( aFrame->GetBoard() );
    m_boardAdapter.m_IsBoardView = true;

    // Altium's 3D layout: layers follow the editor's View Configuration, Altium mouse controls
    m_boardAdapter.m_IsLayoutView = true;
    m_canvas->SetAltiumNavigation( true );

    // The rotation sphere shows while Shift is held, even before the mouse moves
    m_canvas->Bind( wxEVT_KEY_DOWN,
                    [this]( wxKeyEvent& aEvent )
                    {
                        if( aEvent.GetKeyCode() == WXK_SHIFT )
                            m_canvas->ShowRotationSphere( true );

                        aEvent.Skip();
                    } );
    m_canvas->Bind( wxEVT_KEY_UP,
                    [this]( wxKeyEvent& aEvent )
                    {
                        if( aEvent.GetKeyCode() == WXK_SHIFT )
                            m_canvas->ShowRotationSphere( false );

                        aEvent.Skip();
                    } );
    m_canvas->Bind( wxEVT_LEAVE_WINDOW,
                    [this]( wxMouseEvent& aEvent )
                    {
                        m_canvas->ShowRotationSphere( false );
                        aEvent.Skip();
                    } );

    // The 3D viewer's navigation tool, always running
    m_toolManager = new TOOL_MANAGER;
    m_toolManager->SetEnvironment( nullptr, nullptr, nullptr, cfg, this );

    m_actions = new EDA_3D_ACTIONS();
    m_toolDispatcher = new TOOL_DISPATCHER( m_toolManager );
    m_canvas->SetEventDispatcher( m_toolDispatcher );

    m_toolManager->RegisterTool( new EDA_3D_CONTROLLER );
    m_toolManager->InitTools();

    if( EDA_3D_CONTROLLER* ctrlTool = m_toolManager->GetTool<EDA_3D_CONTROLLER>() )
        ctrlTool->SetRotationIncrement( cfg ? cfg->m_Camera.rotation_increment : 10.0 );

    m_toolManager->InvokeTool( "3DViewer.Control" );

    // Bound after the canvas' own handlers, so it sees each key first
    m_canvas->Bind( wxEVT_CHAR_HOOK, &PANEL_3D_LAYOUT::onCanvasCharHook, this );

    // Navigation changes the camera: show it in the editor's status bar
    m_canvas->Bind( wxEVT_MOTION, &PANEL_3D_LAYOUT::onCanvasMouse, this );
    m_canvas->Bind( wxEVT_MOUSEWHEEL, &PANEL_3D_LAYOUT::onCanvasMouse, this );
    m_canvas->Bind( wxEVT_LEFT_UP, &PANEL_3D_LAYOUT::onCanvasMouse, this );
    m_canvas->Bind( wxEVT_MIDDLE_UP, &PANEL_3D_LAYOUT::onCanvasMouse, this );
    m_canvas->Bind( wxEVT_RIGHT_UP, &PANEL_3D_LAYOUT::onCanvasMouse, this );
    m_canvas->Bind( wxEVT_ENTER_WINDOW, &PANEL_3D_LAYOUT::onCanvasMouse, this );

    // Keyboard / toolbar zooms are animated: refresh the status after each frame is drawn
    m_canvas->Bind( wxEVT_PAINT,
                    [this]( wxPaintEvent& aEvent )
                    {
                        aEvent.Skip();

                        if( m_statusPending )
                            return;

                        m_statusPending = true;
                        CallAfter( [this]()
                                   {
                                       m_statusPending = false;
                                       UpdateStatus();
                                   } );
                    } );

    wxBoxSizer* sizer = new wxBoxSizer( wxVERTICAL );
    sizer->Add( m_canvas, 1, wxEXPAND, 0 );
    SetSizer( sizer );
}


PANEL_3D_LAYOUT::~PANEL_3D_LAYOUT()
{
    if( m_toolManager )
        m_toolManager->ShutdownAllTools();

    m_canvas->SetEventDispatcher( nullptr );

    // The canvas refers to the adapter and camera owned here, so it goes first
    delete m_canvas;
    m_canvas = nullptr;

    delete m_toolDispatcher;
    m_toolDispatcher = nullptr;
    delete m_toolManager;
    m_toolManager = nullptr;
    delete m_actions;
    m_actions = nullptr;
}


wxWindow* PANEL_3D_LAYOUT::GetToolCanvas() const
{
    return m_canvas;
}


void PANEL_3D_LAYOUT::ReloadBoard()
{
    updateAdapterSettings();
    m_canvas->ReloadRequest( m_frame->GetBoard(), PROJECT_PCB::Get3DCacheManager( &m_frame->Prj() ) );
    m_canvas->Request_refresh();
}


void PANEL_3D_LAYOUT::updateAdapterSettings()
{
    EDA_3D_VIEWER_SETTINGS* cfg = m_boardAdapter.m_Cfg;

    if( !cfg )
        return;

    using S = EDA_3D_VIEWER_SETTINGS;

    m_boardAdapter.m_BoardThicknessScale = float( cfg->m_LayoutView.board_thickness_scale );

    // Realistic colours (stackup / 3D theme), or the PCB editor's layer colours
    m_boardAdapter.m_ColorOverrides.clear();

    std::map<int, KIGFX::COLOR4D> colors = m_boardAdapter.GetLayerColors();
    bool                          byLayer = cfg->m_LayoutView.colors_by_layer;

    cfg->m_Render.use_board_editor_copper_colors = byLayer;

    if( byLayer )
    {
        if( COLOR_SETTINGS* cs = m_frame->GetColorSettings() )
        {
            colors[LAYER_3D_COPPER_TOP] = cs->GetColor( F_Cu );
            colors[LAYER_3D_COPPER_BOTTOM] = cs->GetColor( B_Cu );
            colors[LAYER_3D_SILKSCREEN_TOP] = cs->GetColor( F_SilkS );
            colors[LAYER_3D_SILKSCREEN_BOTTOM] = cs->GetColor( B_SilkS );
            colors[LAYER_3D_SOLDERMASK_TOP] = cs->GetColor( F_Mask );
            colors[LAYER_3D_SOLDERMASK_BOTTOM] = cs->GetColor( B_Mask );
            colors[LAYER_3D_SOLDERPASTE] = cs->GetColor( F_Paste );
        }
    }

    // Each material's transparency; a fully transparent one is not drawn
    const std::pair<int, int> materials[] = {
        { S::LM_COPPER, LAYER_3D_COPPER_TOP },        { S::LM_COPPER, LAYER_3D_COPPER_BOTTOM },
        { S::LM_CORE, LAYER_3D_BOARD },               { S::LM_SILK_TOP, LAYER_3D_SILKSCREEN_TOP },
        { S::LM_SILK_BOTTOM, LAYER_3D_SILKSCREEN_BOTTOM },
        { S::LM_MASK_TOP, LAYER_3D_SOLDERMASK_TOP },  { S::LM_MASK_BOTTOM, LAYER_3D_SOLDERMASK_BOTTOM },
        { S::LM_PASTE_TOP, LAYER_3D_SOLDERPASTE }
    };

    m_boardAdapter.m_HiddenLayers.reset();

    for( const auto& [material, layer] : materials )
    {
        double         t = cfg->LayoutTransparency( material );
        KIGFX::COLOR4D color = colors[layer];

        // The renderer blends the board body and solder masks; other materials are opaque
        if( layer == LAYER_3D_BOARD || layer == LAYER_3D_SOLDERMASK_TOP
                || layer == LAYER_3D_SOLDERMASK_BOTTOM )
        {
            color.a = std::clamp( 1.0 - t, 0.05, 1.0 );
        }
        else
        {
            color.a = 1.0;
        }

        m_boardAdapter.m_ColorOverrides[layer] = color;

        if( t >= 0.99 )
            m_boardAdapter.m_HiddenLayers.set( layer );
    }

    // Altium has a paste row for each side; KiCad draws one paste layer
    if( cfg->LayoutTransparency( S::LM_PASTE_BOTTOM ) < 0.99 )
        m_boardAdapter.m_HiddenLayers.reset( LAYER_3D_SOLDERPASTE );
}


void PANEL_3D_LAYOUT::ApplyViewSettings()
{
    ReloadBoard();
}


void PANEL_3D_LAYOUT::RefreshView()
{
    m_canvas->Request_refresh();
}


void PANEL_3D_LAYOUT::FlipView()
{
    m_toolManager->RunAction( EDA_3D_ACTIONS::flipView );
    UpdateStatus();
}


bool PANEL_3D_LAYOUT::IsPerspective() const
{
    return m_currentCamera.GetProjection() == PROJECTION_TYPE::PERSPECTIVE;
}


void PANEL_3D_LAYOUT::SetPerspective( bool aPerspective )
{
    if( aPerspective != IsPerspective() )
    {
        m_currentCamera.ToggleProjection();

        if( m_boardAdapter.m_Cfg )
            m_boardAdapter.m_Cfg->m_Camera.projection_mode = (int) m_currentCamera.GetProjection();

        m_canvas->Request_refresh();
    }
}


KIGFX::COLOR4D PANEL_3D_LAYOUT::GetMaterialColor( int aMaterial ) const
{
    using S = EDA_3D_VIEWER_SETTINGS;

    std::map<int, KIGFX::COLOR4D> colors = m_boardAdapter.GetLayerColors();
    int                           layer = LAYER_3D_COPPER_TOP;

    switch( aMaterial )
    {
    case S::LM_CORE:        layer = LAYER_3D_BOARD;             break;
    case S::LM_SILK_TOP:    layer = LAYER_3D_SILKSCREEN_TOP;    break;
    case S::LM_SILK_BOTTOM: layer = LAYER_3D_SILKSCREEN_BOTTOM; break;
    case S::LM_MASK_TOP:    layer = LAYER_3D_SOLDERMASK_TOP;    break;
    case S::LM_MASK_BOTTOM: layer = LAYER_3D_SOLDERMASK_BOTTOM; break;
    case S::LM_PASTE_TOP:
    case S::LM_PASTE_BOTTOM: layer = LAYER_3D_SOLDERPASTE;      break;
    default:                                                    break;
    }

    return colors[layer].WithAlpha( 1.0 );
}


void PANEL_3D_LAYOUT::ScheduleReload()
{
    if( m_reloadPending )
        return;

    m_reloadPending = true;

    CallAfter( [this]()
               {
                   m_reloadPending = false;

                   if( IsShown() )
                       ReloadBoard();
               } );
}


bool PANEL_3D_LAYOUT::RunViewAction( const TOOL_ACTION& aAction )
{
    // The 3D controller handles these (as the 3D viewer window does)
    for( const TOOL_ACTION* action : { &ACTIONS::zoomIn, &ACTIONS::zoomOut, &ACTIONS::zoomInCenter,
                                       &ACTIONS::zoomOutCenter, &ACTIONS::zoomFitScreen,
                                       &ACTIONS::zoomRedraw } )
    {
        if( aAction.GetName() == action->GetName() )
        {
            m_toolManager->RunAction( aAction );
            UpdateStatus();
            return true;
        }
    }

    // Fit to objects / selection mean "fit the board" in 3D
    if( aAction.GetName() == ACTIONS::zoomFitObjects.GetName()
            || aAction.GetName() == ACTIONS::zoomFitSelection.GetName() )
    {
        m_toolManager->RunAction( ACTIONS::zoomFitScreen );
        UpdateStatus();
        return true;
    }

    return false;
}


void PANEL_3D_LAYOUT::UpdateStatus()
{
    if( !IsShown() )
        return;

    const char* engine = m_boardAdapter.m_Cfg
                                 && m_boardAdapter.m_Cfg->m_Render.engine
                                            == RENDER_ENGINE::RAYTRACING
                                 ? "Raytracing" : "OpenGL";

    m_frame->SetStatusText( wxString::Format( _( "3D view (%s)   zoom %.2f   dx %.2f  dy %.2f" ),
                                              engine, 1.0 / m_currentCamera.GetZoom(),
                                              m_currentCamera.GetCameraPos().x,
                                              m_currentCamera.GetCameraPos().y ),
                            0 );
}


void PANEL_3D_LAYOUT::onCanvasMouse( wxMouseEvent& aEvent )
{
    aEvent.Skip();

    if( m_statusPending )
        return;

    m_statusPending = true;

    // After the canvas has applied the event to the camera
    CallAfter( [this]()
               {
                   m_statusPending = false;
                   UpdateStatus();
               } );
}


void PANEL_3D_LAYOUT::FocusCanvas()
{
    m_canvas->SetFocus();
}


bool PANEL_3D_LAYOUT::OpenMenuForMnemonic( int aKey )
{
    return m_frame->OpenMenuForMnemonic( aKey );
}


void PANEL_3D_LAYOUT::onCanvasCharHook( wxKeyEvent& aEvent )
{
    int key = aEvent.GetKeyCode();

    if( key >= WXK_NUMPAD0 && key <= WXK_NUMPAD9 )
        key = '0' + ( key - WXK_NUMPAD0 );
    else if( key >= 'a' && key <= 'z' )
        key = key - 'a' + 'A';

    int hotkey = key;

    if( aEvent.ControlDown() )
        hotkey |= MD_CTRL;

    if( aEvent.AltDown() )
        hotkey |= MD_ALT;

    if( aEvent.ShiftDown() )
        hotkey |= MD_SHIFT;

    if( m_keyHandler && m_keyHandler( hotkey ) )
        return;

    // The editor's zoom keys work on the 3D view
    for( const TOOL_ACTION* action : { &ACTIONS::zoomIn, &ACTIONS::zoomOut, &ACTIONS::zoomInCenter,
                                       &ACTIONS::zoomOutCenter, &ACTIONS::zoomFitScreen,
                                       &ACTIONS::zoomFitObjects, &ACTIONS::zoomRedraw } )
    {
        if( hotkey != 0 && ( hotkey == action->GetHotKey() || hotkey == action->GetHotKeyAlt() ) )
        {
            RunViewAction( *action );
            return;
        }
    }

    aEvent.Skip();
}
