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

#ifndef MESSAGES_PANEL_H
#define MESSAGES_PANEL_H

#include <wx/panel.h>
#include <wx/datetime.h>
#include <wx/timer.h>
#include <wx/aui/framemanager.h>
#include <messages_store.h>
#include <widgets/report_severity.h>

#include <functional>
#include <vector>

class EDA_DRAW_FRAME;
class MESSAGES_LIST;
class wxListEvent;
class wxSplitterWindow;
class wxTopLevelWindow;
class wxTreeCtrl;
class wxTreeEvent;

/**
 * Altium-style Messages panel (docs/altium-parity/panels.md#messages-panel): a grid of
 * messages with Class, Document, Source, Message, Time, Date and No. columns.
 *
 * As in Altium there is one list of messages for the whole project: every editor's panel
 * shows the rows of all open documents (MESSAGES_STORE), each panel adding its own.
 *
 * Rows come from two places:
 *  - the editor's markers (DRC / ERC / parity), supplied by a subclass and re-read whenever
 *    the set of markers changes (e.g. after a DRC or ERC run);
 *  - report output of dialogs owned by the frame (update PCB from schematic, netlist import,
 *    DRC / ERC progress, ...), routed here by PostReport().
 *
 * Double-clicking a row switches to the row's document and runs its activation (zoom to /
 * select the related item).
 *
 * Below the list (on a splitter) is Altium's Details area: a tree with the selected message
 * and, as its children, the objects it is about (PROJECT_MESSAGE::details).  Double-clicking
 * an object zooms to it.
 */
class MESSAGES_PANEL : public wxPanel
{
public:
    using MESSAGE = PROJECT_MESSAGE;

    MESSAGES_PANEL( wxWindow* aParent, EDA_DRAW_FRAME* aFrame );
    ~MESSAGES_PANEL() override;

    static wxString PaneName() { return wxS( "MessagesPanel" ); }

    /// Default docking: a tab of the right-hand column, hidden until opened (or a DRC/ERC run).
    static wxAuiPaneInfo DefaultPaneInfo( wxWindow* aWindow );

    /// Show or hide the Messages pane of a frame (no-op when the frame has none).
    static void TogglePane( wxWindow* aFrame );
    static bool IsPaneShown( wxWindow* aFrame );

    /// Bring the Messages pane of a frame to the front (opening it if needed).
    static void ShowPane( wxWindow* aFrame );

    /// Whether the pane is open, possibly behind another tab (the Panels menu's check mark).
    static bool IsPaneOpen( wxWindow* aFrame );

    void AddMessage( MESSAGE aMessage );
    void ClearAll();

    /// Re-read the markers now (they are also polled once a second).
    void RefreshMarkers() { syncMarkers( true ); }

    /**
     * Report output from a dialog's report panel.  The message goes to the Messages panel of
     * the frame owning the dialog (if it has one), with the dialog title as its source.
     */
    static void PostReport( wxWindow* aOrigin, const wxString& aText, SEVERITY aSeverity );

    // Used by the list control
    size_t            GetCount() const { return MESSAGES_STORE::Messages().size(); }
    const MESSAGE&    GetRow( size_t aIdx ) const { return MESSAGES_STORE::Messages()[aIdx]; }
    static wxString   ClassLabel( SEVERITY aSeverity );

protected:
    /// Append one MESSAGE per marker of the document(s) this frame edits.
    virtual void collectMarkers( std::vector<MESSAGE>& aMessages ) {}

    /// A value that changes whenever the set of markers changes.
    virtual size_t markerSignature() { return 0; }

    /// The document shown in the frame, for report rows.
    virtual wxString documentName() const;

    EDA_DRAW_FRAME* m_frame;

private:
    void syncMarkers( bool aForce );
    void showPane();
    void refreshList( bool aScrollToEnd );
    void scrollIfPending();
    void onActivated( wxListEvent& aEvent );
    void onContextMenu( wxListEvent& aEvent );
    void copySelection( bool aAll );

    /// Fill the Details tree for the selected row (unless it already shows that row).
    void showDetails( bool aForce = false );
    void onDetailActivated( wxTreeEvent& aEvent );

    /// Run a row's or object's action, switching to its document first.
    void runAction( const std::function<void()>& aAction, wxTopLevelWindow* aOwner );

    wxSplitterWindow*    m_splitter;
    wxTreeCtrl*          m_details;
    wxString             m_detailsFor;    ///< identity of the row the Details tree shows
    wxTopLevelWindow*    m_detailsOwner;
    std::vector<std::function<void()>> m_detailActions;   ///< by tree item data index

    MESSAGES_LIST*       m_list;
    int                  m_listenerId;
    wxTimer              m_timer;
    size_t               m_markerSig;
    bool                 m_markersRead;
    bool                 m_scrollPending = false;   ///< new rows arrived while hidden
    wxTimer              m_resizeTimer;   ///< thaws the list once a resize has settled
};

#endif // MESSAGES_PANEL_H
