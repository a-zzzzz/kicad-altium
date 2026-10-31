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

#include "dialog_rule_wizard.h"

#include <algorithm>
#include <cmath>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/checklst.h>
#include <wx/choice.h>
#include <wx/combobox.h>
#include <wx/dataview.h>
#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <wx/radiobut.h>
#include <wx/settings.h>
#include <wx/simplebook.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/treectrl.h>

#include <base_units.h>
#include <board.h>
#include <board_design_settings.h>
#include <confirm.h>
#include <drc/drc_engine.h>
#include <footprint.h>
#include <ki_exception.h>
#include <lset.h>
#include <netinfo.h>
#include <pcb_edit_frame.h>
#include <project.h>
#include <widgets/unit_binder.h>

using namespace PCB_RULES;


namespace
{

wxColour mixColour( const wxColour& a, const wxColour& b, double t )
{
    return wxColour( a.Red() + ( b.Red() - a.Red() ) * t, a.Green() + ( b.Green() - a.Green() ) * t,
                     a.Blue() + ( b.Blue() - a.Blue() ) * t );
}


wxColour faceColour()
{
    return wxSystemSettings::GetColour( wxSYS_COLOUR_BTNFACE );
}


/**
 * Original artwork: a few translucent board layers stacked at an angle, with pads and traces,
 * drawn into aRect.  aFade fades the drawing out towards the left (for the header band).
 */
void drawBoardArt( wxDC& aDC, const wxRect& aRect, bool aFadeLeft )
{
    std::unique_ptr<wxGraphicsContext> gc( wxGraphicsContext::CreateFromUnknownDC( aDC ) );

    if( !gc )
        return;

    gc->Clip( aRect.x, aRect.y, aRect.width, aRect.height );

    const double w = aRect.width;
    const double h = aRect.height;

    // Three boards: a parallelogram each, drifting down the strip
    struct PLANE
    {
        double x, y;     // top-left corner, as a fraction of the rect
        wxColour fill;
        wxColour edge;
    };

    const PLANE planes[] = {
        { -0.25, 0.10, wxColour( 120, 150, 170, 70 ), wxColour( 150, 190, 210, 150 ) },
        { -0.20, 0.38, wxColour( 170, 120, 70, 60 ), wxColour( 220, 150, 80, 140 ) },
        { -0.15, 0.66, wxColour( 90, 140, 110, 70 ), wxColour( 140, 200, 160, 150 ) },
    };

    double pw = w * 1.15;               // plane width
    double ph = h * 0.20;               // plane height
    double skew = ph * 0.9;             // horizontal shift of the bottom edge

    for( const PLANE& p : planes )
    {
        double x0 = aRect.x + p.x * w;
        double y0 = aRect.y + p.y * h;

        auto at = [&]( double u, double v ) -> wxPoint2DDouble
        {
            return wxPoint2DDouble( x0 + u * pw + v * skew, y0 + v * ph - u * ph * 0.35 );
        };

        wxGraphicsPath path = gc->CreatePath();
        wxPoint2DDouble c0 = at( 0, 0 ), c1 = at( 1, 0 ), c2 = at( 1, 1 ), c3 = at( 0, 1 );
        path.MoveToPoint( c0 );
        path.AddLineToPoint( c1 );
        path.AddLineToPoint( c2 );
        path.AddLineToPoint( c3 );
        path.CloseSubpath();

        gc->SetBrush( wxBrush( p.fill ) );
        gc->SetPen( wxPen( p.edge, 1 ) );
        gc->DrawPath( path );

        // Traces
        gc->SetPen( wxPen( wxColour( 230, 160, 80, 170 ), std::max( 1.0, h / 260.0 ) ) );

        for( int t = 0; t < 4; ++t )
        {
            double v = 0.2 + t * 0.18;
            wxGraphicsPath tr = gc->CreatePath();
            tr.MoveToPoint( at( 0.05, v ) );
            tr.AddLineToPoint( at( 0.45, v ) );
            tr.AddLineToPoint( at( 0.55, v + 0.08 ) );
            tr.AddLineToPoint( at( 0.95, v + 0.08 ) );
            gc->StrokePath( tr );
        }

        // Pads
        gc->SetPen( *wxTRANSPARENT_PEN );
        gc->SetBrush( wxBrush( wxColour( 200, 220, 235, 150 ) ) );
        double r = std::max( 1.5, h / 220.0 );

        for( int i = 0; i < 14; ++i )
        {
            wxPoint2DDouble pt = at( 0.08 + i * 0.065, 0.12 );
            gc->DrawEllipse( pt.m_x - r, pt.m_y - r, 2 * r, 2 * r );
        }
    }

    if( aFadeLeft )
    {
        wxColour bg = mixColour( faceColour(), *wxBLACK, 0.17 );
        wxColour clear( bg.Red(), bg.Green(), bg.Blue(), 0 );
        gc->SetBrush( gc->CreateLinearGradientBrush( aRect.x, 0, aRect.x + w * 0.6, 0, bg, clear ) );
        gc->SetPen( *wxTRANSPARENT_PEN );
        gc->DrawRectangle( aRect.x, aRect.y, w * 0.6, h );
    }
}


/// The intro page's picture strip on the left.
class ART_STRIP : public wxPanel
{
public:
    ART_STRIP( wxWindow* aParent ) :
            wxPanel( aParent, wxID_ANY )
    {
        SetBackgroundStyle( wxBG_STYLE_PAINT );
        SetMinSize( FromDIP( wxSize( 190, -1 ) ) );

        Bind( wxEVT_PAINT,
              [this]( wxPaintEvent& )
              {
                  wxAutoBufferedPaintDC dc( this );
                  dc.SetBackground( wxBrush( mixColour( faceColour(), *wxBLACK, 0.25 ) ) );
                  dc.Clear();
                  drawBoardArt( dc, wxRect( GetClientSize() ), false );
              } );
        Bind( wxEVT_SIZE, [this]( wxSizeEvent& aEvent ) { Refresh(); aEvent.Skip(); } );
    }
};


/// The dark band at the top of the wizard pages: bold title, description, artwork on the right.
class HEADER_BAND : public wxPanel
{
public:
    HEADER_BAND( wxWindow* aParent, const wxString& aTitle, const wxString& aDescription ) :
            wxPanel( aParent, wxID_ANY ),
            m_title( aTitle ),
            m_description( aDescription )
    {
        SetBackgroundStyle( wxBG_STYLE_PAINT );
        SetMinSize( wxSize( -1, FromDIP( 92 ) ) );

        Bind( wxEVT_PAINT, &HEADER_BAND::onPaint, this );
        Bind( wxEVT_SIZE, [this]( wxSizeEvent& aEvent ) { Refresh(); aEvent.Skip(); } );
    }

private:
    void onPaint( wxPaintEvent& )
    {
        wxAutoBufferedPaintDC dc( this );
        wxSize                size = GetClientSize();
        wxColour              bg = mixColour( faceColour(), *wxBLACK, 0.17 );
        wxColour              fg = wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOWTEXT );

        dc.SetBackground( wxBrush( bg ) );
        dc.Clear();

        int artW = std::min( size.x / 2, FromDIP( 360 ) );
        drawBoardArt( dc, wxRect( size.x - artW, 0, artW, size.y ), true );

        int x = FromDIP( 14 );
        int y = FromDIP( 14 );

        wxFont title = GetFont().Bold();
        title.SetFractionalPointSize( title.GetFractionalPointSize() * 1.25 );
        dc.SetFont( title );
        dc.SetTextForeground( fg );
        dc.DrawText( m_title, x, y );
        y += dc.GetTextExtent( m_title ).y + FromDIP( 8 );

        // Description, wrapped to the space left of the artwork
        dc.SetFont( GetFont() );
        int      maxW = size.x - artW - x;
        wxString line;

        for( const wxString& word : wxSplit( m_description, ' ', '\0' ) )
        {
            wxString trial = line.IsEmpty() ? word : line + wxS( " " ) + word;

            if( !line.IsEmpty() && dc.GetTextExtent( trial ).x > maxW )
            {
                dc.DrawText( line, x, y );
                y += dc.GetCharHeight() + FromDIP( 2 );
                line = word;
            }
            else
            {
                line = trial;
            }
        }

        if( !line.IsEmpty() )
            dc.DrawText( line, x, y );
    }

    wxString m_title;
    wxString m_description;
};


class CATEGORY_DATA : public wxTreeItemData
{
public:
    CATEGORY_DATA( int aCat ) : m_cat( aCat ) {}
    int m_cat;
};


wxString scopeText( const RULE& aRule, const CATEGORY& aCat )
{
    SCOPE                 s1, s2;
    std::vector<wxString> hidden;
    ParseScopes( aRule.condition, aCat, s1, s2, hidden );

    wxString text = s1.Describe( 'A' );

    if( aCat.binary )
        text << wxS( "  -  " ) << s2.Describe( 'B' );

    return text;
}

} // anonymous namespace


DIALOG_RULE_WIZARD::DIALOG_RULE_WIZARD( PCB_EDIT_FRAME* aFrame ) :
        DIALOG_SHIM( aFrame, wxID_ANY, _( "New Rule Wizard" ), wxDefaultPosition, wxDefaultSize,
                     wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER ),
        m_frame( aFrame ),
        m_board( aFrame->GetBoard() ),
        m_page( PAGE_INTRO ),
        m_ruleUid( 0 ),
        m_ruleCat( -1 ),
        m_valuesCat( -1 ),
        m_nameEdited( false ),
        m_launchEditor( true ),
        m_book( nullptr ),
        m_name( nullptr ),
        m_comment( nullptr ),
        m_typeTree( nullptr ),
        m_netCombo( nullptr ),
        m_netList( nullptr ),
        m_layerChoice( nullptr ),
        m_refCombo( nullptr ),
        m_query( nullptr ),
        m_valuesPanel( nullptr ),
        m_valuesHeading( nullptr ),
        m_priorityList( nullptr ),
        m_upBtn( nullptr ),
        m_downBtn( nullptr ),
        m_launchCheck( nullptr ),
        m_summary( nullptr )
{
    m_path = m_frame->GetDesignRulesPath();

    // A rules file that exists but can't be read must not be overwritten with just the new rule
    if( !m_path.IsEmpty() && !m_doc.LoadFile( m_path ) )
        m_loadFailed = true;

    if( m_board )
    {
        for( NETINFO_ITEM* net : m_board->GetNetInfo() )
        {
            if( net && net->GetNetCode() > 0 && !net->GetNetname().IsEmpty() )
                m_nets.push_back( net->GetNetname() );
        }

        std::sort( m_nets.begin(), m_nets.end(),
                   []( const wxString& a, const wxString& b ) { return a.CmpNoCase( b ) < 0; } );

        for( PCB_LAYER_ID layer : m_board->GetEnabledLayers().CuStack() )
        {
            m_layerNames.push_back( LSET::Name( layer ) );
            m_layerLabels.push_back( m_board->GetLayerName( layer ) );
        }

        for( FOOTPRINT* fp : m_board->Footprints() )
            m_refs.push_back( fp->GetReference() );

        std::sort( m_refs.begin(), m_refs.end(),
                   []( const wxString& a, const wxString& b ) { return a.CmpNoCase( b ) < 0; } );
    }

    wxBoxSizer* main = new wxBoxSizer( wxVERTICAL );

    m_book = new wxSimplebook( this, wxID_ANY );
    m_book->AddPage( makeIntroPage(), wxEmptyString );
    m_book->AddPage( makeTypePage(), wxEmptyString );
    m_book->AddPage( makeScopePage(), wxEmptyString );
    m_book->AddPage( makeValuesPage(), wxEmptyString );
    m_book->AddPage( makePriorityPage(), wxEmptyString );
    m_book->AddPage( makeDonePage(), wxEmptyString );
    m_book->SetMinSize( FromDIP( wxSize( 700, 470 ) ) );
    main->Add( m_book, 1, wxEXPAND | wxALL, FromDIP( 6 ) );

    wxBoxSizer* buttons = new wxBoxSizer( wxHORIZONTAL );
    m_cancelBtn = new wxButton( this, wxID_CANCEL, _( "Cancel" ) );
    m_backBtn = new wxButton( this, wxID_ANY, _( "&Back" ) );
    m_nextBtn = new wxButton( this, wxID_ANY, _( "&Next" ) );
    m_finishBtn = new wxButton( this, wxID_ANY, _( "Finish" ) );

    buttons->AddStretchSpacer();
    buttons->Add( m_cancelBtn, 0, wxRIGHT, FromDIP( 8 ) );
    buttons->Add( m_backBtn, 0, wxRIGHT, FromDIP( 4 ) );
    buttons->Add( m_nextBtn, 0, wxRIGHT, FromDIP( 8 ) );
    buttons->Add( m_finishBtn, 0 );
    main->Add( buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP( 8 ) );

    SetSizer( main );

    m_backBtn->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { showPage( m_page - 1 ); } );
    m_nextBtn->Bind( wxEVT_BUTTON,
                     [this]( wxCommandEvent& )
                     {
                         if( leavePage( m_page ) )
                             showPage( m_page + 1 );
                     } );
    m_finishBtn->Bind( wxEVT_BUTTON,
                       [this]( wxCommandEvent& )
                       {
                           if( finish() )
                               EndModal( wxID_OK );
                       } );

    showPage( PAGE_INTRO );
    finishDialogSettings();
}


DIALOG_RULE_WIZARD::~DIALOG_RULE_WIZARD() = default;


wxPanel* DIALOG_RULE_WIZARD::makeIntroPage()
{
    wxPanel*    page = new wxPanel( m_book );
    wxBoxSizer* row = new wxBoxSizer( wxHORIZONTAL );

    row->Add( new ART_STRIP( page ), 0, wxEXPAND );

    wxBoxSizer* text = new wxBoxSizer( wxVERTICAL );
    text->AddStretchSpacer( 2 );

    wxStaticText* title = new wxStaticText( page, wxID_ANY, _( "Design Rule Wizard" ) );
    wxFont        big = title->GetFont().Bold();
    big.SetFractionalPointSize( big.GetFractionalPointSize() * 1.6 );
    title->SetFont( big );
    text->Add( title, 0, wxBOTTOM, FromDIP( 18 ) );

    wxStaticText* body = new wxStaticText( page, wxID_ANY,
                                           _( "This wizard will guide you through the steps of "
                                              "creating a new Design Rule" ) );
    wxFont mid = body->GetFont();
    mid.SetFractionalPointSize( mid.GetFractionalPointSize() * 1.25 );
    body->SetFont( mid );
    body->Wrap( FromDIP( 430 ) );
    text->Add( body, 0 );
    text->AddStretchSpacer( 5 );

    row->Add( text, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP( 20 ) );
    page->SetSizer( row );
    return page;
}


wxPanel* DIALOG_RULE_WIZARD::makeHeaderPage( const wxString& aTitle, const wxString& aDescription,
                                             wxBoxSizer*& aBody )
{
    wxPanel*    page = new wxPanel( m_book );
    wxBoxSizer* col = new wxBoxSizer( wxVERTICAL );

    col->Add( new HEADER_BAND( page, aTitle, aDescription ), 0, wxEXPAND );

    aBody = new wxBoxSizer( wxVERTICAL );
    col->Add( aBody, 1, wxEXPAND | wxALL, FromDIP( 12 ) );

    page->SetSizer( col );
    return page;
}


wxPanel* DIALOG_RULE_WIZARD::makeTypePage()
{
    wxBoxSizer* body = nullptr;
    wxPanel*    page = makeHeaderPage( _( "Choose the Rule Type" ),
                                       _( "Choose the type of rule you wish to create below. Give "
                                          "it a name and description." ),
                                       body );

    wxBoxSizer* names = new wxBoxSizer( wxHORIZONTAL );
    names->Add( new wxStaticText( page, wxID_ANY, _( "Name" ) ), 0,
                wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP( 6 ) );
    m_name = new wxTextCtrl( page, wxID_ANY, wxEmptyString, wxDefaultPosition,
                             FromDIP( wxSize( 170, -1 ) ) );
    names->Add( m_name, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP( 10 ) );
    names->Add( new wxStaticText( page, wxID_ANY, _( "Comment" ) ), 0,
                wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP( 6 ) );
    m_comment = new wxTextCtrl( page, wxID_ANY );
    names->Add( m_comment, 1, wxALIGN_CENTER_VERTICAL );
    body->Add( names, 0, wxEXPAND | wxBOTTOM, FromDIP( 10 ) );

    m_typeTree = new wxTreeCtrl( page, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                 wxTR_HAS_BUTTONS | wxTR_HIDE_ROOT | wxTR_SINGLE
                                         | wxTR_FULL_ROW_HIGHLIGHT | wxTR_NO_LINES );
    wxTreeItemId root = m_typeTree->AddRoot( wxS( "rules" ) );
    wxTreeItemId first;

    for( const wxString& group : Groups() )
    {
        wxTreeItemId groupItem;

        for( int i = 0; i < (int) Categories().size(); ++i )
        {
            const CATEGORY& c = Categories()[i];

            // Only the rule types KiCad can check
            if( c.group != group || !c.supported || i == CustomCategory() )
                continue;

            if( !groupItem.IsOk() )
            {
                groupItem = m_typeTree->AppendItem( root, group );
                m_typeTree->SetItemBold( groupItem );
            }

            wxTreeItemId item = m_typeTree->AppendItem( groupItem, c.name, -1, -1,
                                                        new CATEGORY_DATA( i ) );

            if( !first.IsOk() )
                first = item;
        }

        if( groupItem.IsOk() )
            m_typeTree->Expand( groupItem );
    }

    body->Add( m_typeTree, 1, wxEXPAND );

    if( first.IsOk() )
    {
        m_typeTree->SelectItem( first );
        m_typeTree->EnsureVisible( first );
    }

    auto updateName =
            [this]()
            {
                int cat = selectedCategory();

                if( cat >= 0 && !m_nameEdited )
                    m_name->ChangeValue( m_doc.UniqueName( Categories()[cat].defaultRuleName ) );
            };

    m_typeTree->Bind( wxEVT_TREE_SEL_CHANGED, [updateName]( wxTreeEvent& ) { updateName(); } );
    m_typeTree->Bind( wxEVT_TREE_ITEM_ACTIVATED,
                      [this]( wxTreeEvent& )
                      {
                          if( selectedCategory() >= 0 && leavePage( m_page ) )
                              showPage( m_page + 1 );
                      } );
    m_name->Bind( wxEVT_TEXT, [this]( wxCommandEvent& ) { m_nameEdited = true; } );

    updateName();
    return page;
}


wxPanel* DIALOG_RULE_WIZARD::makeScopePage()
{
    wxBoxSizer* body = nullptr;
    wxPanel*    page = makeHeaderPage( _( "Choose the Rule Scope" ),
                                       _( "Choose from the options below the one that closest "
                                          "describes the objects that you wish this rule to apply "
                                          "to. You will be able to refine this choice later." ),
                                       body );

    wxStaticText* heading = new wxStaticText( page, wxID_ANY,
                                              _( "What this rule should apply to" ) );
    heading->SetFont( heading->GetFont().Bold() );
    body->Add( heading, 0, wxBOTTOM, FromDIP( 8 ) );

    wxBoxSizer* row = new wxBoxSizer( wxHORIZONTAL );
    wxBoxSizer* radios = new wxBoxSizer( wxVERTICAL );

    const wxString labels[] = { _( "Whole Board" ),
                                _( "1 Net" ),
                                _( "A Few Nets" ),
                                _( "A Net on a Particular Layer" ),
                                _( "A Net in a Particular Component" ),
                                _( "Advanced (Start With a Blank Query)" ) };

    for( int i = 0; i < 6; ++i )
    {
        wxRadioButton* rb = new wxRadioButton( page, wxID_ANY, labels[i], wxDefaultPosition,
                                               wxDefaultSize, i == 0 ? wxRB_GROUP : 0 );
        radios->Add( rb, 0, wxBOTTOM, FromDIP( 8 ) );
        m_scopeRadios.push_back( rb );
        rb->Bind( wxEVT_RADIOBUTTON, [this]( wxCommandEvent& ) { updateScopeControls(); } );
    }

    m_scopeRadios[0]->SetValue( true );
    row->Add( radios, 0, wxRIGHT, FromDIP( 24 ) );

    // The details of the chosen scope
    wxFlexGridSizer* grid = new wxFlexGridSizer( 2, FromDIP( 6 ), FromDIP( 8 ) );
    grid->AddGrowableCol( 1 );
    grid->AddGrowableRow( 1 );

    wxArrayString nets;

    for( const wxString& n : m_nets )
        nets.Add( n );

    wxArrayString refs;

    for( const wxString& r : m_refs )
        refs.Add( r );

    wxArrayString layers;

    for( const wxString& l : m_layerLabels )
        layers.Add( l );

    m_netLabel = new wxStaticText( page, wxID_ANY, _( "Net" ) );
    m_netCombo = new wxComboBox( page, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                 nets );
    m_netCombo->AutoComplete( nets );
    grid->Add( m_netLabel, 0, wxALIGN_CENTER_VERTICAL );
    grid->Add( m_netCombo, 1, wxEXPAND );

    m_netsLabel = new wxStaticText( page, wxID_ANY, _( "Nets" ) );
    m_netList = new wxCheckListBox( page, wxID_ANY, wxDefaultPosition,
                                    FromDIP( wxSize( -1, 140 ) ), nets );
    grid->Add( m_netsLabel, 0, wxALIGN_TOP );
    grid->Add( m_netList, 1, wxEXPAND );

    m_layerLabel = new wxStaticText( page, wxID_ANY, _( "Layer" ) );
    m_layerChoice = new wxChoice( page, wxID_ANY, wxDefaultPosition, wxDefaultSize, layers );

    if( !layers.IsEmpty() )
        m_layerChoice->SetSelection( 0 );

    grid->Add( m_layerLabel, 0, wxALIGN_CENTER_VERTICAL );
    grid->Add( m_layerChoice, 1, wxEXPAND );

    m_refLabel = new wxStaticText( page, wxID_ANY, _( "Component" ) );
    m_refCombo = new wxComboBox( page, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                 refs );
    m_refCombo->AutoComplete( refs );
    grid->Add( m_refLabel, 0, wxALIGN_CENTER_VERTICAL );
    grid->Add( m_refCombo, 1, wxEXPAND );

    m_queryLabel = new wxStaticText( page, wxID_ANY, _( "Query" ) );
    m_query = new wxTextCtrl( page, wxID_ANY, wxEmptyString, wxDefaultPosition,
                              FromDIP( wxSize( -1, 60 ) ), wxTE_MULTILINE );
    m_query->SetHint( wxS( "A.NetClass == 'Power'" ) );
    grid->Add( m_queryLabel, 0, wxALIGN_TOP );
    grid->Add( m_query, 1, wxEXPAND );

    row->Add( grid, 1, wxEXPAND );
    body->Add( row, 1, wxEXPAND );

    updateScopeControls();
    return page;
}


void DIALOG_RULE_WIZARD::updateScopeControls()
{
    int k = 0;

    for( size_t i = 0; i < m_scopeRadios.size(); ++i )
    {
        if( m_scopeRadios[i]->GetValue() )
            k = (int) i;
    }

    bool net = k == 1 || k == 3 || k == 4;
    bool nets = k == 2;
    bool layer = k == 3;
    bool ref = k == 4;
    bool query = k == 5;

    m_netLabel->Show( net );
    m_netCombo->Show( net );
    m_netsLabel->Show( nets );
    m_netList->Show( nets );
    m_layerLabel->Show( layer );
    m_layerChoice->Show( layer );
    m_refLabel->Show( ref );
    m_refCombo->Show( ref );
    m_queryLabel->Show( query );
    m_query->Show( query );

    if( wxWindow* page = m_netCombo->GetParent() )
        page->Layout();
}


SCOPE DIALOG_RULE_WIZARD::currentScope( wxString& aError ) const
{
    int k = 0;

    for( size_t i = 0; i < m_scopeRadios.size(); ++i )
    {
        if( m_scopeRadios[i]->GetValue() )
            k = (int) i;
    }

    auto quoted = []( const wxString& aText ) { return aText.Find( '\'' ) == wxNOT_FOUND; };

    SCOPE    s;
    wxString net = m_netCombo->GetValue().Trim().Trim( false );

    switch( k )
    {
    case 0:
        s.kind = SCOPE_KIND::ALL;
        break;

    case 1:
        if( net.IsEmpty() || !quoted( net ) )
            aError = _( "Choose the net the rule applies to." );

        s.kind = SCOPE_KIND::NET;
        s.net = net;
        break;

    case 2:
    {
        wxArrayInt checked;
        m_netList->GetCheckedItems( checked );

        if( checked.IsEmpty() )
        {
            aError = _( "Tick the nets the rule applies to." );
            break;
        }

        if( checked.size() == 1 )
        {
            s.kind = SCOPE_KIND::NET;
            s.net = m_netList->GetString( checked[0] );
            break;
        }

        s.kind = SCOPE_KIND::CUSTOM;

        for( int idx : checked )
        {
            // A quote in a net name would end the rule's string early
            if( !quoted( m_netList->GetString( idx ) ) )
            {
                aError = wxString::Format( _( "Net '%s' has a quote in its name and can't be used "
                                              "in a rule." ),
                                           m_netList->GetString( idx ) );
                s.query.clear();
                break;
            }

            if( !s.query.IsEmpty() )
                s.query << wxS( " || " );

            s.query << wxS( "A.NetName == '" ) << m_netList->GetString( idx ) << wxS( "'" );
        }

        break;
    }

    case 3:
    {
        int sel = m_layerChoice->GetSelection();

        if( net.IsEmpty() || !quoted( net ) || sel < 0 )
            aError = _( "Choose the net and the layer the rule applies to." );

        s.kind = SCOPE_KIND::NET_AND_LAYER;
        s.net = net;
        s.layer = sel >= 0 ? m_layerNames[sel] : wxString();
        break;
    }

    case 4:
    {
        wxString ref = m_refCombo->GetValue().Trim().Trim( false );

        if( ref.IsEmpty() || !quoted( ref ) || !quoted( net ) )
        {
            aError = _( "Choose the component (and the net) the rule applies to." );
            break;
        }

        s.kind = SCOPE_KIND::CUSTOM;

        if( !net.IsEmpty() )
            s.query << wxS( "A.NetName == '" ) << net << wxS( "' && " );

        s.query << wxS( "A.memberOfFootprint('" ) << ref << wxS( "')" );
        break;
    }

    default:
        s.kind = SCOPE_KIND::CUSTOM;
        s.query = m_query->GetValue().Trim().Trim( false );
        s.query.Replace( wxS( "\n" ), wxS( " " ) );

        if( s.query.IsEmpty() )
            aError = _( "Enter the query that selects the objects the rule applies to." );

        break;
    }

    return s;
}


wxPanel* DIALOG_RULE_WIZARD::makeValuesPage()
{
    wxBoxSizer* body = nullptr;
    wxPanel*    page = makeHeaderPage( _( "Choose the Rule Constraints" ),
                                       _( "Set the values this rule enforces. Leave a value blank "
                                          "if the rule should not constrain it." ),
                                       body );

    m_valuesHeading = new wxStaticText( page, wxID_ANY, wxEmptyString );
    m_valuesHeading->SetFont( m_valuesHeading->GetFont().Bold() );
    body->Add( m_valuesHeading, 0, wxBOTTOM, FromDIP( 8 ) );

    m_valuesPanel = new wxPanel( page );
    body->Add( m_valuesPanel, 1, wxEXPAND );
    return page;
}


void DIALOG_RULE_WIZARD::buildValueControls()
{
    int cat = selectedCategory();

    if( cat < 0 || cat == m_valuesCat )
        return;

    m_valuesCat = cat;
    m_fieldCtrls.clear();
    m_valuesPanel->DestroyChildren();

    const CATEGORY& c = Categories()[cat];
    m_valuesHeading->SetLabel( c.name );

    // Starting values: what the rules editor would give a new rule of this type
    RULE seed;
    m_doc.SeedRule( seed, cat, m_board );

    wxFlexGridSizer* grid = new wxFlexGridSizer( 3, FromDIP( 6 ), FromDIP( 8 ) );
    wxWindow*        p = m_valuesPanel;

    for( int i = 0; i < (int) c.fields.size(); ++i )
    {
        const FIELD& f = c.fields[i];
        FIELD_VALUE  initial = m_doc.GetFieldValue( seed, cat, i );
        FIELD_CTRL   fc;
        fc.field = i;

        switch( f.kind )
        {
        case FIELD_KIND::LENGTH:
        {
            wxStaticText* label = new wxStaticText( p, wxID_ANY, f.label );
            fc.text = new wxTextCtrl( p, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                      FromDIP( wxSize( 130, -1 ) ) );
            wxStaticText* units = new wxStaticText( p, wxID_ANY, wxEmptyString );
            fc.binder = std::make_unique<UNIT_BINDER>( m_frame, label, fc.text, units );

            if( initial.set )
                fc.binder->SetValue( initial.num );
            else
                fc.binder->SetNull();

            grid->Add( label, 0, wxALIGN_CENTER_VERTICAL );
            grid->Add( fc.text, 0, wxEXPAND );
            grid->Add( units, 0, wxALIGN_CENTER_VERTICAL );
            break;
        }

        case FIELD_KIND::COUNT:
            grid->Add( new wxStaticText( p, wxID_ANY, f.label ), 0, wxALIGN_CENTER_VERTICAL );
            fc.text = new wxTextCtrl( p, wxID_ANY,
                                      initial.set ? wxString::Format( wxS( "%lld" ), initial.num )
                                                  : wxString(),
                                      wxDefaultPosition, FromDIP( wxSize( 130, -1 ) ) );
            grid->Add( fc.text, 0, wxEXPAND );
            grid->AddSpacer( 0 );
            break;

        case FIELD_KIND::CONNECT_STYLE:
        {
            grid->Add( new wxStaticText( p, wxID_ANY, f.label ), 0, wxALIGN_CENTER_VERTICAL );
            fc.choice = new wxChoice( p, wxID_ANY );
            fc.choice->Append( _( "(not set)" ) );
            fc.choice->Append( _( "Relief Connect" ) );
            fc.choice->Append( _( "Direct Connect" ) );
            fc.choice->Append( _( "No Connect" ) );
            fc.choice->SetSelection( !initial.set ? 0 : initial.num == 1 ? 1
                                                        : initial.num == 0 ? 2 : 3 );
            grid->Add( fc.choice, 0, wxEXPAND );
            grid->AddSpacer( 0 );
            break;
        }

        case FIELD_KIND::FLAG:
            grid->AddSpacer( 0 );
            fc.check = new wxCheckBox( p, wxID_ANY, f.label );
            fc.check->SetValue( initial.set && initial.num );
            grid->Add( fc.check, 0 );
            grid->AddSpacer( 0 );
            break;

        case FIELD_KIND::ORIENTATION:
        case FIELD_KIND::SIDE_LAYERS:
        {
            grid->Add( new wxStaticText( p, wxID_ANY, f.label ), 0, wxALIGN_TOP );
            wxBoxSizer* col = new wxBoxSizer( wxVERTICAL );
            std::vector<wxString> labels;

            if( f.kind == FIELD_KIND::ORIENTATION )
                labels = { _( "0 Degrees" ), _( "90 Degrees" ), _( "180 Degrees" ),
                           _( "270 Degrees" ) };
            else
                labels = { _( "Top Layer" ), _( "Bottom Layer" ) };

            long long mask = initial.set ? initial.num : ( 1LL << labels.size() ) - 1;

            for( size_t b = 0; b < labels.size(); ++b )
            {
                wxCheckBox* cb = new wxCheckBox( p, wxID_ANY, labels[b] );
                cb->SetValue( mask & ( 1LL << b ) );
                col->Add( cb, 0, wxBOTTOM, FromDIP( 3 ) );
                fc.checks.push_back( cb );
            }

            grid->Add( col, 0 );
            grid->AddSpacer( 0 );
            break;
        }
        }

        m_fieldCtrls.push_back( std::move( fc ) );
    }

    wxBoxSizer* outer = new wxBoxSizer( wxVERTICAL );
    outer->Add( grid, 0, wxLEFT, FromDIP( 8 ) );

    if( !c.note.IsEmpty() )
    {
        wxStaticText* note = new wxStaticText( p, wxID_ANY, c.note );
        note->Wrap( FromDIP( 600 ) );
        outer->Add( note, 0, wxTOP | wxLEFT, FromDIP( 12 ) );
    }

    m_valuesPanel->SetSizer( outer, true );
    m_valuesPanel->Layout();
    m_valuesPanel->GetParent()->Layout();
}


bool DIALOG_RULE_WIZARD::buildRule( wxString& aError )
{
    int cat = selectedCategory();

    if( cat < 0 )
    {
        aError = _( "Choose a rule type." );
        return false;
    }

    const CATEGORY& c = Categories()[cat];

    SCOPE scope = currentScope( aError );

    if( !aError.IsEmpty() )
        return false;

    RULE rule;
    rule.name = m_name->GetValue().Trim().Trim( false );
    rule.comment = m_comment->GetValue().Trim().Trim( false );
    m_doc.SeedRule( rule, cat, m_board );

    // Values from the value page
    for( FIELD_CTRL& fc : m_fieldCtrls )
    {
        const FIELD& f = c.fields[fc.field];
        FIELD_VALUE  v;

        switch( f.kind )
        {
        case FIELD_KIND::LENGTH:
            if( !fc.binder->IsNull() )
            {
                v.set = true;
                v.num = fc.binder->GetValue();
            }

            break;

        case FIELD_KIND::COUNT:
        {
            wxString text = fc.text->GetValue().Trim().Trim( false );

            if( !text.IsEmpty() )
            {
                long n = 0;

                if( !text.ToLong( &n ) || n < 0 )
                {
                    aError = wxString::Format( _( "'%s' must be a whole number." ), f.label );
                    return false;
                }

                v.set = true;
                v.num = n;
            }

            break;
        }

        case FIELD_KIND::CONNECT_STYLE:
        {
            int sel = fc.choice->GetSelection();

            if( sel > 0 )
            {
                v.set = true;
                v.num = sel == 1 ? 1 : sel == 2 ? 0 : 2;
            }

            break;
        }

        case FIELD_KIND::FLAG:
            v.set = true;
            v.num = fc.check->GetValue() ? 1 : 0;
            break;

        case FIELD_KIND::ORIENTATION:
        case FIELD_KIND::SIDE_LAYERS:
            v.set = true;

            for( size_t b = 0; b < fc.checks.size(); ++b )
            {
                if( fc.checks[b]->GetValue() )
                    v.num |= ( 1LL << b );
            }

            if( !v.num )
            {
                aError = wxString::Format( _( "Select at least one entry under '%s'." ), f.label );
                return false;
            }

            break;
        }

        m_doc.SetFieldValue( rule, cat, fc.field, v );
    }

    if( rule.constraints.empty() )
    {
        aError = _( "Set at least one value for the rule." );
        return false;
    }

    // Scope: the category's own terms (e.g. "vias only") plus the chosen objects
    SCOPE                 s1, s2;
    std::vector<wxString> hidden;
    ParseScopes( rule.condition, c, s1, s2, hidden );
    rule.condition = BuildCondition( hidden, scope, SCOPE(), c.binary );
    rule.modified = true;

    wxString syntax = CheckRuleSyntax( rule.Generate() );

    if( !syntax.IsEmpty() )
    {
        aError = _( "The rule is not valid:" ) + wxS( "\n" ) + syntax;
        return false;
    }

    // Keep the place it was given on the priority page when only the contents changed
    if( m_ruleUid && m_ruleCat == cat )
    {
        if( RULE* existing = m_doc.FindRule( m_ruleUid ) )
        {
            rule.uid = existing->uid;
            rule.enabled = existing->enabled;
            *existing = rule;
            return true;
        }
    }

    if( m_ruleUid )
        m_doc.RemoveFromCategory( m_ruleUid, m_ruleCat );

    m_ruleUid = m_doc.AddRule( rule );
    m_ruleCat = cat;
    return true;
}


wxPanel* DIALOG_RULE_WIZARD::makePriorityPage()
{
    wxBoxSizer* body = nullptr;
    wxPanel*    page = makeHeaderPage( _( "Choose the Rule Priority" ),
                                       _( "Set the rule priority in the list below. Priority one is "
                                          "highest. The rule with the highest priority whose scope "
                                          "matches the checked objects will be used." ),
                                       body );

    m_priorityList = new wxDataViewListCtrl( page, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                             wxDV_SINGLE | wxDV_ROW_LINES );
    m_priorityList->AppendTextColumn( _( "Priority" ), wxDATAVIEW_CELL_INERT, FromDIP( 64 ) );
    m_priorityList->AppendToggleColumn( _( "Enabled" ), wxDATAVIEW_CELL_ACTIVATABLE,
                                        FromDIP( 64 ), wxALIGN_CENTER );
    m_priorityList->AppendTextColumn( _( "Name" ), wxDATAVIEW_CELL_INERT, FromDIP( 150 ) );
    m_priorityList->AppendTextColumn( _( "Scope" ), wxDATAVIEW_CELL_INERT, FromDIP( 150 ) );
    m_priorityList->AppendTextColumn( _( "Attributes" ), wxDATAVIEW_CELL_INERT, FromDIP( 260 ) );
    body->Add( m_priorityList, 1, wxEXPAND | wxBOTTOM, FromDIP( 8 ) );

    wxBoxSizer* buttons = new wxBoxSizer( wxHORIZONTAL );
    m_upBtn = new wxButton( page, wxID_ANY, _( "&Increase Priority" ) );
    m_downBtn = new wxButton( page, wxID_ANY, _( "&Decrease Priority" ) );
    buttons->Add( m_upBtn, 0, wxRIGHT, FromDIP( 8 ) );
    buttons->Add( m_downBtn, 0 );
    body->Add( buttons, 0 );

    m_upBtn->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { movePriority( true ); } );
    m_downBtn->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { movePriority( false ); } );

    auto updateButtons =
            [this]()
            {
                int row = m_priorityList->GetSelectedRow();
                m_upBtn->Enable( row > 0 );
                m_downBtn->Enable( row != wxNOT_FOUND
                                   && row + 1 < (int) m_priorityUids.size() );
            };

    m_priorityList->Bind( wxEVT_DATAVIEW_SELECTION_CHANGED,
                          [updateButtons]( wxDataViewEvent& ) { updateButtons(); } );

    m_priorityList->Bind( wxEVT_DATAVIEW_ITEM_VALUE_CHANGED,
                          [this]( wxDataViewEvent& aEvent )
                          {
                              int row = m_priorityList->ItemToRow( aEvent.GetItem() );

                              if( aEvent.GetColumn() != 1 || row < 0
                                  || row >= (int) m_priorityUids.size() )
                              {
                                  return;
                              }

                              if( RULE* r = m_doc.FindRule( m_priorityUids[row] ) )
                              {
                                  bool on = m_priorityList->GetToggleValue( row, 1 );

                                  if( r->enabled != on )
                                  {
                                      r->enabled = on;
                                      r->modified = true;
                                  }
                              }
                          } );

    return page;
}


void DIALOG_RULE_WIZARD::fillPriorityList()
{
    m_priorityList->DeleteAllItems();
    m_priorityUids.clear();

    if( m_ruleCat < 0 )
        return;

    const CATEGORY& c = Categories()[m_ruleCat];
    std::vector<RULE*> rules = m_doc.RulesInCategory( m_ruleCat );
    int selRow = -1;

    for( size_t i = 0; i < rules.size(); ++i )
    {
        RULE* r = rules[i];

        wxVector<wxVariant> row;
        row.push_back( wxVariant( wxString::Format( wxS( "%zu" ), i + 1 ) ) );
        row.push_back( wxVariant( r->enabled ) );
        row.push_back( wxVariant( r->name ) );
        row.push_back( wxVariant( scopeText( *r, c ) ) );
        row.push_back( wxVariant( m_doc.Attributes( *r, m_ruleCat, m_frame->GetUserUnits() ) ) );
        m_priorityList->AppendItem( row );
        m_priorityUids.push_back( r->uid );

        if( r->uid == m_ruleUid )
            selRow = (int) i;
    }

    if( selRow >= 0 )
    {
        m_priorityList->SelectRow( selRow );
        m_priorityList->EnsureVisible( m_priorityList->RowToItem( selRow ) );
    }

    int row = m_priorityList->GetSelectedRow();
    m_upBtn->Enable( row > 0 );
    m_downBtn->Enable( row != wxNOT_FOUND && row + 1 < (int) m_priorityUids.size() );
}


void DIALOG_RULE_WIZARD::movePriority( bool aUp )
{
    int row = m_priorityList->GetSelectedRow();

    if( row == wxNOT_FOUND || row >= (int) m_priorityUids.size() )
        return;

    int uid = m_priorityUids[row];

    if( !m_doc.MovePriority( m_ruleCat, uid, aUp ) )
        return;

    fillPriorityList();

    for( size_t i = 0; i < m_priorityUids.size(); ++i )
    {
        if( m_priorityUids[i] == uid )
        {
            m_priorityList->SelectRow( (int) i );
            m_upBtn->Enable( i > 0 );
            m_downBtn->Enable( i + 1 < m_priorityUids.size() );
        }
    }
}


wxPanel* DIALOG_RULE_WIZARD::makeDonePage()
{
    wxBoxSizer* body = nullptr;
    wxPanel*    page = makeHeaderPage( _( "The New Rule is Complete" ),
                                       _( "This new rule is complete. Check the box below to edit "
                                          "the rule in the main design rule editor dialog." ),
                                       body );

    m_launchCheck = new wxCheckBox( page, wxID_ANY, _( "Launch main design rules dialog" ) );
    m_launchCheck->SetValue( true );
    body->Add( m_launchCheck, 0, wxBOTTOM, FromDIP( 14 ) );

    m_summary = new wxStaticText( page, wxID_ANY, wxEmptyString );
    wxFont mid = m_summary->GetFont();
    mid.SetFractionalPointSize( mid.GetFractionalPointSize() * 1.15 );
    m_summary->SetFont( mid );
    body->Add( m_summary, 1, wxEXPAND );
    return page;
}


void DIALOG_RULE_WIZARD::updateSummary()
{
    const RULE* rule = m_doc.FindRule( m_ruleUid );

    if( !rule || m_ruleCat < 0 )
    {
        m_summary->SetLabel( wxEmptyString );
        return;
    }

    const CATEGORY& c = Categories()[m_ruleCat];
    wxString        text;

    text << c.name << wxS( "\n" )
         << m_doc.Attributes( *rule, m_ruleCat, m_frame->GetUserUnits() ) << wxS( "\n\n" )
         << scopeText( *rule, c );

    m_summary->SetLabel( text );
    m_summary->Wrap( FromDIP( 640 ) );
}


int DIALOG_RULE_WIZARD::selectedCategory() const
{
    if( !m_typeTree )
        return -1;

    wxTreeItemId item = m_typeTree->GetSelection();

    if( !item.IsOk() )
        return -1;

    if( CATEGORY_DATA* data = dynamic_cast<CATEGORY_DATA*>( m_typeTree->GetItemData( item ) ) )
        return data->m_cat;

    return -1;
}


bool DIALOG_RULE_WIZARD::leavePage( int aPage )
{
    wxString error;

    switch( aPage )
    {
    case PAGE_TYPE:
    {
        if( selectedCategory() < 0 )
        {
            DisplayError( this, _( "Choose the type of rule to create." ) );
            return false;
        }

        wxString name = m_name->GetValue().Trim().Trim( false );

        if( name.IsEmpty() || name.Find( '"' ) != wxNOT_FOUND )
        {
            DisplayError( this, _( "Enter a name for the rule (without double quotes)." ) );
            return false;
        }

        // The wizard's own rule may already carry this name from an earlier pass
        if( RULE* other = m_doc.FindRuleByName( name ) )
        {
            if( other->uid != m_ruleUid )
            {
                DisplayError( this, wxString::Format( _( "A rule named '%s' already exists." ),
                                                      name ) );
                return false;
            }
        }

        buildValueControls();
        return true;
    }

    case PAGE_SCOPE:
        currentScope( error );
        break;

    case PAGE_VALUES:
        buildRule( error );
        break;

    default:
        break;
    }

    if( !error.IsEmpty() )
    {
        DisplayError( this, error );
        return false;
    }

    return true;
}


void DIALOG_RULE_WIZARD::showPage( int aPage )
{
    aPage = std::clamp( aPage, 0, PAGE_COUNT - 1 );
    m_page = aPage;

    if( aPage == PAGE_VALUES )
        buildValueControls();
    else if( aPage == PAGE_PRIORITY )
        fillPriorityList();
    else if( aPage == PAGE_DONE )
        updateSummary();

    m_book->ChangeSelection( aPage );

    m_backBtn->Enable( aPage > PAGE_INTRO );
    m_nextBtn->Enable( aPage < PAGE_DONE );
    m_finishBtn->Enable( aPage == PAGE_DONE );

    wxButton* def = aPage == PAGE_DONE ? m_finishBtn : m_nextBtn;
    def->SetDefault();
    def->SetFocus();

    if( aPage == PAGE_TYPE && m_typeTree )
        m_typeTree->SetFocus();
}


bool DIALOG_RULE_WIZARD::finish()
{
    if( !m_doc.FindRule( m_ruleUid ) )
        return false;

    if( m_path.IsEmpty() )
    {
        DisplayError( this, _( "Save the board first; rules are stored next to it in the "
                               "project's .kicad_dru file." ) );
        return false;
    }

    if( m_frame->Prj().IsReadOnly() )
    {
        DisplayError( this, _( "The project is read-only; the rule cannot be saved." ) );
        return false;
    }

    if( m_loadFailed )
    {
        DisplayError( this, wxString::Format( _( "The design rules file '%s' could not be read, "
                                                 "so the rule was not saved (saving would replace "
                                                 "the existing rules). Fix or remove the file "
                                                 "and try again." ),
                                              m_path ) );
        return false;
    }

    if( !m_doc.SaveFile( m_path ) )
    {
        DisplayError( this, wxString::Format( _( "Could not write '%s'." ), m_path ) );
        return false;
    }

    // Use the new rule straight away
    BOARD_DESIGN_SETTINGS& bds = m_board->GetDesignSettings();

    if( bds.m_DRCEngine )
    {
        try
        {
            bds.m_DRCEngine->InitEngine( wxFileName( m_path ) );
        }
        catch( PARSE_ERROR& pe )
        {
            DisplayErrorMessage( this, _( "The rule was saved, but the rules file has an error "
                                          "and DRC will report it." ),
                                 pe.What() );
        }
    }

    m_launchEditor = m_launchCheck->GetValue();
    return true;
}
