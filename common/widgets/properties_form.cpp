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

#include <widgets/properties_form.h>

#include <properties/pg_properties.h>
#include <properties/eda_angle_variant.h>
#include <properties/std_optional_variants.h>
#include <eda_draw_frame.h>
#include <class_draw_panel_gal.h>
#include <origin_transforms.h>

#include <wx/control.h>
#include <wx/dcbuffer.h>
#include <wx/menu.h>
#include <wx/propgrid/propgrid.h>
#include <wx/settings.h>
#include <wx/textctrl.h>

#include <algorithm>


enum class FORM_KIND
{
    TEXT,
    CHOICE,
    CHECK,
    CUSTOM_CHECK,
    SEGMENTED,
    CUSTOM_SEGMENTED,
    XY,
    TWIN,
    INFO,
    CUSTOM_CHOICE,
    TITLE,
    SEPARATOR,
    LINK,
    PAINT,
    BUTTONS,
    TABLE
};


struct PROPERTIES_FORM::ROW
{
    FORM_KIND                                                kind = FORM_KIND::TEXT;
    wxString                                                 prop;
    wxString                                                 prop2;
    wxString                                                 lock;
    wxString                                                 label;
    wxString                                                 label2;
    OPTS                                                     opts;
    bool                                                     boxFirst = false;
    bool                                                     boxed = false;
    std::vector<wxString>                                    labels;
    std::vector<long>                                        values;
    std::vector<bool>                                        enabled;
    std::function<int()>                                     getInt;
    std::function<void( int )>                               setInt;
    std::function<void( bool )>                              setBool;
    std::function<wxString()>                                text;
    std::function<void( const wxString& )>                   editText;   ///< AddCustomText
    std::function<void()>                                    click;
    std::function<void( wxDC&, const wxRect& )>              paint;
    int                                                      heightDIP = 0;
    std::vector<std::pair<wxString, std::function<void()>>>  buttons;
    std::vector<int>                                         weights;
    std::function<TABLE_ROWS()>                              table;
    int*                                                     selected = nullptr;
    TABLE_ROWS                                               cells;
    std::vector<std::vector<wxRect>>                         cellRects;

    // Layout (window coordinates), recomputed before painting and hit testing
    wxRect              rect;
    wxRect              labelRect;
    std::vector<wxRect> hits;
};


namespace
{
const wxColour ON_FILL( 0x5A, 0x7C, 0xA0 );
const wxColour ON_TEXT( 0xF2, 0xF2, 0xF2 );
}


wxColour PROPERTIES_FORM::Mix( const wxColour& a, const wxColour& b, double t )
{
    return wxColour( a.Red() + ( b.Red() - a.Red() ) * t, a.Green() + ( b.Green() - a.Green() ) * t,
                     a.Blue() + ( b.Blue() - a.Blue() ) * t );
}


wxColour PROPERTIES_FORM::LinkColour()
{
    // Altium's link blue, readable on the dark panel greys
    return wxSystemSettings::GetAppearance().IsDark() ? wxColour( 0x6C, 0xAE, 0xEA )
                                                       : wxColour( 0x1E, 0x64, 0xB4 );
}


PROPERTIES_FORM::PROPERTIES_FORM( wxWindow* aParent, PROPERTIES_FORM_HOST* aHost ) :
        wxPanel( aParent ),
        m_host( aHost )
{
    SetBackgroundStyle( wxBG_STYLE_PAINT );

    Bind( wxEVT_PAINT, &PROPERTIES_FORM::onPaint, this );
    Bind( wxEVT_LEFT_DOWN, &PROPERTIES_FORM::onLeftDown, this );
    Bind( wxEVT_LEFT_DCLICK, &PROPERTIES_FORM::onLeftDown, this );
    Bind( wxEVT_MOTION, &PROPERTIES_FORM::onMotion, this );
    Bind( wxEVT_LEAVE_WINDOW,
          [this]( wxMouseEvent& )
          {
              if( m_hoverRow >= 0 )
              {
                  m_hoverRow = -1;
                  Refresh();
              }
          } );
    Bind( wxEVT_SIZE,
          [this]( wxSizeEvent& aEvent )
          {
              aEvent.Skip();

              // Only wrapping button rows depend on the width; never re-enter a layout
              int height = contentHeight( std::max( 1, GetClientSize().x ) );

              if( height != m_height && height > 0 )
              {
                  m_height = height;
                  SetMinSize( wxSize( -1, height ) );
                  CallAfter( [this]() { m_host->FormLayoutChanged(); } );
              }

              Refresh();
          } );
}


PROPERTIES_FORM::~PROPERTIES_FORM()
{
    for( ROW* row : m_rows )
        delete row;
}


wxPropertyGrid* PROPERTIES_FORM::grid() const
{
    return m_host->FormGrid();
}


wxPGProperty* PROPERTIES_FORM::prop( const wxString& aName ) const
{
    if( aName.IsEmpty() || !grid() )
        return nullptr;

    return grid()->GetPropertyByName( aName );
}


// ------------------------------------------------------------------------------------- builders

PROPERTIES_FORM::ROW& PROPERTIES_FORM::AddText( const wxString& aProp, const wxString& aLabel,
                                                const OPTS& aOpts )
{
    ROW* row = new ROW();
    row->kind = FORM_KIND::TEXT;
    row->prop = aProp;
    row->label = aLabel;
    row->opts = aOpts;
    m_rows.push_back( row );
    return *row;
}


PROPERTIES_FORM::ROW& PROPERTIES_FORM::AddChoice( const wxString& aProp, const wxString& aLabel,
                                                  const OPTS& aOpts )
{
    ROW& row = AddText( aProp, aLabel, aOpts );
    row.kind = FORM_KIND::CHOICE;
    return row;
}


PROPERTIES_FORM::ROW& PROPERTIES_FORM::AddCheck( const wxString& aProp, const wxString& aLabel,
                                                 bool aBoxFirst, const OPTS& aOpts )
{
    ROW& row = AddText( aProp, aLabel, aOpts );
    row.kind = FORM_KIND::CHECK;
    row.boxFirst = aBoxFirst;
    return row;
}


PROPERTIES_FORM::ROW& PROPERTIES_FORM::AddCustomCheck( const wxString& aLabel, std::function<int()> aGet,
                                                       std::function<void( bool )> aSet, bool aBoxFirst,
                                                       const OPTS& aOpts )
{
    ROW& row = AddText( wxEmptyString, aLabel, aOpts );
    row.kind = FORM_KIND::CUSTOM_CHECK;
    row.getInt = std::move( aGet );
    row.setBool = std::move( aSet );
    row.boxFirst = aBoxFirst;
    return row;
}


PROPERTIES_FORM::ROW& PROPERTIES_FORM::AddSegmented( const wxString& aProp, std::vector<wxString> aLabels,
                                                     std::vector<long> aValues, const OPTS& aOpts )
{
    ROW& row = AddText( aProp, wxEmptyString, aOpts );
    row.kind = FORM_KIND::SEGMENTED;
    row.labels = std::move( aLabels );
    row.values = std::move( aValues );
    return row;
}


PROPERTIES_FORM::ROW& PROPERTIES_FORM::AddCustomSegmented( std::vector<wxString> aLabels,
                                                           std::function<int()> aGet,
                                                           std::function<void( int )> aSet,
                                                           std::vector<bool> aEnabled,
                                                           const wxString& aLabel, const OPTS& aOpts )
{
    ROW& row = AddText( wxEmptyString, aLabel, aOpts );
    row.kind = FORM_KIND::CUSTOM_SEGMENTED;
    row.labels = std::move( aLabels );
    row.getInt = std::move( aGet );
    row.setInt = std::move( aSet );
    row.enabled = std::move( aEnabled );
    return row;
}


PROPERTIES_FORM::ROW& PROPERTIES_FORM::AddXY( const wxString& aPropX, const wxString& aPropY,
                                              const wxString& aLock, const wxString& aLabel,
                                              const OPTS& aOpts )
{
    ROW& row = AddText( aPropX, aLabel, aOpts );
    row.kind = FORM_KIND::XY;
    row.prop2 = aPropY;
    row.lock = aLock;
    return row;
}


PROPERTIES_FORM::ROW& PROPERTIES_FORM::AddTwin( const wxString& aPropA, const wxString& aCaptionA,
                                                const wxString& aPropB, const wxString& aCaptionB,
                                                const OPTS& aOpts )
{
    ROW& row = AddText( aPropA, aCaptionA, aOpts );
    row.kind = FORM_KIND::TWIN;
    row.prop2 = aPropB;
    row.label2 = aCaptionB;
    return row;
}


PROPERTIES_FORM::ROW& PROPERTIES_FORM::AddInfo( const wxString& aLabel, std::function<wxString()> aValue,
                                                bool aBoxed, const OPTS& aOpts )
{
    ROW& row = AddText( wxEmptyString, aLabel, aOpts );
    row.kind = FORM_KIND::INFO;
    row.text = std::move( aValue );
    row.boxed = aBoxed;
    return row;
}


PROPERTIES_FORM::ROW& PROPERTIES_FORM::AddCustomText( const wxString& aLabel, std::function<wxString()> aGet,
                                                      std::function<void( const wxString& )> aSet,
                                                      const OPTS& aOpts )
{
    ROW& row = AddInfo( aLabel, std::move( aGet ), true, aOpts );
    row.editText = std::move( aSet );
    return row;
}


PROPERTIES_FORM::ROW& PROPERTIES_FORM::AddCustomChoice( const wxString& aLabel, std::vector<wxString> aChoices,
                                                        std::function<int()> aGet,
                                                        std::function<void( int )> aSet,
                                                        const OPTS& aOpts )
{
    ROW& row = AddText( wxEmptyString, aLabel, aOpts );
    row.kind = FORM_KIND::CUSTOM_CHOICE;
    row.labels = std::move( aChoices );
    row.getInt = std::move( aGet );
    row.setInt = std::move( aSet );
    return row;
}


PROPERTIES_FORM::ROW& PROPERTIES_FORM::AddTitle( const wxString& aTitle, const OPTS& aOpts )
{
    ROW& row = AddText( wxEmptyString, aTitle, aOpts );
    row.kind = FORM_KIND::TITLE;
    return row;
}


PROPERTIES_FORM::ROW& PROPERTIES_FORM::AddSeparator( const OPTS& aOpts )
{
    ROW& row = AddText( wxEmptyString, wxEmptyString, aOpts );
    row.kind = FORM_KIND::SEPARATOR;
    return row;
}


PROPERTIES_FORM::ROW& PROPERTIES_FORM::AddLink( std::function<wxString()> aText, std::function<void()> aClick,
                                                const OPTS& aOpts )
{
    ROW& row = AddText( wxEmptyString, wxEmptyString, aOpts );
    row.kind = FORM_KIND::LINK;
    row.text = std::move( aText );
    row.click = std::move( aClick );
    return row;
}


PROPERTIES_FORM::ROW& PROPERTIES_FORM::AddPaint( int aHeightDIP, std::function<void( wxDC&, const wxRect& )> aPaint,
                                                 const OPTS& aOpts )
{
    ROW& row = AddText( wxEmptyString, wxEmptyString, aOpts );
    row.kind = FORM_KIND::PAINT;
    row.heightDIP = aHeightDIP;
    row.paint = std::move( aPaint );
    return row;
}


PROPERTIES_FORM::ROW& PROPERTIES_FORM::AddButtons( std::vector<std::pair<wxString, std::function<void()>>> aButtons,
                                                   const OPTS& aOpts )
{
    ROW& row = AddText( wxEmptyString, wxEmptyString, aOpts );
    row.kind = FORM_KIND::BUTTONS;
    row.buttons = std::move( aButtons );
    return row;
}


PROPERTIES_FORM::ROW& PROPERTIES_FORM::AddTable( std::vector<int> aWeights, std::function<TABLE_ROWS()> aRows,
                                                 int* aSelected, const OPTS& aOpts )
{
    ROW& row = AddText( wxEmptyString, wxEmptyString, aOpts );
    row.kind = FORM_KIND::TABLE;
    row.weights = std::move( aWeights );
    row.table = std::move( aRows );
    row.selected = aSelected;
    return row;
}


void PROPERTIES_FORM::CollectBound( std::set<wxString>& aNames ) const
{
    for( const ROW* row : m_rows )
    {
        for( const wxString& name : { row->prop, row->prop2, row->lock } )
        {
            if( !name.IsEmpty() )
                aNames.insert( name );
        }

        if( row->kind == FORM_KIND::TABLE && row->table )
        {
            for( const std::vector<CELL>& cells : row->table() )
            {
                for( const CELL& cell : cells )
                {
                    if( !cell.prop.IsEmpty() )
                        aNames.insert( cell.prop );
                }
            }
        }
    }
}


// --------------------------------------------------------------------------------------- layout

bool PROPERTIES_FORM::rowShown( const ROW& aRow ) const
{
    if( aRow.opts.visible && !aRow.opts.visible() )
        return false;

    switch( aRow.kind )
    {
    case FORM_KIND::TEXT:
    case FORM_KIND::CHOICE:
    case FORM_KIND::CHECK:
    case FORM_KIND::SEGMENTED:
        return prop( aRow.prop ) != nullptr;

    case FORM_KIND::XY:
        return prop( aRow.prop ) && prop( aRow.prop2 );

    case FORM_KIND::TWIN:
        return prop( aRow.prop ) || prop( aRow.prop2 );

    default:
        return true;
    }
}


int PROPERTIES_FORM::rowHeight( const ROW& aRow ) const
{
    int h = GetCharHeight() + FromDIP( 12 );

    switch( aRow.kind )
    {
    case FORM_KIND::TEXT:      return h + ( std::max( 1, aRow.opts.lines ) - 1 ) * GetCharHeight();
    case FORM_KIND::INFO:      return aRow.boxed ? h + ( std::max( 1, aRow.opts.lines ) - 1 ) * GetCharHeight()
                                                 : GetCharHeight() + FromDIP( 8 );
    case FORM_KIND::TWIN:      return h + GetCharHeight() + FromDIP( 4 );
    case FORM_KIND::TITLE:     return GetCharHeight() + FromDIP( 10 );
    case FORM_KIND::SEPARATOR: return FromDIP( 11 );
    case FORM_KIND::LINK:      return GetCharHeight() + FromDIP( 8 );
    case FORM_KIND::PAINT:     return FromDIP( aRow.heightDIP );
    default:                   return h;
    }
}


void PROPERTIES_FORM::layoutRows( int aWidth )
{
    int pad = FromDIP( 8 );
    int gap = FromDIP( 6 );
    int y = FromDIP( 6 );
    int labelW = aWidth * 2 / 5;
    int valueX = labelW + gap;
    int right = aWidth - pad;
    wxClientDC dc( this );
    dc.SetFont( GetFont() );

    for( ROW* row : m_rows )
    {
        row->hits.clear();

        if( !rowShown( *row ) )
        {
            row->rect = wxRect();
            continue;
        }

        int h = rowHeight( *row );

        // Wrapping buttons: the height depends on the width
        if( row->kind == FORM_KIND::BUTTONS )
        {
            int bh = GetCharHeight() + FromDIP( 12 );
            int x = pad;
            int by = y;

            for( const auto& [label, action] : row->buttons )
            {
                int bw = dc.GetTextExtent( label ).x + FromDIP( 22 );

                if( x > pad && x + bw > right )
                {
                    x = pad;
                    by += bh + FromDIP( 4 );
                }

                row->hits.emplace_back( x, by, bw, bh );
                x += bw + FromDIP( 4 );
            }

            h = by + bh - y + FromDIP( 2 );
        }

        // Tables: as tall as their rows
        if( row->kind == FORM_KIND::TABLE )
        {
            int rh = GetCharHeight() + FromDIP( 8 );
            int x0 = pad;
            int width = std::max( 1, right - pad );
            int total = 0;

            for( int weight : row->weights )
                total += std::max( 1, weight );

            total = std::max( 1, total );

            row->cells = row->table ? row->table() : TABLE_ROWS();
            row->cellRects.clear();

            int ry = y + FromDIP( 2 );

            for( const std::vector<CELL>& cells : row->cells )
            {
                std::vector<wxRect> rects;
                int                 cx = x0;
                int                 acc = 0;

                for( size_t ii = 0; ii < cells.size(); ++ii )
                {
                    int cellRight;

                    if( ii + 1 == cells.size() || ii + 1 >= row->weights.size() )
                    {
                        cellRight = x0 + width;
                    }
                    else
                    {
                        acc += std::max( 1, row->weights[ii] );
                        cellRight = x0 + width * acc / total;
                    }

                    rects.emplace_back( cx, ry, std::max( 1, cellRight - cx ), rh );
                    cx = cellRight;

                    if( cx >= x0 + width )
                        break;
                }

                row->cellRects.push_back( rects );
                ry += rh;
            }

            h = ry - y + FromDIP( 4 );
        }

        row->rect = wxRect( 0, y, aWidth, h );
        row->labelRect = wxRect( pad, y, labelW - pad, h );

        int   boxH = GetCharHeight() + FromDIP( 8 );
        int   boxY = y + ( std::min( h, GetCharHeight() + FromDIP( 12 ) ) - boxH ) / 2;
        int   sq = boxH;

        switch( row->kind )
        {
        case FORM_KIND::TEXT:
        case FORM_KIND::CHOICE:
        case FORM_KIND::CUSTOM_CHOICE:
        case FORM_KIND::INFO:
        {
            int boxRight = right;
            wxRect extraEye, extraPick, extraLock;

            // Altium's order at the right of a box: eye, then lock
            if( row->opts.lockState )
            {
                extraLock = wxRect( boxRight - sq, boxY, sq, boxH );
                boxRight -= sq + FromDIP( 4 );
            }

            if( row->opts.eyeState )
            {
                extraEye = wxRect( boxRight - sq, boxY, sq, boxH );
                boxRight -= sq + FromDIP( 4 );
            }

            if( row->opts.pickerShown && row->opts.pickerShown() )
            {
                extraPick = wxRect( boxRight - sq, boxY, sq, boxH );
                boxRight -= sq + FromDIP( 4 );
            }

            int boxHeight = h - ( boxY - y ) * 2;
            row->hits.emplace_back( valueX, boxY, std::max( FromDIP( 20 ), boxRight - valueX ), boxHeight );
            row->hits.push_back( extraEye );
            row->hits.push_back( extraPick );
            row->hits.push_back( extraLock );
            break;
        }

        case FORM_KIND::CHECK:
        case FORM_KIND::CUSTOM_CHECK:
        {
            int box = FromDIP( 15 );

            if( row->boxFirst )
                row->hits.emplace_back( pad + FromDIP( 4 ), y + ( h - box ) / 2, box, box );
            else
                row->hits.emplace_back( valueX, y + ( h - box ) / 2, box, box );

            break;
        }

        case FORM_KIND::SEGMENTED:
        case FORM_KIND::CUSTOM_SEGMENTED:
        {
            int x0 = row->label.IsEmpty() ? pad : valueX;
            row->hits.emplace_back( x0, boxY, right - x0, boxH );
            break;
        }

        case FORM_KIND::XY:
        {
            int lockW = row->lock.IsEmpty() ? 0 : sq + FromDIP( 4 );
            int w = ( right - lockW - valueX - FromDIP( 4 ) ) / 2;
            row->hits.emplace_back( valueX, boxY, w, boxH );
            row->hits.emplace_back( valueX + w + FromDIP( 4 ), boxY, w, boxH );

            if( lockW )
                row->hits.emplace_back( right - sq, boxY, sq, boxH );

            break;
        }

        case FORM_KIND::TWIN:
        {
            int colW = ( aWidth - 2 * pad - FromDIP( 16 ) ) / 2;
            int x0 = pad + FromDIP( 8 );
            int by = y + GetCharHeight() + FromDIP( 4 );
            row->hits.emplace_back( x0, by, colW - FromDIP( 8 ), boxH );
            row->hits.emplace_back( x0 + colW, by, colW - FromDIP( 8 ), boxH );
            break;
        }

        case FORM_KIND::LINK:
        {
            wxString text = row->text ? row->text() : wxString();
            wxSize   ext = dc.GetTextExtent( text );
            row->hits.emplace_back( pad + FromDIP( 6 ), y + ( h - ext.y ) / 2, ext.x, ext.y );
            break;
        }

        default:
            break;
        }

        y += h;
    }

}


int PROPERTIES_FORM::contentHeight( int aWidth ) const
{
    PROPERTIES_FORM* self = const_cast<PROPERTIES_FORM*>( this );
    self->layoutRows( aWidth );

    int bottom = 0;

    for( const ROW* row : m_rows )
    {
        if( !row->rect.IsEmpty() )
            bottom = std::max( bottom, row->rect.GetBottom() + 1 );
    }

    return bottom > 0 ? bottom + FromDIP( 6 ) : 0;
}


bool PROPERTIES_FORM::HasVisibleRows() const
{
    for( const ROW* row : m_rows )
    {
        if( rowShown( *row ) && row->kind != FORM_KIND::SEPARATOR )
            return true;
    }

    return false;
}


wxSize PROPERTIES_FORM::DoGetBestClientSize() const
{
    int width = GetClientSize().x > 0 ? GetClientSize().x : FromDIP( 260 );
    return wxSize( FromDIP( 160 ), contentHeight( width ) );
}


void PROPERTIES_FORM::Sync()
{
    if( m_editor )
        return;     // don't disturb a value being typed

    int height = contentHeight( GetClientSize().x > 0 ? GetClientSize().x : FromDIP( 260 ) );

    if( height != m_height )
    {
        m_height = height;
        SetMinSize( wxSize( -1, height ) );
        InvalidateBestSize();

        // A hidden form is laid out when it is shown
        if( IsShown() )
            m_host->FormLayoutChanged();
    }

    if( IsShown() )
        Refresh();
}


// ---------------------------------------------------------------------------------------- values

/// Altium shows lengths to 4 decimals in mm, 3 in mils and 5 in inches ("136.2375mm"), where
/// KiCad prints every nanometre ("136.237501 mm").  Display only: an edit that leaves the
/// text as shown is not applied, so the rounding never moves anything.
static wxString roundedDistance( const wxString& aText )
{
    size_t numEnd = 0;

    while( numEnd < aText.length()
           && ( wxIsdigit( aText[numEnd] ) || aText[numEnd] == '.' || aText[numEnd] == '-'
                || aText[numEnd] == ',' ) )
    {
        ++numEnd;
    }

    wxString number = aText.Left( numEnd );
    wxString unit = aText.Mid( numEnd );
    int      decimalSep = std::max( number.Find( '.' ), number.Find( ',' ) );

    if( number.IsEmpty() || decimalSep == wxNOT_FOUND )
        return aText;

    wxString u = unit.Strip( wxString::both ).Lower();
    int      places = u.StartsWith( wxS( "mil" ) ) || u == wxS( "th" ) ? 3
                    : u == wxS( "in" ) || u == wxS( "\"" )               ? 5
                    : u == wxS( "mm" )                                   ? 4
                                                                          : -1;

    if( places < 0 || (int) number.length() - decimalSep - 1 <= places )
        return aText;

    wxChar sep = number[decimalSep];
    double value = 0.0;

    if( !number.BeforeFirst( sep ).Append( '.' ).Append( number.AfterFirst( sep ) ).ToCDouble( &value ) )
        return aText;

    wxString out = wxString::FromCDouble( value, places );

    // Drop trailing zeros (and a bare point), as KiCad's own formatting does
    while( out.EndsWith( wxS( "0" ) ) )
        out.RemoveLast();

    if( out.EndsWith( wxS( "." ) ) )
        out.RemoveLast();

    if( out == wxS( "-0" ) )
        out = wxS( "0" );

    if( sep != '.' )
        out.Replace( wxS( "." ), wxString( sep ) );

    return out + unit;
}


wxString PROPERTIES_FORM::valueText( const wxString& aProp ) const
{
    wxPGProperty* pg = prop( aProp );

    if( !pg || pg->IsValueUnspecified() )
        return wxEmptyString;

    wxString text = pg->GetValueAsString();

    if( dynamic_cast<PGPROPERTY_DISTANCE*>( pg ) )
        text = roundedDistance( text );

    return text;
}


void PROPERTIES_FORM::apply( const wxString& aProp, const wxVariant& aValue )
{
    m_host->FormApply( { { aProp, aValue } } );
}


// --------------------------------------------------------------------------------------- painting

namespace
{
void drawCheck( wxDC& aDC, const wxRect& aBox, int aState, const wxColour& aBg, const wxColour& aFg,
                wxWindow* aWin )
{
    aDC.SetPen( wxPen( PROPERTIES_FORM::Mix( aBg, aFg, 0.35 ) ) );
    aDC.SetBrush( wxBrush( PROPERTIES_FORM::Mix( aBg, *wxBLACK, 0.15 ) ) );
    aDC.DrawRoundedRectangle( aBox, aWin->FromDIP( 2 ) );

    int b = aBox.width;

    if( aState == 1 )
    {
        aDC.SetPen( wxPen( aFg, aWin->FromDIP( 2 ) ) );
        aDC.DrawLine( aBox.x + b / 5, aBox.y + b / 2, aBox.x + b * 2 / 5, aBox.y + b * 3 / 4 );
        aDC.DrawLine( aBox.x + b * 2 / 5, aBox.y + b * 3 / 4, aBox.x + b * 4 / 5, aBox.y + b / 4 );
    }
    else if( aState == 2 )
    {
        aDC.SetPen( wxPen( aFg, aWin->FromDIP( 2 ) ) );
        aDC.DrawLine( aBox.x + b / 4, aBox.y + b / 2, aBox.x + b * 3 / 4, aBox.y + b / 2 );
    }
}


void drawLock( wxDC& aDC, const wxRect& aRect, bool aLocked, const wxColour& aFg, wxWindow* aWin )
{
    int cx = aRect.x + aRect.width / 2;
    int cy = aRect.y + aRect.height / 2;
    int w = aRect.width * 2 / 5;
    int h = aRect.height / 3;
    wxRect body( cx - w / 2, cy - h / 4, w, h );

    aDC.SetPen( wxPen( aFg, aWin->FromDIP( 2 ) ) );
    aDC.SetBrush( *wxTRANSPARENT_BRUSH );

    // Shackle: closed over the body when locked, raised to one side when open
    int sw = w * 3 / 5;
    int sx = aLocked ? cx - sw / 2 : cx - sw / 2 + w / 4;
    int sy = body.y - h * 2 / 3 - ( aLocked ? 0 : aWin->FromDIP( 2 ) );
    aDC.DrawArc( sx + sw, body.y, sx, body.y, sx + sw / 2, body.y );
    aDC.DrawLine( sx, body.y, sx, sy + sw / 2 );
    aDC.DrawLine( sx + sw, aLocked ? body.y : body.y - aWin->FromDIP( 3 ), sx + sw, sy + sw / 2 );
    aDC.DrawEllipticArc( sx, sy, sw, sw, 0, 180 );

    aDC.SetBrush( wxBrush( aFg ) );
    aDC.SetPen( *wxTRANSPARENT_PEN );
    aDC.DrawRoundedRectangle( body, aWin->FromDIP( 1 ) );
}


void drawEye( wxDC& aDC, const wxRect& aRect, bool aShown, const wxColour& aFg, wxWindow* aWin )
{
    int cx = aRect.x + aRect.width / 2;
    int cy = aRect.y + aRect.height / 2;
    int w = aRect.width * 3 / 5;
    int h = aRect.height / 3;

    aDC.SetPen( wxPen( aFg, aWin->FromDIP( 1 ) + 1 ) );
    aDC.SetBrush( *wxTRANSPARENT_BRUSH );
    aDC.DrawEllipse( cx - w / 2, cy - h / 2, w, h );
    aDC.SetBrush( wxBrush( aFg ) );
    aDC.DrawCircle( cx, cy, std::max( 1, h / 3 ) );

    if( !aShown )
        aDC.DrawLine( cx - w / 2, cy + h, cx + w / 2, cy - h );
}


void drawPicker( wxDC& aDC, const wxRect& aRect, const wxColour& aFg, wxWindow* aWin )
{
    int x0 = aRect.x + aRect.width / 4;
    int y1 = aRect.y + aRect.height * 3 / 4;
    int x1 = aRect.x + aRect.width * 3 / 4;
    int y0 = aRect.y + aRect.height / 4;

    aDC.SetPen( wxPen( aFg, aWin->FromDIP( 2 ) ) );
    aDC.DrawLine( x0, y1, x1, y0 );
    aDC.SetBrush( wxBrush( aFg ) );
    aDC.DrawCircle( x1, y0, aWin->FromDIP( 2 ) );
}
} // namespace


void PROPERTIES_FORM::onPaint( wxPaintEvent& )
{
    wxAutoBufferedPaintDC dc( this );
    wxColour              parentBg = GetParent()->GetBackgroundColour();
    wxColour              fg = wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOWTEXT );
    wxColour              grey = wxSystemSettings::GetColour( wxSYS_COLOUR_GRAYTEXT );
    wxColour              block = Mix( parentBg, fg, 0.06 );
    wxColour              boxFill = Mix( block, *wxBLACK, 0.30 );
    wxColour              boxEdge = Mix( block, *wxBLACK, 0.55 );
    wxSize                size = GetClientSize();

    dc.SetBackground( wxBrush( parentBg ) );
    dc.Clear();
    dc.SetPen( *wxTRANSPARENT_PEN );
    dc.SetBrush( wxBrush( block ) );
    dc.DrawRoundedRectangle( wxRect( size ), FromDIP( 3 ) );
    dc.SetFont( GetFont() );

    layoutRows( size.x );

    auto drawBox =
            [&]( const wxRect& aBox, const wxString& aText, bool aReadOnly, bool aArrow,
                 const wxColour& aSwatch, int aLines )
            {
                dc.SetPen( wxPen( boxEdge ) );
                dc.SetBrush( wxBrush( boxFill ) );
                dc.DrawRoundedRectangle( aBox, FromDIP( 2 ) );

                int tx = aBox.x + FromDIP( 6 );

                if( aSwatch.IsOk() )
                {
                    dc.SetPen( *wxTRANSPARENT_PEN );
                    dc.SetBrush( wxBrush( aSwatch ) );
                    dc.DrawRectangle( aBox.x + 1, aBox.y + 1, FromDIP( 7 ), aBox.height - 2 );
                    tx += FromDIP( 7 );
                }

                int textRight = aBox.GetRight() - FromDIP( 4 );

                if( aArrow )
                {
                    int a = FromDIP( 4 );
                    int ax = aBox.GetRight() - FromDIP( 10 );
                    int ay = aBox.y + aBox.height / 2;
                    wxPoint tri[3] = { { ax - a, ay - a / 2 }, { ax + a, ay - a / 2 }, { ax, ay + a / 2 + 1 } };
                    dc.SetPen( wxPen( aReadOnly ? grey : fg ) );
                    dc.SetBrush( wxBrush( aReadOnly ? grey : fg ) );
                    dc.DrawPolygon( 3, tri );
                    textRight = ax - a - FromDIP( 4 );
                }

                dc.SetTextForeground( aReadOnly ? grey : fg );
                wxDCClipper clip( dc, wxRect( aBox.x, aBox.y, std::max( 1, textRight - aBox.x ), aBox.height ) );

                if( aLines > 1 )
                {
                    // Word-wrapped multi-line value
                    int      y = aBox.y + FromDIP( 4 );
                    int      maxW = textRight - tx;
                    wxString line;

                    for( const wxString& word : wxSplit( aText, ' ', 0 ) )
                    {
                        wxString candidate = line.IsEmpty() ? word : line + wxS( " " ) + word;

                        if( !line.IsEmpty() && dc.GetTextExtent( candidate ).x > maxW )
                        {
                            dc.DrawText( line, tx, y );
                            y += GetCharHeight();
                            line = word;
                        }
                        else
                        {
                            line = candidate;
                        }
                    }

                    if( !line.IsEmpty() )
                        dc.DrawText( line, tx, y );
                }
                else
                {
                    wxString shown = wxControl::Ellipsize( aText, dc, wxELLIPSIZE_END, textRight - tx );
                    dc.DrawText( shown, tx, aBox.y + ( aBox.height - GetCharHeight() ) / 2 );
                }
            };

    auto drawLabel =
            [&]( const ROW& aRow )
            {
                if( aRow.label.IsEmpty() )
                    return;

                dc.SetTextForeground( aRow.opts.linkLabel ? LinkColour() : fg );
                wxString shown = wxControl::Ellipsize( aRow.label, dc, wxELLIPSIZE_END, aRow.labelRect.width );
                wxSize   ext = dc.GetTextExtent( shown );
                int      ty = aRow.labelRect.y + ( std::min( aRow.labelRect.height,
                                                             GetCharHeight() + FromDIP( 12 ) ) - ext.y ) / 2;
                dc.DrawText( shown, aRow.labelRect.GetRight() - ext.x, ty );
            };

    auto drawSegments =
            [&]( const wxRect& aBar, const std::vector<wxString>& aLabels, int aActive,
                 const std::vector<bool>& aEnabled,
                 const std::function<void( wxDC&, const wxRect&, int, const wxColour& )>& aPaint )
            {
                int n = (int) aLabels.size();

                if( n == 0 )
                    return;

                dc.SetPen( wxPen( boxEdge ) );
                dc.SetBrush( wxBrush( Mix( block, *wxBLACK, 0.25 ) ) );
                dc.DrawRoundedRectangle( aBar, FromDIP( 3 ) );

                for( int ii = 0; ii < n; ++ii )
                {
                    int    x0 = aBar.x + aBar.width * ii / n;
                    int    x1 = aBar.x + aBar.width * ( ii + 1 ) / n;
                    wxRect seg( x0, aBar.y, x1 - x0, aBar.height );
                    bool   enabled = ii >= (int) aEnabled.size() || aEnabled[ii];

                    if( ii == aActive )
                    {
                        dc.SetPen( *wxTRANSPARENT_PEN );
                        dc.SetBrush( wxBrush( ON_FILL ) );
                        dc.DrawRectangle( seg.Deflate( 1 ) );
                    }

                    if( ii > 0 )
                    {
                        dc.SetPen( wxPen( boxEdge ) );
                        dc.DrawLine( x0, aBar.y + 1, x0, aBar.GetBottom() );
                    }

                    wxColour textColour = !enabled ? grey : ii == aActive ? ON_TEXT : fg;

                    if( aPaint )
                    {
                        aPaint( dc, seg, ii, textColour );
                        continue;
                    }

                    wxString shown = wxControl::Ellipsize( aLabels[ii], dc, wxELLIPSIZE_END,
                                                           seg.width - FromDIP( 4 ) );
                    wxSize   ext = dc.GetTextExtent( shown );
                    dc.SetTextForeground( textColour );
                    dc.DrawText( shown, seg.x + ( seg.width - ext.x ) / 2, seg.y + ( seg.height - ext.y ) / 2 );
                }
            };

    for( int idx = 0; idx < (int) m_rows.size(); ++idx )
    {
        ROW& row = *m_rows[idx];

        if( row.rect.IsEmpty() )
            continue;

        wxPGProperty* pg = prop( row.prop );
        bool readOnly = row.opts.readOnly || ( pg && pg->HasFlag( wxPGFlags::ReadOnly ) );

        switch( row.kind )
        {
        case FORM_KIND::TEXT:
            drawLabel( row );
            drawBox( row.hits[0], valueText( row.prop ), readOnly, false, wxNullColour, row.opts.lines );
            break;

        case FORM_KIND::CHOICE:
        {
            drawLabel( row );
            wxColour swatch;

            if( row.opts.swatch && pg && !pg->IsValueUnspecified() )
            {
                if( PGPROPERTY_COLORENUM* colour = dynamic_cast<PGPROPERTY_COLORENUM*>( pg ) )
                    swatch = colour->GetColor( (int) pg->GetValue().GetLong() );
            }

            drawBox( row.hits[0], valueText( row.prop ), readOnly, !readOnly, swatch, 1 );
            break;
        }

        case FORM_KIND::CUSTOM_CHOICE:
        {
            drawLabel( row );
            if( row.opts.choices )
                row.labels = row.opts.choices();

            int      sel = row.getInt ? row.getInt() : -1;
            wxString text = sel >= 0 && sel < (int) row.labels.size() ? row.labels[sel] : wxString();
            wxColour swatch = row.opts.swatchColour && sel >= 0 ? row.opts.swatchColour()
                                                                : wxNullColour;
            drawBox( row.hits[0], text, row.opts.readOnly, !row.opts.readOnly, swatch, 1 );
            break;
        }

        case FORM_KIND::INFO:
        {
            drawLabel( row );
            wxString text = row.text ? roundedDistance( row.text() ) : wxString();

            if( row.boxed )
            {
                drawBox( row.hits[0], text, !row.editText || row.opts.readOnly, false, wxNullColour,
                         row.opts.lines );
            }
            else
            {
                dc.SetTextForeground( fg );
                dc.DrawText( text, row.hits[0].x,
                             row.rect.y + ( row.rect.height - GetCharHeight() ) / 2 );
            }

            break;
        }

        case FORM_KIND::CHECK:
        case FORM_KIND::CUSTOM_CHECK:
        {
            int state = 0;

            if( row.kind == FORM_KIND::CUSTOM_CHECK )
                state = row.getInt ? row.getInt() : 0;
            else if( pg )
                state = pg->IsValueUnspecified() ? 2 : ( pg->GetValue().GetBool() ? 1 : 0 );

            const wxRect& box = row.hits[0];
            drawCheck( dc, box, state, block, readOnly ? grey : fg, this );

            if( row.boxFirst )
            {
                dc.SetTextForeground( readOnly ? grey : fg );
                dc.DrawText( row.label, box.GetRight() + FromDIP( 8 ),
                             row.rect.y + ( row.rect.height - GetCharHeight() ) / 2 );
            }
            else
            {
                drawLabel( row );
            }

            break;
        }

        case FORM_KIND::SEGMENTED:
        {
            drawLabel( row );
            wxPGChoices           choices = pg ? pg->GetChoices() : wxPGChoices();
            std::vector<wxString> labels = row.labels;
            std::vector<long>     values = row.values;

            if( values.empty() )
            {
                for( unsigned ii = 0; ii < choices.GetCount(); ++ii )
                    values.push_back( choices.GetValue( ii ) );
            }

            if( labels.empty() )
            {
                for( long value : values )
                {
                    int ci = choices.Index( (int) value );
                    labels.push_back( ci >= 0 ? choices.GetLabel( ci ) : wxString() );
                }
            }

            int active = -1;

            if( pg && !pg->IsValueUnspecified() )
            {
                auto it = std::find( values.begin(), values.end(), pg->GetValue().GetLong() );

                if( it != values.end() )
                    active = (int) ( it - values.begin() );
            }

            drawSegments( row.hits[0], labels, active, {}, nullptr );
            break;
        }

        case FORM_KIND::CUSTOM_SEGMENTED:
            drawLabel( row );
            drawSegments( row.hits[0], row.labels, row.getInt ? row.getInt() : -1, row.enabled,
                          row.opts.segmentPaint );
            break;

        case FORM_KIND::XY:
        {
            drawLabel( row );
            wxPGProperty* pgY = prop( row.prop2 );
            bool roY = readOnly || ( pgY && pgY->HasFlag( wxPGFlags::ReadOnly ) );

            drawBox( row.hits[0], valueText( row.prop ), readOnly, false, wxNullColour, 1 );
            drawBox( row.hits[1], valueText( row.prop2 ), roY, false, wxNullColour, 1 );

            if( row.hits.size() > 2 )
            {
                wxPGProperty* lock = prop( row.lock );
                bool locked = lock && !lock->IsValueUnspecified() && lock->GetValue().GetBool();

                dc.SetPen( wxPen( boxEdge ) );
                dc.SetBrush( wxBrush( Mix( block, *wxBLACK, 0.20 ) ) );
                dc.DrawRoundedRectangle( row.hits[2], FromDIP( 2 ) );
                drawLock( dc, row.hits[2], locked, lock ? fg : grey, this );
            }

            break;
        }

        case FORM_KIND::TWIN:
        {
            for( int ii = 0; ii < 2; ++ii )
            {
                const wxString& name = ii == 0 ? row.prop : row.prop2;
                const wxString& caption = ii == 0 ? row.label : row.label2;
                wxPGProperty*   p = prop( name );
                const wxRect&   box = row.hits[ii];

                if( !p )
                    continue;

                dc.SetTextForeground( fg );
                dc.DrawText( caption, box.x, row.rect.y + FromDIP( 2 ) );
                drawBox( box, valueText( name ), row.opts.readOnly || p->HasFlag( wxPGFlags::ReadOnly ),
                         false, wxNullColour, 1 );
            }

            break;
        }

        case FORM_KIND::TITLE:
        {
            wxFont bold = GetFont().Bold();
            dc.SetFont( bold );
            dc.SetTextForeground( fg );
            dc.DrawText( row.label, FromDIP( 8 ),
                         row.rect.y + ( row.rect.height - GetCharHeight() ) / 2 );
            dc.SetFont( GetFont() );

            // Altium's shortcut hint at the right of a sub-title
            if( !row.opts.hint.IsEmpty() )
            {
                wxSize ext = dc.GetTextExtent( row.opts.hint );
                dc.SetTextForeground( grey );
                dc.DrawText( row.opts.hint, size.x - FromDIP( 8 ) - ext.x,
                             row.rect.y + ( row.rect.height - ext.y ) / 2 );
            }

            break;
        }

        case FORM_KIND::SEPARATOR:
            dc.SetPen( wxPen( Mix( block, fg, 0.25 ) ) );
            dc.DrawLine( FromDIP( 8 ), row.rect.y + row.rect.height / 2, size.x - FromDIP( 8 ),
                         row.rect.y + row.rect.height / 2 );
            break;

        case FORM_KIND::LINK:
        {
            wxFont link = GetFont().Underlined();
            dc.SetFont( link );
            dc.SetTextForeground( LinkColour() );
            dc.DrawText( row.text ? row.text() : wxString(), row.hits[0].x, row.hits[0].y );
            dc.SetFont( GetFont() );
            break;
        }

        case FORM_KIND::PAINT:
            if( row.paint )
            {
                wxDCClipper clip( dc, row.rect );
                row.paint( dc, row.rect );
            }

            break;

        case FORM_KIND::TABLE:
        {
            wxColour tableBg = Mix( block, *wxBLACK, 0.18 );
            wxColour headBg = Mix( block, *wxBLACK, 0.06 );
            wxColour line = Mix( block, *wxBLACK, 0.40 );

            if( row.cellRects.empty() )
                break;

            wxRect all = row.cellRects.front().front();

            for( const std::vector<wxRect>& rects : row.cellRects )
            {
                for( const wxRect& r : rects )
                    all.Union( r );
            }

            const bool plain = row.opts.plainTable;

            if( !plain )
            {
                dc.SetPen( *wxTRANSPARENT_PEN );
                dc.SetBrush( wxBrush( tableBg ) );
                dc.DrawRectangle( all );
            }

            for( size_t rr = 0; rr < row.cells.size() && rr < row.cellRects.size(); ++rr )
            {
                const std::vector<CELL>&   cells = row.cells[rr];
                const std::vector<wxRect>& rects = row.cellRects[rr];
                bool heading = !cells.empty() && cells.front().heading;

                if( rects.empty() )
                    continue;

                wxRect band( all.x, rects.front().y, all.width, rects.front().height );

                if( row.selected && *row.selected == (int) rr && !heading )
                {
                    dc.SetPen( *wxTRANSPARENT_PEN );
                    dc.SetBrush( wxBrush( ON_FILL ) );
                    dc.DrawRectangle( band );
                }
                else if( heading && !plain )
                {
                    dc.SetPen( *wxTRANSPARENT_PEN );
                    dc.SetBrush( wxBrush( headBg ) );
                    dc.DrawRectangle( band );
                }

                if( !plain )
                {
                    dc.SetPen( wxPen( line ) );
                    dc.DrawLine( band.x, band.GetBottom(), band.GetRight() + 1, band.GetBottom() );
                }

                for( size_t cc = 0; cc < cells.size() && cc < rects.size(); ++cc )
                {
                    const CELL&   cell = cells[cc];
                    const wxRect& r = rects[cc];
                    wxString      text = cell.prop.IsEmpty() ? cell.text : valueText( cell.prop );
                    wxPGProperty* pg = prop( cell.prop );
                    bool          dim = cell.readOnly || ( pg && pg->HasFlag( wxPGFlags::ReadOnly ) )
                                        || ( !cell.prop.IsEmpty() && !pg );

                    if( cell.link )
                    {
                        // Altium's links are plain blue text
                        dc.SetTextForeground( LinkColour() );
                    }
                    else if( cell.heading )
                    {
                        dc.SetTextForeground( Mix( block, fg, 0.70 ) );
                    }
                    else
                    {
                        bool selected = row.selected && *row.selected == (int) rr;
                        dc.SetTextForeground( selected ? ON_TEXT : dim ? grey : fg );
                    }

                    if( cell.eye >= 0 )
                    {
                        int    side = std::min( r.width, r.height );
                        wxRect icon( r.x + ( r.width - side ) / 2, r.y, side, r.height );
                        drawEye( dc, icon, cell.eye > 0, cell.eye > 0 ? fg : grey, this );

                        if( cc > 0 && !plain )
                        {
                            dc.SetPen( wxPen( line ) );
                            dc.DrawLine( r.x, r.y, r.x, r.GetBottom() );
                        }

                        continue;
                    }

                    wxString shown = wxControl::Ellipsize( text, dc, wxELLIPSIZE_END,
                                                           std::max( 1, r.width - FromDIP( 8 ) ) );
                    int tx = r.x + FromDIP( 4 );

                    // Plain tables label their rows on the right of the first column (link
                    // labels, like Altium's Delay / Length, stay on the left)
                    if( plain && cc == 0 && !cell.link )
                        tx = r.GetRight() - FromDIP( 8 ) - dc.GetTextExtent( shown ).x;

                    dc.DrawText( shown, tx, r.y + ( r.height - GetCharHeight() ) / 2 );
                    dc.SetFont( GetFont() );

                    if( cc > 0 && !plain )
                    {
                        dc.SetPen( wxPen( line ) );
                        dc.DrawLine( r.x, r.y, r.x, r.GetBottom() );
                    }
                }
            }

            if( !plain )
            {
                dc.SetPen( wxPen( boxEdge ) );
                dc.SetBrush( *wxTRANSPARENT_BRUSH );
                dc.DrawRectangle( all );
            }

            break;
        }

        case FORM_KIND::BUTTONS:
            for( size_t ii = 0; ii < row.buttons.size() && ii < row.hits.size(); ++ii )
            {
                const wxRect& b = row.hits[ii];
                bool hover = idx == m_hoverRow && b.Contains( ScreenToClient( wxGetMousePosition() ) );

                dc.SetPen( wxPen( boxEdge ) );
                dc.SetBrush( wxBrush( hover ? Mix( block, *wxWHITE, 0.10 ) : Mix( block, *wxBLACK, 0.22 ) ) );
                dc.DrawRoundedRectangle( b, FromDIP( 3 ) );

                wxSize ext = dc.GetTextExtent( row.buttons[ii].first );
                dc.SetTextForeground( fg );
                dc.DrawText( row.buttons[ii].first, b.x + ( b.width - ext.x ) / 2,
                             b.y + ( b.height - ext.y ) / 2 );
            }

            break;
        }

        // Eye toggle and eyedropper next to text / choice boxes
        if( ( row.kind == FORM_KIND::TEXT || row.kind == FORM_KIND::CHOICE
              || row.kind == FORM_KIND::INFO || row.kind == FORM_KIND::CUSTOM_CHOICE )
            && row.hits.size() >= 3 )
        {
            if( !row.hits[1].IsEmpty() )
            {
                dc.SetPen( wxPen( boxEdge ) );
                dc.SetBrush( wxBrush( Mix( block, *wxBLACK, 0.20 ) ) );
                dc.DrawRoundedRectangle( row.hits[1], FromDIP( 2 ) );
                drawEye( dc, row.hits[1], row.opts.eyeState && row.opts.eyeState(), fg, this );
            }

            if( !row.hits[2].IsEmpty() )
            {
                dc.SetPen( wxPen( boxEdge ) );
                dc.SetBrush( wxBrush( Mix( block, *wxBLACK, 0.20 ) ) );
                dc.DrawRoundedRectangle( row.hits[2], FromDIP( 2 ) );
                drawPicker( dc, row.hits[2], fg, this );
            }

            if( row.hits.size() >= 4 && !row.hits[3].IsEmpty() )
            {
                dc.SetPen( wxPen( boxEdge ) );
                dc.SetBrush( wxBrush( Mix( block, *wxBLACK, 0.20 ) ) );
                dc.DrawRoundedRectangle( row.hits[3], FromDIP( 2 ) );
                drawLock( dc, row.hits[3], row.opts.lockState && row.opts.lockState(), fg, this );
            }
        }
    }
}


// ------------------------------------------------------------------------------------ interaction

void PROPERTIES_FORM::onMotion( wxMouseEvent& aEvent )
{
    layoutRows( GetClientSize().x );

    int  hover = -1;
    bool hand = false;

    for( int idx = 0; idx < (int) m_rows.size(); ++idx )
    {
        const ROW& row = *m_rows[idx];

        if( !row.rect.Contains( aEvent.GetPosition() ) )
            continue;

        hover = idx;

        if( row.kind == FORM_KIND::LINK || row.kind == FORM_KIND::BUTTONS )
        {
            for( const wxRect& r : row.hits )
                hand |= r.Contains( aEvent.GetPosition() );
        }
    }

    SetCursor( hand ? wxCursor( wxCURSOR_HAND ) : wxNullCursor );

    if( hover != m_hoverRow )
    {
        m_hoverRow = hover;
        Refresh();
    }
    else if( hover >= 0 && m_rows[hover]->kind == FORM_KIND::BUTTONS )
    {
        Refresh();
    }

    aEvent.Skip();
}


void PROPERTIES_FORM::onLeftDown( wxMouseEvent& aEvent )
{
    if( m_editor )
        endEdit( true );

    layoutRows( GetClientSize().x );

    wxPoint pt = aEvent.GetPosition();

    for( ROW* rowPtr : m_rows )
    {
        ROW& row = *rowPtr;

        if( row.rect.IsEmpty() || !row.rect.Contains( pt ) )
            continue;

        wxPGProperty* pg = prop( row.prop );
        bool readOnly = row.opts.readOnly || ( pg && pg->HasFlag( wxPGFlags::ReadOnly ) );

        auto hit = [&]( size_t aIdx ) { return aIdx < row.hits.size() && row.hits[aIdx].Contains( pt ); };

        switch( row.kind )
        {
        case FORM_KIND::TEXT:
            if( hit( 3 ) && row.opts.lockToggle )
                row.opts.lockToggle();
            else if( hit( 1 ) && row.opts.eyeToggle )
                row.opts.eyeToggle();
            else if( hit( 2 ) && row.opts.picker )
                row.opts.picker();
            else if( hit( 0 ) && !readOnly )
                beginEdit( row.hits[0], row.prop, row.opts.lines > 1 );

            break;

        case FORM_KIND::CHOICE:
        case FORM_KIND::CUSTOM_CHOICE:
            if( hit( 3 ) && row.opts.lockToggle )
                row.opts.lockToggle();
            else if( hit( 1 ) && row.opts.eyeToggle )
                row.opts.eyeToggle();
            else if( hit( 2 ) && row.opts.picker )
                row.opts.picker();
            else if( hit( 0 ) && !readOnly )
                openChoice( row, row.hits[0] );

            break;

        case FORM_KIND::INFO:
            if( hit( 3 ) && row.opts.lockToggle )
                row.opts.lockToggle();
            else if( hit( 1 ) && row.opts.eyeToggle )
                row.opts.eyeToggle();
            else if( hit( 0 ) && row.editText && !row.opts.readOnly )
                beginTextEdit( row.hits[0], row.text ? roundedDistance( row.text() ) : wxString(), row.opts.lines > 1,
                               row.editText );

            break;

        case FORM_KIND::CHECK:
            if( pg && !readOnly && ( hit( 0 ) || row.boxFirst ) )
            {
                bool on = !pg->IsValueUnspecified() && pg->GetValue().GetBool();
                apply( row.prop, wxVariant( !on ) );
            }

            break;

        case FORM_KIND::CUSTOM_CHECK:
            if( row.setBool && !row.opts.readOnly && ( hit( 0 ) || row.boxFirst ) )
                row.setBool( !( row.getInt && row.getInt() == 1 ) );

            break;

        case FORM_KIND::SEGMENTED:
        case FORM_KIND::CUSTOM_SEGMENTED:
        {
            if( !hit( 0 ) || readOnly )
                break;

            const wxRect& bar = row.hits[0];
            std::vector<long> values = row.values;

            if( row.kind == FORM_KIND::SEGMENTED && values.empty() && pg )
            {
                wxPGChoices choices = pg->GetChoices();

                for( unsigned ii = 0; ii < choices.GetCount(); ++ii )
                    values.push_back( choices.GetValue( ii ) );
            }

            int n = row.kind == FORM_KIND::SEGMENTED ? (int) values.size() : (int) row.labels.size();

            if( n <= 0 )
                break;

            int seg = std::clamp( ( pt.x - bar.x ) * n / std::max( 1, bar.width ), 0, n - 1 );

            if( row.kind == FORM_KIND::SEGMENTED )
            {
                apply( row.prop, wxVariant( values[seg] ) );
            }
            else if( row.setInt && ( seg >= (int) row.enabled.size() || row.enabled[seg] ) )
            {
                row.setInt( seg );
            }

            break;
        }

        case FORM_KIND::XY:
            if( hit( 0 ) && !readOnly )
            {
                beginEdit( row.hits[0], row.prop, false );
            }
            else if( hit( 1 ) )
            {
                wxPGProperty* pgY = prop( row.prop2 );

                if( pgY && !pgY->HasFlag( wxPGFlags::ReadOnly ) && !row.opts.readOnly )
                    beginEdit( row.hits[1], row.prop2, false );
            }
            else if( hit( 2 ) )
            {
                if( wxPGProperty* lock = prop( row.lock ) )
                {
                    if( !lock->HasFlag( wxPGFlags::ReadOnly ) )
                    {
                        bool locked = !lock->IsValueUnspecified() && lock->GetValue().GetBool();
                        apply( row.lock, wxVariant( !locked ) );
                    }
                }
            }

            break;

        case FORM_KIND::TWIN:
            for( int ii = 0; ii < 2; ++ii )
            {
                const wxString& name = ii == 0 ? row.prop : row.prop2;
                wxPGProperty*   p = prop( name );

                if( hit( ii ) && p && !p->HasFlag( wxPGFlags::ReadOnly ) && !row.opts.readOnly )
                    beginEdit( row.hits[ii], name, false );
            }

            break;

        case FORM_KIND::LINK:
            if( hit( 0 ) && row.click )
                CallAfter( [this, rowPtr]() { if( rowPtr->click ) rowPtr->click(); } );

            break;

        case FORM_KIND::BUTTONS:
            for( size_t ii = 0; ii < row.buttons.size(); ++ii )
            {
                if( hit( ii ) && row.buttons[ii].second )
                {
                    std::function<void()> action = row.buttons[ii].second;
                    CallAfter( [action]() { action(); } );
                }
            }

            break;

        case FORM_KIND::TABLE:
            for( size_t rr = 0; rr < row.cells.size() && rr < row.cellRects.size(); ++rr )
            {
                const std::vector<wxRect>& rects = row.cellRects[rr];

                for( size_t cc = 0; cc < rects.size() && cc < row.cells[rr].size(); ++cc )
                {
                    if( !rects[cc].Contains( pt ) )
                        continue;

                    CELL cell = row.cells[rr][cc];

                    if( cell.heading )
                        break;

                    if( row.selected )
                        *row.selected = (int) rr;

                    wxPGProperty* pg = prop( cell.prop );

                    if( ( cell.link || cell.eye >= 0 ) && cell.click )
                    {
                        std::function<void()> action = cell.click;
                        CallAfter( [action]() { action(); } );
                    }
                    else if( cell.readOnly )
                    {
                        // just selects the row
                    }
                    else if( pg && !pg->HasFlag( wxPGFlags::ReadOnly ) )
                    {
                        if( pg->GetChoices().GetCount() > 0 && pg->GetValueType() != wxS( "bool" ) )
                            openPropChoice( cell.prop, rects[cc] );
                        else
                            beginEdit( rects[cc], cell.prop, false );
                    }
                    else if( !cell.choices.empty() && cell.choose )
                    {
                        int current = -1;

                        for( size_t ii = 0; ii < cell.choices.size(); ++ii )
                        {
                            if( cell.choices[ii] == cell.text )
                                current = (int) ii;
                        }

                        openMenu( cell.choices, current, rects[cc], cell.choose );
                    }
                    else if( cell.edit )
                    {
                        beginTextEdit( rects[cc], cell.text, false, cell.edit );
                    }

                    break;
                }
            }

            break;

        default:
            break;
        }

        Refresh();
        return;
    }
}


void PROPERTIES_FORM::openChoice( ROW& aRow, const wxRect& aBox )
{
    if( aRow.kind == FORM_KIND::CUSTOM_CHOICE )
    {
        if( aRow.opts.choices )
            aRow.labels = aRow.opts.choices();

        int current = aRow.getInt ? aRow.getInt() : -1;

        openMenu( aRow.labels, current, aBox,
                  [&aRow, current]( int aIdx )
                  {
                      if( aRow.setInt && aIdx != current )
                          aRow.setInt( aIdx );
                  } );
    }
    else
    {
        openPropChoice( aRow.prop, aBox );
    }
}


void PROPERTIES_FORM::openPropChoice( const wxString& aProp, const wxRect& aBox )
{
    wxPGProperty* pg = prop( aProp );

    if( !pg )
        return;

    wxPGChoices           choices = pg->GetChoices();
    long                  value = pg->IsValueUnspecified() ? LONG_MIN : pg->GetValue().GetLong();
    std::vector<wxString> labels;
    std::vector<long>     values;
    int                   current = -1;

    for( unsigned ii = 0; ii < choices.GetCount(); ++ii )
    {
        labels.push_back( choices.GetLabel( ii ) );
        values.push_back( choices.GetValue( ii ) );

        if( choices.GetValue( ii ) == value )
            current = (int) ii;
    }

    openMenu( labels, current, aBox,
              [this, aProp, values, current]( int aIdx )
              {
                  if( aIdx != current && aIdx >= 0 && aIdx < (int) values.size() )
                      apply( aProp, wxVariant( values[aIdx] ) );
              } );
}


void PROPERTIES_FORM::openMenu( const std::vector<wxString>& aLabels, int aCurrent, const wxRect& aBox,
                                std::function<void( int )> aChoose )
{
    if( aLabels.empty() )
        return;

    wxMenu menu;

    for( size_t ii = 0; ii < aLabels.size(); ++ii )
        menu.AppendCheckItem( wxID_HIGHEST + 1 + (int) ii, aLabels[ii] )->Check( (int) ii == aCurrent );

    int id = GetPopupMenuSelectionFromUser( menu, aBox.GetBottomLeft() );

    if( id <= wxID_HIGHEST || id > wxID_HIGHEST + (int) aLabels.size() )
        return;

    if( aChoose )
        aChoose( id - wxID_HIGHEST - 1 );
}


bool PROPERTIES_FORM::ParseValue( wxPGProperty* aProp, const wxString& aText, wxVariant& aValue,
                                  wxWindow* aContext )
{
    if( !aProp )
        return false;

    // The frame whose units and origin the values use (a floating panel's window is owned by it)
    EDA_DRAW_FRAME* frame = nullptr;

    for( wxWindow* win = aContext; win && !frame; win = win->GetParent() )
        frame = dynamic_cast<EDA_DRAW_FRAME*>( win );

    wxString text = aText;
    text.Trim( true ).Trim( false );

    const wxVariant& current = aProp->GetValue();

    if( dynamic_cast<PGPROPERTY_ANGLE*>( aProp ) )
    {
        double degrees = EDA_UNIT_UTILS::UI::DoubleValueFromString( unityScale, EDA_UNITS::DEGREES,
                                                                    text );

        if( current.GetType() == wxT( "EDA_ANGLE" ) )
            aValue = wxVariant( new EDA_ANGLE_VARIANT_DATA( EDA_ANGLE( degrees, DEGREES_T ) ) );
        else
            aValue = degrees * static_cast<PGPROPERTY_ANGLE*>( aProp )->GetScale();

        return true;
    }

    if( !frame )
        return aProp->StringToValue( aValue, aText );

    if( dynamic_cast<PGPROPERTY_AREA*>( aProp ) )
    {
        aValue = wxLongLong( (wxLongLong_t) frame->ValueFromString( text, EDA_DATA_TYPE::AREA ) );
        return true;
    }

    if( PGPROPERTY_DISTANCE* distance = dynamic_cast<PGPROPERTY_DISTANCE*>( aProp ) )
    {
        // An optional value (e.g. a clearance override) is cleared by emptying it
        if( current.GetType() == wxT( "std::optional<int>" ) )
        {
            if( text.IsEmpty() )
            {
                aValue = wxVariant( std::optional<int>() );
                return true;
            }
        }
        else if( text.IsEmpty() )
        {
            return false;
        }

        int value = frame->ValueFromString( text );
        value = frame->GetOriginTransforms().FromDisplay( value, distance->CoordType() );

        if( current.GetType() == wxT( "std::optional<int>" ) )
            aValue = wxVariant( std::optional<int>( value ) );
        else
            aValue = (long) value;

        return true;
    }

    return aProp->StringToValue( aValue, aText );
}


void PROPERTIES_FORM::beginEdit( const wxRect& aBox, const wxString& aProp, bool aMultiline )
{
    if( !prop( aProp ) )
        return;

    beginTextEdit( aBox, valueText( aProp ), aMultiline,
                   [this, aProp]( const wxString& aText )
                   {
                       if( wxPGProperty* pg = prop( aProp ) )
                       {
                           wxVariant value;

                           if( ParseValue( pg, aText, value, this ) )
                               apply( aProp, value );
                       }
                   } );
}


void PROPERTIES_FORM::beginTextEdit( const wxRect& aBox, const wxString& aInitial, bool aMultiline,
                                     std::function<void( const wxString& )> aCommit )
{
    // Finish (and keep) an edit still open in another field before starting this one
    if( m_editor )
        endEdit( true );

    m_editCommit = std::move( aCommit );
    m_editStart = aInitial;

    wxColour parentBg = GetParent()->GetBackgroundColour();
    wxColour fg = wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOWTEXT );
    wxColour boxFill = Mix( Mix( parentBg, fg, 0.06 ), *wxBLACK, 0.30 );

    long style = wxTE_PROCESS_ENTER | wxBORDER_NONE | ( aMultiline ? wxTE_MULTILINE : 0 );
    wxRect inner = aBox.Deflate( FromDIP( 3 ), 1 );

    m_editor = new wxTextCtrl( this, wxID_ANY, m_editStart, inner.GetPosition(), inner.GetSize(), style );
    m_editor->SetBackgroundColour( boxFill );
    m_editor->SetForegroundColour( fg );
    m_editor->SetFont( GetFont() );
    m_editor->SetFocus();
    m_editor->SelectAll();

    // Enter / Esc finish typing: give the keyboard back to the canvas, as Altium does, so the
    // next Ctrl+Z or hotkey acts on the design without a click first
    auto backToCanvas =
            [this]()
            {
                for( wxWindow* win = GetParent(); win; win = win->GetParent() )
                {
                    if( EDA_DRAW_FRAME* frame = dynamic_cast<EDA_DRAW_FRAME*>( win ) )
                    {
                        if( frame->GetCanvas() )
                            frame->GetCanvas()->SetFocus();

                        return;
                    }
                }
            };

    m_editor->Bind( wxEVT_TEXT_ENTER,
                    [this, backToCanvas]( wxCommandEvent& )
                    {
                        endEdit( true );
                        CallAfter( backToCanvas );
                    } );
    // Losing focus ends this editor only: by the time the deferred call runs, a click may
    // already have opened another field's editor, which must stay open
    wxTextCtrl* editor = m_editor;

    m_editor->Bind( wxEVT_KILL_FOCUS,
                    [this, editor]( wxFocusEvent& aEvent )
                    {
                        aEvent.Skip();
                        CallAfter( [this, editor]()
                                   {
                                       if( m_editor == editor )
                                           endEdit( true );
                                   } );
                    } );
    m_editor->Bind( wxEVT_CHAR_HOOK,
                    [this, backToCanvas]( wxKeyEvent& aEvent )
                    {
                        if( aEvent.GetKeyCode() == WXK_ESCAPE )
                        {
                            endEdit( false );
                            CallAfter( backToCanvas );
                        }
                        else if( aEvent.GetKeyCode() == WXK_TAB )
                            endEdit( true );
                        else
                            aEvent.Skip();
                    } );
}


void PROPERTIES_FORM::endEdit( bool aCommit )
{
    if( !m_editor || m_ending )
        return;

    m_ending = true;

    wxTextCtrl*                            editor = m_editor;
    wxString                               text = editor->GetValue();
    std::function<void( const wxString& )> commit = std::move( m_editCommit );

    m_editor = nullptr;
    m_editCommit = nullptr;
    editor->Hide();
    CallAfter( [editor]() { editor->Destroy(); } );

    if( aCommit && text != m_editStart && commit )
        commit( text );

    m_ending = false;
    Refresh();
}
