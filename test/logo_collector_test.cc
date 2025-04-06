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

#include "logo_collector.hh"

#include "test_helper.hh"

TEST(LogoCollectorTest, NoPacket) {
  MockSource src;
  auto collector = std::make_unique<LogoCollector>();
  auto sink = std::make_unique<MockJsonlSink>();

  EXPECT_CALL(src, GetNextPacket).WillOnce(testing::Return(false));  // EOF
  EXPECT_CALL(*sink, HandleDocument).Times(0);

  collector->Connect(std::move(sink));
  src.Connect(std::move(collector));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
}

// TODO: Add more tests here.
//
// There are no classes and methods in TSDuck which can be used for generating
// TS packets from CDT sections to emulate test scenarios.
