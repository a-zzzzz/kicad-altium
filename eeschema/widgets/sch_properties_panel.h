/*
 * This program source code file is part of KICAD, a free EDA CAD application.
 *
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

#pragma once

#include <widgets/properties_panel.h>
#include <functional>
#include <set>
#include <string>

class SELECTION;
class SCHEMATIC;
class SCH_BASE_FRAME;
class SCH_COMMIT;
class SCH_EDIT_FRAME;
class SCH_SHEET;
class PROPERTY_MANAGER;
class PG_UNIT_EDITOR;
class PG_CHECKBOX_EDITOR;
class PG_COLOR_EDITOR;
class PG_FPID_EDITOR;
class PG_URL_EDITOR;
class PROPERTIES_FORM;
class SCH_ITEM;
class SCH_FIELD;
class FOOTPRINT_PREVIEW_WIDGET;

class SCH_PROPERTIES_PANEL : public PROPERTIES_PANEL
{
public:
    SCH_PROPERTIES_PANEL( wxWindow* aParent, SCH_BASE_FRAME* aFrame );

    virtual ~SCH_PROPERTIES_PANEL();

    void UpdateData() override;

    void AfterCommit() override;

protected:
    void rebuildProperties( const SELECTION& aSelection ) override;
    wxPGProperty* createPGProperty( const PROPERTY_BASE* aProperty ) const override;
    void appendDocumentProperties() override;
    bool getItemValue( EDA_ITEM* aItem, PROPERTY_BASE* aProperty, wxVariant& aValue ) override;

    PROPERTY_BASE* getPropertyFromEvent( const wxPropertyGridEvent& aEvent ) const;

    void valueChanging( wxPropertyGridEvent& aEvent ) override;
    void valueChanged( wxPropertyGridEvent& aEvent ) override;

    bool handleSheetFilenameChange( SCH_EDIT_FRAME* aFrame, SCH_SHEET* aSheet,
                                    SCH_COMMIT& aChanges, const wxString& aNewFilename );

    /// Applies new values to the selection's properties in one commit (grid edits and the
    /// Altium sections' edits both come here).
    void applyPropertyValues( const std::vector<std::pair<wxString, wxVariant>>& aValues ) override;

    /// The pins and footprint filters of the selected symbols, for the footprint chooser
    std::string symbolNetlist();

    // Altium's per-object layouts of the panel (sch_properties_layouts.cpp)
    wxString itemTypeName( EDA_ITEM* aItem ) const override;
    wxString documentCaption() const override;
    void selectionGridUpdated( const SELECTION& aSelection ) override;
    std::vector<wxString> panelTabs( const SELECTION& aSelection ) const override;
    int categoryTab( wxPGProperty* aCategory ) const override;
    wxString documentLayout() const override;

    /// The Altium layout for a selection ("component", "wire", "port", ...), or empty for
    /// KiCad's plain property list.
    wxString layoutFor( const SELECTION& aSelection ) const;

    /// Properties Altium shows that KiCad items have but don't register (text locations, sheet
    /// sizes); added to the property manager once.
    static void registerAltiumProperties();

    void buildAltiumLayouts();
    PROPERTIES_FORM* addLayoutForm( const wxString& aTitle, const wxString& aLayout, int aTab = 0 );
    void buildComponentLayout( const wxString& aLayout );
    void buildFootprintPreview( const wxString& aLayout );
    void buildLocalColors( PROPERTIES_FORM* aForm );
    void buildPartChoices( const wxString& aLayout );
    void buildParametersSection( const wxString& aLayout, int aTab, const wxString& aTitle );
    void buildNetSections( const wxString& aLayout );
    void addFontRows( PROPERTIES_FORM* aForm );
    void addValueFontRows( PROPERTIES_FORM* aForm );
    void buildPinsTab( const wxString& aLayout );
    void buildPowerPortLayout();
    void buildWireLayout( const wxString& aLayout );
    void buildLabelLayout( const wxString& aLayout );
    void buildSheetLayout();
    void buildSheetEntryLayout();
    void buildTextLayout();
    void buildTextFrameLayout();
    void buildNoErcLayout();
    void buildJunctionLayout();
    void buildRectangleLayout();
    void buildPinLayout();
    void buildDocumentLayout();
    void syncLayoutForms();
    void updateFootprintPreview();

    /// A colour row: a swatch with Default / Custom... (the colour picker)
    void addColorRow( PROPERTIES_FORM* aForm, const wxString& aProp, const wxString& aLabel );

    /// The selected schematic items
    std::vector<SCH_ITEM*> selectedItems();

    /// The only selected item, or nullptr
    SCH_ITEM* singleItem();

    /// Changes the selection in one commit; aChange stages the items it changes.
    void commitSelection( const wxString& aMessage,
                          const std::function<void( SCH_COMMIT&, SCH_ITEM* )>& aChange );

    /// The fields (Altium's parameters) of a symbol, library symbol or sheet
    static std::vector<SCH_FIELD*> itemFields( SCH_ITEM* aItem );

    void OnLanguageChanged( wxCommandEvent& aEvent ) override;

    /**
     * Get the current selection from the selection tool.
     * If the selection is empty and we're in the symbol editor, returns the current symbol instead.
     *
     * @param aFallbackSelection [out] local SELECTION object for fallback symbol selection
     * @return const SELECTION& reference to the selection (either real selection or fallback)
     */
    const SELECTION& getSelection( SELECTION& aFallbackSelection );

    /**
     * Get the front item of the current selection.
     * If the selection is empty and we're in the symbol editor, returns the current symbol instead.
     *
     * @return EDA_ITEM* pointer to the front item, or nullptr if no selection
     */
    EDA_ITEM* getFrontItem();

protected:
    SCH_BASE_FRAME*     m_frame;
    PROPERTY_MANAGER&   m_propMgr;
    PG_UNIT_EDITOR*     m_unitEditorInstance;
    PG_CHECKBOX_EDITOR* m_checkboxEditorInstance;
    PG_COLOR_EDITOR*    m_colorEditorInstance;
    PG_FPID_EDITOR*     m_fpEditorInstance;
    PG_URL_EDITOR*      m_urlEditorInstance;

    static std::set<wxString> m_currentSymbolFieldNames;
    static std::set<wxString> m_currentSheetFieldNames;
    wxPGChoices               m_nets;

    struct LAYOUT_FORM
    {
        wxString         layout;
        PROPERTIES_FORM* form;
    };

    std::vector<LAYOUT_FORM>  m_layoutForms;
    wxString                  m_currentLayout;
    int                       m_paramFilter = 0;      ///< Parameters tab: All, Footprints, ...
    int                       m_paramRow = -1;        ///< selected row of the parameter table
    int                       m_docParamRow = -1;     ///< ... of the document's parameters
    int                       m_docParamFilter = 0;
    int                       m_vertexRow = -1;
    const void*               m_paramItem = nullptr;
    bool                      m_showPreview = false;
    int                       m_netFilter = 0;        ///< Parameters (Net): All, Parameters, ...
    wxWindow*                 m_fpPreviewHolder = nullptr;
    FOOTPRINT_PREVIEW_WIDGET* m_fpPreview = nullptr;
    wxString                  m_fpPreviewId;
};
