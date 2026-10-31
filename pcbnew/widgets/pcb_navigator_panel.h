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

#ifndef PCB_NAVIGATOR_PANEL_H
#define PCB_NAVIGATOR_PANEL_H

#include <wx/panel.h>
#include <kiid.h>

#include <set>
#include <vector>

class BOARD;
class PCB_EDIT_FRAME;
class wxCheckBox;
class wxChoice;
class wxListEvent;
class wxListView;
class wxStaticText;

/**
 * Altium-style "PCB" panel (docs/altium-parity/panels.md#pcb-panel): pick a browse mode (Nets,
 * Components, Differential Pairs, Polygons, Hole Sizes), then a class, an object of that
 * class and its primitives.  Picking a row highlights its objects (Normal / Mask / Dim) and
 * optionally selects them and zooms to them.
 */
class PCB_NAVIGATOR_PANEL : public wxPanel
{
public:
    PCB_NAVIGATOR_PANEL( wxWindow* aParent, PCB_EDIT_FRAME* aFrame );
    ~PCB_NAVIGATOR_PANEL() override;

    static wxString PaneName() { return wxS( "PcbNavigator" ); }

    /// Re-read the board (keeps the mode and the picked class where possible).
    void Rebuild();

    enum class MODE
    {
        NETS = 0,
        COMPONENTS,
        DIFF_PAIRS,
        POLYGONS,
        HOLES
    };

    /// One row of one of the three lists.
    struct ROW
    {
        std::vector<wxString> cols;
        std::vector<KIID>     items;      ///< board items the row stands for
        std::set<int>         nets;       ///< nets the row stands for (net-based modes)
        std::set<int>         classes;    ///< objects only: indices of the classes it belongs to
    };

private:
    void onModeChanged();
    void onClassSelected();
    void onObjectSelected();
    void onPrimitiveSelected();
    void onIdle( wxIdleEvent& aEvent );

    void buildRows();
    void buildNets( BOARD* aBoard );
    void buildComponents( BOARD* aBoard );
    void buildDiffPairs( BOARD* aBoard );
    void buildPolygons( BOARD* aBoard );
    void buildHoles( BOARD* aBoard );
    void buildPrimitives( const ROW& aObject );

    void fillList( wxListView* aList, const std::vector<wxString>& aHeaders,
                   const std::vector<ROW>& aRows, const std::vector<size_t>& aShown );
    void fillObjects();

    /// Highlight / select / zoom to the union of the given rows.
    void apply( const std::vector<const ROW*>& aRows );
    void clearHighlight();
    std::vector<size_t> selectedIndices( wxListView* aList ) const;
    size_t boardSignature() const;

    PCB_EDIT_FRAME* m_frame;

    wxChoice*       m_modeChoice;
    wxChoice*       m_highlightChoice;
    wxCheckBox*     m_selectCheck;
    wxCheckBox*     m_zoomCheck;
    wxCheckBox*     m_clearExistingCheck;
    wxListView*     m_classList;
    wxListView*     m_objectList;
    wxListView*     m_primitiveList;
    wxStaticText*   m_objectCount;

    MODE                  m_mode;
    std::vector<wxString> m_classHeaders;
    std::vector<wxString> m_objectHeaders;
    std::vector<wxString> m_primitiveHeaders;
    std::vector<ROW>      m_classes;
    std::vector<ROW>      m_objects;
    std::vector<size_t>   m_shownObjects;   ///< indices into m_objects listed for the class
    std::vector<ROW>      m_primitives;

    std::set<KIID>        m_brightened;     ///< items this panel brightened
    bool                  m_highlighting;   ///< this panel turned net highlighting on
    bool                  m_updating;       ///< ignore list events while filling
    bool                  m_selectionPending;
    size_t                m_boardSig;
};

#endif // PCB_NAVIGATOR_PANEL_H
