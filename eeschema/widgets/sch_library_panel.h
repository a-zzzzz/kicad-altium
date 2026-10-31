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

#ifndef SCH_LIBRARY_PANEL_H
#define SCH_LIBRARY_PANEL_H

#include <kiid.h>
#include <lib_id.h>

#include <wx/panel.h>
#include <wx/timer.h>
#include <wx/choice.h>

#include <vector>

class SYMBOL_EDIT_FRAME;
class wxButton;
class wxListCtrl;
class wxListEvent;
class wxSearchCtrl;
class wxStaticText;


/**
 * Altium-style "SCH Library" panel of the symbol editor: the symbols of the current library
 * (Design Item ID / Description) filtered by a mask, Place / Add / Delete / Edit buttons, and
 * below them the pins of the symbol being edited with the part (unit) each belongs to.
 *
 * Double-clicking a symbol opens it; clicking a pin switches to its part and selects it, and
 * double-clicking one also shows it in the Properties panel.
 */
class SCH_LIBRARY_PANEL : public wxPanel
{
public:
    SCH_LIBRARY_PANEL( wxWindow* aParent, SYMBOL_EDIT_FRAME* aFrame );
    ~SCH_LIBRARY_PANEL() override;

    /// The symbol picked in the list, or an invalid LIB_ID when none is.
    LIB_ID GetSelectedLibId() const;

    /// The library the panel lists (empty before the first rebuild).
    const wxString& GetShownLibrary() const { return m_library; }

    /// Lists the symbols of the given library (as when it is picked in the library choice).
    void ShowLibrary( const wxString& aNickname );

    /// Re-reads the library and the current symbol.
    void Rebuild();

    void ShowChangedLanguage();

private:
    struct SYM_ROW
    {
        wxString name;
        wxString description;
        bool     modified = false;
    };

    struct PIN_ROW
    {
        KIID     id;
        int      unit = 0;
        int      bodyStyle = 0;
    };

    wxString currentLibrary() const;
    void     rebuildSymbols();
    void     applyMask();
    void     rebuildPins();
    void     selectCurrentSymbol();
    void     openSelected();
    void     runOnSelected( int aCommand );

    void onTimer( wxTimerEvent& aEvent );
    void onSymbolSelected( wxListEvent& aEvent );
    void onSymbolContextMenu( wxListEvent& aEvent );
    void onPinSelected( wxListEvent& aEvent );
    void onPinActivated( wxListEvent& aEvent );

    SYMBOL_EDIT_FRAME*   m_frame;
    wxSearchCtrl*        m_mask;
    wxChoice*            m_libraryChoice;  ///< the library shown (Altium: the open SchLib)
    mutable wxString     m_pickedLibrary;  ///< chosen in m_libraryChoice
    mutable wxString     m_lastLoadedLib;  ///< library of the symbol last seen loaded
    wxListCtrl*          m_symbolList;
    wxButton*            m_placeBtn;
    wxButton*            m_addBtn;
    wxButton*            m_deleteBtn;
    wxButton*            m_editBtn;
    wxStaticText*        m_pinsLabel;
    wxListCtrl*          m_pinList;
    wxTimer              m_timer;

    std::vector<SYM_ROW> m_symbols;
    std::vector<size_t>  m_shown;
    std::vector<PIN_ROW> m_pins;

    wxString             m_library;
    wxString             m_symbolSig;
    wxString             m_pinSig;
    bool                 m_inSync;
};

#endif // SCH_LIBRARY_PANEL_H
