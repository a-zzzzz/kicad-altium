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

#ifndef COMPONENTS_PANEL_H
#define COMPONENTS_PANEL_H

#include <lib_id.h>
#include <lib_tree_model_adapter.h>

#include <wx/panel.h>
#include <wx/timer.h>

class EDA_BASE_FRAME;
class HTML_WINDOW;
class LIB_TREE;
class wxBitmapButton;
class wxDataViewEvent;
class wxBoxSizer;
class wxChoice;
class wxSplitterWindow;
class wxStaticText;


/**
 * Altium-style Components panel: pick a library (or all of them), search, see the selected
 * component's models and details below the list, and double-click (or press Enter) to place
 * it in the editor.
 *
 * The editor-specific parts (which libraries, which previews, how to place) come from the
 * derived classes.  The library list is only loaded the first time the panel is shown.
 */
class COMPONENTS_PANEL : public wxPanel
{
public:
    COMPONENTS_PANEL( wxWindow* aParent, EDA_BASE_FRAME* aFrame, const wxString& aSearchKey );
    ~COMPONENTS_PANEL() override;

    /// Rebuilds the library list and the component tree (e.g. after library tables change).
    void RefreshLibraries();

protected:
    /// Builds the derived panel's previews into aParent / aSizer (called once).
    virtual void createPreviews( wxWindow* aParent, wxBoxSizer* aSizer ) = 0;

    /// Libraries offered in the library selector.
    virtual std::vector<wxString> libraryNames() = 0;

    /// A tree adapter holding aLibrary, or every library when aLibrary is empty.
    virtual wxObjectDataPtr<LIB_TREE_MODEL_ADAPTER> createAdapter( const wxString& aLibrary ) = 0;

    /// Shows aLibId in the previews (an invalid id clears them).
    virtual void showPreview( const LIB_ID& aLibId, int aUnit ) = 0;

    /**
     * Places aLibId in the editor: the part follows the cursor until clicked down.  With
     * aRepeat, further copies follow until Esc (double-click); without, one is placed (drag).
     */
    virtual void place( const LIB_ID& aLibId, int aUnit, bool aRepeat ) = 0;

    /// Opens the library table manager.
    virtual void manageLibraries() = 0;

    /// Whether the editor has placed parts selected that Replace would change.
    virtual bool canReplace() { return false; }

    /// Altium's Replace: the selected placed parts become aLibId (keeping designators).
    virtual void replace( const LIB_ID& aLibId, int aUnit ) {}

    EDA_BASE_FRAME* m_frame;

private:
    void build();
    void rebuildTree();
    void onSelected( wxCommandEvent& aEvent );
    void onChosen( wxCommandEvent& aEvent );
    void onMenuButton( wxCommandEvent& aEvent );
    void updateDetailsBar();
    wxString detailsName( const LIB_ID& aSelected ) const;
    void toggleDetails();
    void onItemMenu( wxDataViewEvent& aEvent );
    void selectColumns();

    // Dragging a component from the list onto the canvas
    void         onListMouse( wxMouseEvent& aEvent );
    void         onDragTimer( wxTimerEvent& aEvent );
    wxWindow*    canvasUnderMouse() const;
    void         forwardToCanvas( wxWindow* aCanvas, wxEventType aType );

    wxString                                m_searchKey;
    bool                                    m_built;
    wxChoice*                               m_libraryChoice;
    wxBitmapButton*                         m_menuButton;
    wxSplitterWindow*                       m_splitter;
    wxPanel*                                m_treePanel;
    wxBoxSizer*                             m_treeSizer;
    LIB_TREE*                               m_tree;
    HTML_WINDOW*                            m_details;
    wxStaticText*                           m_count;
    wxPanel*                                m_moreParts = nullptr;   // "Need more components?"
    wxPanel*                                m_detailsPanel = nullptr;
    wxStaticText*                           m_detailsBar = nullptr;
    wxString                                m_detailsName;
    bool                                    m_detailsOpen = false;
    int                                     m_lastCount;
    bool                                    m_pressed;
    bool                                    m_dragging;
    wxPoint                                 m_pressPos;
    wxPoint                                 m_lastDragPos;
    wxTimer                                 m_dragTimer;
    wxObjectDataPtr<LIB_TREE_MODEL_ADAPTER> m_adapter;
};

#endif // COMPONENTS_PANEL_H
