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
