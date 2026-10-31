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

#include <widgets/altium_filter_panel.h>

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <eda_draw_frame.h>
#include <paths.h>
#include <kiplatform/ui.h>
#include <widgets/ui_common.h>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/fileconf.h>
#include <wx/filename.h>
#include <wx/listctrl.h>
#include <wx/menu.h>
#include <wx/radiobut.h>
#include <wx/scrolwin.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/srchctrl.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/stc/stc.h>
#include <wx/textdlg.h>


namespace
{

const int MAX_HISTORY = 30;

enum STYLES
{
    ST_DEFAULT = 0,
    ST_OPERATOR,
    ST_KEYWORD,
    ST_STRING,
    ST_NUMBER
};

const int ERROR_INDICATOR = 8;


wxString settingsGroup( ALTIUM_FILTER_PANEL::MODE aMode )
{
    return aMode == ALTIUM_FILTER_PANEL::MODE::PCB ? wxS( "/PCB" ) : wxS( "/SCH" );
}


wxFileConfig* openConfig()
{
    wxFileName fn( PATHS::GetUserSettingsPath(), wxS( "altium_query.ini" ) );
    return new wxFileConfig( wxEmptyString, wxEmptyString, fn.GetFullPath(), wxEmptyString,
                             wxCONFIG_USE_LOCAL_FILE );
}


/**
 * Altium's Query Helper: the keywords of the language by category, with a description; double
 * click (or Insert) puts one in the query.
 */
class QUERY_HELPER_DIALOG : public wxDialog
{
public:
    QUERY_HELPER_DIALOG( wxWindow* aParent, const std::vector<ALTIUM_QUERY::KEYWORD>& aKeys,
                         const wxString& aQuery ) :
            wxDialog( aParent, wxID_ANY, _( "Query Helper" ), wxDefaultPosition,
                      wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER ),
            m_keys( aKeys )
    {
        wxBoxSizer* top = new wxBoxSizer( wxVERTICAL );

        top->Add( new wxStaticText( this, wxID_ANY, _( "Query" ) ), 0, wxLEFT | wxRIGHT | wxTOP, 8 );
        m_query = new wxTextCtrl( this, wxID_ANY, aQuery, wxDefaultPosition,
                                  FromDIP( wxSize( -1, 70 ) ), wxTE_MULTILINE );
        top->Add( m_query, 0, wxEXPAND | wxALL, 8 );

        // Operators, as in Altium's helper
        wxBoxSizer* ops = new wxBoxSizer( wxHORIZONTAL );

        for( const wxString& op : { wxS( "(" ), wxS( ")" ), wxS( "And" ), wxS( "Or" ),
                                    wxS( "Not" ), wxS( "Xor" ), wxS( "Like" ), wxS( "=" ),
                                    wxS( "<>" ), wxS( "<" ), wxS( ">" ), wxS( "<=" ),
                                    wxS( ">=" ) } )
        {
            wxButton* btn = new wxButton( this, wxID_ANY, op, wxDefaultPosition, wxDefaultSize,
                                          wxBU_EXACTFIT );
            btn->Bind( wxEVT_BUTTON,
                       [this, op]( wxCommandEvent& )
                       {
                           insert( op.length() > 2 || op == wxS( "Or" ) ? wxS( " " ) + op + wxS( " " )
                                                                         : op );
                       } );
            ops->Add( btn, 0, wxRIGHT, 2 );
        }

        top->Add( ops, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8 );

        wxBoxSizer* lists = new wxBoxSizer( wxHORIZONTAL );

        m_categories = new wxListBox( this, wxID_ANY, wxDefaultPosition,
                                      FromDIP( wxSize( 180, -1 ) ) );
        m_categories->Append( _( "All" ) );

        for( const ALTIUM_QUERY::KEYWORD& key : m_keys )
        {
            if( m_categories->FindString( key.category ) == wxNOT_FOUND )
                m_categories->Append( key.category );
        }

        m_categories->SetSelection( 0 );
        lists->Add( m_categories, 0, wxEXPAND | wxRIGHT, 6 );

        wxBoxSizer* right = new wxBoxSizer( wxVERTICAL );
        m_mask = new wxSearchCtrl( this, wxID_ANY );
        m_mask->ShowCancelButton( true );
        m_mask->SetDescriptiveText( _( "Mask" ) );
        right->Add( m_mask, 0, wxEXPAND | wxBOTTOM, 4 );

        m_list = new wxListCtrl( this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                 wxLC_REPORT | wxLC_SINGLE_SEL );
        m_list->AppendColumn( _( "Name" ), wxLIST_FORMAT_LEFT, FromDIP( 190 ) );
        m_list->AppendColumn( _( "Description" ), wxLIST_FORMAT_LEFT, FromDIP( 420 ) );
        right->Add( m_list, 1, wxEXPAND );
        lists->Add( right, 1, wxEXPAND );

        top->Add( lists, 1, wxEXPAND | wxLEFT | wxRIGHT, 8 );

        wxBoxSizer* buttons = new wxBoxSizer( wxHORIZONTAL );
        wxButton*   insertBtn = new wxButton( this, wxID_ANY, _( "Insert" ) );
        buttons->Add( insertBtn, 0, wxRIGHT, 4 );
        buttons->AddStretchSpacer();
        buttons->Add( new wxButton( this, wxID_OK, _( "OK" ) ), 0, wxRIGHT, 4 );
        buttons->Add( new wxButton( this, wxID_CANCEL, _( "Cancel" ) ), 0 );
        top->Add( buttons, 0, wxEXPAND | wxALL, 8 );

        SetSizer( top );
        SetSize( FromDIP( wxSize( 900, 620 ) ) );
        CentreOnParent();

        fill();

        m_categories->Bind( wxEVT_LISTBOX, [this]( wxCommandEvent& ) { fill(); } );
        m_mask->Bind( wxEVT_TEXT, [this]( wxCommandEvent& ) { fill(); } );
        m_list->Bind( wxEVT_LIST_ITEM_ACTIVATED, [this]( wxListEvent& ) { insertSelected(); } );
        insertBtn->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { insertSelected(); } );
    }

    wxString GetQuery() const { return m_query->GetValue(); }

private:
    void fill()
    {
        wxString category = m_categories->GetStringSelection();
        wxString mask = m_mask->GetValue().Lower();

        m_list->DeleteAllItems();
        m_shown.clear();

        for( size_t ii = 0; ii < m_keys.size(); ++ii )
        {
            const ALTIUM_QUERY::KEYWORD& key = m_keys[ii];

            if( m_categories->GetSelection() > 0 && key.category != category )
                continue;

            if( !mask.IsEmpty() && !key.name.Lower().Contains( mask )
                    && !key.description.Lower().Contains( mask ) )
            {
                continue;
            }

            long row = m_list->InsertItem( m_list->GetItemCount(), key.example );
            m_list->SetItem( row, 1, key.description );
            m_shown.push_back( ii );
        }
    }

    void insert( const wxString& aText )
    {
        m_query->WriteText( aText );
        m_query->SetFocus();
    }

    void insertSelected()
    {
        long sel = m_list->GetNextItem( -1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED );

        if( sel < 0 || sel >= (long) m_shown.size() )
            return;

        wxString text = m_keys[m_shown[sel]].example;
        wxString before = m_query->GetRange( 0, m_query->GetInsertionPoint() );

        if( !before.IsEmpty() && !before.EndsWith( wxS( " " ) ) && !before.EndsWith( wxS( "(" ) ) )
            text = wxS( " " ) + text;

        insert( text );
    }

    const std::vector<ALTIUM_QUERY::KEYWORD>& m_keys;
    wxTextCtrl*                               m_query;
    wxListBox*                                m_categories;
    wxSearchCtrl*                             m_mask;
    wxListCtrl*                               m_list;
    std::vector<size_t>                       m_shown;
};

} // namespace


ALTIUM_FILTER_PANEL::ALTIUM_FILTER_PANEL( wxWindow* aParent, EDA_DRAW_FRAME* aFrame, MODE aMode,
                                          ALTIUM_QUERY::ADAPTER& aAdapter ) :
        wxPanel( aParent, wxID_ANY ),
        m_frame( aFrame ),
        m_mode( aMode ),
        m_adapter( aAdapter )
{
    SetFont( KIUI::GetDockedPaneFont( this ) );

    wxBoxSizer* top = new wxBoxSizer( wxVERTICAL );

    if( m_mode == MODE::PCB )
        buildPcbBuilder( top );
    else
        buildSchHeader( top );

    // The query
    top->Add( new wxStaticText( this, wxID_ANY, m_mode == MODE::PCB
                                                        ? _( "Filter" )
                                                        : _( "Find items matching these criteria:" ) ),
              0, wxLEFT | wxRIGHT | wxTOP, 4 );

    m_queryCtrl = new wxStyledTextCtrl( this, wxID_ANY, wxDefaultPosition,
                                        FromDIP( wxSize( -1, m_mode == MODE::PCB ? 90 : 160 ) ),
                                        wxBORDER_SIMPLE );
    m_queryCtrl->SetWrapMode( wxSTC_WRAP_WORD );
    m_queryCtrl->SetMarginWidth( 1, 0 );
    m_queryCtrl->SetMarginWidth( 0, 0 );
    m_queryCtrl->SetUseHorizontalScrollBar( false );
    m_queryCtrl->SetLexer( wxSTC_LEX_CONTAINER );

    wxColour fg = wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOWTEXT );
    bool     dark = KIPLATFORM::UI::IsDarkTheme();

    // Altium's query box is a well darker than the panel around it
    wxColour bg = dark ? GetBackgroundColour().ChangeLightness( 75 )
                       : wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOW );
    wxFont   mono( wxFontInfo( GetFont().GetPointSize() + 1 ).Family( wxFONTFAMILY_TELETYPE )
                                                            .FaceName( wxS( "Consolas" ) ) );

    for( int style : { (int) wxSTC_STYLE_DEFAULT, (int) ST_DEFAULT, (int) ST_OPERATOR,
                       (int) ST_KEYWORD, (int) ST_STRING, (int) ST_NUMBER } )
    {
        m_queryCtrl->StyleSetBackground( style, bg );
        m_queryCtrl->StyleSetForeground( style, fg );
        m_queryCtrl->StyleSetFont( style, mono );
    }

    // Altium: operators in bold blue, strings and numbers set apart
    m_queryCtrl->StyleSetForeground( ST_OPERATOR, dark ? wxColour( 0x56, 0x9C, 0xF0 )
                                                       : wxColour( 0x00, 0x00, 0xC0 ) );
    m_queryCtrl->StyleSetBold( ST_OPERATOR, true );
    m_queryCtrl->StyleSetForeground( ST_KEYWORD, dark ? wxColour( 0xE5, 0xE5, 0xE5 )
                                                      : wxColour( 0x00, 0x00, 0x00 ) );
    m_queryCtrl->StyleSetForeground( ST_STRING, dark ? wxColour( 0xE8, 0xA8, 0x6E )
                                                     : wxColour( 0x80, 0x00, 0x00 ) );
    m_queryCtrl->StyleSetForeground( ST_NUMBER, dark ? wxColour( 0xA6, 0xD8, 0x8E )
                                                     : wxColour( 0x00, 0x70, 0x00 ) );
    m_queryCtrl->SetCaretForeground( fg );
    m_queryCtrl->SetSelBackground( true, wxSystemSettings::GetColour( wxSYS_COLOUR_HIGHLIGHT ) );
    m_queryCtrl->IndicatorSetStyle( ERROR_INDICATOR, wxSTC_INDIC_SQUIGGLE );
    m_queryCtrl->IndicatorSetForeground( ERROR_INDICATOR, wxColour( 0xFF, 0x40, 0x40 ) );

    top->Add( m_queryCtrl, m_mode == MODE::PCB ? 0 : 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 4 );

    m_queryCtrl->Bind( wxEVT_STC_STYLENEEDED, &ALTIUM_FILTER_PANEL::onStyleNeeded, this );
    m_queryCtrl->Bind( wxEVT_STC_CHANGE,
                       [this]( wxStyledTextEvent& aEvent )
                       {
                           m_queryCtrl->SetIndicatorCurrent( ERROR_INDICATOR );
                           m_queryCtrl->IndicatorClearRange( 0, m_queryCtrl->GetLength() );
                           aEvent.Skip();
                       } );

    // Typing goes to the query, not to the editor's hotkeys; Enter applies
    m_queryCtrl->Bind( wxEVT_CHAR_HOOK,
                       [this]( wxKeyEvent& aEvent )
                       {
                           if( ( aEvent.GetKeyCode() == WXK_RETURN
                                 || aEvent.GetKeyCode() == WXK_NUMPAD_ENTER )
                                   && !aEvent.ShiftDown() )
                           {
                               onApply( false );
                               return;
                           }

                           if( aEvent.GetKeyCode() == WXK_ESCAPE )
                           {
                               aEvent.Skip();
                               return;
                           }

                           aEvent.DoAllowNextEvent();
                       } );

    // Helper / Builder / History / Add To Favorites / Create Rule / Favorites
    wxGridSizer* btnGrid = new wxGridSizer( m_mode == MODE::PCB ? 2 : 1, 3, 2, 2 );

    auto makeButton =
            [&]( const wxString& aLabel, const wxString& aTip ) -> wxButton*
            {
                wxButton* btn = new wxButton( this, wxID_ANY, aLabel );
                btn->SetToolTip( aTip );
                btnGrid->Add( btn, 1, wxEXPAND );
                return btn;
            };

    wxButton* helperBtn = makeButton( _( "Helper" ), _( "Build a query from the list of keywords" ) );
    wxButton* builderBtn = nullptr;
    wxButton* addFavBtn = nullptr;
    wxButton* ruleBtn = nullptr;

    if( m_mode == MODE::PCB )
        builderBtn = makeButton( _( "Builder" ), _( "Build a query from conditions" ) );

    wxButton* historyBtn = nullptr;
    wxButton* favBtn = nullptr;

    if( m_mode == MODE::PCB )
    {
        historyBtn = makeButton( _( "History" ), _( "Queries applied earlier" ) );
        addFavBtn = makeButton( _( "Add To Favorites" ), _( "Keep this query in the favorites" ) );
        ruleBtn = makeButton( _( "Create Rule" ), _( "Make a design rule scoped by this query" ) );
        favBtn = makeButton( _( "Favorites" ), _( "Queries kept as favorites" ) );
    }
    else
    {
        favBtn = makeButton( _( "Favorites" ), _( "Queries kept as favorites" ) );
        historyBtn = makeButton( _( "History" ), _( "Queries applied earlier" ) );
    }

    // No KiCad counterpart yet
    if( builderBtn )
        builderBtn->Enable( false );

    if( ruleBtn )
        ruleBtn->Enable( false );

    top->Add( btnGrid, 0, wxEXPAND | wxALL, 4 );

    helperBtn->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { showHelper(); } );
    historyBtn->Bind( wxEVT_BUTTON,
                      [this, historyBtn]( wxCommandEvent& ) { showHistoryMenu( historyBtn ); } );
    favBtn->Bind( wxEVT_BUTTON,
                  [this, favBtn]( wxCommandEvent& ) { showFavoritesMenu( favBtn ); } );

    if( addFavBtn )
        addFavBtn->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { addToFavorites(); } );

    // What happens to matching and non-matching objects
    m_selectCheck = new wxCheckBox( this, wxID_ANY, _( "Select" ) );
    m_zoomCheck = new wxCheckBox( this, wxID_ANY, _( "Zoom" ) );
    m_deselectCheck = new wxCheckBox( this, wxID_ANY, _( "Deselect" ) );
    m_deselectCheck->SetValue( true );

    if( m_mode == MODE::PCB )
    {
        wxBoxSizer* opts = new wxBoxSizer( wxHORIZONTAL );
        wxBoxSizer* matching = new wxBoxSizer( wxVERTICAL );
        wxBoxSizer* nonMatching = new wxBoxSizer( wxVERTICAL );

        matching->Add( new wxStaticText( this, wxID_ANY, _( "Matching" ) ), 0, wxBOTTOM, 4 );
        matching->Add( m_selectCheck, 0, wxBOTTOM, 4 );
        matching->Add( m_zoomCheck, 0 );

        nonMatching->Add( new wxStaticText( this, wxID_ANY, _( "Non-matching" ) ), 0, wxBOTTOM, 4 );
        nonMatching->Add( m_deselectCheck, 0, wxBOTTOM, 4 );

        m_maskChoice = new wxChoice( this, wxID_ANY );
        m_maskChoice->Append( _( "Mask" ) );
        m_maskChoice->Append( _( "Dim" ) );
        m_maskChoice->Append( _( "Normal" ) );
        m_maskChoice->SetSelection( MASK );
        m_maskChoice->SetToolTip( _( "Mask: fade the other objects strongly\n"
                                     "Dim: fade them a little\n"
                                     "Normal: leave them as they are" ) );
        nonMatching->Add( m_maskChoice, 0 );

        opts->Add( matching, 0, wxLEFT, 20 );
        opts->AddStretchSpacer();
        opts->Add( nonMatching, 0, wxRIGHT, 20 );
        top->Add( opts, 0, wxEXPAND | wxALL, 4 );

        wxBoxSizer* apply = new wxBoxSizer( wxHORIZONTAL );
        m_clearBtn = new wxButton( this, wxID_ANY, _( "Clear" ) );
        wxButton* applySelBtn = new wxButton( this, wxID_ANY, _( "Apply to Selected" ) );
        wxButton* applyAllBtn = new wxButton( this, wxID_ANY, _( "Apply to All" ) );
        apply->Add( m_clearBtn, 2, wxRIGHT, 2 );
        apply->Add( applySelBtn, 3, wxRIGHT, 2 );
        apply->Add( applyAllBtn, 3 );
        top->Add( apply, 0, wxEXPAND | wxALL, 4 );

        applySelBtn->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { onApply( true ); } );
        applyAllBtn->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { onApply( false ); } );
    }
    else
    {
        top->Add( new wxStaticText( this, wxID_ANY, _( "Objects passing the filter" ) ), 0,
                  wxLEFT | wxRIGHT | wxTOP, 4 );
        top->Add( m_selectCheck, 0, wxLEFT | wxTOP, 8 );
        top->Add( m_zoomCheck, 0, wxLEFT | wxTOP, 8 );
        m_selectCheck->SetValue( true );
        m_zoomCheck->SetValue( true );

        top->Add( new wxStaticText( this, wxID_ANY, _( "Objects not passing the filter" ) ), 0,
                  wxLEFT | wxRIGHT | wxTOP, 4 );
        top->Add( m_deselectCheck, 0, wxLEFT | wxTOP, 8 );

        m_maskCheck = new wxCheckBox( this, wxID_ANY, _( "Mask out" ) );
        m_maskCheck->SetValue( true );
        top->Add( m_maskCheck, 0, wxLEFT | wxTOP, 8 );

        m_maskSlider = new wxSlider( this, wxID_ANY, 80, 10, 100 );
        m_maskSlider->SetToolTip( _( "How strongly the other objects are faded" ) );
        top->Add( m_maskSlider, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 4 );

        wxBoxSizer* apply = new wxBoxSizer( wxHORIZONTAL );
        m_clearBtn = new wxButton( this, wxID_ANY, _( "Clear" ) );
        wxButton* applyBtn = new wxButton( this, wxID_ANY, _( "Apply" ) );
        apply->Add( m_clearBtn, 1, wxRIGHT, 2 );
        apply->Add( applyBtn, 3 );
        top->Add( apply, 0, wxEXPAND | wxALL, 4 );

        applyBtn->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { onApply( false ); } );
    }

    m_clearBtn->Enable( false );
    m_clearBtn->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { ClearFilter(); } );

    m_status = new wxStaticText( this, wxID_ANY, _( "Not filtered" ), wxDefaultPosition,
                                 wxDefaultSize, wxST_ELLIPSIZE_END );
    top->Add( m_status, 0, wxEXPAND | wxALL, 4 );

    SetSizer( top );

    loadLists();
}


ALTIUM_FILTER_PANEL::~ALTIUM_FILTER_PANEL()
{
}


void ALTIUM_FILTER_PANEL::buildSchHeader( wxSizer* aSizer )
{
    aSizer->Add( new wxStaticText( this, wxID_ANY, _( "Limit search to" ) ), 0,
                 wxLEFT | wxRIGHT | wxTOP, 4 );

    m_limitAll = new wxRadioButton( this, wxID_ANY, _( "All Objects" ), wxDefaultPosition,
                                    wxDefaultSize, wxRB_GROUP );
    m_limitSelected = new wxRadioButton( this, wxID_ANY, _( "Selected Objects" ) );
    m_limitNonSelected = new wxRadioButton( this, wxID_ANY, _( "Non Selected Objects" ) );
    m_limitAll->SetValue( true );

    aSizer->Add( m_limitAll, 0, wxLEFT | wxTOP, 8 );
    aSizer->Add( m_limitSelected, 0, wxLEFT | wxTOP, 8 );
    aSizer->Add( m_limitNonSelected, 0, wxLEFT | wxTOP, 8 );

    aSizer->Add( new wxStaticText( this, wxID_ANY, _( "Consider objects in" ) ), 0,
                 wxLEFT | wxRIGHT | wxTOP, 4 );

    m_scopeChoice = new wxChoice( this, wxID_ANY );
    m_scopeChoice->Append( _( "Current Document" ) );
    m_scopeChoice->Append( _( "Open Documents" ) );
    m_scopeChoice->Append( _( "Whole Project" ) );
    m_scopeChoice->SetSelection( 0 );
    m_scopeChoice->SetToolTip( _( "Other sheets are counted; matches on them are listed in "
                                  "the status line" ) );
    aSizer->Add( m_scopeChoice, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 4 );
}


void ALTIUM_FILTER_PANEL::buildPcbBuilder( wxSizer* aSizer )
{
    // "Select objects to highlight": object kinds by Net / Comp / Free, as in Altium
    wxStaticText* title = new wxStaticText( this, wxID_ANY, _( "Select objects to highlight" ) );
    aSizer->Add( title, 0, wxLEFT | wxRIGHT | wxTOP, 4 );

    wxScrolledWindow* objects = new wxScrolledWindow( this, wxID_ANY, wxDefaultPosition,
                                                      wxDefaultSize, wxVSCROLL | wxBORDER_SIMPLE );
    objects->SetScrollRate( 0, 10 );
    objects->SetBackgroundColour( wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOW ) );

    wxFlexGridSizer* grid = new wxFlexGridSizer( 4, 2, 8 );
    grid->AddGrowableCol( 0, 1 );
    grid->Add( new wxStaticText( objects, wxID_ANY, _( "Object" ) ), 0, wxLEFT, 4 );
    grid->Add( new wxStaticText( objects, wxID_ANY, _( "Net" ) ), 0, wxALIGN_CENTER );
    grid->Add( new wxStaticText( objects, wxID_ANY, _( "Comp" ) ), 0, wxALIGN_CENTER );
    grid->Add( new wxStaticText( objects, wxID_ANY, _( "Free" ) ), 0, wxALIGN_CENTER | wxRIGHT, 6 );

    struct ROWDEF { wxString label; wxString keyword; bool net; bool comp; };

    const ROWDEF defs[] = { { _( "Component" ), wxS( "IsComponent" ), false, false },
                            { _( "Polygon" ),   wxS( "IsPolygon" ),   false, false },
                            { _( "Text" ),      wxS( "IsText" ),      false, true },
                            { _( "Track" ),     wxS( "IsTrack" ),     true, true },
                            { _( "Arc" ),       wxS( "IsArc" ),       true, true },
                            { _( "Via" ),       wxS( "IsVia" ),       true, true },
                            { _( "Pad" ),       wxS( "IsPad" ),       true, true },
                            { _( "Fill" ),      wxS( "IsFill" ),      true, true },
                            { _( "Region" ),    wxS( "IsRegion" ),    true, true } };

    for( const ROWDEF& def : defs )
    {
        OBJECT_ROW row;
        row.keyword = def.keyword;

        grid->Add( new wxStaticText( objects, wxID_ANY, def.label ), 0,
                   wxLEFT | wxALIGN_CENTER_VERTICAL, 4 );

        auto addCheck =
                [&]( bool aShow, wxCheckBox*& aCheck, int aFlags )
                {
                    if( aShow )
                    {
                        aCheck = new wxCheckBox( objects, wxID_ANY, wxEmptyString );
                        aCheck->Bind( wxEVT_CHECKBOX,
                                      [this]( wxCommandEvent& ) { updateQueryFromBuilder(); } );
                        grid->Add( aCheck, 0, wxALIGN_CENTER | aFlags, 6 );
                    }
                    else
                    {
                        grid->AddSpacer( 1 );
                    }
                };

        addCheck( def.net, row.net, 0 );
        addCheck( def.comp, row.comp, 0 );
        addCheck( true, row.free, wxRIGHT );
        m_objectRows.push_back( row );
    }

    objects->SetSizer( grid );
    objects->FitInside();
    aSizer->Add( objects, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 4 );
    objects->SetMinSize( wxSize( -1, grid->GetMinSize().y + 4 ) );

    aSizer->Add( new wxStaticText( this, wxID_ANY, _( "Layer" ) ), 0, wxLEFT | wxRIGHT | wxTOP, 4 );

    wxScrolledWindow* layers = new wxScrolledWindow( this, wxID_ANY, wxDefaultPosition,
                                                     FromDIP( wxSize( -1, 120 ) ),
                                                     wxVSCROLL | wxBORDER_SIMPLE );
    layers->SetScrollRate( 0, 10 );
    layers->SetBackgroundColour( wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOW ) );
    layers->SetSizer( new wxBoxSizer( wxVERTICAL ) );
    m_layerList = layers;
    aSizer->Add( layers, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 4 );
}


void ALTIUM_FILTER_PANEL::RebuildLayerList()
{
    if( !m_layerList )
        return;

    std::vector<wxString> layers = builderLayers();
    std::vector<wxString> keys = { wxS( "<All Layers>" ), wxS( "<Component Layers>" ),
                                   wxS( "<Electrical Layers>" ), wxS( "<Signal Layers>" ) };

    keys.insert( keys.end(), layers.begin(), layers.end() );

    if( keys == m_layerKeys )
        return;

    std::set<wxString> checked;

    for( size_t ii = 0; ii < m_layerChecks.size(); ++ii )
    {
        if( m_layerChecks[ii]->GetValue() )
            checked.insert( m_layerKeys[ii] );
    }

    m_layerList->DestroyChildren();
    m_layerChecks.clear();
    m_layerKeys = keys;

    wxSizer* sizer = m_layerList->GetSizer();
    sizer->Clear();

    for( const wxString& key : keys )
    {
        wxCheckBox* check = new wxCheckBox( m_layerList, wxID_ANY, key );
        check->SetValue( checked.count( key ) > 0 );
        check->Bind( wxEVT_CHECKBOX, [this]( wxCommandEvent& ) { updateQueryFromBuilder(); } );
        sizer->Add( check, 0, wxLEFT | wxTOP, 4 );
        m_layerChecks.push_back( check );
    }

    m_layerList->FitInside();
    m_layerList->Layout();
}


void ALTIUM_FILTER_PANEL::updateQueryFromBuilder()
{
    wxArrayString objects;

    for( const OBJECT_ROW& row : m_objectRows )
    {
        bool net = row.net && row.net->GetValue();
        bool comp = row.comp && row.comp->GetValue();
        bool free = row.free && row.free->GetValue();

        int available = ( row.net ? 1 : 0 ) + ( row.comp ? 1 : 0 ) + 1;
        int checked = ( net ? 1 : 0 ) + ( comp ? 1 : 0 ) + ( free ? 1 : 0 );

        if( checked == 0 )
            continue;

        if( checked == available )
        {
            objects.Add( row.keyword );
            continue;
        }

        wxArrayString kinds;

        if( net )
            kinds.Add( wxS( "InAnyNet" ) );

        if( comp )
            kinds.Add( wxS( "InAnyComponent" ) );

        if( free )
            kinds.Add( wxS( "IsFree" ) );

        wxString which;

        for( const wxString& kind : kinds )
            which += ( which.IsEmpty() ? wxString() : wxString( wxS( " Or " ) ) ) + kind;

        if( kinds.size() > 1 )
            which = wxS( "(" ) + which + wxS( ")" );

        objects.Add( wxS( "(" ) + row.keyword + wxS( " And " ) + which + wxS( ")" ) );
    }

    wxArrayString layers;

    for( size_t ii = 0; ii < m_layerChecks.size(); ++ii )
    {
        if( !m_layerChecks[ii]->GetValue() )
            continue;

        const wxString& key = m_layerKeys[ii];

        if( key == wxS( "<All Layers>" ) )
        {
            layers.clear();
            layers.Add( wxEmptyString );
            break;
        }
        else if( key == wxS( "<Component Layers>" ) )
            layers.Add( wxS( "OnComponentLayer" ) );
        else if( key == wxS( "<Electrical Layers>" ) )
            layers.Add( wxS( "OnElectricalLayer" ) );
        else if( key == wxS( "<Signal Layers>" ) )
            layers.Add( wxS( "OnSignalLayer" ) );
        else
            layers.Add( wxS( "OnLayer('" ) + key + wxS( "')" ) );
    }

    if( layers.size() == 1 && layers[0].IsEmpty() )
        layers.clear();

    auto joinOr =
            []( const wxArrayString& aTerms ) -> wxString
            {
                wxString out;

                for( const wxString& term : aTerms )
                    out += ( out.IsEmpty() ? wxString() : wxString( wxS( " Or " ) ) ) + term;

                return aTerms.size() > 1 ? wxS( "(" ) + out + wxS( ")" ) : out;
            };

    wxString query;

    if( !objects.IsEmpty() && !layers.IsEmpty() )
        query = joinOr( objects ) + wxS( " And " ) + joinOr( layers );
    else if( !objects.IsEmpty() )
        query = joinOr( objects );
    else if( !layers.IsEmpty() )
        query = joinOr( layers );

    // Strip the outer brackets of a lone Or list, as Altium shows it
    if( layers.IsEmpty() && objects.size() > 1 )
        query = query.Mid( 1, query.length() - 2 );

    m_settingFromBuilder = true;
    SetQuery( query );
    m_settingFromBuilder = false;
}


void ALTIUM_FILTER_PANEL::FocusQuery()
{
    m_queryCtrl->SetFocus();
}


wxString ALTIUM_FILTER_PANEL::GetQuery() const
{
    return m_queryCtrl->GetText();
}


void ALTIUM_FILTER_PANEL::SetQuery( const wxString& aQuery )
{
    m_queryCtrl->SetText( aQuery );
    m_queryCtrl->GotoPos( m_queryCtrl->GetLength() );
    styleQuery();
}


void ALTIUM_FILTER_PANEL::onStyleNeeded( wxStyledTextEvent& aEvent )
{
    styleQuery();
}


void ALTIUM_FILTER_PANEL::styleQuery()
{
    wxString text = m_queryCtrl->GetText();
    int      len = m_queryCtrl->GetLength();

    // Style by byte position; the query is ASCII in practice, so map characters 1:1 and stop
    // at anything wider
    std::set<wxString> ops;

    for( const wxString& op : ALTIUM_QUERY::Operators() )
        ops.insert( op.Lower() );

    m_queryCtrl->StartStyling( 0 );
    m_queryCtrl->SetStyling( len, ST_DEFAULT );

    size_t n = text.length();

    if( (int) n != len )
        return;

    size_t i = 0;

    while( i < n )
    {
        wxUniChar c = text[i];

        if( c == '\'' || c == '"' )
        {
            size_t j = i + 1;

            while( j < n && text[j] != c )
                j++;

            j = std::min( n, j + 1 );
            m_queryCtrl->StartStyling( (int) i );
            m_queryCtrl->SetStyling( (int) ( j - i ), ST_STRING );
            i = j;
        }
        else if( wxIsdigit( c ) )
        {
            size_t j = i;

            while( j < n && ( wxIsalnum( text[j] ) || text[j] == '.' ) )
                j++;

            m_queryCtrl->StartStyling( (int) i );
            m_queryCtrl->SetStyling( (int) ( j - i ), ST_NUMBER );
            i = j;
        }
        else if( wxIsalpha( c ) || c == '_' )
        {
            size_t j = i;

            while( j < n && ( wxIsalnum( text[j] ) || text[j] == '_' ) )
                j++;

            wxString word = text.Mid( i, j - i ).Lower();
            m_queryCtrl->StartStyling( (int) i );
            m_queryCtrl->SetStyling( (int) ( j - i ), ops.count( word ) ? ST_OPERATOR : ST_KEYWORD );
            i = j;
        }
        else
        {
            i++;
        }
    }
}


void ALTIUM_FILTER_PANEL::showError( const wxString& aMessage, int aPos )
{
    m_status->SetForegroundColour( wxColour( 0xFF, 0x50, 0x50 ) );
    m_status->SetLabel( aMessage );
    m_status->SetToolTip( aMessage );

    if( aPos >= 0 && aPos <= m_queryCtrl->GetLength() )
    {
        int end = m_queryCtrl->WordEndPosition( aPos, true );

        if( end <= aPos )
            end = std::min( aPos + 1, m_queryCtrl->GetLength() );

        m_queryCtrl->SetIndicatorCurrent( ERROR_INDICATOR );
        m_queryCtrl->IndicatorClearRange( 0, m_queryCtrl->GetLength() );
        m_queryCtrl->IndicatorFillRange( aPos, std::max( 1, end - aPos ) );
        m_queryCtrl->GotoPos( aPos );
    }

    m_queryCtrl->SetFocus();
}


void ALTIUM_FILTER_PANEL::showStatus( const wxString& aMessage )
{
    m_status->SetForegroundColour( wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOWTEXT ) );
    m_status->SetLabel( aMessage );
    m_status->SetToolTip( aMessage );
}


void ALTIUM_FILTER_PANEL::onApply( bool aSelectedOnly )
{
    wxString text = GetQuery();

    if( !m_query.Compile( text, m_adapter ) )
    {
        showError( m_query.GetError(), m_query.GetErrorPos() );
        return;
    }

    if( m_query.IsEmpty() )
    {
        ClearFilter();
        return;
    }

    OPTIONS opts;
    opts.selectedOnly = aSelectedOnly;
    opts.select = m_selectCheck->GetValue();
    opts.zoom = m_zoomCheck->GetValue();
    opts.deselect = m_deselectCheck->GetValue();

    if( m_mode == MODE::PCB )
    {
        int mode = m_maskChoice->GetSelection();
        opts.mask = mode != NORMAL;
        opts.maskLevel = mode == MASK ? 0.85 : 0.55;
    }
    else
    {
        opts.selectedOnly = m_limitSelected->GetValue();
        opts.nonSelectedOnly = m_limitNonSelected->GetValue();
        opts.scope = m_scopeChoice->GetSelection();
        opts.mask = m_maskCheck->GetValue();
        opts.maskLevel = m_maskSlider->GetValue() / 100.0;
    }

    wxString message;
    int      count = applyQuery( m_query, opts, message );

    if( count < 0 )
    {
        showError( message, -1 );
        return;
    }

    rememberQuery( text.Strip( wxString::both ) );
    m_clearBtn->Enable( true );

    if( message.IsEmpty() )
    {
        message = count == 1 ? _( "1 object matches" )
                             : wxString::Format( _( "%d objects match" ), count );
    }

    showStatus( message );
}


void ALTIUM_FILTER_PANEL::ClearFilter()
{
    clearMask();
    m_clearBtn->Enable( false );
    showStatus( _( "Not filtered" ) );
}


void ALTIUM_FILTER_PANEL::showHelper()
{
    QUERY_HELPER_DIALOG dlg( this, m_adapter.Keywords(), GetQuery() );

    if( dlg.ShowModal() == wxID_OK )
        SetQuery( dlg.GetQuery() );
}


void ALTIUM_FILTER_PANEL::showHistoryMenu( wxWindow* aButton )
{
    wxMenu menu;

    if( m_history.empty() )
        menu.Append( wxID_ANY, _( "(no queries yet)" ) )->Enable( false );

    for( size_t ii = 0; ii < m_history.size() && ii < MAX_HISTORY; ++ii )
    {
        wxString label = m_history[ii];
        label.Replace( wxS( "&" ), wxS( "&&" ) );
        menu.Append( wxID_HIGHEST + 1 + (int) ii, label );
    }

    if( !m_history.empty() )
    {
        menu.AppendSeparator();
        menu.Append( wxID_CLEAR, _( "Clear History" ) );
    }

    int picked = aButton->GetPopupMenuSelectionFromUser( menu, 0, aButton->GetSize().y );

    if( picked == wxID_CLEAR )
    {
        m_history.clear();
        saveLists();
    }
    else if( picked > wxID_HIGHEST && picked <= wxID_HIGHEST + (int) m_history.size() )
    {
        SetQuery( m_history[picked - wxID_HIGHEST - 1] );
        FocusQuery();
    }
}


void ALTIUM_FILTER_PANEL::showFavoritesMenu( wxWindow* aButton )
{
    wxMenu  menu;
    wxMenu* removeMenu = new wxMenu;
    int     base = wxID_HIGHEST + 1;
    int     removeBase = base + 1000;

    if( m_favorites.empty() )
        menu.Append( wxID_ANY, _( "(no favorites yet)" ) )->Enable( false );

    for( size_t ii = 0; ii < m_favorites.size(); ++ii )
    {
        wxString label = m_favorites[ii].first;
        label.Replace( wxS( "&" ), wxS( "&&" ) );
        menu.Append( base + (int) ii, label )->SetHelp( m_favorites[ii].second );
        removeMenu->Append( removeBase + (int) ii, label );
    }

    menu.AppendSeparator();
    menu.Append( wxID_ADD, _( "Add Current Query..." ) )->Enable( !GetQuery().IsEmpty() );

    if( !m_favorites.empty() )
        menu.AppendSubMenu( removeMenu, _( "Remove" ) );
    else
        delete removeMenu;

    int picked = aButton->GetPopupMenuSelectionFromUser( menu, 0, aButton->GetSize().y );

    if( picked == wxID_ADD )
    {
        addToFavorites();
    }
    else if( picked >= removeBase && picked < removeBase + (int) m_favorites.size() )
    {
        m_favorites.erase( m_favorites.begin() + ( picked - removeBase ) );
        saveLists();
    }
    else if( picked >= base && picked < base + (int) m_favorites.size() )
    {
        SetQuery( m_favorites[picked - base].second );
        FocusQuery();
    }
}


void ALTIUM_FILTER_PANEL::addToFavorites()
{
    wxString query = GetQuery().Strip( wxString::both );

    if( query.IsEmpty() )
        return;

    wxTextEntryDialog dlg( this, _( "Name:" ), _( "Add To Favorites" ), query );

    if( dlg.ShowModal() != wxID_OK || dlg.GetValue().Strip( wxString::both ).IsEmpty() )
        return;

    wxString name = dlg.GetValue().Strip( wxString::both );

    for( std::pair<wxString, wxString>& fav : m_favorites )
    {
        if( fav.first == name )
        {
            fav.second = query;
            saveLists();
            return;
        }
    }

    m_favorites.emplace_back( name, query );
    saveLists();
}


void ALTIUM_FILTER_PANEL::rememberQuery( const wxString& aQuery )
{
    if( aQuery.IsEmpty() )
        return;

    m_history.erase( std::remove( m_history.begin(), m_history.end(), aQuery ), m_history.end() );
    m_history.insert( m_history.begin(), aQuery );

    if( m_history.size() > MAX_HISTORY )
        m_history.resize( MAX_HISTORY );

    saveLists();
}


void ALTIUM_FILTER_PANEL::loadLists()
{
    std::unique_ptr<wxFileConfig> cfg( openConfig() );
    wxString                      group = settingsGroup( m_mode );
    wxString                      value;

    m_history.clear();
    m_favorites.clear();

    for( int ii = 0; ii < MAX_HISTORY; ++ii )
    {
        if( cfg->Read( wxString::Format( wxS( "%s/History/%d" ), group, ii ), &value )
                && !value.IsEmpty() )
        {
            m_history.push_back( value );
        }
    }

    for( int ii = 0;; ++ii )
    {
        wxString name, query;

        if( !cfg->Read( wxString::Format( wxS( "%s/Favorites/%d/Name" ), group, ii ), &name ) )
            break;

        cfg->Read( wxString::Format( wxS( "%s/Favorites/%d/Query" ), group, ii ), &query );
        m_favorites.emplace_back( name, query );
    }

    // Altium's defaults, so the list isn't empty the first time
    if( m_favorites.empty() && !cfg->HasGroup( group + wxS( "/Favorites" ) ) )
    {
        if( m_mode == MODE::PCB )
        {
            m_favorites.emplace_back( _( "All pads" ), wxS( "IsPad" ) );
            m_favorites.emplace_back( _( "All vias" ), wxS( "IsVia" ) );
            m_favorites.emplace_back( _( "Free primitives" ), wxS( "IsFree" ) );
            m_favorites.emplace_back( _( "Unconnected (no net) copper" ),
                                      wxS( "OnSignalLayer And Not InAnyNet And Not IsComponent" ) );
        }
        else
        {
            m_favorites.emplace_back( _( "All parts" ), wxS( "IsPart" ) );
            m_favorites.emplace_back( _( "Parts without footprint" ),
                                      wxS( "IsPart And Footprint = ''" ) );
            m_favorites.emplace_back( _( "Net labels" ), wxS( "IsNetLabel" ) );
        }
    }
}


void ALTIUM_FILTER_PANEL::saveLists()
{
    std::unique_ptr<wxFileConfig> cfg( openConfig() );
    wxString                      group = settingsGroup( m_mode );

    cfg->DeleteGroup( group );

    for( size_t ii = 0; ii < m_history.size(); ++ii )
        cfg->Write( wxString::Format( wxS( "%s/History/%d" ), group, (int) ii ), m_history[ii] );

    // Written even when empty, so removing every favorite sticks
    cfg->Write( group + wxS( "/Favorites/Count" ), (long) m_favorites.size() );

    for( size_t ii = 0; ii < m_favorites.size(); ++ii )
    {
        cfg->Write( wxString::Format( wxS( "%s/Favorites/%d/Name" ), group, (int) ii ),
                    m_favorites[ii].first );
        cfg->Write( wxString::Format( wxS( "%s/Favorites/%d/Query" ), group, (int) ii ),
                    m_favorites[ii].second );
    }

    cfg->Flush();
}
