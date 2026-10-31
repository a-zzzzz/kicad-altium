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

#ifndef DIALOG_IPC_FOOTPRINT_WIZARD_H
#define DIALOG_IPC_FOOTPRINT_WIZARD_H

#include <dialog_shim.h>
#include <ipc_land_pattern.h>

#include <vector>

class FOOTPRINT;
class FOOTPRINT_EDIT_FRAME;
class IPC_DIAGRAM;
class IPC_PREVIEW;
class wxButton;
class wxCheckBox;
class wxChoice;
class wxFlexGridSizer;
class wxListCtrl;
class wxRadioButton;
class wxSimplebook;
class wxStaticText;
class wxTextCtrl;


/**
 * Altium's IPC Compliant Footprint Wizard: pick a package family, enter the package dimensions
 * from the datasheet, review the IPC-7351B values derived from them page by page, and finish
 * with a footprint in a library.
 */
class DIALOG_IPC_FOOTPRINT_WIZARD : public DIALOG_SHIM
{
public:
    enum class DESTINATION { EXISTING, NEW, CURRENT };

    DIALOG_IPC_FOOTPRINT_WIZARD( FOOTPRINT_EDIT_FRAME* aFrame, const wxString& aCurrentLibrary );

    /// The footprint described by the wizard (a new object owned by the caller), named uniquely
    /// for aLibrary.
    FOOTPRINT* BuildFootprint( const wxString& aLibrary );

    IPC::FAMILY GetFamily() const { return m_input.family; }

    DESTINATION GetDestination() const;

    /// The library chosen for EXISTING / CURRENT.
    wxString GetLibrary() const;

    /// Whether a simple 3D body (VRML box) should be written beside the library.
    bool Want3DModel() const;

    const IPC::RESULT& GetResult() const { return m_result; }

    /**
     * Writes a VRML 2.0 model of the package body (a box for the body plus the terminals) to
     * aPath.  Returns false on failure.
     */
    static bool Write3DModel( const wxString& aPath, const IPC::RESULT& aResult,
                              IPC::FAMILY aFamily );

private:
    struct RANGE_ROW
    {
        wxStaticText* label;
        wxTextCtrl*   min;
        wxTextCtrl*   max;
    };

    struct VALUE_ROW
    {
        wxStaticText* label;
        wxTextCtrl*   value;
    };

    wxPanel* makePage( const wxString& aTitle, const wxString& aSubtitle, bool aPreview );
    void     buildIntro();
    void     buildTypePage();
    void     buildDimensionsPage();
    void     buildHeelPage();
    void     buildFilletsPage();
    void     buildComponentTolerancesPage();
    void     buildIpcTolerancesPage();
    void     buildFootprintPage();
    void     buildSilkPage();
    void     buildMechanicalPage();
    void     buildDescriptionPage();
    void     buildDestinationPage();

    RANGE_ROW addRange( wxWindow* aParent, wxFlexGridSizer* aGrid, const wxString& aLabel );
    VALUE_ROW addValue( wxWindow* aParent, wxFlexGridSizer* aGrid, const wxString& aLabel );
    wxTextCtrl* numberCtrl( wxWindow* aParent );

    void   applyFamily( IPC::FAMILY aFamily );
    void   readInput();
    void   recalculate();
    void   showPage( int aPage );
    void   updateTitles();
    void   updateEnables();
    double number( wxTextCtrl* aCtrl ) const;
    void   setNumber( wxTextCtrl* aCtrl, double aValue );

    FOOTPRINT_EDIT_FRAME* m_frame;
    wxString              m_currentLibrary;
    IPC::INPUT            m_input;
    IPC::RESULT           m_result;
    bool                  m_filling = false;

    wxSimplebook*         m_book;
    std::vector<wxPanel*> m_pages;
    std::vector<wxStaticText*> m_titles;
    std::vector<IPC_PREVIEW*>  m_previews;
    std::vector<IPC_DIAGRAM*>  m_diagrams;
    wxButton*             m_cancel;
    wxButton*             m_back;
    wxButton*             m_next;
    wxButton*             m_finish;
    wxStaticText*         m_error;

    // Component type
    wxListCtrl*   m_families;
    wxStaticText* m_familyText;

    // Package dimensions
    RANGE_ROW     m_rowL, m_rowW, m_rowT, m_rowD, m_rowE;
    VALUE_ROW     m_rowHeight, m_rowPitch, m_rowPins, m_rowEpX, m_rowEpY;
    wxStaticText* m_typeLabel;
    wxChoice*     m_packageType;
    wxStaticText* m_polarityLabel;
    wxRadioButton* m_polarity[3];

    // Heel spacing
    wxCheckBox*   m_heelCalc;
    VALUE_ROW     m_sMin, m_sMax;

    // Fillets
    wxCheckBox*   m_filletDefaults;
    wxChoice*     m_density;
    VALUE_ROW     m_toe, m_heel, m_side;

    // Component tolerances
    wxCheckBox*   m_tolCalc;
    VALUE_ROW     m_tolL, m_tolS, m_tolW;

    // IPC tolerances
    wxCheckBox*   m_ipcDefaults;
    VALUE_ROW     m_fab, m_place, m_courtExcess;

    // Footprint dimensions
    wxStaticText* m_padSummary;
    wxCheckBox*   m_padCalc;
    VALUE_ROW     m_padX, m_padY, m_padC;
    wxChoice*     m_padShape;

    // Silkscreen
    VALUE_ROW     m_silkWidth;
    wxCheckBox*   m_silkCalc;
    VALUE_ROW     m_silkR1, m_silkR2;

    // Courtyard / assembly / body
    wxCheckBox*   m_addCourt;
    wxCheckBox*   m_courtCalc;
    VALUE_ROW     m_courtV1, m_courtV2, m_courtWidth;
    wxCheckBox*   m_addAssy;
    wxCheckBox*   m_assyCalc;
    VALUE_ROW     m_assyA, m_assyB, m_assyWidth;
    wxCheckBox*   m_addBody;
    wxStaticText* m_bodyInfo;

    // Description
    wxCheckBox*   m_nameSuggested;
    wxTextCtrl*   m_name;
    wxTextCtrl*   m_description;

    // Destination
    wxRadioButton* m_destExisting;
    wxRadioButton* m_destNew;
    wxRadioButton* m_destCurrent;
    wxChoice*      m_libraries;
    wxStaticText*  m_currentLabel;
    wxCheckBox*    m_make3D;
};

#endif // DIALOG_IPC_FOOTPRINT_WIZARD_H
