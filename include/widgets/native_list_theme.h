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

#ifndef NATIVE_LIST_THEME_H
#define NATIVE_LIST_THEME_H

#include <kicommon.h>

class wxWindow;

/**
 * Altium's look for the native Windows lists in dark mode: their column headers are drawn in
 * the grey of Altium's header rows (Windows draws them near black and ignores wx colours), and
 * report-style list controls get the panel background instead of the near-black one.
 *
 * Safe to call repeatedly on the same windows; does nothing outside Windows dark mode.
 */
KICOMMON_API void ApplyNativeListTheme( wxWindow* aRoot );

#endif // NATIVE_LIST_THEME_H
