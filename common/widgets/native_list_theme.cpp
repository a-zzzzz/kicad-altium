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

#include <widgets/native_list_theme.h>

#include <wx/listctrl.h>
#include <wx/settings.h>
#include <wx/window.h>

#ifdef __WXMSW__
#include <windows.h>
#include <commctrl.h>
#include <wx/msw/private.h>


namespace
{
const UINT_PTR SUBCLASS_ID = 0x41544859;   // "ATHY"

COLORREF toRef( const wxColour& aColour )
{
    return RGB( aColour.Red(), aColour.Green(), aColour.Blue() );
}


wxColour mix( const wxColour& a, const wxColour& b, double t )
{
    return wxColour( a.Red() + ( b.Red() - a.Red() ) * t, a.Green() + ( b.Green() - a.Green() ) * t,
                     a.Blue() + ( b.Blue() - a.Blue() ) * t );
}


/// Paints a header control the way Altium draws its column headers.
void paintHeader( HWND aHwnd )
{
    PAINTSTRUCT ps;
    HDC         dc = BeginPaint( aHwnd, &ps );
    RECT        client;

    GetClientRect( aHwnd, &client );

    wxColour face = wxSystemSettings::GetColour( wxSYS_COLOUR_BTNFACE );
    wxColour fill = mix( face, *wxWHITE, 0.07 );                  // #4E4E4E on #404040
    wxColour line = mix( mix( face, *wxBLACK, 0.17 ), *wxBLACK, 0.40 );
    wxColour text = wxSystemSettings::GetColour( wxSYS_COLOUR_BTNTEXT );

    HBRUSH fillBrush = CreateSolidBrush( toRef( fill ) );
    HBRUSH lineBrush = CreateSolidBrush( toRef( line ) );
    FillRect( dc, &client, fillBrush );

    HFONT font = (HFONT) SendMessage( aHwnd, WM_GETFONT, 0, 0 );
    HGDIOBJ oldFont = font ? SelectObject( dc, font ) : nullptr;

    SetBkMode( dc, TRANSPARENT );
    SetTextColor( dc, toRef( text ) );

    UINT dpi = GetDpiForWindow( aHwnd );
    int  pad = MulDiv( 6, dpi ? dpi : 96, 96 );
    int  count = Header_GetItemCount( aHwnd );

    for( int ii = 0; ii < count; ++ii )
    {
        RECT r;

        if( !Header_GetItemRect( aHwnd, ii, &r ) )
            continue;

        wchar_t label[256] = { 0 };
        HDITEMW item = {};
        item.mask = HDI_TEXT | HDI_FORMAT;
        item.pszText = label;
        item.cchTextMax = 255;
        SendMessage( aHwnd, HDM_GETITEMW, ii, (LPARAM) &item );

        RECT textRect = { r.left + pad, r.top, r.right - pad, r.bottom };
        UINT align = ( item.fmt & HDF_RIGHT ) ? DT_RIGHT
                     : ( item.fmt & HDF_CENTER ) ? DT_CENTER : DT_LEFT;

        DrawTextW( dc, label, -1, &textRect,
                   align | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX );

        // Sort arrow
        if( item.fmt & ( HDF_SORTUP | HDF_SORTDOWN ) )
        {
            int    s = pad / 2 + 1;
            int    cx = r.right - pad - s;
            int    cy = ( r.top + r.bottom ) / 2;
            bool   up = item.fmt & HDF_SORTUP;
            POINT  tri[3] = { { cx - s, up ? cy + s / 2 : cy - s / 2 },
                              { cx + s, up ? cy + s / 2 : cy - s / 2 },
                              { cx, up ? cy - s / 2 : cy + s / 2 } };
            HBRUSH textBrush = CreateSolidBrush( toRef( text ) );
            HGDIOBJ oldBrush = SelectObject( dc, textBrush );
            HGDIOBJ oldPen = SelectObject( dc, GetStockObject( NULL_PEN ) );
            Polygon( dc, tri, 3 );
            SelectObject( dc, oldPen );
            SelectObject( dc, oldBrush );
            DeleteObject( textBrush );
        }

        // Divider at the column's right edge
        RECT divider = { r.right - 1, r.top, r.right, r.bottom };
        FillRect( dc, &divider, lineBrush );
    }

    RECT bottom = { client.left, client.bottom - 1, client.right, client.bottom };
    FillRect( dc, &bottom, lineBrush );

    if( oldFont )
        SelectObject( dc, oldFont );

    DeleteObject( fillBrush );
    DeleteObject( lineBrush );
    EndPaint( aHwnd, &ps );
}


LRESULT CALLBACK headerProc( HWND aHwnd, UINT aMsg, WPARAM aWParam, LPARAM aLParam,
                             UINT_PTR aId, DWORD_PTR aData )
{
    switch( aMsg )
    {
    case WM_PAINT:
        if( wxSystemSettings::GetAppearance().IsDark() )
        {
            paintHeader( aHwnd );
            return 0;
        }

        break;

    case WM_ERASEBKGND:
        if( wxSystemSettings::GetAppearance().IsDark() )
            return 1;

        break;

    case WM_NCDESTROY:
        RemoveWindowSubclass( aHwnd, headerProc, aId );
        break;
    }

    return DefSubclassProc( aHwnd, aMsg, aWParam, aLParam );
}


BOOL CALLBACK themeChild( HWND aHwnd, LPARAM aParam )
{
    wchar_t cls[64] = { 0 };
    GetClassNameW( aHwnd, cls, 63 );

    if( wcscmp( cls, WC_HEADERW ) == 0 )
    {
        DWORD_PTR data = 0;

        if( !GetWindowSubclass( aHwnd, headerProc, SUBCLASS_ID, &data ) )
        {
            SetWindowSubclass( aHwnd, headerProc, SUBCLASS_ID, 0 );
            InvalidateRect( aHwnd, nullptr, TRUE );
        }
    }
    else if( wcscmp( cls, WC_LISTVIEWW ) == 0 )
    {
        // Report lists: the panel colour rather than the near-black list colour
        if( wxListCtrl* list = dynamic_cast<wxListCtrl*>( wxFindWinFromHandle( aHwnd ) ) )
        {
            // Lists left at Windows' near-black dark-mode colour; lighter ones keep theirs
            wxColour panel = wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOW );
            wxColour bg = list->GetBackgroundColour();

            if( bg.GetLuminance() < panel.GetLuminance() - 0.01 )
            {
                list->SetBackgroundColour( panel );
                list->Refresh();
            }
        }
    }

    return TRUE;
}
} // namespace


void ApplyNativeListTheme( wxWindow* aRoot )
{
    if( !aRoot || !aRoot->GetHWND() || !wxSystemSettings::GetAppearance().IsDark() )
        return;

    EnumChildWindows( (HWND) aRoot->GetHWND(), themeChild, 0 );
}

#else

void ApplyNativeListTheme( wxWindow* aRoot )
{
}

#endif
