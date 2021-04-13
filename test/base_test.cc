#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tsduck/tsduck.h>

#include "base.hh"

TEST(ClockTest, PcrWrapAround) {
  ClockBaseline baseline;
  baseline.SetPid(0x100);
  baseline.SetPcr(kPcrUpperBound - kPcrTicksPerSec);
  baseline.SetTime(ts::Time());
  EXPECT_TRUE(baseline.IsReady());

  Clock clock(baseline);
  clock.UpdatePcr(kPcrUpperBound - kPcrTicksPerSec);
  EXPECT_TRUE(clock.IsReady());
  EXPECT_EQ(ts::Time(), clock.Now());

  clock.UpdatePcr(0);
  EXPECT_EQ(ts::Time() + ts::MilliSecPerSec, clock.Now());
}
