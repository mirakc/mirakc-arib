# SPDX-License-Identifier: GPL-2.0-or-later

# mirakc-arib
# Copyright (C) 2019 masnagam
#
# This program is free software; you can redistribute it and/or modify it under the terms of the
# GNU General Public License as published by the Free Software Foundation; either version 2 of the
# License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
# without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See
# the GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along with this program; if
# not, write to the Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
# 02110-1301, USA.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(MIRAKC_ARIB_HOST_TRIPLE arm-linux-gnueabi)

set(CMAKE_C_COMPILER ${MIRAKC_ARIB_HOST_TRIPLE}-gcc)
set(CMAKE_C_COMPILER_TARGET ${MIRAKC_ARIB_HOST_TRIPLE})

set(CMAKE_CXX_COMPILER ${MIRAKC_ARIB_HOST_TRIPLE}-g++)
set(CMAKE_CXX_COMPILER_TARGET ${MIRAKC_ARIB_HOST_TRIPLE})
