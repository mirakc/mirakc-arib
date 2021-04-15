#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tsduck/tsduck.h>

#include "base.hh"

TEST(ClockTest, PcrWrapAround) {
  ClockBaseline baseline;
  baseline.SetPid(0x100);
  baseline.SetPcr(kPcrUpperBound - kPcrTicksPerMs);
  baseline.SetTime(ts::Time());
  EXPECT_TRUE(baseline.IsReady());

  Clock clock(baseline);
  clock.UpdatePcr(kPcrUpperBound - kPcrTicksPerMs);
  EXPECT_TRUE(clock.IsReady());
  EXPECT_EQ(ts::Time(), clock.Now());

  clock.UpdatePcr(0);
  EXPECT_EQ(ts::Time() + 1, clock.Now());
}

TEST(ClockTest, Invalidate) {
  ClockBaseline baseline;
  baseline.SetPid(0x100);
  baseline.SetPcr(0);
  baseline.SetTime(ts::Time());
  EXPECT_TRUE(baseline.IsReady());

  Clock clock(baseline);
  clock.UpdatePcr(0);
  EXPECT_TRUE(clock.IsReady());
  EXPECT_EQ(ts::Time(), clock.Now());

  clock.UpdatePcr(kPcrTicksPerSec);
  EXPECT_FALSE(clock.IsReady());

  clock.UpdatePcr(0);
  EXPECT_FALSE(clock.IsReady());

  clock.UpdateTime(ts::Time());
  EXPECT_TRUE(clock.IsReady());
  EXPECT_EQ(ts::Time(), clock.Now());
}
