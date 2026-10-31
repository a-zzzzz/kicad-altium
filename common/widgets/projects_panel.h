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

#ifndef PROJECTS_PANEL_H
#define PROJECTS_PANEL_H

#include <functional>
#include <set>
#include <vector>

#include <wx/panel.h>
#include <wx/timer.h>
#include <wx/treectrl.h>

class EDA_BASE_FRAME;
class KIWAY_PLAYER;
class wxSearchCtrl;


/**
 * Altium-style Projects panel: the open project with its source documents (schematic sheets
 * nested by hierarchy, then boards), libraries and settings files.  Double-clicking a document
 * opens it in its editor; the document shown in this window is highlighted.
 */
class PROJECTS_PANEL : public wxPanel
{
public:
    PROJECTS_PANEL( wxWindow* aParent, KIWAY_PLAYER* aFrame );
    ~PROJECTS_PANEL() override;

    /// The file shown in the owning window (defaults to the frame's current file name).
    void SetActiveDocumentFunc( std::function<wxString()> aFunc ) { m_activeDocFunc = aFunc; }

    /**
     * Supplies the project's compiled design data for Altium's "Components" and "Nets" folders
     * (designators and net names, from the schematic or board shown in the owning window).
     */
    void SetDesignDataFunc(
            std::function<void( std::vector<wxString>&, std::vector<wxString>& )> aFunc )
    {
        m_designDataFunc = aFunc;
    }

    /// Re-reads the project folder and rebuilds the tree.
    void Rebuild();

    /**
     * Altium's right-click menu of a project (Validate PCB Project, Add New to Project, ...,
     * Project Options).  Shared by this panel and the project manager's project tree.
     *
     * @param aParent is the window the menu pops up over
     * @param aFrame is the frame owning that window (an editor or the project manager)
     */
    static void ShowProjectMenu( wxWindow* aParent, EDA_BASE_FRAME* aFrame );

    /// Opens a project document in its editor (as a double-click in the panel does).
    static void OpenDocument( KIWAY_PLAYER* aFrame, const wxString& aPath );

private:
    enum ICON
    {
        ICON_PROJECT = 0,
        ICON_FOLDER,
        ICON_SCHEMATIC,
        ICON_PCB,
        ICON_SCH_LIB,
        ICON_PCB_LIB,
        ICON_SETTINGS,
        ICON_OTHER,
        ICON_BOM,
        ICON_COMPONENT,
        ICON_NET
    };

    wxTreeItemId addDocument( const wxTreeItemId& aParent, const wxString& aPath, ICON aIcon,
                              const wxString& aPrefix = wxEmptyString );
    std::set<wxString> modifiedDocuments() const;
    void addSheetChildren( const wxTreeItemId& aParent, const wxString& aSheet,
                           std::set<wxString>& aStack, std::set<wxString>& aSeen );
    bool matchesFilter( const wxString& aPath ) const;
    void pruneEmptyFolders( const wxTreeItemId& aItem );

    wxString signature() const;
    void     openDocument( const wxString& aPath );
    void     onActivated( wxTreeEvent& aEvent );
    void     onContextMenu( wxTreeEvent& aEvent );
    void     onTimer( wxTimerEvent& aEvent );

    KIWAY_PLAYER*             m_frame;
    wxSearchCtrl*             m_search;
    wxTreeCtrl*               m_tree;
    wxTimer                   m_timer;
    wxString                  m_signature;
    std::function<wxString()> m_activeDocFunc;
    std::function<void( std::vector<wxString>&, std::vector<wxString>& )> m_designDataFunc;
    std::set<wxString>        m_modified;     ///< open documents with unsaved changes
    int                       m_pageCounter = 0;
};

#endif // PROJECTS_PANEL_H
