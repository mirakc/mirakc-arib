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

  for (uint8_t i = 0; i < Clock::kPcrGapCountThreshold; ++i) {
    clock.UpdatePcr(kPcrTicksPerSec);
    EXPECT_TRUE(clock.IsReady());
  }

  clock.UpdatePcr(kPcrTicksPerSec);
  EXPECT_FALSE(clock.IsReady());

  clock.UpdatePcr(0);
  EXPECT_FALSE(clock.IsReady());

  clock.UpdateTime(ts::Time());
  EXPECT_TRUE(clock.IsReady());
  EXPECT_EQ(ts::Time(), clock.Now());
}
