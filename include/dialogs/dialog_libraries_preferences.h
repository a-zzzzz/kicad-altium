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

#ifndef DIALOG_LIBRARIES_PREFERENCES_H
#define DIALOG_LIBRARIES_PREFERENCES_H

#include <dialog_shim.h>
#include <libraries/library_table.h>

#include <set>
#include <vector>

class EDA_BASE_FRAME;
class ALTIUM_SUB_TABS;
class wxDataViewListCtrl;
class wxDataViewEvent;
class wxSimplebook;


/**
 * Altium's Libraries Preferences: Project | Installed | Search Path.
 *
 * Project lists the project's library tables (symbol and footprint libraries together, with
 * their type), Installed the global tables (with an Activated check, KiCad's "enabled"), and
 * Search Path the path variables KiCad resolves libraries with.  Add Library... / Install...,
 * Remove and Move Up / Down edit the tables directly; the changes are saved when made.
 */
class DIALOG_LIBRARIES_PREFERENCES : public DIALOG_SHIM
{
public:
    enum TAB { TAB_PROJECT = 0, TAB_INSTALLED, TAB_SEARCH_PATH };

    DIALOG_LIBRARIES_PREFERENCES( EDA_BASE_FRAME* aFrame, TAB aTab = TAB_PROJECT );

    /// Shows the dialog (modal) for a frame of the project.
    static void Run( EDA_BASE_FRAME* aFrame, TAB aTab = TAB_PROJECT );

private:
    /// One listed library: its table and its row there
    struct ENTRY
    {
        LIBRARY_TABLE_TYPE  type;
        LIBRARY_TABLE_SCOPE scope;
        wxString            nickname;
    };

    void rebuild();
    void fillList( wxDataViewListCtrl* aList, LIBRARY_TABLE_SCOPE aScope, std::vector<ENTRY>& aEntries,
                   bool aActivated );
    void fillSearchPaths();

    LIBRARY_TABLE* table( LIBRARY_TABLE_TYPE aType, LIBRARY_TABLE_SCOPE aScope, bool aCreate );
    void           saved( LIBRARY_TABLE_TYPE aType, LIBRARY_TABLE_SCOPE aScope );

    void addLibraries( LIBRARY_TABLE_SCOPE aScope );
    void removeSelected( LIBRARY_TABLE_SCOPE aScope );
    void moveSelected( LIBRARY_TABLE_SCOPE aScope, int aDelta );
    void onActivatedToggled( wxDataViewEvent& aEvent );

    wxDataViewListCtrl* listFor( LIBRARY_TABLE_SCOPE aScope ) const;
    std::vector<ENTRY>& entriesFor( LIBRARY_TABLE_SCOPE aScope );
    int                 selectedEntry( LIBRARY_TABLE_SCOPE aScope ) const;
    void                selectEntry( LIBRARY_TABLE_SCOPE aScope, LIBRARY_TABLE_TYPE aType,
                                     const wxString& aNickname );

    EDA_BASE_FRAME*     m_frame;
    ALTIUM_SUB_TABS*    m_tabs;
    wxSimplebook*       m_book;
    wxDataViewListCtrl* m_projectList;
    wxDataViewListCtrl* m_installedList;
    wxDataViewListCtrl* m_searchList;
    std::vector<ENTRY>  m_projectEntries;
    std::vector<ENTRY>  m_installedEntries;
    bool                m_filling = false;
};

#endif // DIALOG_LIBRARIES_PREFERENCES_H
