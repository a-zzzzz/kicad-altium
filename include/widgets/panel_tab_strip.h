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

#ifndef PANEL_TAB_STRIP_H
#define PANEL_TAB_STRIP_H

#include <wx/panel.h>
#include <map>
#include <optional>
#include <set>
#include <vector>

class wxAuiManager;
class wxAuiManagerEvent;
class SIDE_TAB_BAR;

/**
 * Altium-style panel tabs: several docked panels share one column, and a strip of tabs at
 * the bottom of the column switches between them.
 *
 * The panels stay ordinary AUI panes (so existing show/hide actions, close buttons and saved
 * layouts keep working); the strip only shows the active tab's panes and hides the others.
 * Every tab is listed when the strip is attached.  Opening a pane from a menu makes its tab
 * active; closing the active pane drops its tab (for this session) and activates another.
 *
 * As in Altium, panels move between columns by dragging: a panel dropped into the column (or
 * anywhere on its side of the window) becomes a new tab, and a tab dragged off the strip -- or
 * a panel dragged by its caption -- floats and leaves the strip.
 */
class PANEL_TAB_STRIP : public wxPanel
{
public:
    struct TAB
    {
        wxString              label;   ///< untranslated (_HKI); shown translated
        std::vector<wxString> panes;   ///< AUI panes shown together; the first is the main one
    };

    PANEL_TAB_STRIP( wxWindow* aParent, wxAuiManager& aAuiMgr, std::vector<TAB> aTabs );
    ~PANEL_TAB_STRIP() override;

    /**
     * Dock every tab's panes, and the strip itself below them, in one column on the given
     * side.  Call after the frame has restored its saved layout so the grouping always wins.
     *
     * @param aDirection is a wxAUI_DOCK_* side
     * @param aLayer is the AUI layer of the column
     * @param aPaneName is the strip's own pane name
     */
    void Attach( int aDirection, int aLayer, const wxString& aPaneName );

    /// Hide the column and show its tabs as vertical tabs along the window edge (Altium's
    /// unpinned panels), or restore it with the given tab in front.
    void Collapse();
    void Expand( size_t aTab );
    bool IsCollapsed() const { return m_collapsed; }

    const std::vector<TAB>& GetTabs() const { return m_tabs; }

    /**
     * Whether a panel is open in Altium's sense (the Panels menu's check mark): shown, or
     * behind another tab of its column, or in a collapsed column.
     */
    static bool IsPaneOpen( wxAuiManager& aAuiMgr, const wxString& aPaneName );

    /**
     * The Panels menu's toggle for a panel that is open but not on screen (behind another tab,
     * or in a collapsed column): closes it and returns true.  Otherwise returns false, and the
     * caller shows or hides the pane as usual.
     */
    static bool CloseIfBackground( wxAuiManager& aAuiMgr, const wxString& aPaneName );
    const std::set<size_t>& GetOpenTabs() const { return m_open; }

    /**
     * List a panel's tab in the strip even when it was closed last session (behind the tab in
     * front, which stays in front).  For panels Altium always keeps reachable, e.g. Properties.
     */
    void EnsureListed( const wxString& aPaneName );

private:
    /// Record (aSave) or apply the column state of \a aWindow's strips across a document switch.
    static void saveOrApply( class wxTopLevelWindow* aWindow, bool aSave );

    /// Keep which tabs are open, the one in front, the width and collapsed state for the
    /// next session (Altium reopens its panels as they were left)
    void saveState() const;
    void restoreState();

    void onPaint( wxPaintEvent& aEvent );
    void onLeftDown( wxMouseEvent& aEvent );
    void onLeftUp( wxMouseEvent& aEvent );
    void onMotion( wxMouseEvent& aEvent );
    void onIdle( wxIdleEvent& aEvent );
    void onPaneButton( wxAuiManagerEvent& aEvent );

    /// Adopt panes dropped on this side and drop tabs dragged away.  @return true if changed.
    bool trackDocking();
    void removeTab( size_t aTab );
    void layoutColumn();

    bool tabShown( size_t aTab ) const;
    void showTab( size_t aTab, bool aShow );
    void activate( size_t aTab, bool aSampleWidth = true );

    /// Remember the column's current width as its preferred one (unless it was shrunk to fit)
    void sampleWidth();

    /// The width to show: the remembered one, or less when the window is too narrow for
    /// every column plus a minimum canvas width
    int  fitWidth() const;
    void fitToWindow();
    void applyWidth( int aWidth );
    bool isSideColumn() const;
    void sync();

    wxAuiManager&       m_auiMgr;
    std::vector<TAB>    m_tabs;
    std::set<size_t>    m_open;         ///< tabs with at least one open pane
    size_t              m_active;
    wxString            m_paneName;
    wxString            m_stateKey;     ///< "<frame name>.<strip pane name>"
    int                 m_columnWidth = 0;   ///< Width the column keeps across tab switches
    int                 m_fitTarget = 0;     ///< Width shrunk to (window too narrow); 0 if not
    int                 m_fitWidth = 0;      ///< Width wxAUI gave for m_fitTarget
    int                 m_sampleFrameWidth = 0;   ///< window width at the last sampleWidth()
    std::vector<wxRect> m_tabRects;     ///< parallel to m_tabs; empty rect when not listed
    wxRect              m_collapseRect; ///< collapse button at the right end of the strip
    bool                m_collapsed;
    SIDE_TAB_BAR*       m_sideBar;

    std::map<wxString, bool> m_onSide;  ///< pane name -> shown and docked on this side
    bool                     m_tracking = false;
    std::optional<size_t>    m_dragTab; ///< tab pressed on, until dragged off or released
    wxPoint                  m_dragStart;
};

#endif // PANEL_TAB_STRIP_H
