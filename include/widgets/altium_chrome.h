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

#ifndef ALTIUM_CHROME_H
#define ALTIUM_CHROME_H

#include <functional>
#include <vector>

#include <wx/panel.h>

class ACTION_TOOLBAR;
class TOOL_ACTION;
class wxFrame;


/// Colours shared by the Altium-style title bar and menu row.
struct ALTIUM_CHROME_COLOURS
{
    wxColour background;        ///< menu row
    wxColour titleBackground;   ///< title bar
    wxColour text;
    wxColour inactiveText;
    wxColour hover;
    wxColour pressed;

    static ALTIUM_CHROME_COLOURS Get();
};


/**
 * Altium-style window title bar drawn in the client area: application icon and quick-access
 * buttons at the left, the window title in the middle, minimise / maximise / close at the
 * right.  Empty space behaves like a native caption (drag, double-click, system menu, snap).
 */
class ALTIUM_TITLE_BAR : public wxPanel
{
public:
    /// @param aQuickAccess buttons at the left; only for an EDA_BASE_FRAME (they are actions)
    ALTIUM_TITLE_BAR( wxFrame* aFrame, const std::vector<const TOOL_ACTION*>& aQuickAccess = {} );

    ACTION_TOOLBAR* GetQuickAccess() const { return m_quickAccess; }

#ifdef __WXMSW__
    WXLRESULT MSWWindowProc( WXUINT aMsg, WXWPARAM aWParam, WXLPARAM aLParam ) override;
#endif

private:
    enum BUTTON { NONE = -1, MINIMIZE, MAXIMIZE, CLOSE };

    wxRect buttonRect( int aButton ) const;
    int    buttonAt( const wxPoint& aPos ) const;

    void onPaint( wxPaintEvent& aEvent );
    void onMouse( wxMouseEvent& aEvent );

    wxFrame*        m_frame;
    ACTION_TOOLBAR* m_quickAccess;
    int             m_hover;
    int             m_pressed;
};


/**
 * The menu row under the title bar: the frame's menu titles (drawn here; the frame's own
 * wxMenuBar stays alive but isn't shown natively) and, at the right, the frame's main toolbar.
 */
class ALTIUM_MENU_STRIP : public wxPanel
{
public:
    explicit ALTIUM_MENU_STRIP( wxFrame* aFrame );

    /// Hosts aToolbar at the right end of the row.
    void SetToolbar( ACTION_TOOLBAR* aToolbar );

    /**
     * Drops down the menu at aIndex in the frame's menu bar.  While it is down, pointing at
     * another title or pressing Left / Right at the top level switches to the neighbouring
     * menu, as in a native menu bar.
     *
     * @param aFromKeyboard highlight the first item (menus opened with Alt+letter / F10).
     */
    void OpenMenu( int aIndex, bool aFromKeyboard = false );

    /// Index of the menu whose title has the mnemonic aKey, or -1.
    int FindMnemonic( int aKey ) const;

#ifdef __WXMSW__
    /// Internal: called from the menu-loop hooks while a menu of this strip is down (aMsg is
    /// a MSG*).  Returns true if the message was consumed.
    bool MenuLoopMessage( void* aMsg );

    /// Internal: WM_MENUSELECT seen during the menu loop (aMenu is the HMENU).
    void MenuSelected( void* aMenu, unsigned aFlags );
#endif

private:
    void layoutTitles();
    void layoutToolbar();
    int  titleAt( const wxPoint& aPos ) const;

    void onPaint( wxPaintEvent& aEvent );
    void onMouse( wxMouseEvent& aEvent );

    wxFrame*            m_frame;
    std::vector<wxRect> m_titleRects;
    wxSize              m_toolbarBest;   ///< Toolbar best size at the last layoutToolbar()
    int                 m_hover;
    int                 m_open;

    // State of the running menu loop (OpenMenu)
    int                 m_next;          ///< Menu to open once the current one has closed
    bool                m_nextKeyboard;  ///< ... and whether to highlight its first item
    void*               m_openMenu;      ///< HMENU of the menu that is down
    void*               m_selMenu;       ///< HMENU holding the highlighted item
    bool                m_selIsPopup;    ///< Highlighted item opens a submenu
    wxPoint             m_lastMouse;
};


#ifdef __WXMSW__
namespace ALTIUM_CHROME
{
/**
 * Drops \a aFrame's native caption and menu bar so its client-drawn title bar and menu row
 * replace them.  Call once the title bar is in place; the frame's MSWWindowProc must then
 * pass its messages through WindowProc().
 */
void HideNativeCaption( wxFrame* aFrame );

/**
 * The non-client handling for a frame with a client-drawn title bar: keeps the resize
 * borders, removes the caption, and makes \a aTitleBar's empty space behave as the caption.
 * Other messages (and the default hit test) go to \a aDefault.
 */
WXLRESULT WindowProc( wxFrame* aFrame, wxWindow* aTitleBar, WXUINT aMsg, WXWPARAM aWParam,
                      WXLPARAM aLParam,
                      const std::function<WXLRESULT( WXUINT, WXWPARAM, WXLPARAM )>& aDefault );

/// Opens \a aStrip's menus from the keyboard (F10, Alt+mnemonic) in \a aFrame, which has no
/// native menu bar to do it.  \a aIsHotkey says whether a hotkey code (key + MD_* modifiers)
/// is assigned to a command already; an assigned key runs its command instead.
void BindMenuKeys( wxFrame* aFrame, ALTIUM_MENU_STRIP* aStrip,
                   const std::function<bool( int )>& aIsHotkey = nullptr );
}
#endif

#endif // ALTIUM_CHROME_H
