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

#include <widgets/panel_frame_theme.h>

#include <algorithm>
#include <set>

#include <wx/aui/framemanager.h>
#include <wx/bmpbuttn.h>
#include <wx/button.h>
#include <wx/splitter.h>
#include <wx/dataview.h>
#include <wx/grid.h>
#include <wx/listbox.h>
#include <wx/listctrl.h>
#include <wx/panel.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/srchctrl.h>
#include <wx/treectrl.h>
#include <wx/treelist.h>
#include <wx/html/htmlwin.h>

#ifdef __WXMSW__
#include <windows.h>
#include <commctrl.h>
#endif


namespace
{
#ifdef __WXMSW__
const UINT_PTR FRAME_SUBCLASS_ID = 0x41465246;   // "AFRF"

/// Paints a framed control's 1 px border in Altium's near-black instead of Windows' grey.
LRESULT CALLBACK frameProc( HWND aHwnd, UINT aMsg, WPARAM aWParam, LPARAM aLParam, UINT_PTR aId,
                            DWORD_PTR aData )
{
    switch( aMsg )
    {
    case WM_NCPAINT:
    {
        LRESULT result = DefSubclassProc( aHwnd, aMsg, aWParam, aLParam );   // scrollbars

        if( HDC dc = GetWindowDC( aHwnd ) )
        {
            RECT r;
            GetWindowRect( aHwnd, &r );
            OffsetRect( &r, -r.left, -r.top );

            HBRUSH brush = CreateSolidBrush( RGB( 0x22, 0x22, 0x22 ) );
            FrameRect( dc, &r, brush );
            DeleteObject( brush );
            ReleaseDC( aHwnd, dc );
        }

        return result;
    }

    case WM_NCDESTROY:
        RemoveWindowSubclass( aHwnd, frameProc, aId );
        break;
    }

    return DefSubclassProc( aHwnd, aMsg, aWParam, aLParam );
}
#endif

const wxColour SEARCH_FILL( 0x2E, 0x2E, 0x2E );    // Altium's search box, darker than lists
const wxColour BUTTON_FILL( 0x54, 0x54, 0x54 );    // Altium's panel buttons

std::set<wxWindow*>& processed()
{
    static std::set<wxWindow*> s_processed;
    return s_processed;
}


/// The controls Altium frames: lists, trees, grids, HTML/detail views and search boxes.
bool isFramed( wxWindow* aWin )
{
    return dynamic_cast<wxTreeCtrl*>( aWin ) || dynamic_cast<wxListCtrl*>( aWin )
           || dynamic_cast<wxDataViewCtrl*>( aWin ) || dynamic_cast<wxTreeListCtrl*>( aWin )
           || dynamic_cast<wxGrid*>( aWin ) || dynamic_cast<wxListBox*>( aWin )
           || dynamic_cast<wxHtmlWindow*>( aWin ) || dynamic_cast<wxSearchCtrl*>( aWin );
}


/// Frames the lists, trees and search boxes below aRoot with a 1 px border (as Altium frames
/// its panel contents), and gives search boxes Altium's darker fill.
void frameControls( wxWindow* aRoot )
{
    for( wxWindow* child : aRoot->GetChildren() )
    {
        if( child->IsTopLevel() )
            continue;

        if( isFramed( child ) )
        {
            long style = child->GetWindowStyleFlag();

            if( ( style & wxBORDER_MASK ) != wxBORDER_SIMPLE )
                child->SetWindowStyleFlag( ( style & ~wxBORDER_MASK ) | wxBORDER_SIMPLE );

            if( wxSearchCtrl* search = dynamic_cast<wxSearchCtrl*>( child ) )
                search->SetBackgroundColour( SEARCH_FILL );

            // Report lists fit their panel: the last column takes the width the others leave,
            // so there is no horizontal scrollbar (as in Altium)
            if( wxListCtrl* list = dynamic_cast<wxListCtrl*>( child ) )
            {
                if( list->InReportView() )
                {
                    list->Bind( wxEVT_SIZE,
                                [list]( wxSizeEvent& aEvent )
                                {
                                    aEvent.Skip();

                                    int last = list->GetColumnCount() - 1;

                                    if( last < 0 )
                                        return;

                                    int others = 0;

                                    for( int col = 0; col < last; ++col )
                                        others += list->GetColumnWidth( col );

                                    int client = list->GetClientSize().x;
                                    int minLast = list->FromDIP( 40 );

                                    if( others + minLast > client && others > 0 && client > minLast )
                                    {
                                        // Too narrow even so: every column shrinks in proportion
                                        double factor = double( client - minLast ) / others;
                                        others = 0;

                                        for( int col = 0; col < last; ++col )
                                        {
                                            int w = std::max( list->FromDIP( 24 ),
                                                              int( list->GetColumnWidth( col ) * factor ) );
                                            list->SetColumnWidth( col, w );
                                            others += w;
                                        }
                                    }

                                    list->SetColumnWidth( last, std::max( client - others, minLast ) );
                                } );
                }
            }

#ifdef __WXMSW__
            DWORD_PTR data = 0;
            HWND      hwnd = (HWND) child->GetHWND();

            if( hwnd && !GetWindowSubclass( hwnd, frameProc, FRAME_SUBCLASS_ID, &data ) )
            {
                SetWindowSubclass( hwnd, frameProc, FRAME_SUBCLASS_ID, 0 );
                SetWindowPos( hwnd, nullptr, 0, 0, 0, 0,
                              SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED );
            }
#endif

            child->Refresh();
        }
        else if( wxButton* button = dynamic_cast<wxButton*>( child ) )
        {
            // Altium's flat grey panel buttons (wx owner-draws a button given colours)
            if( !dynamic_cast<wxBitmapButton*>( button ) || !button->GetLabel().IsEmpty() )
            {
                button->SetBackgroundColour( BUTTON_FILL );
                button->SetForegroundColour( wxSystemSettings::GetColour( wxSYS_COLOUR_BTNTEXT ) );
            }
        }
        else if( dynamic_cast<wxPanel*>( child ) || dynamic_cast<wxSplitterWindow*>( child ) )
        {
            frameControls( child );
        }
    }
}
} // namespace


void ApplyAltiumPanelFrames( wxAuiManager& aAuiMgr )
{
    if( !wxSystemSettings::GetAppearance().IsDark() )
        return;

    wxAuiPaneInfoArray& panes = aAuiMgr.GetAllPanes();

    for( size_t ii = 0; ii < panes.GetCount(); ++ii )
    {
        wxAuiPaneInfo& pane = panes.Item( ii );
        wxWindow*      win = pane.window;

        if( !win || pane.IsToolbar() || !pane.HasCaption() || pane.name == wxS( "DrawFrame" ) )
            continue;

        if( processed().count( win ) )
            continue;

        wxSizer* inner = win->GetSizer();

        // Only panels built from a sizer (a bare tree or list docked directly is left alone)
        if( !inner || !dynamic_cast<wxPanel*>( win ) )
            continue;

        processed().insert( win );
        win->Bind( wxEVT_DESTROY,
                   [win]( wxWindowDestroyEvent& aEvent )
                   {
                       if( aEvent.GetEventObject() == win )
                           processed().erase( win );

                       aEvent.Skip();
                   } );

        // The panel body in the chrome grey, its contents inset as in Altium
        wxBoxSizer* outer = new wxBoxSizer( wxVERTICAL );
        win->SetSizer( outer, false );
        outer->Add( inner, 1, wxEXPAND | wxALL, win->FromDIP( 6 ) );

        win->SetBackgroundColour( wxSystemSettings::GetColour( wxSYS_COLOUR_BTNFACE ) );
        frameControls( win );
        win->Layout();
        win->Refresh();
    }
}
