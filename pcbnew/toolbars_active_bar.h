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

#ifndef TOOLBARS_ACTIVE_BAR_H
#define TOOLBARS_ACTIVE_BAR_H

#include <tool/actions.h>
#include <tool/ui/toolbar_configuration.h>
#include <tools/pcb_actions.h>

/**
 * The editing groups at the start of the Altium-style Active Bar, shared by the PCB and the
 * PCB library (footprint) editors: selection filter, move, select and align.
 */
inline void AppendActiveBarEditGroups( TOOLBAR_CONFIGURATION& aConfig, bool aBoardEditor )
{
    aConfig.AppendAction( PCB_ACTIONS::selectionFilterMenu );

    // Snapping: Altium's magnet button
    aConfig.AppendGroup( TOOLBAR_GROUP_CONFIG( _( "Snapping" ) )
                         .AddAction( PCB_ACTIONS::magneticSnapToggle )
                         .AddAction( PCB_ACTIONS::magneticSnapActiveLayer )
                         .AddAction( PCB_ACTIONS::magneticSnapAllLayers ) );

    TOOLBAR_GROUP_CONFIG move( _( "Move objects" ) );
    move.AddAction( PCB_ACTIONS::move )
        .AddAction( PCB_ACTIONS::drag45Degree )
        .AddAction( PCB_ACTIONS::dragFreeAngle )
        .AddAction( PCB_ACTIONS::moveIndividually )
        .AddAction( PCB_ACTIONS::moveExact )
        .AddAction( PCB_ACTIONS::positionRelative )
        .AddAction( PCB_ACTIONS::rotateCcw )
        .AddAction( PCB_ACTIONS::rotateCw )
        .AddAction( PCB_ACTIONS::flip )
        .AddAction( PCB_ACTIONS::swap );

    TOOLBAR_GROUP_CONFIG select( _( "Select objects" ) );
    select.AddAction( PCB_ACTIONS::selectInsideArea )
          .AddAction( ACTIONS::selectSetLasso )
          .AddAction( PCB_ACTIONS::selectOutsideArea )
          .AddAction( PCB_ACTIONS::selectTouchingRect )
          .AddAction( PCB_ACTIONS::selectTouchingLine )
          .AddAction( ACTIONS::selectAll );

    if( aBoardEditor )
    {
        select.AddAction( PCB_ACTIONS::selectNet )
              .AddAction( PCB_ACTIONS::selectConnection );
    }

    select.AddAction( PCB_ACTIONS::selectAllOnLayer );

    if( aBoardEditor )
        select.AddAction( PCB_ACTIONS::selectFreeObjects );

    select.AddAction( PCB_ACTIONS::selectAllLocked )
          .AddAction( PCB_ACTIONS::selectOffGridPads )
          .AddAction( PCB_ACTIONS::toggleSelection )
          .AddAction( ACTIONS::selectSetRect );

    TOOLBAR_GROUP_CONFIG align( _( "Align objects" ) );
    align.AddAction( PCB_ACTIONS::alignLeft )
         .AddAction( PCB_ACTIONS::alignRight )
         .AddAction( PCB_ACTIONS::alignCenterX )
         .AddAction( PCB_ACTIONS::distributeHorizontallyCenters )
         .AddAction( PCB_ACTIONS::distributeHorizontallyGaps )
         .AddAction( PCB_ACTIONS::alignTop )
         .AddAction( PCB_ACTIONS::alignBottom )
         .AddAction( PCB_ACTIONS::alignCenterY )
         .AddAction( PCB_ACTIONS::distributeVerticallyCenters )
         .AddAction( PCB_ACTIONS::distributeVerticallyGaps )
         .AddAction( PCB_ACTIONS::alignToGrid );

    aConfig.AppendGroup( move ).AppendGroup( select ).AppendGroup( align );
}

#endif // TOOLBARS_ACTIVE_BAR_H
