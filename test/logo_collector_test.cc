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
  EXPECT_TRUE(src.FeedPackets());
}

// TODO: Add more tests here.
//
// There are no classes and methods in TSDuck which can be used for generating
// TS packets from CDT sections to emulate test scenarios.
