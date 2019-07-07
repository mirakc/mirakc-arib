// Copyright (c) 2019 Masayuki Nagamachi <masayuki.nagamachi@gmail.com>
//
// Licensed under either of
//
//   * Apache License, Version 2.0
//     (LICENSE-APACHE or http://www.apache.org/licenses/LICENSE-2.0)
//   * MIT License
//     (LICENSE-MIT or http://opensource.org/licenses/MIT)
//
// at your option.

#pragma once

#include "base.hh"

namespace {

class File {
 public:
  File() = default;
  virtual ~File() = default;
  virtual ssize_t Read(uint8_t* buf, size_t len) = 0;

 private:
  MIRAKC_ARIB_NON_COPYABLE(File);
};

}  // namespace
