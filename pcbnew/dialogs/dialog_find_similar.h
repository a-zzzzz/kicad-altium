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

#ifndef DIALOG_FIND_SIMILAR_H
#define DIALOG_FIND_SIMILAR_H

#include <dialogs/dialog_find_similar_objects.h>

class BOARD_ITEM;
class PCB_BASE_FRAME;

/**
 * Find Similar Objects for the board: the common dialog over every board object, with board
 * value text (nets by name).  docs/altium-parity/find-similar.md
 */
class DIALOG_FIND_SIMILAR : public DIALOG_FIND_SIMILAR_OBJECTS
{
public:
    DIALOG_FIND_SIMILAR( PCB_BASE_FRAME* aFrame, BOARD_ITEM* aReference );

private:
    static std::vector<EDA_ITEM*> boardItems( PCB_BASE_FRAME* aFrame );
};

#endif // DIALOG_FIND_SIMILAR_H
