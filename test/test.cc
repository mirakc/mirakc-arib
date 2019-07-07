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

#include <gmock/gmock.h>
#include <tsduck/tsduck.h>

#include "logging.hh"

int main(int argc, char* argv[]) {
  InitLogger("mirakc-arib-test");
  // Don't call ts::DVBCharset::EnableARIBMode() here.  Strings are encoded in
  // UTF-8 for simplicity.
  testing::InitGoogleMock(&argc, argv);
  return RUN_ALL_TESTS();
}
