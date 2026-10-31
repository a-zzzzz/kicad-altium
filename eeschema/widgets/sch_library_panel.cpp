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

#include <widgets/sch_library_panel.h>

#include <lib_symbol.h>
#include <lib_symbol_library_manager.h>
#include <sch_pin.h>
#include <string_utils.h>
#include <symbol_edit_frame.h>
#include <kiway.h>
#include <kiway_player.h>
#include <tool/actions.h>
#include <tool/tool_manager.h>
#include <tools/sch_actions.h>
#include <widgets/lib_tree.h>

#include <wx/button.h>
#include <wx/listctrl.h>
#include <wx/menu.h>
#include <wx/sizer.h>
#include <wx/srchctrl.h>
#include <wx/stattext.h>

#include <algorithm>


namespace
{
enum COMMAND
{
    CMD_PLACE = wxID_HIGHEST + 1,
    CMD_ADD,
    CMD_DELETE,
    CMD_EDIT,
    CMD_OPEN,
    CMD_DERIVE,
    CMD_DUPLICATE,
    CMD_RENAME,
    CMD_CUT,
    CMD_COPY,
    CMD_PASTE,
    CMD_EXPORT,
    CMD_SELECT_ALL,
    CMD_UPDATE_SHEETS,
    CMD_MODEL_MANAGER,
    CMD_REFRESH
};
} // namespace


SCH_LIBRARY_PANEL::SCH_LIBRARY_PANEL( wxWindow* aParent, SYMBOL_EDIT_FRAME* aFrame ) :
        wxPanel( aParent ),
        m_frame( aFrame ),
        m_timer( this ),
        m_inSync( false )
{
    wxBoxSizer* sizer = new wxBoxSizer( wxVERTICAL );

    m_mask = new wxSearchCtrl( this, wxID_ANY );
    m_mask->ShowCancelButton( true );
    m_mask->SetDescriptiveText( _( "Mask" ) );
    sizer->Add( m_mask, 0, wxEXPAND | wxALL, 4 );

    // Altium's panel lists the components of an open SchLib; here, pick any symbol library
    m_libraryChoice = new wxChoice( this, wxID_ANY );
    m_libraryChoice->SetToolTip( _( "Symbol library shown in the list" ) );
    sizer->Add( m_libraryChoice, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4 );

    m_libraryChoice->Bind( wxEVT_CHOICE,
                           [this]( wxCommandEvent& )
                           {
                               m_pickedLibrary = m_libraryChoice->GetStringSelection();
                               m_library = m_pickedLibrary;
                               Rebuild();
                           } );

    m_symbolList = new wxListCtrl( this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                   wxLC_REPORT | wxBORDER_NONE );
    m_symbolList->AppendColumn( _( "Design Item ID" ), wxLIST_FORMAT_LEFT, FromDIP( 140 ) );
    m_symbolList->AppendColumn( _( "Description" ), wxLIST_FORMAT_LEFT, FromDIP( 200 ) );
    sizer->Add( m_symbolList, 5, wxEXPAND );

    wxBoxSizer* buttons = new wxBoxSizer( wxHORIZONTAL );
    m_placeBtn = new wxButton( this, CMD_PLACE, _( "Place" ), wxDefaultPosition, wxDefaultSize,
                               wxBU_EXACTFIT );
    m_addBtn = new wxButton( this, CMD_ADD, _( "Add" ), wxDefaultPosition, wxDefaultSize,
                             wxBU_EXACTFIT );
    m_deleteBtn = new wxButton( this, CMD_DELETE, _( "Delete" ), wxDefaultPosition, wxDefaultSize,
                                wxBU_EXACTFIT );
    m_editBtn = new wxButton( this, CMD_EDIT, _( "Edit" ), wxDefaultPosition, wxDefaultSize,
                              wxBU_EXACTFIT );

    for( wxButton* btn : { m_placeBtn, m_addBtn, m_deleteBtn, m_editBtn } )
    {
        buttons->Add( btn, 1, wxRIGHT, 2 );
        btn->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& aEvent ) { runOnSelected( aEvent.GetId() ); } );
    }

    m_placeBtn->SetToolTip( _( "Place the symbol in the schematic" ) );
    m_addBtn->SetToolTip( _( "Add a new symbol to the library" ) );
    m_deleteBtn->SetToolTip( _( "Delete the selected symbol from the library" ) );
    m_editBtn->SetToolTip( _( "Edit the selected symbol's properties" ) );

    sizer->Add( buttons, 0, wxEXPAND | wxALL, 4 );

    m_pinsLabel = new wxStaticText( this, wxID_ANY, _( "Pins" ) );
    sizer->Add( m_pinsLabel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4 );

    m_pinList = new wxListCtrl( this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_NONE );
    m_pinList->AppendColumn( _( "Pin" ), wxLIST_FORMAT_LEFT, FromDIP( 45 ) );
    m_pinList->AppendColumn( _( "Name" ), wxLIST_FORMAT_LEFT, FromDIP( 100 ) );
    m_pinList->AppendColumn( _( "Type" ), wxLIST_FORMAT_LEFT, FromDIP( 90 ) );
    m_pinList->AppendColumn( _( "Part" ), wxLIST_FORMAT_LEFT, FromDIP( 50 ) );
    sizer->Add( m_pinList, 2, wxEXPAND );

    // AD24's panel has no pin list: the pins are in Properties > Pins
    m_pinsLabel->Hide();
    m_pinList->Hide();

    SetSizer( sizer );

    m_mask->Bind( wxEVT_TEXT, [this]( wxCommandEvent& ) { applyMask(); selectCurrentSymbol(); } );
    m_mask->Bind( wxEVT_SEARCHCTRL_CANCEL_BTN,
                  [this]( wxCommandEvent& )
                  {
                      m_mask->ChangeValue( wxEmptyString );
                      applyMask();
                      selectCurrentSymbol();
                  } );

    m_symbolList->Bind( wxEVT_LIST_ITEM_SELECTED, &SCH_LIBRARY_PANEL::onSymbolSelected, this );
    m_symbolList->Bind( wxEVT_LIST_ITEM_ACTIVATED, [this]( wxListEvent& ) { openSelected(); } );
    m_symbolList->Bind( wxEVT_LIST_ITEM_RIGHT_CLICK, &SCH_LIBRARY_PANEL::onSymbolContextMenu, this );
    m_pinList->Bind( wxEVT_LIST_ITEM_SELECTED, &SCH_LIBRARY_PANEL::onPinSelected, this );
    m_pinList->Bind( wxEVT_LIST_ITEM_ACTIVATED, &SCH_LIBRARY_PANEL::onPinActivated, this );

    Bind( wxEVT_TIMER, &SCH_LIBRARY_PANEL::onTimer, this );
    m_timer.Start( 700 );
}


SCH_LIBRARY_PANEL::~SCH_LIBRARY_PANEL()
{
    m_timer.Stop();
}


void SCH_LIBRARY_PANEL::ShowChangedLanguage()
{
    auto setTitle =
            []( wxListCtrl* aList, int aCol, const wxString& aTitle )
            {
                wxListItem item;
                item.SetMask( wxLIST_MASK_TEXT );
                item.SetText( aTitle );
                aList->SetColumn( aCol, item );
            };

    setTitle( m_symbolList, 0, _( "Design Item ID" ) );
    setTitle( m_symbolList, 1, _( "Description" ) );
    setTitle( m_pinList, 0, _( "Pin" ) );
    setTitle( m_pinList, 1, _( "Name" ) );
    setTitle( m_pinList, 2, _( "Type" ) );
    setTitle( m_pinList, 3, _( "Part" ) );
    m_placeBtn->SetLabel( _( "Place" ) );
    m_addBtn->SetLabel( _( "Add" ) );
    m_deleteBtn->SetLabel( _( "Delete" ) );
    m_editBtn->SetLabel( _( "Edit" ) );
    m_mask->SetDescriptiveText( _( "Mask" ) );
    Rebuild();
}


wxString SCH_LIBRARY_PANEL::currentLibrary() const
{
    // A symbol opened from another library takes over from the user's pick
    wxString loaded;

    if( m_frame->GetCurSymbol() && !m_frame->IsSymbolFromSchematic() )
        loaded = m_frame->GetCurSymbol()->GetLibId().GetUniStringLibNickname();

    if( loaded != m_lastLoadedLib )
    {
        m_lastLoadedLib = loaded;

        if( !loaded.IsEmpty() )
            m_pickedLibrary.clear();
    }

    if( !m_pickedLibrary.IsEmpty() )
        return m_pickedLibrary;

    wxString lib = m_frame->GetCurLib();

    if( lib.IsEmpty() && m_frame->GetCurSymbol() && !m_frame->IsSymbolFromSchematic() )
        lib = m_frame->GetCurSymbol()->GetLibId().GetUniStringLibNickname();

    if( lib.IsEmpty() && m_frame->GetLibTree() )
        lib = m_frame->GetLibTree()->GetSelectedLibId().GetUniStringLibNickname();

    if( lib.IsEmpty() )
        lib = m_library;

    // Nothing chosen yet: show the first library, so the panel is never empty
    if( lib.IsEmpty() )
    {
        wxArrayString names = m_frame->GetLibManager().GetLibraryNames();

        if( !names.IsEmpty() )
            lib = names[0];
    }

    return lib;
}


LIB_ID SCH_LIBRARY_PANEL::GetSelectedLibId() const
{
    long sel = m_symbolList->GetNextItem( -1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED );

    if( sel < 0 || sel >= (long) m_shown.size() || m_library.IsEmpty() )
        return LIB_ID();

    return LIB_ID( m_library, m_symbols[m_shown[sel]].name );
}


void SCH_LIBRARY_PANEL::ShowLibrary( const wxString& aNickname )
{
    // Let currentLibrary() note the loaded item's library first, so it does not drop the pick
    currentLibrary();
    m_pickedLibrary = aNickname;
    m_library = aNickname;
    Rebuild();
}


void SCH_LIBRARY_PANEL::Rebuild()
{
    m_symbolSig.clear();
    m_pinSig.clear();
    rebuildSymbols();
    rebuildPins();
}


void SCH_LIBRARY_PANEL::rebuildSymbols()
{
    LIB_ID   prevSel = GetSelectedLibId();
    wxString lib = currentLibrary();

    m_library = lib;
    m_symbols.clear();

    SYMBOL_LIBRARY_MANAGER& libMgr = m_frame->GetLibManager();

    if( !lib.IsEmpty() && libMgr.LibraryExists( lib ) )
    {
        // Names and descriptions are copied; the library buffers can be replaced later
        for( LIB_SYMBOL* symbol : libMgr.EnumerateSymbols( lib ) )
        {
            if( !symbol )
                continue;

            SYM_ROW row;
            row.name = symbol->GetName();
            row.description = symbol->GetDescription();
            row.modified = libMgr.IsSymbolModified( row.name, lib );
            m_symbols.push_back( row );
        }
    }

    std::sort( m_symbols.begin(), m_symbols.end(),
               []( const SYM_ROW& a, const SYM_ROW& b )
               {
                   return StrNumCmp( a.name, b.name, true ) < 0;
               } );

    // Library chooser: every symbol library, the shown one selected
    wxArrayString names = libMgr.GetLibraryNames();

    if( names != m_libraryChoice->GetStrings() )
        m_libraryChoice->Set( names );

    if( !lib.IsEmpty() )
        m_libraryChoice->SetStringSelection( lib );

    applyMask();

    if( prevSel.IsValid() && prevSel.GetUniStringLibNickname() == m_library )
    {
        for( size_t ii = 0; ii < m_shown.size(); ++ii )
        {
            if( m_symbols[m_shown[ii]].name == prevSel.GetUniStringLibItemName() )
            {
                m_inSync = true;
                m_symbolList->SetItemState( (long) ii, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED );
                m_inSync = false;
                return;
            }
        }
    }

    selectCurrentSymbol();
}


void SCH_LIBRARY_PANEL::applyMask()
{
    wxString mask = m_mask->GetValue().Trim().Trim( false ).Lower();
    bool     wildcard = mask.Contains( wxS( "*" ) ) || mask.Contains( wxS( "?" ) );

    m_shown.clear();

    for( size_t ii = 0; ii < m_symbols.size(); ++ii )
    {
        wxString name = m_symbols[ii].name.Lower();

        if( mask.IsEmpty() || ( wildcard ? name.Matches( mask ) : name.Contains( mask ) ) )
            m_shown.push_back( ii );
    }

    m_symbolList->Freeze();
    m_symbolList->DeleteAllItems();

    for( size_t ii = 0; ii < m_shown.size(); ++ii )
    {
        const SYM_ROW& row = m_symbols[m_shown[ii]];
        long idx = m_symbolList->InsertItem( (long) ii, row.modified ? row.name + wxS( " *" )
                                                                     : row.name );
        m_symbolList->SetItem( idx, 1, row.description );
    }

    m_symbolList->Thaw();
}


void SCH_LIBRARY_PANEL::selectCurrentSymbol()
{
    LIB_SYMBOL* symbol = m_frame->GetCurSymbol();

    if( !symbol || m_frame->IsSymbolFromSchematic() || m_frame->GetCurLib() != m_library )
        return;

    for( size_t ii = 0; ii < m_shown.size(); ++ii )
    {
        if( m_symbols[m_shown[ii]].name == symbol->GetName() )
        {
            m_inSync = true;

            // The open symbol becomes the only selected row
            for( long jj = -1; ( jj = m_symbolList->GetNextItem( jj, wxLIST_NEXT_ALL,
                                                                 wxLIST_STATE_SELECTED ) ) >= 0; )
            {
                if( jj != (long) ii )
                    m_symbolList->SetItemState( jj, 0, wxLIST_STATE_SELECTED );
            }

            m_symbolList->SetItemState( (long) ii, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED );
            m_symbolList->EnsureVisible( (long) ii );
            m_inSync = false;
            return;
        }
    }
}


void SCH_LIBRARY_PANEL::rebuildPins()
{
    LIB_SYMBOL* symbol = m_frame->GetCurSymbol();

    long sel = m_pinList->GetNextItem( -1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED );
    KIID selId = ( sel >= 0 && sel < (long) m_pins.size() ) ? m_pins[sel].id : niluuid;

    m_pins.clear();

    std::vector<SCH_PIN*> pins;

    if( symbol )
        pins = symbol->GetPins();

    std::sort( pins.begin(), pins.end(),
               []( const SCH_PIN* a, const SCH_PIN* b )
               {
                   int cmp = StrNumCmp( a->GetNumber(), b->GetNumber(), true );
                   return cmp != 0 ? cmp < 0 : a->GetUnit() < b->GetUnit();
               } );

    int parts = symbol ? symbol->GetUnitCount() : 0;

    if( symbol )
    {
        m_pinsLabel->SetLabel( parts > 1
                                       ? wxString::Format( _( "Pins (%zu) - %d parts" ), pins.size(), parts )
                                       : wxString::Format( _( "Pins (%zu)" ), pins.size() ) );
    }
    else
    {
        m_pinsLabel->SetLabel( _( "Pins" ) );
    }

    m_pinList->Freeze();
    m_pinList->DeleteAllItems();

    for( size_t ii = 0; ii < pins.size(); ++ii )
    {
        SCH_PIN* pin = pins[ii];
        PIN_ROW  row;

        row.id = pin->m_Uuid;
        row.unit = pin->GetUnit();
        row.bodyStyle = pin->GetBodyStyle();
        m_pins.push_back( row );

        wxString part;

        if( parts > 1 )
            part = row.unit == 0 ? _( "All" ) : symbol->GetUnitDisplayName( row.unit, false );

        long idx = m_pinList->InsertItem( (long) ii, pin->GetNumber() );
        m_pinList->SetItem( idx, 1, pin->GetName() );
        m_pinList->SetItem( idx, 2, pin->GetElectricalTypeName() );
        m_pinList->SetItem( idx, 3, part );

        if( row.id == selId && selId != niluuid )
        {
            m_inSync = true;
            m_pinList->SetItemState( idx, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED );
            m_inSync = false;
        }
    }

    m_pinList->Thaw();
}


void SCH_LIBRARY_PANEL::onTimer( wxTimerEvent& aEvent )
{
    if( !IsShownOnScreen() )
        return;

    wxString    lib = currentLibrary();
    LIB_SYMBOL* symbol = m_frame->GetCurSymbol();
    int         hash = lib.IsEmpty() ? 0 : m_frame->GetLibManager().GetLibraryHash( lib );

    wxString symSig = wxString::Format( wxS( "%s|%d|%s|%d" ), lib, hash,
                                        symbol ? symbol->GetName() : wxString(),
                                        m_frame->IsContentModified() ? 1 : 0 );

    if( symSig != m_symbolSig )
    {
        m_symbolSig = symSig;
        rebuildSymbols();
    }

    wxString pinSig = wxString::Format( wxS( "%p|%d|%d|%d|%zu" ), (void*) symbol,
                                        m_frame->GetUndoCommandCount(),
                                        m_frame->GetRedoCommandCount(),
                                        m_frame->IsContentModified() ? 1 : 0,
                                        symbol ? symbol->GetPins().size() : 0 );

    if( pinSig != m_pinSig )
    {
        m_pinSig = pinSig;
        rebuildPins();
    }
}


void SCH_LIBRARY_PANEL::onSymbolSelected( wxListEvent& aEvent )
{
    if( m_inSync )
        return;

    // Library commands (delete, rename, duplicate, ...) read the library tree's selection,
    // so mirror the pick there.
    LIB_ID id = GetSelectedLibId();

    if( id.IsValid() && m_frame->GetLibTree() )
        m_frame->GetLibTree()->SelectLibId( id );
}


void SCH_LIBRARY_PANEL::openSelected()
{
    LIB_ID      id = GetSelectedLibId();
    LIB_SYMBOL* symbol = m_frame->GetCurSymbol();

    if( !id.IsValid() )
        return;

    if( symbol && !m_frame->IsSymbolFromSchematic() && m_frame->GetCurLib() == m_library
        && symbol->GetName() == id.GetUniStringLibItemName() )
    {
        return;   // already open
    }

    m_frame->LoadSymbol( id.GetUniStringLibItemName(), id.GetUniStringLibNickname(), 0 );
    m_symbolSig.clear();
}


void SCH_LIBRARY_PANEL::runOnSelected( int aCommand )
{
    TOOL_MANAGER* toolMgr = m_frame->GetToolManager();

    if( !toolMgr )
        return;

    LIB_ID id = GetSelectedLibId();

    if( id.IsValid() && m_frame->GetLibTree() )
        m_frame->GetLibTree()->SelectLibId( id );

    const TOOL_ACTION* action = nullptr;

    switch( aCommand )
    {
    case CMD_PLACE:
        openSelected();
        action = &SCH_ACTIONS::addSymbolToSchematic;
        break;

    case CMD_EDIT:
        openSelected();
        action = &SCH_ACTIONS::symbolProperties;
        break;

    case CMD_OPEN:      openSelected();                              break;
    case CMD_ADD:       action = &SCH_ACTIONS::newSymbol;            break;
    case CMD_DELETE:
    {
        std::vector<LIB_ID> ids;

        for( long ii = -1; ( ii = m_symbolList->GetNextItem( ii, wxLIST_NEXT_ALL,
                                                             wxLIST_STATE_SELECTED ) ) >= 0; )
        {
            if( ii < (long) m_shown.size() && !m_library.IsEmpty() )
                ids.emplace_back( m_library, m_symbols[m_shown[ii]].name );
        }

        for( const LIB_ID& libId : ids )
        {
            if( m_frame->GetLibTree() )
                m_frame->GetLibTree()->SelectLibId( libId );

            toolMgr->RunAction( SCH_ACTIONS::deleteSymbol );
        }

        m_symbolSig.clear();
        break;
    }
    case CMD_DERIVE:    action = &SCH_ACTIONS::deriveFromExistingSymbol; break;
    case CMD_DUPLICATE: action = &SCH_ACTIONS::duplicateSymbol;      break;
    case CMD_RENAME:    action = &SCH_ACTIONS::renameSymbol;         break;
    case CMD_CUT:       action = &SCH_ACTIONS::cutSymbol;            break;
    case CMD_COPY:      action = &SCH_ACTIONS::copySymbol;           break;
    case CMD_PASTE:     action = &SCH_ACTIONS::pasteSymbol;          break;
    case CMD_EXPORT:    action = &SCH_ACTIONS::exportSymbol;         break;
    case CMD_REFRESH:   Rebuild();                                   break;

    case CMD_SELECT_ALL:
        for( long ii = 0; ii < m_symbolList->GetItemCount(); ++ii )
            m_symbolList->SetItemState( ii, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED );

        break;

    case CMD_UPDATE_SHEETS:
        // The schematic's Update From Libraries, for the symbols placed from this library
        if( KIWAY_PLAYER* sch = m_frame->Kiway().Player( FRAME_SCH, false ) )
        {
            sch->Show( true );
            sch->Raise();
            sch->GetToolManager()->RunAction( "eeschema.InteractiveEdit.updateSymbols" );
        }
        else
        {
            m_frame->ShowInfoBarMsg( _( "Open the schematic to update its symbols." ) );
        }

        break;

    case CMD_MODEL_MANAGER:
        m_frame->ShowModelEditor( true );
        break;
    default:                                                         break;
    }

    if( action )
    {
        toolMgr->RunAction( *action );
        m_symbolSig.clear();
    }
}


void SCH_LIBRARY_PANEL::onSymbolContextMenu( wxListEvent& aEvent )
{
    // The menu acts on the row clicked, as in Altium: make it the only selected one
    long row = aEvent.GetIndex();

    if( row >= 0 && row < m_symbolList->GetItemCount()
        && !( m_symbolList->GetItemState( row, wxLIST_STATE_SELECTED ) & wxLIST_STATE_SELECTED ) )
    {
        for( long ii = -1; ( ii = m_symbolList->GetNextItem( ii, wxLIST_NEXT_ALL,
                                                             wxLIST_STATE_SELECTED ) ) >= 0; )
        {
            m_symbolList->SetItemState( ii, 0, wxLIST_STATE_SELECTED );
        }

        m_symbolList->SetItemState( row, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                                    wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED );
    }

    LIB_ID id = GetSelectedLibId();

    if( id.IsValid() && m_frame->GetLibTree() )
        m_frame->GetLibTree()->SelectLibId( id );

    bool hasSel = id.IsValid();

    wxMenu menu;
    // Altium's menu (AD24): Select all, Update Schematic Sheets, Model Manager | Copy, Cut,
    // Paste | Delete
    menu.Append( CMD_SELECT_ALL, _( "Select all" ) + wxS( "\tCtrl+A" ) );
    menu.Append( CMD_UPDATE_SHEETS, _( "Update Schematic Sheets" ) )->Enable( hasSel );
    menu.Append( CMD_MODEL_MANAGER, _( "Model Manager" ) );
    menu.AppendSeparator();
    menu.Append( CMD_COPY, _( "Copy" ) + wxS( "\tCtrl+C" ) )->Enable( hasSel );
    menu.Append( CMD_CUT, _( "Cut" ) + wxS( "\tCtrl+X" ) )->Enable( hasSel );
    menu.Append( CMD_PASTE, _( "Paste" ) + wxS( "\tCtrl+V" ) );
    menu.AppendSeparator();
    menu.Append( CMD_DELETE, _( "Delete" ) + wxS( "\tDel" ) )->Enable( hasSel );

    int picked = GetPopupMenuSelectionFromUser( menu );

    if( picked != wxID_NONE )
        runOnSelected( picked );
}


void SCH_LIBRARY_PANEL::onPinSelected( wxListEvent& aEvent )
{
    if( m_inSync )
        return;

    long        sel = aEvent.GetIndex();
    LIB_SYMBOL* symbol = m_frame->GetCurSymbol();

    if( !symbol || sel < 0 || sel >= (long) m_pins.size() )
        return;

    const PIN_ROW& row = m_pins[sel];

    // Show the part (and body style) the pin belongs to
    if( row.unit > 0 && row.unit != m_frame->GetUnit() && row.unit <= symbol->GetUnitCount() )
        m_frame->SetUnit( row.unit );

    if( row.bodyStyle > 0 && row.bodyStyle != m_frame->GetBodyStyle()
        && row.bodyStyle <= symbol->GetBodyStyleCount() )
    {
        m_frame->SetBodyStyle( row.bodyStyle );
    }

    // SetUnit() may rebuild the view; look the pin up again
    symbol = m_frame->GetCurSymbol();

    if( !symbol )
        return;

    for( SCH_PIN* pin : symbol->GetPins() )
    {
        if( pin->m_Uuid == row.id )
        {
            TOOL_MANAGER* toolMgr = m_frame->GetToolManager();
            toolMgr->RunAction( ACTIONS::selectionClear );
            toolMgr->RunAction<EDA_ITEM*>( ACTIONS::selectItem, pin );
            break;
        }
    }
}


void SCH_LIBRARY_PANEL::onPinActivated( wxListEvent& aEvent )
{
    onPinSelected( aEvent );
    m_frame->ShowPropertiesPanel();
}
