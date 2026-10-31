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

#include <messages_store.h>

#include <wx/intl.h>

#include <algorithm>
#include <map>


namespace
{
// Altium keeps the session's messages; cap them so a runaway report can't grow without end
constexpr size_t MAX_MESSAGES = 20000;

std::vector<PROJECT_MESSAGE>& messages()
{
    static std::vector<PROJECT_MESSAGE> list;
    return list;
}

std::map<int, std::function<void()>>& listeners()
{
    static std::map<int, std::function<void()>> map;
    return map;
}

void notify()
{
    // Copy: a listener may (un)register listeners
    std::map<int, std::function<void()>> current = listeners();

    for( auto& [id, listener] : current )
    {
        // Skip one removed by an earlier listener: its owner may already be gone
        if( listener && listeners().count( id ) )
            listener();
    }
}

void trim()
{
    std::vector<PROJECT_MESSAGE>& list = messages();

    if( list.size() > MAX_MESSAGES )
        list.erase( list.begin(), list.begin() + ( list.size() - MAX_MESSAGES ) );
}
} // namespace


const std::vector<PROJECT_MESSAGE>& MESSAGES_STORE::Messages()
{
    return messages();
}


void MESSAGES_STORE::Add( PROJECT_MESSAGE aMessage )
{
    if( !aMessage.when.IsValid() )
        aMessage.when = wxDateTime::Now();

    messages().push_back( std::move( aMessage ) );
    trim();
    notify();
}


bool MESSAGES_STORE::SetMarkers( const void* aOrigin, std::vector<PROJECT_MESSAGE> aMarkers )
{
    std::vector<PROJECT_MESSAGE>& list = messages();

    // Keep the time a violation was first reported while it stays in the design
    std::map<wxString, wxDateTime> seen;

    for( const PROJECT_MESSAGE& msg : list )
    {
        if( msg.fromMarker && msg.origin == aOrigin && !msg.key.IsEmpty() )
            seen[msg.key] = msg.when;
    }

    bool hadMarkers = std::any_of( list.begin(), list.end(),
                                   [aOrigin]( const PROJECT_MESSAGE& msg )
                                   {
                                       return msg.fromMarker && msg.origin == aOrigin;
                                   } );

    if( aMarkers.empty() && !hadMarkers )
        return false;

    std::erase_if( list,
                   [aOrigin]( const PROJECT_MESSAGE& msg )
                   {
                       return msg.fromMarker && msg.origin == aOrigin;
                   } );

    wxDateTime now = wxDateTime::Now();
    bool       newViolation = false;

    for( PROJECT_MESSAGE& msg : aMarkers )
    {
        msg.fromMarker = true;
        msg.origin = aOrigin;

        auto it = seen.find( msg.key );

        if( it != seen.end() )
        {
            msg.when = it->second;
        }
        else
        {
            msg.when = now;
            newViolation |= msg.severity == RPT_SEVERITY_ERROR
                            || msg.severity == RPT_SEVERITY_WARNING;
        }

        list.push_back( std::move( msg ) );
    }

    // Rows stay in the order they were first reported, across all documents
    std::stable_sort( list.begin(), list.end(),
                      []( const PROJECT_MESSAGE& a, const PROJECT_MESSAGE& b )
                      {
                          return a.when.IsEarlierThan( b.when );
                      } );

    trim();
    notify();
    return newViolation;
}


void MESSAGES_STORE::SetCompilerMessages( std::vector<PROJECT_MESSAGE> aMessages )
{
    std::vector<PROJECT_MESSAGE>& list = messages();
    std::map<wxString, wxDateTime> seen;
    const wxString                 eco = _( "Engineering Change Order" );

    auto stale =
            [&]( const PROJECT_MESSAGE& msg )
            {
                return msg.key.StartsWith( wxS( "compiler:" ) ) || msg.source == eco;
            };

    for( const PROJECT_MESSAGE& msg : list )
    {
        if( msg.key.StartsWith( wxS( "compiler:" ) ) )
            seen[msg.key + msg.text] = msg.when;
    }

    std::erase_if( list, stale );

    for( PROJECT_MESSAGE& msg : aMessages )
    {
        auto it = seen.find( msg.key + msg.text );
        msg.when = it != seen.end() ? it->second : wxDateTime::Now();
        list.push_back( std::move( msg ) );
    }

    std::stable_sort( list.begin(), list.end(),
                      []( const PROJECT_MESSAGE& a, const PROJECT_MESSAGE& b )
                      {
                          return a.when.IsEarlierThan( b.when );
                      } );

    trim();
    notify();
}


void MESSAGES_STORE::RemoveOrigin( const void* aOrigin )
{
    std::vector<PROJECT_MESSAGE>& list = messages();
    size_t                        before = list.size();

    std::erase_if( list, [aOrigin]( const PROJECT_MESSAGE& msg ) { return msg.origin == aOrigin; } );

    if( list.size() != before )
        notify();
}


void MESSAGES_STORE::Clear()
{
    messages().clear();
    notify();
}


int MESSAGES_STORE::AddListener( std::function<void()> aListener )
{
    static int nextId = 1;

    listeners()[nextId] = std::move( aListener );
    return nextId++;
}


void MESSAGES_STORE::RemoveListener( int aId )
{
    listeners().erase( aId );
}
