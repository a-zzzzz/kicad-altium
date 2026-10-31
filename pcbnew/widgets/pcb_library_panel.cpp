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

#include <widgets/pcb_library_panel.h>
#include <wx/time.h>

#include <board.h>
#include <footprint.h>
#include <footprint_edit_frame.h>
#include <footprint_library_adapter.h>
#include <ki_exception.h>
#include <pad.h>
#include <pcb_shape.h>
#include <pcb_text.h>
#include <project_pcb.h>
#include <string_utils.h>
#include <tool/actions.h>
#include <tool/tool_manager.h>
#include <tools/pcb_actions.h>
#include <widgets/altium_layer_tabs.h>
#include <widgets/lib_tree.h>
#include <widgets/footprint_preview_widget.h>
#include <widgets/pcb_panel_utils.h>
#include <view/view.h>
#include <zone.h>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/listctrl.h>
#include <wx/menu.h>
#include <wx/sizer.h>
#include <wx/srchctrl.h>
#include <wx/stattext.h>

#include <algorithm>
#include <set>
#include <tuple>
#include <memory>


/// Virtual list of footprints: the panel supplies the cell text.
class LIBRARY_LIST_CTRL : public wxListCtrl
{
public:
    LIBRARY_LIST_CTRL( wxWindow* aParent, PCB_LIBRARY_PANEL* aPanel ) :
            wxListCtrl( aParent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                        wxLC_REPORT | wxLC_VIRTUAL | wxLC_SINGLE_SEL | wxBORDER_NONE ),
            m_panel( aPanel )
    {
    }

    wxString OnGetItemText( long aItem, long aColumn ) const override
    {
        return m_panel->FootprintCellText( aItem, aColumn );
    }

private:
    PCB_LIBRARY_PANEL* m_panel;
};


namespace
{
enum FP_COLUMN { FP_COL_NAME = 0, FP_COL_PADS, FP_COL_PRIMITIVES };

/// Altium's section triangles: open and folded.
wxString openMark()   { return wxString( wxUniChar( 0x25E2 ) ) + wxS( " " ); }
wxString foldedMark() { return wxString( wxUniChar( 0x25B8 ) ) + wxS( " " ); }

enum MENU_ID
{
    ID_NEW_BLANK = wxID_HIGHEST + 1,
    ID_WIZARD,
    ID_OPEN,
    ID_PROPERTIES,
    ID_DUPLICATE,
    ID_RENAME,
    ID_CUT,
    ID_COPY,
    ID_PASTE,
    ID_DELETE,
    ID_EXPORT,
    ID_REFRESH,
    ID_PLACE,
    ID_ADD,
    ID_EDIT
};
} // namespace


PCB_LIBRARY_PANEL::PCB_LIBRARY_PANEL( wxWindow* aParent, FOOTPRINT_EDIT_FRAME* aFrame ) :
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

    // Altium's panel lists the footprints of an open PcbLib; here, pick any footprint library
    m_libraryChoice = new wxChoice( this, wxID_ANY );
    m_libraryChoice->SetToolTip( _( "Footprint library shown in the list" ) );
    sizer->Add( m_libraryChoice, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4 );

    m_libraryChoice->Bind( wxEVT_CHOICE,
                           [this]( wxCommandEvent& )
                           {
                               m_pickedLibrary = m_libraryChoice->GetStringSelection();
                               m_library = m_pickedLibrary;
                               Rebuild();
                           } );

    // Altium's highlight controls: Apply / Clear / Magnify, the mode, and its three options
    wxBoxSizer* applyRow = new wxBoxSizer( wxHORIZONTAL );

    for( auto [id, label, tip] : { std::tuple{ ID_REFRESH + 100, _( "Apply" ),
                                               _( "Highlight the primitives picked below" ) },
                                   std::tuple{ ID_REFRESH + 101, _( "Clear" ),
                                               _( "Remove the highlight" ) },
                                   std::tuple{ ID_REFRESH + 102, _( "Magnify" ),
                                               _( "Zoom to the picked primitives" ) } } )
    {
        wxButton* btn = new wxButton( this, id, label, wxDefaultPosition, wxDefaultSize,
                                      wxBU_EXACTFIT );
        btn->SetToolTip( tip );
        applyRow->Add( btn, 1, wxRIGHT, 2 );
        btn->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& aEvent ) { runButton( aEvent.GetId() ); } );
    }

    sizer->Add( applyRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4 );

    m_highlightMode = new wxChoice( this, wxID_ANY );
    m_highlightMode->Append( _( "Normal" ) );
    m_highlightMode->Append( _( "Mask" ) );
    m_highlightMode->Append( _( "Dim" ) );
    m_highlightMode->SetSelection( 0 );
    m_highlightMode->SetToolTip( _( "How the rest of the footprint is shown when applying" ) );
    sizer->Add( m_highlightMode, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4 );

    wxBoxSizer* options = new wxBoxSizer( wxHORIZONTAL );
    m_selectCheck = new wxCheckBox( this, wxID_ANY, _( "Select" ) );
    m_zoomCheck = new wxCheckBox( this, wxID_ANY, _( "Zoom" ) );
    m_clearExistingCheck = new wxCheckBox( this, wxID_ANY, _( "Clear Existing" ) );

    for( wxCheckBox* check : { m_selectCheck, m_zoomCheck, m_clearExistingCheck } )
    {
        check->SetValue( true );
        options->Add( check, 0, wxRIGHT, 10 );
    }

    sizer->Add( options, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4 );

    m_footprintsLabel = new wxStaticText( this, wxID_ANY, _( "Footprints" ) );
    sizer->Add( m_footprintsLabel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4 );

    m_footprintList = new LIBRARY_LIST_CTRL( this, this );
    m_footprintList->AppendColumn( _( "Name" ), wxLIST_FORMAT_LEFT, FromDIP( 150 ) );
    m_footprintList->AppendColumn( _( "Pads" ), wxLIST_FORMAT_RIGHT, FromDIP( 45 ) );
    m_footprintList->AppendColumn( _( "Primitives" ), wxLIST_FORMAT_RIGHT, FromDIP( 70 ) );
    sizer->Add( m_footprintList, 3, wxEXPAND );

    // Place / Add / Delete / Edit under the list, as in Altium
    wxBoxSizer* buttons = new wxBoxSizer( wxHORIZONTAL );
    m_placeBtn = new wxButton( this, ID_PLACE, _( "Place" ), wxDefaultPosition, wxDefaultSize,
                               wxBU_EXACTFIT );
    m_addBtn = new wxButton( this, ID_ADD, _( "Add" ), wxDefaultPosition, wxDefaultSize,
                             wxBU_EXACTFIT );
    m_deleteBtn = new wxButton( this, ID_DELETE, _( "Delete" ), wxDefaultPosition, wxDefaultSize,
                                wxBU_EXACTFIT );
    m_editBtn = new wxButton( this, ID_EDIT, _( "Edit" ), wxDefaultPosition, wxDefaultSize,
                              wxBU_EXACTFIT );

    m_placeBtn->SetToolTip( _( "Place the footprint on the board" ) );
    m_addBtn->SetToolTip( _( "Add a new blank footprint to the library" ) );
    m_deleteBtn->SetToolTip( _( "Delete the selected footprint from the library" ) );
    m_editBtn->SetToolTip( _( "Edit the selected footprint's properties" ) );

    for( wxButton* btn : { m_placeBtn, m_addBtn, m_deleteBtn, m_editBtn } )
    {
        buttons->Add( btn, 1, wxRIGHT, 2 );
        btn->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& aEvent ) { runButton( aEvent.GetId() ); } );
    }

    sizer->Add( buttons, 0, wxEXPAND | wxALL, 4 );

    m_primitiveList = new wxListCtrl( this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                      wxLC_REPORT | wxBORDER_NONE );
    m_primitiveList->AppendColumn( _( "Type" ), wxLIST_FORMAT_LEFT, FromDIP( 70 ) );
    m_primitiveList->AppendColumn( _( "Name" ), wxLIST_FORMAT_LEFT, FromDIP( 60 ) );
    m_primitiveList->AppendColumn( _( "X-Size" ), wxLIST_FORMAT_RIGHT, FromDIP( 70 ) );
    m_primitiveList->AppendColumn( _( "Y-Size" ), wxLIST_FORMAT_RIGHT, FromDIP( 70 ) );
    m_primitiveList->AppendColumn( _( "Layer" ), wxLIST_FORMAT_LEFT, FromDIP( 90 ) );
    m_primitivesLabel = addFoldingHeading( sizer, _( "Footprint Primitives" ), m_primitiveList );
    sizer->Add( m_primitiveList, 2, wxEXPAND );

    // "Other": a small preview of the footprint picked in the list
    m_previewHolder = new wxPanel( this );
    m_previewHolder->SetSizer( new wxBoxSizer( wxVERTICAL ) );
    m_previewHolder->SetMinSize( FromDIP( wxSize( -1, 140 ) ) );
    m_otherLabel = addFoldingHeading( sizer, _( "Other" ), m_previewHolder );
    sizer->Add( m_previewHolder, 1, wxEXPAND );

    SetSizer( sizer );

    m_mask->Bind( wxEVT_TEXT, &PCB_LIBRARY_PANEL::onMask, this );
    m_mask->Bind( wxEVT_SEARCHCTRL_CANCEL_BTN,
                  [this]( wxCommandEvent& )
                  {
                      m_mask->ChangeValue( wxEmptyString );
                      applyMask();
                  } );

    m_footprintList->Bind( wxEVT_LIST_ITEM_SELECTED, &PCB_LIBRARY_PANEL::onFootprintSelected, this );
    m_footprintList->Bind( wxEVT_LIST_ITEM_ACTIVATED, &PCB_LIBRARY_PANEL::onFootprintActivated, this );
    m_footprintList->Bind( wxEVT_LIST_ITEM_RIGHT_CLICK, &PCB_LIBRARY_PANEL::onFootprintContextMenu,
                           this );
    m_primitiveList->Bind( wxEVT_LIST_ITEM_SELECTED, &PCB_LIBRARY_PANEL::onPrimitiveSelected, this );
    m_primitiveList->Bind( wxEVT_LIST_ITEM_DESELECTED, &PCB_LIBRARY_PANEL::onPrimitiveSelected, this );
    m_primitiveList->Bind( wxEVT_LIST_ITEM_ACTIVATED, &PCB_LIBRARY_PANEL::onPrimitiveActivated, this );

    Bind( wxEVT_TIMER, &PCB_LIBRARY_PANEL::onTimer, this );
    m_timer.Start( 700 );
}


PCB_LIBRARY_PANEL::~PCB_LIBRARY_PANEL()
{
    m_timer.Stop();
}


wxStaticText* PCB_LIBRARY_PANEL::addFoldingHeading( wxSizer* aSizer, const wxString& aTitle,
                                                    wxWindow* aBody )
{
    wxStaticText* heading = new wxStaticText( this, wxID_ANY, openMark() + aTitle );
    wxFont        font = heading->GetFont();

    font.MakeBold();
    heading->SetFont( font );
    heading->SetCursor( wxCursor( wxCURSOR_HAND ) );
    aSizer->Add( heading, 0, wxEXPAND | wxALL, 4 );

    heading->Bind( wxEVT_LEFT_DOWN,
                   [this, heading, aBody, aTitle]( wxMouseEvent& )
                   {
                       bool show = !aBody->IsShown();
                       aBody->Show( show );
                       heading->SetLabel( ( show ? openMark() : foldedMark() ) + aTitle );
                       Layout();
                   } );

    return heading;
}


void PCB_LIBRARY_PANEL::runButton( int aCommand )
{
    TOOL_MANAGER* toolMgr = m_frame->GetToolManager();

    if( !toolMgr )
        return;

    LIB_ID id = GetSelectedLibId();

    switch( aCommand )
    {
    case ID_REFRESH + 100: applyHighlight();                                   return;
    case ID_REFRESH + 101: clearHighlight();                                   return;
    case ID_REFRESH + 102: toolMgr->RunAction( ACTIONS::zoomFitSelection );    return;

    case ID_ADD:
        toolMgr->RunAction( PCB_ACTIONS::newFootprint );
        break;

    case ID_PLACE:
    case ID_EDIT:
        // Both act on the footprint picked in the list: open it first
        if( id.IsValid() && id != m_frame->GetLoadedFPID() )
            m_frame->LoadFootprintFromLibrary( id );

        if( m_frame->GetBoard() && m_frame->GetBoard()->GetFirstFootprint() )
        {
            toolMgr->RunAction( aCommand == ID_PLACE ? PCB_ACTIONS::saveFpToBoard
                                                     : PCB_ACTIONS::footprintProperties );
        }

        break;

    case ID_DELETE:
        if( id.IsValid() && m_frame->GetLibTree() )
            m_frame->GetLibTree()->SelectLibId( id );

        toolMgr->RunAction( PCB_ACTIONS::deleteFootprint );
        break;

    default:
        return;
    }

    m_footprintSig.clear();
}


void PCB_LIBRARY_PANEL::clearHighlight()
{
    BOARD* board = m_frame->GetBoard();

    if( board && !m_masked.empty() )
    {
        KIGFX::VIEW* view = m_frame->GetCanvas()->GetView();

        for( const KIID& id : m_masked )
        {
            if( BOARD_ITEM* item = board->ResolveItem( id, true ) )
            {
                item->SetForcedTransparency( 0.0 );
                view->Update( item, KIGFX::REPAINT );
            }
        }

        m_frame->GetCanvas()->Refresh();
    }

    m_masked.clear();

    if( m_frame->GetToolManager() )
        m_frame->GetToolManager()->RunAction( ACTIONS::selectionClear );
}


void PCB_LIBRARY_PANEL::applyHighlight()
{
    BOARD*     board = m_frame->GetBoard();
    FOOTPRINT* fp = board ? board->GetFirstFootprint() : nullptr;

    if( !fp )
        return;

    std::set<BOARD_ITEM*> picked;

    for( long ii = -1; ( ii = m_primitiveList->GetNextItem( ii, wxLIST_NEXT_ALL,
                                                            wxLIST_STATE_SELECTED ) ) >= 0; )
    {
        if( ii < (long) m_primitives.size() )
        {
            if( BOARD_ITEM* item = board->ResolveItem( m_primitives[ii].id, true ) )
                picked.insert( item );
        }
    }

    // Nothing picked: the whole footprint, as Altium applies to the current component
    if( picked.empty() )
    {
        fp->RunOnChildren( [&]( BOARD_ITEM* aChild ) { picked.insert( aChild ); },
                           RECURSE_MODE::NO_RECURSE );
    }

    // Undo an earlier Mask / Dim before applying this one
    std::vector<KIID> previous;
    previous.swap( m_masked );

    KIGFX::VIEW*  view = m_frame->GetCanvas()->GetView();
    TOOL_MANAGER* toolMgr = m_frame->GetToolManager();

    for( const KIID& id : previous )
    {
        if( BOARD_ITEM* item = board->ResolveItem( id, true ) )
        {
            item->SetForcedTransparency( 0.0 );
            view->Update( item, KIGFX::REPAINT );
        }
    }

    // Mask fades the rest almost away, Dim only partly
    int mode = m_highlightMode->GetSelection();

    if( mode == 1 || mode == 2 )
    {
        double level = mode == 1 ? 0.85 : 0.55;

        fp->RunOnChildren(
                [&]( BOARD_ITEM* aChild )
                {
                    if( picked.count( aChild ) )
                        return;

                    aChild->SetForcedTransparency( level );
                    view->Update( aChild, KIGFX::REPAINT );
                    m_masked.push_back( aChild->m_Uuid );
                },
                RECURSE_MODE::NO_RECURSE );
    }

    if( m_clearExistingCheck->GetValue() )
        toolMgr->RunAction( ACTIONS::selectionClear );

    if( m_selectCheck->GetValue() )
    {
        EDA_ITEMS items( picked.begin(), picked.end() );
        toolMgr->RunAction<EDA_ITEMS*>( ACTIONS::selectItems, &items );
    }

    if( m_zoomCheck->GetValue() )
    {
        BOX2I bbox = ( *picked.begin() )->GetBoundingBox();

        for( BOARD_ITEM* item : picked )
            bbox.Merge( item->GetBoundingBox() );

        PcbZoomToBox( m_frame, bbox );
    }

    m_frame->GetCanvas()->Refresh();
}


void PCB_LIBRARY_PANEL::updatePreview()
{
    if( !m_previewHolder->IsShownOnScreen() )
        return;

    LIB_ID id = GetSelectedLibId();

    if( !id.IsValid() )
        id = m_frame->GetLoadedFPID();

    if( id == m_previewId )
        return;

    m_previewId = id;

    // Created on first use: it is a canvas of its own
    if( !m_preview )
    {
        m_preview = new FOOTPRINT_PREVIEW_WIDGET( m_previewHolder, m_frame->Kiway() );

        if( !m_preview->IsInitialized() )
        {
            m_preview->Destroy();
            m_preview = nullptr;
            return;
        }

        m_preview->SetUserUnits( m_frame->GetUserUnits() );
        m_previewHolder->GetSizer()->Add( m_preview, 1, wxEXPAND );
        m_previewHolder->Layout();
    }

    if( id.IsValid() )
        m_preview->DisplayFootprint( id );
}


void PCB_LIBRARY_PANEL::ShowChangedLanguage()
{
    auto setTitle =
            []( wxListCtrl* aList, int aCol, const wxString& aTitle )
            {
                wxListItem item;
                item.SetMask( wxLIST_MASK_TEXT );
                item.SetText( aTitle );
                aList->SetColumn( aCol, item );
            };

    setTitle( m_footprintList, FP_COL_NAME, _( "Name" ) );
    setTitle( m_footprintList, FP_COL_PADS, _( "Pads" ) );
    setTitle( m_footprintList, FP_COL_PRIMITIVES, _( "Primitives" ) );
    setTitle( m_primitiveList, 0, _( "Type" ) );
    setTitle( m_primitiveList, 1, _( "Name" ) );
    setTitle( m_primitiveList, 2, _( "X-Size" ) );
    setTitle( m_primitiveList, 3, _( "Y-Size" ) );
    setTitle( m_primitiveList, 4, _( "Layer" ) );
    m_mask->SetDescriptiveText( _( "Mask" ) );
    m_primitivesLabel->SetLabel( openMark() + _( "Footprint Primitives" ) );
    m_otherLabel->SetLabel( openMark() + _( "Other" ) );
    m_footprintsLabel->SetLabel( _( "Footprints" ) );
    m_placeBtn->SetLabel( _( "Place" ) );
    m_addBtn->SetLabel( _( "Add" ) );
    m_deleteBtn->SetLabel( _( "Delete" ) );
    m_editBtn->SetLabel( _( "Edit" ) );
    m_footprintSig.clear();
}


wxString PCB_LIBRARY_PANEL::currentLibrary() const
{
    // The user's pick, until a footprint from another library is opened
    wxString loaded = m_frame->GetLoadedFPID().GetUniStringLibNickname();

    if( loaded != m_lastLoadedLib )
    {
        m_lastLoadedLib = loaded;

        if( !loaded.IsEmpty() )
            m_pickedLibrary.clear();
    }

    if( !m_pickedLibrary.IsEmpty() )
        return m_pickedLibrary;

    wxString lib = loaded;

    if( lib.IsEmpty() && m_frame->GetLibTree() )
        lib = m_frame->GetLibTree()->GetSelectedLibId().GetUniStringLibNickname();

    if( lib.IsEmpty() )
        lib = m_library;

    // Nothing chosen yet: show the first library of the project table, else the first one,
    // so the panel is never empty
    if( lib.IsEmpty() )
    {
        if( FOOTPRINT_LIBRARY_ADAPTER* adapter = PROJECT_PCB::FootprintLibAdapter( &m_frame->Prj() ) )
        {
            std::vector<wxString> names = adapter->GetLibraryNames();

            if( !names.empty() )
                lib = names.front();
        }
    }

    return lib;
}


void PCB_LIBRARY_PANEL::countPrimitives( const FOOTPRINT* aFootprint, int& aPads,
                                         int& aPrimitives ) const
{
    aPads = (int) aFootprint->Pads().size();
    aPrimitives = aPads + (int) aFootprint->GraphicalItems().size()
                  + (int) aFootprint->Zones().size();
}


LIB_ID PCB_LIBRARY_PANEL::GetSelectedLibId() const
{
    long sel = m_footprintList->GetNextItem( -1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED );

    if( sel < 0 || sel >= (long) m_shown.size() || m_library.IsEmpty() )
        return LIB_ID();

    return LIB_ID( m_library, m_footprints[m_shown[sel]].name );
}


void PCB_LIBRARY_PANEL::ShowLibrary( const wxString& aNickname )
{
    // Let currentLibrary() note the loaded item's library first, so it does not drop the pick
    currentLibrary();
    m_pickedLibrary = aNickname;
    m_library = aNickname;
    Rebuild();
}


void PCB_LIBRARY_PANEL::Rebuild()
{
    m_footprintSig.clear();
    m_primitiveSig.clear();
    rebuildFootprints();
    rebuildPrimitives();
}


void PCB_LIBRARY_PANEL::rebuildFootprints()
{
    wxString lib = currentLibrary();
    LIB_ID   prevSel = GetSelectedLibId();

    m_library = lib;
    m_footprints.clear();

    FOOTPRINT_LIBRARY_ADAPTER* adapter = PROJECT_PCB::FootprintLibAdapter( &m_frame->Prj() );

    if( adapter && !lib.IsEmpty() && adapter->HasLibrary( lib, true ) )
    {
        adapter->RefreshLibraryIfChanged( lib );

        // Enumerated footprints are cached by the library manager; read their counts now
        // (the cache can be replaced later, so no pointers are kept).
        for( FOOTPRINT* fp : adapter->GetFootprints( lib, true ) )
        {
            if( !fp )
                continue;

            FP_ROW row;
            row.name = fp->GetFPID().GetUniStringLibItemName();
            countPrimitives( fp, row.pads, row.primitives );
            m_footprints.push_back( row );
        }

        if( m_footprints.empty() )
        {
            for( const wxString& name : adapter->GetFootprintNames( lib, true ) )
            {
                FP_ROW row;
                row.name = name;
                m_footprints.push_back( row );
            }
        }
    }

    std::sort( m_footprints.begin(), m_footprints.end(),
               []( const FP_ROW& a, const FP_ROW& b )
               {
                   return StrNumCmp( a.name, b.name, true ) < 0;
               } );

    // Library chooser: every footprint library, the shown one selected
    if( adapter )
    {
        wxArrayString names;

        for( const wxString& name : adapter->GetLibraryNames() )
            names.Add( name );

        if( names != m_libraryChoice->GetStrings() )
            m_libraryChoice->Set( names );
    }

    if( !lib.IsEmpty() )
        m_libraryChoice->SetStringSelection( lib );

    applyMask();

    // A newly opened (or added) footprint takes the highlight; otherwise keep the user's pick
    LIB_ID loadedNow = m_frame->GetLoadedFPID();

    if( loadedNow != m_lastHighlightedLoaded )
    {
        m_lastHighlightedLoaded = loadedNow;

        if( loadedNow.IsValid() && loadedNow.GetUniStringLibNickname() == m_library )
        {
            for( long ii = -1; ( ii = m_footprintList->GetNextItem( ii, wxLIST_NEXT_ALL,
                                                                    wxLIST_STATE_SELECTED ) ) >= 0; )
            {
                m_footprintList->SetItemState( ii, 0, wxLIST_STATE_SELECTED );
            }

            selectLoadedFootprint();
            return;
        }
    }

    if( prevSel.IsValid() && prevSel.GetUniStringLibNickname() == m_library )
    {
        for( size_t ii = 0; ii < m_shown.size(); ++ii )
        {
            if( m_footprints[m_shown[ii]].name == prevSel.GetUniStringLibItemName() )
            {
                m_inSync = true;
                m_footprintList->SetItemState( (long) ii, wxLIST_STATE_SELECTED,
                                               wxLIST_STATE_SELECTED );
                m_inSync = false;
                return;
            }
        }
    }

    selectLoadedFootprint();
}


void PCB_LIBRARY_PANEL::applyMask()
{
    wxString mask = m_mask->GetValue().Trim().Trim( false ).Lower();
    bool     wildcard = mask.Contains( wxS( "*" ) ) || mask.Contains( wxS( "?" ) );

    m_shown.clear();

    for( size_t ii = 0; ii < m_footprints.size(); ++ii )
    {
        wxString name = m_footprints[ii].name.Lower();

        if( mask.IsEmpty() || ( wildcard ? name.Matches( mask ) : name.Contains( mask ) ) )
            m_shown.push_back( ii );
    }

    m_footprintList->SetItemCount( (long) m_shown.size() );
    m_footprintList->Refresh();
}


void PCB_LIBRARY_PANEL::selectLoadedFootprint()
{
    LIB_ID loaded = m_frame->GetLoadedFPID();

    if( !loaded.IsValid() || loaded.GetUniStringLibNickname() != m_library )
        return;

    for( size_t ii = 0; ii < m_shown.size(); ++ii )
    {
        if( m_footprints[m_shown[ii]].name == loaded.GetUniStringLibItemName() )
        {
            m_inSync = true;
            m_footprintList->SetItemState( (long) ii, wxLIST_STATE_SELECTED,
                                           wxLIST_STATE_SELECTED );
            m_footprintList->EnsureVisible( (long) ii );
            m_inSync = false;
            return;
        }
    }
}


wxString PCB_LIBRARY_PANEL::FootprintCellText( long aRow, long aColumn )
{
    if( aRow < 0 || aRow >= (long) m_shown.size() )
        return wxEmptyString;

    FP_ROW& row = m_footprints[m_shown[aRow]];

    if( aColumn == FP_COL_NAME )
        return row.name;

    // The footprint being edited shows its live counts
    LIB_ID     loaded = m_frame->GetLoadedFPID();
    FOOTPRINT* boardFp = m_frame->GetBoard() ? m_frame->GetBoard()->GetFirstFootprint() : nullptr;

    if( boardFp && loaded.GetUniStringLibNickname() == m_library
        && loaded.GetUniStringLibItemName() == row.name )
    {
        int pads = 0, prims = 0;
        countPrimitives( boardFp, pads, prims );
        return wxString::Format( wxS( "%d" ), aColumn == FP_COL_PADS ? pads : prims );
    }

    if( row.pads < 0 )
    {
        // Not in the library cache: load it once to count
        row.pads = 0;
        row.primitives = 0;

        if( FOOTPRINT_LIBRARY_ADAPTER* adapter = PROJECT_PCB::FootprintLibAdapter( &m_frame->Prj() ) )
        {
            try
            {
                std::unique_ptr<FOOTPRINT> fp( adapter->LoadFootprint( m_library, row.name, false ) );

                if( fp )
                    countPrimitives( fp.get(), row.pads, row.primitives );
            }
            catch( const IO_ERROR& )
            {
            }
        }
    }

    return wxString::Format( wxS( "%d" ), aColumn == FP_COL_PADS ? row.pads : row.primitives );
}


void PCB_LIBRARY_PANEL::rebuildPrimitives()
{
    BOARD*     board = m_frame->GetBoard();
    FOOTPRINT* fp = board ? board->GetFirstFootprint() : nullptr;

    long     sel = m_primitiveList->GetNextItem( -1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED );
    KIID     selId = ( sel >= 0 && sel < (long) m_primitives.size() ) ? m_primitives[sel].id
                                                                       : niluuid;

    m_primitives.clear();

    if( fp )
    {
        auto layerName =
                [&]( PCB_LAYER_ID aLayer )
                {
                    return ALTIUM_LAYER_TABS::DisplayName( aLayer, board->GetLayerName( aLayer ) );
                };

        auto sizeText =
                [&]( int aValue )
                {
                    return m_frame->MessageTextFromValue( static_cast<double>( aValue ) );
                };

        for( PAD* pad : fp->Pads() )
        {
            PRIM_ROW row;
            VECTOR2I size = pad->GetSize( PADSTACK::ALL_LAYERS );

            row.id = pad->m_Uuid;
            row.type = _( "Pad" );
            row.name = pad->GetNumber();
            row.xSize = sizeText( size.x );
            row.ySize = sizeText( size.y );

            if( pad->GetAttribute() == PAD_ATTRIB::PTH || pad->GetAttribute() == PAD_ATTRIB::NPTH )
                row.layer = _( "Multi-Layer" );
            else
                row.layer = layerName( pad->GetLayer() );

            m_primitives.push_back( row );
        }

        for( BOARD_ITEM* item : fp->GraphicalItems() )
        {
            PRIM_ROW row;
            BOX2I    bbox = item->GetBoundingBox();

            row.id = item->m_Uuid;
            row.xSize = sizeText( bbox.GetWidth() );
            row.ySize = sizeText( bbox.GetHeight() );
            row.layer = layerName( item->GetLayer() );

            switch( item->Type() )
            {
            case PCB_SHAPE_T:
            {
                PCB_SHAPE* shape = static_cast<PCB_SHAPE*>( item );

                switch( shape->GetShape() )
                {
                case SHAPE_T::SEGMENT:   row.type = _( "Track" );                       break;
                case SHAPE_T::ARC:
                case SHAPE_T::CIRCLE:    row.type = _( "Arc" );                         break;
                case SHAPE_T::RECTANGLE: row.type = shape->IsSolidFill() ? _( "Fill" )
                                                                         : _( "Rectangle" );
                                         break;
                case SHAPE_T::POLY:      row.type = shape->IsSolidFill() ? _( "Region" )
                                                                         : _( "Polyline" );
                                         break;
                case SHAPE_T::BEZIER:    row.type = _( "Bezier" );                      break;
                default:                 row.type = shape->GetFriendlyName();           break;
                }

                break;
            }

            case PCB_TEXT_T:
                row.type = _( "String" );
                row.name = static_cast<PCB_TEXT*>( item )->GetText();
                break;

            case PCB_TEXTBOX_T:
                row.type = _( "Text Frame" );
                break;

            default:
                row.type = item->GetFriendlyName();
                break;
            }

            m_primitives.push_back( row );
        }

        for( ZONE* zone : fp->Zones() )
        {
            PRIM_ROW row;
            BOX2I    bbox = zone->GetBoundingBox();

            row.id = zone->m_Uuid;
            row.type = zone->GetIsRuleArea() ? _( "Keepout" ) : _( "Region" );
            row.name = zone->GetZoneName();
            row.xSize = sizeText( bbox.GetWidth() );
            row.ySize = sizeText( bbox.GetHeight() );

            LSEQ layers = zone->GetLayerSet().Seq();
            row.layer = layers.size() == 1 ? layerName( layers.front() ) : _( "Multi-Layer" );

            m_primitives.push_back( row );
        }
    }

    m_primitivesLabel->SetLabel( fp ? wxString::Format( _( "Footprint Primitives (%zu)" ),
                                                        m_primitives.size() )
                                    : _( "Footprint Primitives" ) );

    m_primitiveList->Freeze();
    m_primitiveList->DeleteAllItems();

    for( size_t ii = 0; ii < m_primitives.size(); ++ii )
    {
        const PRIM_ROW& row = m_primitives[ii];
        long            idx = m_primitiveList->InsertItem( (long) ii, row.type );

        m_primitiveList->SetItem( idx, 1, row.name );
        m_primitiveList->SetItem( idx, 2, row.xSize );
        m_primitiveList->SetItem( idx, 3, row.ySize );
        m_primitiveList->SetItem( idx, 4, row.layer );

        if( row.id == selId && selId != niluuid )
        {
            m_inSync = true;
            m_primitiveList->SetItemState( idx, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED );
            m_inSync = false;
        }
    }

    m_primitiveList->Thaw();
}


void PCB_LIBRARY_PANEL::onTimer( wxTimerEvent& aEvent )
{
    if( !IsShownOnScreen() )
        return;

    BOARD*     board = m_frame->GetBoard();
    FOOTPRINT* fp = board ? board->GetFirstFootprint() : nullptr;
    wxString   lib = currentLibrary();
    LIB_ID     loaded = m_frame->GetLoadedFPID();

    // Library contents change when the library is switched, a footprint is opened, saved
    // (the modified flag clears), or the library changes on disk.
    // Reading the library's timestamp scans its folder on disk: do that every few seconds
    // (or when the library changes), not on every tick, so an idle editor stays idle
    wxLongLong now = wxGetLocalTimeMillis();

    if( lib != m_diskStampLib || now - m_diskStampTime > 5000 )
    {
        m_diskStamp = 0;
        m_diskStampLib = lib;
        m_diskStampTime = now;

        if( FOOTPRINT_LIBRARY_ADAPTER* adapter = PROJECT_PCB::FootprintLibAdapter( &m_frame->Prj() ) )
        {
            if( !lib.IsEmpty() && adapter->HasLibrary( lib, true ) )
                m_diskStamp = adapter->GenerateTimestamp( &lib );
        }
    }

    long long stamp = m_diskStamp;

    wxString fpSig = wxString::Format( wxS( "%s|%s|%d|%lld" ), lib,
                                       loaded.GetUniStringLibItemName(),
                                       m_frame->IsContentModified() ? 1 : 0, stamp );

    if( fpSig != m_footprintSig )
    {
        m_footprintSig = fpSig;
        rebuildFootprints();
    }

    wxString primSig = wxString::Format( wxS( "%p|%d|%d|%d|%zu|%zu|%d" ), (void*) fp,
                                         m_frame->GetUndoCommandCount(),
                                         m_frame->GetRedoCommandCount(),
                                         m_frame->IsContentModified() ? 1 : 0,
                                         fp ? fp->Pads().size() : 0,
                                         fp ? fp->GraphicalItems().size() : 0,
                                         (int) m_frame->GetUserUnits() );

    if( primSig != m_primitiveSig )
    {
        m_primitiveSig = primSig;
        rebuildPrimitives();
        m_footprintList->Refresh();   // live counts of the footprint being edited
    }

    updatePreview();
}


void PCB_LIBRARY_PANEL::onMask( wxCommandEvent& aEvent )
{
    applyMask();
    selectLoadedFootprint();
}


void PCB_LIBRARY_PANEL::onFootprintSelected( wxListEvent& aEvent )
{
    if( m_inSync )
        return;

    // Commands working on "the selected footprint" (delete, rename, duplicate, ...) read the
    // library tree's selection, so mirror the pick there.
    LIB_ID id = GetSelectedLibId();

    if( id.IsValid() && m_frame->GetLibTree() )
        m_frame->GetLibTree()->SelectLibId( id );

    updatePreview();
}


void PCB_LIBRARY_PANEL::onFootprintActivated( wxListEvent& aEvent )
{
    LIB_ID id = GetSelectedLibId();

    if( !id.IsValid() || id == m_frame->GetLoadedFPID() )
        return;

    m_frame->LoadFootprintFromLibrary( id );
    m_footprintSig.clear();
}


void PCB_LIBRARY_PANEL::onFootprintContextMenu( wxListEvent& aEvent )
{
    // The menu acts on the row clicked, as in Altium: make it the only selected one
    long row = aEvent.GetIndex();

    if( row >= 0 && row < m_footprintList->GetItemCount()
        && !( m_footprintList->GetItemState( row, wxLIST_STATE_SELECTED ) & wxLIST_STATE_SELECTED ) )
    {
        for( long ii = -1; ( ii = m_footprintList->GetNextItem( ii, wxLIST_NEXT_ALL,
                                                                wxLIST_STATE_SELECTED ) ) >= 0; )
        {
            m_footprintList->SetItemState( ii, 0, wxLIST_STATE_SELECTED );
        }

        m_footprintList->SetItemState( row, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                                       wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED );
    }

    LIB_ID id = GetSelectedLibId();

    if( id.IsValid() && m_frame->GetLibTree() )
        m_frame->GetLibTree()->SelectLibId( id );

    bool hasSel = id.IsValid();

    wxMenu menu;
    menu.Append( ID_NEW_BLANK, _( "New Blank Footprint" ) );
    menu.Append( ID_WIZARD, _( "Footprint Wizard..." ) );
    menu.AppendSeparator();
    menu.Append( ID_OPEN, _( "Open" ) )->Enable( hasSel );
    menu.Append( ID_PROPERTIES, _( "Footprint Properties..." ) )
            ->Enable( hasSel && id == m_frame->GetLoadedFPID() );
    menu.AppendSeparator();
    menu.Append( ID_CUT, _( "Cut" ) )->Enable( hasSel );
    menu.Append( ID_COPY, _( "Copy" ) )->Enable( hasSel );
    menu.Append( ID_PASTE, _( "Paste" ) );
    menu.Append( ID_DUPLICATE, _( "Duplicate" ) )->Enable( hasSel );
    menu.Append( ID_RENAME, _( "Rename..." ) )->Enable( hasSel );
    menu.Append( ID_DELETE, _( "Delete" ) )->Enable( hasSel );
    menu.AppendSeparator();
    menu.Append( ID_EXPORT, _( "Export Footprint..." ) )
            ->Enable( hasSel && id == m_frame->GetLoadedFPID() );
    menu.AppendSeparator();
    menu.Append( ID_REFRESH, _( "Refresh" ) );

    int picked = GetPopupMenuSelectionFromUser( menu );

    TOOL_MANAGER*      toolMgr = m_frame->GetToolManager();
    const TOOL_ACTION* action = nullptr;

    switch( picked )
    {
    case ID_NEW_BLANK:  action = &PCB_ACTIONS::newFootprint;        break;
    case ID_WIZARD:     action = &PCB_ACTIONS::createFootprint;     break;
    case ID_PROPERTIES: action = &PCB_ACTIONS::footprintProperties; break;
    case ID_CUT:        action = &PCB_ACTIONS::cutFootprint;        break;
    case ID_COPY:       action = &PCB_ACTIONS::copyFootprint;       break;
    case ID_PASTE:      action = &PCB_ACTIONS::pasteFootprint;      break;
    case ID_DUPLICATE:  action = &PCB_ACTIONS::duplicateFootprint;  break;
    case ID_RENAME:     action = &PCB_ACTIONS::renameFootprint;     break;
    case ID_DELETE:     action = &PCB_ACTIONS::deleteFootprint;     break;
    case ID_EXPORT:     action = &PCB_ACTIONS::exportFootprint;     break;

    case ID_OPEN:
    {
        wxListEvent dummy;
        onFootprintActivated( dummy );
        break;
    }

    case ID_REFRESH:
        Rebuild();
        break;

    default:
        break;
    }

    if( action && toolMgr )
    {
        toolMgr->RunAction( *action );
        m_footprintSig.clear();
    }
}


void PCB_LIBRARY_PANEL::onPrimitiveSelected( wxListEvent& aEvent )
{
    if( m_inSync )
        return;

    if( !m_frame->GetBoard() )
        return;

    // The canvas selection follows every selected row (the list allows several)
    std::vector<EDA_ITEM*> items;

    for( long row = -1; ( row = m_primitiveList->GetNextItem( row, wxLIST_NEXT_ALL,
                                                              wxLIST_STATE_SELECTED ) ) >= 0; )
    {
        if( row >= (long) m_primitives.size() )
            continue;

        if( BOARD_ITEM* item = m_frame->GetBoard()->ResolveItem( m_primitives[row].id, true ) )
            items.push_back( item );
    }

    TOOL_MANAGER* toolMgr = m_frame->GetToolManager();
    toolMgr->RunAction( ACTIONS::selectionClear );

    if( !items.empty() )
        toolMgr->RunAction<std::vector<EDA_ITEM*>*>( ACTIONS::selectItems, &items );
}


void PCB_LIBRARY_PANEL::onPrimitiveActivated( wxListEvent& aEvent )
{
    onPrimitiveSelected( aEvent );

    if( m_frame->GetToolManager() )
        m_frame->GetToolManager()->RunAction( ACTIONS::zoomFitSelection );

    m_frame->ShowPropertiesPanel();
}
