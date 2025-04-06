// SPDX-License-Identifier: GPL-2.0-or-later

// mirakc-arib
// Copyright (C) 2019 masnagam
//
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation; either version 2 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
// without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See
// the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with this program; if
// not, write to the Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
// 02110-1301, USA.

#pragma once

#include <cstdlib>

static_assert(EXIT_SUCCESS == 0);
static_assert(EXIT_FAILURE == 1);

// This exit code is returned when `filter-program` stopped before the program
// starts.  This situation occurs when the program was canceled or rescheduled.
//
// There are many 'reserved' code as described in:
// https://tldp.org/LDP/abs/html/exitcodes.html
#define EXIT_RETRY 222
