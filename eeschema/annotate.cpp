/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include <algorithm>
#include <set>

#include <confirm.h>
#include <project/project_file.h>
#include <reporter.h>
#include <sch_edit_frame.h>
#include <schematic.h>
#include <sch_commit.h>
#include <sch_group.h>
#include <erc/erc_settings.h>
#include <sch_reference_list.h>
#include <tools/sch_selection.h>
#include <tools/sch_selection_tool.h>
#include <tool/tool_manager.h>
#include <dialog_erc.h>
#include <eeschema_settings.h>

#include <map>

void SCH_EDIT_FRAME::mapExistingAnnotation( std::map<wxString, wxString>& aMap )
{
    SCH_REFERENCE_LIST references;
    Schematic().Hierarchy().GetSymbols( references, SYMBOL_FILTER_ALL );

    for( size_t i = 0; i < references.GetCount(); i++ )
    {
        SCH_SYMBOL*     symbol = references[ i ].GetSymbol();
        SCH_SHEET_PATH* curr_sheetpath = &references[ i ].GetSheetPath();
        KIID_PATH       curr_full_uuid = curr_sheetpath->Path();

        curr_full_uuid.push_back( symbol->m_Uuid );

        wxString ref = symbol->GetRef( curr_sheetpath, true );

        if( symbol->IsAnnotated( curr_sheetpath ) )
            aMap[ curr_full_uuid.AsString() ] = ref;
    }
}


void SCH_EDIT_FRAME::DeleteAnnotation( ANNOTATE_SCOPE_T aAnnotateScope, bool aRecursive,
                                       REPORTER& aReporter )
{

    SCH_SHEET_LIST sheets = Schematic().Hierarchy();
    SCH_SCREEN*    screen = GetScreen();
    SCH_SHEET_PATH currentSheet = GetCurrentSheet();
    SCH_COMMIT     commit( this );

    auto clearSymbolAnnotation =
            [&]( EDA_ITEM* aItem, SCH_SCREEN* aScreen, SCH_SHEET_PATH* aSheet, bool aResetPrefixes )
            {
                SCH_SYMBOL* symbol = static_cast<SCH_SYMBOL*>( aItem );
                commit.Modify( aItem, aScreen );

                // aSheet == nullptr means all sheets
                if( !aSheet || symbol->IsAnnotated( aSheet ) )
                {
                    wxString msg;

                    if( symbol->GetUnitCount() > 1 )
                    {
                        msg.Printf( _( "Cleared annotation for %s (unit %s)." ),
                                    symbol->GetValue( true, aSheet, false ),
                                    symbol->SubReference( symbol->GetUnit(), false ) );
                    }
                    else
                    {
                        msg.Printf( _( "Cleared annotation for %s." ),
                                    symbol->GetValue( true, aSheet, false ) );
                    }

                    symbol->ClearAnnotation( aSheet, aResetPrefixes );
                    aReporter.Report( msg, RPT_SEVERITY_ACTION );
                }
            };

    auto clearSheetAnnotation =
            [&]( SCH_SCREEN* aScreen, SCH_SHEET_PATH* aSheet, bool aResetPrefixes )
            {
                for( SCH_ITEM* item : aScreen->Items().OfType( SCH_SYMBOL_T ) )
                    clearSymbolAnnotation( item, aScreen, aSheet, aResetPrefixes );
            };

    switch( aAnnotateScope )
    {
    case ANNOTATE_ALL:
    {
        for( SCH_SHEET_PATH& sheet : sheets )
            clearSheetAnnotation( sheet.LastScreen(), &sheet, false );

        break;
    }
    case ANNOTATE_CURRENT_SHEET:
    {
        clearSheetAnnotation( screen, &currentSheet, false );

        if( aRecursive )
        {
            SCH_SHEET_LIST subSheets;

            std::vector<SCH_ITEM*> tempSubSheets;
            currentSheet.LastScreen()->GetSheets( &tempSubSheets );

            for( SCH_ITEM* item : tempSubSheets )
            {
                SCH_SHEET_PATH subSheetPath = currentSheet;
                subSheetPath.push_back( static_cast<SCH_SHEET*>( item ) );

                sheets.GetSheetsWithinPath( subSheets, subSheetPath );
            }

            for( SCH_SHEET_PATH sheet : subSheets )
                clearSheetAnnotation( sheet.LastScreen(), &sheet, false );
        }

        break;
    }

    case ANNOTATE_SELECTION:
    {
        SCH_SELECTION_TOOL* selTool = m_toolManager->GetTool<SCH_SELECTION_TOOL>();
        SCH_SELECTION&      selection = selTool->RequestSelection();
        SCH_SHEET_LIST      selectedSheets;

        for( EDA_ITEM* item : selection.Items() )
        {
            if( item->Type() == SCH_SYMBOL_T )
                clearSymbolAnnotation( item, screen, &currentSheet, false );

            if( item->Type() == SCH_SHEET_T && aRecursive )
            {
                SCH_SHEET_PATH subSheetPath = currentSheet;
                subSheetPath.push_back( static_cast<SCH_SHEET*>( item ) );

                sheets.GetSheetsWithinPath( selectedSheets, subSheetPath );
            }
        }

        for( SCH_SHEET_PATH sheet : selectedSheets )
            clearSheetAnnotation( sheet.LastScreen(), &sheet, false );

        break;
    }
    }

    // Update the references for the sheet that is currently being displayed.
    GetCurrentSheet().UpdateAllScreenReferences();

    wxWindow* erc_dlg = wxWindow::FindWindowByName( DIALOG_ERC_WINDOW_NAME );

    if( erc_dlg )
        static_cast<DIALOG_ERC*>( erc_dlg )->UpdateAnnotationWarning();

    commit.Push( _( "Delete Annotation" ) );

    // Must go after OnModify() so the connectivity graph has been updated
    UpdateNetHighlightStatus();
}


std::unordered_set<SCH_SYMBOL*> getInferredSymbols( const SCH_SELECTION& aSelection )
{
    std::unordered_set<SCH_SYMBOL*> symbols;

    for( EDA_ITEM* item : aSelection )
    {
        switch( item->Type() )
        {
        case SCH_FIELD_T:
        {
            SCH_FIELD*  field = static_cast<SCH_FIELD*>( item );

            if( field->GetId() == FIELD_T::REFERENCE && field->GetParent()->Type() == SCH_SYMBOL_T )
                symbols.insert( static_cast<SCH_SYMBOL*>( field->GetParent() ) );

            break;
        }

        case SCH_SYMBOL_T:
            symbols.insert( static_cast<SCH_SYMBOL*>( item ) );
            break;

        case SCH_GROUP_T:
        {
            SCH_GROUP* group = static_cast<SCH_GROUP*>( item );

            group->RunOnChildren(
                    [&symbols]( SCH_ITEM* aChild )
                    {
                        if( aChild->Type() == SCH_SYMBOL_T )
                            symbols.insert( static_cast<SCH_SYMBOL*>( aChild ) );
                    },
                    RECURSE_MODE::RECURSE );

            break;
        }

        default:
            break;
        }
    }

    return symbols;
}


void SCH_EDIT_FRAME::AnnotateSymbols( SCH_COMMIT* aCommit, ANNOTATE_SCOPE_T aAnnotateScope,
                                      ANNOTATE_ORDER_T aSortOption, ANNOTATE_ALGO_T aAlgoOption, bool aRecursive,
                                      int aStartNumber, bool aResetAnnotation, bool aRegroupUnits,
                                      bool aRepairTimestamps, REPORTER& aReporter, SYMBOL_FILTER aSymbolFilter )
{
    SCH_SELECTION_TOOL* selTool = m_toolManager->GetTool<SCH_SELECTION_TOOL>();
    SCH_SELECTION&      selection = selTool->GetSelection();

    SCH_REFERENCE_LIST  references;
    SCH_SCREENS         screens( Schematic().Root() );
    SCH_SHEET_LIST      sheets = Schematic().Hierarchy();
    SCH_SHEET_PATH      currentSheet = GetCurrentSheet();


    // Store the selected sheets relative to the full hierarchy so we get the correct sheet numbers
    SCH_SHEET_LIST selectedSheets;

    for( EDA_ITEM* item : selection )
    {
        if( item->Type() == SCH_SHEET_T )
        {
            SCH_SHEET_PATH subSheetPath = currentSheet;
            subSheetPath.push_back( static_cast<SCH_SHEET*>( item ) );

            sheets.GetSheetsWithinPath( selectedSheets, subSheetPath );
        }
    }


    // Like above, store subsheets relative to full hierarchy for recursive annotation from current
    // sheet
    SCH_SHEET_LIST subSheets;

    std::vector<SCH_ITEM*> tempSubSheets;
    currentSheet.LastScreen()->GetSheets( &tempSubSheets );

    for( SCH_ITEM* item : tempSubSheets )
    {
        SCH_SHEET_PATH subSheetPath = currentSheet;
        subSheetPath.push_back( static_cast<SCH_SHEET*>( item ) );

        sheets.GetSheetsWithinPath( subSheets, subSheetPath );
    }


    std::unordered_set<SCH_SYMBOL*> selectedSymbols;

    if( aAnnotateScope == ANNOTATE_SELECTION )
        selectedSymbols = getInferredSymbols( selection );


    // Map of locked symbols
    SCH_MULTI_UNIT_REFERENCE_MAP lockedSymbols;

    // Map of previous annotation for building info messages
    std::map<wxString, wxString> previousAnnotation;

    // Test for and replace duplicate time stamps in symbols and sheets.  Duplicate time stamps
    // can happen with old schematics, schematic conversions, or manual editing of files.
    if( aRepairTimestamps )
    {
        int count = screens.ReplaceDuplicateTimeStamps();

        if( count )
        {
            wxString msg;
            msg.Printf( _( "%d duplicate time stamps were found and replaced." ), count );
            aReporter.ReportTail( msg, RPT_SEVERITY_WARNING );
        }
    }

    // Collect all the sets that must be annotated together. When regrouping, we skip this step
    // to allow fresh groupings based on symbol placement. When resetting without regrouping, we
    // collect locked symbols but then check for unit conflicts (duplicate units within a ref).
    if( !aRegroupUnits )
    {
        switch( aAnnotateScope )
        {
        case ANNOTATE_ALL:
            sheets.GetMultiUnitSymbols( lockedSymbols, SYMBOL_FILTER_ALL );
            break;

        case ANNOTATE_CURRENT_SHEET:
            currentSheet.GetMultiUnitSymbols( lockedSymbols, SYMBOL_FILTER_ALL );

            if( aRecursive )
                subSheets.GetMultiUnitSymbols( lockedSymbols, SYMBOL_FILTER_ALL );

            break;

        case ANNOTATE_SELECTION:
            for( SCH_SYMBOL* symbol : selectedSymbols )
                currentSheet.AppendMultiUnitSymbol( lockedSymbols, symbol, aSymbolFilter );

            if( aRecursive )
                selectedSheets.GetMultiUnitSymbols( lockedSymbols, aSymbolFilter );

            break;
        }

        // When resetting annotations, check for and remove groups with unit conflicts (duplicate
        // units within the same reference designator). These will get fresh assignments.
        if( aResetAnnotation )
        {
            std::vector<wxString> conflictingRefs;

            for( auto& [refBase, refList] : lockedSymbols )
            {
                std::set<int> seenUnits;
                bool hasConflict = false;

                for( const SCH_REFERENCE& ref : refList )
                {
                    if( seenUnits.count( ref.GetUnit() ) )
                    {
                        hasConflict = true;
                        break;
                    }

                    seenUnits.insert( ref.GetUnit() );
                }

                if( hasConflict )
                    conflictingRefs.push_back( refBase );
            }

            for( const wxString& ref : conflictingRefs )
                lockedSymbols.erase( ref );
        }
    }

    // Store previous annotations for building info messages
    mapExistingAnnotation( previousAnnotation );

    // Set sheet number and number of sheets.
    SetSheetNumberAndCount();

    // Build symbol list
    switch( aAnnotateScope )
    {
    case ANNOTATE_ALL:
        sheets.GetSymbols( references, SYMBOL_FILTER_ALL );
        break;

    case ANNOTATE_CURRENT_SHEET:
        currentSheet.GetSymbols( references, SYMBOL_FILTER_ALL );

        if( aRecursive )
            subSheets.GetSymbolsWithinPath( references, currentSheet, SYMBOL_FILTER_NON_POWER, true );

        break;

    case ANNOTATE_SELECTION:
        for( SCH_SYMBOL* symbol : selectedSymbols )
            currentSheet.AppendSymbol( references, symbol, aSymbolFilter, true );

        if( aRecursive )
            selectedSheets.GetSymbolsWithinPath( references, currentSheet, aSymbolFilter, true );

        break;
    }

    // Remove annotation only updates the "new" flag to indicate to the algorithm
    // that these references must be reannotated, but keeps the original reference
    // so that we can reannotate multi-unit symbols together.
    if( aResetAnnotation )
        references.RemoveAnnotation();

    // Build additional list of references to be used during reannotation
    // to avoid duplicate designators (no additional references when annotating
    // the full schematic)
    SCH_REFERENCE_LIST additionalRefs;

    if( aAnnotateScope != ANNOTATE_ALL )
    {
        SCH_REFERENCE_LIST allRefs;
        sheets.GetSymbols( allRefs, SYMBOL_FILTER_ALL );

        for( size_t i = 0; i < allRefs.GetCount(); i++ )
        {
            if( !references.Contains( allRefs[i] ) )
                additionalRefs.AddItem( allRefs[i] );
        }
    }

    references.SetRefDesTracker( Schematic().Settings().m_refDesTracker );

    // Break full symbol reference into name (prefix) and number:
    // example: IC1 become IC, and 1
    references.SplitReferences();

    // Annotate all of the references we've collected by our options
    references.AnnotateByOptions( aSortOption, aAlgoOption, aStartNumber, lockedSymbols,
                                  additionalRefs, false );

    for( size_t i = 0; i < references.GetCount(); i++ )
    {
        SCH_REFERENCE&  ref = references[i];
        SCH_SYMBOL*     symbol = ref.GetSymbol();
        SCH_SHEET_PATH* sheet = &ref.GetSheetPath();

        aCommit->Modify( symbol, sheet->LastScreen() );
        ref.Annotate();

        KIID_PATH full_uuid = sheet->Path();
        full_uuid.push_back( symbol->m_Uuid );

        wxString  prevRef = previousAnnotation[ full_uuid.AsString() ];
        wxString  newRef  = symbol->GetRef( sheet );

        if( symbol->GetUnitCount() > 1 )
            newRef << symbol->SubReference( symbol->GetUnitSelection( sheet ) );

        wxString msg;

        if( prevRef.Length() )
        {
            if( newRef == prevRef )
                continue;

            if( symbol->GetUnitCount() > 1 )
            {
                msg.Printf( _( "Updated %s (unit %s) from %s to %s." ),
                            symbol->GetValue( true, sheet, false ),
                            symbol->SubReference( symbol->GetUnit(), false ),
                            prevRef,
                            newRef );
            }
            else
            {
                msg.Printf( _( "Updated %s from %s to %s." ),
                            symbol->GetValue( true, sheet, false ),
                            prevRef,
                            newRef );
            }
        }
        else
        {
            if( symbol->GetUnitCount() > 1 )
            {
                msg.Printf( _( "Annotated %s (unit %s) as %s." ),
                            symbol->GetValue( true, sheet, false ),
                            symbol->SubReference( symbol->GetUnit(), false ),
                            newRef );
            }
            else
            {
                msg.Printf( _( "Annotated %s as %s." ),
                            symbol->GetValue( true, sheet, false ),
                            newRef );
            }
        }

        aReporter.Report( msg, RPT_SEVERITY_ACTION );
    }

    // Final control (just in case ... ).
    if( !CheckAnnotate(
                [&aReporter]( ERCE_T, const wxString& aMsg, SCH_REFERENCE*, SCH_REFERENCE* )
                {
                    aReporter.Report( aMsg, RPT_SEVERITY_ERROR );
                },
                aAnnotateScope, aRecursive, aSymbolFilter ) )
    {
        aReporter.ReportTail( _( "Annotation complete." ), RPT_SEVERITY_ACTION );
    }

    // Update on screen references, that can be modified by previous calculations:
    GetCurrentSheet().UpdateAllScreenReferences();
    SetSheetNumberAndCount();

    wxWindow* erc_dlg = wxWindow::FindWindowByName( DIALOG_ERC_WINDOW_NAME );

    if( erc_dlg )
        static_cast<DIALOG_ERC*>( erc_dlg )->UpdateAnnotationWarning();

    SyncView();
    GetCanvas()->Refresh();
    OnModify();

    // Must go after OnModify() so the connectivity graph has been updated
    UpdateNetHighlightStatus();
}


int SCH_EDIT_FRAME::CheckAnnotate( ANNOTATION_ERROR_HANDLER aErrorHandler, ANNOTATE_SCOPE_T aAnnotateScope,
                                   bool aRecursive, SYMBOL_FILTER aSymbolFilter )
{
    SCH_REFERENCE_LIST  referenceList;
    SCH_SHEET_LIST      sheets = Schematic().Hierarchy();
    SCH_SHEET_PATH      currentSheet = GetCurrentSheet();

    // Build the list of symbols
    switch( aAnnotateScope )
    {
    case ANNOTATE_ALL:
        sheets.GetSymbols( referenceList, SYMBOL_FILTER_ALL );
        break;

    case ANNOTATE_CURRENT_SHEET:
        GetCurrentSheet().GetSymbols( referenceList, SYMBOL_FILTER_NON_POWER );

        if( aRecursive )
        {
            SCH_SHEET_LIST subSheets;

            std::vector<SCH_ITEM*> tempSubSheets;
            currentSheet.LastScreen()->GetSheets( &tempSubSheets );

            for( SCH_ITEM* item : tempSubSheets )
            {
                SCH_SHEET_PATH subSheetPath = currentSheet;
                subSheetPath.push_back( static_cast<SCH_SHEET*>( item ) );

                sheets.GetSheetsWithinPath( subSheets, subSheetPath );
            }

            for( SCH_SHEET_PATH sheet : subSheets )
                sheet.GetSymbols( referenceList, SYMBOL_FILTER_NON_POWER );
        }

        break;

    case ANNOTATE_SELECTION:
        SCH_SELECTION_TOOL* selTool = m_toolManager->GetTool<SCH_SELECTION_TOOL>();
        SCH_SELECTION&      selection = selTool->RequestSelection();

        for( SCH_SYMBOL* symbol : getInferredSymbols( selection ) )
            GetCurrentSheet().AppendSymbol( referenceList, symbol, aSymbolFilter, true );

        if( aRecursive )
        {
            SCH_SHEET_LIST selectedSheets;

            for( EDA_ITEM* item : selection.Items() )
            {
                if( item->Type() == SCH_SHEET_T )
                {
                    SCH_SHEET_PATH subSheetPath = currentSheet;
                    subSheetPath.push_back( static_cast<SCH_SHEET*>( item ) );

                    sheets.GetSheetsWithinPath( selectedSheets, subSheetPath );
                }
            }

            for( SCH_SHEET_PATH sheet : selectedSheets )
                sheet.GetSymbols( referenceList, aSymbolFilter );
        }

        break;
    }

    // Empty schematic does not need annotation
    if( referenceList.GetCount() == 0 )
        return 0;

    return referenceList.CheckAnnotation( aErrorHandler );
}


namespace
{
/// Counts the designators an annotation run changed (it reports one action per symbol).
class ANNOTATION_COUNTER : public REPORTER
{
public:
    REPORTER& Report( const wxString& aText, SEVERITY aSeverity = RPT_SEVERITY_UNDEFINED ) override
    {
        REPORTER::Report( aText, aSeverity );

        if( aSeverity == RPT_SEVERITY_ACTION )
            ++m_actions;
        else if( aSeverity == RPT_SEVERITY_ERROR )
            ++m_errors;

        return *this;
    }

    // The closing "Annotation complete." is a tail message: don't count it
    REPORTER& ReportTail( const wxString& aText, SEVERITY aSeverity = RPT_SEVERITY_UNDEFINED ) override
    {
        return REPORTER::Report( aText, aSeverity );
    }

    bool HasMessage() const override { return m_actions + m_errors > 0; }

    int m_actions = 0;
    int m_errors = 0;
};
}


int SCH_EDIT_FRAME::countUnannotated()
{
    SCH_REFERENCE_LIST references;
    Schematic().Hierarchy().GetSymbols( references, SYMBOL_FILTER_NON_POWER );

    int count = 0;

    for( size_t ii = 0; ii < references.GetCount(); ++ii )
    {
        if( !references[ii].GetSymbol()->IsAnnotated( &references[ii].GetSheetPath() ) )
            ++count;
    }

    return count;
}


void SCH_EDIT_FRAME::ResetAllDesignators()
{
    SCH_REFERENCE_LIST references;
    Schematic().Hierarchy().GetSymbols( references, SYMBOL_FILTER_NON_POWER );

    int annotated = (int) references.GetCount() - countUnannotated();

    if( annotated <= 0 )
    {
        ShowInfoBarMsg( _( "No schematic designators to reset." ) );
        return;
    }

    wxString msg = wxString::Format( wxPLURAL( "%d designator on all schematic sheets will be "
                                               "reset to its unannotated form (e.g. R?).\n\n"
                                               "Proceed?",
                                               "%d designators on all schematic sheets will be "
                                               "reset to their unannotated form (e.g. R?).\n\n"
                                               "Proceed?",
                                               annotated ),
                                     annotated );

    if( !IsOK( this, msg ) )
        return;

    // DeleteAnnotation() makes one commit: a single undo step
    NULL_REPORTER reporter;
    DeleteAnnotation( ANNOTATE_ALL, true, reporter );
}


void SCH_EDIT_FRAME::ResetDuplicateDesignators()
{
    SCH_REFERENCE_LIST references;
    Schematic().Hierarchy().GetSymbols( references, SYMBOL_FILTER_NON_POWER );

    // A designator (for multi-unit symbols: designator and unit) used more than once: keep the
    // first one in sheet order and reset the others, so the next annotation renumbers them
    std::map<wxString, int>     uses;
    std::vector<SCH_REFERENCE*> duplicates;

    for( size_t ii = 0; ii < references.GetCount(); ++ii )
    {
        SCH_REFERENCE&  ref = references[ii];
        SCH_SYMBOL*     symbol = ref.GetSymbol();
        SCH_SHEET_PATH& sheet = ref.GetSheetPath();

        if( !symbol->IsAnnotated( &sheet ) )
            continue;

        wxString key = symbol->GetRef( &sheet, false );

        if( symbol->GetUnitCount() > 1 )
            key << wxS( "\t" ) << symbol->GetUnitSelection( &sheet );

        if( uses[key]++ > 0 )
            duplicates.push_back( &ref );
    }

    if( duplicates.empty() )
    {
        ShowInfoBarMsg( _( "No duplicate schematic designators found." ) );
        return;
    }

    wxString msg = wxString::Format( wxPLURAL( "%d duplicate designator will be reset to its "
                                               "unannotated form (e.g. R?).\n\nProceed?",
                                               "%d duplicate designators will be reset to their "
                                               "unannotated form (e.g. R?).\n\nProceed?",
                                               duplicates.size() ),
                                     (int) duplicates.size() );

    if( !IsOK( this, msg ) )
        return;

    SCH_COMMIT commit( this );

    for( SCH_REFERENCE* ref : duplicates )
    {
        SCH_SHEET_PATH& sheet = ref->GetSheetPath();

        commit.Modify( ref->GetSymbol(), sheet.LastScreen() );
        ref->GetSymbol()->ClearAnnotation( &sheet, false );
    }

    GetCurrentSheet().UpdateAllScreenReferences();
    commit.Push( _( "Reset Duplicate Designators" ) );

    if( wxWindow* erc_dlg = wxWindow::FindWindowByName( DIALOG_ERC_WINDOW_NAME ) )
        static_cast<DIALOG_ERC*>( erc_dlg )->UpdateAnnotationWarning();

    UpdateNetHighlightStatus();
}


void SCH_EDIT_FRAME::annotateWithLastSettings( bool aReset )
{
    EESCHEMA_SETTINGS*  cfg = eeconfig();
    SCHEMATIC_SETTINGS& projSettings = Schematic().Settings();

    ANNOTATE_ORDER_T sortOrder = (ANNOTATE_ORDER_T) projSettings.m_AnnotateSortOrder;
    ANNOTATE_ALGO_T  algo = (ANNOTATE_ALGO_T) projSettings.m_AnnotateMethod;
    bool             regroup = aReset && cfg && cfg->m_AnnotatePanel.regroup_units;

    SCH_COMMIT         commit( this );
    ANNOTATION_COUNTER counter;

    // As Altium does, these commands cover every schematic of the project, whatever scope the
    // Annotate dialog used last; order, numbering and start number are the dialog's last ones
    AnnotateSymbols( &commit, ANNOTATE_ALL, sortOrder, algo, true, projSettings.m_AnnotateStartNum,
                     aReset, regroup, true, counter, SYMBOL_FILTER_NON_POWER );

    commit.Push( aReset ? _( "Force Annotate All Schematics" ) : _( "Annotate Schematics" ) );

    if( counter.m_errors )
        ShowInfoBarError( _( "Annotation finished with errors; run Validate to list them." ) );
    else
        ShowInfoBarMsg( wxString::Format( wxPLURAL( "%d designator annotated.",
                                                    "%d designators annotated.",
                                                    counter.m_actions ),
                                          counter.m_actions ) );
}


void SCH_EDIT_FRAME::AnnotateQuietly()
{
    if( countUnannotated() == 0 )
    {
        ShowInfoBarMsg( _( "All schematic designators are already annotated." ) );
        return;
    }

    annotateWithLastSettings( false );
}


void SCH_EDIT_FRAME::ForceAnnotateAll()
{
    if( !IsOK( this, _( "All schematic designators will be reset and annotated again with the "
                        "last annotation settings.\n\nProceed?" ) ) )
    {
        return;
    }

    annotateWithLastSettings( true );
}
