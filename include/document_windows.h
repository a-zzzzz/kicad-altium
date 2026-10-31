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

#ifndef DOCUMENT_WINDOWS_H
#define DOCUMENT_WINDOWS_H

#include <kicommon.h>
#include <functional>
#include <vector>

#include <wx/event.h>

class wxTopLevelWindow;
class wxWindow;

/// Sent to a document window the first time it comes on screen after loading in the
/// background, so it can redo what needed a laid-out canvas (zoom to fit)
wxDECLARE_EXPORTED_EVENT( KICOMMON_API, EDA_EVT_DOCUMENT_FIRST_SHOWN, wxCommandEvent );

/**
 * Makes the separate editor frames behave as one Altium-style main window
 * (docs/altium-parity/documents.md).
 *
 * Every frame that shows the document tabs (schematic, PCB, footprint and symbol editors,
 * Layer Stack Manager, project manager) is a "document window".  The state lives in kicommon
 * so the frames of every kiface share it.  Only one of them is visible
 * at a time: activating a document gives it the current window's placement (position, size,
 * maximized state) and hides ("parks") the others together with their floating tool windows,
 * so the taskbar and Alt+Tab show a single window.  Closing that window with its title-bar
 * close button closes all documents; closing a document from its tab or menu closes just that
 * one and shows the previously used document.
 */
namespace DOCUMENT_WINDOWS
{
/// Make \a aWindow a document window.  Called by the document tab bar it owns.
KICOMMON_API void Register( wxTopLevelWindow* aWindow );

KICOMMON_API bool IsDocument( const wxWindow* aWindow );

/// True for a document window that is open but hidden behind the current document.
KICOMMON_API bool IsParked( const wxWindow* aWindow );

/// True for a document window that is shown or parked (i.e. open), or any other shown window.
KICOMMON_API bool IsOpen( const wxWindow* aWindow );

/// True while a document switch is showing / hiding windows.
KICOMMON_API bool InSwitch();

/**
 * Called when a document takes another's place, before it appears: first with aSave = true
 * for the window being left (record what should carry over, e.g. panel column widths, with
 * SetCarryOver), then with aSave = false for the window coming up (apply it).  Each editor
 * module registers its own hook, since its panels are only reachable from inside it.
 */
using SWITCH_HOOK = std::function<void( wxTopLevelWindow* aWindow, bool aSave )>;
KICOMMON_API void AddSwitchHook( SWITCH_HOOK aHook );

/// Values carried from the document being left to the one coming up (see AddSwitchHook).
KICOMMON_API void     SetCarryOver( const wxString& aKey, const wxString& aValue );
KICOMMON_API wxString GetCarryOver( const wxString& aKey );

/// Bring \a aWindow up in the current window's place and park the other documents.
KICOMMON_API void Activate( wxTopLevelWindow* aWindow );

/// Activate a document window, or just bring any other window to the front.
KICOMMON_API void SwitchTo( wxTopLevelWindow* aWindow );

/**
 * Open a document window that isn't shown yet as a background tab, keeping the current
 * document in front (e.g. editors loaded only to run output jobs).  Shows it normally when
 * nothing else is on screen, or when \a aWindow isn't a document window.
 */
KICOMMON_API void OpenInBackground( wxTopLevelWindow* aWindow );

/// The visible document window used most recently, or nullptr.
KICOMMON_API wxTopLevelWindow* Current();

/// The open documents in opening order, then the other visible KiCad frames (tab order).
KICOMMON_API std::vector<wxTopLevelWindow*> TabWindows();

/// Show every open document side by side (for Window > Tile / Cascade).
KICOMMON_API void ShowAll();

/// Close every document, as closing Altium's main window does (save prompts included).
KICOMMON_API void CloseAll();
}

#endif // DOCUMENT_WINDOWS_H
