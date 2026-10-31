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

#ifndef MESSAGES_STORE_H
#define MESSAGES_STORE_H

#include <kicommon.h>
#include <widgets/report_severity.h>
#include <bitmaps/bitmaps_list.h>

#include <wx/datetime.h>
#include <wx/string.h>

#include <functional>
#include <vector>

class wxTopLevelWindow;


/// An object a message is about (Altium's Details tree under the message: "Pin U4-7", ...).
struct PROJECT_MESSAGE_DETAIL
{
    wxString              text;
    BITMAPS               icon = BITMAPS::INVALID_BITMAP;
    std::function<void()> activate;         ///< double-click: select / zoom to the object
};


/// One row of the Messages panel.
struct PROJECT_MESSAGE
{
    SEVERITY              severity = RPT_SEVERITY_INFO;
    wxString              document;
    wxString              source;
    wxString              text;
    wxString              key;              ///< stable identity (marker UUID) to keep its time
    bool                  fromMarker = false;
    wxDateTime            when;
    std::function<void()> activate;         ///< double-click action; may be empty
    wxTopLevelWindow*     owner = nullptr;  ///< document window the row is about
    const void*           origin = nullptr; ///< who reported it (a Messages panel)
    std::vector<PROJECT_MESSAGE_DETAIL> details;   ///< the objects involved
};


/**
 * The session's messages, shared by the Messages panels of every editor (schematic, PCB, ...)
 * as Altium has one Messages panel for the whole project: DRC results show in the schematic's
 * panel and ERC results in the PCB's.  Lives in kicommon so all editor modules see one list.
 */
namespace MESSAGES_STORE
{
KICOMMON_API const std::vector<PROJECT_MESSAGE>& Messages();

/// Append a report row.
KICOMMON_API void Add( PROJECT_MESSAGE aMessage );

/**
 * Replace the marker rows reported by \a aOrigin with \a aMarkers.  A marker that was already
 * listed (same key) keeps the time it was first reported.
 *
 * @return true if a new error or warning was added.
 */
KICOMMON_API bool SetMarkers( const void* aOrigin, std::vector<PROJECT_MESSAGE> aMarkers );

/**
 * The results of a project compile.  As in Altium, a compile replaces the previous compiler
 * rows (keys starting with "compiler:") and the rows of an earlier Engineering Change Order,
 * which a recompile makes stale.  A message still present keeps the time it was first reported.
 */
KICOMMON_API void SetCompilerMessages( std::vector<PROJECT_MESSAGE> aMessages );

/// Forget the rows reported by \a aOrigin (its editor is closing).
KICOMMON_API void RemoveOrigin( const void* aOrigin );

KICOMMON_API void Clear();

/// Called after every change; returns an id for RemoveListener().
KICOMMON_API int  AddListener( std::function<void()> aListener );
KICOMMON_API void RemoveListener( int aId );
}

#endif // MESSAGES_STORE_H
