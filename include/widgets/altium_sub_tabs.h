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

#ifndef ALTIUM_SUB_TABS_H
#define ALTIUM_SUB_TABS_H

#include <functional>
#include <vector>
#include <wx/window.h>

class wxBookCtrlBase;

/**
 * Altium's sub-tabs inside a panel (Properties' General | Parameters, View Configuration's
 * Layers & Colors | View Options, ...): boxed tabs that share their borders, the others in the
 * dark tab-strip colour and the open one lighter, over a line along the whole width.
 */
class ALTIUM_SUB_TABS : public wxWindow
{
public:
    ALTIUM_SUB_TABS( wxWindow* aParent, std::function<void( int )> aOnSelect = nullptr );

    void SetTabs( const std::vector<wxString>& aLabels, int aActive );
    void SetActive( int aActive );
    int  GetActive() const { return m_active; }

    /**
     * Drive a book control (normally a wxSimplebook, which has no tabs of its own): the tabs
     * follow its pages' texts and selection, and picking a tab changes its page.
     */
    void AttachBook( wxBookCtrlBase* aBook );

    /// Re-reads the attached book's page texts and selection.
    void SyncFromBook();

private:
    std::vector<wxRect> tabRects( wxDC& aDC ) const;
    void                onPaint();

    std::vector<wxString>      m_labels;
    int                        m_active = 0;
    std::function<void( int )> m_onSelect;
    wxBookCtrlBase*            m_book = nullptr;
};

#endif // ALTIUM_SUB_TABS_H
