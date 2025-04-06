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

#include "base.hh"

namespace {

enum class SeekMode {
  kSet,
  kCur,
  kEnd,
};

class File {
 public:
  File() = default;
  virtual ~File() = default;
  virtual const std::string& path() const = 0;
  virtual ssize_t Read(uint8_t* buf, size_t len) = 0;
  virtual ssize_t Write(uint8_t* buf, size_t len) = 0;
  virtual bool Sync() = 0;
  virtual bool Trunc(int64_t size) = 0;
  virtual int64_t Seek(int64_t offset, SeekMode mode) = 0;

 private:
  MIRAKC_ARIB_NON_COPYABLE(File);
};

}  // namespace
