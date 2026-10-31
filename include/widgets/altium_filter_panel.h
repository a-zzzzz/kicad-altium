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

#pragma once

#include <vector>
#include <altium_query.h>
#include <wx/panel.h>

class EDA_DRAW_FRAME;
class wxButton;
class wxCheckBox;
class wxChoice;
class wxRadioButton;
class wxSlider;
class wxStaticText;
class wxStyledTextCtrl;
class wxStyledTextEvent;


/**
 * Altium's PCB Filter / SCH Filter panel: a query in Altium's language (with Helper, History
 * and Favorites) applied to the document, selecting and zooming to the matching objects and
 * masking the others.  The editor-specific parts (which objects there are and what applying
 * does) come from a derived class.
 */
class ALTIUM_FILTER_PANEL : public wxPanel
{
public:
    enum class MODE { PCB, SCH };

    enum MASK_MODE { MASK = 0, DIM, NORMAL };

    struct OPTIONS
    {
        bool   selectedOnly = false;      ///< PCB Apply to Selected / SCH Selected Objects
        bool   nonSelectedOnly = false;   ///< SCH Non Selected Objects
        int    scope = 0;                 ///< SCH: 0 current document, 1 open, 2 whole project
        bool   select = false;
        bool   zoom = true;
        bool   deselect = true;
        bool   mask = true;
        double maskLevel = 0.8;           ///< 0 (none) .. 1 (invisible)
    };

    ALTIUM_FILTER_PANEL( wxWindow* aParent, EDA_DRAW_FRAME* aFrame, MODE aMode,
                         ALTIUM_QUERY::ADAPTER& aAdapter );
    ~ALTIUM_FILTER_PANEL() override;

    void     FocusQuery();
    wxString GetQuery() const;
    void     SetQuery( const wxString& aQuery );

    /// Removes the mask and the filter (Altium's Clear, Shift+C).
    void ClearFilter();

protected:
    /**
     * Applies the compiled query to the document: selects, zooms and masks as asked.
     * @return the number of matching objects, or -1 with aMessage set on failure.
     */
    virtual int applyQuery( ALTIUM_QUERY::QUERY& aQuery, const OPTIONS& aOptions,
                            wxString& aMessage ) = 0;

    /// Removes the mask set by the last applyQuery().
    virtual void clearMask() = 0;

    /// PCB builder: the board's layers by their Altium-style names (top to bottom).
    virtual std::vector<wxString> builderLayers() const { return {}; }

    /// Refreshes the builder's layer list (after the board changed).
    void RebuildLayerList();

private:
    void buildPcbBuilder( wxSizer* aSizer );
    void buildSchHeader( wxSizer* aSizer );
    void updateQueryFromBuilder();
    void onApply( bool aSelectedOnly );
    void onStyleNeeded( wxStyledTextEvent& aEvent );
    void styleQuery();
    void showError( const wxString& aMessage, int aPos );
    void showStatus( const wxString& aMessage );

    void showHelper();
    void showHistoryMenu( wxWindow* aButton );
    void showFavoritesMenu( wxWindow* aButton );
    void addToFavorites();

    void loadLists();
    void saveLists();
    void rememberQuery( const wxString& aQuery );

    EDA_DRAW_FRAME*        m_frame;
    MODE                   m_mode;
    ALTIUM_QUERY::ADAPTER& m_adapter;
    ALTIUM_QUERY::QUERY    m_query;

    wxStyledTextCtrl*      m_queryCtrl = nullptr;
    wxStaticText*          m_status = nullptr;

    // PCB
    struct OBJECT_ROW
    {
        wxString    keyword;
        wxCheckBox* net = nullptr;
        wxCheckBox* comp = nullptr;
        wxCheckBox* free = nullptr;
    };

    std::vector<OBJECT_ROW>  m_objectRows;
    wxWindow*                m_layerList = nullptr;
    std::vector<wxCheckBox*> m_layerChecks;
    std::vector<wxString>    m_layerKeys;
    bool                     m_settingFromBuilder = false;
    wxChoice*                m_maskChoice = nullptr;

    // SCH
    wxRadioButton* m_limitAll = nullptr;
    wxRadioButton* m_limitSelected = nullptr;
    wxRadioButton* m_limitNonSelected = nullptr;
    wxChoice*      m_scopeChoice = nullptr;
    wxCheckBox*    m_maskCheck = nullptr;
    wxSlider*      m_maskSlider = nullptr;

    wxCheckBox*    m_selectCheck = nullptr;
    wxCheckBox*    m_zoomCheck = nullptr;
    wxCheckBox*    m_deselectCheck = nullptr;
    wxButton*      m_clearBtn = nullptr;

    std::vector<wxString>                          m_history;
    std::vector<std::pair<wxString, wxString>>     m_favorites;   ///< name, query
};
