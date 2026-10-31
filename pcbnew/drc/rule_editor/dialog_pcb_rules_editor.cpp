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

#include "dialog_pcb_rules_editor.h"

#include <algorithm>
#include <functional>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/combobox.h>
#include <wx/display.h>
#include <wx/grid.h>
#include <wx/listbox.h>
#include <wx/listctrl.h>
#include <wx/menu.h>
#include <wx/scrolwin.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/treectrl.h>

#include <base_units.h>
#include <board.h>
#include <board_design_settings.h>
#include <confirm.h>
#include <kidialog.h>
#include <drc/drc_engine.h>
#include <lset.h>
#include <netclass.h>
#include <netinfo.h>
#include <pcb_edit_frame.h>
#include <project.h>
#include <project/net_settings.h>
#include <widgets/unit_binder.h>
#include <ki_exception.h>

using namespace PCB_RULES;
using NODE = DIALOG_PCB_RULES_EDITOR::NODE;
using NODE_KEY = DIALOG_PCB_RULES_EDITOR::NODE_KEY;


namespace
{

enum MENU_IDS
{
    ID_NEW_RULE = wxID_HIGHEST + 3100,
    ID_DUPLICATE_RULE,
    ID_DELETE_RULE,
    ID_PRIORITIES,
    ID_TOGGLE_ENABLED
};

enum GRID_COLS
{
    COL_NAME = 0,
    COL_PRIORITY,
    COL_ENABLED,
    COL_TYPE,
    COL_CATEGORY,
    COL_SCOPE1,
    COL_SCOPE2,
    COL_ATTRIBUTES,
    COL_COUNT
};


class RULES_TREE_DATA : public wxTreeItemData
{
public:
    explicit RULES_TREE_DATA( const NODE_KEY& aKey ) : m_key( aKey ) {}

    NODE_KEY m_key;
};


bool sameKey( const NODE_KEY& a, const NODE_KEY& b )
{
    if( a.kind != b.kind )
        return false;

    switch( a.kind )
    {
    case NODE::ROOT:     return true;
    case NODE::GROUP:    return a.group == b.group;
    case NODE::CATEGORY:
    case NODE::BOARD:    return a.cat == b.cat;
    case NODE::RULE:     return a.cat == b.cat && a.uid == b.uid;
    }

    return false;
}


wxTreeItemId findItem( wxTreeCtrl* aTree, const wxTreeItemId& aParent, const NODE_KEY& aKey )
{
    if( !aParent.IsOk() )
        return wxTreeItemId();

    if( RULES_TREE_DATA* data = dynamic_cast<RULES_TREE_DATA*>( aTree->GetItemData( aParent ) ) )
    {
        if( sameKey( data->m_key, aKey ) )
            return aParent;
    }

    wxTreeItemIdValue cookie;

    for( wxTreeItemId child = aTree->GetFirstChild( aParent, cookie ); child.IsOk();
         child = aTree->GetNextChild( aParent, cookie ) )
    {
        wxTreeItemId found = findItem( aTree, child, aKey );

        if( found.IsOk() )
            return found;
    }

    return wxTreeItemId();
}


const wxString& scopeKindLabel( int aKind )
{
    static const wxString labels[] = { _( "All" ), _( "Net" ), _( "Net Class" ), _( "Layer" ),
                                       _( "Net and Layer" ), _( "Custom Query" ) };
    return labels[std::clamp( aKind, 0, 5 )];
}


bool containsBadChars( const wxString& aText, const wxString& aBad )
{
    for( wxChar c : aBad )
    {
        if( aText.Contains( wxString( c ) ) )
            return true;
    }

    return false;
}


wxString netclassRowAttributes( const NETCLASS* aNc, const wxString& aCatId, EDA_UNITS aUnits )
{
    auto fmt = [&]( int aValue )
    {
        return EDA_UNIT_UTILS::UI::MessageTextFromValue( pcbIUScale, aUnits, aValue );
    };

    wxString out;

    auto add = [&]( const wxString& aLabel, bool aHas, int aValue )
    {
        if( !aHas )
            return;

        if( !out.IsEmpty() )
            out << wxS( "   " );

        out << aLabel << wxS( " = " ) << fmt( aValue );
    };

    if( aCatId == wxS( "clearance" ) )
    {
        add( _( "Minimum Clearance" ), aNc->HasClearance(), aNc->GetClearance() );
    }
    else if( aCatId == wxS( "width" ) )
    {
        add( _( "Preferred Width" ), aNc->HasTrackWidth(), aNc->GetTrackWidth() );
    }
    else if( aCatId == wxS( "via_style" ) )
    {
        add( _( "Via Diameter Preferred" ), aNc->HasViaDiameter(), aNc->GetViaDiameter() );
        add( _( "Via Hole Size Preferred" ), aNc->HasViaDrill(), aNc->GetViaDrill() );
    }
    else if( aCatId == wxS( "diff_pairs" ) )
    {
        add( _( "Preferred Width" ), aNc->HasDiffPairWidth(), aNc->GetDiffPairWidth() );
        add( _( "Preferred Gap" ), aNc->HasDiffPairGap(), aNc->GetDiffPairGap() );
    }

    return out;
}


// ------------------------------------------------------------------------------------------
// Priorities dialog
// ------------------------------------------------------------------------------------------

class DIALOG_RULE_PRIORITIES : public DIALOG_SHIM
{
public:
    DIALOG_RULE_PRIORITIES( wxWindow* aParent, RULES_DOC& aDoc, int aCat, EDA_UNITS aUnits ) :
            DIALOG_SHIM( aParent, wxID_ANY, _( "Edit Rule Priorities" ), wxDefaultPosition,
                         wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER ),
            m_doc( aDoc ),
            m_units( aUnits ),
            m_changed( false )
    {
        wxBoxSizer* main = new wxBoxSizer( wxVERTICAL );

        wxBoxSizer* top = new wxBoxSizer( wxHORIZONTAL );
        top->Add( new wxStaticText( this, wxID_ANY, _( "Rule Type:" ) ), 0,
                  wxALIGN_CENTER_VERTICAL | wxRIGHT, 5 );
        m_typeChoice = new wxChoice( this, wxID_ANY );
        top->Add( m_typeChoice, 1, wxEXPAND );
        main->Add( top, 0, wxEXPAND | wxALL, 8 );

        int initial = -1;

        for( int i = 0; i < (int) Categories().size(); ++i )
        {
            if( !Categories()[i].supported || m_doc.RulesInCategory( i ).empty() )
                continue;

            m_cats.push_back( i );
            m_typeChoice->Append( Categories()[i].name );

            if( i == aCat )
                initial = (int) m_cats.size() - 1;
        }

        if( initial < 0 && !m_cats.empty() )
            initial = 0;

        if( initial >= 0 )
            m_typeChoice->SetSelection( initial );

        wxBoxSizer* body = new wxBoxSizer( wxHORIZONTAL );
        m_list = new wxListCtrl( this, wxID_ANY, wxDefaultPosition, wxSize( 640, 260 ),
                                 wxLC_REPORT | wxLC_SINGLE_SEL );
        m_list->AppendColumn( _( "Priority" ), wxLIST_FORMAT_LEFT, 60 );
        m_list->AppendColumn( _( "Enabled" ), wxLIST_FORMAT_LEFT, 60 );
        m_list->AppendColumn( _( "Name" ), wxLIST_FORMAT_LEFT, 150 );
        m_list->AppendColumn( _( "Scope" ), wxLIST_FORMAT_LEFT, 200 );
        m_list->AppendColumn( _( "Attributes" ), wxLIST_FORMAT_LEFT, 260 );
        body->Add( m_list, 1, wxEXPAND | wxRIGHT, 8 );

        wxBoxSizer* buttons = new wxBoxSizer( wxVERTICAL );
        m_upBtn = new wxButton( this, wxID_ANY, _( "Increase Priority" ) );
        m_downBtn = new wxButton( this, wxID_ANY, _( "Decrease Priority" ) );
        buttons->Add( m_upBtn, 0, wxEXPAND | wxBOTTOM, 5 );
        buttons->Add( m_downBtn, 0, wxEXPAND );
        body->Add( buttons, 0 );
        main->Add( body, 1, wxEXPAND | wxLEFT | wxRIGHT, 8 );

        main->Add( new wxStaticText( this, wxID_ANY,
                                     _( "Priority 1 is the highest: where several rules of a type "
                                        "match an object, the highest priority rule applies." ) ),
                   0, wxALL, 8 );

        wxStdDialogButtonSizer* sdb = new wxStdDialogButtonSizer();
        sdb->AddButton( new wxButton( this, wxID_OK, _( "Close" ) ) );
        sdb->Realize();
        main->Add( sdb, 0, wxEXPAND | wxALL, 8 );

        SetSizer( main );

        m_typeChoice->Bind( wxEVT_CHOICE, [this]( wxCommandEvent& ) { fill( -1 ); } );
        m_upBtn->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { move( true ); } );
        m_downBtn->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { move( false ); } );
        m_list->Bind( wxEVT_LIST_ITEM_SELECTED, [this]( wxListEvent& ) { updateButtons(); } );
        m_list->Bind( wxEVT_LIST_ITEM_DESELECTED, [this]( wxListEvent& ) { updateButtons(); } );

        fill( -1 );
        finishDialogSettings();
    }

    bool Changed() const { return m_changed; }

private:
    int currentCat() const
    {
        int sel = m_typeChoice->GetSelection();
        return sel >= 0 && sel < (int) m_cats.size() ? m_cats[sel] : -1;
    }

    void fill( int aSelectUid )
    {
        m_list->DeleteAllItems();
        m_uids.clear();

        int cat = currentCat();

        if( cat < 0 )
        {
            updateButtons();
            return;
        }

        const CATEGORY&    c = Categories()[cat];
        std::vector<RULE*> rules = m_doc.RulesInCategory( cat );

        for( size_t i = 0; i < rules.size(); ++i )
        {
            RULE*                 r = rules[i];
            SCOPE                 s1, s2;
            std::vector<wxString> hidden;
            ParseScopes( r->condition, c, s1, s2, hidden );

            wxString scope = s1.Describe( 'A' );

            if( c.binary )
                scope << wxS( "  |  " ) << s2.Describe( 'B' );

            long row = m_list->InsertItem( (long) i, wxString::Format( wxS( "%zu" ), i + 1 ) );
            m_list->SetItem( row, 1, r->enabled ? _( "Yes" ) : _( "No" ) );
            m_list->SetItem( row, 2, r->name );
            m_list->SetItem( row, 3, scope );
            m_list->SetItem( row, 4, m_doc.Attributes( *r, cat, m_units ) );
            m_uids.push_back( r->uid );

            if( r->uid == aSelectUid )
            {
                m_list->SetItemState( row, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                                      wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED );
                m_list->EnsureVisible( row );
            }
        }

        updateButtons();
    }

    int selectedRow() const
    {
        return (int) m_list->GetNextItem( -1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED );
    }

    void updateButtons()
    {
        int row = selectedRow();
        m_upBtn->Enable( row > 0 );
        m_downBtn->Enable( row >= 0 && row + 1 < (int) m_uids.size() );
    }

    void move( bool aUp )
    {
        int row = selectedRow();

        if( row < 0 || row >= (int) m_uids.size() )
            return;

        int uid = m_uids[row];

        if( m_doc.MovePriority( currentCat(), uid, aUp ) )
        {
            m_changed = true;
            fill( uid );
        }
    }

    RULES_DOC&       m_doc;
    EDA_UNITS        m_units;
    bool             m_changed;
    wxChoice*        m_typeChoice;
    wxListCtrl*      m_list;
    wxButton*        m_upBtn;
    wxButton*        m_downBtn;
    std::vector<int> m_cats;
    std::vector<int> m_uids;
};


// ------------------------------------------------------------------------------------------
// Rule wizard
// ------------------------------------------------------------------------------------------

class DIALOG_RULE_WIZARD : public DIALOG_SHIM
{
public:
    DIALOG_RULE_WIZARD( wxWindow* aParent, RULES_DOC& aDoc, int aCat,
                        const std::vector<wxString>& aNets,
                        const std::vector<wxString>& aClasses,
                        const std::vector<wxString>& aLayers ) :
            DIALOG_SHIM( aParent, wxID_ANY, _( "New Rule Wizard" ), wxDefaultPosition,
                         wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER ),
            m_doc( aDoc ),
            m_nets( aNets ),
            m_classes( aClasses ),
            m_layers( aLayers ),
            m_nameEdited( false )
    {
        wxBoxSizer* main = new wxBoxSizer( wxVERTICAL );

        main->Add( new wxStaticText( this, wxID_ANY, _( "1. Choose the rule type:" ) ), 0,
                   wxLEFT | wxRIGHT | wxTOP, 10 );

        m_typeList = new wxListBox( this, wxID_ANY, wxDefaultPosition, wxSize( 420, 260 ) );
        int sel = 0;

        for( int i = 0; i < (int) Categories().size(); ++i )
        {
            const CATEGORY& c = Categories()[i];

            if( !c.supported )
                continue;

            if( i == aCat )
                sel = (int) m_cats.size();

            m_cats.push_back( i );
            m_typeList->Append( c.group + wxS( "  ›  " ) + c.name );
        }

        main->Add( m_typeList, 1, wxEXPAND | wxALL, 10 );

        wxFlexGridSizer* grid = new wxFlexGridSizer( 2, 5, 8 );
        grid->AddGrowableCol( 1 );

        grid->Add( new wxStaticText( this, wxID_ANY, _( "2. Name:" ) ), 0,
                   wxALIGN_CENTER_VERTICAL );
        m_name = new wxTextCtrl( this, wxID_ANY );
        grid->Add( m_name, 1, wxEXPAND );

        grid->Add( new wxStaticText( this, wxID_ANY, _( "3. Applies to:" ) ), 0,
                   wxALIGN_CENTER_VERTICAL );
        m_scopeKind = new wxChoice( this, wxID_ANY );

        for( int k : { 0, 1, 2, 3, 5 } )
            m_scopeKind->Append( scopeKindLabel( k ) );

        m_scopeKind->SetSelection( 0 );
        grid->Add( m_scopeKind, 1, wxEXPAND );

        grid->Add( new wxStaticText( this, wxID_ANY, wxEmptyString ) );
        m_scopeValue = new wxComboBox( this, wxID_ANY );
        grid->Add( m_scopeValue, 1, wxEXPAND );

        main->Add( grid, 0, wxEXPAND | wxLEFT | wxRIGHT, 10 );

        wxStdDialogButtonSizer* sdb = new wxStdDialogButtonSizer();
        sdb->AddButton( new wxButton( this, wxID_OK, _( "Finish" ) ) );
        sdb->AddButton( new wxButton( this, wxID_CANCEL ) );
        sdb->Realize();
        main->Add( sdb, 0, wxEXPAND | wxALL, 10 );

        SetSizer( main );

        m_typeList->SetSelection( sel );
        updateName();
        updateScope();

        m_typeList->Bind( wxEVT_LISTBOX, [this]( wxCommandEvent& ) { updateName(); } );
        m_typeList->Bind( wxEVT_LISTBOX_DCLICK,
                          [this]( wxCommandEvent& )
                          {
                              m_name->SetFocus();
                          } );
        m_name->Bind( wxEVT_TEXT, [this]( wxCommandEvent& ) { m_nameEdited = true; } );
        m_scopeKind->Bind( wxEVT_CHOICE, [this]( wxCommandEvent& ) { updateScope(); } );

        SetInitialFocus( m_typeList );
        finishDialogSettings();
    }

    bool TransferDataFromWindow() override
    {
        wxString name = m_name->GetValue().Trim().Trim( false );

        if( name.IsEmpty() || containsBadChars( name, wxS( "\"\n" ) ) )
        {
            DisplayError( this, _( "Enter a name for the rule (without double quotes)." ) );
            return false;
        }

        if( m_doc.FindRuleByName( name ) )
        {
            DisplayError( this, wxString::Format( _( "A rule named '%s' already exists." ),
                                                  name ) );
            return false;
        }

        SCOPE s = Scope();

        if( s.kind != SCOPE_KIND::ALL
            && ( s.kind == SCOPE_KIND::CUSTOM ? s.query : ( s.kind == SCOPE_KIND::LAYER ? s.layer
                                                                                         : s.net ) )
                       .IsEmpty() )
        {
            DisplayError( this, _( "Enter what the rule applies to." ) );
            return false;
        }

        return true;
    }

    int Category() const
    {
        int sel = m_typeList->GetSelection();
        return sel >= 0 ? m_cats[sel] : -1;
    }

    wxString Name() const { return m_name->GetValue().Trim().Trim( false ); }

    SCOPE Scope() const
    {
        static const SCOPE_KIND kinds[] = { SCOPE_KIND::ALL, SCOPE_KIND::NET, SCOPE_KIND::NETCLASS,
                                            SCOPE_KIND::LAYER, SCOPE_KIND::CUSTOM };
        SCOPE    s;
        int      k = std::max( 0, m_scopeKind->GetSelection() );
        wxString v = m_scopeValue->GetValue().Trim().Trim( false );

        s.kind = kinds[k];

        if( s.kind == SCOPE_KIND::LAYER )
            s.layer = v;
        else if( s.kind == SCOPE_KIND::CUSTOM )
            s.query = v;
        else if( s.kind != SCOPE_KIND::ALL )
            s.net = v;

        return s;
    }

private:
    void updateName()
    {
        int cat = Category();

        if( cat >= 0 && !m_nameEdited )
        {
            m_name->ChangeValue( m_doc.UniqueName( Categories()[cat].defaultRuleName ) );
            m_nameEdited = false;
        }
    }

    void updateScope()
    {
        int k = m_scopeKind->GetSelection();
        m_scopeValue->Clear();

        const std::vector<wxString>* items = nullptr;

        if( k == 1 )
            items = &m_nets;
        else if( k == 2 )
            items = &m_classes;
        else if( k == 3 )
            items = &m_layers;

        if( items )
        {
            for( const wxString& s : *items )
                m_scopeValue->Append( s );
        }

        m_scopeValue->Enable( k != 0 );
        m_scopeValue->SetValue( wxEmptyString );
    }

    RULES_DOC&            m_doc;
    std::vector<wxString> m_nets;
    std::vector<wxString> m_classes;
    std::vector<wxString> m_layers;
    std::vector<int>      m_cats;
    bool                  m_nameEdited;
    wxListBox*            m_typeList;
    wxTextCtrl*           m_name;
    wxChoice*             m_scopeKind;
    wxComboBox*           m_scopeValue;
};

} // anonymous namespace


// ------------------------------------------------------------------------------------------
// Main dialog
// ------------------------------------------------------------------------------------------

DIALOG_PCB_RULES_EDITOR::DIALOG_PCB_RULES_EDITOR( PCB_EDIT_FRAME* aFrame ) :
        DIALOG_SHIM( aFrame, wxID_ANY, _( "PCB Rules and Constraints Editor" ), wxDefaultPosition,
                     wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxMAXIMIZE_BOX ),
        m_frame( aFrame ),
        m_board( aFrame->GetBoard() ),
        m_units( aFrame->GetUserUnits() ),
        m_dirty( false ),
        m_building( false ),
        m_refreshPending( false ),
        m_splitter( nullptr ),
        m_tree( nullptr ),
        m_page( nullptr ),
        m_pageSizer( nullptr ),
        m_pageKind( PAGE::NONE ),
        m_pageCat( -1 ),
        m_pageUid( 0 ),
        m_nameCtrl( nullptr ),
        m_commentCtrl( nullptr ),
        m_layerChoice( nullptr ),
        m_initialLayerSel( 0 ),
        m_rawCtrl( nullptr ),
        m_grid( nullptr )
{
    SetTitle( wxString::Format( _( "PCB Rules and Constraints Editor [%s]" ),
                                EDA_UNIT_UTILS::GetLabel( m_units ) ) );

    for( PCB_LAYER_ID layer : m_board->GetEnabledLayers().UIOrder() )
    {
        m_layerNames.push_back( LSET::Name( layer ) );
        m_layerLabels.push_back( m_board->GetLayerName( layer ) );
    }

    wxString path = m_frame->GetDesignRulesPath();

    if( !path.IsEmpty() && !m_doc.LoadFile( path ) )
        m_loadError = wxString::Format( _( "Could not read '%s'." ), path );

    wxBoxSizer* main = new wxBoxSizer( wxVERTICAL );

    // One saved size / position / splitter for the dialog, whatever units are in the title
    m_hash_key = "DIALOG_PCB_RULES_EDITOR";

    m_splitter = new wxSplitterWindow( this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                       wxSP_LIVE_UPDATE | wxSP_3DSASH );
    m_splitter->SetMinSize( FromDIP( wxSize( 760, 460 ) ) );
    m_splitter->SetMinimumPaneSize( FromDIP( 160 ) );
    m_splitter->SetSashGravity( 0.0 );     // the tree keeps its width; the page grows

    m_tree = new wxTreeCtrl( m_splitter, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                             wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_SINGLE
                                     | wxTR_FULL_ROW_HIGHLIGHT );

    m_page = new wxScrolledWindow( m_splitter, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                   wxVSCROLL | wxHSCROLL );
    m_page->SetScrollRate( 10, 10 );
    m_pageSizer = new wxBoxSizer( wxVERTICAL );
    m_page->SetSizer( m_pageSizer );

    // The page's controls are rebuilt for each rule; only the splitter and geometry persist
    OptOut( m_page );

    m_splitter->SplitVertically( m_tree, m_page, FromDIP( 300 ) );
    main->Add( m_splitter, 1, wxEXPAND | wxALL, 6 );

    wxBoxSizer* bottom = new wxBoxSizer( wxHORIZONTAL );
    wxButton*   wizardBtn = new wxButton( this, wxID_ANY, _( "Rule Wizard..." ) );
    wxButton*   prioritiesBtn = new wxButton( this, wxID_ANY, _( "Priorities..." ) );
    wxButton*   defaultsBtn = new wxButton( this, wxID_ANY, _( "Create Default Rules" ) );
    bottom->Add( wizardBtn, 0, wxRIGHT, 5 );
    bottom->Add( prioritiesBtn, 0, wxRIGHT, 5 );
    bottom->Add( defaultsBtn, 0, wxRIGHT, 5 );
    bottom->AddStretchSpacer();

    wxStdDialogButtonSizer* sdb = new wxStdDialogButtonSizer();
    sdb->AddButton( new wxButton( this, wxID_OK ) );
    sdb->AddButton( new wxButton( this, wxID_CANCEL ) );
    sdb->AddButton( new wxButton( this, wxID_APPLY ) );
    sdb->Realize();
    bottom->Add( sdb, 0, wxALIGN_CENTER_VERTICAL );

    main->Add( bottom, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6 );
    SetSizer( main );

    wizardBtn->Bind( wxEVT_BUTTON, &DIALOG_PCB_RULES_EDITOR::onRuleWizard, this );
    prioritiesBtn->Bind( wxEVT_BUTTON, &DIALOG_PCB_RULES_EDITOR::onPriorities, this );
    defaultsBtn->Bind( wxEVT_BUTTON, &DIALOG_PCB_RULES_EDITOR::onCreateDefaults, this );
    Bind( wxEVT_BUTTON, &DIALOG_PCB_RULES_EDITOR::onApply, this, wxID_APPLY );

    // Cancel, Esc and the close box ask before throwing away unapplied edits
    Bind( wxEVT_BUTTON,
          [this]( wxCommandEvent& aEvent )
          {
              if( confirmDiscard() )
                  aEvent.Skip();
          },
          wxID_CANCEL );

    m_tree->Bind( wxEVT_TREE_SEL_CHANGING, &DIALOG_PCB_RULES_EDITOR::onTreeSelChanging, this );
    m_tree->Bind( wxEVT_TREE_SEL_CHANGED, &DIALOG_PCB_RULES_EDITOR::onTreeSelChanged, this );
    m_tree->Bind( wxEVT_TREE_ITEM_MENU, &DIALOG_PCB_RULES_EDITOR::onTreeMenu, this );

    rebuildTree( NODE_KEY() );

    SetInitialFocus( m_tree );
    finishDialogSettings();

    // Altium's editor opens large.  Used when there is no saved size; DIALOG_SHIM saves the
    // size, position and splitter (under m_hash_key) when the dialog closes.
    wxSize    preferred = FromDIP( wxSize( 1400, 880 ) );
    int       display = wxDisplay::GetFromWindow( aFrame );
    wxRect    area = wxDisplay( display == wxNOT_FOUND ? 0 : display ).GetClientArea();

    preferred.x = std::max( GetSize().x, std::min( preferred.x, area.width * 9 / 10 ) );
    preferred.y = std::max( GetSize().y, std::min( preferred.y, area.height * 9 / 10 ) );
    m_initialSize = preferred;
}


static bool sameScope( const SCOPE& a, const SCOPE& b );


bool DIALOG_PCB_RULES_EDITOR::hasUnappliedChanges()
{
    return m_dirty || !m_boardEdits.empty() || pageModified();
}


bool DIALOG_PCB_RULES_EDITOR::pageModified()
{
    switch( m_pageKind )
    {
    case PAGE::RULE:
    {
        if( !m_doc.FindRule( m_pageUid ) || !m_nameCtrl )
            return false;

        wxString name = m_nameCtrl->GetValue();
        name.Trim().Trim( false );

        if( name != m_initialName || m_commentCtrl->GetValue() != m_initialComment )
            return true;

        if( !sameScope( readScope( 0 ), m_scope[0].initial ) )
            return true;

        if( Categories()[m_pageCat].binary && !sameScope( readScope( 1 ), m_scope[1].initial ) )
            return true;

        if( m_layerChoice && m_layerChoice->GetSelection() != m_initialLayerSel )
            return true;

        break;
    }

    case PAGE::BOARD:
        break;

    case PAGE::CUSTOM:
        return m_rawCtrl && m_rawCtrl->GetValue() != m_rawInitial;

    default:
        return false;
    }

    // Constraint values (rule and Board Setup default pages)
    for( const FIELD_CTRL& fc : m_fields )
    {
        FIELD_VALUE v;
        wxString    error;

        if( !readField( fc, v, error ) )
            return true;

        if( m_pageKind == PAGE::BOARD && !v.set )
            v.num = 0;

        if( m_pageKind == PAGE::BOARD ? v.num != fc.initial.num
                                      : ( v.set != fc.initial.set
                                          || ( v.set && v.num != fc.initial.num ) ) )
        {
            return true;
        }
    }

    return false;
}


bool DIALOG_PCB_RULES_EDITOR::confirmDiscard()
{
    if( !hasUnappliedChanges() )
        return true;

    KIDIALOG dlg( this, _( "Discard changes?" ), _( "Confirmation" ),
                  wxOK | wxCANCEL | wxICON_WARNING );
    dlg.SetExtendedMessage( _( "The rules have changes that have not been applied." ) );
    dlg.SetOKCancelLabels( _( "Discard Changes" ), _( "Cancel" ) );

    return dlg.ShowModal() == wxID_OK;
}


DIALOG_PCB_RULES_EDITOR::~DIALOG_PCB_RULES_EDITOR()
{
    // Binders reference page controls; drop them first.
    m_fields.clear();
}


// ------------------------------------------------------------------------------------------
// Helpers
// ------------------------------------------------------------------------------------------

std::vector<wxString> DIALOG_PCB_RULES_EDITOR::netNames() const
{
    std::vector<wxString> names;

    for( NETINFO_ITEM* net : m_board->GetNetInfo() )
    {
        if( net && net->GetNetCode() > 0 )
            names.push_back( net->GetNetname() );
    }

    std::sort( names.begin(), names.end(),
               []( const wxString& a, const wxString& b )
               {
                   return a.CmpNoCase( b ) < 0;
               } );

    return names;
}


std::vector<wxString> DIALOG_PCB_RULES_EDITOR::netclassNames() const
{
    std::vector<wxString> names;
    names.push_back( NETCLASS::Default );

    const BOARD_DESIGN_SETTINGS& bds = m_board->GetDesignSettings();

    if( bds.m_NetSettings )
    {
        for( const auto& [name, nc] : bds.m_NetSettings->GetNetclasses() )
        {
            if( name != NETCLASS::Default )
                names.push_back( name );
        }
    }

    return names;
}


int DIALOG_PCB_RULES_EDITOR::boardValue( int BOARD_DESIGN_SETTINGS::*aMember ) const
{
    for( const auto& [member, value] : m_boardEdits )
    {
        if( member == aMember )
            return value;
    }

    return m_board->GetDesignSettings().*aMember;
}


int DIALOG_PCB_RULES_EDITOR::categoryFromKey( const NODE_KEY& aKey ) const
{
    return aKey.cat;
}


// ------------------------------------------------------------------------------------------
// Tree
// ------------------------------------------------------------------------------------------

void DIALOG_PCB_RULES_EDITOR::rebuildTree( const NODE_KEY& aSelect )
{
    m_building = true;
    m_tree->Freeze();
    m_tree->DeleteAllItems();

    NODE_KEY rootKey;
    rootKey.kind = NODE::ROOT;
    wxTreeItemId root = m_tree->AddRoot( _( "Design Rules" ), -1, -1,
                                         new RULES_TREE_DATA( rootKey ) );

    wxColour grey = wxSystemSettings::GetColour( wxSYS_COLOUR_GRAYTEXT );
    wxFont   italic = m_tree->GetFont();
    italic.SetStyle( wxFONTSTYLE_ITALIC );

    const std::vector<CATEGORY>& cats = Categories();

    for( const wxString& group : Groups() )
    {
        NODE_KEY groupKey;
        groupKey.kind = NODE::GROUP;
        groupKey.group = group;
        wxTreeItemId groupItem = m_tree->AppendItem( root, group, -1, -1,
                                                     new RULES_TREE_DATA( groupKey ) );

        for( int i = 0; i < (int) cats.size(); ++i )
        {
            if( cats[i].group != group )
                continue;

            std::vector<RULE*> rules = m_doc.RulesInCategory( i );

            NODE_KEY catKey;
            catKey.kind = NODE::CATEGORY;
            catKey.cat = i;

            wxString label = cats[i].name;

            if( !rules.empty() )
                label << wxString::Format( wxS( " (%zu)" ), rules.size() );

            wxTreeItemId catItem = m_tree->AppendItem( groupItem, label, -1, -1,
                                                       new RULES_TREE_DATA( catKey ) );

            if( !cats[i].supported )
                m_tree->SetItemTextColour( catItem, grey );

            for( RULE* rule : rules )
            {
                NODE_KEY ruleKey;
                ruleKey.kind = NODE::RULE;
                ruleKey.cat = i;
                ruleKey.uid = rule->uid;

                wxTreeItemId item = m_tree->AppendItem( catItem, rule->name, -1, -1,
                                                        new RULES_TREE_DATA( ruleKey ) );

                if( !rule->enabled )
                    m_tree->SetItemTextColour( item, grey );

                if( rule->parseError )
                    m_tree->SetItemTextColour( item, *wxRED );
            }

            if( HasBoardLink( i ) )
            {
                NODE_KEY boardKey;
                boardKey.kind = NODE::BOARD;
                boardKey.cat = i;

                wxTreeItemId item = m_tree->AppendItem( catItem, _( "Board Setup (default)" ),
                                                        -1, -1, new RULES_TREE_DATA( boardKey ) );
                m_tree->SetItemFont( item, italic );
            }

            if( !rules.empty() )
                m_tree->Expand( catItem );
        }

        m_tree->Expand( groupItem );
    }

    m_tree->Expand( root );
    m_tree->Thaw();

    if( !selectKey( aSelect ) )
    {
        // Fall back to the category, then the root
        NODE_KEY fallback;

        if( aSelect.cat >= 0 )
        {
            fallback.kind = NODE::CATEGORY;
            fallback.cat = aSelect.cat;
        }

        if( !selectKey( fallback ) )
            selectKey( NODE_KEY() );
    }

    m_building = false;
    showPage( currentKey() );
}


bool DIALOG_PCB_RULES_EDITOR::selectKey( const NODE_KEY& aKey )
{
    wxTreeItemId item = findItem( m_tree, m_tree->GetRootItem(), aKey );

    if( !item.IsOk() )
        return false;

    bool wasBuilding = m_building;
    m_building = true;
    m_tree->SelectItem( item );
    m_tree->EnsureVisible( item );
    m_building = wasBuilding;
    return true;
}


NODE_KEY DIALOG_PCB_RULES_EDITOR::currentKey() const
{
    wxTreeItemId item = m_tree->GetSelection();

    if( item.IsOk() )
    {
        if( RULES_TREE_DATA* data = dynamic_cast<RULES_TREE_DATA*>( m_tree->GetItemData( item ) ) )
            return data->m_key;
    }

    return NODE_KEY();
}


void DIALOG_PCB_RULES_EDITOR::scheduleRefresh()
{
    if( m_refreshPending )
        return;

    m_refreshPending = true;

    CallAfter(
            [this]()
            {
                m_refreshPending = false;
                rebuildTree( currentKey() );
            } );
}


void DIALOG_PCB_RULES_EDITOR::onTreeSelChanging( wxTreeEvent& aEvent )
{
    if( m_building )
        return;

    if( !commitPage() )
        aEvent.Veto();
}


void DIALOG_PCB_RULES_EDITOR::onTreeSelChanged( wxTreeEvent& aEvent )
{
    if( m_building )
        return;

    showPage( currentKey() );
}


void DIALOG_PCB_RULES_EDITOR::onTreeMenu( wxTreeEvent& aEvent )
{
    wxTreeItemId item = aEvent.GetItem();

    if( !item.IsOk() )
        return;

    if( item != m_tree->GetSelection() )
    {
        m_tree->SelectItem( item );   // commits the current page (may veto)

        if( item != m_tree->GetSelection() )
            return;
    }

    showContextMenu( currentKey() );
}


void DIALOG_PCB_RULES_EDITOR::showContextMenu( const NODE_KEY& aKey )
{
    int  cat = aKey.cat;
    bool supportedCat = cat >= 0 && Categories()[cat].supported;
    bool isRule = aKey.kind == NODE::RULE;

    wxMenu menu;
    menu.Append( ID_NEW_RULE, _( "New Rule..." ) );
    menu.Append( ID_DUPLICATE_RULE, _( "Duplicate Rule" ) );
    menu.Append( ID_DELETE_RULE, _( "Delete Rule" ) );
    menu.AppendSeparator();
    menu.AppendCheckItem( ID_TOGGLE_ENABLED, _( "Enabled" ) );
    menu.AppendSeparator();
    menu.Append( ID_PRIORITIES, _( "Priorities..." ) );

    menu.Enable( ID_NEW_RULE, supportedCat );
    menu.Enable( ID_DUPLICATE_RULE, isRule );
    menu.Enable( ID_DELETE_RULE, isRule );
    menu.Enable( ID_TOGGLE_ENABLED, isRule );

    if( isRule )
    {
        if( RULE* rule = m_doc.FindRule( aKey.uid ) )
            menu.Check( ID_TOGGLE_ENABLED, rule->enabled );
    }

    int id = GetPopupMenuSelectionFromUser( menu );

    if( id == wxID_NONE )
        return;

    // Run the command once the tree/grid event that opened the menu has finished: commands
    // rebuild the tree and the page.
    NODE_KEY target = aKey;
    CallAfter( [this, id, target]() { runMenuCommand( id, target ); } );
}


void DIALOG_PCB_RULES_EDITOR::runMenuCommand( int id, const NODE_KEY& aKey )
{
    int cat = aKey.cat;

    switch( id )
    {
    case ID_NEW_RULE:
        if( commitPage() )
            newRule( cat );

        break;

    case ID_DUPLICATE_RULE:
        if( commitPage() )
            duplicateRule( cat, aKey.uid );

        break;

    case ID_DELETE_RULE:
        deleteRule( cat, aKey.uid );
        break;

    case ID_TOGGLE_ENABLED:
        if( commitPage() )
        {
            if( RULE* rule = m_doc.FindRule( aKey.uid ) )
            {
                rule->enabled = !rule->enabled;
                m_dirty = true;
                rebuildTree( currentKey() );
            }
        }

        break;

    case ID_PRIORITIES:
    {
        wxCommandEvent dummy;
        onPriorities( dummy );
        break;
    }

    default:
        break;
    }
}


// ------------------------------------------------------------------------------------------
// Pages
// ------------------------------------------------------------------------------------------

void DIALOG_PCB_RULES_EDITOR::clearPage()
{
    m_fields.clear();      // UNIT_BINDERs first; they reference controls
    m_page->Freeze();
    m_pageSizer->Clear( true );
    m_page->DestroyChildren();
    m_page->Thaw();

    m_pageKind = PAGE::NONE;
    m_pageCat = -1;
    m_pageUid = 0;
    m_nameCtrl = nullptr;
    m_commentCtrl = nullptr;
    m_layerChoice = nullptr;
    m_layerClauses.clear();
    m_rawCtrl = nullptr;
    m_grid = nullptr;
    m_gridRows.clear();
    m_hiddenTerms.clear();
    m_scope[0] = SCOPE_CTRLS();
    m_scope[1] = SCOPE_CTRLS();
}


void DIALOG_PCB_RULES_EDITOR::addPageTitle( const wxString& aTitle, const wxString& aSubtitle )
{
    wxStaticText* title = new wxStaticText( m_page, wxID_ANY, aTitle );
    wxFont        font = title->GetFont();
    font.SetPointSize( font.GetPointSize() + 3 );
    font.SetWeight( wxFONTWEIGHT_BOLD );
    title->SetFont( font );
    m_pageSizer->Add( title, 0, wxLEFT | wxRIGHT | wxTOP, 10 );

    if( !aSubtitle.IsEmpty() )
    {
        wxStaticText* sub = new wxStaticText( m_page, wxID_ANY, aSubtitle );
        sub->SetForegroundColour( wxSystemSettings::GetColour( wxSYS_COLOUR_GRAYTEXT ) );
        m_pageSizer->Add( sub, 0, wxLEFT | wxRIGHT | wxTOP, 10 );
        sub->Wrap( 700 );
    }
}


void DIALOG_PCB_RULES_EDITOR::showPage( const NODE_KEY& aKey )
{
    m_page->Freeze();
    clearPage();

    switch( aKey.kind )
    {
    case NODE::ROOT:
    case NODE::GROUP:
        showSummary( aKey );
        break;

    case NODE::CATEGORY:
        if( Categories()[aKey.cat].supported )
            showSummary( aKey );
        else
            showUnsupported( aKey.cat );

        break;

    case NODE::RULE:
        if( aKey.cat == CustomCategory() )
            showCustomRule( aKey.uid );
        else
            showRule( aKey.cat, aKey.uid );

        break;

    case NODE::BOARD:
        showBoardDefaults( aKey.cat );
        break;
    }

    m_page->Layout();
    m_page->FitInside();
    m_page->Scroll( 0, 0 );
    m_page->Thaw();
}


void DIALOG_PCB_RULES_EDITOR::showSummary( const NODE_KEY& aKey )
{
    m_pageKind = PAGE::SUMMARY;
    m_pageCat = aKey.cat;

    const std::vector<CATEGORY>& cats = Categories();
    std::vector<int>             catList;

    for( int i = 0; i < (int) cats.size(); ++i )
    {
        if( !cats[i].supported )
            continue;

        if( aKey.kind == NODE::ROOT || ( aKey.kind == NODE::GROUP && cats[i].group == aKey.group )
            || ( aKey.kind == NODE::CATEGORY && i == aKey.cat ) )
        {
            catList.push_back( i );
        }
    }

    wxString title = aKey.kind == NODE::ROOT    ? _( "Design Rules" )
                     : aKey.kind == NODE::GROUP ? aKey.group
                                                : cats[aKey.cat].name;
    wxString subtitle;

    if( aKey.kind == NODE::CATEGORY )
        subtitle = cats[aKey.cat].note;
    else
        subtitle = wxString::Format( _( "Rules file: %s" ), m_frame->GetDesignRulesPath() );

    if( !m_loadError.IsEmpty() )
        subtitle << wxS( "\n" ) << m_loadError;

    if( m_frame->Prj().IsReadOnly() )
        subtitle << wxS( "\n" ) << _( "The project is read-only; changes cannot be saved." );

    addPageTitle( title, subtitle );

    // Gather rows
    struct ROW
    {
        NODE_KEY key;
        wxString name, priority, type, category, scope1, scope2, attributes;
        bool     enabled = true;
        bool     editableEnabled = false;
        bool     grey = false;
    };

    std::vector<ROW> rows;
    std::shared_ptr<NET_SETTINGS> netSettings = m_board->GetDesignSettings().m_NetSettings;

    for( int cat : catList )
    {
        const CATEGORY& c = cats[cat];
        std::vector<RULE*> rules = m_doc.RulesInCategory( cat );

        for( size_t i = 0; i < rules.size(); ++i )
        {
            RULE*                 r = rules[i];
            SCOPE                 s1, s2;
            std::vector<wxString> hidden;
            ParseScopes( r->condition, c, s1, s2, hidden );

            ROW row;
            row.key.kind = NODE::RULE;
            row.key.cat = cat;
            row.key.uid = r->uid;
            row.name = r->name;
            row.priority = wxString::Format( wxS( "%zu" ), i + 1 );
            row.enabled = r->enabled;
            row.editableEnabled = true;
            row.type = c.name;
            row.category = c.group;
            row.scope1 = cat == CustomCategory() ? r->condition : s1.Describe( 'A' );
            row.scope2 = c.binary ? s2.Describe( 'B' ) : wxString();
            row.attributes = m_doc.Attributes( *r, cat, m_units );
            rows.push_back( row );
        }

        if( netSettings
            && ( c.id == wxS( "clearance" ) || c.id == wxS( "width" ) || c.id == wxS( "via_style" )
                 || c.id == wxS( "diff_pairs" ) ) )
        {
            std::vector<std::pair<wxString, std::shared_ptr<NETCLASS>>> classes;

            for( const auto& [name, nc] : netSettings->GetNetclasses() )
            {
                if( name != NETCLASS::Default )
                    classes.emplace_back( name, nc );
            }

            classes.emplace_back( NETCLASS::Default, netSettings->GetDefaultNetclass() );

            for( const auto& [name, nc] : classes )
            {
                if( !nc )
                    continue;

                wxString attrs = netclassRowAttributes( nc.get(), c.id, m_units );

                if( attrs.IsEmpty() )
                    continue;

                ROW row;
                row.key.kind = NODE::CATEGORY;
                row.key.cat = cat;
                row.name = wxString::Format( _( "Net Class '%s'" ), name );
                row.priority = _( "(net class)" );
                row.type = c.name;
                row.category = c.group;
                row.scope1 = wxString::Format( wxS( "A.hasNetclass('%s')" ), name );
                row.scope2 = c.binary ? _( "All" ) : wxString();
                row.attributes = attrs + wxS( "   " ) + _( "(edit in Board Setup > Net Classes)" );
                row.grey = true;
                rows.push_back( row );
            }
        }

        if( HasBoardLink( cat ) )
        {
            ROW row;
            row.key.kind = NODE::BOARD;
            row.key.cat = cat;
            row.name = _( "Board Setup (default)" );
            row.priority = _( "(lowest)" );
            row.type = c.name;
            row.category = c.group;
            row.scope1 = _( "All" );
            row.scope2 = c.binary ? _( "All" ) : wxString();

            wxString attrs;

            for( size_t f = 0; f < c.fields.size(); ++f )
            {
                if( const BOARD_LINK* link = FindBoardLink( cat, (int) f ) )
                {
                    if( !attrs.IsEmpty() )
                        attrs << wxS( "   " );

                    wxString value = c.fields[f].kind == FIELD_KIND::COUNT
                                             ? wxString::Format( wxS( "%d" ),
                                                                 boardValue( link->member ) )
                                             : EDA_UNIT_UTILS::UI::MessageTextFromValue(
                                                       pcbIUScale, m_units,
                                                       boardValue( link->member ) );
                    attrs << c.fields[f].label << wxS( " = " ) << value;
                }
            }

            row.attributes = attrs;
            row.grey = true;
            rows.push_back( row );
        }
    }

    m_grid = new wxGrid( m_page, wxID_ANY );
    m_grid->CreateGrid( (int) rows.size(), COL_COUNT );
    m_grid->SetRowLabelSize( 0 );
    m_grid->EnableDragRowSize( false );
    m_grid->SetSelectionMode( wxGrid::wxGridSelectRows );
    m_grid->SetColLabelValue( COL_NAME, _( "Name" ) );
    m_grid->SetColLabelValue( COL_PRIORITY, _( "Priority" ) );
    m_grid->SetColLabelValue( COL_ENABLED, _( "Enabled" ) );
    m_grid->SetColLabelValue( COL_TYPE, _( "Type" ) );
    m_grid->SetColLabelValue( COL_CATEGORY, _( "Category" ) );
    m_grid->SetColLabelValue( COL_SCOPE1, _( "Scope (First Object)" ) );
    m_grid->SetColLabelValue( COL_SCOPE2, _( "Scope (Second Object)" ) );
    m_grid->SetColLabelValue( COL_ATTRIBUTES, _( "Attributes" ) );
    m_grid->SetColFormatBool( COL_ENABLED );

    wxColour greyText = wxSystemSettings::GetColour( wxSYS_COLOUR_GRAYTEXT );

    for( int r = 0; r < (int) rows.size(); ++r )
    {
        const ROW& row = rows[r];
        m_grid->SetCellValue( r, COL_NAME, row.name );
        m_grid->SetCellValue( r, COL_PRIORITY, row.priority );
        m_grid->SetCellValue( r, COL_ENABLED, row.enabled ? wxString( wxS( "1" ) ) : wxString() );
        m_grid->SetCellValue( r, COL_TYPE, row.type );
        m_grid->SetCellValue( r, COL_CATEGORY, row.category );
        m_grid->SetCellValue( r, COL_SCOPE1, row.scope1 );
        m_grid->SetCellValue( r, COL_SCOPE2, row.scope2 );
        m_grid->SetCellValue( r, COL_ATTRIBUTES, row.attributes );

        for( int col = 0; col < COL_COUNT; ++col )
        {
            m_grid->SetReadOnly( r, col, true );

            if( row.grey || !row.enabled )
                m_grid->SetCellTextColour( r, col, greyText );
        }

        m_gridRows.push_back( row.key );
    }

    m_grid->AutoSizeColumns( false );

    for( int col = 0; col < COL_COUNT; ++col )
        m_grid->SetColSize( col, std::min( m_grid->GetColSize( col ), 420 ) );

    m_grid->Bind( wxEVT_GRID_CELL_LEFT_CLICK, &DIALOG_PCB_RULES_EDITOR::onGridClick, this );
    m_grid->Bind( wxEVT_GRID_CELL_LEFT_DCLICK, &DIALOG_PCB_RULES_EDITOR::onGridDClick, this );
    m_grid->Bind( wxEVT_GRID_CELL_RIGHT_CLICK, &DIALOG_PCB_RULES_EDITOR::onGridMenu, this );

    m_pageSizer->Add( m_grid, 1, wxEXPAND | wxALL, 10 );

    if( rows.empty() )
    {
        wxStaticText* hint = new wxStaticText(
                m_page, wxID_ANY,
                _( "No rules.  Right-click a rule type in the tree and choose New Rule..., or use "
                   "the Rule Wizard." ) );
        m_pageSizer->Add( hint, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10 );
    }
}


void DIALOG_PCB_RULES_EDITOR::showUnsupported( int aCat )
{
    m_pageKind = PAGE::UNSUPPORTED;
    m_pageCat = aCat;

    const CATEGORY& c = Categories()[aCat];
    addPageTitle( c.name, wxEmptyString );

    wxStaticText* text = new wxStaticText( m_page, wxID_ANY,
                                           _( "This rule type is not available in KiCad." )
                                                   + wxS( "\n\n" ) + c.note );
    m_pageSizer->Add( text, 0, wxALL, 10 );
    text->Wrap( 640 );
}


void DIALOG_PCB_RULES_EDITOR::buildScopeBox( wxSizer* aSizer, int aIndex, const wxString& aTitle,
                                             const SCOPE& aScope, bool aEnabled )
{
    SCOPE_CTRLS& sc = m_scope[aIndex];
    sc.initial = aScope;

    wxStaticBoxSizer* box = new wxStaticBoxSizer( wxVERTICAL, m_page, aTitle );
    wxWindow*         parent = box->GetStaticBox();

    wxFlexGridSizer* grid = new wxFlexGridSizer( 2, 4, 8 );
    grid->AddGrowableCol( 1 );

    sc.kind = new wxChoice( parent, wxID_ANY );

    for( int k = 0; k <= 5; ++k )
        sc.kind->Append( scopeKindLabel( k ) );

    grid->Add( new wxStaticText( parent, wxID_ANY, _( "Applies to:" ) ), 0,
               wxALIGN_CENTER_VERTICAL );
    grid->Add( sc.kind, 0 );

    sc.valueLabel = new wxStaticText( parent, wxID_ANY, _( "Net:" ) );
    sc.value = new wxComboBox( parent, wxID_ANY, wxEmptyString, wxDefaultPosition,
                               wxSize( 260, -1 ) );
    grid->Add( sc.valueLabel, 0, wxALIGN_CENTER_VERTICAL );
    grid->Add( sc.value, 1, wxEXPAND );

    sc.layerLabel = new wxStaticText( parent, wxID_ANY, _( "Layer:" ) );
    sc.layer = new wxChoice( parent, wxID_ANY );

    for( const wxString& label : m_layerLabels )
        sc.layer->Append( label );

    grid->Add( sc.layerLabel, 0, wxALIGN_CENTER_VERTICAL );
    grid->Add( sc.layer, 0 );

    box->Add( grid, 0, wxEXPAND | wxALL, 5 );

    sc.query = new wxTextCtrl( parent, wxID_ANY, wxEmptyString, wxDefaultPosition,
                               wxSize( -1, 54 ), wxTE_MULTILINE );
    box->Add( sc.query, 0, wxEXPAND | wxLEFT | wxRIGHT, 5 );

    sc.preview = new wxStaticText( parent, wxID_ANY, wxEmptyString );
    sc.preview->SetForegroundColour( wxSystemSettings::GetColour( wxSYS_COLOUR_GRAYTEXT ) );
    box->Add( sc.preview, 0, wxEXPAND | wxALL, 5 );

    // Initial values
    sc.kind->SetSelection( (int) aScope.kind );
    sc.value->SetValue( aScope.net );
    sc.query->ChangeValue( aScope.kind == SCOPE_KIND::CUSTOM ? aScope.query : wxString() );

    int layerSel = -1;

    for( size_t i = 0; i < m_layerNames.size(); ++i )
    {
        if( m_layerNames[i] == aScope.layer || m_layerLabels[i] == aScope.layer )
            layerSel = (int) i;
    }

    if( layerSel < 0 && !aScope.layer.IsEmpty() )
    {
        // Unknown layer name: keep it selectable as written
        m_layerNames.push_back( aScope.layer );
        m_layerLabels.push_back( aScope.layer );
        sc.layer->Append( aScope.layer );
        layerSel = (int) m_layerNames.size() - 1;
    }

    sc.layer->SetSelection( layerSel );

    sc.kind->Enable( aEnabled );
    sc.value->Enable( aEnabled );
    sc.layer->Enable( aEnabled );
    sc.query->Enable( aEnabled );

    aSizer->Add( box, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10 );

    updateScopeCtrls( aIndex );

    sc.kind->Bind( wxEVT_CHOICE,
                   [this, aIndex]( wxCommandEvent& )
                   {
                       SCOPE_CTRLS& s = m_scope[aIndex];

                       // Switching to Custom Query starts from the current query text
                       if( s.kind->GetSelection() == (int) SCOPE_KIND::CUSTOM
                           && s.query->GetValue().IsEmpty() )
                       {
                           SCOPE before;
                           before.kind = (SCOPE_KIND) std::max( 0, s.shownKind );
                           before.net = s.value->GetValue();
                           int layerSel = s.layer->GetSelection();

                           if( layerSel >= 0 && layerSel < (int) m_layerNames.size() )
                               before.layer = m_layerNames[layerSel];

                           if( before.kind != SCOPE_KIND::CUSTOM )
                               s.query->ChangeValue( before.ToExpr( aIndex == 0 ? 'A' : 'B' ) );
                       }

                       updateScopeCtrls( aIndex );
                       m_page->Layout();
                       m_page->FitInside();
                   } );

    auto refreshPreview = [this, aIndex]( wxCommandEvent& aEvent )
    {
        SCOPE s = readScope( aIndex );
        m_scope[aIndex].preview->SetLabel( _( "Query: " )
                                           + ( s.kind == SCOPE_KIND::ALL
                                                       ? _( "All" )
                                                       : s.ToExpr( aIndex == 0 ? 'A' : 'B' ) ) );
        aEvent.Skip();
    };

    sc.value->Bind( wxEVT_TEXT, refreshPreview );
    sc.value->Bind( wxEVT_COMBOBOX, refreshPreview );
    sc.layer->Bind( wxEVT_CHOICE, refreshPreview );
    sc.query->Bind( wxEVT_TEXT, refreshPreview );
}


void DIALOG_PCB_RULES_EDITOR::updateScopeCtrls( int aIndex )
{
    SCOPE_CTRLS& sc = m_scope[aIndex];
    int          kind = std::max( 0, sc.kind->GetSelection() );

    bool showValue = kind == (int) SCOPE_KIND::NET || kind == (int) SCOPE_KIND::NETCLASS
                     || kind == (int) SCOPE_KIND::NET_AND_LAYER;
    bool showLayer = kind == (int) SCOPE_KIND::LAYER || kind == (int) SCOPE_KIND::NET_AND_LAYER;
    bool showQuery = kind == (int) SCOPE_KIND::CUSTOM;

    if( kind != sc.shownKind )
    {
        bool wasClass = sc.shownKind == (int) SCOPE_KIND::NETCLASS;
        bool isClass = kind == (int) SCOPE_KIND::NETCLASS;

        if( sc.shownKind < 0 || wasClass != isClass )
        {
            wxString value = sc.value->GetValue();
            sc.value->Clear();

            for( const wxString& name : isClass ? netclassNames() : netNames() )
                sc.value->Append( name );

            sc.value->SetValue( sc.shownKind < 0 ? value : wxString() );
        }

        sc.valueLabel->SetLabel( isClass ? _( "Net class:" ) : _( "Net:" ) );
        sc.shownKind = kind;
    }

    sc.valueLabel->Show( showValue );
    sc.value->Show( showValue );
    sc.layerLabel->Show( showLayer );
    sc.layer->Show( showLayer );
    sc.query->Show( showQuery );

    SCOPE s = readScope( aIndex );
    sc.preview->SetLabel( _( "Query: " )
                          + ( s.kind == SCOPE_KIND::ALL ? _( "All" )
                                                        : s.ToExpr( aIndex == 0 ? 'A' : 'B' ) ) );
}


SCOPE DIALOG_PCB_RULES_EDITOR::readScope( int aIndex ) const
{
    const SCOPE_CTRLS& sc = m_scope[aIndex];
    SCOPE              s;

    if( !sc.kind )
        return s;

    s.kind = (SCOPE_KIND) std::max( 0, sc.kind->GetSelection() );

    switch( s.kind )
    {
    case SCOPE_KIND::ALL:
        break;

    case SCOPE_KIND::NET:
    case SCOPE_KIND::NETCLASS:
        s.net = sc.value->GetValue();
        s.net.Trim().Trim( false );
        break;

    case SCOPE_KIND::LAYER:
    case SCOPE_KIND::NET_AND_LAYER:
    {
        int sel = sc.layer->GetSelection();

        if( sel >= 0 && sel < (int) m_layerNames.size() )
            s.layer = m_layerNames[sel];

        if( s.kind == SCOPE_KIND::NET_AND_LAYER )
        {
            s.net = sc.value->GetValue();
            s.net.Trim().Trim( false );
        }

        break;
    }

    case SCOPE_KIND::CUSTOM:
        s.query = sc.query->GetValue();
        s.query.Trim().Trim( false );
        break;
    }

    return s;
}


static bool sameScope( const SCOPE& a, const SCOPE& b )
{
    return a.kind == b.kind && a.net == b.net && a.layer == b.layer && a.query == b.query;
}


void DIALOG_PCB_RULES_EDITOR::buildFieldCtrls( wxWindow* aParent, wxSizer* aSizer, int aCat,
                                               const std::vector<int>& aFields,
                                               const std::vector<FIELD_VALUE>& aValues )
{
    const CATEGORY&  c = Categories()[aCat];
    wxFlexGridSizer* grid = new wxFlexGridSizer( 3, 6, 8 );

    for( size_t i = 0; i < aFields.size(); ++i )
    {
        int          fieldIdx = aFields[i];
        const FIELD& f = c.fields[fieldIdx];
        FIELD_CTRL   fc;
        fc.field = fieldIdx;
        fc.initial = aValues[i];

        switch( f.kind )
        {
        case FIELD_KIND::LENGTH:
        {
            wxStaticText* label = new wxStaticText( aParent, wxID_ANY, f.label + wxS( ":" ) );
            fc.text = new wxTextCtrl( aParent, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                      wxSize( 120, -1 ) );
            wxStaticText* units = new wxStaticText( aParent, wxID_ANY, wxEmptyString );
            fc.binder = std::make_unique<UNIT_BINDER>( m_frame, label, fc.text, units );

            if( fc.initial.set )
                fc.binder->SetValue( fc.initial.num );
            else
                fc.binder->SetNull();

            grid->Add( label, 0, wxALIGN_CENTER_VERTICAL );
            grid->Add( fc.text, 0, wxEXPAND );
            grid->Add( units, 0, wxALIGN_CENTER_VERTICAL );
            break;
        }

        case FIELD_KIND::COUNT:
        {
            grid->Add( new wxStaticText( aParent, wxID_ANY, f.label + wxS( ":" ) ), 0,
                       wxALIGN_CENTER_VERTICAL );
            fc.text = new wxTextCtrl( aParent, wxID_ANY,
                                      fc.initial.set ? wxString::Format( wxS( "%lld" ),
                                                                         fc.initial.num )
                                                     : wxString(),
                                      wxDefaultPosition, wxSize( 120, -1 ) );
            grid->Add( fc.text, 0, wxEXPAND );
            grid->AddSpacer( 0 );
            break;
        }

        case FIELD_KIND::CONNECT_STYLE:
        {
            grid->Add( new wxStaticText( aParent, wxID_ANY, f.label + wxS( ":" ) ), 0,
                       wxALIGN_CENTER_VERTICAL );
            fc.choice = new wxChoice( aParent, wxID_ANY );
            fc.choice->Append( _( "(not set)" ) );
            fc.choice->Append( _( "Relief Connect" ) );
            fc.choice->Append( _( "Direct Connect" ) );
            fc.choice->Append( _( "No Connect" ) );

            int sel = 0;

            if( fc.initial.set )
                sel = fc.initial.num == 1 ? 1 : fc.initial.num == 0 ? 2 : 3;

            fc.choice->SetSelection( sel );
            grid->Add( fc.choice, 0, wxEXPAND );
            grid->AddSpacer( 0 );
            break;
        }

        case FIELD_KIND::FLAG:
        {
            grid->AddSpacer( 0 );
            fc.check = new wxCheckBox( aParent, wxID_ANY, f.label );
            fc.check->SetValue( fc.initial.set && fc.initial.num );
            grid->Add( fc.check, 0 );
            grid->AddSpacer( 0 );
            break;
        }

        case FIELD_KIND::ORIENTATION:
        {
            grid->Add( new wxStaticText( aParent, wxID_ANY, f.label + wxS( ":" ) ), 0,
                       wxALIGN_TOP );
            wxBoxSizer* col = new wxBoxSizer( wxVERTICAL );
            static const wxChar* labels[] = { wxS( "0 Degrees" ), wxS( "90 Degrees" ),
                                              wxS( "180 Degrees" ), wxS( "270 Degrees" ) };

            for( int a = 0; a < 4; ++a )
            {
                wxCheckBox* cb = new wxCheckBox( aParent, wxID_ANY, wxGetTranslation( labels[a] ) );
                cb->SetValue( fc.initial.num & ( 1LL << a ) );
                col->Add( cb, 0, wxBOTTOM, 3 );
                fc.checks.push_back( cb );
            }

            wxCheckBox* all = new wxCheckBox( aParent, wxID_ANY, _( "All Orientations" ) );
            all->SetValue( ( fc.initial.num & 15 ) == 15 );
            col->Add( all, 0, wxTOP, 4 );

            std::vector<wxCheckBox*> checks = fc.checks;

            all->Bind( wxEVT_CHECKBOX,
                       [checks]( wxCommandEvent& aEvent )
                       {
                           for( wxCheckBox* cb : checks )
                               cb->SetValue( aEvent.IsChecked() );
                       } );

            for( wxCheckBox* cb : checks )
            {
                cb->Bind( wxEVT_CHECKBOX,
                          [checks, all]( wxCommandEvent& )
                          {
                              bool every = true;

                              for( wxCheckBox* c2 : checks )
                                  every &= c2->GetValue();

                              all->SetValue( every );
                          } );
            }

            grid->Add( col, 0 );
            grid->AddSpacer( 0 );
            break;
        }

        case FIELD_KIND::SIDE_LAYERS:
        {
            grid->Add( new wxStaticText( aParent, wxID_ANY, f.label + wxS( ":" ) ), 0,
                       wxALIGN_TOP );
            wxBoxSizer* col = new wxBoxSizer( wxVERTICAL );
            wxCheckBox* top = new wxCheckBox( aParent, wxID_ANY, _( "Top Layer" ) );
            wxCheckBox* bot = new wxCheckBox( aParent, wxID_ANY, _( "Bottom Layer" ) );
            top->SetValue( fc.initial.num & 1 );
            bot->SetValue( fc.initial.num & 2 );
            col->Add( top, 0, wxBOTTOM, 3 );
            col->Add( bot, 0 );
            fc.checks = { top, bot };
            grid->Add( col, 0 );
            grid->AddSpacer( 0 );
            break;
        }
        }

        m_fields.push_back( std::move( fc ) );
    }

    aSizer->Add( grid, 0, wxALL, 8 );
}


bool DIALOG_PCB_RULES_EDITOR::readField( const FIELD_CTRL& aCtrl, FIELD_VALUE& aValue,
                                         wxString& aError )
{
    const FIELD& f = Categories()[m_pageCat].fields[aCtrl.field];
    aValue = FIELD_VALUE();

    switch( f.kind )
    {
    case FIELD_KIND::LENGTH:
        if( !aCtrl.binder->IsNull() )
        {
            aValue.set = true;
            aValue.num = aCtrl.binder->GetValue();
        }

        break;

    case FIELD_KIND::COUNT:
    {
        wxString text = aCtrl.text->GetValue();
        text.Trim().Trim( false );

        if( !text.IsEmpty() )
        {
            long v = 0;

            if( !text.ToLong( &v ) || v < 0 )
            {
                aError = wxString::Format( _( "'%s' must be a whole number." ), f.label );
                return false;
            }

            aValue.set = true;
            aValue.num = v;
        }

        break;
    }

    case FIELD_KIND::CONNECT_STYLE:
    {
        int sel = aCtrl.choice->GetSelection();

        if( sel > 0 )
        {
            aValue.set = true;
            aValue.num = sel == 1 ? 1 : sel == 2 ? 0 : 2;
        }

        break;
    }

    case FIELD_KIND::FLAG:
        aValue.set = true;
        aValue.num = aCtrl.check->GetValue() ? 1 : 0;
        break;

    case FIELD_KIND::ORIENTATION:
    case FIELD_KIND::SIDE_LAYERS:
        aValue.set = true;

        for( size_t i = 0; i < aCtrl.checks.size(); ++i )
        {
            if( aCtrl.checks[i]->GetValue() )
                aValue.num |= ( 1LL << i );
        }

        if( !aValue.num )
        {
            aError = wxString::Format( _( "Select at least one entry under '%s'." ), f.label );
            return false;
        }

        break;
    }

    return true;
}


void DIALOG_PCB_RULES_EDITOR::showRule( int aCat, int aUid )
{
    RULE* rule = m_doc.FindRule( aUid );

    if( !rule )
        return;

    m_pageKind = PAGE::RULE;
    m_pageCat = aCat;
    m_pageUid = aUid;

    const CATEGORY& c = Categories()[aCat];

    addPageTitle( rule->name, c.group + wxS( "  ›  " ) + c.name );

    // Name / Comment
    wxFlexGridSizer* header = new wxFlexGridSizer( 4, 4, 8 );
    header->AddGrowableCol( 3 );
    header->Add( new wxStaticText( m_page, wxID_ANY, _( "Name" ) ), 0, wxALIGN_CENTER_VERTICAL );
    m_nameCtrl = new wxTextCtrl( m_page, wxID_ANY, rule->name, wxDefaultPosition,
                                 wxSize( 200, -1 ) );
    header->Add( m_nameCtrl, 0 );
    header->Add( new wxStaticText( m_page, wxID_ANY, _( "Comment" ) ), 0,
                 wxALIGN_CENTER_VERTICAL | wxLEFT, 10 );
    m_commentCtrl = new wxTextCtrl( m_page, wxID_ANY, rule->comment );
    header->Add( m_commentCtrl, 1, wxEXPAND );
    m_pageSizer->Add( header, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10 );

    m_initialName = rule->name;
    m_initialComment = rule->comment;

    // Scopes
    SCOPE s1, s2;
    ParseScopes( rule->condition, c, s1, s2, m_hiddenTerms );

    buildScopeBox( m_pageSizer, 0, _( "Where The First Object Matches" ), s1, true );

    if( c.binary )
        buildScopeBox( m_pageSizer, 1, _( "Where The Second Object Matches" ), s2, true );

    // Constraints
    wxStaticBoxSizer* box = new wxStaticBoxSizer( wxVERTICAL, m_page, _( "Constraints" ) );
    wxWindow*         boxParent = box->GetStaticBox();

    std::vector<int>         fields;
    std::vector<FIELD_VALUE> values;

    for( int f = 0; f < (int) c.fields.size(); ++f )
    {
        fields.push_back( f );
        values.push_back( m_doc.GetFieldValue( *rule, aCat, f ) );
    }

    buildFieldCtrls( boxParent, box, aCat, fields, values );

    // Layer clause
    wxBoxSizer* layerRow = new wxBoxSizer( wxHORIZONTAL );
    layerRow->Add( new wxStaticText( boxParent, wxID_ANY, _( "Only on layer:" ) ), 0,
                   wxALIGN_CENTER_VERTICAL | wxRIGHT, 6 );
    m_layerChoice = new wxChoice( boxParent, wxID_ANY );
    m_layerChoice->Append( _( "(All layers)" ) );
    m_layerClauses.push_back( wxEmptyString );
    m_layerChoice->Append( _( "Outer copper layers" ) );
    m_layerClauses.push_back( wxS( "(layer outer)" ) );
    m_layerChoice->Append( _( "Inner copper layers" ) );
    m_layerClauses.push_back( wxS( "(layer inner)" ) );

    for( size_t i = 0; i < m_layerNames.size(); ++i )
    {
        m_layerChoice->Append( m_layerLabels[i] );
        m_layerClauses.push_back( wxString::Format( wxS( "(layer \"%s\")" ), m_layerNames[i] ) );
    }

    int sel = -1;

    auto normalizeClause = []( const wxString& aClause )
    {
        wxString s = aClause;
        s.Replace( wxS( " " ), wxEmptyString );
        s.Replace( wxS( "\"" ), wxEmptyString );
        s.Replace( wxS( "'" ), wxEmptyString );
        return s;
    };

    for( size_t i = 0; i < m_layerClauses.size(); ++i )
    {
        if( normalizeClause( m_layerClauses[i] ) == normalizeClause( rule->layerClause ) )
            sel = (int) i;
    }

    if( sel < 0 )
    {
        // A user layer name, or anything else: keep it as written
        for( size_t i = 0; i < m_layerLabels.size(); ++i )
        {
            if( normalizeClause( wxString::Format( wxS( "(layer %s)" ), m_layerLabels[i] ) )
                == normalizeClause( rule->layerClause ) )
            {
                sel = (int) i + 3;
            }
        }
    }

    if( sel < 0 )
    {
        m_layerChoice->Append( rule->layerClause );
        m_layerClauses.push_back( rule->layerClause );
        sel = (int) m_layerClauses.size() - 1;
    }

    m_layerChoice->SetSelection( sel );
    m_initialLayerSel = sel;
    layerRow->Add( m_layerChoice, 0 );
    box->Add( layerRow, 0, wxALL, 8 );

    m_pageSizer->Add( box, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10 );

    // Notes
    wxString notes = c.note;
    std::vector<int> others = m_doc.CategoriesOf( *rule );
    wxString         alsoIn;

    for( int other : others )
    {
        if( other == aCat )
            continue;

        if( !alsoIn.IsEmpty() )
            alsoIn << wxS( ", " );

        alsoIn << Categories()[other].name;
    }

    if( !alsoIn.IsEmpty() )
    {
        if( !notes.IsEmpty() )
            notes << wxS( "\n" );

        notes << wxString::Format( _( "This rule also holds constraints listed under: %s." ),
                                   alsoIn );
    }

    if( !rule->severityClause.IsEmpty() )
    {
        if( !notes.IsEmpty() )
            notes << wxS( "\n" );

        notes << wxString::Format( _( "Severity: %s" ), rule->severityClause );
    }

    if( !notes.IsEmpty() )
    {
        wxStaticText* note = new wxStaticText( m_page, wxID_ANY, notes );
        note->SetForegroundColour( wxSystemSettings::GetColour( wxSYS_COLOUR_GRAYTEXT ) );
        m_pageSizer->Add( note, 0, wxALL, 10 );
        note->Wrap( 700 );
    }
}


void DIALOG_PCB_RULES_EDITOR::showBoardDefaults( int aCat )
{
    m_pageKind = PAGE::BOARD;
    m_pageCat = aCat;

    const CATEGORY& c = Categories()[aCat];

    addPageTitle( _( "Board Setup (default)" ), c.group + wxS( "  ›  " ) + c.name );

    wxStaticText* info = new wxStaticText(
            m_page, wxID_ANY,
            _( "Board-wide minimum from Board Setup > Design Rules > Constraints.  It applies to "
               "all objects with the lowest priority; any matching rule above overrides it." ) );
    m_pageSizer->Add( info, 0, wxALL, 10 );
    info->Wrap( 700 );

    wxStaticBoxSizer* box = new wxStaticBoxSizer( wxVERTICAL, m_page, _( "Constraints" ) );

    std::vector<int>         fields;
    std::vector<FIELD_VALUE> values;

    for( int f = 0; f < (int) c.fields.size(); ++f )
    {
        if( const BOARD_LINK* link = FindBoardLink( aCat, f ) )
        {
            FIELD_VALUE v;
            v.set = true;
            v.num = boardValue( link->member );
            fields.push_back( f );
            values.push_back( v );
        }
    }

    buildFieldCtrls( box->GetStaticBox(), box, aCat, fields, values );
    m_pageSizer->Add( box, 0, wxEXPAND | wxLEFT | wxRIGHT, 10 );
}


void DIALOG_PCB_RULES_EDITOR::showCustomRule( int aUid )
{
    RULE* rule = m_doc.FindRule( aUid );

    if( !rule )
        return;

    m_pageKind = PAGE::CUSTOM;
    m_pageCat = CustomCategory();
    m_pageUid = aUid;

    addPageTitle( rule->name, _( "Custom  ›  Custom Rules" ) );

    wxString info = _( "This rule uses KiCad constraints that have no rule type of their own.  "
                       "Edit it in KiCad's rule syntax (see Board Setup > Custom Rules for "
                       "help)." );

    if( rule->parseError )
        info << wxS( "\n\n" ) << _( "Syntax error: " ) << rule->errorMsg;

    wxStaticText* text = new wxStaticText( m_page, wxID_ANY, info );

    if( rule->parseError )
        text->SetForegroundColour( *wxRED );

    m_pageSizer->Add( text, 0, wxALL, 10 );
    text->Wrap( 700 );

    m_rawInitial = rule->ToText();
    m_rawCtrl = new wxTextCtrl( m_page, wxID_ANY, m_rawInitial, wxDefaultPosition,
                                wxSize( -1, 320 ), wxTE_MULTILINE | wxTE_DONTWRAP );
    m_rawCtrl->SetFont( wxFont( wxFontInfo( m_rawCtrl->GetFont().GetPointSize() )
                                        .Family( wxFONTFAMILY_TELETYPE ) ) );
    m_pageSizer->Add( m_rawCtrl, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10 );
}


// ------------------------------------------------------------------------------------------
// Commit
// ------------------------------------------------------------------------------------------

bool DIALOG_PCB_RULES_EDITOR::commitPage()
{
    switch( m_pageKind )
    {
    case PAGE::RULE:   return commitRule();
    case PAGE::BOARD:  return commitBoardDefaults();
    case PAGE::CUSTOM: return commitCustomRule();
    default:           return true;
    }
}


bool DIALOG_PCB_RULES_EDITOR::commitRule()
{
    RULE* rule = m_doc.FindRule( m_pageUid );

    if( !rule )
        return true;

    const CATEGORY& c = Categories()[m_pageCat];
    RULE            work = *rule;
    bool            changed = false;
    bool            structural = false;

    auto fail = [&]( const wxString& aMsg )
    {
        DisplayErrorMessage( this, wxString::Format( _( "Rule '%s' cannot be saved." ),
                                                     m_initialName ),
                             aMsg );
        return false;
    };

    // Name and comment
    wxString name = m_nameCtrl->GetValue();
    name.Trim().Trim( false );

    if( name != m_initialName )
    {
        if( name.IsEmpty() || containsBadChars( name, wxS( "\"\n\r\\" ) ) )
            return fail( _( "The rule name must not be empty or contain quotes." ) );

        if( RULE* other = m_doc.FindRuleByName( name ) )
        {
            if( other->uid != rule->uid )
                return fail( wxString::Format( _( "A rule named '%s' already exists." ), name ) );
        }

        work.name = name;
        changed = true;
        structural = true;
    }

    wxString comment = m_commentCtrl->GetValue();

    if( comment != m_initialComment )
    {
        work.comment = comment;
        changed = true;
    }

    // Scopes
    SCOPE s1 = readScope( 0 );
    SCOPE s2 = c.binary ? readScope( 1 ) : SCOPE();

    if( !sameScope( s1, m_scope[0].initial ) || ( c.binary && !sameScope( s2, m_scope[1].initial ) ) )
    {
        for( const SCOPE* s : { &s1, &s2 } )
        {
            if( ( s->kind == SCOPE_KIND::NET || s->kind == SCOPE_KIND::NETCLASS
                  || s->kind == SCOPE_KIND::NET_AND_LAYER )
                && ( s->net.IsEmpty() || containsBadChars( s->net, wxS( "'\"" ) ) ) )
            {
                return fail( _( "Choose a net or net class name (names with quotes cannot be "
                                "used)." ) );
            }

            if( ( s->kind == SCOPE_KIND::LAYER || s->kind == SCOPE_KIND::NET_AND_LAYER )
                && s->layer.IsEmpty() )
            {
                return fail( _( "Choose a layer." ) );
            }

            if( s->kind == SCOPE_KIND::CUSTOM
                && ( s->query.IsEmpty() || containsBadChars( s->query, wxS( "\"" ) ) ) )
            {
                return fail( _( "Enter a custom query (use single quotes for strings)." ) );
            }
        }

        work.condition = BuildCondition( m_hiddenTerms, s1, s2, c.binary );
        work.modified = true;
        changed = true;
        structural = true;
    }

    // Layer clause
    int layerSel = m_layerChoice ? m_layerChoice->GetSelection() : m_initialLayerSel;

    if( layerSel != m_initialLayerSel && layerSel >= 0 && layerSel < (int) m_layerClauses.size() )
    {
        work.layerClause = m_layerClauses[layerSel];
        changed = true;
    }

    // Constraint values
    for( const FIELD_CTRL& fc : m_fields )
    {
        FIELD_VALUE v;
        wxString    error;

        if( !readField( fc, v, error ) )
            return fail( error );

        if( v.set != fc.initial.set || ( v.set && v.num != fc.initial.num ) )
        {
            m_doc.SetFieldValue( work, m_pageCat, fc.field, v );
            changed = true;
            structural = true;
        }
    }

    if( !changed )
        return true;

    work.modified = true;

    // Value sanity: min <= opt <= max per constraint
    for( const wxString& clause : work.constraints )
    {
        CLAUSE_VALUES cv;

        if( !ParseConstraintClause( clause, cv ) )
            continue;

        if( ( cv.min && cv.opt && *cv.min > *cv.opt ) || ( cv.opt && cv.max && *cv.opt > *cv.max )
            || ( cv.min && cv.max && *cv.min > *cv.max ) )
        {
            return fail( wxString::Format( _( "Minimum, preferred and maximum values of '%s' are "
                                              "out of order." ),
                                           cv.keyword ) );
        }
    }

    if( work.constraints.empty() )
        return fail( _( "A rule needs at least one constraint value.  To remove the rule, use "
                        "Delete Rule." ) );

    wxString syntax = CheckRuleSyntax( work.Generate() );

    if( !syntax.IsEmpty() )
        return fail( syntax );

    *rule = work;
    m_dirty = true;

    // Pick up the new values as the page's baseline
    m_initialName = work.name;
    m_initialComment = work.comment;
    m_scope[0].initial = s1;
    m_scope[1].initial = s2;
    m_initialLayerSel = layerSel;

    for( FIELD_CTRL& fc : m_fields )
    {
        wxString error;
        readField( fc, fc.initial, error );
    }

    if( structural )
        scheduleRefresh();

    return true;
}


bool DIALOG_PCB_RULES_EDITOR::commitBoardDefaults()
{
    for( FIELD_CTRL& fc : m_fields )
    {
        FIELD_VALUE v;
        wxString    error;

        if( !readField( fc, v, error ) )
        {
            DisplayError( this, error );
            return false;
        }

        if( !v.set )
            v.num = 0;

        if( v.num == fc.initial.num )
            continue;

        const BOARD_LINK* link = FindBoardLink( m_pageCat, fc.field );

        if( !link )
            continue;

        bool found = false;

        for( auto& [member, value] : m_boardEdits )
        {
            if( member == link->member )
            {
                value = (int) v.num;
                found = true;
            }
        }

        if( !found )
            m_boardEdits.emplace_back( link->member, (int) v.num );

        fc.initial = v;
    }

    return true;
}


bool DIALOG_PCB_RULES_EDITOR::commitCustomRule()
{
    RULE* rule = m_doc.FindRule( m_pageUid );

    if( !rule || !m_rawCtrl )
        return true;

    wxString text = m_rawCtrl->GetValue();

    if( text == m_rawInitial )
        return true;

    text.Trim().Trim( false );

    RULE work;
    work.uid = rule->uid;
    work.enabled = rule->enabled;
    ParseRule( text, work );

    if( work.parseError || !text.StartsWith( wxS( "(rule" ) ) )
    {
        DisplayErrorMessage( this, _( "The rule text is not valid." ),
                             work.errorMsg.IsEmpty() ? wxString( _( "Expected (rule ...)." ) )
                                                     : work.errorMsg );
        return false;
    }

    if( RULE* other = m_doc.FindRuleByName( work.name ) )
    {
        if( other->uid != rule->uid )
        {
            DisplayError( this, wxString::Format( _( "A rule named '%s' already exists." ),
                                                  work.name ) );
            return false;
        }
    }

    *rule = work;          // written back exactly as typed
    m_rawInitial = m_rawCtrl->GetValue();
    m_dirty = true;
    scheduleRefresh();
    return true;
}


// ------------------------------------------------------------------------------------------
// Grid
// ------------------------------------------------------------------------------------------

void DIALOG_PCB_RULES_EDITOR::onGridClick( wxGridEvent& aEvent )
{
    int row = aEvent.GetRow();

    if( aEvent.GetCol() == COL_ENABLED && row >= 0 && row < (int) m_gridRows.size()
        && m_gridRows[row].kind == NODE::RULE )
    {
        if( RULE* rule = m_doc.FindRule( m_gridRows[row].uid ) )
        {
            rule->enabled = !rule->enabled;
            m_dirty = true;
            scheduleRefresh();
        }

        return;
    }

    aEvent.Skip();
}


void DIALOG_PCB_RULES_EDITOR::onGridDClick( wxGridEvent& aEvent )
{
    int row = aEvent.GetRow();

    if( row < 0 || row >= (int) m_gridRows.size() )
        return;

    NODE_KEY key = m_gridRows[row];

    CallAfter(
            [this, key]()
            {
                wxTreeItemId item = findItem( m_tree, m_tree->GetRootItem(), key );

                if( item.IsOk() )
                {
                    m_tree->SelectItem( item );
                    m_tree->EnsureVisible( item );
                }
            } );
}


void DIALOG_PCB_RULES_EDITOR::onGridMenu( wxGridEvent& aEvent )
{
    int row = aEvent.GetRow();

    if( row < 0 || row >= (int) m_gridRows.size() )
        return;

    m_grid->SelectRow( row );
    showContextMenu( m_gridRows[row] );
}


// ------------------------------------------------------------------------------------------
// Commands
// ------------------------------------------------------------------------------------------

void DIALOG_PCB_RULES_EDITOR::newRule( int aCat, const SCOPE& aScope, const wxString& aName )
{
    if( aCat < 0 || !Categories()[aCat].supported )
        return;

    const CATEGORY& c = Categories()[aCat];
    RULE            rule;
    rule.name = aName.IsEmpty() ? m_doc.UniqueName( c.defaultRuleName ) : aName;
    m_doc.SeedRule( rule, aCat, m_board );

    if( aScope.kind != SCOPE_KIND::ALL )
    {
        SCOPE                 s1, s2;
        std::vector<wxString> hidden;
        ParseScopes( rule.condition, c, s1, s2, hidden );
        rule.condition = BuildCondition( hidden, aScope, SCOPE(), c.binary );
    }

    NODE_KEY key;
    key.kind = NODE::RULE;
    key.cat = aCat;
    key.uid = m_doc.AddRule( rule );
    m_dirty = true;

    m_pageKind = PAGE::NONE;    // nothing left to commit
    rebuildTree( key );
}


void DIALOG_PCB_RULES_EDITOR::duplicateRule( int aCat, int aUid )
{
    RULE* src = m_doc.FindRule( aUid );

    if( !src )
        return;

    RULE copy = *src;
    copy.name = m_doc.UniqueName( src->name );
    copy.modified = true;

    NODE_KEY key;
    key.kind = NODE::RULE;
    key.cat = aCat;
    key.uid = m_doc.AddRule( copy );
    m_dirty = true;

    m_pageKind = PAGE::NONE;
    rebuildTree( key );
}


void DIALOG_PCB_RULES_EDITOR::deleteRule( int aCat, int aUid )
{
    RULE* rule = m_doc.FindRule( aUid );

    if( !rule )
        return;

    if( !IsOK( this, wxString::Format( _( "Delete rule '%s'?" ), rule->name ) ) )
        return;

    m_doc.RemoveFromCategory( aUid, aCat );
    m_dirty = true;

    NODE_KEY key;
    key.kind = NODE::CATEGORY;
    key.cat = aCat;

    m_pageKind = PAGE::NONE;    // discard the deleted rule's page
    rebuildTree( key );
}


void DIALOG_PCB_RULES_EDITOR::onRuleWizard( wxCommandEvent& aEvent )
{
    if( !commitPage() )
        return;

    std::vector<wxString> layers = m_layerNames;
    DIALOG_RULE_WIZARD    dlg( this, m_doc, std::max( 0, currentKey().cat ), netNames(),
                               netclassNames(), layers );

    if( dlg.ShowModal() != wxID_OK )
        return;

    newRule( dlg.Category(), dlg.Scope(), dlg.Name() );
}


void DIALOG_PCB_RULES_EDITOR::onPriorities( wxCommandEvent& aEvent )
{
    if( !commitPage() )
        return;

    DIALOG_RULE_PRIORITIES dlg( this, m_doc, currentKey().cat, m_units );
    dlg.ShowModal();

    if( dlg.Changed() )
    {
        m_dirty = true;
        m_pageKind = PAGE::NONE;
        rebuildTree( currentKey() );
    }
}


void DIALOG_PCB_RULES_EDITOR::onCreateDefaults( wxCommandEvent& aEvent )
{
    if( !commitPage() )
        return;

    static const wxChar* defaults[] = { wxS( "hole_size" ),     wxS( "hole_to_hole" ),
                                        wxS( "annular_ring" ),  wxS( "mask_sliver" ),
                                        wxS( "silk_to_mask" ),  wxS( "silk_to_silk" ),
                                        wxS( "board_outline" ), wxS( "component_clearance" ) };

    std::vector<int> missing;

    for( const wxChar* id : defaults )
    {
        int cat = CategoryIndex( id );

        if( cat >= 0 && m_doc.RulesInCategory( cat ).empty() )
            missing.push_back( cat );
    }

    if( missing.empty() )
    {
        DisplayInfoMessage( this, _( "All rule types with default rules already have rules." ) );
        return;
    }

    wxString list;

    for( int cat : missing )
        list << wxS( "  • " ) << Categories()[cat].name << wxS( "\n" );

    if( !IsOK( this, _( "Create default rules (with the Board Setup values) for these rule "
                        "types?\n\n" )
                             + list
                             + _( "\nClearance, width, via and differential pair defaults come "
                                  "from Board Setup and the net classes and are always shown as "
                                  "the lowest-priority entries." ) ) )
    {
        return;
    }

    for( int cat : missing )
    {
        RULE rule;
        rule.name = m_doc.UniqueName( Categories()[cat].defaultRuleName );
        m_doc.SeedRule( rule, cat, m_board );
        m_doc.AddRule( rule );
    }

    m_dirty = true;
    m_pageKind = PAGE::NONE;
    rebuildTree( currentKey() );
}


void DIALOG_PCB_RULES_EDITOR::onApply( wxCommandEvent& aEvent )
{
    if( applyChanges() )
    {
        m_pageKind = PAGE::NONE;
        rebuildTree( currentKey() );
    }
}


bool DIALOG_PCB_RULES_EDITOR::TransferDataFromWindow()
{
    return applyChanges();
}


bool DIALOG_PCB_RULES_EDITOR::applyChanges()
{
    if( !commitPage() )
        return false;

    BOARD_DESIGN_SETTINGS& bds = m_board->GetDesignSettings();
    wxString               path = m_frame->GetDesignRulesPath();

    if( m_dirty )
    {
        if( path.IsEmpty() )
        {
            DisplayError( this, _( "Save the board first; rules are stored next to it in the "
                                   "project's .kicad_dru file." ) );
            return false;
        }

        if( m_frame->Prj().IsReadOnly() )
        {
            DisplayError( this, _( "The project is read-only; rules cannot be saved." ) );
            return false;
        }

        if( !m_doc.SaveFile( path ) )
        {
            DisplayError( this, wxString::Format( _( "Could not write '%s'." ), path ) );
            return false;
        }

        for( RULE* rule : m_doc.AllRules() )
        {
            if( rule->modified )
            {
                rule->originalText = rule->Generate();
                rule->modified = false;
            }
        }

        m_dirty = false;
    }

    if( !m_boardEdits.empty() )
    {
        for( const auto& [member, value] : m_boardEdits )
            bds.*member = value;

        m_boardEdits.clear();
        m_frame->OnModify();
    }

    if( bds.m_DRCEngine && !path.IsEmpty() )
    {
        try
        {
            bds.m_DRCEngine->InitEngine( wxFileName( path ) );
        }
        catch( PARSE_ERROR& pe )
        {
            DisplayErrorMessage( this, _( "The rules were saved, but the rules file has an error "
                                          "and DRC will report it." ),
                                 pe.What() );
        }
    }

    return true;
}
