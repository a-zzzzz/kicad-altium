/*
 * This program source code file is part of KICAD, a free EDA CAD application.
 *
 * Copyright (C) 2020 CERN
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * @author Maciej Suminski <maciej.suminski@cern.ch>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef PCB_PROPERTIES_PANEL_H
#define PCB_PROPERTIES_PANEL_H

#include <widgets/properties_panel.h>
#include <functional>
#include <set>

class SELECTION;
class BOARD;
class PCB_BASE_EDIT_FRAME;
class PROPERTY_MANAGER;
class PG_UNIT_EDITOR;
class PG_CHECKBOX_EDITOR;
class PG_RATIO_EDITOR;
class PG_NET_SELECTOR_EDITOR;
class PG_FPID_EDITOR;
class PG_URL_EDITOR;
class PROPERTIES_FORM;
class BOARD_ITEM;
class BOARD_COMMIT;
class ROUTER_TOOL;

class PCB_PROPERTIES_PANEL : public PROPERTIES_PANEL
{
public:
    PCB_PROPERTIES_PANEL( wxWindow* aParent, PCB_BASE_EDIT_FRAME* aFrame );

    virtual ~PCB_PROPERTIES_PANEL();

    void UpdateData() override;

    void AfterCommit() override;

    PCB_BASE_EDIT_FRAME* GetFrame() const { return m_frame; }

protected:
    void rebuildProperties( const SELECTION& aSelection ) override;
    wxPGProperty* createPGProperty( const PROPERTY_BASE* aProperty ) const override;
    wxString documentCaption() const override;
    wxString itemTypeName( EDA_ITEM* aItem ) const override;
    void appendDocumentProperties() override;
    bool documentValueChanged( const wxString& aName, const wxVariant& aValue ) override;

    /// Altium's Interactive Routing page: while a track is being routed (and nothing is
    /// selected) the panel shows the route's options instead of the board's
    void appendRoutingProperties();

    /// The router, while it routes a single track; nullptr otherwise
    ROUTER_TOOL* activeRouter() const;
    void buildRoutingLayout();
    bool m_routingShown = false;
    bool getItemValue( EDA_ITEM* aItem, PROPERTY_BASE* aProperty, wxVariant& aValue ) override;

    PROPERTY_BASE* getPropertyFromEvent( const wxPropertyGridEvent& aEvent ) const;

    void valueChanging( wxPropertyGridEvent& aEvent ) override;
    void valueChanged( wxPropertyGridEvent& aEvent ) override;

    /// Applies new values to the selection's properties in one commit (grid edits and the
    /// Altium sections' edits both come here).
    void applyPropertyValues( const std::vector<std::pair<wxString, wxVariant>>& aValues ) override;

    // Altium's per-object layouts of the panel (pcb_properties_layouts.cpp)
    void selectionGridUpdated( const SELECTION& aSelection ) override;
    std::vector<wxString> panelTabs( const SELECTION& aSelection ) const override;

    /// Altium's PcbLib Library Options open on the Footprint tab
    int defaultTab( const std::vector<wxString>& aTabs ) const override;
    int categoryTab( wxPGProperty* aCategory ) const override;
    wxString selectionFilterSummary() const override;
    void onSelectionFilterButton( wxWindow* aButton ) override;

    /// The Altium layout for a selection ("component", "polygon", "via", "pad", "track"), or
    /// empty for KiCad's plain property list.
    wxString layoutFor( const SELECTION& aSelection ) const;

    void buildAltiumLayouts();
    PROPERTIES_FORM* addLayoutForm( const wxString& aTitle, const wxString& aLayout );
    void buildNetInformation( const wxString& aLayout );
    void buildComponentLayout();
    void buildPolygonLayout();
    void buildViaLayout();
    void buildPadLayout();
    void buildTrackLayout();
    void buildTextLayout();
    void buildArcLayout();
    void buildLineLayout();
    void buildFillLayout();
    void buildRegionLayout();
    void buildDimensionLayout();
    void buildKeepoutLayout();
    void buildLibraryFootprintLayout();

    /// Altium's Text "Frame" mode: converts texts to text boxes (aFrame) or back, in one commit
    void convertTexts( bool aFrame );

    /// Altium's Outline Vertices section for the single selected zone or polygon shape
    void buildOutlineVertices( const wxString& aLayout );

    /// Altium's Solder Mask Expansion section for tracks and graphic shapes on outer copper
    void buildShapeSolderMask( const wxString& aLayout );

    /// The layout of the footprint editor while nothing is selected (Library Options)
    wxString documentLayout() const override;
    void buildBoardInformation();
    void updateBoardInformation();
    void appendBoardParameters();
    void appendHealthCheck();
    void syncLayoutForms();

    /// True in the board editor; the footprint editor (Altium's PcbLib) has no nets
    bool isBoardEditor() const;

    /// The selection the panel shows: in the footprint editor an empty selection is the
    /// library's own options, not the footprint
    const SELECTION& shownSelection( SELECTION& aFallbackSelection );

    /// The text a set of selected items has in common (empty if they differ)
    wxString commonText( const std::function<bool( BOARD_ITEM*, wxString& )>& aText );

    /// The selected board items
    std::vector<BOARD_ITEM*> selectedItems();

    /// The only selected item, or nullptr
    BOARD_ITEM* singleItem();

    /// Changes the selection in one commit; aChange stages the items it changes.
    void commitSelection( const wxString& aMessage,
                          const std::function<void( BOARD_COMMIT&, BOARD_ITEM* )>& aChange );

    /// A value for a property, parsed from text the way the grid would (e.g. "45" for an angle)
    bool parsePropertyValue( const wxString& aName, const wxString& aText, wxVariant& aValue );

    ///< Regenerates caches storing layer and net names
    void updateLists( const BOARD* aBoard );

    /**
     * Get the current selection from the selection tool.
     * If the selection is empty and we're in the footprint editor, returns the footprint instead.
     *
     * @param aSelection [out] reference to a SELECTION pointer that will be set to the selection
     * @param aFallbackSelection [out] local SELECTION object for fallback footprint selection
     * @return const SELECTION& reference to the selection (either real selection or fallback)
     */
    const SELECTION& getSelection( SELECTION& aFallbackSelection );

    /**
     * Get the front item of the current selection.
     * If the selection is empty and we're in the footprint editor, returns the footprint instead.
     *
     * @return EDA_ITEM* pointer to the front item, or nullptr if no selection
     */
    EDA_ITEM* getFrontItem();

protected:
    PCB_BASE_EDIT_FRAME* m_frame;
    PROPERTY_MANAGER&    m_propMgr;
    PG_UNIT_EDITOR*      m_unitEditorInstance;
    PG_CHECKBOX_EDITOR*  m_checkboxEditorInstance;
    PG_RATIO_EDITOR*     m_ratioEditorInstance;
    PG_NET_SELECTOR_EDITOR* m_netSelectorEditorInstance;
    PG_FPID_EDITOR*      m_fpEditorInstance;
    PG_URL_EDITOR*       m_urlEditorInstance;

    static std::set<wxString> m_currentFieldNames;
    wxPGChoices m_nets;

    struct LAYOUT_FORM
    {
        wxString         layout;
        PROPERTIES_FORM* form;
    };

    std::vector<LAYOUT_FORM> m_layoutForms;
    wxString                 m_currentLayout;
    PROPERTIES_FORM*         m_boardInfo = nullptr;
    bool                     m_showPreview = true;
    int                      m_vertexRow = -1;
    int                      m_fpParamRow = -1;      ///< selected row of the footprint Parameters
    const void*              m_vertexItem = nullptr;

    /// Board Information values, refreshed with the document rows
    std::vector<std::pair<wxString, wxString>> m_boardStats;
};

#endif /* PCB_PROPERTIES_PANEL_H */
