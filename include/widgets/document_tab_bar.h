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

#ifndef DOCUMENT_TAB_BAR_H
#define DOCUMENT_TAB_BAR_H

#include <wx/panel.h>
#include <vector>

class wxFrame;
class wxTopLevelWindow;

/**
 * Altium-style document tabs above the canvas: one tab per open document window. Clicking a
 * tab shows that document in this window's place and hides this one (see DOCUMENT_WINDOWS),
 * so the editors behave as one main window (docs/altium-parity/documents.md).
 */
class DOCUMENT_TAB_BAR : public wxPanel
{
public:
    DOCUMENT_TAB_BAR( wxFrame* aOwner );

private:
    struct TAB
    {
        wxTopLevelWindow* window;
        wxString          label;
        wxRect            rect;
        wxRect            closeRect;
    };

    /**
     * What the bar shows: a document's tab, or (when the tabs don't fit, as Altium's "Group
     * documents if need" by document kind) one tab for several documents of the same kind,
     * labelled "(3) PCB Library Document" with a drop-down listing them.
     */
    struct ENTRY
    {
        std::vector<int> members;   ///< indices into m_tabs
        wxString         label;
        bool             group = false;
        bool             active = false;
        wxRect           rect;
        wxRect           closeRect;
        wxRect           arrowRect;
    };

    void layoutEntries( wxDC& aDC );
    void showGroupMenu( const ENTRY& aEntry );

    void onPaint( wxPaintEvent& aEvent );
    void onLeftDown( wxMouseEvent& aEvent );
    void onMotion( wxMouseEvent& aEvent );
    void onIdle( wxIdleEvent& aEvent );

    std::vector<TAB> currentTabs() const;
    void switchTo( wxTopLevelWindow* aWindow );

    wxFrame*         m_owner;
    std::vector<TAB>   m_tabs;
    std::vector<ENTRY> m_entries;
    int                m_hoverClose;   ///< index of the entry whose close box is hovered, or -1
};

#endif // DOCUMENT_TAB_BAR_H
