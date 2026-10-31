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

#ifndef LAYER_STACK_MANAGER_FRAME_H
#define LAYER_STACK_MANAGER_FRAME_H

#include <map>
#include <vector>

#include <wx/frame.h>

#include <board.h>                                  // LAYER_T
#include <board_stackup_manager/board_stackup.h>
#include <project/board_project_settings.h>         // LAYER_PAIR_INFO
#include <project/tuning_profiles.h>                // TUNING_PROFILE

class ALTIUM_MENU_STRIP;
class ALTIUM_TITLE_BAR;
class PCB_EDIT_FRAME;
class wxButton;
class wxCheckBox;
class wxChoice;
class wxGrid;
class wxListBox;
class wxSimplebook;
class wxTextCtrl;
class wxGridEvent;
class wxMenu;
class wxStaticText;
class wxPanel;
class wxToolBar;
class wxWindowDestroyEvent;


/**
 * Altium-style Layer Stack Manager: a document-like window (listed in the document tab bar as
 * "<board> [Stackup]") with three views: Stackup (an editable table of the board's physical
 * stackup), Impedance (impedance profiles: target impedance -> trace width / gap per layer) and
 * Via Types (the via spans offered while routing).  Edits are made to a working copy with its
 * own undo/redo; Save writes the stackup, copper layer count, enabled layers, copper layer
 * names/types, impedance profiles (project file, "tuning profiles") and via types (project
 * file, layer pair presets) back (docs/altium-parity/layer-stack-manager.md).
 */
class LAYER_STACK_MANAGER_FRAME : public wxFrame
{
public:
    /// Open (or bring to front) the Layer Stack Manager of \a aFrame's board.
    static void ShowFor( PCB_EDIT_FRAME* aFrame );

    ~LAYER_STACK_MANAGER_FRAME() override;

#ifdef __WXMSW__
    /// The Altium title bar replaces the native caption, as in the editor windows
    WXLRESULT MSWWindowProc( WXUINT aMsg, WXWPARAM aWParam, WXLPARAM aLParam ) override;
#endif

private:
    /// The editable model: everything the window can change on the board.
    struct STATE
    {
        BOARD_STACKUP                    m_stackup;
        LSET                             m_layers;   ///< Copper + mask/paste/silk layers in the stack
        std::map<PCB_LAYER_ID, wxString> m_names;    ///< Copper layer (board) names
        std::map<PCB_LAYER_ID, LAYER_T>  m_types;    ///< Copper layer types

        std::vector<TUNING_PROFILE>      m_profiles;     ///< Impedance profiles
        std::vector<wxString>            m_profileOrig;  ///< Name of each profile when loaded
        std::vector<LAYER_PAIR_INFO>     m_viaTypes;     ///< Blind / buried / micro via spans

        bool operator==( const STATE& aOther ) const;
        int  CopperCount() const;
    };

    /// One grid row: a stackup item, and for dielectrics one of its sub-layers.
    struct ROW
    {
        int m_item;
        int m_sub;
    };

    LAYER_STACK_MANAGER_FRAME( PCB_EDIT_FRAME* aFrame );

    void buildMenuBar();
    void buildToolbar( wxWindow* aParent );
    void buildGrid( wxWindow* aParent );
    wxWindow* buildProperties( wxWindow* aParent );
    void      refreshProperties();
    void      enableTool( int aId, bool aEnable );

    /// Fill one of the read-only stack tables shown beside the Impedance and Via Types views.
    void      fillStackView( wxGrid* aGrid, const std::vector<int>& aColumns );
    void      onAddButton();
    void      onDeleteButton();
    wxWindow* buildImpedancePage( wxWindow* aParent );
    wxWindow* buildViaTypesPage( wxWindow* aParent );
    void      showView( int aView );
    void      styleCaption();

    STATE loadFromBoard() const;
    void  rebuildStackup( STATE& aState ) const;
    bool  applyToBoard();
    void  refreshPcbEditor();
    void  syncFromBoard();

    void refreshGrid();
    void refreshViews();
    void fillRow( int aRow, int& aCopperIndex );

    // Impedance view
    void syncDerived( STATE& aState ) const;
    void computeEntry( const STATE& aState, const TUNING_PROFILE& aProfile,
                       DELAY_PROFILE_TRACK_PROPAGATION_ENTRY& aEntry, bool aSolveWidth ) const;
    bool evaluateEntry( const STATE& aState, const TUNING_PROFILE& aProfile,
                        const DELAY_PROFILE_TRACK_PROPAGATION_ENTRY& aEntry, double& aImpedance,
                        double& aDelayPsPerMm, wxString& aError ) const;
    void refreshImpedance();
    void refreshProfileList();
    void addProfile( bool aDifferential );
    void deleteProfile();
    void recalculateProfile();
    void onProfilePropertyChanged( int aWhich );
    void onImpedanceCellChanged( wxGridEvent& aEvent );
    TUNING_PROFILE* currentProfile();

    // Via Types view
    void refreshViaTypes();
    void addViaType();
    void deleteViaType();
    void onViaCellChanged( wxGridEvent& aEvent );
    wxString viaTypeKind( PCB_LAYER_ID aFirst, PCB_LAYER_ID aLast ) const;

    void updateChrome();
    void setStatus( const wxString& aText, bool aError = false );

    bool isDirty() const { return !( m_state == m_saved ); }
    int  selectedRow() const;
    void selectRow( int aItem, int aSub );
    void commitChange( const STATE& aBefore, const wxString& aMessage = wxEmptyString );

    bool applyCellEdit( int aRow, int aCol, const wxString& aValue, wxString& aError );
    bool validateLayerName( PCB_LAYER_ID aLayer, const wxString& aName, wxString& aError ) const;
    wxString layerDisplayName( PCB_LAYER_ID aLayer ) const;

    bool canInsertDielectric( bool aAbove, int* aItem = nullptr, int* aPos = nullptr,
                              int* aSource = nullptr ) const;
    bool canDelete() const;
    bool canMove( int aDir ) const;

    void addLayerPair();
    void addTechLayer( PCB_LAYER_ID aLayer );
    void insertDielectric( bool aAbove );
    void deleteLayer();
    void moveSublayer( int aDir );
    void chooseMaterial( int aRow );
    void pickCustomColour( int aRow );
    void undo();
    void redo();
    void revert();

    void fillAddMenu( wxMenu& aMenu ) const;

    void onCellChanged( wxGridEvent& aEvent );
    void onCellRightClick( wxGridEvent& aEvent );
    void onClose( wxCloseEvent& aEvent );
    void onActivate( wxActivateEvent& aEvent );
    void onPcbClose( wxCloseEvent& aEvent );
    void onPcbDestroy( wxWindowDestroyEvent& aEvent );
    void onUnitsChanged( wxCommandEvent& aEvent );

    PCB_EDIT_FRAME*    m_frame;
    BOARD*             m_board;
    bool               m_frameAlive;

    STATE              m_state;      ///< What the window shows
    STATE              m_saved;      ///< The board's state when last loaded or saved
    std::vector<STATE> m_undo;
    std::vector<STATE> m_redo;
    std::vector<ROW>   m_rows;

    ALTIUM_TITLE_BAR*  m_titleBar;
    ALTIUM_MENU_STRIP* m_menuStrip;
    wxWindow*          m_toolbar;
    std::map<int, wxWindow*> m_tools;    ///< Toolbar buttons by command id
    wxCheckBox*        m_impedance;

    // Properties panel (Altium's Layer / Substack sections)
    wxWindow*          m_props;
    wxTextCtrl*        m_propName;
    wxTextCtrl*        m_propComment;
    wxChoice*          m_propColour;
    wxStaticText*      m_propColourLabel;
    wxTextCtrl*        m_propStackName;
    wxStaticText*      m_propLayers;
    wxStaticText*      m_propDielectrics;
    wxStaticText*      m_propConductive;
    wxStaticText*      m_propDielectricThk;
    wxStaticText*      m_propTotal;
    wxSimplebook*      m_book;
    wxWindow*          m_viewTabs;
    int                m_view;
    wxGrid*            m_grid;

    // Impedance view
    wxListBox*         m_profileList;
    wxTextCtrl*        m_profileName;
    wxChoice*          m_profileType;
    wxTextCtrl*        m_profileTarget;
    wxButton*          m_profileDelete;
    wxButton*          m_profileRecalc;
    wxGrid*            m_impGrid;
    int                m_profileSel;
    std::vector<PCB_LAYER_ID> m_impRows;

    // Read-only stack tables of the Impedance and Via Types views, and the via drawing
    wxGrid*            m_impStack = nullptr;
    wxGrid*            m_viaStack = nullptr;
    wxWindow*          m_viaDrawing = nullptr;
    wxWindow*          m_impEmpty = nullptr;    ///< "Add Impedance Profile" (no profiles yet)
    wxWindow*          m_impBody = nullptr;     ///< Profile controls and table

    // Via Types view
    wxGrid*            m_viaGrid;
    wxButton*          m_viaDelete;
    wxStaticText*      m_viaInfo;

    std::map<wxGrid*, std::vector<int>> m_colBase;   ///< Natural column widths, before stretching
    wxStaticText*      m_status;
    wxStaticText*      m_layerCount;
    wxStaticText*      m_thickness;

    bool m_refreshingVias = false;
    int  m_lastViaRow = -1;          ///< via grid row the drawing last showed as selected   ///< refreshViaTypes() is moving the via grid cursor
};

#endif // LAYER_STACK_MANAGER_FRAME_H
