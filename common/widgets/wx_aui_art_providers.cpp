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

#include <algorithm>
#include <wx/aui/aui.h>
#include <wx/aui/framemanager.h>
#include <wx/aui/auibook.h>
#include <wx/bitmap.h>
#include <wx/dc.h>
#include <wx/settings.h>

#include <kiplatform/ui.h>
#include <pgm_base.h>
#include <settings/common_settings.h>
#include <widgets/panel_notebook_base.h>
#include <widgets/wx_aui_art_providers.h>
#include <gal/color4d.h>


namespace
{
// Altium-style flat light palette, used whenever the system theme is light.
const wxColour ALT_HOVER_FILL( 229, 241, 251 );
const wxColour ALT_HOVER_BORDER( 204, 228, 247 );
const wxColour ALT_CHECKED_FILL( 204, 228, 247 );
const wxColour ALT_CHECKED_BORDER( 153, 201, 239 );
const wxColour ALT_PRESSED_FILL( 153, 201, 239 );

const wxColour ALT_CAPTION_BG( 225, 225, 225 );
const wxColour ALT_CAPTION_ACTIVE_BG( 209, 224, 241 );
const wxColour ALT_CAPTION_TEXT( 40, 40, 40 );
const wxColour ALT_SASH( 205, 205, 205 );
const wxColour ALT_BORDER( 190, 190, 190 );
}

#if wxCHECK_VERSION( 3, 3, 0 )
wxSize WX_AUI_TOOLBAR_ART::GetToolSize( wxReadOnlyDC& aDc, wxWindow* aWindow,
                                        const wxAuiToolBarItem& aItem )
#else
wxSize WX_AUI_TOOLBAR_ART::GetToolSize( wxDC& aDc, wxWindow* aWindow,
                                        const wxAuiToolBarItem& aItem )
#endif
{
    // Based on the upstream wxWidgets implementation, but simplified for our application
    int size = aWindow->FromDIP( Pgm().GetCommonSettings()->m_Appearance.toolbar_icon_size );

    int width = size;
    int height = size;

    if( ( m_flags & wxAUI_TB_TEXT ) && !aItem.GetLabel().empty() )
    {
        aDc.SetFont( m_font );
        int tx, ty;

        if( m_textOrientation == wxAUI_TBTOOL_TEXT_BOTTOM )
        {
            aDc.GetTextExtent( wxT( "ABCDHgj" ), &tx, &ty );
            height += ty;

            if( !aItem.GetLabel().empty() )
            {
                aDc.GetTextExtent( aItem.GetLabel(), &tx, &ty );
                width = wxMax( width, tx + aWindow->FromDIP( 6 ) );
            }
        }
        else if( m_textOrientation == wxAUI_TBTOOL_TEXT_RIGHT )
        {
            width += aWindow->FromDIP( 3 ); // space between left border and bitmap
            width += aWindow->FromDIP( 3 ); // space between bitmap and text

            if( !aItem.GetLabel().empty() )
            {
                aDc.GetTextExtent( aItem.GetLabel(), &tx, &ty );
                width += tx;
                height = wxMax( height, ty );
            }
        }
    }

    if( aItem.HasDropDown() )
    {
        int dropdownWidth = GetElementSize( wxAUI_TBART_DROPDOWN_SIZE );
        width += dropdownWidth + aWindow->FromDIP( 4 );
    }

    return wxSize( width, height );
}


void WX_AUI_TOOLBAR_ART::DrawButton( wxDC& aDc, wxWindow* aWindow, const wxAuiToolBarItem& aItem,
                                     const wxRect& aRect )
{
    // Based on upstream implementation
    int bmpX = 0, bmpY = 0;
    int textX = 0, textY = 0;

    const wxBitmap& bmp = aItem.GetCurrentBitmapFor( aWindow );
    const wxSize    bmpSize = bmp.IsOk() ? bmp.GetLogicalSize() : wxSize( 0, 0 );

    if( ( m_flags & wxAUI_TB_TEXT ) && !aItem.GetLabel().empty() )
    {
        aDc.SetFont( m_font );

        int textWidth = 0, textHeight = 0;
        int tx, ty;

        aDc.GetTextExtent( wxT( "ABCDHgj" ), &tx, &textHeight );
        aDc.GetTextExtent( aItem.GetLabel(), &textWidth, &ty );

        if( m_textOrientation == wxAUI_TBTOOL_TEXT_BOTTOM )
        {
            bmpX = aRect.x + ( aRect.width / 2 ) - ( bmpSize.x / 2 );

            bmpY = aRect.y + ( ( aRect.height - textHeight ) / 2 ) - ( bmpSize.y / 2 );

            textX = aRect.x + ( aRect.width / 2 ) - ( textWidth / 2 ) + 1;
            textY = aRect.y + aRect.height - textHeight - 1;
        }
        else if( m_textOrientation == wxAUI_TBTOOL_TEXT_RIGHT )
        {
            bmpX = aRect.x + aWindow->FromDIP( 3 );

            bmpY = aRect.y + ( aRect.height / 2 ) - ( bmpSize.y / 2 );

            textX = bmpX + aWindow->FromDIP( 3 ) + bmpSize.x;
            textY = aRect.y + ( aRect.height / 2 ) - ( textHeight / 2 );
        }
    }
    else
    {
        bmpX = aRect.x + ( aRect.width / 2 ) - ( bmpSize.x / 2 );
        bmpY = aRect.y + ( aRect.height / 2 ) - ( bmpSize.y / 2 );
    }

    bool isThemeDark = KIPLATFORM::UI::IsDarkTheme();

    if( !isThemeDark && !( aItem.GetState() & wxAUI_BUTTON_STATE_DISABLED ) )
    {
        int state = aItem.GetState();

        if( state & wxAUI_BUTTON_STATE_PRESSED )
        {
            aDc.SetPen( wxPen( ALT_CHECKED_BORDER ) );
            aDc.SetBrush( wxBrush( ALT_PRESSED_FILL ) );
            aDc.DrawRectangle( aRect );
        }
        else if( state & wxAUI_BUTTON_STATE_CHECKED )
        {
            aDc.SetPen( wxPen( ALT_CHECKED_BORDER ) );
            aDc.SetBrush( wxBrush( ALT_CHECKED_FILL ) );
            aDc.DrawRectangle( aRect );
        }
        else if( ( state & wxAUI_BUTTON_STATE_HOVER ) || aItem.IsSticky() )
        {
            aDc.SetPen( wxPen( ALT_HOVER_BORDER ) );
            aDc.SetBrush( wxBrush( ALT_HOVER_FILL ) );
            aDc.DrawRectangle( aRect );
        }
    }
    else if( !( aItem.GetState() & wxAUI_BUTTON_STATE_DISABLED ) )
    {
        if( aItem.GetState() & wxAUI_BUTTON_STATE_PRESSED )
        {
            aDc.SetPen( wxPen( m_highlightColour ) );
            aDc.SetBrush( wxBrush( m_highlightColour.ChangeLightness( isThemeDark ? 20 : 150 ) ) );
            aDc.DrawRectangle( aRect );
        }
        else if( ( aItem.GetState() & wxAUI_BUTTON_STATE_HOVER ) || aItem.IsSticky() )
        {
            aDc.SetPen( wxPen( m_highlightColour ) );
            aDc.SetBrush( wxBrush( m_highlightColour.ChangeLightness( isThemeDark ? 40 : 170 ) ) );

            // draw an even lighter background for checked item hovers (since
            // the hover background is the same color as the check background)
            if( aItem.GetState() & wxAUI_BUTTON_STATE_CHECKED )
                aDc.SetBrush(
                        wxBrush( m_highlightColour.ChangeLightness( isThemeDark ? 50 : 180 ) ) );

            aDc.DrawRectangle( aRect );
        }
        else if( aItem.GetState() & wxAUI_BUTTON_STATE_CHECKED )
        {
            // it's important to put this code in an else statement after the
            // hover, otherwise hovers won't draw properly for checked items
            aDc.SetPen( wxPen( m_highlightColour ) );
            aDc.SetBrush( wxBrush( m_highlightColour.ChangeLightness( isThemeDark ? 40 : 170 ) ) );
            aDc.DrawRectangle( aRect );
        }
    }

    if( bmp.IsOk() )
        aDc.DrawBitmap( bmp, bmpX, bmpY, true );

    // set the item's text color based on if it is disabled
    aDc.SetTextForeground( wxSystemSettings::GetColour( wxSYS_COLOUR_BTNTEXT ) );

    if( aItem.GetState() & wxAUI_BUTTON_STATE_DISABLED )
    {
        aDc.SetTextForeground( wxSystemSettings::GetColour( wxSYS_COLOUR_GRAYTEXT ) );
    }

    if( ( m_flags & wxAUI_TB_TEXT ) && !aItem.GetLabel().empty() )
    {
        aDc.DrawText( aItem.GetLabel(), textX, textY );
    }
}


void WX_AUI_TOOLBAR_ART::saturateHighlightColor()
{
#ifdef __WXOSX__
    // Use a slightly stronger highlight colour over grey toolbar backgrounds
    KIGFX::COLOR4D highlight( m_highlightColour );
    m_highlightColour = highlight.Saturate( 0.6 ).ToColour();
#endif
}


void WX_AUI_TOOLBAR_ART::UpdateColoursFromSystem()
{
    wxAuiDefaultToolBarArt::UpdateColoursFromSystem();
    saturateHighlightColor();
}


class ToolbarCommandCapture : public wxEvtHandler
{
public:
    ToolbarCommandCapture() { m_lastId = 0; }
    int GetCommandId() const { return m_lastId; }

    bool ProcessEvent( wxEvent& evt ) override
    {
        if( evt.GetEventType() == wxEVT_MENU )
        {
            m_lastId = evt.GetId();
            return true;
        }

        if( GetNextHandler() )
            return GetNextHandler()->ProcessEvent( evt );

        return false;
    }

private:
    int m_lastId;
};


int WX_AUI_TOOLBAR_ART::ShowDropDown( wxWindow* wnd, const wxAuiToolBarItemArray& items )
{
    wxMenu menuPopup;
    bool   skipNextSeparator = true;

    size_t i, count = items.GetCount();
    for( i = 0; i < count; ++i )
    {
        wxAuiToolBarItem& item = items.Item( i );

        if( item.GetKind() == wxITEM_SEPARATOR )
        {
            if( !skipNextSeparator )
            {
                menuPopup.AppendSeparator();
                skipNextSeparator = true;
            }
        }
        else if( item.GetKind() == wxITEM_NORMAL || item.GetKind() == wxITEM_CHECK || item.GetKind() == wxITEM_RADIO )
        {
            wxString text = item.GetShortHelp();

            if( text.empty() )
                text = item.GetLabel();

            if( text.empty() )
                text = wxT( " " );

            wxString firstLine = text.BeforeFirst( '\n' );
            wxString accel;
            wxString label = firstLine.BeforeFirst( '\t', &accel );

            text = label;

            if( !accel.empty() )
            {
                // Remove brackets from accelerator string so it's recognized
                if( accel.starts_with( "(" ) && accel.ends_with( ")" ) )
                    accel = accel.Mid( 1, accel.size() - 2 );

                text << "\t" << accel;
            }

            bool       checked = item.GetState() & wxAUI_BUTTON_STATE_CHECKED;
            wxItemKind menuKind = wxITEM_NORMAL;

            if( ( item.GetKind() == wxITEM_CHECK || item.GetKind() == wxITEM_RADIO ) && checked )
                menuKind = static_cast<wxItemKind>( item.GetKind() );

            wxMenuItem* m = new wxMenuItem( &menuPopup, item.GetId(), text, item.GetShortHelp(), menuKind );

            if( !m->IsCheckable() )
                m->SetBitmap( item.GetBitmapBundle() );

            menuPopup.Append( m );

            if( m->IsCheckable() )
                m->Check( checked );

            skipNextSeparator = false;
        }
    }

    // find out where to put the popup menu of window items
    wxPoint pt = ::wxGetMousePosition();
    pt = wnd->ScreenToClient( pt );

    // find out the screen coordinate at the bottom of the tab ctrl
    wxRect cli_rect = wnd->GetClientRect();
    pt.y = cli_rect.y + cli_rect.height;

    ToolbarCommandCapture* cc = new ToolbarCommandCapture;
    wnd->PushEventHandler( cc );
    wnd->PopupMenu( &menuPopup, pt );
    int command = cc->GetCommandId();
    wnd->PopEventHandler( true );

    return command;
}


WX_AUI_DOCK_ART::WX_AUI_DOCK_ART() :
        wxAuiDefaultDockArt()
{
#if defined( _WIN32 )
    // Use normal control font, wx likes to use "small"
    m_captionFont = *wxNORMAL_FONT;
    m_captionFont.MakeBold();   // Altium's panel titles are bold

    // Increase the box the caption rests in size a bit
    m_captionSize = ( wxNORMAL_FONT->GetPointSize() * 7 ) / 4 + 6;
#endif

    SetColour( wxAUI_DOCKART_ACTIVE_CAPTION_TEXT_COLOUR,
               wxSystemSettings::GetColour( wxSYS_COLOUR_BTNTEXT ) );
    SetColour( wxAUI_DOCKART_INACTIVE_CAPTION_TEXT_COLOUR,
               wxSystemSettings::GetColour( wxSYS_COLOUR_BTNTEXT ) );

    // Turn off the ridiculous looking gradient
    m_gradientType = wxAUI_GRADIENT_NONE;

    if( !KIPLATFORM::UI::IsDarkTheme() )
    {
        // Flat Altium-style pane captions and sashes
        SetColour( wxAUI_DOCKART_INACTIVE_CAPTION_COLOUR, ALT_CAPTION_BG );
        SetColour( wxAUI_DOCKART_ACTIVE_CAPTION_COLOUR, ALT_CAPTION_ACTIVE_BG );
        SetColour( wxAUI_DOCKART_INACTIVE_CAPTION_TEXT_COLOUR, ALT_CAPTION_TEXT );
        SetColour( wxAUI_DOCKART_ACTIVE_CAPTION_TEXT_COLOUR, ALT_CAPTION_TEXT );
        SetColour( wxAUI_DOCKART_SASH_COLOUR, ALT_SASH );
        SetColour( wxAUI_DOCKART_BORDER_COLOUR, ALT_BORDER );
        SetMetric( wxAUI_DOCKART_SASH_SIZE, 4 );
    }
    else
    {
        // Altium's dark theme: grey caption bars (steel blue on the focused panel) and dark
        // gaps between panels
        SetColour( wxAUI_DOCKART_INACTIVE_CAPTION_COLOUR, wxColour( 0x4E, 0x4E, 0x4E ) );
        SetColour( wxAUI_DOCKART_ACTIVE_CAPTION_COLOUR, wxColour( 0x62, 0x83, 0xA2 ) );
        SetColour( wxAUI_DOCKART_INACTIVE_CAPTION_TEXT_COLOUR, wxColour( 0xE5, 0xE5, 0xE5 ) );
        SetColour( wxAUI_DOCKART_ACTIVE_CAPTION_TEXT_COLOUR, wxColour( 0xFF, 0xFF, 0xFF ) );
        SetColour( wxAUI_DOCKART_SASH_COLOUR, wxColour( 0x35, 0x35, 0x35 ) );
        SetColour( wxAUI_DOCKART_BACKGROUND_COLOUR, wxColour( 0x35, 0x35, 0x35 ) );
        SetColour( wxAUI_DOCKART_BORDER_COLOUR, wxColour( 0x22, 0x22, 0x22 ) );
        SetMetric( wxAUI_DOCKART_SASH_SIZE, 4 );
    }
}

void WX_AUI_DOCK_ART::DrawPaneButton( wxDC& aDC, wxWindow* aWindow, int aButton, int aButtonState,
                                      const wxRect& aRect, wxAuiPaneInfo& aPane )
{
    if( aButton != wxAUI_BUTTON_MAXIMIZE_RESTORE && aButton != wxAUI_BUTTON_PIN )
    {
        wxAuiDefaultDockArt::DrawPaneButton( aDC, aWindow, aButton, aButtonState, aRect, aPane );
        return;
    }

    bool     active = aPane.HasFlag( wxAuiPaneInfo::optionActive );
    wxColour ink = GetColour( active ? wxAUI_DOCKART_ACTIVE_CAPTION_TEXT_COLOUR
                                     : wxAUI_DOCKART_INACTIVE_CAPTION_TEXT_COLOUR );

    if( aButtonState == wxAUI_BUTTON_STATE_HOVER || aButtonState == wxAUI_BUTTON_STATE_PRESSED )
    {
        wxColour bg = GetColour( active ? wxAUI_DOCKART_ACTIVE_CAPTION_COLOUR
                                        : wxAUI_DOCKART_INACTIVE_CAPTION_COLOUR );
        aDC.SetPen( *wxTRANSPARENT_PEN );
        aDC.SetBrush( wxBrush( bg.ChangeLightness( aButtonState == wxAUI_BUTTON_STATE_PRESSED ? 80
                                                                                              : 120 ) ) );
        wxRect hot( aRect );
        aDC.DrawRectangle( hot.Deflate( 1 ) );
    }

    int      cx = aRect.x + aRect.width / 2;
    int      cy = aRect.y + aRect.height / 2;
    int      u = std::max( 2, std::min( aRect.width, aRect.height ) / 6 );

    aDC.SetPen( wxPen( ink ) );
    aDC.SetBrush( wxBrush( ink ) );

    if( aButton == wxAUI_BUTTON_MAXIMIZE_RESTORE )
    {
        // Drop-down arrow: the panels of this group
        wxPoint tri[3] = { wxPoint( cx - 2 * u, cy - u ), wxPoint( cx + 2 * u, cy - u ),
                           wxPoint( cx, cy + u + 1 ) };
        aDC.DrawPolygon( 3, tri );
    }
    else
    {
        // Upright pin: head, collar and point
        aDC.DrawRectangle( cx - u, cy - 2 * u - 1, 2 * u + 1, 2 * u );
        aDC.DrawRectangle( cx - 2 * u, cy, 4 * u + 1, std::max( 1, u / 2 + 1 ) );
        aDC.DrawLine( cx, cy, cx, cy + 2 * u + 1 );
    }
}


#if wxCHECK_VERSION( 3, 3, 0 )
void WX_AUI_TAB_ART::SetSizingInfo( const wxSize& tabCtrlSize, size_t tabCount, wxWindow* wnd )
{
    // SetSizingInfo is called after adding pages.
    // Remove the close button from tabs of unclosable panels.
    PANEL_NOTEBOOK_BASE* panel = dynamic_cast<PANEL_NOTEBOOK_BASE*>( wnd );

    if( panel && !panel->GetClosable() )
    {
        if( wxAuiNotebook* notebook = dynamic_cast<wxAuiNotebook*>( panel->GetParent() ) )
        {
            for( wxAuiTabCtrl* ctrl : notebook->GetAllTabCtrls() )
            {
                for( size_t i = 0; i < ctrl->GetPageCount(); i++ )
                {
                    wxAuiNotebookPage& page = ctrl->GetPage( i );

                    if( page.window == wnd )
                        page.buttons.clear();
                }
            }
        }
    }

    wxAuiDefaultTabArt::SetSizingInfo( tabCtrlSize, tabCount, wnd );
}
#else
void WX_AUI_TAB_ART::DrawTab( wxDC& dc, wxWindow* wnd, const wxAuiNotebookPage& page, const wxRect& in_rect,
                              int close_button_state, wxRect* out_tab_rect, wxRect* out_button_rect,
                              int* x_extent )
{
    PANEL_NOTEBOOK_BASE* panel = dynamic_cast<PANEL_NOTEBOOK_BASE*>( page.window );

    if( panel && !panel->GetClosable() )
        close_button_state = wxAUI_BUTTON_STATE_HIDDEN;

    return wxAuiGenericTabArt::DrawTab( dc, wnd, page, in_rect, close_button_state, out_tab_rect,
                                        out_button_rect, x_extent );
}
#endif
