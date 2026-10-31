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

#ifndef DIALOG_RULE_WIZARD_H
#define DIALOG_RULE_WIZARD_H

#include <dialog_shim.h>
#include <drc/rule_editor/pcb_rules_model.h>

#include <memory>
#include <vector>

class PCB_EDIT_FRAME;
class BOARD;
class wxSimplebook;
class wxButton;
class wxTextCtrl;
class wxTreeCtrl;
class wxRadioButton;
class wxChoice;
class wxComboBox;
class wxCheckBox;
class wxCheckListBox;
class wxDataViewListCtrl;
class wxStaticText;
class wxPanel;
class wxBoxSizer;
class wxDataViewEvent;
class UNIT_BINDER;


/**
 * Design > Rule Wizard (Altium's "New Rule Wizard"): creates one custom design rule in the
 * project's .kicad_dru file, step by step -- rule type and name, what it applies to, its values,
 * its priority among the rules of the same type -- then optionally opens the rules editor.
 */
class DIALOG_RULE_WIZARD : public DIALOG_SHIM
{
public:
    DIALOG_RULE_WIZARD( PCB_EDIT_FRAME* aFrame );
    ~DIALOG_RULE_WIZARD() override;

    /// True when the user asked to open the main design rules dialog after Finish.
    bool LaunchRulesEditor() const { return m_launchEditor; }

private:
    enum PAGE
    {
        PAGE_INTRO = 0,
        PAGE_TYPE,
        PAGE_SCOPE,
        PAGE_VALUES,
        PAGE_PRIORITY,
        PAGE_DONE,
        PAGE_COUNT
    };

    struct FIELD_CTRL
    {
        int                          field = -1;
        std::unique_ptr<UNIT_BINDER> binder;
        wxTextCtrl*              text = nullptr;
        wxChoice*                choice = nullptr;
        wxCheckBox*              check = nullptr;
        std::vector<wxCheckBox*> checks;
    };

    wxPanel* makeIntroPage();
    wxPanel* makeTypePage();
    wxPanel* makeScopePage();
    wxPanel* makeValuesPage();
    wxPanel* makePriorityPage();
    wxPanel* makeDonePage();

    /// A page with the dark header band; the content goes into the returned sizer.
    wxPanel* makeHeaderPage( const wxString& aTitle, const wxString& aDescription,
                             wxBoxSizer*& aBody );

    void showPage( int aPage );
    bool leavePage( int aPage );

    int  selectedCategory() const;
    void updateScopeControls();
    void buildValueControls();
    bool buildRule( wxString& aError );
    void fillPriorityList();
    void movePriority( bool aUp );
    void updateSummary();
    bool finish();

    PCB_RULES::SCOPE currentScope( wxString& aError ) const;

    PCB_EDIT_FRAME*          m_frame;
    BOARD*                   m_board;
    wxString                 m_path;
    PCB_RULES::RULES_DOC     m_doc;
    int                      m_page;
    int                      m_ruleUid;        ///< uid of the new rule in m_doc, once built
    int                      m_ruleCat;        ///< category the new rule was built for
    int                      m_valuesCat;      ///< category the value controls are built for
    bool                     m_nameEdited;
    bool                     m_launchEditor;

    std::vector<wxString>    m_nets;
    std::vector<wxString>    m_layerNames;     ///< canonical layer names
    std::vector<wxString>    m_layerLabels;    ///< user layer names
    std::vector<wxString>    m_refs;

    wxSimplebook*            m_book;
    wxButton*                m_cancelBtn;
    wxButton*                m_backBtn;
    wxButton*                m_nextBtn;
    wxButton*                m_finishBtn;

    // Type page
    wxTextCtrl*              m_name;
    wxTextCtrl*              m_comment;
    wxTreeCtrl*              m_typeTree;

    // Scope page
    std::vector<wxRadioButton*> m_scopeRadios;
    wxComboBox*              m_netCombo;
    wxCheckListBox*          m_netList;
    wxChoice*                m_layerChoice;
    wxComboBox*              m_refCombo;
    wxTextCtrl*              m_query;
    wxStaticText*            m_netLabel;
    wxStaticText*            m_netsLabel;
    wxStaticText*            m_layerLabel;
    wxStaticText*            m_refLabel;
    wxStaticText*            m_queryLabel;

    // Values page
    wxPanel*                 m_valuesPanel;
    wxStaticText*            m_valuesHeading;
    std::vector<FIELD_CTRL>  m_fieldCtrls;

    // Priority page
    wxDataViewListCtrl*      m_priorityList;
    wxButton*                m_upBtn;
    wxButton*                m_downBtn;
    std::vector<int>         m_priorityUids;

    // Done page
    wxCheckBox*              m_launchCheck;
    wxStaticText*            m_summary;

    bool m_loadFailed = false;   ///< the existing rules file could not be read
};

#endif // DIALOG_RULE_WIZARD_H
