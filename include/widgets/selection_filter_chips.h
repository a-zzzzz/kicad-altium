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

#ifndef SELECTION_FILTER_CHIPS_H
#define SELECTION_FILTER_CHIPS_H

#include <wx/panel.h>

#include <functional>
#include <utility>
#include <vector>

class wxCheckBox;

/**
 * Altium-style toggle buttons ("chips"): one button per option, blue while it is on, wrapping
 * to the panel width.  Used for the selection filter (with an "All - On / Off" button on its
 * own row) and for snap options.
 *
 * The owner's check boxes stay the model (settings, flashing, menus); the buttons only show
 * and flip them.
 */
class SELECTION_FILTER_CHIPS : public wxPanel
{
public:
    struct CHIP
    {
        wxCheckBox* box;
        wxString    label;
        bool        allButton = false;   ///< "All - On / Off", on a row of its own
        bool        rowBreak = false;    ///< start a new row after this chip
    };

    SELECTION_FILTER_CHIPS( wxWindow* aParent, std::function<void( wxCheckBox* )> aToggle,
                            std::function<void( wxCheckBox*, wxMouseEvent& )> aRightClick = nullptr,
                            std::function<double( wxCheckBox* )> aFlash = nullptr );

    void SetChips( std::vector<CHIP> aChips );

    /// Draw the lighter rounded block behind the buttons (Altium's section panel).
    void SetDrawBlock( bool aDraw ) { m_drawBlock = aDraw; Refresh(); }

    wxSize DoGetBestClientSize() const override;

private:
    int      chipHeight() const;
    wxString labelOf( const CHIP& aChip ) const;
    int      chipWidth( const CHIP& aChip ) const;
    int      minWidth() const;

    /// Chip rectangles for a given width, and the height they need.
    std::pair<std::vector<wxRect>, int> layout( int aWidth ) const;

    void updateHeight();
    int  hitTest( const wxPoint& aPos ) const;
    void onLeftDown( wxMouseEvent& aEvent );
    void onPaint( wxPaintEvent& aEvent );

    std::vector<CHIP>                                 m_chips;
    std::function<void( wxCheckBox* )>                m_toggle;
    std::function<void( wxCheckBox*, wxMouseEvent& )> m_rightClick;
    std::function<double( wxCheckBox* )>              m_flash;
    int                                               m_hover = -1;
    int                                               m_height = -1;
    int                                               m_prevHeight = -1;
    bool                                              m_layoutPending = false;
    bool                                              m_drawBlock = true;
};

#endif // SELECTION_FILTER_CHIPS_H
