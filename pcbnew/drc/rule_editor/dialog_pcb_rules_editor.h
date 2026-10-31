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

#ifndef DIALOG_PCB_RULES_EDITOR_H
#define DIALOG_PCB_RULES_EDITOR_H

#include <map>
#include <memory>
#include <vector>

#include <dialog_shim.h>
#include <eda_units.h>

#include "pcb_rules_model.h"

class BOARD;
class PCB_EDIT_FRAME;
class UNIT_BINDER;
class wxCheckBox;
class wxChoice;
class wxComboBox;
class wxGrid;
class wxGridEvent;
class wxScrolledWindow;
class wxSplitterWindow;
class wxStaticText;
class wxTextCtrl;
class wxTreeCtrl;
class wxTreeEvent;
class wxTreeItemId;


/**
 * PCB Rules and Constraints Editor (Design > Rules..., D R).
 *
 * Left: a tree of rule categories grouped as Electrical, Routing, ... with the named rules of
 * each category.  Right: a summary grid of the rules under the selected tree node, or the
 * editor of the selected rule (name, comment, object scopes and constraint values).
 *
 * Rules are the project's custom design rules (.kicad_dru); OK/Apply write that file and reload
 * the DRC engine.  Board Setup minimums appear as each category's "Board Setup" default rule.
 */
class DIALOG_PCB_RULES_EDITOR : public DIALOG_SHIM
{
public:
    DIALOG_PCB_RULES_EDITOR( PCB_EDIT_FRAME* aFrame );
    ~DIALOG_PCB_RULES_EDITOR() override;

    bool TransferDataFromWindow() override;

    enum class NODE
    {
        ROOT,
        GROUP,
        CATEGORY,
        RULE,
        BOARD
    };

    struct NODE_KEY
    {
        NODE     kind = NODE::ROOT;
        int      cat = -1;
        int      uid = 0;
        wxString group;
    };

private:
    // Tree
    void         rebuildTree( const NODE_KEY& aSelect );
    bool         selectKey( const NODE_KEY& aKey );
    NODE_KEY     currentKey() const;
    void         onTreeSelChanging( wxTreeEvent& aEvent );
    void         onTreeSelChanged( wxTreeEvent& aEvent );
    void         onTreeMenu( wxTreeEvent& aEvent );
    void         showContextMenu( const NODE_KEY& aKey );
    void         runMenuCommand( int aId, const NODE_KEY& aKey );
    void         scheduleRefresh();

    // Pages
    void clearPage();
    void showPage( const NODE_KEY& aKey );
    void showSummary( const NODE_KEY& aKey );
    void showRule( int aCat, int aUid );
    void showBoardDefaults( int aCat );
    void showUnsupported( int aCat );
    void showCustomRule( int aUid );
    void addPageTitle( const wxString& aTitle, const wxString& aSubtitle );

    struct SCOPE_CTRLS
    {
        wxChoice*        kind = nullptr;
        wxComboBox*      value = nullptr;
        wxStaticText*    valueLabel = nullptr;
        wxChoice*        layer = nullptr;
        wxStaticText*    layerLabel = nullptr;
        wxTextCtrl*      query = nullptr;
        wxStaticText*    preview = nullptr;
        PCB_RULES::SCOPE initial;
        int              shownKind = -1;
    };

    void             buildScopeBox( wxSizer* aSizer, int aIndex, const wxString& aTitle,
                                    const PCB_RULES::SCOPE& aScope, bool aEnabled );
    void             updateScopeCtrls( int aIndex );
    PCB_RULES::SCOPE readScope( int aIndex ) const;

    struct FIELD_CTRL
    {
        int                          field = -1;
        wxTextCtrl*                  text = nullptr;
        std::unique_ptr<UNIT_BINDER> binder;
        wxChoice*                    choice = nullptr;
        wxCheckBox*                  check = nullptr;
        std::vector<wxCheckBox*>     checks;
        PCB_RULES::FIELD_VALUE       initial;
    };

    void buildFieldCtrls( wxWindow* aParent, wxSizer* aSizer, int aCat,
                          const std::vector<int>& aFields,
                          const std::vector<PCB_RULES::FIELD_VALUE>& aValues );
    bool readField( const FIELD_CTRL& aCtrl, PCB_RULES::FIELD_VALUE& aValue, wxString& aError );

    // Commit the shown page to the model.  Returns false (after telling the user) if invalid.
    bool commitPage();
    bool commitRule();
    bool commitBoardDefaults();
    bool commitCustomRule();

    // Summary grid
    void onGridClick( wxGridEvent& aEvent );
    void onGridDClick( wxGridEvent& aEvent );
    void onGridMenu( wxGridEvent& aEvent );

    // Commands
    void newRule( int aCat, const PCB_RULES::SCOPE& aScope = PCB_RULES::SCOPE(),
                  const wxString& aName = wxEmptyString );
    void duplicateRule( int aCat, int aUid );
    void deleteRule( int aCat, int aUid );
    void onRuleWizard( wxCommandEvent& aEvent );
    void onPriorities( wxCommandEvent& aEvent );
    void onCreateDefaults( wxCommandEvent& aEvent );
    void onApply( wxCommandEvent& aEvent );
    bool applyChanges();

    /// True if anything (including the shown page) differs from what was last applied.
    bool hasUnappliedChanges();
    bool pageModified();

    /// Asks "Discard changes?" if there are unapplied changes.  @return true to close.
    bool confirmDiscard();

    int  boardValue( int BOARD_DESIGN_SETTINGS::*aMember ) const;
    int  categoryFromKey( const NODE_KEY& aKey ) const;

    std::vector<wxString> netNames() const;
    std::vector<wxString> netclassNames() const;

private:
    PCB_EDIT_FRAME*      m_frame;
    BOARD*               m_board;
    EDA_UNITS            m_units;
    PCB_RULES::RULES_DOC m_doc;
    bool                 m_dirty;
    bool                 m_building;
    bool                 m_refreshPending;
    wxString             m_loadError;

    // Pending Board Setup edits (pointers to members have no ordering, hence a vector)
    std::vector<std::pair<int BOARD_DESIGN_SETTINGS::*, int>> m_boardEdits;

    wxSplitterWindow* m_splitter;
    wxTreeCtrl*       m_tree;
    wxScrolledWindow* m_page;
    wxSizer*          m_pageSizer;

    // Page state
    enum class PAGE
    {
        NONE,
        SUMMARY,
        RULE,
        BOARD,
        UNSUPPORTED,
        CUSTOM
    };

    PAGE                    m_pageKind;
    int                     m_pageCat;
    int                     m_pageUid;
    wxTextCtrl*             m_nameCtrl;
    wxTextCtrl*             m_commentCtrl;
    wxString                m_initialName;
    wxString                m_initialComment;
    SCOPE_CTRLS             m_scope[2];
    std::vector<wxString>   m_hiddenTerms;
    wxChoice*               m_layerChoice;
    std::vector<wxString>   m_layerClauses;
    int                     m_initialLayerSel;
    std::vector<FIELD_CTRL> m_fields;
    wxTextCtrl*             m_rawCtrl;
    wxString                m_rawInitial;
    wxGrid*                 m_grid;
    std::vector<NODE_KEY>   m_gridRows;

    std::vector<wxString>   m_layerNames;     ///< canonical names, UI order
    std::vector<wxString>   m_layerLabels;    ///< user names, UI order
};

#endif // DIALOG_PCB_RULES_EDITOR_H
