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

#include <bitmaps.h>
#include <bitmap_store.h>
#include <kicad_manager_frame.h>
#include <kiplatform/policy.h>
#include <policy_keys.h>
#include <tool/tool_manager.h>
#include <tools/kicad_manager_actions.h>
#include <tools/kicad_manager_control.h>
#include <file_history.h>
#include <widgets/bitmap_button.h>
#include <paths.h>
#include <wx/button.h>
#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/listctrl.h>
#include <wx/stattext.h>

#include "panel_kicad_launcher.h"


PANEL_KICAD_LAUNCHER::PANEL_KICAD_LAUNCHER( wxWindow* aParent ) :
        PANEL_KICAD_LAUNCHER_BASE( aParent ),
        m_frame( static_cast<KICAD_MANAGER_FRAME*>( aParent->GetParent() ) ),
        m_recentTimer( this )
{
    // Altium-style Home page: quick actions and recent projects above the application tiles
    m_homeSizer = new wxBoxSizer( wxVERTICAL );
    m_scrolledWindow->GetSizer()->Insert( 0, m_homeSizer, 0, wxEXPAND | wxALL, 5 );

    CreateLaunchers();

    Bind( wxEVT_TIMER, [this]( wxTimerEvent& ) { refreshRecentProjects( false ); } );
    m_recentTimer.Start( 1000 );

    Bind( wxEVT_SYS_COLOUR_CHANGED, wxSysColourChangedEventHandler( PANEL_KICAD_LAUNCHER::onThemeChanged ), this );
}


PANEL_KICAD_LAUNCHER::~PANEL_KICAD_LAUNCHER()
{
    m_recentTimer.Stop();
    m_frame->SetPcmButton( nullptr );

    for( wxWindow* window : m_scrolledWindow->GetChildren() )
    {
        if( dynamic_cast<BITMAP_BUTTON*>( window ) != nullptr )
            window->Unbind( wxEVT_BUTTON, &PANEL_KICAD_LAUNCHER::onLauncherButtonClick, this );
    }

    Unbind( wxEVT_SYS_COLOUR_CHANGED, wxSysColourChangedEventHandler( PANEL_KICAD_LAUNCHER::onThemeChanged ), this );
}


void PANEL_KICAD_LAUNCHER::onLauncherButtonClick( wxCommandEvent& aEvent )
{
    // Don't accept clicks processed during wxProgressReporter updating.  In particular, the wxSafeYield()
    // call below will puke.
    if( m_frame->GetToolManager()->GetTool<KICAD_MANAGER_CONTROL>()->InShowPlayer() )
        return;

    // Defocus the button because leaving the large buttons focused after a click looks out of place in
    // the launcher
    m_frame->SetFocus();

    // Gives a slice of time to update the button state (mandatory on GTK, useful on MSW to avoid some
    // cosmetic issues).
    wxSafeYield();

    BITMAP_BUTTON*     button = (BITMAP_BUTTON*) aEvent.GetEventObject();
    const TOOL_ACTION* action = static_cast<const TOOL_ACTION*>( button->GetClientData() );

    if( action == nullptr )
        return;

    OPT_TOOL_EVENT evt = action->MakeEvent();
    evt->SetHasPosition( false );
    m_frame->GetToolManager()->ProcessEvent( *evt );
}


void PANEL_KICAD_LAUNCHER::CreateLaunchers()
{
    m_frame->SetPcmButton( nullptr );

    if( m_toolsSizer->GetEffectiveRowsCount() > 0 )
    {
        m_toolsSizer->Clear( true );
        m_toolsSizer->SetRows( 0 );
    }

    wxFont titleFont = wxSystemSettings::GetFont( wxSYS_DEFAULT_GUI_FONT );
#ifndef __WXGTK__
    titleFont.SetPointSize( titleFont.GetPointSize() + 2 );
#endif
    titleFont.SetWeight( wxFONTWEIGHT_BOLD );

    wxFont helpFont = wxSystemSettings::GetFont( wxSYS_DEFAULT_GUI_FONT );
    helpFont.SetStyle( wxFONTSTYLE_ITALIC );

    createHomeSection();

    auto addLauncher =
            [&]( const TOOL_ACTION& aAction, BITMAPS aBitmaps, const wxString& aHelpText, bool enabled = true )
            {
                BITMAP_BUTTON* btn = new BITMAP_BUTTON( m_scrolledWindow, wxID_ANY );
                btn->SetBitmap( KiBitmapBundle( aBitmaps ) );
                btn->SetDisabledBitmap( KiDisabledBitmapBundle( aBitmaps ) );
                btn->SetPadding( 4 );
                btn->SetToolTip( aAction.GetTooltip() );

                m_scrolledWindow->SetFont( titleFont ); // Use font inheritance to avoid extra SetFont call.
                wxStaticText* label = new wxStaticText( m_scrolledWindow, wxID_ANY, aAction.GetFriendlyName() );
                label->SetToolTip( aAction.GetTooltip() );

                m_scrolledWindow->SetFont( helpFont ); // Use font inheritance to avoid extra SetFont call.
                wxStaticText* help = new wxStaticText( m_scrolledWindow, wxID_ANY, aHelpText );

                btn->Bind( wxEVT_BUTTON, &PANEL_KICAD_LAUNCHER::onLauncherButtonClick, this );
                btn->SetClientData( (void*) &aAction );

                // The title and description launch too, like the entries on Altium's Home page.
                // They are sized to their text (not the column), with a hand cursor as the cue.
                const TOOL_ACTION* action = &aAction;

                for( wxStaticText* text : { label, help } )
                {
                    text->SetCursor( wxCursor( wxCURSOR_HAND ) );
                    text->Bind( wxEVT_LEFT_UP,
                                [this, btn, action]( wxMouseEvent& )
                                {
                                    if( !btn->IsEnabled() )
                                        return;

                                    // Same path as the button: guards against re-entering
                                    // an editor that is still loading
                                    runAction( *action );
                                } );
                }

                m_toolsSizer->Add( btn, 1, wxALIGN_CENTER_VERTICAL );

                wxBoxSizer* textSizer = new wxBoxSizer( wxVERTICAL );

                textSizer->Add( label );
                textSizer->Add( help );

                m_toolsSizer->Add( textSizer, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL );

                btn->Enable( enabled );

                if( !enabled )
                {
                    help->Disable();
                    label->Disable();
                }

                return btn;
            };

    addLauncher( KICAD_MANAGER_ACTIONS::editSchematic, BITMAPS::icon_eeschema,
                 _( "Edit the project schematic" ) );

    addLauncher( KICAD_MANAGER_ACTIONS::editSymbols, BITMAPS::icon_libedit,
                 _( "Edit global and/or project schematic symbol libraries" ) );

    addLauncher( KICAD_MANAGER_ACTIONS::editPCB, BITMAPS::icon_pcbnew,
                 _( "Edit the project PCB design" ) );

    addLauncher( KICAD_MANAGER_ACTIONS::editFootprints, BITMAPS::icon_modedit,
                 _( "Edit global and/or project PCB footprint libraries" ) );

    addLauncher( KICAD_MANAGER_ACTIONS::viewGerbers, BITMAPS::icon_gerbview,
                 _( "Preview Gerber files" ) );

    addLauncher( KICAD_MANAGER_ACTIONS::convertImage, BITMAPS::icon_bitmap2component,
                 _( "Convert bitmap images to schematic symbols or PCB footprints" ) );

    addLauncher( KICAD_MANAGER_ACTIONS::showCalculator, BITMAPS::icon_pcbcalculator,
                 _( "Show tools for calculating resistance, current capacity, etc." ) );

    addLauncher( KICAD_MANAGER_ACTIONS::editDrawingSheet, BITMAPS::icon_pagelayout_editor,
                 _( "Edit drawing sheet borders and title blocks for use in schematics and PCB designs" ) );

    BITMAP_BUTTON* bb = addLauncher( KICAD_MANAGER_ACTIONS::showPluginManager, BITMAPS::icon_pcm,
                                     _( "Manage downloadable packages from KiCad and 3rd party repositories" ),
                                     KIPLATFORM::POLICY::GetPolicyBool( POLICY_KEY_PCM )
                                            != KIPLATFORM::POLICY::PBOOL::DISABLED );

    m_frame->SetPcmButton( bb );

    Layout();
}


void PANEL_KICAD_LAUNCHER::onThemeChanged( wxSysColourChangedEvent& aEvent )
{
    GetBitmapStore()->ThemeChanged();
    CreateLaunchers();

    aEvent.Skip();
}


void PANEL_KICAD_LAUNCHER::runAction( const TOOL_ACTION& aAction )
{
    if( m_frame->GetToolManager()->GetTool<KICAD_MANAGER_CONTROL>()->InShowPlayer() )
        return;

    m_frame->SetFocus();

    OPT_TOOL_EVENT evt = aAction.MakeEvent();
    evt->SetHasPosition( false );
    m_frame->GetToolManager()->ProcessEvent( *evt );
}


void PANEL_KICAD_LAUNCHER::createHomeSection()
{
    // Rebuilt with the launchers (theme and language changes)
    m_homeSizer->Clear( true );
    m_recentList = nullptr;
    m_recentSig.clear();

    wxFont pageFont = wxSystemSettings::GetFont( wxSYS_DEFAULT_GUI_FONT );
    pageFont.SetPointSize( pageFont.GetPointSize() + 6 );
    pageFont.SetWeight( wxFONTWEIGHT_BOLD );

    wxFont headFont = wxSystemSettings::GetFont( wxSYS_DEFAULT_GUI_FONT );
    headFont.SetPointSize( headFont.GetPointSize() + 1 );
    headFont.SetWeight( wxFONTWEIGHT_BOLD );

    wxFont plainFont = wxSystemSettings::GetFont( wxSYS_DEFAULT_GUI_FONT );

    auto heading =
            [&]( const wxString& aText, const wxFont& aFont )
            {
                wxStaticText* label = new wxStaticText( m_scrolledWindow, wxID_ANY, aText );
                label->SetFont( aFont );
                return label;
            };

    m_homeSizer->Add( heading( _( "Home" ), pageFont ), 0, wxBOTTOM, 8 );

    // Quick actions: one button per common start
    m_homeSizer->Add( heading( _( "Quick Actions" ), headFont ), 0, wxBOTTOM, 4 );

    wxBoxSizer* actions = new wxBoxSizer( wxHORIZONTAL );

    auto addAction =
            [&]( const TOOL_ACTION& aAction, const wxString& aLabel )
            {
                wxButton* btn = new wxButton( m_scrolledWindow, wxID_ANY, aLabel );
                btn->SetFont( plainFont );
                btn->SetBitmap( KiBitmapBundle( aAction.GetIcon() ) );
                btn->SetToolTip( aAction.GetTooltip() );
                btn->Bind( wxEVT_BUTTON, [this, &aAction]( wxCommandEvent& ) { runAction( aAction ); } );
                actions->Add( btn, 0, wxRIGHT, 6 );
            };

    addAction( KICAD_MANAGER_ACTIONS::newProject, _( "New Project..." ) );
    addAction( KICAD_MANAGER_ACTIONS::openProject, _( "Open Project..." ) );

    if( wxDir::Exists( PATHS::GetStockDemosPath() ) )
        addAction( KICAD_MANAGER_ACTIONS::openDemoProject, _( "Open Demo Project..." ) );

    addAction( KICAD_MANAGER_ACTIONS::openJobsetFile, _( "Open Jobset..." ) );

    m_homeSizer->Add( actions, 0, wxBOTTOM, 12 );

    // Recent projects: double-click (or Enter) opens one
    m_homeSizer->Add( heading( _( "Recent Projects" ), headFont ), 0, wxBOTTOM, 4 );

    m_recentList = new wxListCtrl( m_scrolledWindow, wxID_ANY, wxDefaultPosition,
                                   FromDIP( wxSize( 560, 150 ) ),
                                   wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_THEME );
    m_recentList->SetFont( plainFont );
    m_recentList->AppendColumn( _( "Project" ), wxLIST_FORMAT_LEFT, FromDIP( 180 ) );
    m_recentList->AppendColumn( _( "Location" ), wxLIST_FORMAT_LEFT, FromDIP( 370 ) );
    m_recentList->Bind( wxEVT_LIST_ITEM_ACTIVATED, &PANEL_KICAD_LAUNCHER::onRecentActivated, this );
    m_homeSizer->Add( m_recentList, 0, wxEXPAND | wxBOTTOM, 12 );

    m_homeSizer->Add( heading( _( "Applications" ), headFont ), 0, wxBOTTOM, 4 );

    refreshRecentProjects( true );
}


void PANEL_KICAD_LAUNCHER::refreshRecentProjects( bool aForce )
{
    if( !m_recentList || ( !aForce && !IsShownOnScreen() ) )
        return;

    FILE_HISTORY& history = m_frame->GetFileHistory();
    wxString      sig;

    for( size_t ii = 0; ii < history.GetCount(); ++ii )
        sig << history.GetHistoryFile( ii ) << wxS( "|" );

    if( !aForce && sig == m_recentSig )
        return;

    m_recentSig = sig;
    m_recentFiles.clear();

    m_recentList->Freeze();
    m_recentList->DeleteAllItems();

    for( size_t ii = 0; ii < history.GetCount(); ++ii )
    {
        wxFileName fn( history.GetHistoryFile( ii ) );
        long       row = m_recentList->InsertItem( (long) m_recentFiles.size(), fn.GetName() );

        m_recentList->SetItem( row, 1, fn.GetPath() );

        if( !fn.FileExists() )
            m_recentList->SetItemTextColour( row, wxSystemSettings::GetColour( wxSYS_COLOUR_GRAYTEXT ) );

        m_recentFiles.push_back( fn.GetFullPath() );
    }

    if( m_recentFiles.empty() )
        m_recentList->InsertItem( 0, _( "No recent projects" ) );

    m_recentList->Thaw();
}


void PANEL_KICAD_LAUNCHER::onRecentActivated( wxListEvent& aEvent )
{
    long row = aEvent.GetIndex();

    if( row < 0 || row >= (long) m_recentFiles.size() )
        return;

    if( m_frame->GetToolManager()->GetTool<KICAD_MANAGER_CONTROL>()->InShowPlayer() )
        return;

    wxFileName fn( m_recentFiles[row] );

    if( !fn.FileExists() )
    {
        wxBell();
        return;
    }

    // Deferred: loading a project rebuilds this page
    CallAfter( [this, fn]() { m_frame->LoadProject( fn ); } );
}
