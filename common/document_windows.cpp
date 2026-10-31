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

#include <document_windows.h>

#include <dialog_shim.h>

#include <algorithm>
#include <map>

#include <wx/app.h>
#include <wx/dialog.h>
#include <wx/frame.h>
#include <wx/toplevel.h>
#include <wx/weakref.h>

#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#include <commctrl.h>
#pragma comment( lib, "comctl32.lib" )
#endif


wxDEFINE_EVENT( EDA_EVT_DOCUMENT_FIRST_SHOWN, wxCommandEvent );


namespace
{
struct DOC
{
    wxTopLevelWindow*                          window;
    bool                                       parked = false;

    /// Been on screen: a document loaded in the background fits its view on first showing
    bool                                       shownOnce = false;

    /// Floating tool windows / modeless dialogs hidden together with the parked document
    std::vector<wxWeakRef<wxTopLevelWindow>>   hiddenOwned;
};

std::vector<DOC>               s_docs;          ///< in opening order
std::vector<wxTopLevelWindow*> s_mru;           ///< most recently activated first
int                            s_switching = 0;
bool                           s_closingAll = false;
bool                           s_ensurePending = false;


struct SWITCH_GUARD
{
    SWITCH_GUARD()  { ++s_switching; }
    ~SWITCH_GUARD() { --s_switching; }
};


DOC* findDoc( const wxWindow* aWindow )
{
    for( DOC& doc : s_docs )
    {
        if( doc.window == aWindow )
            return &doc;
    }

    return nullptr;
}


/// Closing windows stay on screen until the next idle; treat them as gone.
bool isDying( const wxWindow* aWindow )
{
    return aWindow->IsBeingDeleted()
           || ( wxTheApp
                && wxTheApp->IsScheduledForDestruction( const_cast<wxWindow*>( aWindow ) ) );
}


bool isOpenDoc( const DOC& aDoc )
{
    return !isDying( aDoc.window ) && ( aDoc.parked || aDoc.window->IsShown() );
}


void touchMru( wxTopLevelWindow* aWindow )
{
    s_mru.erase( std::remove( s_mru.begin(), s_mru.end(), aWindow ), s_mru.end() );
    s_mru.insert( s_mru.begin(), aWindow );
}


/// The document (or other top-level window) a floating window belongs to.
wxTopLevelWindow* owningDocument( wxWindow* aWindow )
{
    for( wxWindow* parent = aWindow->GetParent(); parent; parent = parent->GetParent() )
    {
        if( findDoc( parent ) )
            return static_cast<wxTopLevelWindow*>( parent );
    }

    return nullptr;
}


/// The project manager frame (window name as set by KICAD_MANAGER_FRAME).  This file is in
/// kicommon, which can't see the frame classes, so it is recognised by name.
bool isManager( const wxWindow* aWindow )
{
    return aWindow->GetName() == wxS( "KicadFrame" );
}


/// The project manager is the "home" document; register it even if it has no tab bar.
void registerManager()
{
    for( wxWindow* win : wxTopLevelWindows )
    {
        wxFrame* frame = dynamic_cast<wxFrame*>( win );

        if( frame && isManager( frame ) && !isDying( frame ) && !findDoc( frame )
            && frame->IsShown() )
        {
            DOCUMENT_WINDOWS::Register( frame );
        }
    }
}


wxTopLevelWindow* findManager()
{
    for( const DOC& doc : s_docs )
    {
        if( isManager( doc.window ) && !isDying( doc.window ) )
            return doc.window;
    }

    return nullptr;
}


/// Give \a aTarget the window placement (rect and maximized state) of \a aSource.
void copyPlacement( wxTopLevelWindow* aSource, wxTopLevelWindow* aTarget )
{
#ifdef __WXMSW__
    HWND            src = (HWND) aSource->GetHWND();
    HWND            tgt = (HWND) aTarget->GetHWND();
    WINDOWPLACEMENT wp = {};
    wp.length = sizeof( wp );

    if( !src || !tgt || !::GetWindowPlacement( src, &wp ) )
        return;

    // A minimized source still knows whether it will restore maximized
    bool maximized = wp.showCmd == SW_SHOWMAXIMIZED
                     || ( wp.showCmd == SW_SHOWMINIMIZED && ( wp.flags & WPF_RESTORETOMAXIMIZED ) );

    // Keep wx's idea of the state in sync (for a hidden window this only records it)
    if( aTarget->IsIconized() )
        aTarget->Iconize( false );

    aTarget->Maximize( maximized );

    wp.flags = 0;
    wp.showCmd = !aTarget->IsShown() ? SW_HIDE
                                     : ( maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL );
    ::SetWindowPlacement( tgt, &wp );
#else
    if( aTarget->IsIconized() )
        aTarget->Iconize( false );

    if( aSource->IsMaximized() )
    {
        aTarget->Maximize( true );
    }
    else
    {
        if( aTarget->IsMaximized() )
            aTarget->Maximize( false );

        aTarget->SetSize( aSource->GetRect() );
    }
#endif
}


/**
 * wx remembers the full-screen size of a hidden window told to maximise and reports it as the
 * window's size until the window is shown with Show().  ShowWithoutActivating() doesn't clear
 * it, so a document that was maximised when saved but comes up at normal size would keep
 * laying itself out full-screen (caption buttons off the edge, status bar in mid-window).
 */
struct PENDING_SIZE_RESET : public wxTopLevelWindow
{
    static void Reset( wxTopLevelWindow* aWindow )
    {
#if wxUSE_DEFERRED_SIZING
        static_cast<PENDING_SIZE_RESET*>( aWindow )->m_pendingSize = wxDefaultSize;
#endif
    }
};


/// Make wx's idea of the window's size match the real one and lay it out again.
void resync( wxTopLevelWindow* aWindow )
{
    PENDING_SIZE_RESET::Reset( aWindow );
    aWindow->SendSizeEvent();

    if( wxFrame* frame = dynamic_cast<wxFrame*>( aWindow ) )
        frame->SendSizeEvent();

    aWindow->Layout();
}


void park( DOC& aDoc )
{
    aDoc.shownOnce = true;

    for( wxWindow* win : wxTopLevelWindows )
    {
        wxTopLevelWindow* tlw = dynamic_cast<wxTopLevelWindow*>( win );

        if( !tlw || tlw == aDoc.window || !tlw->IsShown() || findDoc( tlw ) || isDying( tlw )
            || owningDocument( tlw ) != aDoc.window )
        {
            continue;
        }

        // Never hide a window someone is waiting on (a disabled document has a modal or
        // quasi-modal dialog open)
        if( !aDoc.window->IsEnabled() && dynamic_cast<wxDialog*>( tlw ) )
            continue;

        if( wxDialog* dlg = dynamic_cast<wxDialog*>( tlw ) )
        {
            if( dlg->IsModal() )
                continue;
        }

        if( DIALOG_SHIM* dlg = dynamic_cast<DIALOG_SHIM*>( tlw ) )
        {
            if( dlg->IsQuasiModal() )
                continue;
        }

        tlw->Hide();
        aDoc.hiddenOwned.emplace_back( tlw );
    }

    aDoc.parked = true;
    aDoc.window->Hide();
}


void restoreOwned( DOC& aDoc )
{
    for( wxWeakRef<wxTopLevelWindow>& owned : aDoc.hiddenOwned )
    {
        if( owned && !isDying( owned.get() ) )
            owned->Show( true );
    }

    aDoc.hiddenOwned.clear();
}


void ensureVisible()
{
    s_ensurePending = false;

    if( s_closingAll || s_switching )
        return;

    for( const DOC& doc : s_docs )
    {
        if( doc.window->IsShown() && !isDying( doc.window ) )
            return;
    }

    // The current document went away: show the one used before it, else the first one open
    for( wxTopLevelWindow* win : s_mru )
    {
        DOC* doc = findDoc( win );

        if( doc && isOpenDoc( *doc ) )
        {
            DOCUMENT_WINDOWS::Activate( win );
            return;
        }
    }

    for( DOC& doc : s_docs )
    {
        if( isOpenDoc( doc ) )
        {
            DOCUMENT_WINDOWS::Activate( doc.window );
            return;
        }
    }
}


void scheduleEnsureVisible()
{
    if( s_ensurePending || !wxTheApp )
        return;

    s_ensurePending = true;
    wxTheApp->CallAfter( []() { ensureVisible(); } );
}


bool otherOpenDocuments( const wxWindow* aWindow )
{
    for( const DOC& doc : s_docs )
    {
        if( doc.window != aWindow && isOpenDoc( doc ) )
            return true;
    }

    return false;
}


#ifdef __WXMSW__
/// Sees the window's own close commands (title-bar close button, Alt+F4, system menu,
/// taskbar "Close window"): those close the whole "main window", i.e. every document.
LRESULT CALLBACK documentSubclassProc( HWND aHwnd, UINT aMsg, WPARAM aWParam, LPARAM aLParam,
                                       UINT_PTR aId, DWORD_PTR aData )
{
    if( aMsg == WM_SYSCOMMAND && ( aWParam & 0xFFF0 ) == SC_CLOSE )
    {
        wxTopLevelWindow* win = reinterpret_cast<wxTopLevelWindow*>( aData );

        if( findDoc( win ) && otherOpenDocuments( win ) )
        {
            DOCUMENT_WINDOWS::CloseAll();
            return 0;
        }
    }
    else if( aMsg == WM_NCDESTROY )
    {
        ::RemoveWindowSubclass( aHwnd, documentSubclassProc, aId );
    }

    return ::DefSubclassProc( aHwnd, aMsg, aWParam, aLParam );
}
#endif
} // namespace


std::vector<DOCUMENT_WINDOWS::SWITCH_HOOK>& switchHooks()
{
    static std::vector<DOCUMENT_WINDOWS::SWITCH_HOOK> hooks;
    return hooks;
}


std::map<wxString, wxString>& carryOver()
{
    static std::map<wxString, wxString> values;
    return values;
}


void DOCUMENT_WINDOWS::AddSwitchHook( SWITCH_HOOK aHook )
{
    switchHooks().push_back( std::move( aHook ) );
}


void DOCUMENT_WINDOWS::SetCarryOver( const wxString& aKey, const wxString& aValue )
{
    carryOver()[aKey] = aValue;
}


wxString DOCUMENT_WINDOWS::GetCarryOver( const wxString& aKey )
{
    auto it = carryOver().find( aKey );
    return it == carryOver().end() ? wxString() : it->second;
}


void DOCUMENT_WINDOWS::Register( wxTopLevelWindow* aWindow )
{
    if( !aWindow || findDoc( aWindow ) )
        return;

    s_docs.push_back( { aWindow } );

    aWindow->Bind( wxEVT_SHOW,
                   [aWindow]( wxShowEvent& aEvent )
                   {
                       aEvent.Skip();

                       if( aEvent.GetEventObject() != aWindow || s_switching )
                           return;

                       if( aEvent.IsShown() )
                       {
                           // Shown by code that doesn't go through Activate() (e.g. a plain
                           // wxFrame): finish the switch once the show is complete
                           aWindow->CallAfter(
                                   [aWindow]()
                                   {
                                       if( findDoc( aWindow ) && aWindow->IsShown()
                                           && !isDying( aWindow ) )
                                       {
                                           DOCUMENT_WINDOWS::Activate( aWindow );
                                       }
                                   } );
                       }
                       else
                       {
                           scheduleEnsureVisible();
                       }
                   } );

    aWindow->Bind( wxEVT_ACTIVATE,
                   [aWindow]( wxActivateEvent& aEvent )
                   {
                       aEvent.Skip();

                       if( aEvent.GetActive() && aEvent.GetEventObject() == aWindow )
                           touchMru( aWindow );
                   } );

    aWindow->Bind( wxEVT_CLOSE_WINDOW,
                   []( wxCloseEvent& aEvent )
                   {
                       aEvent.Skip();
                       scheduleEnsureVisible();
                   } );

    aWindow->Bind( wxEVT_DESTROY,
                   [aWindow]( wxWindowDestroyEvent& aEvent )
                   {
                       aEvent.Skip();

                       // Destroy events of child windows propagate up to here too
                       if( aEvent.GetEventObject() != aWindow )
                           return;

                       s_docs.erase( std::remove_if( s_docs.begin(), s_docs.end(),
                                                     [aWindow]( const DOC& aDoc )
                                                     {
                                                         return aDoc.window == aWindow;
                                                     } ),
                                     s_docs.end() );

                       s_mru.erase( std::remove( s_mru.begin(), s_mru.end(), aWindow ),
                                    s_mru.end() );

                       scheduleEnsureVisible();
                   } );

#ifdef __WXMSW__
    if( HWND hwnd = (HWND) aWindow->GetHWND() )
    {
        ::SetWindowSubclass( hwnd, documentSubclassProc, 1,
                             reinterpret_cast<DWORD_PTR>( aWindow ) );
    }
#endif

    if( aWindow->IsShown() )
        touchMru( aWindow );
}


bool DOCUMENT_WINDOWS::IsDocument( const wxWindow* aWindow )
{
    return findDoc( aWindow ) != nullptr;
}


bool DOCUMENT_WINDOWS::IsParked( const wxWindow* aWindow )
{
    const DOC* doc = findDoc( aWindow );
    return doc && doc->parked && !isDying( aWindow );
}


bool DOCUMENT_WINDOWS::IsOpen( const wxWindow* aWindow )
{
    if( !aWindow )
        return false;

    if( const DOC* doc = findDoc( aWindow ) )
        return isOpenDoc( *doc );

    return aWindow->IsShown() && !isDying( aWindow );
}


bool DOCUMENT_WINDOWS::InSwitch()
{
    return s_switching > 0;
}


wxTopLevelWindow* DOCUMENT_WINDOWS::Current()
{
    for( wxTopLevelWindow* win : s_mru )
    {
        if( win->IsShown() && !isDying( win ) )
            return win;
    }

    for( const DOC& doc : s_docs )
    {
        if( doc.window->IsShown() && !isDying( doc.window ) )
            return doc.window;
    }

    return nullptr;
}


void DOCUMENT_WINDOWS::Activate( wxTopLevelWindow* aWindow )
{
    if( !aWindow || s_switching || isDying( aWindow ) )
        return;

    registerManager();

    DOC* doc = findDoc( aWindow );

    if( !doc )
    {
        aWindow->Show( true );
        aWindow->Raise();
        return;
    }

    SWITCH_GUARD guard;

    // The window whose place the document takes
    wxTopLevelWindow* source = nullptr;

    for( wxTopLevelWindow* win : s_mru )
    {
        if( win != aWindow && win->IsShown() && !isDying( win ) )
        {
            source = win;
            break;
        }
    }

    for( size_t ii = 0; !source && ii < s_docs.size(); ++ii )
    {
        wxTopLevelWindow* win = s_docs[ii].window;

        if( win != aWindow && win->IsShown() && !isDying( win ) )
            source = win;
    }

    // A document that is closing still marks the window's place until it goes away
    for( size_t ii = 0; !source && ii < s_docs.size(); ++ii )
    {
        wxTopLevelWindow* win = s_docs[ii].window;

        if( win != aWindow && win->IsShown() && !win->IsBeingDeleted() )
            source = win;
    }

    doc->parked = false;

    // Loaded while hidden, its zoom-to-fit used a canvas that wasn't laid out yet
    bool firstShow = !doc->shownOnce && !aWindow->IsShown();
    doc->shownOnce = true;

    if( source )
        copyPlacement( source, aWindow );     // while hidden: position it before it appears
    else if( aWindow->IsIconized() )
        aWindow->Iconize( false );

    // Carry the panel layout over, so the columns don't jump when the document changes
    if( source && !aWindow->IsShown() )
    {
        carryOver().clear();

        for( SWITCH_HOOK& hook : switchHooks() )
            hook( source, true );

        for( SWITCH_HOOK& hook : switchHooks() )
            hook( aWindow, false );
    }

    if( !aWindow->IsShown() )
    {
#ifdef __WXMSW__
        // Bring it up *behind* the current window and let it paint completely there, then
        // raise it: the switch shows a finished window instead of a blank one
        if( source && source->IsShown() )
        {
            HWND tgt = (HWND) aWindow->GetHWND();
            HWND src = (HWND) source->GetHWND();

            ::SetWindowPos( tgt, src, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE );
            aWindow->ShowWithoutActivating();
            ::SetWindowPos( tgt, src, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE );

            copyPlacement( source, aWindow );
            resync( aWindow );
            ::RedrawWindow( tgt, nullptr, nullptr,
                            RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW );
        }
        else
#endif
        {
            aWindow->Show( true );

            // A window hidden while maximized can come back maximized; settle the placement
            if( source )
                copyPlacement( source, aWindow );
        }
    }

    restoreOwned( *doc );

    aWindow->Raise();
    touchMru( aWindow );

    if( firstShow )
    {
        wxCommandEvent* evt = new wxCommandEvent( EDA_EVT_DOCUMENT_FIRST_SHOWN );
        evt->SetEventObject( aWindow );
        wxQueueEvent( aWindow, evt );
    }

    // Hide the others only now, so there's always a window on screen
    for( DOC& other : s_docs )
    {
        if( other.window == aWindow || !other.window->IsShown() )
            continue;

        if( isDying( other.window ) )
            other.window->Hide();
        else
            park( other );
    }

    if( wxWindow* focus = wxWindow::FindFocus(); !focus || wxGetTopLevelParent( focus ) != aWindow )
        aWindow->SetFocus();
}


void DOCUMENT_WINDOWS::SwitchTo( wxTopLevelWindow* aWindow )
{
    if( !aWindow )
        return;

    if( findDoc( aWindow ) )
    {
        Activate( aWindow );
        return;
    }

    if( aWindow->IsIconized() )
        aWindow->Iconize( false );

    aWindow->Show( true );
    aWindow->Raise();
    aWindow->SetFocus();
}


void DOCUMENT_WINDOWS::OpenInBackground( wxTopLevelWindow* aWindow )
{
    if( !aWindow || isDying( aWindow ) || aWindow->IsShown() )
        return;

    registerManager();

    DOC* doc = findDoc( aWindow );

    if( !doc )
    {
        aWindow->Show( true );
        return;
    }

    if( !Current() )
    {
        Activate( aWindow );
        return;
    }

    // An open tab behind the current document; the tab bars pick it up on idle
    doc->parked = true;
}


std::vector<wxTopLevelWindow*> DOCUMENT_WINDOWS::TabWindows()
{
    registerManager();

    std::vector<wxTopLevelWindow*> windows;

    for( const DOC& doc : s_docs )
    {
        if( isOpenDoc( doc ) )
            windows.push_back( doc.window );
    }

    // Other KiCad frames on screen (3D viewer, simulator, ...) keep their own windows.
    // Floating panels (no taskbar entry) and tool windows aren't listed.
    for( wxWindow* win : wxTopLevelWindows )
    {
        wxFrame* frame = dynamic_cast<wxFrame*>( win );

        if( !frame || !frame->IsShown() || isDying( frame ) || findDoc( frame )
            || frame->HasFlag( wxFRAME_NO_TASKBAR ) || frame->HasFlag( wxFRAME_TOOL_WINDOW ) )
        {
            continue;
        }

        windows.push_back( frame );
    }

    return windows;
}


void DOCUMENT_WINDOWS::ShowAll()
{
    SWITCH_GUARD guard;

    for( DOC& doc : s_docs )
    {
        if( !doc.parked || isDying( doc.window ) )
            continue;

        doc.parked = false;
        doc.shownOnce = true;

        if( doc.window->IsIconized() )
            doc.window->Iconize( false );

        doc.window->Show( true );
        restoreOwned( doc );
    }
}


void DOCUMENT_WINDOWS::CloseAll()
{
    if( s_closingAll )
        return;

    s_closingAll = true;
    registerManager();

    // Every document saves its window placement as it closes: give the parked ones the
    // current window's (e.g. maximized), since they all share that one window
    if( wxTopLevelWindow* current = Current() )
    {
        for( const DOC& doc : s_docs )
        {
            if( doc.window != current && !isDying( doc.window ) && !doc.window->IsShown() )
                copyPlacement( current, doc.window );
        }
    }

    if( wxTopLevelWindow* manager = findManager() )
    {
        // The project manager closes every editor (asking about unsaved changes) and exits
        manager->Close();
    }
    else
    {
        // Close the current document last, so it stays on screen while the others ask
        std::vector<wxTopLevelWindow*> order;
        wxTopLevelWindow*              current = Current();

        for( const DOC& doc : s_docs )
        {
            if( doc.window != current && isOpenDoc( doc ) )
                order.push_back( doc.window );
        }

        if( current )
            order.push_back( current );

        for( wxTopLevelWindow* win : order )
        {
            if( !findDoc( win ) || isDying( win ) )
                continue;

            if( !win->Close() )
                break;
        }
    }

    s_closingAll = false;

    // Something refused to close: make sure a document is still on screen
    scheduleEnsureVisible();
}
