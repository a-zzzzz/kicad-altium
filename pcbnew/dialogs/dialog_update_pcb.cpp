/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
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

#include <pcb_edit_frame.h>
#include <pcbnew_settings.h>
#include <dialog_update_pcb.h>
#include <widgets/wx_html_report_panel.h>
#include <ratsnest/ratsnest_data.h>
#include <netlist_reader/pcb_netlist.h>
#include <netlist_reader/board_netlist_updater.h>
#include <pcb_group.h>
#include <string_utils.h>
#include <tool/tool_manager.h>
#include <tools/pcb_actions.h>
#include <tools/pcb_selection_tool.h>
#include <view/view_controls.h>
#include <kiface_base.h>
#include <kiplatform/ui.h>
#include <wildcards_and_files_ext.h>
#include <messages_store.h>
#include <widgets/messages_panel.h>

#include <functional>

#include <wx/button.h>
#include <wx/control.h>
#include <wx/panel.h>
#include <wx/checkbox.h>
#include <wx/dcbuffer.h>
#include <wx/ffile.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/hyperlink.h>
#include <wx/stattext.h>
#include <wx/graphics.h>
#include <wx/menu.h>
#include <wx/regex.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/textctrl.h>
#include <wx/vscroll.h>


namespace
{
/// Change groups, in display order
enum ECO_GROUP_ID
{
    G_ADD_COMPONENTS,
    G_ADD_NETS,
    G_ADD_PINS,
    G_CHANGE_FOOTPRINTS,
    G_CHANGE_DESIGNATORS,
    G_CHANGE_COMMENTS,
    G_CHANGE_PARAMETERS,
    G_REMOVE_PARAMETERS,
    G_CHANGE_PROPERTIES,
    G_CHANGE_CLASSES,
    G_CHANGE_GROUPS,
    G_CHANGE_VARIANTS,
    G_CHANGE_PINS,
    G_CHANGE_ROUTED,
    G_REMOVE_PINS,
    G_REMOVE_COMPONENTS,
    G_REMOVE_NETS,
    G_PROBLEMS,
    G_COUNT
};


enum ECO_COLUMN
{
    C_ENABLE,
    C_ACTION,
    C_OBJECT,
    C_TO,
    C_DOCUMENT,
    C_CHECK,
    C_DONE,
    C_MESSAGE,
    C_COUNT
};


/// Collects the updater's messages
class COLLECTING_REPORTER : public REPORTER
{
public:
    explicit COLLECTING_REPORTER( std::vector<std::pair<wxString, SEVERITY>>& aLines ) :
            m_lines( aLines )
    {}

    REPORTER& Report( const wxString& aText, SEVERITY aSeverity ) override
    {
        m_lines.emplace_back( aText, aSeverity );
        return REPORTER::Report( aText, aSeverity );
    }

    // The tail only holds the totals
    REPORTER& ReportTail( const wxString& aText, SEVERITY aSeverity ) override { return *this; }

private:
    std::vector<std::pair<wxString, SEVERITY>>& m_lines;
};


bool isChangeSeverity( SEVERITY aSeverity )
{
    return aSeverity == RPT_SEVERITY_ACTION || aSeverity == RPT_SEVERITY_WARNING
           || aSeverity == RPT_SEVERITY_ERROR;
}


/// An updater message in present tense ("Added R1" and "Add R1" name the same change)
wxString normalise( const wxString& aText )
{
    wxString s = UnescapeHTML( aText );
    s.Trim( true ).Trim( false );

    static const std::vector<std::pair<wxString, wxString>> tenses = {
        { wxS( "Added " ), wxS( "Add " ) },
        { wxS( "Changed " ), wxS( "Change " ) },
        { wxS( "Removed " ), wxS( "Remove " ) },
        { wxS( "Connected " ), wxS( "Connect " ) },
        { wxS( "Reconnected " ), wxS( "Reconnect " ) },
        { wxS( "Disconnected " ), wxS( "Disconnect " ) },
        { wxS( "Updated " ), wxS( "Update " ) },
        { wxS( "Could not " ), wxS( "Cannot " ) },
    };

    for( const auto& [past, present] : tenses )
    {
        if( s.StartsWith( past ) )
            return present + s.Mid( past.length() );
    }

    return s;
}


/// The reason given in an updater warning / error: the text in its last brackets, if any
wxString reasonOf( const wxString& aText )
{
    wxString s = UnescapeHTML( aText );
    s.Trim();

    if( s.EndsWith( wxS( "." ) ) )
        s.RemoveLast();

    if( s.EndsWith( wxS( ")" ) ) )
    {
        int open = s.Find( '(', true );

        if( open != wxNOT_FOUND )
            return s.Mid( open + 1, s.length() - open - 2 );
    }

    return s;
}


wxString stripDot( wxString aText )
{
    aText.Trim();

    if( aText.EndsWith( wxS( "." ) ) )
        aText.RemoveLast();

    return aText;
}


/// Altium's dark list colours, falling back on the system colours in a light theme
struct ECO_COLOURS
{
    wxColour bg, fg, dim, sel, selText, line, header, ok, warning, error, box;

    ECO_COLOURS()
    {
        bg = wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOW );
        fg = wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOWTEXT );
        dim = wxSystemSettings::GetColour( wxSYS_COLOUR_GRAYTEXT );

        bool dark = bg.GetLuminance() < 0.5;

        sel = dark ? wxColour( 0x62, 0x83, 0xA2 ) : wxSystemSettings::GetColour( wxSYS_COLOUR_HIGHLIGHT );
        selText = dark ? *wxWHITE : wxSystemSettings::GetColour( wxSYS_COLOUR_HIGHLIGHTTEXT );
        line = bg.ChangeLightness( dark ? 118 : 92 );
        header = wxSystemSettings::GetColour( wxSYS_COLOUR_BTNFACE ).ChangeLightness( dark ? 112 : 97 );
        box = dark ? wxColour( 0xB0, 0xB0, 0xB0 ) : wxColour( 0x60, 0x60, 0x60 );
        ok = wxColour( 0x4C, 0xAF, 0x50 );
        warning = wxColour( 0xE8, 0xA3, 0x17 );
        error = wxColour( 0xE0, 0x4B, 0x4B );
    }
};


/// A check box drawn to suit the dark list (native boxes stay light in dark mode).
/// aState: 0 = clear, 1 = ticked, 2 = mixed
void drawCheckBox( wxGraphicsContext* aGc, const wxRect& aRect, int aState, bool aEnabled,
                   const ECO_COLOURS& aColours )
{
    wxColour border = aEnabled ? aColours.box : aColours.dim.ChangeLightness( 80 );
    wxColour mark = aEnabled ? aColours.fg : aColours.dim;

    aGc->SetPen( wxPen( border, 1 ) );
    aGc->SetBrush( wxBrush( aColours.bg.ChangeLightness( 85 ) ) );
    aGc->DrawRectangle( aRect.x + 0.5, aRect.y + 0.5, aRect.width - 1, aRect.height - 1 );

    if( aState == 1 )
    {
        wxGraphicsPath path = aGc->CreatePath();
        double         w = aRect.width;
        path.MoveToPoint( aRect.x + w * 0.22, aRect.y + w * 0.52 );
        path.AddLineToPoint( aRect.x + w * 0.42, aRect.y + w * 0.72 );
        path.AddLineToPoint( aRect.x + w * 0.80, aRect.y + w * 0.28 );
        aGc->SetPen( wxPen( mark, std::max( 2, aRect.width / 7 ) ) );
        aGc->StrokePath( path );
    }
    else if( aState == 2 )
    {
        aGc->SetPen( *wxTRANSPARENT_PEN );
        aGc->SetBrush( wxBrush( mark ) );
        aGc->DrawRectangle( aRect.x + aRect.width * 0.28, aRect.y + aRect.height * 0.28,
                            aRect.width * 0.44, aRect.height * 0.44 );
    }
}


/// Status symbols: a green tick, an amber warning triangle or a red cross
void drawStatus( wxGraphicsContext* aGc, const wxRect& aRect, int aStatus,
                 const ECO_COLOURS& aColours )
{
    double d = std::min( aRect.width, aRect.height );
    double x = aRect.x + ( aRect.width - d ) / 2.0;
    double y = aRect.y + ( aRect.height - d ) / 2.0;

    if( aStatus == DIALOG_UPDATE_PCB::STATUS_WARNING )
    {
        wxGraphicsPath tri = aGc->CreatePath();
        tri.MoveToPoint( x + d / 2, y );
        tri.AddLineToPoint( x + d, y + d );
        tri.AddLineToPoint( x, y + d );
        tri.CloseSubpath();
        aGc->SetPen( *wxTRANSPARENT_PEN );
        aGc->SetBrush( wxBrush( aColours.warning ) );
        aGc->FillPath( tri );

        aGc->SetPen( wxPen( wxColour( 0x30, 0x30, 0x30 ), std::max( 1.5, d / 9 ) ) );
        aGc->StrokeLine( x + d / 2, y + d * 0.35, x + d / 2, y + d * 0.68 );
        aGc->StrokeLine( x + d / 2, y + d * 0.80, x + d / 2, y + d * 0.86 );
        return;
    }

    wxColour fill = aStatus == DIALOG_UPDATE_PCB::STATUS_OK ? aColours.ok : aColours.error;

    aGc->SetPen( *wxTRANSPARENT_PEN );
    aGc->SetBrush( wxBrush( fill ) );
    aGc->DrawEllipse( x, y, d, d );

    aGc->SetPen( wxPen( *wxWHITE, std::max( 1.5, d / 8 ) ) );

    if( aStatus == DIALOG_UPDATE_PCB::STATUS_OK )
    {
        wxGraphicsPath path = aGc->CreatePath();
        path.MoveToPoint( x + d * 0.26, y + d * 0.52 );
        path.AddLineToPoint( x + d * 0.43, y + d * 0.69 );
        path.AddLineToPoint( x + d * 0.75, y + d * 0.33 );
        aGc->StrokePath( path );
    }
    else
    {
        aGc->StrokeLine( x + d * 0.32, y + d * 0.32, x + d * 0.68, y + d * 0.68 );
        aGc->StrokeLine( x + d * 0.68, y + d * 0.32, x + d * 0.32, y + d * 0.68 );
    }
}


/// Row icons (own drawings): what a change is about
enum ECO_ICON
{
    ICON_FOLDER,
    ICON_COMPONENT,
    ICON_NET,
    ICON_BOARD,
    ICON_OTHER
};


void drawIcon( wxGraphicsContext* aGc, const wxRect& aRect, ECO_ICON aIcon )
{
    double x = aRect.x;
    double y = aRect.y;
    double w = aRect.width;
    double h = aRect.height;

    switch( aIcon )
    {
    case ICON_FOLDER:
    {
        // An orange folder: tab on the top left, body below
        aGc->SetPen( *wxTRANSPARENT_PEN );
        aGc->SetBrush( wxBrush( wxColour( 0xC8, 0x6A, 0x2E ) ) );
        aGc->DrawRectangle( x + w * 0.05, y + h * 0.18, w * 0.42, h * 0.2 );
        aGc->SetBrush( wxBrush( wxColour( 0xF0, 0x8A, 0x4B ) ) );
        aGc->DrawRectangle( x + w * 0.05, y + h * 0.3, w * 0.9, h * 0.58 );
        break;
    }

    case ICON_COMPONENT:
    {
        // A yellow chip body with pins on both sides
        aGc->SetPen( *wxTRANSPARENT_PEN );
        aGc->SetBrush( wxBrush( wxColour( 0x9A, 0x86, 0x40 ) ) );

        for( int i = 0; i < 4; ++i )
        {
            double py = y + h * ( 0.14 + i * 0.2 );
            aGc->DrawRectangle( x + w * 0.12, py, w * 0.76, h * 0.08 );
        }

        aGc->SetBrush( wxBrush( wxColour( 0xF2, 0xC8, 0x5B ) ) );
        aGc->DrawRectangle( x + w * 0.3, y + h * 0.05, w * 0.4, h * 0.9 );
        break;
    }

    case ICON_NET:
    {
        // Two blue wavy wires
        aGc->SetPen( wxPen( wxColour( 0x4F, 0x9C, 0xF0 ), std::max( 1.5, h / 9 ) ) );

        for( double row : { 0.35, 0.68 } )
        {
            wxGraphicsPath p = aGc->CreatePath();
            double         cy = y + h * row;
            p.MoveToPoint( x + w * 0.05, cy );
            p.AddCurveToPoint( x + w * 0.3, cy - h * 0.18, x + w * 0.45, cy + h * 0.18,
                               x + w * 0.65, cy );
            p.AddLineToPoint( x + w * 0.95, cy );
            aGc->StrokePath( p );
        }

        break;
    }

    case ICON_BOARD:
    {
        // A green board with two traces and a pad
        aGc->SetPen( wxPen( wxColour( 0x1E, 0x7A, 0x3C ), 1 ) );
        aGc->SetBrush( wxBrush( wxColour( 0x2E, 0xA0, 0x55 ) ) );
        aGc->DrawRectangle( x + w * 0.06, y + h * 0.16, w * 0.88, h * 0.68 );
        aGc->SetPen( wxPen( wxColour( 0xE8, 0xE8, 0xE8 ), 1 ) );
        aGc->StrokeLine( x + w * 0.2, y + h * 0.36, x + w * 0.6, y + h * 0.36 );
        aGc->StrokeLine( x + w * 0.2, y + h * 0.62, x + w * 0.5, y + h * 0.62 );
        aGc->SetPen( *wxTRANSPARENT_PEN );
        aGc->SetBrush( wxBrush( wxColour( 0xE8, 0xE8, 0xE8 ) ) );
        aGc->DrawRectangle( x + w * 0.64, y + h * 0.28, w * 0.18, h * 0.42 );
        break;
    }

    default:
    {
        // A grey document page
        aGc->SetPen( wxPen( wxColour( 0x90, 0x90, 0x90 ), 1 ) );
        aGc->SetBrush( wxBrush( wxColour( 0xD8, 0xD8, 0xD8 ) ) );
        aGc->DrawRectangle( x + w * 0.22, y + h * 0.08, w * 0.56, h * 0.84 );
        break;
    }
    }
}


/// The icon of a change group's rows
ECO_ICON iconForGroup( int aGroup )
{
    switch( aGroup )
    {
    case G_ADD_NETS:
    case G_REMOVE_NETS:
    case G_ADD_PINS:
    case G_REMOVE_PINS:
    case G_CHANGE_PINS:
    case G_CHANGE_ROUTED:
        return ICON_NET;

    case G_PROBLEMS:
    case G_CHANGE_VARIANTS:
        return ICON_OTHER;

    default:
        return ICON_COMPONENT;
    }
}
} // namespace


/**
 * The Modifications / Status table: groups of changes with an expander and an Enable box,
 * drawn by hand so group rows can span the columns as in Altium.
 */
class ECO_LIST : public wxVScrolledWindow
{
public:
    ECO_LIST( wxWindow* aParent, DIALOG_UPDATE_PCB* aDialog ) :
            wxVScrolledWindow( aParent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                               wxWANTS_CHARS | wxBORDER_NONE ),
            m_dialog( aDialog ),
            m_selected( -1 ),
            m_onlyErrors( false ),
            m_tipRow( -2 )
    {
        SetBackgroundStyle( wxBG_STYLE_PAINT );
        SetBackgroundColour( ECO_COLOURS().bg );
        m_rowHeight = FromDIP( 22 );

        Bind( wxEVT_PAINT, &ECO_LIST::onPaint, this );
        Bind( wxEVT_LEFT_DOWN, &ECO_LIST::onLeftDown, this );
        Bind( wxEVT_LEFT_DCLICK, &ECO_LIST::onDClick, this );
        Bind( wxEVT_MOTION, &ECO_LIST::onMotion, this );
        Bind( wxEVT_KEY_DOWN, &ECO_LIST::onKey, this );
        Bind( wxEVT_SIZE,
              [this]( wxSizeEvent& aEvent )
              {
                  Refresh();

                  if( m_header )
                      m_header->Refresh();

                  aEvent.Skip();
              } );
    }

    void SetHeader( wxWindow* aHeader ) { m_header = aHeader; }

    void SetOnlyErrors( bool aOnly )
    {
        m_onlyErrors = aOnly;
        m_selected = -1;
        Rebuild();
    }

    /// Recompute the shown rows (after a change of rows, expansion or filter)
    void Rebuild()
    {
        m_entries.clear();

        const auto& groups = m_dialog->Groups();
        const auto& rows = m_dialog->Rows();

        for( int g = 0; g < (int) groups.size(); ++g )
        {
            std::vector<int> shown;

            for( int r : groups[g].rows )
            {
                if( !m_onlyErrors || isProblem( rows[r] ) )
                    shown.push_back( r );
            }

            if( shown.empty() )
                continue;

            m_entries.push_back( { g, -1 } );

            if( groups[g].expanded )
            {
                for( int r : shown )
                    m_entries.push_back( { g, r } );
            }
        }

        if( m_selected >= (int) m_entries.size() )
            m_selected = (int) m_entries.size() - 1;

        SetRowCount( m_entries.size() );
        Refresh();
    }

    /// Left edge of each column (and the right edge of the last) for the current width
    std::vector<int> Columns() const
    {
        static const int dips[C_COUNT] = { 76, 84, 230, 50, 190, 50, 50, 200 };

        std::vector<int> widths( C_COUNT );
        int              total = 0;

        for( int c = 0; c < C_COUNT; ++c )
        {
            widths[c] = FromDIP( dips[c] );
            total += widths[c];
        }

        int width = GetClientSize().x;

        // The Message column takes what is left; Affected Object / Document give way first
        if( width > total )
        {
            widths[C_MESSAGE] += width - total;
        }
        else
        {
            int excess = total - width;

            for( int c : { C_MESSAGE, C_OBJECT, C_DOCUMENT } )
            {
                int give = std::min( excess, widths[c] - FromDIP( 80 ) );

                if( give > 0 )
                {
                    widths[c] -= give;
                    excess -= give;
                }
            }
        }

        std::vector<int> x( C_COUNT + 1, 0 );

        for( int c = 0; c < C_COUNT; ++c )
            x[c + 1] = x[c] + widths[c];

        return x;
    }

private:
    struct ENTRY
    {
        int group;
        int row;    ///< -1: the group's own row
    };

    static bool isProblem( const DIALOG_UPDATE_PCB::ECO_ROW& aRow )
    {
        return aRow.check >= DIALOG_UPDATE_PCB::STATUS_WARNING
               || aRow.done >= DIALOG_UPDATE_PCB::STATUS_WARNING;
    }

    wxCoord OnGetRowHeight( size_t aRow ) const override { return m_rowHeight; }

    int expanderWidth() const { return FromDIP( 16 ); }
    int boxSize() const { return FromDIP( 13 ); }

    wxRect boxRect( const ENTRY& aEntry, int aTop ) const
    {
        int x = FromDIP( 4 ) + expanderWidth() + ( aEntry.row >= 0 ? FromDIP( 14 ) : 0 );
        return wxRect( x, aTop + ( m_rowHeight - boxSize() ) / 2, boxSize(), boxSize() );
    }

    int groupState( int aGroup ) const
    {
        const auto& rows = m_dialog->Rows();
        int         on = 0;
        int         count = (int) m_dialog->Groups()[aGroup].rows.size();

        for( int r : m_dialog->Groups()[aGroup].rows )
            on += rows[r].enabled ? 1 : 0;

        return on == 0 ? 0 : ( on == count ? 1 : 2 );
    }

    void onPaint( wxPaintEvent& aEvent )
    {
        wxBufferedPaintDC dc( this );
        ECO_COLOURS       colours;

        dc.SetBackground( wxBrush( colours.bg ) );
        dc.Clear();
        dc.SetFont( GetFont() );

        std::vector<int> cols = Columns();
        size_t           first = GetVisibleRowsBegin();
        size_t           last = std::min( GetVisibleRowsEnd(), m_entries.size() );
        int              width = GetClientSize().x;
        int              pad = FromDIP( 5 );

        const auto& groups = m_dialog->Groups();
        const auto& rows = m_dialog->Rows();

        // Shapes are drawn after the text, through a graphics context (anti-aliased)
        std::vector<std::function<void( wxGraphicsContext* )>> shapes;

        int iconSide = FromDIP( 16 );

        auto text =
                [&]( const wxString& aText, int aCol, int aTop, const wxColour& aColour,
                     int aRight = -1, int aIndent = 0 )
                {
                    int left = cols[aCol] + pad + aIndent;
                    int right = ( aRight >= 0 ? aRight : cols[aCol + 1] ) - pad;

                    if( right <= left || aText.IsEmpty() )
                        return;

                    wxString shown = wxControl::Ellipsize( aText, dc, wxELLIPSIZE_END,
                                                           right - left );
                    int      h = dc.GetCharHeight();
                    dc.SetTextForeground( aColour );
                    dc.DrawText( shown, left, aTop + ( m_rowHeight - h ) / 2 );
                };

        for( size_t i = first; i < last; ++i )
        {
            const ENTRY& entry = m_entries[i];
            int          top = (int) ( i - first ) * m_rowHeight;
            bool         selected = (int) i == m_selected;
            wxColour     fg = selected ? colours.selText : colours.fg;

            if( selected )
            {
                dc.SetPen( *wxTRANSPARENT_PEN );
                dc.SetBrush( wxBrush( colours.sel ) );
                dc.DrawRectangle( 0, top, width, m_rowHeight );
            }

            dc.SetPen( wxPen( colours.line ) );
            dc.DrawLine( 0, top + m_rowHeight - 1, width, top + m_rowHeight - 1 );

            const DIALOG_UPDATE_PCB::ECO_GROUP& group = groups[entry.group];
            bool fixed = group.mode == DIALOG_UPDATE_PCB::ENABLE_FIXED;
            wxRect box = boxRect( entry, top );

            if( entry.row < 0 )
            {
                // Expander, Enable box and the group title across the Modifications columns
                bool expanded = group.expanded;
                int  ex = FromDIP( 4 );
                int  cy = top + m_rowHeight / 2;
                int  s = FromDIP( 4 );
                wxColour arrow = selected ? colours.selText : colours.dim;

                shapes.push_back(
                        [=]( wxGraphicsContext* gc )
                        {
                            wxGraphicsPath p = gc->CreatePath();

                            if( expanded )
                            {
                                p.MoveToPoint( ex + 2, cy - s / 2 );
                                p.AddLineToPoint( ex + 2 + 2 * s, cy - s / 2 );
                                p.AddLineToPoint( ex + 2 + s, cy + s / 2 + 1 );
                            }
                            else
                            {
                                p.MoveToPoint( ex + 4, cy - s );
                                p.AddLineToPoint( ex + 4 + s, cy );
                                p.AddLineToPoint( ex + 4, cy + s );
                            }

                            p.CloseSubpath();
                            gc->SetPen( *wxTRANSPARENT_PEN );
                            gc->SetBrush( wxBrush( arrow ) );
                            gc->FillPath( p );
                        } );

                int state = groupState( entry.group );

                if( entry.group != G_PROBLEMS )     // problems are not changes: no Enable box
                    shapes.push_back(
                        [=]( wxGraphicsContext* gc )
                        {
                            drawCheckBox( gc, box, state, !fixed, colours );
                        } );

                wxRect folder( cols[C_ACTION] + pad, top + ( m_rowHeight - iconSide ) / 2,
                               iconSide, iconSide );

                shapes.push_back(
                        [=]( wxGraphicsContext* gc )
                        {
                            drawIcon( gc, folder, ICON_FOLDER );
                        } );

                wxString title = wxString::Format( wxS( "%s(%d)" ), group.title,
                                                   (int) group.rows.size() );
                text( title, C_ACTION, top, fg, cols[C_CHECK], iconSide + pad );
                continue;
            }

            const DIALOG_UPDATE_PCB::ECO_ROW& row = rows[entry.row];
            bool problemRow = entry.group == G_PROBLEMS;

            if( !problemRow )
            {
                int  state = row.enabled ? 1 : 0;
                shapes.push_back(
                        [=]( wxGraphicsContext* gc )
                        {
                            drawCheckBox( gc, box, state, !fixed, colours );
                        } );
            }

            wxColour rowFg = row.enabled || selected ? fg : colours.dim;

            text( row.action, C_ACTION, top, rowFg );

            // The object and the document with their icons, as in Altium
            wxRect   objIcon( cols[C_OBJECT] + pad, top + ( m_rowHeight - iconSide ) / 2,
                              iconSide, iconSide );
            ECO_ICON objKind = iconForGroup( entry.group );

            shapes.push_back(
                    [=]( wxGraphicsContext* gc )
                    {
                        drawIcon( gc, objIcon, objKind );
                    } );

            text( row.object, C_OBJECT, top, rowFg, problemRow ? cols[C_CHECK] : -1,
                  iconSide + FromDIP( 3 ) );

            if( !problemRow )
            {
                text( _( "To" ), C_TO, top, rowFg );

                wxRect docIcon( cols[C_DOCUMENT] + pad, top + ( m_rowHeight - iconSide ) / 2,
                                iconSide, iconSide );

                shapes.push_back(
                        [=]( wxGraphicsContext* gc )
                        {
                            drawIcon( gc, docIcon, ICON_BOARD );
                        } );

                text( m_dialog->DocumentName(), C_DOCUMENT, top, rowFg, -1,
                      iconSide + FromDIP( 3 ) );
            }

            int iconSize = FromDIP( 14 );

            for( auto [col, status] : { std::pair<int, int>( C_CHECK, row.check ),
                                        std::pair<int, int>( C_DONE, row.done ) } )
            {
                if( status == DIALOG_UPDATE_PCB::STATUS_NONE )
                    continue;

                wxRect r( cols[col] + ( cols[col + 1] - cols[col] - iconSize ) / 2,
                          top + ( m_rowHeight - iconSize ) / 2, iconSize, iconSize );
                int    st = status;

                shapes.push_back(
                        [=]( wxGraphicsContext* gc )
                        {
                            drawStatus( gc, r, st, colours );
                        } );
            }

            text( row.message, C_MESSAGE, top, fg );
        }

        // Column separators of the status area
        dc.SetPen( wxPen( colours.line ) );
        int bottom = (int) ( last - first ) * m_rowHeight;
        dc.DrawLine( cols[C_CHECK], 0, cols[C_CHECK], bottom );

        if( wxGraphicsContext* gc = wxGraphicsContext::Create( dc ) )
        {
            gc->SetAntialiasMode( wxANTIALIAS_DEFAULT );

            for( auto& shape : shapes )
                shape( gc );

            delete gc;
        }
    }

    int entryAt( const wxPoint& aPos ) const
    {
        int row = VirtualHitTest( aPos.y );
        return row >= 0 && row < (int) m_entries.size() ? row : -1;
    }

    void select( int aRow )
    {
        if( aRow < 0 || aRow >= (int) m_entries.size() )
            return;

        m_selected = aRow;

        if( aRow < (int) GetVisibleRowsBegin() || aRow >= (int) GetVisibleRowsEnd() - 1 )
            ScrollToRow( std::max( 0, aRow - 2 ) );

        Refresh();
    }

    void onLeftDown( wxMouseEvent& aEvent )
    {
        SetFocus();

        int idx = entryAt( aEvent.GetPosition() );

        if( idx < 0 )
            return;

        ENTRY entry = m_entries[idx];
        int   top = (int) ( idx - GetVisibleRowsBegin() ) * m_rowHeight;
        int   x = aEvent.GetPosition().x;

        m_selected = idx;

        wxRect box = boxRect( entry, top ).Inflate( FromDIP( 3 ) );

        if( entry.row < 0 && x < box.GetLeft() )
            m_dialog->SetGroupExpanded( entry.group, !m_dialog->Groups()[entry.group].expanded );
        else if( entry.group != G_PROBLEMS && box.Contains( aEvent.GetPosition() ) )
            m_dialog->ToggleEnable( entry.group, entry.row );

        Refresh();
    }

    void onDClick( wxMouseEvent& aEvent )
    {
        int idx = entryAt( aEvent.GetPosition() );

        if( idx >= 0 && m_entries[idx].row < 0 )
        {
            int group = m_entries[idx].group;
            m_dialog->SetGroupExpanded( group, !m_dialog->Groups()[group].expanded );
        }
        else
        {
            onLeftDown( aEvent );
        }
    }

    void onMotion( wxMouseEvent& aEvent )
    {
        int idx = entryAt( aEvent.GetPosition() );

        if( idx == m_tipRow )
            return;

        m_tipRow = idx;

        if( idx >= 0 && m_entries[idx].row >= 0 )
        {
            const auto& row = m_dialog->Rows()[m_entries[idx].row];
            wxString    tip = UnescapeHTML( row.text );

            if( !row.message.IsEmpty() && !tip.Contains( row.message ) )
                tip << wxS( "\n" ) << row.message;

            SetToolTip( tip );
        }
        else
        {
            UnsetToolTip();
        }
    }

    void onKey( wxKeyEvent& aEvent )
    {
        int count = (int) m_entries.size();

        if( count == 0 )
        {
            aEvent.Skip();
            return;
        }

        switch( aEvent.GetKeyCode() )
        {
        case WXK_UP:       select( std::max( 0, m_selected - 1 ) );                 break;
        case WXK_DOWN:     select( std::min( count - 1, m_selected + 1 ) );         break;
        case WXK_HOME:     select( 0 );                                             break;
        case WXK_END:      select( count - 1 );                                     break;
        case WXK_PAGEUP:   select( std::max( 0, m_selected - 10 ) );                break;
        case WXK_PAGEDOWN: select( std::min( count - 1, m_selected + 10 ) );        break;

        case WXK_SPACE:
            if( m_selected >= 0 && m_entries[m_selected].group != G_PROBLEMS )
                m_dialog->ToggleEnable( m_entries[m_selected].group, m_entries[m_selected].row );

            break;

        case WXK_LEFT:
        case WXK_RIGHT:
            if( m_selected >= 0 )
            {
                int group = m_entries[m_selected].group;
                m_dialog->SetGroupExpanded( group, aEvent.GetKeyCode() == WXK_RIGHT );
            }

            break;

        default:
            aEvent.Skip();
        }
    }

    DIALOG_UPDATE_PCB*  m_dialog;
    wxWindow*           m_header = nullptr;
    std::vector<ENTRY>  m_entries;
    int                 m_selected;
    bool                m_onlyErrors;
    int                 m_rowHeight;
    int                 m_tipRow;
};


/// The two-level header: "Modifications" and "Status" bands over the column titles
class ECO_HEADER : public wxWindow
{
public:
    ECO_HEADER( wxWindow* aParent, ECO_LIST* aList ) :
            wxWindow( aParent, wxID_ANY ),
            m_list( aList )
    {
        SetBackgroundStyle( wxBG_STYLE_PAINT );
        SetMinSize( wxSize( -1, FromDIP( 44 ) ) );
        Bind( wxEVT_PAINT, &ECO_HEADER::onPaint, this );
    }

private:
    void onPaint( wxPaintEvent& aEvent )
    {
        wxBufferedPaintDC dc( this );
        ECO_COLOURS       colours;
        wxSize            size = GetClientSize();
        int               half = size.y / 2;
        std::vector<int>  cols = m_list->Columns();

        dc.SetBackground( wxBrush( colours.header ) );
        dc.Clear();
        dc.SetFont( GetFont() );
        dc.SetTextForeground( colours.fg );

        wxColour sep = colours.header.ChangeLightness( colours.bg.GetLuminance() < 0.5 ? 70 : 85 );
        dc.SetPen( wxPen( sep ) );

        auto label =
                [&]( const wxString& aText, int aLeft, int aRight, int aTop, int aHeight )
                {
                    int      pad = FromDIP( 5 );
                    wxString shown = wxControl::Ellipsize( aText, dc, wxELLIPSIZE_END,
                                                           std::max( 0, aRight - aLeft - 2 * pad ) );
                    dc.DrawText( shown, aLeft + pad, aTop + ( aHeight - dc.GetCharHeight() ) / 2 );
                };

        // Band row
        label( _( "Modifications" ), cols[C_ENABLE], cols[C_CHECK], 0, half );
        label( _( "Status" ), cols[C_CHECK], cols[C_COUNT], 0, half );
        dc.DrawLine( 0, half, size.x, half );
        dc.DrawLine( cols[C_CHECK], 0, cols[C_CHECK], size.y );

        // Column titles
        static const wxString titles[C_COUNT] = { _( "Enable" ), _( "Action" ),
                                                  _( "Affected Object" ), wxEmptyString,
                                                  _( "Affected Document" ), _( "Check" ),
                                                  _( "Done" ), _( "Message" ) };

        for( int c = 0; c < C_COUNT; ++c )
        {
            label( titles[c], cols[c], cols[c + 1], half, size.y - half );

            if( c > 0 && c != C_TO && c != C_CHECK )
                dc.DrawLine( cols[c], half + FromDIP( 3 ), cols[c], size.y - FromDIP( 3 ) );
        }

        dc.DrawLine( 0, size.y - 1, size.x, size.y - 1 );
    }

    ECO_LIST* m_list;
};


namespace
{
/// Report Changes...: the change list as text, with Export
class ECO_REPORT_DIALOG : public DIALOG_SHIM
{
public:
    ECO_REPORT_DIALOG( wxWindow* aParent, const wxString& aText, const wxString& aDefaultFile ) :
            DIALOG_SHIM( aParent, wxID_ANY, _( "Report Preview" ), wxDefaultPosition,
                         wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER ),
            m_text( aText ),
            m_defaultFile( aDefaultFile )
    {
        wxBoxSizer* top = new wxBoxSizer( wxVERTICAL );
        wxTextCtrl* view = new wxTextCtrl( this, wxID_ANY, aText, wxDefaultPosition,
                                           FromDIP( wxSize( 760, 420 ) ),
                                           wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP );
        view->SetFont( wxFont( wxFontInfo( GetFont().GetPointSize() ).Family( wxFONTFAMILY_TELETYPE ) ) );
        top->Add( view, 1, wxEXPAND | wxALL, FromDIP( 8 ) );

        wxBoxSizer* buttons = new wxBoxSizer( wxHORIZONTAL );
        wxButton*   exportBtn = new wxButton( this, wxID_ANY, _( "Export..." ) );
        buttons->Add( exportBtn, 0, wxALL, FromDIP( 5 ) );
        buttons->AddStretchSpacer();

        wxStdDialogButtonSizer* sdb = new wxStdDialogButtonSizer();
        sdb->AddButton( new wxButton( this, wxID_CANCEL, _( "Close" ) ) );
        sdb->Realize();
        buttons->Add( sdb, 0, wxALL, FromDIP( 5 ) );
        top->Add( buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP( 3 ) );

        SetSizerAndFit( top );

        exportBtn->Bind( wxEVT_BUTTON,
                         [this]( wxCommandEvent& )
                         {
                             wxFileName fn( m_defaultFile );
                             wxFileDialog dlg( this, _( "Export Report" ), fn.GetPath(),
                                               fn.GetFullName(),
                                               FILEEXT::ReportFileWildcard(),
                                               wxFD_SAVE | wxFD_OVERWRITE_PROMPT );

                             if( dlg.ShowModal() != wxID_OK )
                                 return;

                             wxFFile file( dlg.GetPath(), wxS( "wb" ) );

                             if( file.IsOpened() )
                                 file.Write( m_text );
                         } );

        finishDialogSettings();
    }

private:
    wxString m_text;
    wxString m_defaultFile;
};
} // namespace


DIALOG_UPDATE_PCB::DIALOG_UPDATE_PCB( PCB_EDIT_FRAME* aParent, NETLIST* aNetlist ) :
        DIALOG_SHIM( aParent, wxID_ANY, _( "Engineering Change Order" ), wxDefaultPosition,
                     wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER ),
        m_frame( aParent ),
        m_netlist( aNetlist ),
        m_initialized( false ),
        m_runDragCommand( false ),
        m_executed( false )
{
    m_netlist->SortByReference();

    wxFileName boardFile( m_frame->GetBoard()->GetFileName() );
    m_document = boardFile.GetFullName();

    if( m_document.IsEmpty() )
        m_document = _( "PCB" );

    // Change groups (titles as in Altium's Engineering Change Order)
    m_groups.resize( G_COUNT );
    m_groups[G_ADD_COMPONENTS].title = _( "Add Components" );
    m_groups[G_ADD_NETS].title = _( "Add Nets" );
    m_groups[G_ADD_PINS].title = _( "Add Pins To Nets" );
    m_groups[G_CHANGE_FOOTPRINTS].title = _( "Change Component Footprints" );
    m_groups[G_CHANGE_DESIGNATORS].title = _( "Change Component Designators" );
    m_groups[G_CHANGE_COMMENTS].title = _( "Change Component Comments" );
    m_groups[G_CHANGE_PARAMETERS].title = _( "Change Component Parameters" );
    m_groups[G_REMOVE_PARAMETERS].title = _( "Remove Component Parameters" );
    m_groups[G_CHANGE_PROPERTIES].title = _( "Change Component Properties" );
    m_groups[G_CHANGE_CLASSES].title = _( "Change Component Classes" );
    m_groups[G_CHANGE_GROUPS].title = _( "Change Component Groups" );
    m_groups[G_CHANGE_VARIANTS].title = _( "Change Variants" );
    m_groups[G_CHANGE_PINS].title = _( "Change Pin Nets" );
    m_groups[G_CHANGE_ROUTED].title = _( "Change Via And Polygon Nets" );
    m_groups[G_REMOVE_PINS].title = _( "Remove Pins From Nets" );
    m_groups[G_REMOVE_COMPONENTS].title = _( "Remove Components" );
    m_groups[G_REMOVE_NETS].title = _( "Remove Nets" );
    m_groups[G_PROBLEMS].title = _( "Errors And Warnings" );

    // KiCad's options, kept (hidden) for their saved values
    m_optionHolder = new wxPanel( this, wxID_ANY );
    m_optionHolder->Hide();

    auto option =
            [&]( const wxString& aLabel, bool aDefault )
            {
                wxCheckBox* cb = new wxCheckBox( m_optionHolder, wxID_ANY, aLabel );
                cb->SetValue( aDefault );
                return cb;
            };

    m_cbRelinkFootprints = option( _( "Re-link footprints to schematic symbols based on their reference designators" ), false );
    m_cbTransferGroups = option( _( "Group footprints based on symbol group" ), false );
    m_cbApplyDesignBlockLayouts = option( _( "Apply design block layouts to new groups" ), false );
    m_cbUpdateFootprints = option( _( "Replace footprints with those specified by symbols" ), true );
    m_cbDeleteExtraFootprints = option( _( "Delete footprints with no symbols" ), false );
    m_cbOverrideLocks = option( _( "Override locks" ), false );
    m_cbUpdateFields = option( _( "Update footprint fields from symbols" ), true );
    m_cbRemoveExtraFields = option( _( "Remove footprint fields not found in symbols" ), false );

    m_groups[G_ADD_COMPONENTS].mode = ENABLE_PER_ROW;
    m_groups[G_REMOVE_COMPONENTS].mode = ENABLE_PER_ROW;
    m_groups[G_CHANGE_FOOTPRINTS].mode = ENABLE_GROUP;
    m_groups[G_CHANGE_FOOTPRINTS].option = m_cbUpdateFootprints;
    m_groups[G_CHANGE_PARAMETERS].mode = ENABLE_GROUP;
    m_groups[G_CHANGE_PARAMETERS].option = m_cbUpdateFields;
    m_groups[G_REMOVE_PARAMETERS].mode = ENABLE_GROUP;
    m_groups[G_REMOVE_PARAMETERS].option = m_cbRemoveExtraFields;
    m_groups[G_CHANGE_GROUPS].mode = ENABLE_GROUP;
    m_groups[G_CHANGE_GROUPS].option = m_cbTransferGroups;

    // Layout: the table, then Validate / Execute / Report / Only Show Errors ... Close
    wxBoxSizer* top = new wxBoxSizer( wxVERTICAL );

    wxPanel*    frame = new wxPanel( this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                     wxBORDER_SIMPLE );
    wxBoxSizer* frameSizer = new wxBoxSizer( wxVERTICAL );

    m_list = new ECO_LIST( frame, this );
    m_header = new ECO_HEADER( frame, m_list );
    m_list->SetHeader( m_header );

    frameSizer->Add( m_header, 0, wxEXPAND );
    frameSizer->Add( m_list, 1, wxEXPAND );
    frame->SetSizer( frameSizer );
    frame->SetMinSize( FromDIP( wxSize( 1000, 460 ) ) );

    top->Add( frame, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP( 8 ) );

    // Altium compiles the project first: a red line under the list when that found errors,
    // with a link to them in the Messages panel
    int compileErrors = compileProject();

    if( compileErrors > 0 )
    {
        wxBoxSizer*   warn = new wxBoxSizer( wxHORIZONTAL );
        wxStaticText* text = new wxStaticText( this, wxID_ANY,
                                               _( "Warning: Errors occurred during compilation of "
                                                  "the project!" ) );
        text->SetForegroundColour( wxColour( 0xE0, 0x6C, 0x6C ) );

        wxHyperlinkCtrl* link = new wxHyperlinkCtrl( this, wxID_ANY,
                                                     _( "Click here to review them before "
                                                        "continuing." ),
                                                     wxEmptyString );
        link->Bind( wxEVT_HYPERLINK,
                    [this]( wxHyperlinkEvent& )
                    {
                        MESSAGES_PANEL::ShowPane( m_frame );
                    } );

        warn->Add( text, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP( 6 ) );
        warn->Add( link, 0, wxALIGN_CENTER_VERTICAL );
        top->Add( warn, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP( 8 ) );
    }

    wxBoxSizer* buttons = new wxBoxSizer( wxHORIZONTAL );

    m_validateBtn = new wxButton( this, wxID_ANY, _( "Validate Changes" ) );
    m_executeBtn = new wxButton( this, wxID_ANY, _( "Execute Changes" ) );
    m_reportBtn = new wxButton( this, wxID_ANY, _( "Report Changes..." ) );
    m_onlyErrors = new wxCheckBox( this, wxID_ANY, _( "Only Show Errors" ) );
    m_optionsBtn = nullptr;     // Altium has no such button: the options are in the list's menu

    int gap = FromDIP( 5 );
    buttons->Add( m_validateBtn, 0, wxALIGN_CENTER_VERTICAL | wxALL, gap );
    buttons->Add( m_executeBtn, 0, wxALIGN_CENTER_VERTICAL | wxALL, gap );
    buttons->Add( m_reportBtn, 0, wxALIGN_CENTER_VERTICAL | wxALL, gap );
    buttons->Add( m_onlyErrors, 0, wxALIGN_CENTER_VERTICAL | wxALL, gap );
    buttons->AddStretchSpacer();

    wxStdDialogButtonSizer* sdb = new wxStdDialogButtonSizer();
    m_closeBtn = new wxButton( this, wxID_CANCEL, _( "Close" ) );
    sdb->AddButton( m_closeBtn );
    sdb->Realize();
    buttons->Add( sdb, 0, wxALIGN_CENTER_VERTICAL | wxALL, gap );

    top->Add( buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP( 6 ) );

    SetSizer( top );
    top->SetSizeHints( this );

    m_validateBtn->Bind( wxEVT_BUTTON, &DIALOG_UPDATE_PCB::onValidate, this );
    m_executeBtn->Bind( wxEVT_BUTTON, &DIALOG_UPDATE_PCB::onExecute, this );
    m_reportBtn->Bind( wxEVT_BUTTON, &DIALOG_UPDATE_PCB::onReport, this );
    m_onlyErrors->Bind( wxEVT_CHECKBOX, &DIALOG_UPDATE_PCB::onOnlyErrors, this );
    m_list->Bind( wxEVT_CONTEXT_MENU,
                  [this]( wxContextMenuEvent& )
                  {
                      wxCommandEvent dummy;
                      onOptions( dummy );
                  } );

    m_executeBtn->SetDefault();
    SetInitialFocus( m_list );

    finishDialogSettings();

    m_initialized = true;
}


DIALOG_UPDATE_PCB::~DIALOG_UPDATE_PCB()
{
    if( m_runDragCommand )
    {
        PCB_SELECTION_TOOL* selTool = m_frame->GetToolManager()->GetTool<PCB_SELECTION_TOOL>();
        PCB_SELECTION&      selection = selTool->GetSelection();

        // Set the reference point to (0,0) where the new footprints were spread. This ensures
        // the move tool knows where the items are located, preventing an offset when the "warp
        // cursor to origin of moved object" preference is disabled.
        if( selection.Size() > 0 )
            selection.SetReferencePoint( VECTOR2I( 0, 0 ) );

        KIGFX::VIEW_CONTROLS* controls = m_frame->GetCanvas()->GetViewControls();
        controls->SetCursorPosition( controls->GetMousePosition() );
        m_frame->GetToolManager()->RunAction( PCB_ACTIONS::move );
    }
}


int DIALOG_UPDATE_PCB::compileProject()
{
    // Altium's compiler check before an ECO: nets connected to a single pin
    std::map<wxString, std::vector<wxString>> pinsByNet;

    for( unsigned ii = 0; ii < m_netlist->GetCount(); ++ii )
    {
        COMPONENT* comp = m_netlist->GetComponent( ii );

        for( unsigned jj = 0; jj < comp->GetNetCount(); ++jj )
        {
            const COMPONENT_NET& net = comp->GetNet( jj );

            if( net.GetNetName().IsEmpty() || net.GetNetName().StartsWith( wxS( "unconnected-" ) ) )
                continue;

            pinsByNet[net.GetNetName()].push_back( comp->GetReference() + wxS( "-" )
                                                   + net.GetPinName() );
        }
    }

    wxString schDoc = wxFileName( m_frame->Prj().GetProjectPath(), m_frame->Prj().GetProjectName(),
                                  FILEEXT::KiCadSchematicFileExtension ).GetFullName();
    int      errors = 0;
    std::vector<PROJECT_MESSAGE> results;

    for( const auto& [netName, pins] : pinsByNet )
    {
        if( pins.size() != 1 )
            continue;

        PROJECT_MESSAGE msg;
        msg.severity = RPT_SEVERITY_ERROR;
        msg.document = schDoc;
        msg.source = _( "Compiler" );
        msg.text = wxString::Format( _( "Net %s has only one pin (Pin %s)" ),
                                     UnescapeString( netName ), pins.front() );
        msg.key = wxS( "compiler:single-pin:" ) + netName;
        msg.when = wxDateTime::Now();

        PROJECT_MESSAGE_DETAIL detail;
        detail.text = wxS( "Pin " ) + pins.front();
        detail.icon = BITMAPS::pin;
        msg.details.push_back( detail );

        results.push_back( std::move( msg ) );
        errors++;
    }

    // As in Altium, a recompile replaces the previous compiler messages (and the stale rows of
    // an earlier change order)
    MESSAGES_STORE::SetCompilerMessages( std::move( results ) );

    // As in Altium, the compile results come to the front
    if( errors > 0 )
        MESSAGES_PANEL::ShowPane( m_frame );

    return errors;
}


bool DIALOG_UPDATE_PCB::TransferDataToWindow()
{
    populate();
    return true;
}


void DIALOG_UPDATE_PCB::runUpdater( bool aDryRun, bool aAll,
                                    std::vector<std::pair<wxString, SEVERITY>>& aLines )
{
    COLLECTING_REPORTER reporter( aLines );

    bool relink = m_cbRelinkFootprints->GetValue();
    bool replace = aAll || m_cbUpdateFootprints->GetValue();

    // Unticked rows: components not to add, unused footprints to keep
    std::vector<std::pair<COMPONENT*, std::map<wxString, wxString>>> restore;
    std::set<wxString>                                               keep;
    bool                                                             anyRemove = false;

    if( !aAll )
    {
        for( int r : m_groups[G_ADD_COMPONENTS].rows )
        {
            const ECO_ROW& row = m_rows[r];

            if( row.enabled || row.ref.IsEmpty() )
                continue;

            if( COMPONENT* comp = m_netlist->GetComponentByReference( row.ref ) )
            {
                std::map<wxString, wxString> props = comp->GetProperties();
                restore.emplace_back( comp, props );
                props[wxS( "exclude_from_board" )] = wxEmptyString;
                comp->SetProperties( props );
            }
        }

        for( int r : m_groups[G_REMOVE_COMPONENTS].rows )
        {
            if( m_rows[r].enabled )
                anyRemove = true;
            else if( !m_rows[r].ref.IsEmpty() )
                keep.insert( m_rows[r].ref );
        }
    }

    m_netlist->SetFindByTimeStamp( !relink );
    m_netlist->SetReplaceFootprints( replace );

    if( !aDryRun )
    {
        m_frame->GetToolManager()->DeactivateTool();
        m_frame->GetToolManager()->RunAction( ACTIONS::selectionClear );
    }

    bool transferGroups = aAll || m_cbTransferGroups->GetValue();

    BOARD_NETLIST_UPDATER updater( m_frame, m_frame->GetBoard() );
    updater.SetReporter( &reporter );
    updater.SetIsDryRun( aDryRun );
    updater.SetLookupByTimestamp( !relink );
    updater.SetDeleteUnusedFootprints( aAll || anyRemove );
    updater.SetKeepFootprints( keep );
    updater.SetReplaceFootprints( replace );
    updater.SetTransferGroups( transferGroups );
    updater.SetApplyDesignBlockLayouts( transferGroups && m_cbApplyDesignBlockLayouts->GetValue() );
    updater.SetOverrideLocks( m_cbOverrideLocks->GetValue() );
    updater.SetUpdateFields( aAll || m_cbUpdateFields->GetValue() );
    updater.SetRemoveExtraFields( aAll || m_cbRemoveExtraFields->GetValue() );
    updater.UpdateNetlist( *m_netlist );

    for( auto& [comp, props] : restore )
        comp->SetProperties( props );

    if( aDryRun )
        return;

    m_frame->OnNetlistChanged( updater, &m_runDragCommand );

    if( transferGroups && m_cbApplyDesignBlockLayouts->GetValue() )
    {
        TOOL_MANAGER*       toolMgr = m_frame->GetToolManager();
        PCB_SELECTION_TOOL* selTool = toolMgr->GetTool<PCB_SELECTION_TOOL>();

        for( PCB_GROUP* group : updater.GetAddedGroups() )
        {
            if( !group->HasDesignBlockLink() )
                continue;

            toolMgr->RunAction( ACTIONS::selectionClear );
            selTool->select( group );
            toolMgr->RunAction( PCB_ACTIONS::applyDesignBlockLayout );
        }

        toolMgr->RunAction( ACTIONS::selectionClear );
    }
}


int DIALOG_UPDATE_PCB::groupFor( const wxString& aKey, SEVERITY aSeverity, wxString& aAction,
                                 wxString& aObject, wxString& aRef )
{
    // The updater's messages (in present tense, see normalise()) mapped onto change groups
    struct PATTERN
    {
        const wchar_t* regex;
        int            group;
        const wchar_t* action;
        const wchar_t* object;   ///< \1.. substituted
        int            refGroup; ///< sub-match holding the component reference, or 0
    };

    static const PATTERN patterns[] = {
        { L"^Cannot add (\\S+) \\((.*)\\)\\.?$",                   G_ADD_COMPONENTS,     L"Add",    L"\\1",               1 },
        { L"^Add net (.*)\\.$",                                    G_ADD_NETS,           L"Add",    L"\\1",               0 },
        { L"^Add (\\S+) \\(footprint '(.*)'\\)\\.$",               G_ADD_COMPONENTS,     L"Add",    L"\\1",               1 },
        { L"^Connect (\\S+) pin (\\S+) to (.*)\\.$",               G_ADD_PINS,           L"Add",    L"\\1-\\2 to \\3",    0 },
        { L"^Disconnect (\\S+) pin (\\S+)\\.$",                    G_REMOVE_PINS,        L"Remove", L"\\1-\\2",           0 },
        { L"^Reconnect via from (.*) to (.*)\\.$",                 G_CHANGE_ROUTED,      L"Change", L"Via: \\1 → \\2", 0 },
        { L"^Reconnect copper zone '(.*)' from (.*) to (.*)\\.$",  G_CHANGE_ROUTED,      L"Change", L"Polygon \\1: \\2 → \\3", 0 },
        { L"^Reconnect copper zone from (.*) to (.*)\\.$",         G_CHANGE_ROUTED,      L"Change", L"Polygon: \\1 → \\2", 0 },
        { L"^Reconnect (\\S+) pin (\\S+) from (.*) to (.*)\\.$",   G_CHANGE_PINS,        L"Change", L"\\1-\\2: \\3 → \\4", 0 },
        { L"^Cannot remove unused footprint (\\S+) \\((.*)\\)\\.?$", G_REMOVE_COMPONENTS, L"Remove", L"\\1",               1 },
        { L"^Remove unused footprint (\\S+)\\.$",                  G_REMOVE_COMPONENTS,  L"Remove", L"\\1",               1 },
        { L"^Remove unused net (.*)\\.$",                          G_REMOVE_NETS,        L"Remove", L"\\1",               0 },
        { L"^Cannot change (\\S+) footprint from '(.*)' to '(.*)' \\(.*$", G_CHANGE_FOOTPRINTS, L"Change", L"\\1: \\2 → \\3", 0 },
        { L"^Change (\\S+) footprint from '(.*)' to '(.*)'\\.$",   G_CHANGE_FOOTPRINTS,  L"Change", L"\\1: \\2 → \\3", 0 },
        { L"^Cannot update (\\S+) \\((.*)\\)\\.?$",                G_CHANGE_FOOTPRINTS,  L"Change", L"\\1",               0 },
        { L"^Change (\\S+) reference designator to (.*)\\.$",      G_CHANGE_DESIGNATORS, L"Change", L"\\1 → \\2",     0 },
        { L"^Change (\\S+) value from (.*) to (.*)\\.$",           G_CHANGE_COMMENTS,    L"Change", L"\\1: \\2 → \\3", 0 },
        { L"^Update (\\S+) fields\\.$",                            G_CHANGE_PARAMETERS,  L"Change", L"\\1",               0 },
        { L"^Remove (\\S+) footprint fields not in symbol\\.$",    G_REMOVE_PARAMETERS,  L"Remove", L"\\1",               0 },
        { L"^(Change|Remove) (\\S+) component class(.*)\\.$",      G_CHANGE_CLASSES,     L"\\1",    L"\\2\\3",            0 },
    };

    static std::vector<std::unique_ptr<wxRegEx>> compiled;

    if( compiled.empty() )
    {
        for( const PATTERN& p : patterns )
            compiled.push_back( std::make_unique<wxRegEx>( p.regex, wxRE_ADVANCED ) );
    }

    for( size_t i = 0; i < compiled.size(); ++i )
    {
        wxRegEx& re = *compiled[i];

        if( !re.IsValid() || !re.Matches( aKey ) )
            continue;

        wxString action = patterns[i].action;
        wxString object = patterns[i].object;

        for( int m = (int) re.GetMatchCount() - 1; m >= 1; --m )
        {
            wxString sub = re.GetMatch( aKey, m );
            action.Replace( wxString::Format( wxS( "\\%d" ), m ), sub );
            object.Replace( wxString::Format( wxS( "\\%d" ), m ), sub );
        }

        aAction = wxGetTranslation( action );
        aObject = object;

        if( patterns[i].refGroup > 0 )
            aRef = re.GetMatch( aKey, patterns[i].refGroup );

        return patterns[i].group;
    }

    wxString first = aKey.BeforeFirst( ' ' );
    wxString rest = stripDot( aKey.AfterFirst( ' ' ) );

    if( aSeverity == RPT_SEVERITY_ACTION )
    {
        aAction = first == wxS( "Update" ) ? wxString( _( "Change" ) ) : first;
        aObject = rest;

        if( aKey.Contains( wxS( "group" ) ) )
            return G_CHANGE_GROUPS;

        if( aKey.Contains( wxS( "variant" ) ) )
            return G_CHANGE_VARIANTS;

        return G_CHANGE_PROPERTIES;
    }

    aAction = aSeverity == RPT_SEVERITY_ERROR ? _( "Error" ) : _( "Warning" );
    aObject = stripDot( aKey );
    return G_PROBLEMS;
}


int DIALOG_UPDATE_PCB::addRow( const wxString& aText, SEVERITY aSeverity, bool aAfterExecute )
{
    wxString key = normalise( aText );

    if( auto it = m_rowByKey.find( key ); it != m_rowByKey.end() )
        return it->second;

    ECO_ROW row;
    row.key = key;
    row.text = aText;
    row.group = groupFor( key, aSeverity, row.action, row.object, row.ref );

    // The component a change is about: the word after the verb ("Update D1 sheetname ...")
    wxString subject = key.AfterFirst( ' ' ).BeforeFirst( ' ' );

    if( row.group == G_ADD_COMPONENTS && aSeverity == RPT_SEVERITY_ACTION && !row.ref.IsEmpty() )
        m_addedRefs.insert( row.ref );

    // KiCad's bookkeeping (symbol links, sheet names, unit data, footprint filters) is not an
    // Altium change; nor is anything a component being added brings along (its pins, fields,
    // class): Altium lists such a component under Add Components only
    static const wxString internal[] = { wxS( " symbol association " ), wxS( " sheetname " ),
                                         wxS( " sheetfile " ), wxS( " footprint filters " ),
                                         wxS( " unit metadata" ), wxS( " jumper pad groups" ) };

    for( const wxString& part : internal )
    {
        if( key.Contains( part ) )
            row.hidden = true;
    }

    if( row.group != G_ADD_COMPONENTS && row.group != G_ADD_NETS && row.group != G_PROBLEMS
            && m_addedRefs.count( subject ) )
    {
        row.hidden = true;
    }

    // The list is complete before Execute: what only shows up while executing stays out of it
    // (problems excepted)
    if( aAfterExecute && row.group != G_PROBLEMS )
        row.hidden = true;

    const ECO_GROUP& group = m_groups[row.group];

    if( row.group == G_REMOVE_COMPONENTS )
        row.enabled = m_cbDeleteExtraFootprints->GetValue();
    else if( group.mode == ENABLE_GROUP && group.option )
        row.enabled = group.option->GetValue();

    int idx = (int) m_rows.size();
    m_rows.push_back( row );

    if( !row.hidden )
        m_groups[row.group].rows.push_back( idx );

    m_rowByKey[key] = idx;
    return idx;
}


void DIALOG_UPDATE_PCB::populate()
{
    std::vector<std::pair<wxString, SEVERITY>> lines;
    runUpdater( true, true, lines );

    m_rows.clear();
    m_rowByKey.clear();
    m_addedRefs.clear();

    for( ECO_GROUP& group : m_groups )
        group.rows.clear();

    for( const auto& [text, severity] : lines )
    {
        if( isChangeSeverity( severity ) )
            addRow( text, severity );
    }

    refreshList();
}


void DIALOG_UPDATE_PCB::applyStatus( const std::vector<std::pair<wxString, SEVERITY>>& aLines,
                                     bool aDone )
{
    for( ECO_ROW& row : m_rows )
    {
        if( aDone )
            row.done = STATUS_NONE;
        else
            row.check = STATUS_NONE;

        row.message.Clear();
    }

    for( const auto& [text, severity] : aLines )
    {
        if( !isChangeSeverity( severity ) )
            continue;

        int      idx = addRow( text, severity, aDone );
        ECO_ROW& row = m_rows[idx];
        int      status = severity == RPT_SEVERITY_ACTION  ? STATUS_OK
                          : severity == RPT_SEVERITY_ERROR ? STATUS_ERROR
                                                           : STATUS_WARNING;

        // A change with a problem keeps the worst status
        int& column = aDone ? row.done : row.check;
        column = std::max( column, status );

        if( status != STATUS_OK )
            row.message = row.group == G_PROBLEMS ? wxString( stripDot( UnescapeHTML( text ) ) )
                                                  : reasonOf( text );
    }
}


void DIALOG_UPDATE_PCB::refreshList()
{
    if( m_list )
        m_list->Rebuild();

    bool any = false;

    for( const ECO_ROW& row : m_rows )
    {
        if( row.group != G_PROBLEMS && row.enabled && !row.hidden )
            any = true;
    }

    m_validateBtn->Enable( !m_executed && !m_rows.empty() );
    m_executeBtn->Enable( !m_executed && any );
}


void DIALOG_UPDATE_PCB::syncGroupOptions()
{
    // Remove Components: delete unused footprints if any of them is ticked
    bool anyRemove = false;

    for( int r : m_groups[G_REMOVE_COMPONENTS].rows )
        anyRemove |= m_rows[r].enabled;

    if( !m_groups[G_REMOVE_COMPONENTS].rows.empty() )
        m_cbDeleteExtraFootprints->SetValue( anyRemove );

    if( !m_cbTransferGroups->GetValue() )
        m_cbApplyDesignBlockLayouts->SetValue( false );
}


void DIALOG_UPDATE_PCB::ToggleEnable( int aGroup, int aRow )
{
    ECO_GROUP& group = m_groups[aGroup];

    if( m_executed || group.mode == ENABLE_FIXED || group.rows.empty() )
    {
        wxBell();
        return;
    }

    bool newValue;

    if( aRow < 0 || group.mode == ENABLE_GROUP )
    {
        bool all = true;

        for( int r : group.rows )
            all &= m_rows[r].enabled;

        newValue = aRow < 0 ? !all : !m_rows[aRow].enabled;

        for( int r : group.rows )
            m_rows[r].enabled = newValue;

        if( group.option )
            group.option->SetValue( newValue );
    }
    else
    {
        m_rows[aRow].enabled = !m_rows[aRow].enabled;
    }

    // A changed selection invalidates the last validation of the group
    for( int r : group.rows )
        m_rows[r].check = STATUS_NONE;

    syncGroupOptions();
    refreshList();
}


void DIALOG_UPDATE_PCB::SetAllEnabled( bool aEnable )
{
    for( ECO_GROUP& group : m_groups )
    {
        if( group.mode == ENABLE_FIXED )
            continue;

        for( int r : group.rows )
            m_rows[r].enabled = aEnable;

        if( group.option && !group.rows.empty() )
            group.option->SetValue( aEnable );
    }

    syncGroupOptions();
    refreshList();
}


void DIALOG_UPDATE_PCB::SetGroupExpanded( int aGroup, bool aExpanded )
{
    m_groups[aGroup].expanded = aExpanded;
    refreshList();
}


void DIALOG_UPDATE_PCB::onValidate( wxCommandEvent& aEvent )
{
    std::vector<std::pair<wxString, SEVERITY>> lines;
    runUpdater( true, false, lines );
    applyStatus( lines, false );
    refreshList();
}


void DIALOG_UPDATE_PCB::onExecute( wxCommandEvent& aEvent )
{
    // As in Altium, executing validates first
    std::vector<std::pair<wxString, SEVERITY>> lines;
    runUpdater( true, false, lines );
    applyStatus( lines, false );

    lines.clear();
    runUpdater( false, false, lines );
    applyStatus( lines, true );

    // Altium lists the problems an ECO ran into in the Messages panel too
    for( const auto& [text, severity] : lines )
        WX_HTML_REPORT_PANEL::NotifyReportListeners( this, text, severity );

    m_executed = true;
    refreshList();

    m_closeBtn->SetDefault();
    m_closeBtn->SetFocus();
}


void DIALOG_UPDATE_PCB::onOnlyErrors( wxCommandEvent& aEvent )
{
    m_list->SetOnlyErrors( m_onlyErrors->GetValue() );
}


void DIALOG_UPDATE_PCB::onOptions( wxCommandEvent& aEvent )
{
    enum
    {
        ID_RELINK = wxID_HIGHEST + 1,
        ID_LOCKS,
        ID_LAYOUTS,
        ID_ENABLE_ALL,
        ID_DISABLE_ALL
    };

    wxMenu menu;
    menu.AppendCheckItem( ID_RELINK, m_cbRelinkFootprints->GetLabel(), m_cbRelinkFootprints->GetToolTipText() )
            ->Check( m_cbRelinkFootprints->GetValue() );
    menu.AppendCheckItem( ID_LOCKS, m_cbOverrideLocks->GetLabel() )
            ->Check( m_cbOverrideLocks->GetValue() );
    menu.AppendCheckItem( ID_LAYOUTS, m_cbApplyDesignBlockLayouts->GetLabel() )
            ->Check( m_cbApplyDesignBlockLayouts->GetValue() );
    menu.Enable( ID_LAYOUTS, m_cbTransferGroups->GetValue() );
    menu.AppendSeparator();
    menu.Append( ID_ENABLE_ALL, _( "Enable All" ) );
    menu.Append( ID_DISABLE_ALL, _( "Disable All" ) );

    if( m_executed )
    {
        for( int id : { ID_RELINK, ID_LOCKS, ID_LAYOUTS, ID_ENABLE_ALL, ID_DISABLE_ALL } )
            menu.Enable( id, false );
    }

    int id = GetPopupMenuSelectionFromUser( menu );

    switch( id )
    {
    case ID_RELINK:
        m_cbRelinkFootprints->SetValue( !m_cbRelinkFootprints->GetValue() );
        populate();
        break;

    case ID_LOCKS:
        m_cbOverrideLocks->SetValue( !m_cbOverrideLocks->GetValue() );
        populate();
        break;

    case ID_LAYOUTS:
        m_cbApplyDesignBlockLayouts->SetValue( !m_cbApplyDesignBlockLayouts->GetValue() );
        break;

    case ID_ENABLE_ALL:  SetAllEnabled( true );  break;
    case ID_DISABLE_ALL: SetAllEnabled( false ); break;
    default: break;
    }
}


wxString DIALOG_UPDATE_PCB::reportText() const
{
    auto statusText =
            []( int aStatus ) -> wxString
            {
                switch( aStatus )
                {
                case STATUS_OK:      return _( "OK" );
                case STATUS_WARNING: return _( "Warning" );
                case STATUS_ERROR:   return _( "Error" );
                default:             return wxEmptyString;
                }
            };

    wxString out;
    out << _( "Engineering Change Order Report" ) << wxS( "\n" );
    out << wxString::Format( _( "Document: %s" ), m_document ) << wxS( "\n\n" );
    out << wxString::Format( wxS( "%-8s %-8s %-48s %-28s %-8s %-8s %s\n" ), _( "Enable" ),
                             _( "Action" ), _( "Affected Object" ), _( "Affected Document" ),
                             _( "Check" ), _( "Done" ), _( "Message" ) );

    for( const ECO_GROUP& group : m_groups )
    {
        if( group.rows.empty() )
            continue;

        out << wxS( "\n" ) << wxString::Format( wxS( "%s(%d)" ), group.title, (int) group.rows.size() )
            << wxS( "\n" );

        for( int r : group.rows )
        {
            const ECO_ROW& row = m_rows[r];
            out << wxString::Format( wxS( "  %-6s %-8s %-48s %-28s %-8s %-8s %s\n" ),
                                     row.enabled ? wxS( "[x]" ) : wxS( "[ ]" ), row.action,
                                     row.object, m_document, statusText( row.check ),
                                     statusText( row.done ), row.message );
        }
    }

    return out;
}


void DIALOG_UPDATE_PCB::onReport( wxCommandEvent& aEvent )
{
    wxFileName fn( m_frame->GetBoard()->GetFileName() );
    fn.SetName( fn.GetName() + wxS( "_ECO" ) );
    fn.SetExt( FILEEXT::ReportFileExtension );

    ECO_REPORT_DIALOG dlg( this, reportText(), fn.GetFullPath() );
    dlg.ShowModal();
}
