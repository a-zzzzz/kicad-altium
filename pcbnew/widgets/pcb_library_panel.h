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

#ifndef PCB_LIBRARY_PANEL_H
#define PCB_LIBRARY_PANEL_H

#include <kiid.h>
#include <lib_id.h>

#include <wx/panel.h>
#include <wx/timer.h>
#include <wx/longlong.h>
#include <wx/choice.h>

#include <vector>

class FOOTPRINT;
class FOOTPRINT_EDIT_FRAME;
class LIBRARY_LIST_CTRL;
class wxListCtrl;
class wxListEvent;
class wxSearchCtrl;
class wxChoice;
class wxStaticText;
class wxButton;
class wxCheckBox;
class FOOTPRINT_PREVIEW_WIDGET;


/**
 * Altium-style "PCB Library" panel of the footprint editor: the footprints of the current
 * library (Name / Pads / Primitives), filtered by a mask, and below them the primitives of
 * the footprint being edited (Type / Name / X-Size / Y-Size / Layer).
 *
 * Double-clicking a footprint opens it; clicking a primitive selects it on the canvas and
 * double-clicking one also zooms to it and shows it in the Properties panel.
 */
class PCB_LIBRARY_PANEL : public wxPanel
{
public:
    PCB_LIBRARY_PANEL( wxWindow* aParent, FOOTPRINT_EDIT_FRAME* aFrame );
    ~PCB_LIBRARY_PANEL() override;

    /// The footprint picked in the list, or an invalid LIB_ID when none is.
    LIB_ID GetSelectedLibId() const;

    /// The library the panel lists (empty before the first rebuild).
    const wxString& GetShownLibrary() const { return m_library; }

    /// Lists the footprints of the given library (as when it is picked in the library choice).
    void ShowLibrary( const wxString& aNickname );

    /// Re-reads the library and the current footprint.
    void Rebuild();

    /// Re-applies translated column titles.
    void ShowChangedLanguage();

    struct FP_ROW
    {
        wxString name;
        int      pads = -1;        ///< -1 until known
        int      primitives = -1;
    };

    struct PRIM_ROW
    {
        KIID     id;
        wxString type;
        wxString name;
        wxString xSize;
        wxString ySize;
        wxString layer;
    };

    /// Text of a cell in the (virtual) footprint list; loads counts on demand.
    wxString FootprintCellText( long aRow, long aColumn );

private:
    wxString currentLibrary() const;
    void     rebuildFootprints();
    void     applyMask();
    void     rebuildPrimitives();
    void     countPrimitives( const FOOTPRINT* aFootprint, int& aPads, int& aPrimitives ) const;
    void     selectLoadedFootprint();

    void onTimer( wxTimerEvent& aEvent );
    void onMask( wxCommandEvent& aEvent );
    void onFootprintSelected( wxListEvent& aEvent );
    void onFootprintActivated( wxListEvent& aEvent );
    void onFootprintContextMenu( wxListEvent& aEvent );
    void onPrimitiveSelected( wxListEvent& aEvent );
    void onPrimitiveActivated( wxListEvent& aEvent );

    // Altium's highlight row: Apply / Clear / Magnify with Normal / Mask / Dim and the
    // Select / Zoom / Clear Existing options, acting on the primitives picked in the list
    void applyHighlight();
    void clearHighlight();
    void runButton( int aCommand );
    void updatePreview();

    /// A heading that folds the given window away when clicked (Altium's section headers).
    wxStaticText* addFoldingHeading( wxSizer* aSizer, const wxString& aTitle, wxWindow* aBody );

    FOOTPRINT_EDIT_FRAME* m_frame;
    wxSearchCtrl*         m_mask;
    wxChoice*             m_libraryChoice;  ///< the library shown (Altium: the open PcbLib)
    mutable wxString      m_pickedLibrary;  ///< chosen in m_libraryChoice
    mutable wxString      m_lastLoadedLib;  ///< library of the footprint last seen loaded
    LIB_ID                m_lastHighlightedLoaded; ///< loaded footprint the list last followed
    LIBRARY_LIST_CTRL*    m_footprintList;
    wxStaticText*         m_primitivesLabel;
    wxListCtrl*           m_primitiveList;
    wxChoice*             m_highlightMode;  ///< Normal / Mask / Dim
    wxCheckBox*           m_selectCheck;
    wxCheckBox*           m_zoomCheck;
    wxCheckBox*           m_clearExistingCheck;
    wxStaticText*         m_footprintsLabel;
    wxButton*             m_placeBtn;
    wxButton*             m_addBtn;
    wxButton*             m_deleteBtn;
    wxButton*             m_editBtn;
    wxStaticText*         m_otherLabel;
    wxPanel*              m_previewHolder;
    FOOTPRINT_PREVIEW_WIDGET* m_preview = nullptr;
    LIB_ID                m_previewId;
    std::vector<KIID>     m_masked;         ///< items faded by Apply (Mask / Dim)
    wxTimer               m_timer;

    std::vector<FP_ROW>   m_footprints;     ///< every footprint of the library
    std::vector<size_t>   m_shown;          ///< rows of m_footprints passing the mask
    std::vector<PRIM_ROW> m_primitives;

    wxString              m_library;        ///< library listed
    wxString              m_footprintSig;   ///< what the footprint list was built from
    long long             m_diskStamp = 0;      ///< library timestamp last read from disk
    wxString              m_diskStampLib;       ///< ... for this library
    wxLongLong            m_diskStampTime = 0;  ///< ... at this time (ms)
    wxString              m_primitiveSig;   ///< what the primitive list was built from
    bool                  m_inSync;         ///< selecting rows from code, not the user
};

#endif // PCB_LIBRARY_PANEL_H
