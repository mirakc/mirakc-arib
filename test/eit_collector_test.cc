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

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tsduck/tsduck.h>

#include "eit_collector.hh"

#include "test_helper.hh"

namespace {
const EitCollectorOption kEmptyOption {};
}

TEST(EitCollectorTest, NoPacket) {
  MockSource src;
  auto collector = std::make_unique<EitCollector>(kEmptyOption);
  auto sink = std::make_unique<MockJsonlSink>();

  EXPECT_CALL(src, GetNextPacket).WillOnce(testing::Return(false));  // EOF
  EXPECT_CALL(*sink, HandleDocument).Times(0);

  collector->Connect(std::move(sink));
  src.Connect(std::move(collector));
  src.FeedPackets();
}

// TODO: Add more tests here.
//
// There are no classes and methods in TSDuck which can be used for generating
// TS packets from event information sections to emulate test scenarios.
