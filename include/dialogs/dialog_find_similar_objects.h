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

#ifndef DIALOG_FIND_SIMILAR_OBJECTS_H
#define DIALOG_FIND_SIMILAR_OBJECTS_H

#include <dialog_shim.h>
#include <wx/variant.h>
#include <functional>
#include <vector>

class EDA_DRAW_FRAME;
class EDA_ITEM;
class PROPERTY_BASE;
class wxCheckBox;
class wxGrid;

/**
 * Altium-style "Find Similar Objects", editor-neutral: list the reference object's properties,
 * let the user mark each Any / Same / Different, and collect the candidates of the same kind
 * that match (docs/altium-parity/find-similar.md).
 */
class DIALOG_FIND_SIMILAR_OBJECTS : public DIALOG_SHIM
{
public:
    /// Shows a property value as text (editors add their own kinds, e.g. nets).
    using FORMATTER = std::function<wxString( PROPERTY_BASE*, const wxVariant& )>;

    /**
     * @param aCandidates objects to search; only those of the reference's kind are considered.
     * @param aFormatter  value text; defaults to PROPERTY_VALUE_TEXT::Format in frame units.
     */
    DIALOG_FIND_SIMILAR_OBJECTS( EDA_DRAW_FRAME* aFrame, EDA_ITEM* aReference,
                                 const std::vector<EDA_ITEM*>& aCandidates,
                                 FORMATTER aFormatter = nullptr );

    /// Objects matching the chosen rules; valid after the dialog returns wxID_OK.
    const std::vector<EDA_ITEM*>& GetMatches() const { return m_matches; }

    bool ClearExistingSelection() const;
    bool ZoomToMatches() const;

private:
    bool TransferDataFromWindow() override;

    struct ROW
    {
        PROPERTY_BASE* property;
        wxVariant      value;
    };

    EDA_DRAW_FRAME*        m_frame;
    EDA_ITEM*              m_reference;
    std::vector<EDA_ITEM*> m_candidates;
    std::vector<ROW>       m_rows;
    std::vector<int>       m_gridRows;   ///< Grid row -> index in m_rows, -1 for group headers
    std::vector<EDA_ITEM*> m_matches;

    wxGrid*                m_grid;
    wxCheckBox*            m_clearExisting;
    wxCheckBox*            m_zoomToMatches;
};

#endif // DIALOG_FIND_SIMILAR_OBJECTS_H
