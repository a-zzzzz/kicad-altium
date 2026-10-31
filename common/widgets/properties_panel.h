/*
 * This program source code file is part of KICAD, a free EDA CAD application.
 *
 * Copyright (C) 2016 CERN
 * @author Maciej Suminski <maciej.suminski@cern.ch>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */
#ifndef PROPERTIES_PANEL_H
#define PROPERTIES_PANEL_H

#include <wx/panel.h>
#include <wx/longlong.h>
#include <wx/propgrid/propgrid.h>
#include <widgets/properties_form.h>

#include <vector>
#include <memory>
#include <set>
#include <map>

class EDA_BASE_FRAME;
class EDA_DRAW_FRAME;
class EDA_ITEM;
class SELECTION;
class PROPERTY_BASE;
class wxStaticText;
class wxSearchCtrl;
class wxCommandEvent;

class PROPERTIES_PANEL : public wxPanel, public PROPERTIES_FORM_HOST
{
public:
    PROPERTIES_PANEL( wxWindow* aParent, EDA_BASE_FRAME* aFrame );

    virtual ~PROPERTIES_PANEL();

    virtual void UpdateData() = 0;

    virtual void AfterCommit() {}

    wxPropertyGrid* GetPropertyGrid()
    {
        return m_grid;
    }

    int PropertiesCount() const
    {
        return m_displayed.size();
    }

    const std::vector<PROPERTY_BASE*>& Properties() const
    {
        return m_displayed;
    }

    void RecalculateSplitterPos();

    void SetSplitterProportion( float aProportion );
    float SplitterProportion() const { return m_splitter_key_proportion; }

    /**
     * Show a section of controls (e.g. the selection filter) under the search box while nothing
     * is selected, as Altium's Properties panel does.  Sections stack in the order they are
     * added, or at \a aPosition (0 = first).  The panel takes over \a aWidget.
     */
    void SetDocumentWidget( wxWindow* aWidget, const wxString& aTitle, int aPosition = -1 );

    // PROPERTIES_FORM_HOST: section forms show (and edit) the grid's values
    wxPropertyGrid* FormGrid() override { return m_grid; }
    void FormApply( const std::vector<std::pair<wxString, wxVariant>>& aValues ) override;
    void FormLayoutChanged() override { relayout(); }

protected:
    /// Applies new values to the named properties of the selection, in one undo step.
    virtual void applyPropertyValues( const std::vector<std::pair<wxString, wxVariant>>& aValues ) {}

    /// Altium's header summary of the selection filter ("Components (and 12 more)"); empty hides
    /// the summary and its funnel button.
    virtual wxString selectionFilterSummary() const { return wxEmptyString; }

    /// The funnel button next to the summary was clicked.
    virtual void onSelectionFilterButton( wxWindow* aButton ) {}

    /// Refreshes the selection filter summary in the header.
    void updateFilterSummary();

    /// Tabs under the search box (Altium's General | Parameters ...) for a selection; none by
    /// default.  The first tab shows the sections; categoryTab() says which tab shows a category.
    virtual std::vector<wxString> panelTabs( const SELECTION& aSelection ) const { return {}; }

    /// The tab a set of panelTabs() opens on the first time it is shown
    virtual int defaultTab( const std::vector<wxString>& aTabs ) const { return 0; }
    /// (-1: not shown, except to answer a search)
    virtual int categoryTab( wxPGProperty* aCategory ) const { return 0; }
    int activeTab() const { return m_activeTab; }

    /**
     * Adds a section shown for one layout of the selection (see showSelectionSections()), above
     * the property rows.  The panel takes over  aWidget.
     */
    void addSelectionSection( wxWindow* aWidget, const wxString& aTitle, const wxString& aLayout );

    /**
     * As above, on the tab \a aTab of the layout (e.g. Altium's Parameters tab).  An empty title
     * shows the section without a header.
     */
    void addSelectionSection( wxWindow* aWidget, const wxString& aTitle, const wxString& aLayout,
                              int aTab );

    /// The layout whose selection sections show while nothing is selected (empty: none)
    virtual wxString documentLayout() const { return wxEmptyString; }

    /// Shows the selection sections of a layout (empty: none).
    void setSelectionLayout( const wxString& aLayout );

    /// Called after the grid shows a selection's values: choose the layout, the bound
    /// properties, and sync the section forms.
    virtual void selectionGridUpdated( const SELECTION& aSelection ) {}

    /// Shows or hides the selection sections for the layout, tab and search box.
    void updateSelectionSections();

    /**
     * Generates the property grid for a given selection of items.
     *
     * @param aSelection is a set of items to show properties for.
     */
    virtual void rebuildProperties( const SELECTION& aSelection );

    virtual wxPGProperty* createPGProperty( const PROPERTY_BASE* aProperty ) const = 0;

    /**
     * Shows the document options (General, plus whatever appendDocumentProperties() adds)
     * instead of an empty grid when nothing is selected.  Rows are created on the first call
     * and only refreshed afterwards, so it is cheap to call repeatedly.
     */
    void rebuildDocumentProperties();

    /// Name of an item's kind for the header ("Component", "Track", ...).
    virtual wxString itemTypeName( EDA_ITEM* aItem ) const;

    /// Header shown above the document options ("Board", "Document Options", ...).
    virtual wxString documentCaption() const;

    /// Adds editor-specific sections to the document options; use the docXxx() helpers.
    virtual void appendDocumentProperties() {}

    /// Handles a change to a document option.  @return true if handled.
    virtual bool documentValueChanged( const wxString& aName, const wxVariant& aValue );

    /// Altium's Grid Manager section: the grids (the ticked one is the snap grid), the fast
    /// grids and a button for the grid dialog.
    void appendGridManager( EDA_DRAW_FRAME* aFrame );

    wxPGProperty* docCategory( const wxString& aName, const wxString& aLabel );
    wxPGProperty* docText( wxPGProperty* aCategory, const wxString& aName, const wxString& aLabel,
                           const wxString& aValue, bool aReadOnly = true );
    wxPGProperty* docChoice( wxPGProperty* aCategory, const wxString& aName, const wxString& aLabel,
                             const wxArrayString& aChoices, int aSelection );
    wxPGProperty* docBool( wxPGProperty* aCategory, const wxString& aName, const wxString& aLabel,
                           bool aValue );

    /// Shows or hides the document widget section.
    void showDocumentWidget( bool aShow );

    /// Make the grid exactly as tall as its visible rows (the panel scrolls, not the grid).
    void fitGridHeight();
    void relayout();

    /// Sets the header and footer from the selection.
    void updateHeader( const SELECTION& aSelection );

    /// Hides rows (and emptied sections) that don't match the search box.
    void applyFilter();

    /// Re-applies remembered collapsed sections after the grid is rebuilt.
    void applyCollapsedState();

    // Event handlers
    virtual void valueChanging( wxPropertyGridEvent& aEvent ) { aEvent.Skip(); }
    virtual void valueChanged( wxPropertyGridEvent& aEvent ) { aEvent.Skip(); }
    void onCharHook( wxKeyEvent& aEvent );
    void onGridChanging( wxPropertyGridEvent& aEvent );
    void onGridChanged( wxPropertyGridEvent& aEvent );
    void onGridLeftDown( wxMouseEvent& aEvent );
    void onShow( wxShowEvent& aEvent );

    virtual void OnLanguageChanged( wxCommandEvent& aEvent );

    /**
     * Utility to fetch a property value and convert to wxVariant
     * Precondition: aItem is known to have property aProperty
     * @return true if conversion succeeded
     */
    virtual bool getItemValue( EDA_ITEM* aItem, PROPERTY_BASE* aProperty, wxVariant& aValue );

    /**
     * Processes a selection and determines whether the given property should be available or not
     * and what the common value should be for the items in the selection.
     * @param aSelection is a set of EDA_ITEMs to process
     * @param aProperty is the property to look up
     * @param aValue will be filled with the value common to the selection, or null if different
     * @param aWritable will be set to whether or not the property can be written for the selection
     * @return true if the property is available for all the items in the selection
     */
    bool extractValueAndWritability( const SELECTION& aSelection, const wxString& aPropName,
                                     wxVariant& aValue, bool& aWritable, wxPGChoices& aChoices );

public:
    int                         m_SuppressGridChangeEvents;

protected:
    std::vector<PROPERTY_BASE*> m_displayed;    // no ownership of pointers
    wxPropertyGrid*             m_grid;
    EDA_BASE_FRAME*             m_frame;
    wxStaticText*               m_caption;
    wxStaticText*               m_footer;
    wxSearchCtrl*               m_search;
    struct DOC_SECTION
    {
        wxWindow* widget;
        wxWindow* header;
        bool      folded = false;
        bool      below = false;    ///< under the grid (e.g. the Grid Manager)
    };

    /// Add a section under the grid (Altium's Grid Manager).
    void addDocumentSectionBelow( wxWindow* aWidget, const wxString& aTitle );

    wxWindow*                   m_gridManager = nullptr;
    class wxScrolledWindow*     m_scroll = nullptr;
    class wxBoxSizer*           m_scrollSizer = nullptr;
    int                         m_gridHeight = -1;
    bool                        m_relayouting = false;
    int                         m_widthFixes = 0;   ///< width refits since the last settle
    int                         m_wheelRemainder = 0;

    std::vector<DOC_SECTION>    m_docSections;
    bool                        m_docSectionsShown = true;

    struct SEL_SECTION
    {
        wxWindow* widget;
        wxWindow* header;
        wxString  layout;
        bool      folded = false;
        int       tab = 0;
        bool      headerless = false;
    };

    std::vector<SEL_SECTION>    m_selSections;
    wxString                    m_selLayout;

    /// Properties shown by the selection section forms: hidden from the grid (unless searching)
    std::set<wxString>          m_boundProps;

    std::vector<wxString>       m_tabs;
    int                         m_activeTab = 0;
    std::map<wxString, int>     m_lastTabs;    ///< tab set -> the tab it was left on
    class wxWindow*             m_tabBar = nullptr;
    wxStaticText*               m_filterSummary = nullptr;
    wxWindow*                   m_filterButton = nullptr;
    bool                        m_docRequested = false;

    void setTabs( const std::vector<wxString>& aTabs );
    void selectTab( int aTab );

    /// True when an editor shows its snap options as a section widget instead of grid rows.
    bool                        m_snapOptionsWidget = false;
    bool                        m_documentMode;
    bool                        m_wasOnScreen;
    wxLongLong                  m_lastDocumentRefresh;
    std::set<wxString>          m_collapsedGroups;   // by category label

    /// Proportion of the grid column splitter that is used for the key column (0.0 - 1.0)
    float m_splitter_key_proportion;
};

#endif /* PROPERTIES_PANEL_H */
