/*
 * This program source code file is part of KICAD, a free EDA CAD application.
 *
 * Copyright (C) 2020-2023 CERN
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 * @author Maciej Suminski <maciej.suminski@cern.ch>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "properties_panel.h"
#include <frame_type.h>
#include <widgets/altium_sub_tabs.h>
#include <tool/selection.h>
#include <eda_base_frame.h>
#include <eda_draw_frame.h>
#include <eda_item.h>
#include <import_export.h>
#include <pgm_base.h>
#include <properties/pg_cell_renderer.h>
#include <properties/property.h>
#include <properties/property_mgr.h>
#include <settings/app_settings.h>
#include <tool/actions.h>
#include <tool/grid_menu.h>
#include <tool/tool_manager.h>

#include <algorithm>
#include <iterator>
#include <set>

#include <functional>

#include <bitmaps.h>
#include <settings/grid_settings.h>

#include <wx/bmpbuttn.h>
#include <wx/button.h>
#include <wx/listctrl.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/menu.h>
#include <wx/clipbrd.h>
#include <wx/dcbuffer.h>
#include <wx/dataobj.h>
#include <wx/settings.h>
#include <wx/stattext.h>
#include <wx/srchctrl.h>
#include <wx/time.h>
#include <wx/propgrid/advprops.h>


// This is provided by wx >3.3.0
#if !wxCHECK_VERSION( 3, 3, 0 )
extern APIIMPORT wxPGGlobalVarsClass* wxPGGlobalVars;
#endif


/**
 * Altium-style section title: a small triangle and the name in bold; a click folds the section.
 */
class PROPERTIES_SECTION_HEADER : public wxPanel
{
public:
    PROPERTIES_SECTION_HEADER( wxWindow* aParent, const wxString& aTitle,
                               std::function<void( bool )> aOnFold ) :
            wxPanel( aParent ),
            m_title( aTitle ),
            m_onFold( std::move( aOnFold ) )
    {
        SetBackgroundStyle( wxBG_STYLE_PAINT );
        SetFont( GetFont().Bold() );
        SetMinSize( wxSize( -1, GetCharHeight() + FromDIP( 10 ) ) );
        SetCursor( wxCursor( wxCURSOR_HAND ) );

        Bind( wxEVT_PAINT,
              [this]( wxPaintEvent& )
              {
                  wxAutoBufferedPaintDC dc( this );
                  wxColour fg = wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOWTEXT );

                  dc.SetBackground( wxBrush( GetParent()->GetBackgroundColour() ) );
                  dc.Clear();

                  int s = FromDIP( 4 );
                  int cx = FromDIP( 6 );
                  int cy = GetClientSize().y / 2;

                  dc.SetPen( wxPen( fg ) );
                  dc.SetBrush( wxBrush( fg ) );

                  if( m_folded )
                  {
                      wxPoint tri[3] = { { cx - s / 2, cy - s }, { cx - s / 2, cy + s }, { cx + s / 2 + 1, cy } };
                      dc.DrawPolygon( 3, tri );
                  }
                  else
                  {
                      // Open: a small triangle in the lower-right corner, as Altium draws it
                      wxPoint tri[3] = { { cx + s / 2, cy - s / 2 }, { cx + s / 2, cy + s / 2 + 1 },
                                         { cx - s / 2 - 1, cy + s / 2 + 1 } };
                      dc.DrawPolygon( 3, tri );
                  }

                  dc.SetFont( GetFont() );
                  dc.SetTextForeground( fg );
                  dc.DrawText( m_title, FromDIP( 14 ), ( GetClientSize().y - GetCharHeight() ) / 2 );
              } );

        Bind( wxEVT_LEFT_DOWN,
              [this]( wxMouseEvent& )
              {
                  m_folded = !m_folded;
                  Refresh();
                  m_onFold( m_folded );
              } );
    }

    void SetTitle( const wxString& aTitle ) { m_title = aTitle; Refresh(); }

private:
    wxString                    m_title;
    std::function<void( bool )> m_onFold;
    bool                        m_folded = false;
};


/**
 * Altium's Grid Manager: the grids as a table (the snap grid ticked) with Add / Properties /
 * delete buttons under it.  Double-click a grid to snap to it; right-click for the fast grids.
 */
class GRID_MANAGER_SECTION : public wxPanel
{
public:
    GRID_MANAGER_SECTION( wxWindow* aParent, EDA_DRAW_FRAME* aFrame ) :
            wxPanel( aParent ),
            m_frame( aFrame )
    {
        m_list = new wxListView( this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                 wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_SIMPLE );
        m_list->AppendColumn( _( "Priority" ) );
        m_list->AppendColumn( _( "Name" ) );
        m_list->AppendColumn( _( "Step" ) );
        m_list->AppendColumn( _( "Snap" ), wxLIST_FORMAT_CENTER );
        m_list->AppendColumn( _( "Fast" ), wxLIST_FORMAT_CENTER );

        int rowHeight = GetCharHeight() + FromDIP( 8 );
        m_list->SetMinSize( wxSize( FromDIP( 200 ), rowHeight * 7 ) );

        wxButton* add = new wxButton( this, wxID_ANY, _( "Add" ) + wxS( " \u25BE" ) );
        wxButton* props = new wxButton( this, wxID_ANY, _( "Properties" ) );
        m_delete = new wxBitmapButton( this, wxID_ANY, KiBitmapBundle( BITMAPS::small_trash ) );
        m_delete->SetToolTip( _( "Delete the selected grid" ) );

        wxBoxSizer* buttons = new wxBoxSizer( wxHORIZONTAL );
        buttons->AddStretchSpacer();
        buttons->Add( add, 0, wxRIGHT, FromDIP( 4 ) );
        buttons->Add( props, 0, wxRIGHT, FromDIP( 4 ) );
        buttons->Add( m_delete, 0 );

        wxBoxSizer* sizer = new wxBoxSizer( wxVERTICAL );
        sizer->Add( m_list, 1, wxEXPAND );
        sizer->Add( buttons, 0, wxEXPAND | wxTOP, FromDIP( 4 ) );
        SetSizer( sizer );

        add->Bind( wxEVT_BUTTON,
                   [this, add]( wxCommandEvent& )
                   {
                       wxMenu menu;
                       menu.Append( wxID_ADD, _( "Add Grid..." ) );
                       menu.Bind( wxEVT_MENU, [this]( wxCommandEvent& ) { gridDialog(); }, wxID_ADD );
                       add->PopupMenu( &menu, 0, add->GetSize().y );
                   } );
        props->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { gridDialog(); } );
        m_delete->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { deleteGrid( m_list->GetFirstSelected() ); } );

        m_list->Bind( wxEVT_LIST_ITEM_ACTIVATED,
                      [this]( wxListEvent& aEvent ) { makeSnapGrid( aEvent.GetIndex() ); } );
        m_list->Bind( wxEVT_LIST_ITEM_RIGHT_CLICK,
                      [this]( wxListEvent& aEvent ) { onRightClick( aEvent.GetIndex() ); } );
        m_list->Bind( wxEVT_SIZE,
                      [this]( wxSizeEvent& aEvent )
                      {
                          fitColumns();
                          aEvent.Skip();
                      } );

        // Grids change from the toolbar, hotkeys and the grid dialog: follow them while shown
        Bind( wxEVT_IDLE,
              [this]( wxIdleEvent& aEvent )
              {
                  if( IsShownOnScreen() )
                      refresh();

                  aEvent.Skip();
              } );

        refresh();
    }

private:
    WINDOW_SETTINGS* window() const
    {
        APP_SETTINGS_BASE* cfg = m_frame->config();
        return cfg ? m_frame->GetWindowSettings( cfg ) : nullptr;
    }

    void refresh()
    {
        WINDOW_SETTINGS* win = window();

        if( !win )
            return;

        wxArrayString grids;
        GRID_MENU::BuildChoiceList( &grids, win, m_frame );

        wxString signature = wxString::Format( wxS( "%d|%d|%d|" ), win->grid.last_size_idx,
                                               win->grid.fast_grid_1, win->grid.fast_grid_2 );

        for( const wxString& grid : grids )
            signature << grid << wxS( "|" );

        if( signature == m_signature )
            return;

        m_signature = signature;

        long selected = m_list->GetFirstSelected();

        m_list->Freeze();
        m_list->DeleteAllItems();

        for( size_t ii = 0; ii < grids.size(); ++ii )
        {
            const GRID* grid = ii < win->grid.grids.size() ? &win->grid.grids[ii] : nullptr;
            wxString    name = grid && !grid->name.IsEmpty() ? grid->name
                                                             : wxString::Format( _( "Grid %zu" ), ii + 1 );
            wxString    step = grid ? grid->UserUnitsMessageText( m_frame ) : grids[ii];
            wxString    fast;

            if( (int) ii == win->grid.fast_grid_1 )
                fast = wxS( "1" );

            if( (int) ii == win->grid.fast_grid_2 )
                fast = fast.IsEmpty() ? wxString( wxS( "2" ) ) : fast + wxS( ", 2" );

            long row = m_list->InsertItem( (long) ii, wxString::Format( wxS( "%zu" ), ii + 1 ) );
            m_list->SetItem( row, 1, name );
            m_list->SetItem( row, 2, step );
            m_list->SetItem( row, 3, (int) ii == win->grid.last_size_idx ? wxString( wxS( "\u2713" ) )
                                                                       : wxString() );
            m_list->SetItem( row, 4, fast );
        }

        if( selected >= 0 && selected < m_list->GetItemCount() )
            m_list->Select( selected );

        m_list->Thaw();

        m_fittedWidth = -1;
        fitColumns();
        m_delete->Enable( grids.size() > 1 );
    }

    void fitColumns()
    {
        // Column widths change the scrollbars, which resize the list: don't recurse, and
        // leave slack so a horizontal scrollbar never appears
        if( m_fitting )
            return;

        // Use the outer width less room for a vertical scrollbar, so the result doesn't depend
        // on whether the scrollbar is shown (the native list can loop forever toggling it)
        int width = m_list->GetSize().x - wxSystemSettings::GetMetric( wxSYS_VSCROLL_X, m_list )
                    - FromDIP( 6 );

        if( width <= 0 || width == m_fittedWidth )
            return;

        m_fitting = true;
        m_fittedWidth = width;

        int narrow = FromDIP( 46 );
        int step = std::max( FromDIP( 60 ), ( width - 3 * narrow ) / 2 );

        m_list->SetColumnWidth( 0, narrow );
        m_list->SetColumnWidth( 1, std::max( FromDIP( 40 ), width - 3 * narrow - step ) );
        m_list->SetColumnWidth( 2, step );
        m_list->SetColumnWidth( 3, narrow );
        m_list->SetColumnWidth( 4, narrow );
        m_fitting = false;
    }

    void gridDialog()
    {
        m_frame->GetToolManager()->RunAction( ACTIONS::gridProperties );
        m_signature.clear();
        refresh();
    }

    void makeSnapGrid( long aRow )
    {
        if( aRow < 0 )
            return;

        m_frame->GetToolManager()->RunAction( ACTIONS::gridPreset, static_cast<int>( aRow ) );
        refresh();
    }

    void deleteGrid( long aRow )
    {
        WINDOW_SETTINGS* win = window();

        if( !win || aRow < 0 || aRow >= (long) win->grid.grids.size() || win->grid.grids.size() < 2 )
            return;

        win->grid.grids.erase( win->grid.grids.begin() + aRow );

        int last = (int) win->grid.grids.size() - 1;

        for( int* idx : { &win->grid.last_size_idx, &win->grid.fast_grid_1, &win->grid.fast_grid_2 } )
        {
            if( *idx > aRow )
                ( *idx )--;

            *idx = std::clamp( *idx, 0, last );
        }

        m_frame->GetToolManager()->RunAction( ACTIONS::gridPreset, win->grid.last_size_idx );
        m_frame->UpdateGridSelectBox();
        refresh();
    }

    void onRightClick( long aRow )
    {
        WINDOW_SETTINGS* win = window();

        if( !win || aRow < 0 )
            return;

        enum { ID_SNAP = wxID_HIGHEST + 1, ID_FAST1, ID_FAST2, ID_DELETE };

        wxMenu menu;
        menu.Append( ID_SNAP, _( "Use as Snap Grid" ) );
        menu.Append( ID_FAST1, _( "Use as Fast Grid 1" ) );
        menu.Append( ID_FAST2, _( "Use as Fast Grid 2" ) );
        menu.AppendSeparator();
        menu.Append( ID_DELETE, _( "Delete" ) );
        menu.Enable( ID_DELETE, win->grid.grids.size() > 1 );

        switch( GetPopupMenuSelectionFromUser( menu ) )
        {
        case ID_SNAP:   makeSnapGrid( aRow ); break;
        case ID_FAST1:  win->grid.fast_grid_1 = (int) aRow; refresh(); break;
        case ID_FAST2:  win->grid.fast_grid_2 = (int) aRow; refresh(); break;
        case ID_DELETE: deleteGrid( aRow ); break;
        default: break;
        }
    }

    EDA_DRAW_FRAME* m_frame;
    wxListView*     m_list = nullptr;
    wxBitmapButton* m_delete = nullptr;
    wxString        m_signature;
    bool            m_fitting = false;
    int             m_fittedWidth = -1;
};


class PROPERTIES_PANEL_GRID : public wxPropertyGrid
{
public:
    PROPERTIES_PANEL_GRID( wxWindow* aParent ) :
            wxPropertyGrid( aParent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                            wxPG_DEFAULT_STYLE | wxPG_TOOLTIPS, PG_CELL_RENDERER::PANEL_GRID_NAME )
    {
    }

#if wxUSE_STATUSBAR
    wxStatusBar* GetStatusBar() override { return nullptr; }
#endif
};


/**
 * Altium's selection filter button in the Properties header: a blue funnel and a small arrow.
 */
class FILTER_FUNNEL_BUTTON : public wxWindow
{
public:
    FILTER_FUNNEL_BUTTON( wxWindow* aParent, std::function<void( wxWindow* )> aOnClick ) :
            wxWindow( aParent, wxID_ANY ),
            m_onClick( std::move( aOnClick ) )
    {
        SetBackgroundStyle( wxBG_STYLE_PAINT );
        SetMinSize( FromDIP( wxSize( 30, 20 ) ) );
        SetCursor( wxCursor( wxCURSOR_HAND ) );
        SetToolTip( _( "Selection filter" ) );

        Bind( wxEVT_PAINT,
              [this]( wxPaintEvent& )
              {
                  wxAutoBufferedPaintDC dc( this );
                  wxColour bg = GetParent()->GetBackgroundColour();
                  wxColour fg = wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOWTEXT );
                  wxColour blue = PROPERTIES_FORM::LinkColour();
                  wxSize   size = GetClientSize();

                  dc.SetBackground( wxBrush( m_hover ? PROPERTIES_FORM::Mix( bg, fg, 0.12 ) : bg ) );
                  dc.Clear();

                  // Funnel: a wide top narrowing to a short spout
                  int     h = size.y * 3 / 5;
                  int     top = ( size.y - h ) / 2;
                  int     left = FromDIP( 4 );
                  int     w = FromDIP( 13 );
                  wxPoint funnel[6] = { { left, top },
                                        { left + w, top },
                                        { left + w * 3 / 5, top + h / 2 },
                                        { left + w * 3 / 5, top + h },
                                        { left + w * 2 / 5, top + h - FromDIP( 2 ) },
                                        { left + w * 2 / 5, top + h / 2 } };

                  dc.SetPen( wxPen( blue ) );
                  dc.SetBrush( wxBrush( blue ) );
                  dc.DrawPolygon( 6, funnel );

                  int     a = FromDIP( 3 );
                  int     ax = left + w + FromDIP( 7 );
                  int     ay = size.y / 2;
                  wxPoint arrow[3] = { { ax - a, ay - a / 2 }, { ax + a, ay - a / 2 },
                                       { ax, ay + a / 2 + 1 } };
                  dc.SetPen( wxPen( fg ) );
                  dc.SetBrush( wxBrush( fg ) );
                  dc.DrawPolygon( 3, arrow );
              } );

        Bind( wxEVT_ENTER_WINDOW, [this]( wxMouseEvent& ) { m_hover = true; Refresh(); } );
        Bind( wxEVT_LEAVE_WINDOW, [this]( wxMouseEvent& ) { m_hover = false; Refresh(); } );
        Bind( wxEVT_LEFT_DOWN,
              [this]( wxMouseEvent& ) { CallAfter( [this]() { m_onClick( this ); } ); } );
    }

private:
    std::function<void( wxWindow* )> m_onClick;
    bool                             m_hover = false;
};


PROPERTIES_PANEL::PROPERTIES_PANEL( wxWindow* aParent, EDA_BASE_FRAME* aFrame ) :
        wxPanel( aParent ),
        m_SuppressGridChangeEvents( 0 ),
        m_frame( aFrame ),
        m_footer( nullptr ),
        m_search( nullptr ),
        m_documentMode( false ),
        m_wasOnScreen( false ),
        m_lastDocumentRefresh( 0 ),
        m_splitter_key_proportion( -1 )
{
    wxBoxSizer* mainSizer = new wxBoxSizer( wxVERTICAL );

#if !wxCHECK_VERSION( 3, 3, 0 )
    // on some platforms wxPGGlobalVars is initialized automatically,
    // but others need an explicit init
    if( !wxPGGlobalVars )
        wxPGInitResourceModule();
#endif

    // See https://gitlab.com/kicad/code/kicad/-/issues/12297
    // and https://github.com/wxWidgets/wxWidgets/issues/11787
    if( wxPGGlobalVars->m_mapEditorClasses.empty() )
    {
        wxPGEditor_TextCtrl = nullptr;
        wxPGEditor_Choice = nullptr;
        wxPGEditor_ComboBox = nullptr;
        wxPGEditor_TextCtrlAndButton = nullptr;
        wxPGEditor_CheckBox = nullptr;
        wxPGEditor_ChoiceAndButton = nullptr;
        wxPGEditor_SpinCtrl = nullptr;
        wxPGEditor_DatePickerCtrl = nullptr;
    }

    if( !Pgm().m_PropertyGridInitialized )
    {
        delete wxPGGlobalVars->m_defaultRenderer;
        wxPGGlobalVars->m_defaultRenderer = new PG_CELL_RENDERER();
        Pgm().m_PropertyGridInitialized = true;
    }

    // Altium-style header: the kind of selection in bold, then a search box.
    m_caption = new wxStaticText( this, wxID_ANY, _( "Nothing selected" ), wxDefaultPosition,
                                  wxDefaultSize, wxST_ELLIPSIZE_END );

    // ... with the selection filter summary and its funnel button on the right
    wxBoxSizer* headerSizer = new wxBoxSizer( wxHORIZONTAL );
    headerSizer->Add( m_caption, 1, wxALIGN_CENTER_VERTICAL );

    m_filterSummary = new wxStaticText( this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                        wxDefaultSize, wxST_ELLIPSIZE_END );
    headerSizer->Add( m_filterSummary, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 6 );

    m_filterButton = new FILTER_FUNNEL_BUTTON( this,
                                               [this]( wxWindow* aButton )
                                               {
                                                   onSelectionFilterButton( aButton );
                                                   updateFilterSummary();
                                               } );
    headerSizer->Add( m_filterButton, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 2 );
    m_filterSummary->Hide();
    m_filterButton->Hide();

    mainSizer->Add( headerSizer, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, 6 );

    m_search = new wxSearchCtrl( this, wxID_ANY );
    m_search->ShowCancelButton( true );
    m_search->SetDescriptiveText( _( "Search" ) );
    mainSizer->Add( m_search, 0, wxALL | wxEXPAND, 5 );

    m_tabBar = new ALTIUM_SUB_TABS( this, [this]( int aTab ) { selectTab( aTab ); } );
    m_tabBar->Hide();
    mainSizer->Add( m_tabBar, 0, wxLEFT | wxRIGHT | wxEXPAND, 5 );

    m_search->Bind( wxEVT_TEXT, [this]( wxCommandEvent& ) { applyFilter(); } );
    m_search->Bind( wxEVT_SEARCH_CANCEL,
                    [this]( wxCommandEvent& )
                    {
                        m_search->ChangeValue( wxEmptyString );
                        applyFilter();
                    } );

    // As in Altium, everything under the search box scrolls as one: the sections, the property
    // rows (the grid is as tall as its rows, so it never scrolls by itself) and the sections
    // under it
    m_scroll = new wxScrolledWindow( this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                     wxVSCROLL | wxBORDER_NONE );
    m_scroll->SetScrollRate( 0, FromDIP( 12 ) );

    // Vertical scrolling only: the content always takes the panel's width
    m_scroll->Bind( wxEVT_SIZE,
                    [this]( wxSizeEvent& aEvent )
                    {
                        aEvent.Skip();

                        // Only the width follows the panel; don't re-enter a layout in progress
                        if( m_relayouting )
                            return;

                        int width = m_scroll->GetClientSize().x;

                        // A few tries at most: content that re-wraps taller at the narrower
                        // width can make the scrollbar come and go
                        if( width == m_scroll->GetVirtualSize().x )
                            m_widthFixes = 0;
                        else if( m_widthFixes++ < 3 )
                            CallAfter( [this]() { relayout(); } );
                    } );
    m_scrollSizer = new wxBoxSizer( wxVERTICAL );
    m_scroll->SetSizer( m_scrollSizer );

    // The panel itself resized (not just its scrollbar coming or going): refit freely again
    Bind( wxEVT_SIZE,
          [this]( wxSizeEvent& aEvent )
          {
              m_widthFixes = 0;
              aEvent.Skip();
          } );

    m_grid = new PROPERTIES_PANEL_GRID( m_scroll );
    m_grid->SetUnspecifiedValueAppearance( wxPGCell( wxT( "<...>" ) ) );

#if wxCHECK_VERSION( 3, 3, 0 )
    m_grid->SetValidationFailureBehavior( wxPGVFBFlags::MarkCell );
#else
    m_grid->SetValidationFailureBehavior( wxPG_VFB_MARK_CELL );
#endif

#if wxCHECK_VERSION( 3, 3, 0 )
    m_grid->AddActionTrigger( wxPGKeyboardAction::NextProperty, WXK_RETURN );
    m_grid->AddActionTrigger( wxPGKeyboardAction::NextProperty, WXK_NUMPAD_ENTER );
    m_grid->AddActionTrigger( wxPGKeyboardAction::NextProperty, WXK_DOWN );
    m_grid->AddActionTrigger( wxPGKeyboardAction::PrevProperty, WXK_UP );
    m_grid->AddActionTrigger( wxPGKeyboardAction::Edit, WXK_SPACE );
#else
    m_grid->AddActionTrigger( wxPG_ACTION_NEXT_PROPERTY, WXK_RETURN );
    m_grid->AddActionTrigger( wxPG_ACTION_NEXT_PROPERTY, WXK_NUMPAD_ENTER );
    m_grid->AddActionTrigger( wxPG_ACTION_NEXT_PROPERTY, WXK_DOWN );
    m_grid->AddActionTrigger( wxPG_ACTION_PREV_PROPERTY, WXK_UP );
    m_grid->AddActionTrigger( wxPG_ACTION_EDIT, WXK_SPACE );
#endif

    m_grid->DedicateKey( WXK_RETURN );
    m_grid->DedicateKey( WXK_NUMPAD_ENTER );
    m_grid->DedicateKey( WXK_DOWN );
    m_grid->DedicateKey( WXK_UP );
    m_scrollSizer->Add( m_grid, 0, wxEXPAND );
    mainSizer->Add( m_scroll, 1, wxEXPAND );

    // The grid has no room to scroll; the wheel scrolls the whole panel.  One notch moves the
    // content by the system's "lines per notch" (3 by default) property rows, as in Altium and
    // in the other panels' native lists; the scroll window's own handler would only move it by
    // 3 scroll units (36 px), about a row and a half.
    auto onWheel =
            [this]( wxMouseEvent& aEvent )
            {
                if( aEvent.GetWheelAxis() != wxMOUSE_WHEEL_VERTICAL )
                {
                    aEvent.Skip();
                    return;
                }

                // Accumulate partial notches (precision touchpads send small rotations)
                m_wheelRemainder += aEvent.GetWheelRotation();
                int delta = std::max( 1, aEvent.GetWheelDelta() );
                int notches = m_wheelRemainder / delta;
                m_wheelRemainder -= notches * delta;

                if( notches == 0 )
                    return;

                int pixels;

                if( aEvent.IsPageScroll() || aEvent.GetLinesPerAction() <= 0 )
                    pixels = notches * m_scroll->GetClientSize().y;
                else
                    pixels = notches * aEvent.GetLinesPerAction() * m_grid->GetRowHeight();

                int unitX, unitY, x, y;
                m_scroll->GetScrollPixelsPerUnit( &unitX, &unitY );
                m_scroll->GetViewStart( &x, &y );

                int units = std::max( 1, ( std::abs( pixels ) + unitY / 2 ) / std::max( 1, unitY ) );
                m_scroll->Scroll( x, std::max( 0, pixels > 0 ? y - units : y + units ) );
            };

    m_grid->Bind( wxEVT_MOUSEWHEEL, onWheel );
    m_scroll->Bind( wxEVT_MOUSEWHEEL, onWheel );
    m_grid->Bind( wxEVT_PG_ITEM_COLLAPSED, [this]( wxPropertyGridEvent& aEvent ) { fitGridHeight(); aEvent.Skip(); } );
    m_grid->Bind( wxEVT_PG_ITEM_EXPANDED, [this]( wxPropertyGridEvent& aEvent ) { fitGridHeight(); aEvent.Skip(); } );

    m_footer = new wxStaticText( this, wxID_ANY, _( "Nothing selected" ) );
    mainSizer->Add( m_footer, 0, wxALL | wxEXPAND, 5 );

    // Read-only values (board information) read as plainly as the rest, as in Altium
    m_grid->SetCellDisabledTextColour( wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOWTEXT ) );

    // Flat Altium-like palette derived from the system theme: section bars a shade off the
    // window colour, rows on the window colour, faint row lines.
    auto mix =
            []( const wxColour& a, const wxColour& b, double t )
            {
                return wxColour( a.Red() + ( b.Red() - a.Red() ) * t,
                                 a.Green() + ( b.Green() - a.Green() ) * t,
                                 a.Blue() + ( b.Blue() - a.Blue() ) * t );
            };

    wxColour win  = wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOW );
    wxColour text = wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOWTEXT );

    // Altium: bold section titles straight on the panel colour, each section's rows on a
    // slightly lighter block, no lines between rows
    wxColour block = mix( win, text, 0.05 );

    m_grid->SetCaptionBackgroundColour( win );
    m_grid->SetCaptionTextColour( text );
    m_grid->SetCellBackgroundColour( block );
    m_grid->SetMarginColour( win );
    m_grid->SetLineColour( block );
    m_grid->SetEmptySpaceColour( win );
    m_grid->SetVerticalSpacing( 3 );

#ifdef __WXGTK__
    // Needed for dark mode, on wx 3.0 at least.
    m_grid->SetCaptionTextColour( wxSystemSettings::GetColour( wxSYS_COLOUR_CAPTIONTEXT ) );
#endif

    SetFont( KIUI::GetDockedPaneFont( this ) );

    // Altium titles the panel ("Polygon Pour", "Board") in the regular weight, a little larger
    wxFont captionFont = KIUI::GetDockedPaneFont( this );
    captionFont.SetFractionalPointSize( captionFont.GetFractionalPointSize() * 1.1 );
    m_caption->SetFont( captionFont );
    m_footer->SetForegroundColour( wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOWTEXT ) );

    SetSizer( mainSizer );
    Layout();

    m_grid->CenterSplitter();

    Connect( wxEVT_CHAR_HOOK, wxKeyEventHandler( PROPERTIES_PANEL::onCharHook ), nullptr, this );
    Connect( wxEVT_PG_CHANGED, wxPropertyGridEventHandler( PROPERTIES_PANEL::onGridChanged ), nullptr, this );
    Connect( wxEVT_PG_CHANGING, wxPropertyGridEventHandler( PROPERTIES_PANEL::onGridChanging ), nullptr, this );

    // Sections open and close with a single click on their bar, and stay that way across
    // selections.
    m_grid->Bind( wxEVT_LEFT_DOWN, &PROPERTIES_PANEL::onGridLeftDown, this );
    m_grid->Bind( wxEVT_PG_ITEM_COLLAPSED,
                  [this]( wxPropertyGridEvent& aEvent )
                  {
                      if( aEvent.GetProperty() && aEvent.GetProperty()->IsCategory() )
                          m_collapsedGroups.insert( aEvent.GetProperty()->GetLabel() );

                      aEvent.Skip();
                  } );
    m_grid->Bind( wxEVT_PG_ITEM_EXPANDED,
                  [this]( wxPropertyGridEvent& aEvent )
                  {
                      if( aEvent.GetProperty() && aEvent.GetProperty()->IsCategory() )
                          m_collapsedGroups.erase( aEvent.GetProperty()->GetLabel() );

                      aEvent.Skip();
                  } );
    Connect( wxEVT_SHOW, wxShowEventHandler( PROPERTIES_PANEL::onShow ), nullptr, this );

    Bind( wxEVT_PG_COL_END_DRAG,
          [&]( wxPropertyGridEvent& )
          {
              m_splitter_key_proportion = static_cast<float>( m_grid->GetSplitterPosition() ) / m_grid->GetSize().x;
          } );

    Bind( wxEVT_SIZE,
          [&]( wxSizeEvent& aEvent )
          {
              CallAfter( [this]()
                         {
                            RecalculateSplitterPos();
                         } );
              aEvent.Skip();
          } );

    m_frame->Bind( EDA_LANG_CHANGED, &PROPERTIES_PANEL::OnLanguageChanged, this );

    // A panel revealed by a tab strip doesn't always get a show event, and the frame skips
    // updates while it is off screen; catch up whenever it comes (back) on screen.
    Bind( wxEVT_IDLE,
          [this]( wxIdleEvent& aEvent )
          {
              bool onScreen = IsShownOnScreen();

              if( onScreen && !m_wasOnScreen )
                  UpdateData();

              // Document options and counts change without selection events (loading, sheet
              // switches, edits); keep them current while they are on show.  Not while a tool
              // is busy: a drag clears the selection, and rebuilding the page every second
              // made the panel blink until the drag ended.
              bool busy = !m_frame->ToolStackIsEmpty()
                          || wxGetMouseState().ButtonIsDown( wxMOUSE_BTN_ANY );

              if( onScreen && m_documentMode && !busy
                      && wxGetLocalTimeMillis() - m_lastDocumentRefresh > 1000 )
              {
                  m_lastDocumentRefresh = wxGetLocalTimeMillis();
                  UpdateData();
              }

              if( onScreen )
                  fitGridHeight();

              m_wasOnScreen = onScreen;
              aEvent.Skip();
          } );
}


void PROPERTIES_PANEL::fitGridHeight()
{
    int rows = 0;

    for( wxPropertyGridIterator it = m_grid->GetIterator( wxPG_ITERATE_VISIBLE ); !it.AtEnd(); ++it )
        rows++;

    // No rows (an Altium layout shows everything in its sections): no grid, not even its border
    int height = rows > 0 ? rows * m_grid->GetRowHeight() + FromDIP( 2 ) : 0;

    if( height == m_gridHeight )
        return;

    if( m_grid->IsShown() != ( rows > 0 ) )
        m_grid->Show( rows > 0 );

    m_gridHeight = height;
    // A small minimum width: the grid follows the panel width rather than its own best width
    m_grid->SetMinSize( wxSize( FromDIP( 80 ), height ) );
    m_grid->SetSize( wxSize( m_grid->GetSize().x, height ) );
    relayout();
}


void PROPERTIES_PANEL::relayout()
{
    if( m_relayouting )
        return;

    m_relayouting = true;

    // Height from the content, width from the panel.  Setting the height can show or hide
    // the scrollbar, which changes the width left for the content: fit again then, so nothing
    // sits under the scrollbar.
    for( int pass = 0; pass < 2; ++pass )
    {
        int    width = m_scroll->GetClientSize().x;
        wxSize best = m_scrollSizer->CalcMin();

        m_scroll->SetVirtualSize( width, best.y );
        m_scroll->Layout();

        if( m_scroll->GetClientSize().x == width )
            break;
    }

    Layout();

    // Sections move when another one changes height: repaint the whole scrolled area, or the
    // old position of a section stays on screen (ghost buttons / overlapping rows)
    m_scroll->Refresh();

    m_relayouting = false;
}


PROPERTIES_PANEL::~PROPERTIES_PANEL()
{
    m_frame->Unbind( EDA_LANG_CHANGED, &PROPERTIES_PANEL::OnLanguageChanged, this );
}


void PROPERTIES_PANEL::OnLanguageChanged( wxCommandEvent& aEvent )
{
    if( m_grid->IsEditorFocused() )
        m_grid->CommitChangesFromEditor();

    m_grid->Clear();
    m_displayed.clear();    // no ownership of pointers
    m_documentMode = false;

    UpdateData();

    aEvent.Skip();
}


class SUPPRESS_GRID_CHANGED_EVENTS
{
public:
    SUPPRESS_GRID_CHANGED_EVENTS( PROPERTIES_PANEL* aPanel ) :
            m_panel( aPanel )
    {
        m_panel->m_SuppressGridChangeEvents++;
    }

    ~SUPPRESS_GRID_CHANGED_EVENTS()
    {
        m_panel->m_SuppressGridChangeEvents--;
    }

private:
    PROPERTIES_PANEL* m_panel;
};


void PROPERTIES_PANEL::rebuildProperties( const SELECTION& aSelection )
{
    SUPPRESS_GRID_CHANGED_EVENTS raii( this );

    auto reset =
            [&]()
            {
                if( m_grid->IsEditorFocused() )
                    m_grid->CommitChangesFromEditor();

                m_grid->Clear();
                m_displayed.clear();
                m_documentMode = false;
            };

    updateHeader( aSelection );
    setTabs( panelTabs( aSelection ) );
    showDocumentWidget( aSelection.Empty() );

    if( aSelection.Empty() )
    {
        m_boundProps.clear();
        setSelectionLayout( wxEmptyString );
        rebuildDocumentProperties();
        return;
    }

    if( m_documentMode )
        reset();

    // Get all the selected types
    std::set<TYPE_ID> types;

    for( EDA_ITEM* item : aSelection )
        types.insert( TYPE_HASH( *item ) );

    wxCHECK( !types.empty(), /* void */ );  // already guarded above, but Coverity doesn't know that

    PROPERTY_MANAGER&                  propMgr = PROPERTY_MANAGER::Instance();
    std::map<wxString, PROPERTY_BASE*> commonProps;
    const std::vector<PROPERTY_BASE*>& allProperties = propMgr.GetProperties( *types.begin() );

    for( PROPERTY_BASE* property : allProperties )
        commonProps.emplace( property->Name(), property );

    std::map<wxString, int> displayOrder;
    for( const auto& entry : propMgr.GetDisplayOrder( *types.begin() ) )
        displayOrder.emplace( entry.first->Name(), entry.second );

    std::vector<wxString> groupDisplayOrder = propMgr.GetGroupDisplayOrder( *types.begin() );
    std::set<wxString>    groups( groupDisplayOrder.begin(), groupDisplayOrder.end() );

    // Get all possible properties
    for( auto itType = std::next( types.begin() ); itType != types.end(); ++itType )
    {
        TYPE_ID type = *itType;

        for( const wxString& group : propMgr.GetGroupDisplayOrder( type ) )
        {
            if( !groups.count( group ) )
            {
                groupDisplayOrder.emplace_back( group );
                groups.insert( group );
            }
        }

        for( auto it = commonProps.begin(); it != commonProps.end(); )
        {
            if( !propMgr.GetProperty( type, it->first ) )
                it = commonProps.erase( it );
            else
                ++it;
        }
    }

    bool isLibraryEditor = m_frame->IsType( FRAME_FOOTPRINT_EDITOR )
                        || m_frame->IsType( FRAME_SCH_SYMBOL_EDITOR );

    bool isDesignEditor = m_frame->IsType( FRAME_PCB_EDITOR )
                       || m_frame->IsType( FRAME_SCH );

    std::set<wxString> availableProps;

    // Find a set of properties that is common to all selected items
    for( auto& [name, property] : commonProps )
    {
        if( property->IsHiddenFromPropertiesManager() )
            continue;

        if( isLibraryEditor && property->IsHiddenFromLibraryEditors() )
            continue;

        if( isDesignEditor && property->IsHiddenFromDesignEditors() )
            continue;

        wxVariant   dummy;
        wxPGChoices choices;
        bool        writable;

        if( extractValueAndWritability( aSelection, name, dummy, writable, choices ) )
            availableProps.insert( name );
    }

    bool               writeable = true;
    std::set<wxString> existingProps;

    for( wxPropertyGridIterator it = m_grid->GetIterator(); !it.AtEnd(); it.Next() )
    {
        wxPGProperty* pgProp = it.GetProperty();
        wxString      name   = pgProp->GetName();

        // Store the existing name before checking available properties so we can
        // remove the properties when they are no longer available
        existingProps.insert( name );

        if( !availableProps.count( name ) )
            continue;

        wxVariant   commonVal;
        wxPGChoices choices;

        extractValueAndWritability( aSelection, name, commonVal, writeable, choices );

        if( choices.GetCount() > 0 )
            pgProp->SetChoices( choices );

        pgProp->SetValue( commonVal );
        pgProp->ChangeFlag( wxPG_PROP_READONLY, !writeable );
    }

    if( !existingProps.empty() && existingProps == availableProps )
    {
        selectionGridUpdated( aSelection );
        applyFilter();
        return;
    }

    // Some difference exists:  start from scratch
    reset();

    std::map<wxPGProperty*, int>                   pgPropOrders;
    std::map<wxString, std::vector<wxPGProperty*>> pgPropGroups;
    const wxString                                 locationGroup = wxS( "\x01Location" );

    for( const wxString& name : availableProps )
    {
        PROPERTY_BASE* property = commonProps[name];
        wxPGProperty*  pgProp   = createPGProperty( property );
        wxVariant      commonVal;
        wxPGChoices    choices;

        if( !extractValueAndWritability( aSelection, name, commonVal, writeable, choices ) )
            continue;

        if( pgProp )
        {
            if( choices.GetCount() )
                pgProp->SetChoices( choices );

            pgProp->SetValue( commonVal );
            pgProp->ChangeFlag( wxPG_PROP_READONLY, !writeable );
            m_displayed.push_back( property );

            wxASSERT( displayOrder.count( name ) );
            pgPropOrders[pgProp] = displayOrder[name];

            // Altium leads with a Location section holding position and rotation.
            if( name == wxS( "Position X" ) || name == wxS( "Position Y" )
                    || name == wxS( "Orientation" ) )
            {
                pgPropGroups[locationGroup].emplace_back( pgProp );
            }
            else
            {
                pgPropGroups[property->Group()].emplace_back( pgProp );
            }
        }
    }

    groupDisplayOrder.insert( groupDisplayOrder.begin(), locationGroup );

    const wxString unspecifiedGroupCaption = _( "Properties" );

    for( const wxString& groupName : groupDisplayOrder )
    {
        if( !pgPropGroups.count( groupName ) )
            continue;

        std::vector<wxPGProperty*>& properties = pgPropGroups[groupName];
        wxString                    groupCaption = groupName == locationGroup
                                                           ? _( "Location" )
                                                           : wxGetTranslation( groupName );

        auto groupItem = new wxPropertyCategory( groupName.IsEmpty() ? unspecifiedGroupCaption
                                                                     : groupCaption );

        m_grid->Append( groupItem );

        std::sort( properties.begin(), properties.end(),
                   [&]( wxPGProperty*& aFirst, wxPGProperty*& aSecond )
                   {
                       return pgPropOrders[aFirst] < pgPropOrders[aSecond];
                   } );

        for( wxPGProperty* property : properties )
            m_grid->Append( property );
    }

    applyCollapsedState();
    selectionGridUpdated( aSelection );
    applyFilter();
    RecalculateSplitterPos();
}


void PROPERTIES_PANEL::updateHeader( const SELECTION& aSelection )
{
    wxString caption;
    wxString footer;

    if( aSelection.Empty() )
    {
        caption = documentCaption();
        footer = _( "Nothing selected" );
    }
    else
    {
        caption = itemTypeName( aSelection.Front() );

        for( EDA_ITEM* item : aSelection )
        {
            if( itemTypeName( item ) != caption )
            {
                caption = _( "Mixed Objects" );
                break;
            }
        }

        footer = wxString::Format( wxPLURAL( "%d object is selected", "%d objects are selected",
                                             aSelection.Size() ),
                                   aSelection.Size() );
    }

    if( m_caption->GetLabel() != caption )
        m_caption->SetLabel( caption );

    if( m_footer->GetLabel() != footer )
        m_footer->SetLabel( footer );

    updateFilterSummary();
}


void PROPERTIES_PANEL::updateFilterSummary()
{
    if( !m_filterSummary || !m_filterButton )
        return;

    wxString summary = selectionFilterSummary();
    bool     show = !summary.IsEmpty();
    bool     changed = m_filterSummary->IsShown() != show || m_filterButton->IsShown() != show;

    if( m_filterSummary->GetLabel() != summary )
    {
        m_filterSummary->SetLabel( summary );
        changed = true;
    }

    if( changed )
    {
        m_filterSummary->Show( show );
        m_filterButton->Show( show );

        if( wxSizer* sizer = m_filterSummary->GetContainingSizer() )
            sizer->Layout();
    }
}


void PROPERTIES_PANEL::FormApply( const std::vector<std::pair<wxString, wxVariant>>& aValues )
{
    if( aValues.empty() || m_documentMode )
        return;

    applyPropertyValues( aValues );
}


void PROPERTIES_PANEL::setTabs( const std::vector<wxString>& aTabs )
{
    if( aTabs == m_tabs )
        return;

    // Each set of tabs (Document Options, a component, Library Options, ...) comes back on the
    // tab it was left on, as in Altium: deselecting doesn't jump back to General
    auto key =
            []( const std::vector<wxString>& aSet )
            {
                wxString joined;

                for( const wxString& tab : aSet )
                    joined << tab << wxS( "" );

                return joined;
            };

    if( !m_tabs.empty() )
        m_lastTabs[key( m_tabs )] = m_activeTab;

    m_tabs = aTabs;

    auto last = m_lastTabs.find( key( m_tabs ) );
    m_activeTab = last != m_lastTabs.end() && last->second < (int) m_tabs.size()
                          ? last->second
                          : std::clamp( defaultTab( m_tabs ), 0, std::max( 0, (int) m_tabs.size() - 1 ) );

    if( ALTIUM_SUB_TABS* bar = static_cast<ALTIUM_SUB_TABS*>( m_tabBar ) )
    {
        bar->SetTabs( m_tabs, m_activeTab );

        if( bar->IsShown() != !m_tabs.empty() )
        {
            bar->Show( !m_tabs.empty() );
            Layout();
        }
    }
}


void PROPERTIES_PANEL::selectTab( int aTab )
{
    if( aTab == m_activeTab )
        return;

    m_activeTab = aTab;
    applyFilter();
    showDocumentWidget( m_docRequested );
    fitGridHeight();
}


void PROPERTIES_PANEL::addSelectionSection( wxWindow* aWidget, const wxString& aTitle,
                                            const wxString& aLayout, int aTab )
{
    addSelectionSection( aWidget, aTitle, aLayout );

    if( !m_selSections.empty() && m_selSections.back().widget == aWidget )
    {
        m_selSections.back().tab = aTab;
        m_selSections.back().headerless = aTitle.IsEmpty();
    }
}


void PROPERTIES_PANEL::addSelectionSection( wxWindow* aWidget, const wxString& aTitle,
                                            const wxString& aLayout )
{
    if( !aWidget || !m_scrollSizer )
        return;

    aWidget->Reparent( m_scroll );

    // Just above the grid, after the sections already there
    int index = 0;

    for( wxSizerItem* item : m_scrollSizer->GetChildren() )
    {
        if( item->GetWindow() == m_grid )
            break;

        index++;
    }

    wxWindow* header = new PROPERTIES_SECTION_HEADER( m_scroll, aTitle,
            [this, aWidget]( bool aFolded )
            {
                for( SEL_SECTION& section : m_selSections )
                {
                    if( section.widget == aWidget )
                    {
                        section.folded = aFolded;
                        section.widget->Show( !aFolded );
                    }
                }

                relayout();
            } );

    m_selSections.push_back( { aWidget, header, aLayout } );

    m_scrollSizer->Insert( index, header, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, 4 );
    m_scrollSizer->Insert( index + 1, aWidget, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 6 );

    header->Hide();
    aWidget->Hide();
}


void PROPERTIES_PANEL::setSelectionLayout( const wxString& aLayout )
{
    m_selLayout = aLayout;
    updateSelectionSections();
}


void PROPERTIES_PANEL::updateSelectionSections()
{
    wxString search = m_search ? m_search->GetValue() : wxString();
    bool     searching = !search.Trim().Trim( false ).IsEmpty();
    bool     changed = false;

    // While nothing is selected, the document's layout (if the editor has one) shows instead
    wxString layout = m_documentMode ? documentLayout() : m_selLayout;
    int      tab = m_tabs.empty() ? 0 : m_activeTab;

    for( SEL_SECTION& section : m_selSections )
    {
        bool show = !searching && section.tab == tab && !layout.IsEmpty()
                    && section.layout == layout;

        if( show )
        {
            if( PROPERTIES_FORM* form = dynamic_cast<PROPERTIES_FORM*>( section.widget ) )
                show = form->HasVisibleRows();
        }

        bool showWidget = show && !section.folded;

        bool showHeader = show && !section.headerless;

        if( section.header->IsShown() != showHeader )
        {
            section.header->Show( showHeader );
            changed = true;
        }

        if( section.widget->IsShown() != showWidget )
        {
            section.widget->Show( showWidget );
            changed = true;
        }
    }

    if( changed )
        relayout();
}


wxString PROPERTIES_PANEL::itemTypeName( EDA_ITEM* aItem ) const
{
    return aItem->GetFriendlyName();
}


wxString PROPERTIES_PANEL::documentCaption() const
{
    return _( "Document Options" );
}


void PROPERTIES_PANEL::applyCollapsedState()
{
    wxPGProperty* root = m_grid->GetRoot();

    for( unsigned ii = 0; ii < root->GetChildCount(); ++ii )
    {
        wxPGProperty* child = root->Item( ii );

        if( child->IsCategory() && m_collapsedGroups.count( child->GetLabel() ) )
            m_grid->Collapse( child );
    }
}


void PROPERTIES_PANEL::applyFilter()
{
    if( !m_search )
        return;

    wxString filter = m_search->GetValue().Lower();
    filter.Trim( true ).Trim( false );

#if wxCHECK_VERSION( 3, 3, 0 )
    const wxPGPropertyValuesFlags dontRecurse = wxPGPropertyValuesFlags::DontRecurse;
#else
    const int dontRecurse = wxPG_DONT_RECURSE;
#endif

    wxPGProperty* root = m_grid->GetRoot();

    // Only touch rows whose state changes: hiding repaints and re-lays out the grid
    auto hide =
            [&]( wxPGProperty* aProp, bool aHide )
            {
                if( aProp->HasFlag( wxPG_PROP_HIDDEN ) != aHide )
                    m_grid->HideProperty( aProp, aHide, dontRecurse );
            };

    // Rows shown by the section forms stay out of the grid, except to answer a search
    auto rowShown =
            [&]( wxPGProperty* aRow )
            {
                if( !filter.IsEmpty() )
                    return aRow->GetLabel().Lower().Contains( filter );

                // Only the first tab has the section forms
                if( !m_tabs.empty() && m_activeTab != 0 )
                    return true;

                return !m_boundProps.count( aRow->GetName() );
            };

    m_grid->Freeze();

    for( unsigned ii = 0; ii < root->GetChildCount(); ++ii )
    {
        wxPGProperty* section = root->Item( ii );
        int           tab = categoryTab( section );

        // A negative tab: a category Altium doesn't show for this object (its settings are in
        // the object's dialog); only a search still finds its rows
        bool          onTab = tab < 0 ? !filter.IsEmpty()
                                      : m_tabs.empty() || tab == m_activeTab;

        if( !section->IsCategory() )
        {
            hide( section, !onTab || !rowShown( section ) );
            continue;
        }

        bool anyShown = false;

        for( unsigned jj = 0; jj < section->GetChildCount(); ++jj )
        {
            wxPGProperty* row = section->Item( jj );
            bool          show = onTab && rowShown( row );

            hide( row, !show );
            anyShown |= show;
        }

        hide( section, !anyShown );
    }

    m_grid->Thaw();

    updateSelectionSections();
}


void PROPERTIES_PANEL::onGridLeftDown( wxMouseEvent& aEvent )
{
    wxPoint                     pt = m_grid->CalcUnscrolledPosition( aEvent.GetPosition() );
    wxPropertyGridHitTestResult hit = m_grid->HitTest( pt );
    wxPGProperty*               prop = hit.GetProperty();

    // Clicks in the margin already toggle via the expander button.
    if( prop && prop->IsCategory() && hit.GetColumn() >= 0 )
    {
        if( prop->IsExpanded() )
        {
            m_grid->Collapse( prop );
            m_collapsedGroups.insert( prop->GetLabel() );
        }
        else
        {
            m_grid->Expand( prop );
            m_collapsedGroups.erase( prop->GetLabel() );
        }

        return;
    }

    aEvent.Skip();
}


void PROPERTIES_PANEL::onGridChanging( wxPropertyGridEvent& aEvent )
{
    if( m_documentMode )
    {
        aEvent.Skip();
        return;
    }

    valueChanging( aEvent );
}


void PROPERTIES_PANEL::onGridChanged( wxPropertyGridEvent& aEvent )
{
    if( m_documentMode )
    {
        if( m_SuppressGridChangeEvents == 0 )
        {
            documentValueChanged( aEvent.GetPropertyName(), aEvent.GetPropertyValue() );

            // Actions may change what the rows show (e.g. grid names follow the units).
            CallAfter( [this]() { UpdateData(); } );
        }

        return;
    }

    valueChanged( aEvent );
}


wxPGProperty* PROPERTIES_PANEL::docCategory( const wxString& aName, const wxString& aLabel )
{
    if( wxPGProperty* existing = m_grid->GetPropertyByName( aName ) )
        return existing;

    return m_grid->Append( new wxPropertyCategory( aLabel, aName ) );
}


wxPGProperty* PROPERTIES_PANEL::docText( wxPGProperty* aCategory, const wxString& aName,
                                         const wxString& aLabel, const wxString& aValue,
                                         bool aReadOnly )
{
    wxPGProperty* prop = m_grid->GetPropertyByName( aName );

    if( !prop )
    {
        prop = m_grid->AppendIn( aCategory, new wxStringProperty( aLabel, aName, aValue ) );
        prop->ChangeFlag( wxPG_PROP_READONLY, aReadOnly );
    }
    else if( prop->GetValueAsString() != aValue )
    {
        prop->SetValue( aValue );
    }

    return prop;
}


wxPGProperty* PROPERTIES_PANEL::docChoice( wxPGProperty* aCategory, const wxString& aName,
                                           const wxString& aLabel, const wxArrayString& aChoices,
                                           int aSelection )
{
    wxPGProperty* prop = m_grid->GetPropertyByName( aName );

    if( !prop )
    {
        return m_grid->AppendIn( aCategory, new wxEnumProperty( aLabel, aName, aChoices,
                                                                wxArrayInt(), aSelection ) );
    }

    const wxPGChoices& current = prop->GetChoices();
    bool               same = current.GetCount() == aChoices.GetCount();

    for( unsigned ii = 0; same && ii < aChoices.GetCount(); ++ii )
        same = current.GetLabel( ii ) == aChoices[ii];

    if( !same )
        prop->SetChoices( wxPGChoices( aChoices ) );

    if( prop->GetChoiceSelection() != aSelection )
        prop->SetChoiceSelection( aSelection );

    return prop;
}


wxPGProperty* PROPERTIES_PANEL::docBool( wxPGProperty* aCategory, const wxString& aName,
                                         const wxString& aLabel, bool aValue )
{
    wxPGProperty* prop = m_grid->GetPropertyByName( aName );

    if( !prop )
    {
        prop = m_grid->AppendIn( aCategory, new wxBoolProperty( aLabel, aName, aValue ) );
        prop->SetAttribute( wxPG_BOOL_USE_CHECKBOX, true );
    }
    else if( prop->GetValue().GetBool() != aValue )
    {
        prop->SetValue( aValue );
    }

    return prop;
}


void PROPERTIES_PANEL::SetDocumentWidget( wxWindow* aWidget, const wxString& aTitle, int aPosition )
{
    wxSizer* sizer = m_scrollSizer;

    if( !aWidget || !sizer )
        return;

    aWidget->Reparent( m_scroll );

    // Sections above the grid come first in the list; those under it follow
    size_t above = std::count_if( m_docSections.begin(), m_docSections.end(),
                                  []( const DOC_SECTION& aSection ) { return !aSection.below; } );
    size_t index = ( aPosition < 0 || aPosition > (int) above ) ? above : (size_t) aPosition;

    // An Altium section title (folds the section), then the controls
    wxWindow* header = new PROPERTIES_SECTION_HEADER( m_scroll, aTitle,
            [this, aWidget]( bool aFolded )
            {
                for( DOC_SECTION& section : m_docSections )
                {
                    if( section.widget == aWidget )
                    {
                        section.folded = aFolded;
                        section.widget->Show( !aFolded );
                    }
                }

                relayout();
            } );

    m_docSections.insert( m_docSections.begin() + index, { aWidget, header } );

    // After the sections before it, above the grid
    sizer->Insert( 2 * index, header, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, 4 );
    sizer->Insert( 1 + 2 * index, aWidget, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 6 );

    m_docSectionsShown = !m_documentMode;    // force the update below
    showDocumentWidget( m_documentMode );
}


void PROPERTIES_PANEL::addDocumentSectionBelow( wxWindow* aWidget, const wxString& aTitle )
{
    wxSizer* sizer = m_scrollSizer;

    if( !aWidget || !sizer )
        return;

    aWidget->Reparent( m_scroll );

    // Below the grid, after the sections already there
    int index = 0;

    for( wxSizerItem* item : sizer->GetChildren() )
    {
        if( item->GetWindow() == m_grid )
            break;

        index++;
    }

    for( const DOC_SECTION& section : m_docSections )
    {
        if( section.below )
            index += 2;
    }

    wxWindow* header = new PROPERTIES_SECTION_HEADER( m_scroll, aTitle,
            [this, aWidget]( bool aFolded )
            {
                for( DOC_SECTION& section : m_docSections )
                {
                    if( section.widget == aWidget )
                    {
                        section.folded = aFolded;
                        section.widget->Show( !aFolded );
                    }
                }

                relayout();
            } );

    DOC_SECTION section{ aWidget, header };
    section.below = true;
    m_docSections.push_back( section );

    sizer->Insert( index + 1, header, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, 4 );
    sizer->Insert( index + 2, aWidget, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 6 );

    m_docSectionsShown = !m_documentMode;
    showDocumentWidget( m_documentMode );
}


void PROPERTIES_PANEL::showDocumentWidget( bool aShow )
{
    m_docRequested = aShow;

    // The sections belong to the first tab
    aShow &= m_tabs.empty() || m_activeTab == 0;

    if( m_docSections.empty() || m_docSectionsShown == aShow )
        return;

    m_docSectionsShown = aShow;

    for( DOC_SECTION& section : m_docSections )
    {
        section.widget->Show( aShow && !section.folded );
        section.header->Show( aShow );
    }

    relayout();
}


void PROPERTIES_PANEL::rebuildDocumentProperties()
{
    SUPPRESS_GRID_CHANGED_EVENTS raii( this );

    // Don't disturb a row the user is typing in.
    if( m_documentMode && m_grid->IsEditorFocused() )
        return;

    bool created = !m_documentMode;

    if( !m_documentMode )
    {
        if( m_grid->IsEditorFocused() )
            m_grid->CommitChangesFromEditor();

        m_grid->Clear();
        m_displayed.clear();
        m_documentMode = true;
    }

    unsigned before = m_grid->GetRoot()->GetChildCount();

    if( EDA_DRAW_FRAME* drawFrame = dynamic_cast<EDA_DRAW_FRAME*>( m_frame ) )
    {
        wxPGProperty* general = docCategory( wxS( "doc.general" ), _( "General" ) );

        wxArrayString units;
        units.Add( _( "mm" ) );
        units.Add( _( "mils" ) );
        units.Add( _( "in" ) );

        int unitsIdx = 0;

        if( drawFrame->GetUserUnits() == EDA_UNITS::MILS )
            unitsIdx = 1;
        else if( drawFrame->GetUserUnits() == EDA_UNITS::INCH )
            unitsIdx = 2;

        docChoice( general, wxS( "doc.units" ), _( "Units" ), units, unitsIdx );

        if( APP_SETTINGS_BASE* cfg = drawFrame->config() )
        {
            if( WINDOW_SETTINGS* window = drawFrame->GetWindowSettings( cfg ) )
            {
                wxArrayString grids;
                GRID_MENU::BuildChoiceList( &grids, window, drawFrame );

                if( !grids.IsEmpty() )
                {
                    int idx = std::clamp( window->grid.last_size_idx, 0, (int) grids.size() - 1 );
                    docChoice( general, wxS( "doc.grid" ), _( "Snap Grid" ), grids, idx );
                }
            }
        }

        docBool( general, wxS( "doc.gridVisible" ), _( "Visible Grid" ),
                 drawFrame->IsGridVisible() );

        // Altium's Snap Options; editors add their object snapping rows to this section (or
        // show the whole section as a widget above the grid)
        if( !m_snapOptionsWidget )
        {
            wxPGProperty* snap = docCategory( wxS( "doc.snap" ), _( "Snap Options" ) );

            wxArrayString snapModes;
            snapModes.Add( _( "Always" ) );
            snapModes.Add( _( "When Grid Shown" ) );
            snapModes.Add( _( "Never" ) );

            docChoice( snap, wxS( "doc.gridSnap" ), _( "Snap to Grids" ), snapModes,
                       static_cast<int>( drawFrame->GetGalDisplayOptions().m_gridSnapping ) );
        }
    }

    appendDocumentProperties();

    EDA_DRAW_FRAME* gridFrame = dynamic_cast<EDA_DRAW_FRAME*>( m_frame );

    // Only Altium's PCB documents have a Grid Manager; the schematic's Document Options don't
    if( gridFrame && !gridFrame->IsType( FRAME_PCB_EDITOR ) && !gridFrame->IsType( FRAME_FOOTPRINT_EDITOR ) )
        gridFrame = nullptr;

    if( EDA_DRAW_FRAME* drawFrame = gridFrame )
    {
        // Altium's Grid Manager table sits under the grid; create it on first use
        if( !m_gridManager )
        {
            m_gridManager = new GRID_MANAGER_SECTION( this, drawFrame );
            addDocumentSectionBelow( m_gridManager, _( "Grid Manager" ) );
        }
    }

    created |= m_grid->GetRoot()->GetChildCount() != before;

    if( created )
    {
        applyCollapsedState();
        applyFilter();
        RecalculateSplitterPos();
    }
}


bool PROPERTIES_PANEL::documentValueChanged( const wxString& aName, const wxVariant& aValue )
{
    EDA_DRAW_FRAME* drawFrame = dynamic_cast<EDA_DRAW_FRAME*>( m_frame );

    if( !drawFrame || !drawFrame->GetToolManager() )
        return false;

    TOOL_MANAGER* toolMgr = drawFrame->GetToolManager();

    if( aName == wxS( "doc.units" ) )
    {
        switch( aValue.GetLong() )
        {
        case 1:  toolMgr->RunAction( ACTIONS::milsUnits );        break;
        case 2:  toolMgr->RunAction( ACTIONS::inchesUnits );      break;
        default: toolMgr->RunAction( ACTIONS::millimetersUnits ); break;
        }

        return true;
    }
    else if( aName == wxS( "doc.grid" ) )
    {
        toolMgr->RunAction( ACTIONS::gridPreset, static_cast<int>( aValue.GetLong() ) );
        return true;
    }
    else if( aName == wxS( "doc.gridVisible" ) )
    {
        drawFrame->SetGridVisibility( aValue.GetBool() );
        return true;
    }
    else if( aName == wxS( "doc.gridSnap" ) )
    {
        long mode = std::clamp( aValue.GetLong(), 0L, 2L );

        GAL_DISPLAY_OPTIONS_IMPL& options = drawFrame->GetGalDisplayOptions();
        options.m_gridSnapping = static_cast<KIGFX::GRID_SNAPPING>( mode );
        options.NotifyChanged();

        if( APP_SETTINGS_BASE* cfg = drawFrame->config() )
        {
            if( WINDOW_SETTINGS* window = drawFrame->GetWindowSettings( cfg ) )
                window->grid.snap = (int) mode;
        }

        return true;
    }
    else if( aName.StartsWith( wxS( "doc.gridMgr." ) ) )
    {
        // Grid Manager: ticking a grid makes it the snap grid (unticking the active one is
        // undone when the rows refresh)
        long idx = 0;

        if( aValue.GetBool() && aName.Mid( 12 ).ToLong( &idx ) )
            toolMgr->RunAction( ACTIONS::gridPreset, static_cast<int>( idx ) );

        return true;
    }
    else if( aName == wxS( "doc.fastGrid1" ) || aName == wxS( "doc.fastGrid2" ) )
    {
        if( APP_SETTINGS_BASE* cfg = drawFrame->config() )
        {
            if( WINDOW_SETTINGS* window = drawFrame->GetWindowSettings( cfg ) )
            {
                if( aName == wxS( "doc.fastGrid1" ) )
                    window->grid.fast_grid_1 = (int) aValue.GetLong();
                else
                    window->grid.fast_grid_2 = (int) aValue.GetLong();
            }
        }

        return true;
    }
    else if( aName == wxS( "doc.gridEdit" ) )
    {
        return true;    // the row's button opens the grid dialog; its text isn't editable
    }

    return false;
}


namespace
{
/// A read-only row with a "..." button that runs an action (e.g. the grid dialog).
class DOC_BUTTON_PROPERTY : public wxStringProperty
{
public:
    DOC_BUTTON_PROPERTY( const wxString& aLabel, const wxString& aName, const wxString& aValue,
                         std::function<void()> aOnClick ) :
            wxStringProperty( aLabel, aName, aValue ),
            m_onClick( std::move( aOnClick ) )
    {
    }

    const wxPGEditor* DoGetEditorClass() const override { return wxPGEditor_TextCtrlAndButton; }

    using wxStringProperty::StringToValue;

    bool StringToValue( wxVariant&, const wxString&, wxPGPropValFormatFlags ) const override
    {
        return false;   // typing doesn't change it
    }

    bool OnEvent( wxPropertyGrid* aGrid, wxWindow* aPrimary, wxEvent& aEvent ) override
    {
        if( aEvent.GetEventType() == wxEVT_BUTTON )
        {
            if( m_onClick )
                aGrid->CallAfter( m_onClick );

            return false;
        }

        return wxStringProperty::OnEvent( aGrid, aPrimary, aEvent );
    }

private:
    std::function<void()> m_onClick;
};
}


void PROPERTIES_PANEL::appendGridManager( EDA_DRAW_FRAME* aFrame )
{
    APP_SETTINGS_BASE* cfg = aFrame->config();
    WINDOW_SETTINGS*   window = cfg ? aFrame->GetWindowSettings( cfg ) : nullptr;

    if( !window )
        return;

    wxArrayString grids;
    GRID_MENU::BuildChoiceList( &grids, window, aFrame );

    // Rebuild the section when the list of grids changed (e.g. after the grid dialog)
    if( wxPGProperty* existing = m_grid->GetPropertyByName( wxS( "doc.gridManager" ) ) )
    {
        bool same = !m_grid->GetPropertyByName( wxString::Format( wxS( "doc.gridMgr.%zu" ),
                                                                  grids.size() ) );

        for( size_t ii = 0; same && ii < grids.size(); ++ii )
        {
            wxPGProperty* row = m_grid->GetPropertyByName( wxString::Format( wxS( "doc.gridMgr.%zu" ),
                                                                             ii ) );
            same = row && row->GetLabel() == grids[ii];
        }

        if( !same )
            m_grid->DeleteProperty( existing );
    }

    wxPGProperty* manager = docCategory( wxS( "doc.gridManager" ), _( "Grid Manager" ) );
    int           active = std::clamp( window->grid.last_size_idx, 0, (int) grids.size() - 1 );

    for( size_t ii = 0; ii < grids.size(); ++ii )
    {
        docBool( manager, wxString::Format( wxS( "doc.gridMgr.%zu" ), ii ), grids[ii],
                 (int) ii == active );
    }

    if( !grids.IsEmpty() )
    {
        docChoice( manager, wxS( "doc.fastGrid1" ), _( "Fast Grid 1" ), grids,
                   std::clamp( window->grid.fast_grid_1, 0, (int) grids.size() - 1 ) );
        docChoice( manager, wxS( "doc.fastGrid2" ), _( "Fast Grid 2" ), grids,
                   std::clamp( window->grid.fast_grid_2, 0, (int) grids.size() - 1 ) );
    }

    if( !m_grid->GetPropertyByName( wxS( "doc.gridEdit" ) ) )
    {
        m_grid->AppendIn( manager, new DOC_BUTTON_PROPERTY(
                _( "Edit Grids" ), wxS( "doc.gridEdit" ), _( "Grid properties..." ),
                [this, aFrame]()
                {
                    aFrame->GetToolManager()->RunAction( ACTIONS::gridProperties );
                    UpdateData();
                } ) );
    }
}


bool PROPERTIES_PANEL::getItemValue( EDA_ITEM* aItem, PROPERTY_BASE* aProperty, wxVariant& aValue )
{
    const wxAny& any = aItem->Get( aProperty );
    bool converted = false;

    if( aProperty->HasChoices() )
    {
        // handle enums as ints, since there are no default conversion functions for wxAny
        int tmp;
        converted = any.GetAs<int>( &tmp );

        if( converted )
            aValue = wxVariant( tmp );
    }

    if( !converted )                // all other types
        converted = any.GetAs( &aValue );

    if( !converted )
    {
        wxString propName = aProperty->Name();
        propName.Replace( ' ', '_' );
        wxFAIL_MSG( wxString::Format( wxS( "Could not convert wxAny to wxVariant for %s::%s" ),
                                      aItem->GetClass(),
                                      propName ) );
    }

    return converted;
}


bool PROPERTIES_PANEL::extractValueAndWritability( const SELECTION& aSelection, const wxString& aPropName,
                                                   wxVariant& aValue, bool& aWritable, wxPGChoices& aChoices )
{
    PROPERTY_MANAGER& propMgr = PROPERTY_MANAGER::Instance();
    bool              different = false;
    bool              first = true;

    aWritable = true;

    for( EDA_ITEM* item : aSelection )
    {
        PROPERTY_BASE* property = propMgr.GetProperty( TYPE_HASH( *item ), aPropName );

        if( !property )
            return false;

        if( !propMgr.IsAvailableFor( TYPE_HASH( *item ), property, item ) )
            return false;

        if( property->IsHiddenFromPropertiesManager() )
            return false;

        wxPGChoices choices = property->GetChoices( item );

        if( first )
        {
            aChoices = choices;
            first = false;
        }
        else
        {
            wxArrayString labels = choices.GetLabels();
            wxArrayInt    values = choices.GetValuesForStrings( labels );

            if( labels != aChoices.GetLabels() || values != aChoices.GetValuesForStrings( labels ) )
                return false;
        }

        // If read-only for any of the selection, read-only for the whole selection.
        if( !propMgr.IsWriteableFor( TYPE_HASH( *item ), property, item ) )
            aWritable = false;

        wxVariant value;

        if( getItemValue( item, property, value ) )
        {
            // Null value indicates different property values between items
            if( !different && !aValue.IsNull() && value != aValue )
            {
                different = true;
                aValue.MakeNull();
            }
            else if( !different )
            {
                aValue = value;
            }
        }
        else
        {
            // getItemValue returned false -- not available for this item
            return false;
        }
    }

    return true;
}


void PROPERTIES_PANEL::onShow( wxShowEvent& aEvent )
{
    if( aEvent.IsShown() )
        UpdateData();

    aEvent.Skip();
}


void PROPERTIES_PANEL::onCharHook( wxKeyEvent& aEvent )
{
    if( aEvent.GetKeyCode() == WXK_TAB
        && ( aEvent.GetModifiers() == wxMOD_NONE || aEvent.GetModifiers() == wxMOD_SHIFT ) )
    {
        // wxPropertyGrid hard-codes Tab to focus the current editor and then navigate out of
        // the grid, so it never steps between properties.  Intercept Tab here to commit any
        // pending edit and move selection to the next (or previous, with Shift) editable
        // property, wrapping around at the ends.
        m_grid->CommitChangesFromEditor();

        const bool forward = !aEvent.ShiftDown();

        auto isTabStop =
                []( wxPGProperty* aProp )
                {
                    return aProp && !aProp->IsCategory() && aProp->IsVisible() && aProp->IsEnabled()
                           && !aProp->HasFlag( wxPG_PROP_READONLY ) && aProp->GetEditorClass();
                };

        auto findTarget =
                [&]( wxPropertyGridIterator aIt )
                {
                    while( !aIt.AtEnd() )
                    {
                        if( isTabStop( aIt.GetProperty() ) )
                            return aIt.GetProperty();

                        if( forward )
                            aIt.Next();
                        else
                            aIt.Prev();
                    }

                    return static_cast<wxPGProperty*>( nullptr );
                };

        wxPGProperty* current = m_grid->GetSelectedProperty();
        wxPGProperty* target = nullptr;

        if( current )
        {
            wxPropertyGridIterator it = m_grid->GetIterator( wxPG_ITERATE_VISIBLE, current );

            if( forward )
                it.Next();
            else
                it.Prev();

            target = findTarget( it );
        }

        // Wrap around (or pick a starting property if nothing is selected)
        if( !target )
        {
            wxPropertyGridIterator it = m_grid->GetIterator( wxPG_ITERATE_VISIBLE,
                                                             forward ? wxTOP : wxBOTTOM );
            target = findTarget( it );
        }

        if( target )
            m_grid->SelectProperty( target, true );
        else
            aEvent.Skip();

        return;
    }

    if( aEvent.GetKeyCode() == 'C' && aEvent.GetModifiers() == wxMOD_CONTROL )
    {
        if( wxPGProperty* prop = m_grid->GetSelectedProperty() )
        {
            if( wxTheClipboard->Open() )
            {
                wxTheClipboard->SetData( new wxTextDataObject( prop->GetValueAsString() ) );
                wxTheClipboard->Close();
                return;
            }
        }
    }

    if( aEvent.GetKeyCode() == WXK_SPACE )
    {
        if( wxPGProperty* prop = m_grid->GetSelectedProperty() )
        {
            if( prop->GetValueType() == wxT( "bool" ) )
            {
                m_grid->SetPropertyValue( prop, !prop->GetValue().GetBool() );
                return;
            }
        }
    }

    if( aEvent.GetKeyCode() == WXK_RETURN || aEvent.GetKeyCode() == WXK_NUMPAD_ENTER
        || aEvent.GetKeyCode() == WXK_DOWN || aEvent.GetKeyCode() == WXK_UP )
    {
        m_grid->CommitChangesFromEditor();

        CallAfter( [this]()
                   {
                       m_grid->SelectProperty( m_grid->GetSelectedProperty(), true );
                   } );
    }

    aEvent.Skip();
}


void PROPERTIES_PANEL::RecalculateSplitterPos()
{
    if( m_splitter_key_proportion < 0 )
        m_grid->CenterSplitter();
    else
        m_grid->SetSplitterPosition( m_splitter_key_proportion * m_grid->GetSize().x );
}


void PROPERTIES_PANEL::SetSplitterProportion( float aProportion )
{
    m_splitter_key_proportion = aProportion;
    RecalculateSplitterPos();
}
