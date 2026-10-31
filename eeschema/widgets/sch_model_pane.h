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

#ifndef SCH_MODEL_PANE_H
#define SCH_MODEL_PANE_H

#include <lib_id.h>
#include <wx/panel.h>
#include <wx/timer.h>

#include <vector>

class SCH_BASE_FRAME;
class SYMBOL_EDIT_FRAME;
class FOOTPRINT_PREVIEW_WIDGET;
class wxListCtrl;
class wxListEvent;
class wxButton;
class wxStaticText;


/**
 * Altium's "Browse Libraries" for footprints: a library, its footprints (with a mask) and a
 * preview.  Opens on  aStart's library, else the project's own.  Invalid when cancelled.
 */
LIB_ID BrowseFootprintLibraries( SCH_BASE_FRAME* aFrame, const LIB_ID& aStart );


/**
 * Altium's SCH Library "Editor" pane under the symbol canvas: the models of the symbol being
 * edited (Model / Type / Location / Description), Add Footprint / Remove / Edit... below them
 * and a preview of the picked model at the right.
 *
 * KiCad keeps one footprint per symbol (its Footprint field) and an optional simulation model
 * (the Sim.* fields); both are listed.
 */
class SCH_MODEL_PANE : public wxPanel
{
public:
    SCH_MODEL_PANE( wxWindow* aParent, SYMBOL_EDIT_FRAME* aFrame );
    ~SCH_MODEL_PANE() override;

    static wxString PaneName() { return wxS( "SchModelEditor" ); }

    /// Re-reads the current symbol's models.
    void Rebuild();

    /// Opens the PCB Model dialog to add (or, with aEdit, change) the symbol's footprint.
    void EditFootprint( bool aEdit );

private:
    struct ROW
    {
        bool     footprint = true;
        wxString model;
        wxString type;
        wxString location;
        wxString description;
        LIB_ID   fpid;
    };

    void onTimer( wxTimerEvent& aEvent );
    void onSelected( wxListEvent& aEvent );
    void onAddMenu();
    void removeModel();
    void updatePreview();
    void setFootprint( const wxString& aFootprint );

    SYMBOL_EDIT_FRAME*        m_frame;
    wxListCtrl*               m_list;
    wxButton*                 m_addBtn;
    wxButton*                 m_addMenuBtn;
    wxButton*                 m_removeBtn;
    wxButton*                 m_editBtn;
    wxPanel*                  m_previewHolder;
    wxStaticText*             m_noPreview;
    FOOTPRINT_PREVIEW_WIDGET* m_preview = nullptr;
    wxString                  m_previewId;
    std::vector<ROW>          m_rows;
    wxString                  m_sig;
    wxTimer                   m_timer;
};

#endif // SCH_MODEL_PANE_H
