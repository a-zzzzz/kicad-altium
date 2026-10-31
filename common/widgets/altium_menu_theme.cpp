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

#include <widgets/altium_menu_theme.h>

#ifdef __WXMSW__

#include <wx/msw/wrapwin.h>
#include <commctrl.h>

#include <wx/image.h>
#include <wx/menu.h>

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#pragma comment( lib, "comctl32.lib" )
#pragma comment( lib, "msimg32.lib" )

namespace
{
// Altium's menu colours (measured from AD24)
const COLORREF BG          = RGB( 0x40, 0x40, 0x40 );
const COLORREF HOT_BG      = RGB( 0x62, 0x83, 0xA2 );
const COLORREF HOT_OFF_BG  = RGB( 0x4E, 0x4E, 0x4E );   // highlighted but disabled
const COLORREF TEXT        = RGB( 0xF0, 0xF0, 0xF0 );
const COLORREF HOT_TEXT    = RGB( 0xFF, 0xFF, 0xFF );
const COLORREF GRAY_TEXT   = RGB( 0x86, 0x86, 0x86 );
const COLORREF SEPARATOR   = RGB( 0x5A, 0x5A, 0x5A );
const COLORREF ARROW       = RGB( 0xD0, 0xD0, 0xD0 );

const UINT_PTR SUBCLASS_ID = 0x41544D4E;    // "ATMN"


/// One owner-drawn item while its menu is open: what Windows forgets when the type changes
struct ITEM
{
    std::wstring text;           ///< label, with '&' mnemonic and "\tshortcut"
    HBITMAP      bitmap = nullptr;
    bool         separator = false;
    bool         submenu = false;
    UINT         origType = 0;
    ULONG_PTR    origData = 0;
    HBITMAP      origBitmap = nullptr;    ///< the item's hbmpItem, restored on close

    /// \a bitmap resized to the menu's icon size (see drawBitmap()); owned
    mutable HBITMAP scaledBitmap = nullptr;
    mutable int     scaledSize = 0;

    /// The wxWidgets item, when the menu is one of wx's: its bitmap bundle gives a sharp icon
    /// at any size (popup menus get 1x bitmaps, having no window when their items are added)
    wxMenuItem*      wxItem = nullptr;
    mutable wxBitmap sharpBitmap;
    mutable int      sharpSize = 0;

    ITEM() = default;
    ITEM( const ITEM& ) = delete;
    ITEM& operator=( const ITEM& ) = delete;

    ~ITEM()
    {
        if( scaledBitmap )
            ::DeleteObject( scaledBitmap );
    }
};


struct OPEN_MENU
{
    std::vector<std::unique_ptr<ITEM>> items;     ///< by position; null = left native
};


std::map<HMENU, OPEN_MENU>& openMenus()
{
    static std::map<HMENU, OPEN_MENU> menus;
    return menus;
}


std::set<const ITEM*>& liveItems()
{
    static std::set<const ITEM*> items;
    return items;
}


HHOOK s_hook = nullptr;



UINT dpiOf( HWND aWnd )
{
    using GET_DPI = UINT( WINAPI* )( HWND );
    static GET_DPI getDpi = reinterpret_cast<GET_DPI>(
            ::GetProcAddress( ::GetModuleHandleW( L"user32.dll" ), "GetDpiForWindow" ) );

    UINT dpi = getDpi && aWnd ? getDpi( aWnd ) : 0;

    if( !dpi )
    {
        HDC dc = ::GetDC( nullptr );
        dpi = ::GetDeviceCaps( dc, LOGPIXELSY );
        ::ReleaseDC( nullptr, dc );
    }

    return dpi ? dpi : 96;
}


int scaled( int aValue, UINT aDpi )
{
    return ::MulDiv( aValue, (int) aDpi, 96 );
}


/// The system menu font at a DPI (cached)
HFONT menuFont( UINT aDpi )
{
    static std::map<UINT, HFONT> fonts;

    auto it = fonts.find( aDpi );

    if( it != fonts.end() )
        return it->second;

    using SPI_FOR_DPI = BOOL( WINAPI* )( UINT, UINT, PVOID, UINT, UINT );
    static SPI_FOR_DPI spiForDpi = reinterpret_cast<SPI_FOR_DPI>(
            ::GetProcAddress( ::GetModuleHandleW( L"user32.dll" ), "SystemParametersInfoForDpi" ) );

    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof( ncm );

    if( spiForDpi )
    {
        spiForDpi( SPI_GETNONCLIENTMETRICS, sizeof( ncm ), &ncm, 0, aDpi );
    }
    else
    {
        ::SystemParametersInfoW( SPI_GETNONCLIENTMETRICS, sizeof( ncm ), &ncm, 0 );
        ncm.lfMenuFont.lfHeight = ::MulDiv( ncm.lfMenuFont.lfHeight, (int) aDpi, 96 );
    }

    HFONT font = ::CreateFontIndirectW( &ncm.lfMenuFont );
    fonts[aDpi] = font;
    return font;
}


HBRUSH bgBrush()
{
    static HBRUSH brush = ::CreateSolidBrush( BG );
    return brush;
}


std::wstring labelPart( const std::wstring& aText )
{
    size_t tab = aText.find( L'\t' );
    return tab == std::wstring::npos ? aText : aText.substr( 0, tab );
}


std::wstring shortcutPart( const std::wstring& aText )
{
    size_t tab = aText.find( L'\t' );
    return tab == std::wstring::npos ? std::wstring() : aText.substr( tab + 1 );
}


// --------------------------------------------------------------------------- open / close

/// The wxMenuItem behind a native item: wx keeps it in the item data (src/msw/menu.cpp).
/// Checked, because other menus (the system menu) keep other things there.
wxMenuItem* wxItemOf( HMENU aMenu, ULONG_PTR aData )
{
    if( !aData )
        return nullptr;

    __try
    {
        wxMenuItem* item = reinterpret_cast<wxMenuItem*>( aData );
        wxMenu*     menu = item->GetMenu();

        if( menu && static_cast<HMENU>( menu->GetHMenu() ) == aMenu )
            return item;
    }
    __except( EXCEPTION_EXECUTE_HANDLER )
    {
    }

    return nullptr;
}


void convert( HMENU aMenu )
{
    if( !aMenu || openMenus().count( aMenu ) )
        return;

    OPEN_MENU& open = openMenus()[aMenu];
    int        count = ::GetMenuItemCount( aMenu );

    for( int pos = 0; pos < count; ++pos )
    {
        MENUITEMINFOW mii = {};
        mii.cbSize = sizeof( mii );
        mii.fMask = MIIM_FTYPE | MIIM_STRING | MIIM_BITMAP | MIIM_SUBMENU | MIIM_DATA;

        if( !::GetMenuItemInfoW( aMenu, pos, TRUE, &mii ) || ( mii.fType & MFT_OWNERDRAW )
                || ( mii.fType & MFT_BITMAP ) )
        {
            open.items.emplace_back();     // already owner-drawn by someone else: leave it
            continue;
        }

        auto item = std::make_unique<ITEM>();
        item->separator = ( mii.fType & MFT_SEPARATOR ) != 0;
        item->submenu = mii.hSubMenu != nullptr;
        item->origType = mii.fType;
        item->origData = mii.dwItemData;
        item->wxItem = wxItemOf( aMenu, mii.dwItemData );
        item->origBitmap = mii.hbmpItem;
        item->bitmap = ( mii.hbmpItem && mii.hbmpItem != HBMMENU_CALLBACK
                         && (ULONG_PTR) mii.hbmpItem > 11 ) ? mii.hbmpItem : nullptr;

        if( !item->separator && mii.cch > 0 )
        {
            std::wstring text( mii.cch + 1, L'\0' );
            mii.fMask = MIIM_STRING;
            mii.dwTypeData = text.data();
            mii.cch = (UINT) text.size();

            if( ::GetMenuItemInfoW( aMenu, pos, TRUE, &mii ) )
                item->text.assign( text.c_str() );
        }

        MENUITEMINFOW set = {};
        set.cbSize = sizeof( set );
        // No bitmap while owner-drawn: Windows doesn't ask the owner to measure items that
        // have one (we draw the icon ourselves)
        set.fMask = MIIM_FTYPE | MIIM_DATA | MIIM_BITMAP;
        set.hbmpItem = nullptr;
        set.fType = ( item->origType & ( MFT_SEPARATOR | MFT_RADIOCHECK | MFT_RIGHTJUSTIFY
                                         | MFT_MENUBREAK | MFT_MENUBARBREAK ) )
                    | MFT_OWNERDRAW;
        set.dwItemData = reinterpret_cast<ULONG_PTR>( item.get() );

        if( ::SetMenuItemInfoW( aMenu, pos, TRUE, &set ) )
        {
            liveItems().insert( item.get() );
            open.items.push_back( std::move( item ) );
        }
        else
        {
            open.items.emplace_back();
        }
    }

    MENUINFO info = {};
    info.cbSize = sizeof( info );
    info.fMask = MIM_BACKGROUND;
    info.hbrBack = bgBrush();
    ::SetMenuInfo( aMenu, &info );
}


void revert( HMENU aMenu )
{
    auto it = openMenus().find( aMenu );

    if( it == openMenus().end() )
        return;

    if( ::IsMenu( aMenu ) )
    {
        int count = ::GetMenuItemCount( aMenu );

        for( int pos = 0; pos < count && pos < (int) it->second.items.size(); ++pos )
        {
            ITEM* item = it->second.items[pos].get();

            if( !item )
                continue;

            MENUITEMINFOW set = {};
            set.cbSize = sizeof( set );
            set.fMask = MIIM_FTYPE | MIIM_DATA | MIIM_BITMAP;
            set.fType = item->origType;
            set.dwItemData = item->origData;
            set.hbmpItem = item->origBitmap;

            if( !item->separator )
            {
                set.fMask |= MIIM_STRING;
                set.dwTypeData = item->text.data();
            }

            ::SetMenuItemInfoW( aMenu, pos, TRUE, &set );
        }
    }

    for( const std::unique_ptr<ITEM>& item : it->second.items )
        liveItems().erase( item.get() );

    openMenus().erase( it );
}


void revertAll()
{
    while( !openMenus().empty() )
        revert( openMenus().begin()->first );
}


// ------------------------------------------------------------------------------- drawing

void measure( HWND aWnd, MEASUREITEMSTRUCT* aMis )
{
    const ITEM* item = reinterpret_cast<const ITEM*>( aMis->itemData );
    UINT        dpi = dpiOf( aWnd );

    if( item->separator )
    {
        aMis->itemWidth = scaled( 40, dpi );
        aMis->itemHeight = scaled( 7, dpi );
        return;
    }

    HDC   dc = ::GetDC( aWnd );
    HGDIOBJ old = ::SelectObject( dc, menuFont( dpi ) );

    RECT label = {};
    std::wstring text = labelPart( item->text );
    ::DrawTextW( dc, text.c_str(), (int) text.size(), &label, DT_CALCRECT | DT_SINGLELINE );

    RECT shortcut = {};
    std::wstring keys = shortcutPart( item->text );

    if( !keys.empty() )
        ::DrawTextW( dc, keys.c_str(), (int) keys.size(), &shortcut, DT_CALCRECT | DT_SINGLELINE );

    ::SelectObject( dc, old );
    ::ReleaseDC( aWnd, dc );

    int gutter = scaled( 34, dpi );
    int width = gutter + label.right + scaled( 12, dpi );

    if( shortcut.right > 0 )
        width += scaled( 36, dpi ) + shortcut.right;

    width += scaled( 24, dpi );   // submenu arrow / right margin

    aMis->itemWidth = width;
    // Altium's rows: about 23 px at 100 % (34 px at 150 %)
    aMis->itemHeight = std::max<int>( label.bottom + scaled( 7, dpi ), scaled( 22, dpi ) );
}


void drawCheck( HDC aDC, const RECT& aBox, COLORREF aColour, UINT aDpi )
{
    int  w = std::max( 1, scaled( 2, aDpi ) );
    HPEN pen = ::CreatePen( PS_SOLID, w, aColour );
    HGDIOBJ old = ::SelectObject( aDC, pen );

    int cx = ( aBox.left + aBox.right ) / 2;
    int cy = ( aBox.top + aBox.bottom ) / 2;
    int s = scaled( 4, aDpi );

    POINT pts[3] = { { cx - s - s / 2, cy }, { cx - s / 2, cy + s }, { cx + s + s / 2, cy - s } };
    ::Polyline( aDC, pts, 3 );

    ::SelectObject( aDC, old );
    ::DeleteObject( pen );
}


void drawArrow( HDC aDC, const RECT& aItem, COLORREF aColour, UINT aDpi )
{
    int cx = aItem.right - scaled( 12, aDpi );
    int cy = ( aItem.top + aItem.bottom ) / 2;
    int s = scaled( 4, aDpi );

    POINT pts[3] = { { cx - s / 2, cy - s }, { cx + s / 2 + 1, cy }, { cx - s / 2, cy + s } };

    HBRUSH  brush = ::CreateSolidBrush( aColour );
    HPEN    pen = ::CreatePen( PS_SOLID, 1, aColour );
    HGDIOBJ oldBrush = ::SelectObject( aDC, brush );
    HGDIOBJ oldPen = ::SelectObject( aDC, pen );
    ::Polygon( aDC, pts, 3 );
    ::SelectObject( aDC, oldBrush );
    ::SelectObject( aDC, oldPen );
    ::DeleteObject( brush );
    ::DeleteObject( pen );
}


/**
 * A copy of \a aSource at \a aSize x \a aSize pixels, as a 32-bit premultiplied DIB.
 *
 * Menu items carry the HBITMAP wxWidgets realised from their bitmap bundle for the menu's
 * window.  A popup menu has no window when its items are added (it only gets one while it is
 * shown), so its bitmaps are realised at 1x -- 16 px icons in a menu drawn for 150% or 200%,
 * next to menu-bar menus whose icons are realised at the window's scale.
 */
HBITMAP resizedBitmap( HBITMAP aSource, int aSize )
{
    BITMAP bm = {};

    if( aSize <= 0 || !::GetObjectW( aSource, sizeof( bm ), &bm ) || bm.bmWidth <= 0
            || bm.bmHeight <= 0 )
    {
        return nullptr;
    }

    int w = bm.bmWidth;
    int h = bm.bmHeight;

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof( bi.bmiHeader );
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;     // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    std::vector<unsigned char> src( (size_t) w * h * 4 );
    HDC screen = ::GetDC( nullptr );
    int lines = ::GetDIBits( screen, aSource, 0, h, src.data(), &bi, DIB_RGB_COLORS );
    ::ReleaseDC( nullptr, screen );

    if( lines != h )
        return nullptr;

    bool hasAlpha = bm.bmBitsPixel == 32;

    // Resample the premultiplied colours and the alpha separately (premultiplied data filters
    // without dark fringes)
    wxImage img( w, h, false );
    img.InitAlpha();

    unsigned char* rgb = img.GetData();
    unsigned char* alpha = img.GetAlpha();

    for( int ii = 0; ii < w * h; ++ii )
    {
        rgb[ii * 3 + 0] = src[ii * 4 + 2];
        rgb[ii * 3 + 1] = src[ii * 4 + 1];
        rgb[ii * 3 + 2] = src[ii * 4 + 0];
        alpha[ii] = hasAlpha ? src[ii * 4 + 3] : 255;
    }

    // Square icons; keep the aspect of anything else
    int dw = aSize;
    int dh = aSize;

    if( w != h )
    {
        if( w > h )
            dh = std::max( 1, ::MulDiv( h, aSize, w ) );
        else
            dw = std::max( 1, ::MulDiv( w, aSize, h ) );
    }

    img.Rescale( dw, dh, wxIMAGE_QUALITY_HIGH );

    bi.bmiHeader.biWidth = dw;
    bi.bmiHeader.biHeight = -dh;

    void*   bits = nullptr;
    HBITMAP out = ::CreateDIBSection( nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0 );

    if( !out || !bits )
        return nullptr;

    unsigned char* dst = static_cast<unsigned char*>( bits );

    rgb = img.GetData();
    alpha = img.GetAlpha();

    for( int ii = 0; ii < dw * dh; ++ii )
    {
        unsigned char a = alpha ? alpha[ii] : 255;

        // The filter can overshoot: premultiplied colour never exceeds alpha
        dst[ii * 4 + 0] = std::min( rgb[ii * 3 + 2], a );
        dst[ii * 4 + 1] = std::min( rgb[ii * 3 + 1], a );
        dst[ii * 4 + 2] = std::min( rgb[ii * 3 + 0], a );
        dst[ii * 4 + 3] = a;
    }

    ::GdiFlush();
    return out;
}


/// The item's icon at \a aSize px: its own bitmap when that is the size already
HBITMAP iconFor( const ITEM& aItem, int aSize )
{
    BITMAP bm = {};

    if( !aItem.bitmap || !::GetObjectW( aItem.bitmap, sizeof( bm ), &bm ) )
        return aItem.bitmap;

    // Within a pixel or two (e.g. 24 px icons at 150%) is the right size: never resample those
    if( std::abs( std::max( bm.bmWidth, bm.bmHeight ) - aSize ) <= 2 )
        return aItem.bitmap;

    // The wx item's bundle has the icon drawn for larger sizes: use that rather than
    // stretching the small one
    if( aItem.wxItem && aItem.wxItem->GetBitmapBundle().IsOk() )
    {
        if( aItem.sharpSize != aSize )
        {
            aItem.sharpBitmap = aItem.wxItem->GetBitmapBundle().GetBitmap( wxSize( aSize, aSize ) );
            aItem.sharpSize = aSize;
        }

        if( aItem.sharpBitmap.IsOk() && aItem.sharpBitmap.GetHBITMAP() )
            return static_cast<HBITMAP>( aItem.sharpBitmap.GetHBITMAP() );
    }

    if( aItem.scaledSize != aSize )
    {
        if( aItem.scaledBitmap )
            ::DeleteObject( aItem.scaledBitmap );

        aItem.scaledBitmap = resizedBitmap( aItem.bitmap, aSize );
        aItem.scaledSize = aSize;
    }

    return aItem.scaledBitmap ? aItem.scaledBitmap : aItem.bitmap;
}


void drawBitmap( HDC aDC, HBITMAP aBitmap, const RECT& aBox, bool aDisabled )
{
    BITMAP bm = {};

    if( !::GetObjectW( aBitmap, sizeof( bm ), &bm ) || bm.bmWidth <= 0 || bm.bmHeight <= 0 )
        return;

    int x = ( aBox.left + aBox.right - bm.bmWidth ) / 2;
    int y = ( aBox.top + aBox.bottom - bm.bmHeight ) / 2;

    HDC     mem = ::CreateCompatibleDC( aDC );
    HGDIOBJ old = ::SelectObject( mem, aBitmap );

    if( bm.bmBitsPixel == 32 )
    {
        BLENDFUNCTION blend = { AC_SRC_OVER, 0, (BYTE) ( aDisabled ? 100 : 255 ), AC_SRC_ALPHA };
        ::AlphaBlend( aDC, x, y, bm.bmWidth, bm.bmHeight, mem, 0, 0, bm.bmWidth, bm.bmHeight, blend );
    }
    else
    {
        ::BitBlt( aDC, x, y, bm.bmWidth, bm.bmHeight, mem, 0, 0, SRCCOPY );
    }

    ::SelectObject( mem, old );
    ::DeleteDC( mem );
}


void draw( HWND aWnd, DRAWITEMSTRUCT* aDis )
{
    const ITEM* item = reinterpret_cast<const ITEM*>( aDis->itemData );
    HDC         dc = aDis->hDC;
    RECT        rc = aDis->rcItem;
    UINT        dpi = dpiOf( aWnd );

    bool disabled = ( aDis->itemState & ( ODS_DISABLED | ODS_GRAYED ) ) != 0;
    bool hot = ( aDis->itemState & ODS_SELECTED ) != 0;
    bool checked = ( aDis->itemState & ODS_CHECKED ) != 0;

    COLORREF bg = hot ? ( disabled ? HOT_OFF_BG : HOT_BG ) : BG;
    HBRUSH   brush = ::CreateSolidBrush( bg );
    ::FillRect( dc, &rc, brush );
    ::DeleteObject( brush );

    if( item->separator )
    {
        RECT line = rc;
        line.left += scaled( 6, dpi );
        line.right -= scaled( 6, dpi );
        line.top = ( rc.top + rc.bottom ) / 2;
        line.bottom = line.top + 1;

        HBRUSH sep = ::CreateSolidBrush( SEPARATOR );
        ::FillRect( dc, &line, sep );
        ::DeleteObject( sep );
        return;
    }

    COLORREF fg = disabled ? GRAY_TEXT : ( hot ? HOT_TEXT : TEXT );
    int      gutter = scaled( 34, dpi );
    RECT     icon = { rc.left, rc.top, rc.left + gutter, rc.bottom };

    if( item->bitmap )
    {
        if( checked )
        {
            // A checked item with an icon: Altium frames the icon
            RECT   frame = icon;
            ::InflateRect( &frame, -scaled( 4, dpi ), -scaled( 2, dpi ) );
            HBRUSH hl = ::CreateSolidBrush( HOT_BG );
            ::FrameRect( dc, &frame, hl );
            ::DeleteObject( hl );
        }

        // Menu icons are 16 px at 100%, scaled with the window's DPI as the menu bar does
        drawBitmap( dc, iconFor( *item, scaled( 16, dpi ) ), icon, disabled );
    }
    else if( checked )
    {
        drawCheck( dc, icon, fg, dpi );
    }

    HGDIOBJ oldFont = ::SelectObject( dc, menuFont( dpi ) );
    ::SetBkMode( dc, TRANSPARENT );
    ::SetTextColor( dc, fg );

    UINT prefix = ( aDis->itemState & ODS_NOACCEL ) ? DT_HIDEPREFIX : 0;

    RECT text = rc;
    text.left += gutter + scaled( 4, dpi );
    text.right -= scaled( 24, dpi );
    std::wstring label = labelPart( item->text );
    ::DrawTextW( dc, label.c_str(), (int) label.size(), &text,
                 DT_SINGLELINE | DT_VCENTER | DT_LEFT | prefix );

    std::wstring keys = shortcutPart( item->text );

    if( !keys.empty() )
    {
        ::SetTextColor( dc, disabled ? GRAY_TEXT : ( hot ? HOT_TEXT : RGB( 0xC8, 0xC8, 0xC8 ) ) );
        ::DrawTextW( dc, keys.c_str(), (int) keys.size(), &text,
                     DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX );
    }

    ::SelectObject( dc, oldFont );

    if( item->submenu )
    {
        drawArrow( dc, rc, disabled ? GRAY_TEXT : ARROW, dpi );

        // Windows draws its own arrow after us; keep it off our item
        RECT arrow = rc;
        arrow.left = rc.right - scaled( 24, dpi );
        ::ExcludeClipRect( dc, arrow.left, arrow.top, arrow.right, arrow.bottom );
    }
}


/// Owner-drawn items lose their mnemonics: find the item for a typed letter ourselves
LRESULT menuChar( HMENU aMenu, wchar_t aKey, bool& aHandled )
{
    aHandled = false;
    auto it = openMenus().find( aMenu );

    if( it == openMenus().end() )
        return 0;

    // Ours from here on, matched or not: wxWidgets reads an owner-drawn item's data as its
    // own wxMenuItem*, so letting it see this menu would crash (e.g. Ctrl+E in a context menu)
    aHandled = true;

    wchar_t key = (wchar_t) ::CharLowerW( reinterpret_cast<LPWSTR>( (ULONG_PTR) aKey ) );

    for( size_t pos = 0; pos < it->second.items.size(); ++pos )
    {
        const ITEM* item = it->second.items[pos].get();

        if( !item || item->separator )
            continue;

        std::wstring label = labelPart( item->text );

        for( size_t ii = 0; ii + 1 < label.size(); ++ii )
        {
            if( label[ii] != L'&' )
                continue;

            if( label[ii + 1] == L'&' )
            {
                ++ii;
                continue;
            }

            wchar_t mnemonic = (wchar_t) ::CharLowerW(
                    reinterpret_cast<LPWSTR>( (ULONG_PTR) label[ii + 1] ) );

            if( mnemonic == key )
            {
                return MAKELRESULT( pos, item->submenu ? MNC_SELECT : MNC_EXECUTE );
            }

            break;
        }
    }

    return MAKELRESULT( 0, MNC_IGNORE );
}


LRESULT CALLBACK ownerProc( HWND aWnd, UINT aMsg, WPARAM aWParam, LPARAM aLParam, UINT_PTR,
                            DWORD_PTR )
{
    switch( aMsg )
    {
    case WM_MEASUREITEM:
    {
        MEASUREITEMSTRUCT* mis = reinterpret_cast<MEASUREITEMSTRUCT*>( aLParam );

        if( mis && mis->CtlType == ODT_MENU
                && liveItems().count( reinterpret_cast<const ITEM*>( mis->itemData ) ) )
        {
            measure( aWnd, mis );
            return TRUE;
        }

        break;
    }

    case WM_DRAWITEM:
    {
        DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>( aLParam );

        if( dis && dis->CtlType == ODT_MENU
                && liveItems().count( reinterpret_cast<const ITEM*>( dis->itemData ) ) )
        {
            draw( aWnd, dis );
            return TRUE;
        }

        break;
    }

    case WM_MENUCHAR:
    {
        bool    handled = false;
        LRESULT result = menuChar( reinterpret_cast<HMENU>( aLParam ), LOWORD( aWParam ), handled );

        if( handled )
            return result;

        break;
    }

    case WM_NCDESTROY:
        ::RemoveWindowSubclass( aWnd, ownerProc, SUBCLASS_ID );
        break;
    }

    return ::DefSubclassProc( aWnd, aMsg, aWParam, aLParam );
}


LRESULT CALLBACK hookProc( int aCode, WPARAM aWParam, LPARAM aLParam )
{
    if( aCode == HC_ACTION )
    {
        const CWPRETSTRUCT* msg = reinterpret_cast<const CWPRETSTRUCT*>( aLParam );

        switch( msg->message )
        {
        case WM_INITMENUPOPUP:
            // After the owner (wxWidgets) has updated the items' labels and states
            if( !HIWORD( msg->lParam ) )
            {
                ::SetWindowSubclass( msg->hwnd, ownerProc, SUBCLASS_ID, 0 );
                convert( reinterpret_cast<HMENU>( msg->wParam ) );
            }

            break;

        case WM_UNINITMENUPOPUP:
            revert( reinterpret_cast<HMENU>( msg->wParam ) );
            break;

        case WM_EXITMENULOOP:
            revertAll();
            break;
        }
    }

    return ::CallNextHookEx( s_hook, aCode, aWParam, aLParam );
}
} // namespace


void ALTIUM_MENU_THEME::Install()
{
    // Every module links this code; the first one in the process installs the hook
    // (keyed by process id: child processes inherit the environment)
    wchar_t name[64] = {};
    wchar_t flag[8] = {};
    ::wsprintfW( name, L"KICAD_ALTIUM_MENU_THEME_%lu", ::GetCurrentProcessId() );

    if( s_hook || ::GetEnvironmentVariableW( name, flag, 8 ) > 0 )
        return;

    s_hook = ::SetWindowsHookExW( WH_CALLWNDPROCRET, hookProc, nullptr, ::GetCurrentThreadId() );

    if( s_hook )
        ::SetEnvironmentVariableW( name, L"1" );
}

#else

void ALTIUM_MENU_THEME::Install()
{
}

#endif
