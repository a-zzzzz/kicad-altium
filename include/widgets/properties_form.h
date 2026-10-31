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

#ifndef PROPERTIES_FORM_H
#define PROPERTIES_FORM_H


#include <wx/panel.h>
#include <wx/variant.h>
#include <wx/colour.h>

#include <functional>
#include <set>
#include <utility>
#include <vector>

class wxPropertyGrid;
class wxPGProperty;
class wxTextCtrl;
class wxDC;


/**
 * What a PROPERTIES_FORM needs from the panel that owns it: the property grid whose rows the
 * form shows (the grid keeps them, hidden; the form is another view of the same values) and a
 * way to apply edits, all in one undo step.
 */
class PROPERTIES_FORM_HOST
{
public:
    virtual ~PROPERTIES_FORM_HOST() = default;

    virtual wxPropertyGrid* FormGrid() = 0;

    /// Apply new values to the named properties of the selection (one commit).
    virtual void FormApply( const std::vector<std::pair<wxString, wxVariant>>& aValues ) = 0;

    /// The form changed height: lay the panel out again.
    virtual void FormLayoutChanged() = 0;
};


/**
 * One Altium-style Properties section body: right-aligned labels and dark value boxes, segmented
 * choices, check boxes, (X/Y) pairs with a lock, read-only and computed values.  Rows are drawn,
 * not native controls; a text box becomes an edit control while it is being typed in, and choices
 * open a menu.
 *
 * Rows are bound to properties of the host's grid by name.  A row whose property doesn't exist
 * (or whose visibility function says so) is not shown.  Custom rows take getter/setter functions
 * instead, for values that aren't grid properties.
 */
class PROPERTIES_FORM : public wxPanel
{
public:
    struct ROW;

    PROPERTIES_FORM( wxWindow* aParent, PROPERTIES_FORM_HOST* aHost );
    ~PROPERTIES_FORM() override;

    /// Options shared by the row kinds
    struct OPTS
    {
        bool                           readOnly = false;   ///< show as a greyed, read-only box
        bool                           linkLabel = false;  ///< blue label (Altium's parameter rows)
        bool                           swatch = false;     ///< colour swatch (layer choices)
        int                            lines = 1;          ///< text rows taller than one line
        std::function<bool()>          visible;            ///< empty = always
        std::function<bool()>          eyeState;           ///< show an eye toggle at the right
        std::function<void()>          eyeToggle;
        std::function<bool()>          lockState;          ///< show a lock toggle at the far right
        std::function<void()>          lockToggle;
        std::function<bool()>          pickerShown;        ///< show an eyedropper button
        std::function<void()>          picker;
        std::function<std::vector<wxString>()> choices;    ///< custom choice: list read when shown
        std::function<wxColour()>      swatchColour;       ///< custom choice: its swatch
        bool                           plainTable = false; ///< table without fill or rules, first
                                                           ///< column right-aligned (Net Information)
        wxString                       hint;               ///< title: grey text at the right
                                                           ///< (a shortcut, e.g. "Shift + R")
        /// custom segmented: draw each segment's content (an icon) instead of its label
        std::function<void( wxDC&, const wxRect&, int, const wxColour& )> segmentPaint;
    };

    /// Text value bound to a property (units, numbers and text all parse through the property)
    ROW& AddText( const wxString& aProp, const wxString& aLabel, const OPTS& aOpts = OPTS() );

    /// Enumerated value bound to a property: a box with a drop-down arrow, opening a menu
    ROW& AddChoice( const wxString& aProp, const wxString& aLabel, const OPTS& aOpts = OPTS() );

    /// Check box bound to a bool property; label on the left and box on the right, or (aBoxFirst)
    /// box then label, as Altium uses for option lists
    ROW& AddCheck( const wxString& aProp, const wxString& aLabel, bool aBoxFirst = false,
                   const OPTS& aOpts = OPTS() );

    /// A check box with its own getter/setter
    ROW& AddCustomCheck( const wxString& aLabel, std::function<int()> aGet,
                         std::function<void( bool )> aSet, bool aBoxFirst = true,
                         const OPTS& aOpts = OPTS() );

    /// Full-width segmented buttons bound to an enum property; aLabels overrides the property's
    /// choice labels (same order); aValues the choice values each segment sets (default: all)
    ROW& AddSegmented( const wxString& aProp, std::vector<wxString> aLabels = {},
                       std::vector<long> aValues = {}, const OPTS& aOpts = OPTS() );

    /// Segmented buttons with their own getter (index, -1 = none) and setter; aEnabled greys
    /// segments that can't be chosen
    ROW& AddCustomSegmented( std::vector<wxString> aLabels, std::function<int()> aGet,
                             std::function<void( int )> aSet, std::vector<bool> aEnabled = {},
                             const wxString& aLabel = wxEmptyString, const OPTS& aOpts = OPTS() );

    /// Altium's "(X/Y)" row: two boxes and a lock button (aLock may be empty)
    ROW& AddXY( const wxString& aPropX, const wxString& aPropY, const wxString& aLock,
                const wxString& aLabel = wxS( "(X/Y)" ), const OPTS& aOpts = OPTS() );

    /// Two values side by side with their captions above (e.g. Track Width | Grid Size)
    ROW& AddTwin( const wxString& aPropA, const wxString& aCaptionA, const wxString& aPropB,
                  const wxString& aCaptionB, const OPTS& aOpts = OPTS() );

    /// A computed, read-only value; aBoxed draws it in a greyed box rather than as plain text
    ROW& AddInfo( const wxString& aLabel, std::function<wxString()> aValue, bool aBoxed = false,
                  const OPTS& aOpts = OPTS() );

    /// An editable text box with its own getter and setter (values that aren't grid properties);
    /// the setter gets the typed text
    ROW& AddCustomText( const wxString& aLabel, std::function<wxString()> aGet,
                        std::function<void( const wxString& )> aSet, const OPTS& aOpts = OPTS() );

    /// A computed choice with its own setter (e.g. IPC 4761 via type)
    ROW& AddCustomChoice( const wxString& aLabel, std::vector<wxString> aChoices,
                          std::function<int()> aGet, std::function<void( int )> aSet,
                          const OPTS& aOpts = OPTS() );

    /// A plain bold sub-title (Altium's "Hatch mode", "Board Size", ...)
    ROW& AddTitle( const wxString& aTitle, const OPTS& aOpts = OPTS() );

    /// A thin separator line
    ROW& AddSeparator( const OPTS& aOpts = OPTS() );

    /// A link-styled clickable text (e.g. "Hide preview")
    ROW& AddLink( std::function<wxString()> aText, std::function<void()> aClick,
                  const OPTS& aOpts = OPTS() );

    /// A painted area of the given height (illustrations, previews)
    ROW& AddPaint( int aHeightDIP, std::function<void( wxDC&, const wxRect& )> aPaint,
                   const OPTS& aOpts = OPTS() );

    /// A row of buttons that wrap to the form width (Altium's "Actions")
    ROW& AddButtons( std::vector<std::pair<wxString, std::function<void()>>> aButtons,
                     const OPTS& aOpts = OPTS() );

    /// One cell of a table row
    struct CELL
    {
        wxString                               text;         ///< shown unless bound to a property
        wxString                               prop;         ///< bound property: shown and edited through it
        bool                                   heading = false;  ///< a title cell (COPPER, X-Size, ...)
        bool                                   link = false;     ///< blue, clickable
        bool                                   readOnly = false;
        std::function<void()>                  click;        ///< link action
        std::function<void( const wxString& )> edit;         ///< custom text edit
        std::vector<wxString>                  choices;      ///< custom choice menu ...
        std::function<void( int )>             choose;       ///< ... and its setter
        int                                    eye = -1;     ///< >= 0: an eye icon (1 = shown)
                                                             ///< instead of text; click toggles

        CELL( const wxString& aText = wxEmptyString ) : text( aText ) {}
    };

    using TABLE_ROWS = std::vector<std::vector<CELL>>;

    /**
     * A small table (Altium's Net Information, Outline Vertices and Pad Stack).  aWeights sets the
     * column widths; a row with fewer cells lets its last cell span the rest.  Rows are read from
     * aRows each time the form is laid out.  Clicking a row stores its index in  aSelected (if
     * given); clicking an editable cell edits it.
     */
    ROW& AddTable( std::vector<int> aWeights, std::function<TABLE_ROWS()> aRows,
                   int* aSelected = nullptr, const OPTS& aOpts = OPTS() );

    /// The grid properties the rows show (the panel hides them from the grid)
    void CollectBound( std::set<wxString>& aNames ) const;

    /// Re-read every row from the grid (after the selection or its values changed)
    void Sync();

    /// True if at least one row is shown
    bool HasVisibleRows() const;

    wxSize DoGetBestClientSize() const override;

    /**
     * Parse typed text into a value for \a aProp, as the property grid's own editors do: units
     * and origin transforms for distances and coordinates, degrees for angles, empty text for an
     * unset optional value.  (KiCad's unit properties don't parse in StringToValue(); their grid
     * editor does it.)  \a aContext finds the frame that owns the units.
     */
    static bool ParseValue( wxPGProperty* aProp, const wxString& aText, wxVariant& aValue,
                            wxWindow* aContext );

    static wxColour Mix( const wxColour& a, const wxColour& b, double t );
    static wxColour LinkColour();

private:
    wxPropertyGrid* grid() const;
    wxPGProperty*   prop( const wxString& aName ) const;
    bool            rowShown( const ROW& aRow ) const;
    int             rowHeight( const ROW& aRow ) const;
    void            layoutRows( int aWidth );
    int             contentHeight( int aWidth ) const;

    void            onPaint( wxPaintEvent& aEvent );
    void            onLeftDown( wxMouseEvent& aEvent );
    void            onMotion( wxMouseEvent& aEvent );

    wxString        valueText( const wxString& aProp ) const;
    void            beginEdit( const wxRect& aBox, const wxString& aProp, bool aMultiline );
    void            beginTextEdit( const wxRect& aBox, const wxString& aInitial, bool aMultiline,
                                   std::function<void( const wxString& )> aCommit );
    void            openPropChoice( const wxString& aProp, const wxRect& aBox );
    void            openMenu( const std::vector<wxString>& aLabels, int aCurrent, const wxRect& aBox,
                              std::function<void( int )> aChoose );
    void            endEdit( bool aCommit );
    void            apply( const wxString& aProp, const wxVariant& aValue );
    void            openChoice( ROW& aRow, const wxRect& aBox );

    PROPERTIES_FORM_HOST*             m_host;
    std::vector<ROW*>                 m_rows;
    wxTextCtrl*                       m_editor = nullptr;
    std::function<void( const wxString& )> m_editCommit;
    wxString                          m_editStart;
    bool                              m_ending = false;
    int                               m_height = -1;
    int                               m_hoverRow = -1;
};

#endif // PROPERTIES_FORM_H
