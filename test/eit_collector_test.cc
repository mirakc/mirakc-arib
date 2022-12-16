#include <cstdlib>
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
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
}

TEST(EitCollectorTest, TimedOut) {
  EitCollectorOption option;
  option.time_limit = 5000;

  TableSource src;
  auto collector = std::make_unique<EitCollector>(kEmptyOption);
  auto sink = std::make_unique<MockJsonlSink>();

  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <TOT UTC_time="2020-02-05 00:00:00" test-pid="0x0014" test-cc="0" />
      <TOT UTC_time="2020-02-05 00:00:05" test-pid="0x0014" test-cc="1" />
    </tsduck>
  )");

  EXPECT_CALL(*sink, HandleDocument).Times(0);

  collector->Connect(std::move(sink));
  src.Connect(std::move(collector));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(EitCollectorTest, Streaming) {
  EitCollectorOption option;
  option.time_limit = 5000;
  option.streaming = true;

  TableSource src;
  auto collector = std::make_unique<EitCollector>(kEmptyOption);
  auto sink = std::make_unique<MockJsonlSink>();

  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <TOT UTC_time="2020-02-05 00:00:00" test-pid="0x0014" test-cc="0" />
      <TOT UTC_time="2020-02-05 00:00:05" test-pid="0x0014" test-cc="1" />
      <TOT UTC_time="2020-02-05 00:00:10" test-pid="0x0014" test-cc="2" />
    </tsduck>
  )");

  EXPECT_CALL(*sink, HandleDocument).Times(0);

  collector->Connect(std::move(sink));
  src.Connect(std::move(collector));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

// TODO: Add more tests here.
//
// There are no classes and methods in TSDuck which can be used for generating
// TS packets from event information sections to emulate test scenarios.
