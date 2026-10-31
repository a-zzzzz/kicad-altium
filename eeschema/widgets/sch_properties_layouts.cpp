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

/**
 * Altium's per-object layouts of the schematic and symbol editors' Properties panel: a
 * Component, Wire, Bus, Net Label, Port, Off Sheet Connector, Power Port, Sheet Symbol, Sheet
 * Entry, Text, Text Frame, No ERC, Junction and (in the symbol editor) Pin each get Altium's
 * sections, drawn by PROPERTIES_FORMs bound to the panel's property rows.  Nothing selected shows
 * Altium's Document Options (General | Parameters).  KiCad-only rows stay out of sight under a
 * layout (a search still finds them); they are in the object's properties dialog.
 */

#include "sch_properties_panel.h"

#include <dialogs/dialog_color_picker.h>
#include <dialogs/dialog_component_pin_editor.h>
#include <search_stack.h>
#include <embedded_files.h>
#include <eda_doc.h>
#include <frame_type.h>
#include <kiway.h>
#include <kiway_mail.h>
#include <kiway_player.h>
#include <lib_symbol.h>
#include <page_info.h>
#include <project.h>
#include <properties/property.h>
#include <properties/property_mgr.h>
#include <sch_base_frame.h>
#include <netclass.h>
#include <sch_commit.h>
#include <sch_connection.h>
#include <sch_draw_panel.h>
#include <sch_edit_frame.h>
#include <sch_field.h>
#include <sch_junction.h>
#include <sch_label.h>
#include <sch_line.h>
#include <sch_no_connect.h>
#include <sch_pin.h>
#include <sch_screen.h>
#include <sch_shape.h>
#include <sch_sheet.h>
#include <sch_sheet_pin.h>
#include <sch_symbol.h>
#include <power_port_style.h>
#include <font/font.h>
#include <font/kicad_font_name.h>
#include <wx/fontenum.h>
#include <sch_text.h>
#include <sch_textbox.h>
#include <schematic.h>
#include <schematic_settings.h>
#include <settings/app_settings.h>
#include <settings/color_settings.h>
#include <eeschema_settings.h>
#include <sim/sim_model.h>
#include <string_utils.h>
#include <symbol_edit_frame.h>
#include <template_fieldnames.h>
#include <title_block.h>
#include <tool/actions.h>
#include <tool/grid_menu.h>
#include <tool/tool_manager.h>
#include <tools/sch_actions.h>
#include <widgets/sch_model_pane.h>
#include <widgets/footprint_preview_widget.h>
#include <widgets/properties_form.h>

#include <wx/filename.h>
#include <wx/propgrid/propgrid.h>
#include <wx/sizer.h>
#include <wx/textdlg.h>

#include <algorithm>


namespace
{
const wxString LAYOUT_COMPONENT = wxS( "component" );
const wxString LAYOUT_LIBCOMPONENT = wxS( "libcomponent" );
const wxString LAYOUT_POWER = wxS( "power" );
const wxString LAYOUT_WIRE = wxS( "wire" );
const wxString LAYOUT_BUS = wxS( "bus" );
const wxString LAYOUT_LINE = wxS( "line" );
const wxString LAYOUT_NETLABEL = wxS( "netlabel" );
const wxString LAYOUT_PORT = wxS( "port" );
const wxString LAYOUT_OFFSHEET = wxS( "offsheet" );
const wxString LAYOUT_SHEET = wxS( "sheet" );
const wxString LAYOUT_ENTRY = wxS( "entry" );
const wxString LAYOUT_TEXT = wxS( "text" );
const wxString LAYOUT_TEXTFRAME = wxS( "textframe" );
const wxString LAYOUT_NOERC = wxS( "noerc" );
const wxString LAYOUT_JUNCTION = wxS( "junction" );
const wxString LAYOUT_PIN = wxS( "pin" );
const wxString LAYOUT_DOCUMENT = wxS( "document" );
const wxString LAYOUT_RECTANGLE = wxS( "rectangle" );

// Properties registered here (registerAltiumProperties)
const wxString PROP_X = wxS( "Position X" );
const wxString PROP_Y = wxS( "Position Y" );
const wxString PROP_SHEET_WIDTH = wxS( "Sheet Width" );
const wxString PROP_SHEET_HEIGHT = wxS( "Sheet Height" );
const wxString PROP_LIB_DESCRIPTION = wxS( "Description" );

using CELL = PROPERTIES_FORM::CELL;
using OPTS = PROPERTIES_FORM::OPTS;


CELL heading( const wxString& aText )
{
    CELL cell( aText );
    cell.heading = true;
    return cell;
}


CELL fixed( const wxString& aText )
{
    CELL cell( aText );
    cell.readOnly = true;
    return cell;
}


/// The wxPGProperty's value as a long, or aDefault when unset / mixed
long propLong( wxPropertyGrid* aGrid, const wxString& aName, long aDefault = -1 )
{
    wxPGProperty* prop = aGrid->GetPropertyByName( aName );

    if( !prop || prop->IsValueUnspecified() )
        return aDefault;

    return prop->GetValue().GetLong();
}


/// A bool property as 0 / 1, or -1 when unset / mixed
long propBool( wxPropertyGrid* aGrid, const wxString& aName )
{
    wxPGProperty* prop = aGrid->GetPropertyByName( aName );

    if( !prop || prop->IsValueUnspecified() )
        return -1;

    return prop->GetValue().GetBool() ? 1 : 0;
}


/**
 * A property KiCad's schematic items have but don't register (text locations, sheet sizes, a
 * library symbol's description), added to the property manager at run time like the panel's
 * field properties.
 */
template <typename V>
class SCH_EXTRA_PROPERTY : public PROPERTY_BASE
{
public:
    SCH_EXTRA_PROPERTY( const wxString& aName, size_t aOwner, PROPERTY_DISPLAY aDisplay,
                        ORIGIN_TRANSFORMS::COORD_TYPES_T aCoordType,
                        std::function<V( const void* )> aGet, std::function<void( void*, V )> aSet ) :
            PROPERTY_BASE( aName, aDisplay, aCoordType ),
            m_owner( aOwner ),
            m_get( std::move( aGet ) ),
            m_set( std::move( aSet ) )
    {
    }

    size_t OwnerHash() const override { return m_owner; }
    size_t BaseHash() const override { return m_owner; }
    size_t TypeHash() const override { return TYPE_HASH( V ); }

    bool Writeable( INSPECTABLE* aObject ) const override
    {
        return PROPERTY_BASE::Writeable( aObject );
    }

    void setter( void* obj, wxAny& v ) override
    {
        V value;

        if( v.GetAs( &value ) )
            m_set( obj, value );
    }

    wxAny getter( const void* obj ) const override { return wxAny( m_get( obj ) ); }

private:
    size_t                          m_owner;
    std::function<V( const void* )> m_get;
    std::function<void( void*, V )> m_set;
};


/// Altium's Designator / Comment locks, kept as private (hidden, never listed) fields so they
/// travel with the symbol into the library and the schematic
const wxString LOCK_DESIGNATOR_FIELD = wxS( "Designator Locked" );
const wxString LOCK_COMMENT_FIELD = wxS( "Comment Locked" );


SCH_FIELD* namedField( SCH_ITEM* aItem, const wxString& aName )
{
    if( SCH_SYMBOL* symbol = dynamic_cast<SCH_SYMBOL*>( aItem ) )
        return symbol->GetField( aName );

    if( LIB_SYMBOL* libSymbol = dynamic_cast<LIB_SYMBOL*>( aItem ) )
        return libSymbol->GetField( aName );

    return nullptr;
}


bool fieldLocked( SCH_ITEM* aItem, const wxString& aLockField )
{
    SCH_FIELD* field = namedField( aItem, aLockField );
    return field && field->GetText() == wxS( "1" );
}


void setFieldLocked( SCH_ITEM* aItem, const wxString& aLockField, bool aLocked )
{
    SCH_FIELD* field = namedField( aItem, aLockField );

    if( !aLocked )
    {
        if( !field )
            return;

        if( SCH_SYMBOL* symbol = dynamic_cast<SCH_SYMBOL*>( aItem ) )
            symbol->RemoveField( field );
        else if( LIB_SYMBOL* libSymbol = dynamic_cast<LIB_SYMBOL*>( aItem ) )
            libSymbol->RemoveField( field );

        return;
    }

    if( field )
    {
        field->SetText( wxS( "1" ) );
        return;
    }

    SCH_FIELD newField( aItem, FIELD_T::USER, aLockField );
    newField.SetText( wxS( "1" ) );
    newField.SetVisible( false );
    newField.SetPrivate( true );

    if( SCH_SYMBOL* symbol = dynamic_cast<SCH_SYMBOL*>( aItem ) )
    {
        newField.SetPosition( symbol->GetPosition() );
        symbol->AddField( newField );
    }
    else if( LIB_SYMBOL* libSymbol = dynamic_cast<LIB_SYMBOL*>( aItem ) )
    {
        libSymbol->AddField( newField );
    }
}


/// The graphics of a library symbol's body (what Altium's Local Colors recolour)
std::vector<SCH_SHAPE*> bodyShapes( LIB_SYMBOL* aSymbol )
{
    std::vector<SCH_SHAPE*> shapes;

    if( aSymbol )
    {
        for( SCH_ITEM& item : aSymbol->GetDrawItems() )
        {
            if( item.Type() == SCH_SHAPE_T )
                shapes.push_back( static_cast<SCH_SHAPE*>( &item ) );
        }
    }

    return shapes;
}


/// Location rows (X / Y) for an item kind that keeps its position in GetPosition()
template <typename T>
void addLocation( PROPERTY_MANAGER& aMgr )
{
    if( aMgr.GetProperty( TYPE_HASH( T ), PROP_X ) )
        return;

    const wxString group = _HKI( "Location" );

    aMgr.AddProperty( new SCH_EXTRA_PROPERTY<int>( PROP_X, TYPE_HASH( T ), PT_COORD,
                                                   ORIGIN_TRANSFORMS::ABS_X_COORD,
            []( const void* aObj )
            {
                return reinterpret_cast<const T*>( aObj )->GetPosition().x;
            },
            []( void* aObj, int aValue )
            {
                T*       item = reinterpret_cast<T*>( aObj );
                VECTOR2I pos = item->GetPosition();
                pos.x = aValue;
                item->SetPosition( pos );
            } ),
            group );

    aMgr.AddProperty( new SCH_EXTRA_PROPERTY<int>( PROP_Y, TYPE_HASH( T ), PT_COORD,
                                                   ORIGIN_TRANSFORMS::ABS_Y_COORD,
            []( const void* aObj )
            {
                return reinterpret_cast<const T*>( aObj )->GetPosition().y;
            },
            []( void* aObj, int aValue )
            {
                T*       item = reinterpret_cast<T*>( aObj );
                VECTOR2I pos = item->GetPosition();
                pos.y = aValue;
                item->SetPosition( pos );
            } ),
            group );
}


/// Altium's Rotation of text-like items (0 / 90 / 180 / 270 degrees), or -1 for none
int textRotation( const SCH_ITEM* aItem )
{
    if( const SCH_LABEL_BASE* label = dynamic_cast<const SCH_LABEL_BASE*>( aItem ) )
    {
        SPIN_STYLE spin = label->GetSpinStyle();

        if( spin == SPIN_STYLE::RIGHT )
            return 0;
        else if( spin == SPIN_STYLE::UP )
            return 1;
        else if( spin == SPIN_STYLE::LEFT )
            return 2;
        else
            return 3;
    }

    if( const SCH_TEXT* text = dynamic_cast<const SCH_TEXT*>( aItem ) )
    {
        // KiCad keeps text upright: 180 / 270 degrees are 0 / 90 justified the other way
        bool vertical = text->GetTextAngle().IsVertical();
        bool flipped = text->GetHorizJustify() == GR_TEXT_H_ALIGN_RIGHT;
        return ( vertical ? 1 : 0 ) + ( flipped ? 2 : 0 );
    }

    return -1;
}


void setTextRotation( SCH_ITEM* aItem, int aRotation )
{
    if( SCH_LABEL_BASE* label = dynamic_cast<SCH_LABEL_BASE*>( aItem ) )
    {
        static const SPIN_STYLE::SPIN spins[] = { SPIN_STYLE::RIGHT, SPIN_STYLE::UP, SPIN_STYLE::LEFT,
                                                  SPIN_STYLE::BOTTOM };
        label->SetSpinStyle( SPIN_STYLE( spins[std::clamp( aRotation, 0, 3 )] ) );
        return;
    }

    if( SCH_TEXT* text = dynamic_cast<SCH_TEXT*>( aItem ) )
    {
        // Counter-clockwise quarter turns: 0 -> 90 -> 180 -> 270 -> 0
        for( int ii = 0; ii < 4 && textRotation( text ) != aRotation; ++ii )
            text->Rotate90( false );
    }
}


std::vector<wxString> rotationNames()
{
    return { _( "0 Degrees" ), _( "90 Degrees" ), _( "180 Degrees" ), _( "270 Degrees" ) };
}


/// Which of Altium's Parameters-tab kinds a field is (1 Footprints, 2 Models, 3 Parameters,
/// 4 Links), or 0 for fields the General tab shows instead
int parameterKind( const SCH_FIELD* aField )
{
    switch( aField->GetId() )
    {
    case FIELD_T::REFERENCE:
    case FIELD_T::VALUE:
    case FIELD_T::DESCRIPTION:
    case FIELD_T::SHEET_NAME:
    case FIELD_T::SHEET_FILENAME:
        return 0;

    case FIELD_T::FOOTPRINT:
        return 1;

    case FIELD_T::DATASHEET:
        return 4;

    default:
        break;
    }

    if( aField->GetName().StartsWith( wxS( "Sim." ) ) )
        return 2;

    wxString text = aField->GetText().Lower();

    if( text.StartsWith( wxS( "http://" ) ) || text.StartsWith( wxS( "https://" ) ) )
        return 4;

    return 3;
}


bool isMandatory( const SCH_FIELD* aField )
{
    return aField->GetId() != FIELD_T::USER;
}


/// Altium's component Type, expressed with KiCad's BOM / board exclusions
struct COMPONENT_TYPE
{
    wxString label;
    bool     noBom;
    bool     noBoard;
};


const std::vector<COMPONENT_TYPE>& componentTypes()
{
    static const std::vector<COMPONENT_TYPE> types = {
        { _( "Standard" ),          false, false },
        { _( "Standard (No BOM)" ), true,  false },
        { _( "Graphical" ),         true,  true  } };

    return types;
}
} // namespace


// ------------------------------------------------------------------------------------- plumbing

void SCH_PROPERTIES_PANEL::registerAltiumProperties()
{
    static bool done = false;

    if( done )
        return;

    done = true;

    PROPERTY_MANAGER& mgr = PROPERTY_MANAGER::Instance();

    // Text, labels, ports and sheet entries (all SCH_TEXT), sheets, junctions and no-connects
    addLocation<SCH_TEXT>( mgr );
    addLocation<SCH_SHEET>( mgr );
    addLocation<SCH_JUNCTION>( mgr );

    // No-connect flags aren't registered with the property manager at all
    REGISTER_TYPE( SCH_NO_CONNECT );
    mgr.InheritsAfter( TYPE_HASH( SCH_NO_CONNECT ), TYPE_HASH( SCH_ITEM ) );
    addLocation<SCH_NO_CONNECT>( mgr );

    // A sheet symbol's size
    auto sheetSize =
            []( bool aHeight )
            {
                return new SCH_EXTRA_PROPERTY<int>( aHeight ? PROP_SHEET_HEIGHT : PROP_SHEET_WIDTH,
                                                    TYPE_HASH( SCH_SHEET ), PT_SIZE,
                                                    ORIGIN_TRANSFORMS::NOT_A_COORD,
                        [aHeight]( const void* aObj )
                        {
                            const SCH_SHEET* sheet = reinterpret_cast<const SCH_SHEET*>( aObj );
                            return aHeight ? sheet->GetSize().y : sheet->GetSize().x;
                        },
                        [aHeight]( void* aObj, int aValue )
                        {
                            SCH_SHEET* sheet = reinterpret_cast<SCH_SHEET*>( aObj );
                            VECTOR2I   size = sheet->GetSize();

                            if( aHeight )
                                size.y = std::max( aValue, sheet->GetMinHeight( false ) );
                            else
                                size.x = std::max( aValue, sheet->GetMinWidth( false ) );

                            sheet->Resize( size );
                        } );
            };

    if( !mgr.GetProperty( TYPE_HASH( SCH_SHEET ), PROP_SHEET_WIDTH ) )
    {
        mgr.AddProperty( sheetSize( false ) );
        mgr.AddProperty( sheetSize( true ) );
    }

    // A library symbol's description (a field KiCad's property list doesn't show)
    if( !mgr.GetProperty( TYPE_HASH( LIB_SYMBOL ), PROP_LIB_DESCRIPTION ) )
    {
        mgr.AddProperty( new SCH_EXTRA_PROPERTY<wxString>( PROP_LIB_DESCRIPTION, TYPE_HASH( LIB_SYMBOL ),
                                                           PT_DEFAULT, ORIGIN_TRANSFORMS::NOT_A_COORD,
                []( const void* aObj )
                {
                    return reinterpret_cast<const LIB_SYMBOL*>( aObj )->GetDescription();
                },
                []( void* aObj, wxString aValue )
                {
                    reinterpret_cast<LIB_SYMBOL*>( aObj )->SetDescription( aValue );
                } ) );
    }
}


std::vector<SCH_ITEM*> SCH_PROPERTIES_PANEL::selectedItems()
{
    SELECTION              fallback;
    const SELECTION&       selection = getSelection( fallback );
    std::vector<SCH_ITEM*> items;

    for( EDA_ITEM* item : selection )
    {
        if( item->IsSCH_ITEM() )
            items.push_back( static_cast<SCH_ITEM*>( item ) );
    }

    return items;
}


SCH_ITEM* SCH_PROPERTIES_PANEL::singleItem()
{
    std::vector<SCH_ITEM*> items = selectedItems();
    return items.size() == 1 ? items.front() : nullptr;
}


void SCH_PROPERTIES_PANEL::commitSelection( const wxString& aMessage,
                                            const std::function<void( SCH_COMMIT&, SCH_ITEM* )>& aChange )
{
    SCH_COMMIT             commit( m_frame );
    std::vector<SCH_ITEM*> items = selectedItems();

    for( SCH_ITEM* item : items )
    {
        if( !item->IsNew() )
            aChange( commit, item );
    }

    if( !commit.Empty() )
        commit.Push( aMessage );

    for( SCH_ITEM* item : selectedItems() )
        m_frame->UpdateItem( item );

    m_frame->GetCanvas()->Refresh();
    AfterCommit();
}


std::vector<SCH_FIELD*> SCH_PROPERTIES_PANEL::itemFields( SCH_ITEM* aItem )
{
    std::vector<SCH_FIELD*> fields;

    if( SYMBOL* symbol = dynamic_cast<SYMBOL*>( aItem ) )
    {
        symbol->GetFields( fields, false );
    }
    else if( SCH_SHEET* sheet = dynamic_cast<SCH_SHEET*>( aItem ) )
    {
        for( SCH_FIELD& field : sheet->GetFields() )
            fields.push_back( &field );
    }
    else if( SCH_LABEL_BASE* label = dynamic_cast<SCH_LABEL_BASE*>( aItem ) )
    {
        for( SCH_FIELD& field : label->GetFields() )
            fields.push_back( &field );
    }

    std::erase_if( fields, []( SCH_FIELD* aField ) { return aField->IsPrivate(); } );
    return fields;
}


PROPERTIES_FORM* SCH_PROPERTIES_PANEL::addLayoutForm( const wxString& aTitle, const wxString& aLayout, int aTab )
{
    // Created on the panel (for its font) and moved into the scrolled area as a section
    PROPERTIES_FORM* form = new PROPERTIES_FORM( this, this );
    addSelectionSection( form, aTitle, aLayout, aTab );
    m_layoutForms.push_back( { aLayout, form } );
    return form;
}


wxString SCH_PROPERTIES_PANEL::layoutFor( const SELECTION& aSelection ) const
{
    if( aSelection.Empty() )
        return wxEmptyString;

    bool     symbolEditor = m_frame->IsType( FRAME_SCH_SYMBOL_EDITOR );
    wxString layout;

    for( EDA_ITEM* item : aSelection )
    {
        wxString kind;

        switch( item->Type() )
        {
        case SCH_SYMBOL_T:
            kind = static_cast<SCH_SYMBOL*>( item )->IsPower() ? LAYOUT_POWER : LAYOUT_COMPONENT;
            break;

        case LIB_SYMBOL_T:
            if( symbolEditor )
                kind = LAYOUT_LIBCOMPONENT;

            break;

        case SCH_LINE_T:
            if( static_cast<SCH_LINE*>( item )->IsWire() )
                kind = LAYOUT_WIRE;
            else if( static_cast<SCH_LINE*>( item )->IsBus() )
                kind = LAYOUT_BUS;
            else
                kind = LAYOUT_LINE;

            break;

        case SCH_LABEL_T:        kind = LAYOUT_NETLABEL;  break;
        case SCH_HIER_LABEL_T:   kind = LAYOUT_PORT;      break;
        case SCH_GLOBAL_LABEL_T: kind = LAYOUT_OFFSHEET;  break;
        case SCH_SHEET_T:        kind = LAYOUT_SHEET;     break;
        case SCH_SHEET_PIN_T:    kind = LAYOUT_ENTRY;     break;
        case SCH_TEXT_T:         kind = LAYOUT_TEXT;      break;
        case SCH_TEXTBOX_T:      kind = LAYOUT_TEXTFRAME; break;
        case SCH_NO_CONNECT_T:   kind = LAYOUT_NOERC;     break;
        case SCH_JUNCTION_T:     kind = LAYOUT_JUNCTION;  break;

        case SCH_SHAPE_T:
            if( static_cast<SCH_SHAPE*>( item )->GetShape() == SHAPE_T::RECTANGLE )
                kind = LAYOUT_RECTANGLE;

            break;

        case SCH_PIN_T:
            if( symbolEditor )
                kind = LAYOUT_PIN;

            break;

        default:
            break;
        }

        if( kind.IsEmpty() || ( !layout.IsEmpty() && kind != layout ) )
            return wxEmptyString;

        layout = kind;
    }

    return layout;
}


wxString SCH_PROPERTIES_PANEL::documentLayout() const
{
    // Document Options (schematic) and Library Options (symbol editor)
    return m_frame->IsType( FRAME_SCH ) || m_frame->IsType( FRAME_SCH_SYMBOL_EDITOR )
                   ? LAYOUT_DOCUMENT : wxString();
}


void SCH_PROPERTIES_PANEL::selectionGridUpdated( const SELECTION& aSelection )
{
    wxString    layout = layoutFor( aSelection );
    const void* front = aSelection.Empty() ? nullptr : aSelection.Front();

    if( front != m_paramItem )
    {
        m_paramItem = front;
        m_paramRow = -1;
        m_vertexRow = -1;
    }

    m_currentLayout = layout;
    m_boundProps.clear();

    if( !layout.IsEmpty() )
    {
        for( LAYOUT_FORM& entry : m_layoutForms )
        {
            if( entry.layout == layout )
                entry.form->CollectBound( m_boundProps );
        }
    }

    syncLayoutForms();
    setSelectionLayout( layout );
    updateFootprintPreview();
}


void SCH_PROPERTIES_PANEL::syncLayoutForms()
{
    for( LAYOUT_FORM& entry : m_layoutForms )
    {
        if( entry.layout == m_currentLayout )
            entry.form->Sync();
    }
}


std::vector<wxString> SCH_PROPERTIES_PANEL::panelTabs( const SELECTION& aSelection ) const
{
    if( aSelection.Empty() )
    {
        if( m_frame->IsType( FRAME_SCH ) )
            return { _( "General" ), _( "Parameters" ) };

        return {};
    }

    wxString layout = layoutFor( aSelection );

    if( layout == LAYOUT_COMPONENT || layout == LAYOUT_LIBCOMPONENT )
        return { _( "General" ), _( "Pins" ) };

    if( layout == LAYOUT_SHEET || layout == LAYOUT_PORT || layout == LAYOUT_PIN )
        return { _( "General" ), _( "Parameters" ) };

    return {};
}


int SCH_PROPERTIES_PANEL::categoryTab( wxPGProperty* aCategory ) const
{
    if( m_documentMode )
    {
        // Document Options: the General and Page Options sections replace KiCad's rows
        if( ( m_frame->IsType( FRAME_SCH ) || m_frame->IsType( FRAME_SCH_SYMBOL_EDITOR ) )
                && ( aCategory->GetName() == wxS( "doc.general" )
                     || aCategory->GetName() == wxS( "doc.snap" ) ) )
        {
            return -1;
        }

        return 0;
    }

    // Under an Altium layout the sections are the whole panel, as in Altium; KiCad's other
    // settings for the object are in its properties dialog (double-click)
    if( !m_currentLayout.IsEmpty() )
        return -1;

    return 0;
}


wxString SCH_PROPERTIES_PANEL::documentCaption() const
{
    if( m_frame->IsType( FRAME_SCH_SYMBOL_EDITOR ) )
        return _( "Library Options" );

    return _( "Document Options" );
}


wxString SCH_PROPERTIES_PANEL::itemTypeName( EDA_ITEM* aItem ) const
{
    // Altium's object names
    switch( aItem->Type() )
    {
    case SCH_SYMBOL_T:
        return static_cast<SCH_SYMBOL*>( aItem )->IsPower() ? _( "Power Port" ) : _( "Component" );

    case LIB_SYMBOL_T:       return _( "Component" );

    case SCH_LINE_T:
        if( static_cast<SCH_LINE*>( aItem )->IsWire() )
            return _( "Wire" );
        else if( static_cast<SCH_LINE*>( aItem )->IsBus() )
            return _( "Bus" );

        return _( "Line" );

    case SCH_LABEL_T:        return _( "Net Label" );
    case SCH_HIER_LABEL_T:   return _( "Port" );
    case SCH_GLOBAL_LABEL_T: return _( "Off Sheet Connector" );
    case SCH_SHEET_T:        return _( "Sheet Symbol" );
    case SCH_SHEET_PIN_T:    return _( "Sheet Entry" );
    case SCH_TEXT_T:         return _( "Text" );
    case SCH_TEXTBOX_T:      return _( "Text Frame" );
    case SCH_NO_CONNECT_T:   return _( "No ERC" );
    case SCH_JUNCTION_T:     return _( "Junction" );
    case SCH_PIN_T:          return _( "Pin" );
    case SCH_FIELD_T:        return _( "Parameter" );

    case SCH_SHAPE_T:
        switch( static_cast<SCH_SHAPE*>( aItem )->GetShape() )
        {
        case SHAPE_T::RECTANGLE: return _( "Rectangle" );
        case SHAPE_T::ARC:       return _( "Arc" );
        case SHAPE_T::CIRCLE:    return _( "Full Circle" );
        case SHAPE_T::POLY:      return _( "Polygon" );
        case SHAPE_T::BEZIER:    return _( "Bezier" );
        default:                 break;
        }

        break;

    default:
        break;
    }

    return PROPERTIES_PANEL::itemTypeName( aItem );
}


void SCH_PROPERTIES_PANEL::addColorRow( PROPERTIES_FORM* aForm, const wxString& aProp, const wxString& aLabel )
{
    // The colour of the first selected item that has the property
    auto colour =
            [this, aProp]() -> std::optional<KIGFX::COLOR4D>
            {
                for( SCH_ITEM* item : selectedItems() )
                {
                    if( PROPERTY_BASE* prop = m_propMgr.GetProperty( TYPE_HASH( *item ), aProp ) )
                    {
                        wxAny          value = item->Get( prop );
                        KIGFX::COLOR4D colour;

                        if( value.GetAs( &colour ) )
                            return colour;
                    }
                }

                return std::nullopt;
            };

    OPTS opts;
    opts.visible = [this, aProp]() { return m_grid->GetPropertyByName( aProp ) != nullptr; };
    opts.swatchColour =
            [colour]()
            {
                std::optional<KIGFX::COLOR4D> c = colour();
                return c && c.value() != KIGFX::COLOR4D::UNSPECIFIED ? c.value().ToColour() : wxNullColour;
            };

    // Default | Custom, and Choose... to pick (another) colour
    aForm->AddCustomChoice( aLabel, { _( "Default" ), _( "Custom" ), _( "Choose..." ) },
            [colour]() -> int
            {
                std::optional<KIGFX::COLOR4D> c = colour();

                if( !c )
                    return -1;

                return c.value() == KIGFX::COLOR4D::UNSPECIFIED ? 0 : 1;
            },
            [this, colour, aProp]( int aChoice )
            {
                KIGFX::COLOR4D newColour = KIGFX::COLOR4D::UNSPECIFIED;

                if( aChoice >= 1 )
                {
                    std::optional<KIGFX::COLOR4D> c = colour();
                    DIALOG_COLOR_PICKER dlg( this, c ? c.value() : KIGFX::COLOR4D::UNSPECIFIED, true );

                    if( dlg.ShowModal() != wxID_OK )
                        return;

                    newColour = dlg.GetColor();
                }

                commitSelection( _( "Change Color" ),
                        [&]( SCH_COMMIT& aCommit, SCH_ITEM* aItem )
                        {
                            if( PROPERTY_BASE* prop = m_propMgr.GetProperty( TYPE_HASH( *aItem ), aProp ) )
                            {
                                aCommit.Modify( aItem, m_frame->GetScreen() );

                                wxAny value( newColour );
                                aItem->Set( prop, value );
                            }
                        } );
            },
            opts );
}


// ---------------------------------------------------------------------------------- the layouts

void SCH_PROPERTIES_PANEL::buildAltiumLayouts()
{
    if( m_frame->IsType( FRAME_SCH ) )
    {
        buildComponentLayout( LAYOUT_COMPONENT );
        buildPowerPortLayout();
        buildWireLayout( LAYOUT_WIRE );
        buildWireLayout( LAYOUT_BUS );
        buildWireLayout( LAYOUT_LINE );
        buildLabelLayout( LAYOUT_NETLABEL );
        buildLabelLayout( LAYOUT_PORT );
        buildLabelLayout( LAYOUT_OFFSHEET );
        buildSheetLayout();
        buildSheetEntryLayout();
        buildTextLayout();
        buildTextFrameLayout();
        buildNoErcLayout();
        buildJunctionLayout();
        buildRectangleLayout();
        buildDocumentLayout();
    }
    else if( m_frame->IsType( FRAME_SCH_SYMBOL_EDITOR ) )
    {
        buildComponentLayout( LAYOUT_LIBCOMPONENT );
        buildDocumentLayout();
        buildPinLayout();
        buildRectangleLayout();
        buildTextLayout();
        buildTextFrameLayout();
    }
}


void SCH_PROPERTIES_PANEL::addFontRows( PROPERTIES_FORM* aForm )
{
    // Altium's Font row: face, size and colour, then the style buttons
    aForm->AddChoice( wxS( "Font" ), _( "Font" ) );
    aForm->AddText( wxS( "Text Size" ), _( "Size" ) );
    addColorRow( aForm, wxS( "Color" ), _( "Color" ) );
    aForm->AddCheck( wxS( "Bold" ), _( "Bold" ), true );
    aForm->AddCheck( wxS( "Italic" ), _( "Italic" ), true );
}


// ------------------------------------------------------------------------------------ component

void SCH_PROPERTIES_PANEL::buildComponentLayout( const wxString& aLayout )
{
    const wxString& L = aLayout;
    const bool      lib = aLayout == LAYOUT_LIBCOMPONENT;

    // Property names differ in case between placed and library symbols
    const wxString noBomProp = lib ? wxS( "Exclude from Bill of Materials" )
                                   : wxS( "Exclude From Bill of Materials" );
    const wxString noBoardProp = lib ? wxS( "Exclude from Board" ) : wxS( "Exclude From Board" );

    // Designator and Comment: an eye that shows / hides the text on the sheet
    auto fieldEye =
            [this]( FIELD_T aField )
            {
                OPTS opts;
                opts.linkLabel = true;
                opts.eyeState =
                        [this, aField]()
                        {
                            SCH_ITEM* item = selectedItems().empty() ? nullptr : selectedItems().front();

                            for( SCH_FIELD* field : item ? itemFields( item ) : std::vector<SCH_FIELD*>() )
                            {
                                if( field->GetId() == aField )
                                    return field->IsVisible();
                            }

                            return false;
                        };
                opts.eyeToggle =
                        [this, aField, eyeState = opts.eyeState]()
                        {
                            bool show = !eyeState();

                            commitSelection( show ? _( "Show Parameter" ) : _( "Hide Parameter" ),
                                    [&]( SCH_COMMIT& aCommit, SCH_ITEM* aItem )
                                    {
                                        for( SCH_FIELD* field : itemFields( aItem ) )
                                        {
                                            if( field->GetId() == aField )
                                            {
                                                aCommit.Modify( aItem, m_frame->GetScreen() );
                                                field->SetVisible( show );
                                            }
                                        }
                                    } );
                        };
                return opts;
            };

    // General
    PROPERTIES_FORM* general = addLayoutForm( _( "General" ), L );

    auto designItemId =
            [this]() -> wxString
            {
                SCH_ITEM* item = singleItem();

                if( LIB_SYMBOL* libSymbol = dynamic_cast<LIB_SYMBOL*>( item ) )
                    return UnescapeString( libSymbol->GetName() );

                if( SCH_SYMBOL* symbol = dynamic_cast<SCH_SYMBOL*>( item ) )
                    return UnescapeString( symbol->GetLibId().GetLibItemName() );

                return wxEmptyString;
            };

    if( lib )
        general->AddInfo( _( "Design Item ID" ), designItemId, true );

    // ... and Altium's lock beside the eye
    auto withLock =
            [this]( OPTS aOpts, const wxString& aLockField, const wxString& aLockMsg,
                    const wxString& aUnlockMsg )
            {
                aOpts.lockState =
                        [this, aLockField]()
                        {
                            SCH_ITEM* item = selectedItems().empty() ? nullptr : selectedItems().front();
                            return item && fieldLocked( item, aLockField );
                        };
                aOpts.lockToggle =
                        [this, aLockField, aLockMsg, aUnlockMsg, state = aOpts.lockState]()
                        {
                            bool lock = !state();

                            commitSelection( lock ? aLockMsg : aUnlockMsg,
                                    [&]( SCH_COMMIT& aCommit, SCH_ITEM* aItem )
                                    {
                                        aCommit.Modify( aItem, m_frame->GetScreen() );
                                        setFieldLocked( aItem, aLockField, lock );
                                    } );
                        };
                return aOpts;
            };

    general->AddText( wxS( "Reference" ), _( "Designator" ),
                      withLock( fieldEye( FIELD_T::REFERENCE ), LOCK_DESIGNATOR_FIELD,
                                _( "Lock Designator" ), _( "Unlock Designator" ) ) );
    general->AddText( wxS( "Value" ), _( "Comment" ),
                      withLock( fieldEye( FIELD_T::VALUE ), LOCK_COMMENT_FIELD,
                                _( "Lock Comment" ), _( "Unlock Comment" ) ) );

    if( lib )
    {
        // Altium's lock on "of Parts": the parts are not interchangeable
        OPTS partsLock;
        partsLock.lockState =
                [this]()
                {
                    LIB_SYMBOL* libSymbol = dynamic_cast<LIB_SYMBOL*>( singleItem() );
                    return libSymbol && libSymbol->UnitsLocked();
                };
        partsLock.lockToggle =
                [this, state = partsLock.lockState]()
                {
                    bool lock = !state();

                    commitSelection( lock ? _( "Lock Parts" ) : _( "Unlock Parts" ),
                            [&]( SCH_COMMIT& aCommit, SCH_ITEM* aItem )
                            {
                                if( LIB_SYMBOL* libSymbol = dynamic_cast<LIB_SYMBOL*>( aItem ) )
                                {
                                    aCommit.Modify( aItem, m_frame->GetScreen() );
                                    libSymbol->LockUnits( lock );
                                }
                            } );
                };
        general->AddText( wxS( "Number of Symbol Units" ), _( "of Parts" ), partsLock );
    }
    else
    {
        general->AddChoice( wxS( "Unit" ), _( "Part" ) );
    }

    OPTS description;
    description.lines = 3;
    general->AddText( lib ? PROP_LIB_DESCRIPTION : GetCanonicalFieldName( FIELD_T::DESCRIPTION ),
                      _( "Description" ), description );

    std::vector<wxString> typeNames;

    for( const COMPONENT_TYPE& type : componentTypes() )
        typeNames.push_back( type.label );

    general->AddCustomChoice( _( "Type" ), typeNames,
            [this, noBomProp, noBoardProp]() -> int
            {
                long noBom = propBool( m_grid, noBomProp );
                long noBoard = propBool( m_grid, noBoardProp );
                const std::vector<COMPONENT_TYPE>& types = componentTypes();

                for( size_t ii = 0; ii < types.size(); ++ii )
                {
                    if( noBom == (long) types[ii].noBom && noBoard == (long) types[ii].noBoard )
                        return (int) ii;
                }

                return -1;     // a combination Altium has no name for, or a mixed selection
            },
            [this, noBomProp, noBoardProp]( int aType )
            {
                const std::vector<COMPONENT_TYPE>& types = componentTypes();

                if( aType < 0 || aType >= (int) types.size() )
                    return;

                applyPropertyValues( { { noBomProp, wxVariant( types[aType].noBom ) },
                                       { noBoardProp, wxVariant( types[aType].noBoard ) } } );
            } );

    if( !lib )
    {
        // Design Item ID and Source: the library symbol the component was placed from
        general->AddInfo( _( "Design Item ID" ), designItemId, true );
        general->AddInfo( _( "Source" ),
                [this]() -> wxString
                {
                    SCH_SYMBOL* symbol = dynamic_cast<SCH_SYMBOL*>( singleItem() );
                    return symbol ? UnescapeString( symbol->GetLibId().GetLibNickname() ) : wxString();
                },
                true );

        // Location
        PROPERTIES_FORM* location = addLayoutForm( _( "Location" ), L );
        location->AddXY( wxS( "Position X" ), wxS( "Position Y" ), wxEmptyString );

        static const std::vector<long> angles = { SYMBOL_ANGLE_0, SYMBOL_ANGLE_90, SYMBOL_ANGLE_180,
                                                  SYMBOL_ANGLE_270 };

        location->AddCustomChoice( _( "Rotation" ), rotationNames(),
                [this]() -> int
                {
                    long value = propLong( m_grid, wxS( "Orientation" ), LONG_MIN );
                    auto it = std::find( angles.begin(), angles.end(), value );
                    return it == angles.end() ? -1 : (int) ( it - angles.begin() );
                },
                [this]( int aIdx )
                {
                    if( aIdx >= 0 && aIdx < (int) angles.size() )
                        applyPropertyValues( { { wxS( "Orientation" ), wxVariant( angles[aIdx] ) } } );
                } );
    }

    // Parameters (with the footprint, models and links), and the footprint preview it shows
    buildParametersSection( L, 0, _( "Parameters" ) );
    buildFootprintPreview( L );

    // Graphical: mode and mirroring of a placed part; Local Colors of the library body
    PROPERTIES_FORM* graphical = addLayoutForm( _( "Graphical" ), L );

    if( !lib )
    {
        graphical->AddChoice( wxS( "Body Style" ), _( "Mode" ) );
        graphical->AddCheck( wxS( "Mirror Y" ), _( "Mirrored" ), true );
    }
    else
    {
        buildLocalColors( graphical );
    }

    buildPartChoices( L );
    buildPinsTab( L );
}


void SCH_PROPERTIES_PANEL::buildLocalColors( PROPERTIES_FORM* aForm )
{
    // Altium's Local Colors: the body's fill and line colours instead of the theme's.  KiCad
    // keeps them on the body graphics (pins have no colour of their own).
    auto shapes = [this]() { return bodyShapes( dynamic_cast<LIB_SYMBOL*>( singleItem() ) ); };

    auto localFill =
            []( SCH_SHAPE* aShape )
            {
                return aShape->GetFillMode() == FILL_T::FILLED_WITH_COLOR
                       && aShape->GetFillColor() != KIGFX::COLOR4D::UNSPECIFIED;
            };

    auto localLine =
            []( SCH_SHAPE* aShape )
            {
                return aShape->GetStroke().GetColor() != KIGFX::COLOR4D::UNSPECIFIED;
            };

    aForm->AddCustomCheck( _( "Local Colors" ),
            [shapes, localFill, localLine]() -> int
            {
                for( SCH_SHAPE* shape : shapes() )
                {
                    if( localFill( shape ) || localLine( shape ) )
                        return 1;
                }

                return 0;
            },
            [this]( bool aOn )
            {
                commitSelection( aOn ? _( "Local Colors" ) : _( "Theme Colors" ),
                        [&]( SCH_COMMIT& aCommit, SCH_ITEM* aItem )
                        {
                            std::vector<SCH_SHAPE*> list = bodyShapes( dynamic_cast<LIB_SYMBOL*>( aItem ) );

                            if( list.empty() )
                                return;

                            aCommit.Modify( aItem, m_frame->GetScreen() );

                            for( SCH_SHAPE* shape : list )
                            {
                                STROKE_PARAMS stroke = shape->GetStroke();

                                if( aOn )
                                {
                                    // Start from Altium's defaults: dark red lines, pale yellow fill
                                    stroke.SetColor( KIGFX::COLOR4D( 0.5, 0.0, 0.0, 1.0 ) );

                                    if( shape->GetFillMode() != FILL_T::NO_FILL )
                                    {
                                        shape->SetFillMode( FILL_T::FILLED_WITH_COLOR );
                                        shape->SetFillColor( KIGFX::COLOR4D( 1.0, 1.0, 0.69, 1.0 ) );
                                    }
                                }
                                else
                                {
                                    stroke.SetColor( KIGFX::COLOR4D::UNSPECIFIED );

                                    if( shape->GetFillMode() == FILL_T::FILLED_WITH_COLOR )
                                        shape->SetFillMode( FILL_T::FILLED_WITH_BG_BODYCOLOR );
                                }

                                shape->SetStroke( stroke );
                            }
                        } );
            } );

    auto colourRow =
            [this, aForm, shapes]( const wxString& aLabel, bool aFill )
            {
                auto current =
                        [shapes, aFill]() -> KIGFX::COLOR4D
                        {
                            for( SCH_SHAPE* shape : shapes() )
                            {
                                KIGFX::COLOR4D c = aFill ? ( shape->GetFillMode() == FILL_T::FILLED_WITH_COLOR
                                                                     ? shape->GetFillColor()
                                                                     : KIGFX::COLOR4D::UNSPECIFIED )
                                                         : shape->GetStroke().GetColor();

                                if( c != KIGFX::COLOR4D::UNSPECIFIED )
                                    return c;
                            }

                            return KIGFX::COLOR4D::UNSPECIFIED;
                        };

                OPTS opts;
                opts.swatchColour =
                        [current]()
                        {
                            KIGFX::COLOR4D c = current();
                            return c == KIGFX::COLOR4D::UNSPECIFIED ? wxNullColour : c.ToColour();
                        };

                aForm->AddCustomChoice( aLabel, { _( "Default" ), _( "Custom" ), _( "Choose..." ) },
                        [current]() -> int
                        {
                            return current() == KIGFX::COLOR4D::UNSPECIFIED ? 0 : 1;
                        },
                        [this, current, aFill]( int aChoice )
                        {
                            KIGFX::COLOR4D colour = KIGFX::COLOR4D::UNSPECIFIED;

                            if( aChoice >= 1 )
                            {
                                DIALOG_COLOR_PICKER dlg( this, current(), true );

                                if( dlg.ShowModal() != wxID_OK )
                                    return;

                                colour = dlg.GetColor();
                            }

                            commitSelection( _( "Change Color" ),
                                    [&]( SCH_COMMIT& aCommit, SCH_ITEM* aItem )
                                    {
                                        std::vector<SCH_SHAPE*> list =
                                                bodyShapes( dynamic_cast<LIB_SYMBOL*>( aItem ) );

                                        if( list.empty() )
                                            return;

                                        aCommit.Modify( aItem, m_frame->GetScreen() );

                                        for( SCH_SHAPE* shape : list )
                                        {
                                            if( aFill )
                                            {
                                                if( shape->GetFillMode() == FILL_T::NO_FILL )
                                                    continue;

                                                if( colour == KIGFX::COLOR4D::UNSPECIFIED )
                                                {
                                                    shape->SetFillMode( FILL_T::FILLED_WITH_BG_BODYCOLOR );
                                                }
                                                else
                                                {
                                                    shape->SetFillMode( FILL_T::FILLED_WITH_COLOR );
                                                    shape->SetFillColor( colour );
                                                }
                                            }
                                            else
                                            {
                                                STROKE_PARAMS stroke = shape->GetStroke();
                                                stroke.SetColor( colour );
                                                shape->SetStroke( stroke );
                                            }
                                        }
                                    } );
                        },
                        opts );
            };

    colourRow( _( "Fills" ), true );
    colourRow( _( "Lines" ), false );

    // Pins have no colour of their own in KiCad: the theme's pin colour
    aForm->AddInfo( _( "Pins" ), []() { return _( "Theme color" ); }, true );
}


void SCH_PROPERTIES_PANEL::buildPartChoices( const wxString& aLayout )
{
    // Altium's Part Choices: the manufacturer parts the component can be bought as.  KiCad keeps
    // them as parameters (Manufacturer / MPN, Supplier / Supplier Part Number ...).
    static const std::vector<std::pair<wxString, wxString>> pairs = {
        { wxS( "Manufacturer" ), wxS( "MPN" ) },
        { wxS( "Manufacturer" ), wxS( "Manufacturer Part Number" ) },
        { wxS( "Supplier" ), wxS( "Supplier Part Number" ) },
        { wxS( "Supplier 1" ), wxS( "Supplier Part Number 1" ) },
        { wxS( "Supplier 2" ), wxS( "Supplier Part Number 2" ) } };

    PROPERTIES_FORM* choices = addLayoutForm( _( "Part Choices" ), aLayout );

    auto rows =
            [this]()
            {
                std::vector<std::pair<wxString, wxString>> found;
                SCH_ITEM* item = singleItem();

                if( !item )
                    return found;

                auto text =
                        [&]( const wxString& aName ) -> wxString
                        {
                            SCH_FIELD* field = namedField( item, aName );
                            return field ? field->GetShownText( false ) : wxString();
                        };

                for( const auto& [maker, number] : pairs )
                {
                    wxString part = text( number );

                    if( !part.IsEmpty() )
                        found.emplace_back( text( maker ), part );
                }

                return found;
            };

    choices->AddButtons( { { _( "Edit Supplier Links..." ),
                             [this]()
                             {
                                 // Adds the Manufacturer / MPN parameters to fill in
                                 commitSelection( _( "Add Part Choice" ),
                                         [&]( SCH_COMMIT& aCommit, SCH_ITEM* aItem )
                                         {
                                             aCommit.Modify( aItem, m_frame->GetScreen() );

                                             for( const wxString& name : { wxString( wxS( "Manufacturer" ) ),
                                                                           wxString( wxS( "MPN" ) ) } )
                                             {
                                                 if( namedField( aItem, name ) )
                                                     continue;

                                                 SCH_FIELD field( aItem, FIELD_T::USER, name );
                                                 field.SetVisible( false );

                                                 if( SCH_SYMBOL* symbol = dynamic_cast<SCH_SYMBOL*>( aItem ) )
                                                 {
                                                     field.SetPosition( symbol->GetPosition() );
                                                     symbol->AddField( field );
                                                 }
                                                 else if( LIB_SYMBOL* lib = dynamic_cast<LIB_SYMBOL*>( aItem ) )
                                                 {
                                                     lib->AddField( field );
                                                 }
                                             }
                                         } );

                                 m_paramFilter = 3;   // Parameters, where they are filled in
                                 syncLayoutForms();
                             } } } );

    OPTS none;
    none.visible = [rows]() { return rows().empty(); };
    choices->AddInfo( wxEmptyString, []() { return _( "No part choices found" ); }, false, none );

    OPTS some;
    some.visible = [rows]() { return !rows().empty(); };
    choices->AddTable( { 1, 1 },
            [rows]()
            {
                PROPERTIES_FORM::TABLE_ROWS table;
                table.push_back( { heading( _( "Manufacturer" ) ), heading( _( "Part Number" ) ) } );

                for( const auto& [maker, part] : rows() )
                    table.push_back( { CELL( maker ), CELL( part ) } );

                return table;
            },
            nullptr, some );
}


void SCH_PROPERTIES_PANEL::buildFootprintPreview( const wxString& aLayout )
{
    // A headerless section under Parameters, created on first use ("Show" in the Footprint row)
    if( !m_fpPreviewHolder )
    {
        m_fpPreviewHolder = new wxPanel( this );
        m_fpPreviewHolder->SetSizer( new wxBoxSizer( wxVERTICAL ) );
    }

    addSelectionSection( m_fpPreviewHolder, wxEmptyString, aLayout, 0 );
}


void SCH_PROPERTIES_PANEL::updateFootprintPreview()
{
    if( !m_fpPreviewHolder )
        return;

    bool component = m_currentLayout == LAYOUT_COMPONENT || m_currentLayout == LAYOUT_LIBCOMPONENT;

    // Only while asked for: the preview is a GAL canvas
    if( !component || !m_showPreview )
    {
        if( m_fpPreview && m_fpPreview->IsShown() )
        {
            m_fpPreview->Hide();
            m_fpPreviewHolder->SetMinSize( wxSize( -1, 0 ) );
            relayout();
        }

        m_fpPreviewHolder->SetMinSize( wxSize( -1, 0 ) );
        return;
    }

    wxString text;

    if( SCH_ITEM* item = singleItem() )
    {
        for( SCH_FIELD* field : itemFields( item ) )
        {
            if( field->GetId() == FIELD_T::FOOTPRINT )
                text = field->GetText();
        }
    }

    bool needLayout = !m_fpPreview || !m_fpPreview->IsShown()
                      || m_fpPreviewHolder->GetMinSize().y < FromDIP( 170 );

    m_fpPreviewHolder->SetMinSize( wxSize( -1, FromDIP( 170 ) ) );

    if( !m_fpPreview )
    {
        try
        {
            m_fpPreview = new FOOTPRINT_PREVIEW_WIDGET( m_fpPreviewHolder, m_frame->Kiway() );
        }
        catch( ... )
        {
            m_fpPreview = nullptr;
        }

        if( !m_fpPreview )
            return;

        m_fpPreview->SetUserUnits( m_frame->GetUserUnits() );
        m_fpPreviewHolder->GetSizer()->Add( m_fpPreview, 1, wxEXPAND );
        m_fpPreviewId = wxS( "\x01" );
    }

    m_fpPreview->Show();

    if( text != m_fpPreviewId && m_fpPreview->IsInitialized() )
    {
        m_fpPreviewId = text;
        LIB_ID id;

        if( text.IsEmpty() )
        {
            m_fpPreview->SetStatusText( _( "No footprint specified" ) );
        }
        else if( id.Parse( text ) == -1 && id.IsValid() )
        {
            m_fpPreview->ClearStatus();
            m_fpPreview->DisplayFootprint( id );
        }
        else
        {
            m_fpPreview->SetStatusText( _( "Invalid footprint specified" ) );
        }
    }

    if( needLayout )
    {
        m_fpPreviewHolder->Layout();
        relayout();
    }
}


// ---------------------------------------------------------------------------- parameters section

void SCH_PROPERTIES_PANEL::buildParametersSection( const wxString& aLayout, int aTab, const wxString& aTitle )
{
    const bool component = aLayout == LAYOUT_COMPONENT || aLayout == LAYOUT_LIBCOMPONENT;

    PROPERTIES_FORM* params = addLayoutForm( aTitle, aLayout, aTab );

    // Altium's filter chips.  A component has All | Footprints | Models | Parameters | Links |
    // Rules (KiCad has no component rules: always "No Rules"); other objects All | Parameters |
    // Rules.  The index stored is a component-style kind (0 All, 1 Footprints, ... 5 Rules).
    std::vector<wxString> chips;
    std::vector<int>      chipKinds;

    if( component )
    {
        chips = { _( "All" ), _( "Footprints" ), _( "Models" ), _( "Parameters" ), _( "Links" ), _( "Rules" ) };
        chipKinds = { 0, 1, 2, 3, 4, 5 };
    }
    else
    {
        chips = { _( "All" ), _( "Parameters" ), _( "Rules" ) };
        chipKinds = { 0, 3, 5 };
    }

    params->AddCustomSegmented( chips,
            [this, chipKinds]() -> int
            {
                auto it = std::find( chipKinds.begin(), chipKinds.end(), m_paramFilter );
                return it == chipKinds.end() ? 0 : (int) ( it - chipKinds.begin() );
            },
            [this, chipKinds]( int aChip )
            {
                m_paramFilter = chipKinds[aChip];
                m_paramRow = -1;
                syncLayoutForms();
            } );

    auto setFieldText =
            [this]( const wxString& aName, const wxString& aCanonical, const wxString& aText )
            {
                SCH_ITEM* owner = singleItem();

                if( !owner )
                    return;

                // Placed symbols and sheets go through their field properties (variants, other
                // units); other fields are changed directly
                if( ( owner->Type() == SCH_SYMBOL_T || owner->Type() == SCH_SHEET_T )
                        && m_grid->GetPropertyByName( aCanonical ) )
                {
                    applyPropertyValues( { { aCanonical, wxVariant( aText ) } } );
                    return;
                }

                commitSelection( _( "Edit Parameter" ),
                        [&]( SCH_COMMIT& aCommit, SCH_ITEM* aItem )
                        {
                            for( SCH_FIELD* field : itemFields( aItem ) )
                            {
                                if( field->GetName() == aName )
                                {
                                    aCommit.Modify( aItem, m_frame->GetScreen() );
                                    field->SetText( aText );
                                }
                            }
                        } );
            };

    auto chooseFootprint =
            [this]()
            {
                SCH_ITEM* owner = singleItem();
                wxString  fpid;

                for( SCH_FIELD* field : owner ? itemFields( owner ) : std::vector<SCH_FIELD*>() )
                {
                    if( field->GetId() == FIELD_T::FOOTPRINT )
                        fpid = field->GetText();
                }

                // Altium's Browse Libraries (as the PCB Model dialog uses), not KiCad's chooser
                LIB_ID start;
                start.Parse( fpid );

                LIB_ID picked = BrowseFootprintLibraries( m_frame, start );

                if( picked.IsValid() )
                {
                    applyPropertyValues( { { wxS( "Footprint" ),
                                             wxVariant( wxString( picked.Format().wx_str() ) ) } } );
                }
            };

    // Table rows: [eye] | Name | Value [| Show]; the fields behind them are kept for the buttons
    auto fieldRows =
            [this]()
            {
                std::vector<SCH_FIELD*> result;
                SCH_ITEM*               item = singleItem();

                if( !item )
                    return result;

                std::vector<SCH_FIELD*> fields = itemFields( item );

                // Altium's order: footprints, models, parameters, links
                for( int kind = 1; kind <= 4; ++kind )
                {
                    if( m_paramFilter != 0 && m_paramFilter != kind )
                        continue;

                    for( SCH_FIELD* field : fields )
                    {
                        if( parameterKind( field ) == kind )
                            result.push_back( field );
                    }
                }

                return result;
            };

    params->AddTable( { 2, 8, 8, 3 },
            [this, component, fieldRows, setFieldText, chooseFootprint]()
            {
                PROPERTIES_FORM::TABLE_ROWS rows;
                rows.push_back( { heading( wxEmptyString ), heading( _( "Name" ) ), heading( _( "Value" ) ) } );

                if( selectedItems().size() > 1 )
                {
                    rows.push_back( { fixed( _( "Select one object to see its parameters" ) ) } );
                    return rows;
                }

                std::vector<SCH_FIELD*> fields = fieldRows();
                bool                    anyModel = false;
                bool                    anyParam = false;

                for( SCH_FIELD* field : fields )
                {
                    int      kind = parameterKind( field );
                    wxString fieldName = field->GetName();
                    CELL     eye;

                    anyModel |= kind == 2;
                    anyParam |= kind == 3;

                    // "No Models" between the footprints and the parameters
                    if( component && kind >= 3 && !anyModel && m_paramFilter == 0 )
                    {
                        rows.push_back( { fixed( _( "No Models" ) ) } );
                        anyModel = true;
                    }

                    if( kind != 1 )
                    {
                        eye.eye = field->IsVisible() ? 1 : 0;
                        eye.click =
                                [this, fieldName]()
                                {
                                    commitSelection( _( "Show / Hide Parameter" ),
                                            [&]( SCH_COMMIT& aCommit, SCH_ITEM* aItem )
                                            {
                                                for( SCH_FIELD* f : itemFields( aItem ) )
                                                {
                                                    if( f->GetName() == fieldName )
                                                    {
                                                        aCommit.Modify( aItem, m_frame->GetScreen() );
                                                        f->SetVisible( !f->IsVisible() );
                                                    }
                                                }
                                            } );
                                };
                    }

                    // Parameter names are blue in Altium; KiCad's own fields keep their names
                    CELL name( fieldName );

                    if( isMandatory( field ) )
                    {
                        // KiCad's own fields keep their names (a click selects the row)
                    }
                    else
                    {
                        name.link = kind == 3;
                        name.edit =
                                [this, fieldName]( const wxString& aNewName )
                                {
                                    wxString newName = aNewName;
                                    newName.Trim().Trim( false );

                                    if( newName.IsEmpty() )
                                        return;

                                    commitSelection( _( "Rename Parameter" ),
                                            [&]( SCH_COMMIT& aCommit, SCH_ITEM* aItem )
                                            {
                                                for( SCH_FIELD* f : itemFields( aItem ) )
                                                {
                                                    if( f->GetName() == fieldName )
                                                    {
                                                        aCommit.Modify( aItem, m_frame->GetScreen() );
                                                        f->SetName( newName );
                                                    }
                                                }
                                            } );
                                };

                        // (blue, as Altium's parameter names; a click edits the name)
                    }

                    wxString canonical = field->GetCanonicalName();
                    wxString text = field->GetText();
                    CELL     value( kind == 1 ? UnescapeString( text ) : text );

                    if( kind == 1 )
                    {
                        // Footprint: the chooser, and Altium's "Show" link for the preview
                        value.readOnly = false;
                        value.link = false;
                        value.choices = { _( "Choose..." ), _( "None" ) };
                        value.choose =
                                [this, chooseFootprint]( int aIdx )
                                {
                                    if( aIdx == 0 )
                                        CallAfter( [chooseFootprint]() { chooseFootprint(); } );
                                    else
                                        applyPropertyValues( { { wxS( "Footprint" ), wxVariant( wxString() ) } } );
                                };

                        CELL show( m_showPreview ? _( "Hide" ) : _( "Show" ) );
                        show.link = true;
                        show.click =
                                [this]()
                                {
                                    m_showPreview = !m_showPreview;
                                    updateFootprintPreview();
                                    syncLayoutForms();
                                };

                        rows.push_back( { eye, name, value, show } );
                        continue;
                    }

                    if( kind == 4 && !text.IsEmpty() && text != wxS( "~" ) )
                    {
                        // A link opens on a click; the edit button changes it
                        value.link = true;
                        value.click = [this, text]() { GetAssociatedDocument( this, text, &m_frame->Prj() ); };
                    }
                    else
                    {
                        value.edit = [setFieldText, fieldName, canonical]( const wxString& aText )
                                     {
                                         setFieldText( fieldName, canonical, aText );
                                     };
                    }

                    rows.push_back( { eye, name, value } );
                }

                if( fields.empty() && m_paramFilter == 3 )
                    rows.push_back( { fixed( _( "No Parameters" ) ) } );

                if( component && !anyModel && ( m_paramFilter == 0 || m_paramFilter == 2 ) )
                    rows.push_back( { fixed( _( "No Models" ) ) } );

                if( !component && !anyParam && m_paramFilter == 0 )
                    rows.push_back( { fixed( _( "No Parameters" ) ) } );

                if( m_paramFilter == 0 || m_paramFilter == 5 )
                    rows.push_back( { fixed( _( "No Rules" ) ) } );

                return rows;
            },
            &m_paramRow );

    // Add... / edit / delete
    auto selectedField =
            [this, fieldRows]() -> SCH_FIELD*
            {
                // The table's rows: the heading, then one per field (plus placeholder rows,
                // which come after the fields they would precede only for "No Models")
                std::vector<SCH_FIELD*>     fields = fieldRows();
                int                         row = 1;
                bool                        modelGap = false;

                for( SCH_FIELD* field : fields )
                {
                    int kind = parameterKind( field );

                    if( !modelGap && kind >= 3 && m_paramFilter == 0
                            && ( m_currentLayout == LAYOUT_COMPONENT || m_currentLayout == LAYOUT_LIBCOMPONENT ) )
                    {
                        bool anyModel = std::any_of( fields.begin(), fields.end(),
                                                     []( SCH_FIELD* f ) { return parameterKind( f ) == 2; } );

                        if( !anyModel )
                            row++;

                        modelGap = true;
                    }

                    if( row == m_paramRow )
                        return field;

                    row++;
                }

                return nullptr;
            };

    params->AddButtons( {
            { _( "Add..." ),
              [this]()
              {
                  SCH_ITEM* owner = singleItem();

                  if( !owner )
                      return;

                  wxTextEntryDialog dlg( this, _( "Parameter name:" ), _( "Add Parameter" ) );

                  if( dlg.ShowModal() != wxID_OK )
                      return;

                  wxString name = dlg.GetValue();
                  name.Trim().Trim( false );

                  if( name.IsEmpty() )
                      return;

                  for( SCH_FIELD* field : itemFields( owner ) )
                  {
                      if( field->GetName() == name )
                          return;     // already there
                  }

                  commitSelection( _( "Add Parameter" ),
                          [&]( SCH_COMMIT& aCommit, SCH_ITEM* aItem )
                          {
                              aCommit.Modify( aItem, m_frame->GetScreen() );

                              if( SCH_SYMBOL* symbol = dynamic_cast<SCH_SYMBOL*>( aItem ) )
                              {
                                  SCH_FIELD field( symbol, FIELD_T::USER, name );
                                  field.SetTextPos( symbol->GetPosition() );
                                  field.SetVisible( false );
                                  symbol->AddField( field );
                              }
                              else if( LIB_SYMBOL* libSymbol = dynamic_cast<LIB_SYMBOL*>( aItem ) )
                              {
                                  SCH_FIELD* field = new SCH_FIELD( libSymbol, FIELD_T::USER, name );
                                  field->SetVisible( false );
                                  libSymbol->AddField( field );
                              }
                              else if( SCH_SHEET* sheet = dynamic_cast<SCH_SHEET*>( aItem ) )
                              {
                                  SCH_FIELD field( sheet, FIELD_T::USER, name );
                                  field.SetTextPos( sheet->GetPosition() );
                                  field.SetVisible( false );
                                  sheet->AddField( field );
                              }
                              else if( SCH_LABEL_BASE* label = dynamic_cast<SCH_LABEL_BASE*>( aItem ) )
                              {
                                  SCH_FIELD field( label, FIELD_T::USER, name );
                                  field.SetTextPos( label->GetPosition() );
                                  field.SetVisible( false );
                                  label->GetFields().push_back( field );
                              }
                          } );
              } },
            { _( "Edit" ),
              [this, selectedField]()
              {
                  SCH_FIELD* field = selectedField();

                  if( !field )
                      return;

                  wxTextEntryDialog dlg( this, wxString::Format( _( "Value of '%s':" ), field->GetName() ),
                                         _( "Edit Parameter" ), field->GetText() );

                  if( dlg.ShowModal() != wxID_OK )
                      return;

                  wxString name = field->GetName();
                  wxString text = dlg.GetValue();

                  commitSelection( _( "Edit Parameter" ),
                          [&]( SCH_COMMIT& aCommit, SCH_ITEM* aItem )
                          {
                              for( SCH_FIELD* f : itemFields( aItem ) )
                              {
                                  if( f->GetName() == name )
                                  {
                                      aCommit.Modify( aItem, m_frame->GetScreen() );
                                      f->SetText( text );
                                  }
                              }
                          } );
              } },
            { _( "Delete" ),
              [this, selectedField]()
              {
                  SCH_FIELD* selected = selectedField();

                  if( !selected || isMandatory( selected ) )
                      return;

                  wxString name = selected->GetName();
                  m_paramRow = -1;

                  commitSelection( _( "Remove Parameter" ),
                          [&]( SCH_COMMIT& aCommit, SCH_ITEM* aItem )
                          {
                              aCommit.Modify( aItem, m_frame->GetScreen() );

                              auto removeFrom =
                                      [&]( std::vector<SCH_FIELD>& aFields )
                                      {
                                          std::erase_if( aFields, [&]( const SCH_FIELD& f )
                                                                  {
                                                                      return f.GetName() == name;
                                                                  } );
                                      };

                              if( SCH_SYMBOL* symbol = dynamic_cast<SCH_SYMBOL*>( aItem ) )
                              {
                                  symbol->RemoveField( name );
                              }
                              else if( LIB_SYMBOL* libSymbol = dynamic_cast<LIB_SYMBOL*>( aItem ) )
                              {
                                  if( SCH_FIELD* field = libSymbol->GetField( name ) )
                                      libSymbol->RemoveField( field );
                              }
                              else if( SCH_SHEET* sheet = dynamic_cast<SCH_SHEET*>( aItem ) )
                              {
                                  std::vector<SCH_FIELD> kept = sheet->GetFields();
                                  removeFrom( kept );
                                  sheet->SetFields( kept );
                              }
                              else if( SCH_LABEL_BASE* label = dynamic_cast<SCH_LABEL_BASE*>( aItem ) )
                              {
                                  removeFrom( label->GetFields() );
                              }
                          } );
              } } } );
}


void SCH_PROPERTIES_PANEL::buildPinsTab( const wxString& aLayout )
{
    const bool lib = aLayout == LAYOUT_LIBCOMPONENT;

    PROPERTIES_FORM* pins = addLayoutForm( _( "Pins" ), aLayout, 1 );

    pins->AddTable( { 1, 5, 1, 6 },
            [this]()
            {
                PROPERTIES_FORM::TABLE_ROWS rows;
                rows.push_back( { heading( wxEmptyString ), heading( _( "Pins" ) ), heading( wxEmptyString ),
                                  heading( _( "Name" ) ) } );

                SYMBOL* symbol = dynamic_cast<SYMBOL*>( singleItem() );

                if( !symbol )
                    return rows;

                std::vector<SCH_PIN*> list;

                if( LIB_SYMBOL* libSymbol = dynamic_cast<LIB_SYMBOL*>( symbol ) )
                    list = libSymbol->GetGraphicalPins( 0, 0 );
                else
                    list = symbol->GetPins();

                std::sort( list.begin(), list.end(),
                           []( SCH_PIN* a, SCH_PIN* b )
                           {
                               return StrNumCmp( a->GetShownNumber(), b->GetShownNumber(), true ) < 0;
                           } );

                for( SCH_PIN* pin : list )
                {
                    // Hidden pins are greyed, as in Altium
                    CELL eye;
                    eye.eye = pin->IsVisible() ? 1 : 0;

                    CELL number( wxString::Format( _( "- Pin %s" ), pin->GetShownNumber() ) );
                    CELL name( pin->GetShownName() );
                    number.readOnly = !pin->IsVisible();
                    name.readOnly = !pin->IsVisible();

                    rows.push_back( { eye, number, eye, name } );
                }

                return rows;
            } );

    // Altium's Add / edit buttons: the Component Pin Editor in the symbol editor
    if( lib )
    {
        pins->AddButtons( { { _( "Add" ),
                              [this]()
                              {
                                  m_frame->GetToolManager()->RunAction( SCH_ACTIONS::placeSymbolPin );
                              } },
                            { _( "Edit..." ),
                              [this]()
                              {
                                  if( SYMBOL_EDIT_FRAME* frame = dynamic_cast<SYMBOL_EDIT_FRAME*>( m_frame ) )
                                      DIALOG_COMPONENT_PIN_EDITOR::Run( frame );
                              } } } );
    }
    else
    {
        pins->AddButtons( { { _( "Edit..." ),
                              [this]()
                              {
                                  m_frame->GetToolManager()->RunAction( SCH_ACTIONS::editWithLibEdit );
                              } } } );
    }
}


// --------------------------------------------------------------------------- net sections (wires)

void SCH_PROPERTIES_PANEL::buildNetSections( const wxString& aLayout )
{
    auto connection =
            [this]() -> SCH_CONNECTION*
            {
                SCH_ITEM*       item = singleItem();
                SCH_EDIT_FRAME* frame = dynamic_cast<SCH_EDIT_FRAME*>( m_frame );

                if( !item || !frame )
                    return nullptr;

                SCH_CONNECTION* conn = item->Connection( &frame->GetCurrentSheet() );

                // A power port's net is its pin's
                if( !conn )
                {
                    if( SCH_SYMBOL* symbol = dynamic_cast<SCH_SYMBOL*>( item ) )
                    {
                        for( SCH_PIN* pin : symbol->GetPins( &frame->GetCurrentSheet() ) )
                        {
                            if( ( conn = pin->Connection( &frame->GetCurrentSheet() ) ) != nullptr )
                                break;
                        }
                    }
                }

                return conn;
            };

    // General (Net)
    PROPERTIES_FORM* net = addLayoutForm( _( "General (Net)" ), aLayout );

    auto netName =
            [connection]() -> wxString
            {
                SCH_CONNECTION* conn = connection();
                return conn ? UnescapeString( conn->Name() ) : wxString();
            };

    // Altium's rows: KiCad has one name per net, so the physical name is the net name, and
    // nets carry no supply voltage, speed or pairing of their own
    net->AddInfo( _( "Physical Name" ), netName );
    net->AddInfo( _( "Net Name" ), netName );
    net->AddInfo( _( "Power Net" ), []() { return wxString( wxS( "0.000 V" ) ); }, true );
    net->AddInfo( _( "High Speed" ), []() { return wxString( wxS( "0.000 Hz" ) ); }, true );
    net->AddInfo( _( "Differential Pair" ), []() { return _( "None" ); } );

    // Parameters (Net): KiCad's nets have a net class, no parameters or rules of their own
    PROPERTIES_FORM* params = addLayoutForm( _( "Parameters (Net)" ), aLayout );

    params->AddCustomSegmented( { _( "All" ), _( "Parameters" ), _( "Rules" ), _( "Classes" ) },
            [this]() { return m_netFilter; },
            [this]( int aFilter )
            {
                m_netFilter = aFilter;
                syncLayoutForms();
            } );

    params->AddTable( { 2, 8, 8 },
            [this]()
            {
                PROPERTIES_FORM::TABLE_ROWS rows;
                rows.push_back( { heading( wxEmptyString ), heading( _( "Name" ) ), heading( _( "Value" ) ) } );

                if( m_netFilter == 0 || m_netFilter == 1 )
                    rows.push_back( { fixed( _( "No Parameters" ) ) } );

                if( m_netFilter == 0 || m_netFilter == 2 )
                    rows.push_back( { fixed( _( "No Rules" ) ) } );

                if( m_netFilter == 0 || m_netFilter == 3 )
                {
                    SCH_ITEM*       item = singleItem();
                    SCH_EDIT_FRAME* frame = dynamic_cast<SCH_EDIT_FRAME*>( m_frame );
                    std::shared_ptr<NETCLASS> nc;

                    if( item && frame )
                        nc = item->GetEffectiveNetClass( &frame->GetCurrentSheet() );

                    if( nc )
                    {
                        CELL value( nc->GetHumanReadableName() );
                        value.link = true;
                        value.click = [this]()
                                      {
                                          m_frame->GetToolManager()->RunAction( SCH_ACTIONS::schematicSetup );
                                      };

                        rows.push_back( { fixed( wxEmptyString ), fixed( _( "Net Class" ) ), value } );
                    }
                    else
                    {
                        rows.push_back( { fixed( _( "No Classes" ) ) } );
                    }
                }

                return rows;
            } );
}


// ----------------------------------------------------------------------------------- power port

void SCH_PROPERTIES_PANEL::buildPowerPortLayout()
{
    const wxString& L = LAYOUT_POWER;

    PROPERTIES_FORM* location = addLayoutForm( _( "Location" ), L );
    location->AddXY( wxS( "Position X" ), wxS( "Position Y" ), wxEmptyString );

    static const std::vector<long> angles = { SYMBOL_ANGLE_0, SYMBOL_ANGLE_90, SYMBOL_ANGLE_180,
                                              SYMBOL_ANGLE_270 };

    location->AddCustomChoice( _( "Rotation" ), rotationNames(),
            [this]() -> int
            {
                long value = propLong( m_grid, wxS( "Orientation" ), LONG_MIN );
                auto it = std::find( angles.begin(), angles.end(), value );
                return it == angles.end() ? -1 : (int) ( it - angles.begin() );
            },
            [this]( int aIdx )
            {
                if( aIdx >= 0 && aIdx < (int) angles.size() )
                    applyPropertyValues( { { wxS( "Orientation" ), wxVariant( angles[aIdx] ) } } );
            } );

    PROPERTIES_FORM* props = addLayoutForm( _( "Properties" ), L );

    // Name, with the eye that shows / hides the net name on the sheet
    OPTS nameEye;
    nameEye.eyeState =
            [this]()
            {
                SCH_SYMBOL* symbol = dynamic_cast<SCH_SYMBOL*>( singleItem() );
                return symbol && symbol->GetField( FIELD_T::VALUE )->IsVisible();
            };
    nameEye.eyeToggle =
            [this, eyeState = nameEye.eyeState]()
            {
                bool show = !eyeState();

                commitSelection( show ? _( "Show Net Name" ) : _( "Hide Net Name" ),
                        [&]( SCH_COMMIT& aCommit, SCH_ITEM* aItem )
                        {
                            if( SCH_SYMBOL* symbol = dynamic_cast<SCH_SYMBOL*>( aItem ) )
                            {
                                aCommit.Modify( symbol, m_frame->GetScreen() );
                                symbol->GetField( FIELD_T::VALUE )->SetVisible( show );
                            }
                        } );
            };

    props->AddText( wxS( "Value" ), _( "Name" ), nameEye );

    // Style: Altium's glyphs (Circle, Arrow, Bar, ...); the port gets a generated power symbol
    props->AddCustomChoice( _( "Style" ), POWER_PORT_STYLE::Names(),
            [this]() -> int
            {
                int style = -2;

                for( SCH_ITEM* item : selectedItems() )
                {
                    if( SCH_SYMBOL* symbol = dynamic_cast<SCH_SYMBOL*>( item ) )
                    {
                        int s = POWER_PORT_STYLE::Get( symbol );
                        style = ( style == -2 || style == s ) ? s : -1;
                    }
                }

                return style < 0 ? -1 : style;
            },
            [this]( int aStyle )
            {
                commitSelection( _( "Change Power Port Style" ),
                        [&]( SCH_COMMIT& aCommit, SCH_ITEM* aItem )
                        {
                            SCH_SYMBOL* symbol = dynamic_cast<SCH_SYMBOL*>( aItem );

                            if( symbol && symbol->IsPower() )
                            {
                                aCommit.Modify( symbol, m_frame->GetScreen() );
                                POWER_PORT_STYLE::Apply( symbol, aStyle );
                            }
                        } );
            } );

    props->AddSeparator();
    addValueFontRows( props );

    buildNetSections( L );
}


void SCH_PROPERTIES_PANEL::addValueFontRows( PROPERTIES_FORM* aForm )
{
    // Altium's Font row for the power port's name: face, size, colour, bold and italic (KiCad
    // text has no underline or strike-through)
    auto valueField =
            [this]() -> SCH_FIELD*
            {
                SCH_SYMBOL* symbol = dynamic_cast<SCH_SYMBOL*>( singleItem() );

                if( !symbol )
                {
                    for( SCH_ITEM* item : selectedItems() )
                    {
                        if( ( symbol = dynamic_cast<SCH_SYMBOL*>( item ) ) != nullptr )
                            break;
                    }
                }

                return symbol ? symbol->GetField( FIELD_T::VALUE ) : nullptr;
            };

    auto editFields =
            [this]( const wxString& aMessage, std::function<void( SCH_FIELD* )> aEdit )
            {
                commitSelection( aMessage,
                        [&]( SCH_COMMIT& aCommit, SCH_ITEM* aItem )
                        {
                            if( SCH_SYMBOL* symbol = dynamic_cast<SCH_SYMBOL*>( aItem ) )
                            {
                                aCommit.Modify( symbol, m_frame->GetScreen() );
                                aEdit( symbol->GetField( FIELD_T::VALUE ) );
                            }
                        } );
            };

    // The font list: KiCad's stroke font, then the system faces
    static std::vector<wxString> faces;

    if( faces.empty() )
    {
        faces.push_back( KICAD_FONT_NAME );

        wxArrayString names = wxFontEnumerator::GetFacenames();
        names.Sort();

        for( const wxString& name : names )
        {
            if( !name.StartsWith( wxS( "@" ) ) )
                faces.push_back( name );
        }
    }

    aForm->AddCustomChoice( _( "Font" ), faces,
            [valueField]() -> int
            {
                SCH_FIELD* field = valueField();

                if( !field )
                    return -1;

                wxString name = field->GetFont() ? field->GetFont()->GetName() : wxString( KICAD_FONT_NAME );
                auto     it = std::find( faces.begin(), faces.end(), name );
                return it == faces.end() ? 0 : (int) ( it - faces.begin() );
            },
            [editFields]( int aIdx )
            {
                if( aIdx < 0 || aIdx >= (int) faces.size() )
                    return;

                wxString face = faces[aIdx];

                editFields( _( "Change Font" ),
                        [&]( SCH_FIELD* aField )
                        {
                            aField->SetFont( KIFONT::FONT::GetFont( aIdx == 0 ? wxString() : face,
                                                                    aField->IsBold(),
                                                                    aField->IsItalic() ) );
                        } );
            } );

    aForm->AddCustomText( _( "Size" ),
            [this, valueField]()
            {
                SCH_FIELD* field = valueField();
                return field ? m_frame->MessageTextFromValue( field->GetTextHeight() ) : wxString();
            },
            [this, editFields]( const wxString& aText )
            {
                int size = m_frame->ValueFromString( aText );

                if( size <= 0 )
                    return;

                editFields( _( "Change Text Size" ),
                        [&]( SCH_FIELD* aField ) { aField->SetTextSize( VECTOR2I( size, size ) ); } );
            } );

    OPTS colourOpts;
    colourOpts.swatchColour =
            [valueField]()
            {
                SCH_FIELD* field = valueField();
                return field && field->GetTextColor() != KIGFX::COLOR4D::UNSPECIFIED
                               ? field->GetTextColor().ToColour()
                               : wxNullColour;
            };

    aForm->AddCustomChoice( _( "Color" ), { _( "Default" ), _( "Custom" ), _( "Choose..." ) },
            [valueField]() -> int
            {
                SCH_FIELD* field = valueField();

                if( !field )
                    return -1;

                return field->GetTextColor() == KIGFX::COLOR4D::UNSPECIFIED ? 0 : 1;
            },
            [this, valueField, editFields]( int aChoice )
            {
                KIGFX::COLOR4D colour = KIGFX::COLOR4D::UNSPECIFIED;

                if( aChoice >= 1 )
                {
                    SCH_FIELD*          field = valueField();
                    DIALOG_COLOR_PICKER dlg( this, field ? field->GetTextColor()
                                                         : KIGFX::COLOR4D::UNSPECIFIED, true );

                    if( dlg.ShowModal() != wxID_OK )
                        return;

                    colour = dlg.GetColor();
                }

                editFields( _( "Change Color" ),
                        [&]( SCH_FIELD* aField ) { aField->SetTextColor( colour ); } );
            },
            colourOpts );

    aForm->AddCustomCheck( _( "Bold" ),
            [valueField]() -> int
            {
                SCH_FIELD* field = valueField();
                return field ? ( field->IsBold() ? 1 : 0 ) : -1;
            },
            [editFields]( bool aOn )
            {
                editFields( _( "Change Bold" ), [&]( SCH_FIELD* aField ) { aField->SetBold( aOn ); } );
            },
            false );

    aForm->AddCustomCheck( _( "Italic" ),
            [valueField]() -> int
            {
                SCH_FIELD* field = valueField();
                return field ? ( field->IsItalic() ? 1 : 0 ) : -1;
            },
            [editFields]( bool aOn )
            {
                editFields( _( "Change Italic" ),
                        [&]( SCH_FIELD* aField ) { aField->SetItalic( aOn ); } );
            },
            false );
}


// ----------------------------------------------------------------------------------- wire / bus

void SCH_PROPERTIES_PANEL::buildWireLayout( const wxString& aLayout )
{
    PROPERTIES_FORM* vertices = nullptr;

    if( aLayout == LAYOUT_LINE )
    {
        // Altium's graphic Line: width, colour and style, then the vertices
        PROPERTIES_FORM* props = addLayoutForm( _( "Properties" ), aLayout );
        props->AddText( wxS( "Line Width" ), _( "Line" ) );
        addColorRow( props, wxS( "Color" ), _( "Color" ) );
        props->AddChoice( wxS( "Line Style" ), _( "Line Style" ) );

        vertices = addLayoutForm( _( "Vertices" ), aLayout );
    }
    else
    {
        buildNetSections( aLayout );

        // Vertices: width and colour, then the points (KiCad's wires and buses are single segments)
        vertices = addLayoutForm( _( "Vertices" ), aLayout );

        vertices->AddText( wxS( "Line Width" ), _( "Width" ) );
        addColorRow( vertices, wxS( "Color" ), _( "Color" ) );
    }

    OPTS single;
    single.visible = [this]() { return selectedItems().size() == 1; };

    auto boundCell =
            []( const wxString& aProp )
            {
                CELL cell;
                cell.prop = aProp;
                return cell;
            };

    vertices->AddTable( { 2, 5, 5 },
            [boundCell]()
            {
                PROPERTIES_FORM::TABLE_ROWS rows;
                rows.push_back( { heading( _( "Index" ) ), heading( wxS( "X" ) ), heading( wxS( "Y" ) ) } );
                rows.push_back( { fixed( wxS( "1" ) ), boundCell( wxS( "Start X" ) ), boundCell( wxS( "Start Y" ) ) } );
                rows.push_back( { fixed( wxS( "2" ) ), boundCell( wxS( "End X" ) ), boundCell( wxS( "End Y" ) ) } );
                return rows;
            },
            &m_vertexRow, single );
}


// ---------------------------------------------------------------- net label / port / off sheet

void SCH_PROPERTIES_PANEL::buildLabelLayout( const wxString& aLayout )
{
    PROPERTIES_FORM* location = addLayoutForm( _( "Location" ), aLayout );
    location->AddXY( PROP_X, PROP_Y, wxEmptyString );

    // Altium's ports have no rotation row (KiCad's label orientation is still on the grid)
    if( aLayout != LAYOUT_PORT )
    {
        location->AddCustomChoice( _( "Rotation" ), rotationNames(),
                [this]() -> int
                {
                    int rotation = -2;

                    for( SCH_ITEM* item : selectedItems() )
                    {
                        int r = textRotation( item );

                        if( rotation != -2 && r != rotation )
                            return -1;

                        rotation = r;
                    }

                    return std::max( rotation, -1 );
                },
                [this]( int aRotation )
                {
                    commitSelection( _( "Rotate" ),
                            [&]( SCH_COMMIT& aCommit, SCH_ITEM* aItem )
                            {
                                if( textRotation( aItem ) != aRotation )
                                {
                                    aCommit.Modify( aItem, m_frame->GetScreen() );
                                    setTextRotation( aItem, aRotation );
                                }
                            } );
                } );
    }

    PROPERTIES_FORM* props = addLayoutForm( _( "Properties" ), aLayout );

    props->AddText( wxS( "Text" ), aLayout == LAYOUT_PORT ? _( "Name" ) : _( "Net Name" ) );

    // A port's I/O Type is KiCad's label shape (Input, Output, Bidirectional, ...)
    if( aLayout == LAYOUT_PORT || aLayout == LAYOUT_OFFSHEET )
        props->AddChoice( wxS( "Shape" ), _( "I/O Type" ) );

    props->AddSeparator();
    addFontRows( props );

    if( aLayout == LAYOUT_PORT )
    {
        buildNetSections( aLayout );
        buildParametersSection( aLayout, 1, _( "Parameters" ) );
    }
    else if( aLayout == LAYOUT_OFFSHEET )
    {
        buildNetSections( aLayout );
    }
}


// ---------------------------------------------------------------------------------- sheet symbol

void SCH_PROPERTIES_PANEL::buildSheetLayout()
{
    const wxString& L = LAYOUT_SHEET;

    PROPERTIES_FORM* location = addLayoutForm( _( "Location" ), L );
    location->AddXY( PROP_X, PROP_Y, wxEmptyString );

    PROPERTIES_FORM* props = addLayoutForm( _( "Properties" ), L );

    auto sheetEye =
            [this]( FIELD_T aField )
            {
                OPTS opts;
                opts.linkLabel = true;
                opts.eyeState =
                        [this, aField]()
                        {
                            SCH_SHEET* sheet = dynamic_cast<SCH_SHEET*>( singleItem() );
                            return sheet && sheet->GetField( aField )->IsVisible();
                        };
                opts.eyeToggle =
                        [this, aField, eyeState = opts.eyeState]()
                        {
                            bool show = !eyeState();

                            commitSelection( show ? _( "Show Text" ) : _( "Hide Text" ),
                                    [&]( SCH_COMMIT& aCommit, SCH_ITEM* aItem )
                                    {
                                        if( SCH_SHEET* sheet = dynamic_cast<SCH_SHEET*>( aItem ) )
                                        {
                                            aCommit.Modify( sheet, m_frame->GetScreen() );
                                            sheet->GetField( aField )->SetVisible( show );
                                        }
                                    } );
                        };
                return opts;
            };

    props->AddText( wxS( "Sheet Name" ), _( "Designator" ), sheetEye( FIELD_T::SHEET_NAME ) );
    props->AddInfo( _( "File Name" ),
            [this]() -> wxString
            {
                SCH_SHEET* sheet = dynamic_cast<SCH_SHEET*>( singleItem() );
                return sheet ? sheet->GetFileName() : wxString();
            },
            false, sheetEye( FIELD_T::SHEET_FILENAME ) );
    props->AddTwin( PROP_SHEET_WIDTH, _( "Width" ), PROP_SHEET_HEIGHT, _( "Height" ) );
    props->AddText( wxS( "Border Width" ), _( "Line Style" ) );
    addColorRow( props, wxS( "Border Color" ), _( "Line Color" ) );
    addColorRow( props, wxS( "Background Color" ), _( "Fill Color" ) );

    // Source: the sheet's file
    PROPERTIES_FORM* source = addLayoutForm( _( "Source" ), L );
    source->AddText( GetCanonicalFieldName( FIELD_T::SHEET_FILENAME ), _( "File Name" ) );

    // Sheet Entries
    PROPERTIES_FORM* entries = addLayoutForm( _( "Sheet Entries" ), L );

    entries->AddTable( { 5, 5 },
            [this]()
            {
                PROPERTIES_FORM::TABLE_ROWS rows;
                rows.push_back( { heading( _( "Name" ) ), heading( _( "PortIO" ) ) } );

                SCH_SHEET* sheet = dynamic_cast<SCH_SHEET*>( singleItem() );

                if( !sheet )
                    return rows;

                for( SCH_SHEET_PIN* pin : sheet->GetPins() )
                {
                    wxString io = ENUM_MAP<LABEL_SHAPE>::Instance().ToString( pin->GetLabelShape() );
                    rows.push_back( { CELL( pin->GetShownText( false ) ), CELL( wxGetTranslation( io ) ) } );
                }

                return rows;
            } );

    entries->AddButtons( { { _( "Add" ),
                             [this]()
                             {
                                 m_frame->GetToolManager()->RunAction( SCH_ACTIONS::placeSheetPin );
                             } } } );

    buildParametersSection( L, 1, _( "Parameters" ) );
}


void SCH_PROPERTIES_PANEL::buildSheetEntryLayout()
{
    const wxString& L = LAYOUT_ENTRY;

    PROPERTIES_FORM* props = addLayoutForm( _( "Properties" ), L );

    props->AddText( wxS( "Text" ), _( "Name" ) );
    props->AddChoice( wxS( "Shape" ), _( "I/O Type" ) );
    props->AddSeparator();
    addFontRows( props );

    buildNetSections( L );
}


// ------------------------------------------------------------------------------ text / text frame

void SCH_PROPERTIES_PANEL::buildTextLayout()
{
    const wxString& L = LAYOUT_TEXT;

    PROPERTIES_FORM* location = addLayoutForm( _( "Location" ), L );
    location->AddXY( PROP_X, PROP_Y, wxEmptyString );

    location->AddCustomChoice( _( "Rotation" ), rotationNames(),
            [this]() -> int
            {
                int rotation = -2;

                for( SCH_ITEM* item : selectedItems() )
                {
                    int r = textRotation( item );

                    if( rotation != -2 && r != rotation )
                        return -1;

                    rotation = r;
                }

                return std::max( rotation, -1 );
            },
            [this]( int aRotation )
            {
                commitSelection( _( "Rotate" ),
                        [&]( SCH_COMMIT& aCommit, SCH_ITEM* aItem )
                        {
                            if( textRotation( aItem ) != aRotation )
                            {
                                aCommit.Modify( aItem, m_frame->GetScreen() );
                                setTextRotation( aItem, aRotation );
                            }
                        } );
            } );

    PROPERTIES_FORM* props = addLayoutForm( _( "Properties" ), L );

    OPTS multi;
    multi.lines = 3;
    props->AddText( wxS( "Text" ), _( "Text" ), multi );
    props->AddSeparator();
    addFontRows( props );
    props->AddSegmented( wxS( "Horizontal Justification" ), { _( "Left" ), _( "Center" ), _( "Right" ) } );
    props->AddSegmented( wxS( "Vertical Justification" ), { _( "Top" ), _( "Center" ), _( "Bottom" ) } );
    props->AddText( wxS( "Hyperlink" ), _( "URL" ) );
}


void SCH_PROPERTIES_PANEL::buildTextFrameLayout()
{
    const wxString& L = LAYOUT_TEXTFRAME;

    PROPERTIES_FORM* location = addLayoutForm( _( "Location" ), L );
    location->AddXY( wxS( "Start X" ), wxS( "Start Y" ), wxEmptyString );

    PROPERTIES_FORM* props = addLayoutForm( _( "Properties" ), L );

    OPTS multi;
    multi.lines = 4;
    props->AddText( wxS( "Text" ), _( "Text" ), multi );
    props->AddSeparator();
    props->AddChoice( wxS( "Font" ), _( "Font" ) );
    props->AddText( wxS( "Text Size" ), _( "Size" ) );
    addColorRow( props, wxS( "Color" ), _( "Color" ) );
    props->AddCheck( wxS( "Bold" ), _( "Bold" ), true );
    props->AddCheck( wxS( "Italic" ), _( "Italic" ), true );
    props->AddSegmented( wxS( "Horizontal Justification" ), { _( "Left" ), _( "Center" ), _( "Right" ) } );
    props->AddSeparator();
    props->AddText( wxS( "Margin Left" ), _( "Text Margin" ) );
    props->AddTwin( wxS( "Width" ), _( "Width" ), wxS( "Height" ), _( "Height" ) );
    props->AddText( wxS( "Line Width" ), _( "Border" ) );
    addColorRow( props, wxS( "Line Color" ), _( "Border Color" ) );
    props->AddChoice( wxS( "Fill" ), _( "Fill" ) );
    addColorRow( props, wxS( "Fill Color" ), _( "Fill Color" ) );
}


// --------------------------------------------------------------------------------- rectangle

void SCH_PROPERTIES_PANEL::buildRectangleLayout()
{
    const wxString& L = LAYOUT_RECTANGLE;

    PROPERTIES_FORM* location = addLayoutForm( _( "Location" ), L );
    location->AddXY( wxS( "Start X" ), wxS( "Start Y" ), wxEmptyString );

    PROPERTIES_FORM* props = addLayoutForm( _( "Properties" ), L );
    props->AddText( wxS( "Width" ), _( "Width" ) );
    props->AddText( wxS( "Height" ), _( "Height" ) );

    auto shapes =
            [this]()
            {
                std::vector<SCH_SHAPE*> list;

                for( SCH_ITEM* item : selectedItems() )
                {
                    if( item->Type() == SCH_SHAPE_T )
                        list.push_back( static_cast<SCH_SHAPE*>( item ) );
                }

                return list;
            };

    auto editShapes =
            [this]( const wxString& aMessage, std::function<void( SCH_SHAPE* )> aEdit )
            {
                commitSelection( aMessage,
                        [&]( SCH_COMMIT& aCommit, SCH_ITEM* aItem )
                        {
                            if( aItem->Type() != SCH_SHAPE_T )
                                return;

                            aCommit.Modify( aItem, m_frame->GetScreen() );
                            aEdit( static_cast<SCH_SHAPE*>( aItem ) );
                        } );
            };

    // Altium's border widths; Smallest is the default (thinnest) line
    static const std::vector<int> borderMils = { 0, 10, 30, 50 };

    props->AddCustomChoice( _( "Border" ), { _( "Smallest" ), _( "Small" ), _( "Medium" ), _( "Large" ) },
            [shapes]() -> int
            {
                std::vector<SCH_SHAPE*> list = shapes();

                if( list.empty() )
                    return -1;

                int    width = list.front()->GetStroke().GetWidth();
                int    best = 0;
                double bestDiff = 1e18;

                for( size_t ii = 0; ii < borderMils.size(); ++ii )
                {
                    double diff = std::abs( width - schIUScale.MilsToIU( borderMils[ii] ) );

                    if( diff < bestDiff )
                    {
                        bestDiff = diff;
                        best = (int) ii;
                    }
                }

                return best;
            },
            [editShapes]( int aChoice )
            {
                if( aChoice < 0 || aChoice >= (int) borderMils.size() )
                    return;

                editShapes( _( "Change Border" ),
                        [&]( SCH_SHAPE* aShape )
                        {
                            STROKE_PARAMS stroke = aShape->GetStroke();
                            stroke.SetWidth( schIUScale.MilsToIU( borderMils[aChoice] ) );
                            aShape->SetStroke( stroke );
                        } );
            } );

    addColorRow( props, wxS( "Line Color" ), _( "Border Color" ) );
    props->AddChoice( wxS( "Line Style" ), _( "Line Style" ) );
    props->AddSeparator();

    auto filled =
            []( SCH_SHAPE* aShape )
            {
                return aShape->GetFillMode() != FILL_T::NO_FILL;
            };

    props->AddCustomCheck( _( "Fill Color" ),
            [shapes, filled]() -> int
            {
                std::vector<SCH_SHAPE*> list = shapes();
                return list.empty() ? -1 : ( filled( list.front() ) ? 1 : 0 );
            },
            [editShapes, this]( bool aOn )
            {
                bool lib = m_frame->IsType( FRAME_SCH_SYMBOL_EDITOR );

                editShapes( aOn ? _( "Fill" ) : _( "No Fill" ),
                        [&]( SCH_SHAPE* aShape )
                        {
                            if( !aOn )
                                aShape->SetFillMode( FILL_T::NO_FILL );
                            else if( aShape->GetFillColor() != KIGFX::COLOR4D::UNSPECIFIED || !lib )
                                aShape->SetFillMode( FILL_T::FILLED_WITH_COLOR );
                            else
                                aShape->SetFillMode( FILL_T::FILLED_WITH_BG_BODYCOLOR );
                        } );
            },
            false );

    OPTS fillOpts;
    fillOpts.visible = [shapes, filled]() { return !shapes().empty() && filled( shapes().front() ); };
    fillOpts.swatchColour =
            [shapes]()
            {
                std::vector<SCH_SHAPE*> list = shapes();

                if( list.empty() || list.front()->GetFillMode() != FILL_T::FILLED_WITH_COLOR )
                    return wxNullColour;

                return list.front()->GetFillColor().WithAlpha( 1.0 ).ToColour();
            };

    props->AddCustomChoice( _( "Fill" ), { _( "Body Color" ), _( "Custom" ), _( "Choose..." ) },
            [shapes]() -> int
            {
                std::vector<SCH_SHAPE*> list = shapes();

                if( list.empty() )
                    return -1;

                return list.front()->GetFillMode() == FILL_T::FILLED_WITH_COLOR ? 1 : 0;
            },
            [this, shapes, editShapes]( int aChoice )
            {
                KIGFX::COLOR4D colour = KIGFX::COLOR4D::UNSPECIFIED;

                if( aChoice >= 1 )
                {
                    std::vector<SCH_SHAPE*> list = shapes();
                    DIALOG_COLOR_PICKER dlg( this, list.empty() ? KIGFX::COLOR4D::UNSPECIFIED
                                                                : list.front()->GetFillColor(), true );

                    if( dlg.ShowModal() != wxID_OK )
                        return;

                    colour = dlg.GetColor();
                }

                editShapes( _( "Change Fill" ),
                        [&]( SCH_SHAPE* aShape )
                        {
                            if( colour == KIGFX::COLOR4D::UNSPECIFIED )
                            {
                                aShape->SetFillMode( FILL_T::FILLED_WITH_BG_BODYCOLOR );
                            }
                            else
                            {
                                aShape->SetFillMode( FILL_T::FILLED_WITH_COLOR );
                                aShape->SetFillColor( colour );
                            }
                        } );
            },
            fillOpts );

    // Transparent: the fill drawn half see-through
    props->AddCustomCheck( _( "Transparent" ),
            [shapes, filled]() -> int
            {
                std::vector<SCH_SHAPE*> list = shapes();

                if( list.empty() || !filled( list.front() ) )
                    return 0;

                return list.front()->GetFillMode() == FILL_T::FILLED_WITH_COLOR
                               && list.front()->GetFillColor().a < 1.0 ? 1 : 0;
            },
            [this, editShapes]( bool aOn )
            {
                // The body colour of the theme, when the fill has none of its own
                KIGFX::COLOR4D body = m_frame->GetColorSettings()->GetColor( LAYER_DEVICE_BACKGROUND );

                editShapes( aOn ? _( "Transparent Fill" ) : _( "Opaque Fill" ),
                        [&]( SCH_SHAPE* aShape )
                        {
                            if( aShape->GetFillMode() == FILL_T::NO_FILL )
                                return;

                            KIGFX::COLOR4D colour = aShape->GetFillMode() == FILL_T::FILLED_WITH_COLOR
                                                            ? aShape->GetFillColor()
                                                            : body;

                            aShape->SetFillMode( FILL_T::FILLED_WITH_COLOR );
                            aShape->SetFillColor( colour.WithAlpha( aOn ? 0.5 : 1.0 ) );
                        } );
            },
            false );
}


// --------------------------------------------------------------------------------- no ERC / junction

void SCH_PROPERTIES_PANEL::buildNoErcLayout()
{
    const wxString& L = LAYOUT_NOERC;

    PROPERTIES_FORM* location = addLayoutForm( _( "Location" ), L );
    location->AddXY( PROP_X, PROP_Y, wxEmptyString );

    // KiCad's no-connect flag: a cross that marks a pin as intentionally unconnected
    PROPERTIES_FORM* props = addLayoutForm( _( "Properties" ), L );
    props->AddInfo( _( "Symbol" ), []() { return _( "Cross" ); }, true );

    PROPERTIES_FORM* suppressed = addLayoutForm( _( "Suppressed Violations" ), L );
    suppressed->AddCustomSegmented( { _( "All Violations" ), _( "Specific Violations" ) },
                                    []() { return 1; }, []( int ) {}, { false, true } );
    suppressed->AddInfo( _( "Violation" ), []() { return _( "Unconnected pin" ); } );
}


void SCH_PROPERTIES_PANEL::buildJunctionLayout()
{
    const wxString& L = LAYOUT_JUNCTION;

    PROPERTIES_FORM* location = addLayoutForm( _( "Location" ), L );
    location->AddXY( PROP_X, PROP_Y, wxEmptyString );

    PROPERTIES_FORM* props = addLayoutForm( _( "Properties" ), L );
    props->AddText( wxS( "Diameter" ), _( "Size" ) );
    addColorRow( props, wxS( "Color" ), _( "Color" ) );
    props->AddCheck( wxS( "Locked" ), _( "Locked" ), true );
}


// --------------------------------------------------------------------------------- pin (SchLib)

void SCH_PROPERTIES_PANEL::buildPinLayout()
{
    const wxString& L = LAYOUT_PIN;

    PROPERTIES_FORM* location = addLayoutForm( _( "Location" ), L );
    location->AddXY( wxS( "Position X" ), wxS( "Position Y" ), wxEmptyString );

    // Altium's 0 degrees has the electrical end on the right (KiCad's "Left": the pin extends
    // leftwards from its connection point)
    static const std::vector<long> orientations = { (long) PIN_ORIENTATION::PIN_LEFT,
                                                    (long) PIN_ORIENTATION::PIN_DOWN,
                                                    (long) PIN_ORIENTATION::PIN_RIGHT,
                                                    (long) PIN_ORIENTATION::PIN_UP };

    location->AddCustomChoice( _( "Rotation" ), rotationNames(),
            [this]() -> int
            {
                long value = propLong( m_grid, wxS( "Orientation" ), LONG_MIN );
                auto it = std::find( orientations.begin(), orientations.end(), value );
                return it == orientations.end() ? -1 : (int) ( it - orientations.begin() );
            },
            [this]( int aIdx )
            {
                if( aIdx >= 0 && aIdx < (int) orientations.size() )
                    applyPropertyValues( { { wxS( "Orientation" ), wxVariant( orientations[aIdx] ) } } );
            } );

    PROPERTIES_FORM* props = addLayoutForm( _( "Properties" ), L );

    props->AddText( wxS( "Pin Number" ), _( "Designator" ) );
    props->AddText( wxS( "Pin Name" ), _( "Name" ) );
    props->AddChoice( wxS( "Electrical Type" ), _( "Electrical Type" ) );
    props->AddChoice( wxS( "Unit" ), _( "Part Number" ) );

    // Altium's pin illustration: the body on the left, the pin, its name inside the body
    props->AddPaint( 100,
            [this]( wxDC& aDC, const wxRect& aRect )
            {
                SCH_PIN* pin = dynamic_cast<SCH_PIN*>( singleItem() );
                wxRect   area = aRect.Deflate( FromDIP( 8 ), FromDIP( 6 ) );
                wxColour bg = PROPERTIES_FORM::Mix( GetBackgroundColour(), *wxBLACK, 0.12 );

                aDC.SetPen( *wxTRANSPARENT_PEN );
                aDC.SetBrush( wxBrush( bg ) );
                aDC.DrawRectangle( area );

                int    bodyW = area.width / 3;
                int    bodyH = area.height * 3 / 5;
                wxRect body( area.x + area.width / 4, area.y + ( area.height - bodyH ) / 2, bodyW, bodyH );

                aDC.SetBrush( wxBrush( wxColour( 0x80, 0x80, 0x80 ) ) );
                aDC.DrawRectangle( body );

                aDC.SetPen( wxPen( wxColour( 0x5A, 0x7C, 0xC8 ), FromDIP( 3 ) ) );
                int y = body.y + body.height * 2 / 3;
                aDC.DrawLine( body.GetRight(), y, body.GetRight() + bodyW, y );

                if( pin )
                {
                    aDC.SetFont( GetFont().Larger() );
                    aDC.SetTextForeground( wxColour( 0xE0, 0xE0, 0xE0 ) );
                    wxString name = pin->GetShownName();
                    wxSize   ext = aDC.GetTextExtent( name );
                    aDC.DrawText( name, body.x + ( body.width - ext.x ) / 2, body.y + ( body.height - ext.y ) / 2 );
                    aDC.SetFont( GetFont() );
                }
            } );

    props->AddText( wxS( "Length" ), _( "Pin Length" ) );
    props->AddCheck( wxS( "Visible" ), _( "Visible" ), true );

    // Symbols: Altium splits KiCad's graphic style into the inside edge (clock) and outside edge
    // (dot, active-low input / output) symbols
    PROPERTIES_FORM* symbols = addLayoutForm( _( "Symbols" ), L );

    using GPS = GRAPHIC_PINSHAPE;

    // (inside edge, outside edge) for each KiCad shape; -1 = not expressible
    auto split =
            []( long aShape ) -> std::pair<int, int>
            {
                switch( static_cast<GPS>( aShape ) )
                {
                case GPS::LINE:           return { 0, 0 };
                case GPS::INVERTED:       return { 0, 1 };
                case GPS::CLOCK:          return { 1, 0 };
                case GPS::INVERTED_CLOCK: return { 1, 1 };
                case GPS::INPUT_LOW:      return { 0, 2 };
                case GPS::CLOCK_LOW:      return { 1, 2 };
                case GPS::OUTPUT_LOW:     return { 0, 3 };
                default:                  return { -1, -1 };
                }
            };

    auto join =
            []( int aInside, int aOutside ) -> long
            {
                static const GPS shapes[2][4] = {
                    { GPS::LINE, GPS::INVERTED, GPS::INPUT_LOW, GPS::OUTPUT_LOW },
                    { GPS::CLOCK, GPS::INVERTED_CLOCK, GPS::CLOCK_LOW, GPS::LAST_OPTION } };

                if( aInside < 0 || aInside > 1 || aOutside < 0 || aOutside > 3 )
                    return -1;

                // KiCad has no clock with an active-low output
                if( aInside == 1 && aOutside == 3 )
                    return -1;

                return (long) shapes[aInside][aOutside];
            };

    symbols->AddCustomChoice( _( "Inside Edge" ), { _( "No Symbol" ), _( "Clock" ) },
            [this, split]() { return split( propLong( m_grid, wxS( "Graphic Style" ), -1 ) ).first; },
            [this, split, join]( int aInside )
            {
                std::pair<int, int> cur = split( propLong( m_grid, wxS( "Graphic Style" ), -1 ) );
                long shape = join( aInside, std::max( cur.second, 0 ) );

                if( shape >= 0 )
                    applyPropertyValues( { { wxS( "Graphic Style" ), wxVariant( shape ) } } );
            } );

    symbols->AddCustomChoice( _( "Outside Edge" ),
            { _( "No Symbol" ), _( "Dot" ), _( "Active Low Input" ), _( "Active Low Output" ) },
            [this, split]() { return split( propLong( m_grid, wxS( "Graphic Style" ), -1 ) ).second; },
            [this, split, join]( int aOutside )
            {
                std::pair<int, int> cur = split( propLong( m_grid, wxS( "Graphic Style" ), -1 ) );
                long shape = join( std::max( cur.first, 0 ), aOutside );

                if( shape >= 0 )
                    applyPropertyValues( { { wxS( "Graphic Style" ), wxVariant( shape ) } } );
            } );

    PROPERTIES_FORM* font = addLayoutForm( _( "Font Settings" ), L );
    font->AddText( wxS( "Number Text Size" ), _( "Designator" ) );
    font->AddText( wxS( "Name Text Size" ), _( "Name" ) );

    // Parameters tab: KiCad's pins have none
    PROPERTIES_FORM* params = addLayoutForm( _( "Parameters" ), L, 1 );
    params->AddTable( { 1, 1 },
            []()
            {
                PROPERTIES_FORM::TABLE_ROWS rows;
                rows.push_back( { heading( _( "Name" ) ), heading( _( "Value" ) ) } );
                rows.push_back( { fixed( _( "No Parameters" ) ) } );
                return rows;
            } );
}


// ----------------------------------------------------------------------------- document options

void SCH_PROPERTIES_PANEL::buildDocumentLayout()
{
    const wxString& L = LAYOUT_DOCUMENT;

    auto drawFrame = [this]() { return static_cast<EDA_DRAW_FRAME*>( m_frame ); };

    auto docChanged =
            [this]( const wxString& aName, const wxVariant& aValue )
            {
                documentValueChanged( aName, aValue );
                CallAfter( [this]() { UpdateData(); } );
            };

    // General
    PROPERTIES_FORM* general = addLayoutForm( _( "General" ), L );

    general->AddTitle( _( "Units" ) );
    general->AddCustomSegmented( { _( "mm" ), _( "mils" ) },
            [this]() -> int
            {
                switch( m_frame->GetUserUnits() )
                {
                case EDA_UNITS::MM:   return 0;
                case EDA_UNITS::MILS: return 1;
                default:              return -1;
                }
            },
            [docChanged]( int aIdx ) { docChanged( wxS( "doc.units" ), wxVariant( (long) aIdx ) ); } );

    general->AddSeparator();

    auto gridNames =
            [this]()
            {
                std::vector<wxString> names;
                EDA_DRAW_FRAME*       frame = static_cast<EDA_DRAW_FRAME*>( m_frame );

                if( APP_SETTINGS_BASE* cfg = frame->config() )
                {
                    if( WINDOW_SETTINGS* window = frame->GetWindowSettings( cfg ) )
                    {
                        wxArrayString grids;
                        GRID_MENU::BuildChoiceList( &grids, window, frame );

                        for( const wxString& grid : grids )
                            names.push_back( grid );
                    }
                }

                return names;
            };

    OPTS visibleGrid;
    visibleGrid.choices = gridNames;
    visibleGrid.eyeState = [drawFrame]() { return drawFrame()->IsGridVisible(); };
    visibleGrid.eyeToggle =
            [drawFrame, docChanged]()
            {
                docChanged( wxS( "doc.gridVisible" ), wxVariant( !drawFrame()->IsGridVisible() ) );
            };

    general->AddCustomChoice( _( "Visible Grid" ), {},
            [drawFrame]() -> int
            {
                EDA_DRAW_FRAME* frame = drawFrame();

                if( APP_SETTINGS_BASE* cfg = frame->config() )
                {
                    if( WINDOW_SETTINGS* window = frame->GetWindowSettings( cfg ) )
                        return window->grid.last_size_idx;
                }

                return -1;
            },
            [docChanged]( int aIdx ) { docChanged( wxS( "doc.grid" ), wxVariant( (long) aIdx ) ); },
            visibleGrid );

    general->AddCustomCheck( _( "Snap Grid" ),
            [drawFrame]() -> int
            {
                return drawFrame()->GetGalDisplayOptions().m_gridSnapping
                               != KIGFX::GRID_SNAPPING::NEVER ? 1 : 0;
            },
            [docChanged]( bool aOn )
            {
                docChanged( wxS( "doc.gridSnap" ),
                            wxVariant( (long) ( aOn ? KIGFX::GRID_SNAPPING::ALWAYS
                                                    : KIGFX::GRID_SNAPPING::NEVER ) ) );
            },
            false );

    // Library Options (symbol editor) stop here: the rest is about the schematic sheet
    if( !m_frame->IsType( FRAME_SCH ) )
        return;

    general->AddInfo( _( "Document Font" ),
            [this]() -> wxString
            {
                SCH_EDIT_FRAME* frame = dynamic_cast<SCH_EDIT_FRAME*>( m_frame );

                if( !frame )
                    return wxEmptyString;

                return wxString::Format( _( "Default font, %s" ),
                        m_frame->MessageTextFromValue( frame->Schematic().Settings().m_DefaultTextSize ) );
            } );

    // Sheet Border: KiCad's page limits; Sheet Color: the colour theme's background
    general->AddCustomCheck( _( "Sheet Border" ),
            [this]() -> int
            {
                EESCHEMA_SETTINGS* cfg = m_frame->eeconfig();
                return cfg && cfg->m_Appearance.show_page_limits ? 1 : 0;
            },
            [this]( bool aOn )
            {
                if( EESCHEMA_SETTINGS* cfg = m_frame->eeconfig() )
                {
                    cfg->m_Appearance.show_page_limits = aOn;
                    m_frame->HardRedraw();
                    CallAfter( [this]() { UpdateData(); } );
                }
            },
            false );

    OPTS sheetColor;
    sheetColor.readOnly = true;
    sheetColor.swatchColour =
            [this]()
            {
                COLOR_SETTINGS* cs = m_frame->IsClosing() ? nullptr : m_frame->GetColorSettings();
                return cs ? cs->GetColor( LAYER_SCHEMATIC_BACKGROUND ).ToColour() : wxNullColour;
            };

    general->AddCustomChoice( _( "Sheet Color" ), { _( "Color theme" ) }, []() { return 0; }, []( int ) {},
                              sheetColor );

    // Page Options
    PROPERTIES_FORM* page = addLayoutForm( _( "Page Options" ), L );

    auto pageInfo =
            [this]()
            {
                return m_frame->GetScreen() ? m_frame->GetScreen()->GetPageSettings() : PAGE_INFO();
            };

    auto setPage =
            [this]( const PAGE_INFO& aPage )
            {
                m_frame->SetPageSettings( aPage );
                m_frame->OnModify();
                m_frame->HardRedraw();
                CallAfter( [this]() { UpdateData(); } );
            };

    page->AddTitle( _( "Formatting and Size" ) );
    page->AddCustomSegmented( { _( "Standard" ), _( "Custom" ) },
            [pageInfo]() { return pageInfo().IsCustom() ? 1 : 0; },
            [this, pageInfo, setPage]( int aIdx )
            {
                PAGE_INFO info = pageInfo();

                if( aIdx == 1 && !info.IsCustom() )
                {
                    // Custom: the page settings dialog has the width and height
                    m_frame->GetToolManager()->RunAction( ACTIONS::pageSettings );
                    CallAfter( [this]() { UpdateData(); } );
                }
                else if( aIdx == 0 && info.IsCustom() )
                {
                    info.SetType( PAGE_SIZE_TYPE::A4 );
                    setPage( info );
                }
            } );

    page->AddInfo( _( "Template" ),
            [this]() -> wxString
            {
                SCH_EDIT_FRAME* frame = dynamic_cast<SCH_EDIT_FRAME*>( m_frame );

                if( !frame )
                    return wxEmptyString;

                wxString file = frame->Schematic().Settings().m_SchDrawingSheetFileName;
                return file.IsEmpty() ? _( "Default" ) : wxFileName( file ).GetFullName();
            } );

    auto standardNames =
            []()
            {
                std::vector<wxString> names;

                for( const PAGE_INFO& info : PAGE_INFO::GetPageFormatsList() )
                {
                    if( !info.IsCustom() )
                        names.push_back( info.GetPageFormatDescription() );
                }

                return names;
            };

    OPTS standardOnly;
    standardOnly.visible = [pageInfo]() { return !pageInfo().IsCustom(); };
    standardOnly.choices = standardNames;

    page->AddCustomChoice( _( "Sheet Size" ), {},
            [pageInfo]() -> int
            {
                PAGE_INFO info = pageInfo();
                int       idx = 0;

                for( const PAGE_INFO& format : PAGE_INFO::GetPageFormatsList() )
                {
                    if( format.IsCustom() )
                        continue;

                    if( format.GetType() == info.GetType() )
                        return idx;

                    idx++;
                }

                return -1;
            },
            [pageInfo, setPage]( int aIdx )
            {
                int idx = 0;

                for( const PAGE_INFO& format : PAGE_INFO::GetPageFormatsList() )
                {
                    if( format.IsCustom() )
                        continue;

                    if( idx++ == aIdx )
                    {
                        PAGE_INFO info = pageInfo();
                        info.SetType( format.GetType(), info.IsPortrait() );
                        setPage( info );
                        return;
                    }
                }
            },
            standardOnly );

    OPTS notCustom;
    notCustom.visible = [pageInfo]() { return !pageInfo().IsCustom(); };

    page->AddCustomChoice( _( "Orientation" ), { _( "Landscape" ), _( "Portrait" ) },
            [pageInfo]() { return pageInfo().IsPortrait() ? 1 : 0; },
            [pageInfo, setPage]( int aIdx )
            {
                PAGE_INFO info = pageInfo();
                info.SetPortrait( aIdx == 1 );
                setPage( info );
            },
            notCustom );

    page->AddInfo( _( "Width" ),
            [this, pageInfo]()
            {
                return m_frame->MessageTextFromValue( pageInfo().GetWidthIU( schIUScale.IU_PER_MILS ) );
            } );
    page->AddInfo( _( "Height" ),
            [this, pageInfo]()
            {
                return m_frame->MessageTextFromValue( pageInfo().GetHeightIU( schIUScale.IU_PER_MILS ) );
            } );

    page->AddButtons( { { _( "Page Setup..." ),
                          [this]()
                          {
                              m_frame->GetToolManager()->RunAction( ACTIONS::pageSettings );
                              UpdateData();
                          } } } );

    // Source: the sheet's file and its place in the hierarchy
    PROPERTIES_FORM* source = addLayoutForm( _( "Source" ), L );

    source->AddInfo( _( "File Name" ),
            [this]() -> wxString
            {
                SCH_SCREEN* screen = m_frame->GetScreen();
                return screen ? wxFileName( screen->GetFileName() ).GetFullName() : wxString();
            },
            true );
    source->AddInfo( _( "Sheet Path" ),
            [this]() -> wxString
            {
                SCH_EDIT_FRAME* frame = dynamic_cast<SCH_EDIT_FRAME*>( m_frame );
                return frame ? frame->GetCurrentSheet().PathHumanReadable( false ) : wxString();
            },
            true );

    // Parameters: the sheet's title block and the project's text variables
    PROPERTIES_FORM* params = addLayoutForm( _( "Parameters" ), L, 1 );

    // All | Parameters | Rules (KiCad has no document rules)
    params->AddCustomSegmented( { _( "All" ), _( "Parameters" ), _( "Rules" ) },
                                [this]() { return m_docParamFilter; },
                                [this]( int aIdx )
                                {
                                    m_docParamFilter = aIdx;
                                    syncLayoutForms();
                                } );

    struct TB_ENTRY
    {
        wxString                                             name;
        std::function<wxString( const TITLE_BLOCK& )>        get;
        std::function<void( TITLE_BLOCK&, const wxString& )> set;
    };

    static std::vector<TB_ENTRY> entries;

    if( entries.empty() )
    {
        entries.push_back( { wxS( "Title" ), []( const TITLE_BLOCK& tb ) { return tb.GetTitle(); },
                             []( TITLE_BLOCK& tb, const wxString& v ) { tb.SetTitle( v ); } } );
        entries.push_back( { wxS( "Date" ), []( const TITLE_BLOCK& tb ) { return tb.GetDate(); },
                             []( TITLE_BLOCK& tb, const wxString& v ) { tb.SetDate( v ); } } );
        entries.push_back( { wxS( "Revision" ), []( const TITLE_BLOCK& tb ) { return tb.GetRevision(); },
                             []( TITLE_BLOCK& tb, const wxString& v ) { tb.SetRevision( v ); } } );
        entries.push_back( { wxS( "Company" ), []( const TITLE_BLOCK& tb ) { return tb.GetCompany(); },
                             []( TITLE_BLOCK& tb, const wxString& v ) { tb.SetCompany( v ); } } );

        for( int ii = 0; ii < 9; ++ii )
        {
            entries.push_back( { wxString::Format( wxS( "Comment%d" ), ii + 1 ),
                                 [ii]( const TITLE_BLOCK& tb ) { return tb.GetComment( ii ); },
                                 [ii]( TITLE_BLOCK& tb, const wxString& v ) { tb.SetComment( ii, v ); } } );
        }
    }

    params->AddTable( { 4, 6 },
            [this]()
            {
                PROPERTIES_FORM::TABLE_ROWS rows;
                rows.push_back( { heading( _( "Name" ) ), heading( _( "Value" ) ) } );

                if( !m_frame->GetScreen() )
                    return rows;

                if( m_docParamFilter == 2 )
                {
                    rows.push_back( { fixed( _( "No Rules" ) ) } );
                    return rows;
                }

                TITLE_BLOCK tb = m_frame->GetScreen()->GetTitleBlock();

                for( const TB_ENTRY& entry : entries )
                {
                    CELL value( entry.get( tb ) );
                    value.edit =
                            [this, entry]( const wxString& aText )
                            {
                                TITLE_BLOCK block = m_frame->GetScreen()->GetTitleBlock();
                                entry.set( block, aText );
                                m_frame->SetTitleBlock( block );
                                m_frame->OnModify();
                                m_frame->GetCanvas()->Refresh();
                                CallAfter( [this]() { UpdateData(); } );
                            };

                    rows.push_back( { CELL( entry.name ), value } );
                }

                // The project's text variables
                for( const auto& [name, text] : m_frame->Prj().GetTextVars() )
                {
                    CELL value( text );
                    wxString varName = name;
                    value.edit =
                            [this, varName]( const wxString& aText )
                            {
                                m_frame->Prj().GetTextVars()[varName] = aText;
                                m_frame->Prj().IncrementTextVarsTicker();
                                m_frame->OnModify();
                                m_frame->HardRedraw();
                                CallAfter( [this]() { UpdateData(); } );
                            };

                    rows.push_back( { CELL( name ), value } );
                }

                return rows;
            },
            &m_docParamRow );

    params->AddButtons( {
            { _( "Add..." ),
              [this]()
              {
                  wxTextEntryDialog dlg( this, _( "Parameter (text variable) name:" ), _( "Add Parameter" ) );

                  if( dlg.ShowModal() != wxID_OK )
                      return;

                  wxString name = dlg.GetValue();
                  name.Trim().Trim( false );

                  if( name.IsEmpty() || m_frame->Prj().GetTextVars().count( name ) )
                      return;

                  m_frame->Prj().GetTextVars()[name] = wxEmptyString;
                  m_frame->Prj().IncrementTextVarsTicker();
                  m_frame->OnModify();
                  UpdateData();
              } },
            { _( "Edit" ),
              [this]()
              {
                  // The selected row's value (title block entries, then text variables)
                  int                           row = m_docParamRow - 1;
                  TITLE_BLOCK                   block = m_frame->GetScreen()->GetTitleBlock();
                  std::map<wxString, wxString>& vars = m_frame->Prj().GetTextVars();
                  wxString                      current;
                  wxString                      name;

                  if( row >= 0 && row < (int) entries.size() )
                  {
                      name = entries[row].name;
                      current = entries[row].get( block );
                  }
                  else if( row >= (int) entries.size() && row < (int) ( entries.size() + vars.size() ) )
                  {
                      auto it = vars.begin();
                      std::advance( it, row - (int) entries.size() );
                      name = it->first;
                      current = it->second;
                  }
                  else
                  {
                      return;
                  }

                  wxTextEntryDialog dlg( this, wxString::Format( _( "Value of '%s':" ), name ),
                                         _( "Edit Parameter" ), current );

                  if( dlg.ShowModal() != wxID_OK )
                      return;

                  if( row < (int) entries.size() )
                  {
                      entries[row].set( block, dlg.GetValue() );
                      m_frame->SetTitleBlock( block );
                  }
                  else
                  {
                      vars[name] = dlg.GetValue();
                      m_frame->Prj().IncrementTextVarsTicker();
                  }

                  m_frame->OnModify();
                  m_frame->HardRedraw();
                  UpdateData();
              } },
            { _( "Delete" ),
              [this]()
              {
                  // Only text variables can be removed; the title block entries always exist
                  int idx = m_docParamRow - 1 - (int) entries.size();
                  std::map<wxString, wxString>& vars = m_frame->Prj().GetTextVars();

                  if( idx < 0 || idx >= (int) vars.size() )
                      return;

                  auto it = vars.begin();
                  std::advance( it, idx );
                  vars.erase( it );
                  m_docParamRow = -1;
                  m_frame->Prj().IncrementTextVarsTicker();
                  m_frame->OnModify();
                  m_frame->HardRedraw();
                  UpdateData();
              } } } );
}
