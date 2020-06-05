#include <gmock/gmock.h>
#include <tsduck/tsduck.h>
#include <spdlog/cfg/env.h>

#include "logging.hh"

int main(int argc, char* argv[]) {
  spdlog::cfg::load_env_levels("MIRAKC_ARIB_TEST_LOG");
  InitLogger("mirakc-arib-test");
  // Don't call ts::DVBCharset::EnableARIBMode() here.  Strings are encoded in
  // UTF-8 for simplicity.
  testing::InitGoogleMock(&argc, argv);
  return RUN_ALL_TESTS();
}
