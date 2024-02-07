#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tsduck/tsduck.h>

#include "airtime_tracker.hh"

#include "test_helper.hh"

namespace {
const AirtimeTrackerOption kOption{0x0003, 0x0004};
}

TEST(AirtimeTrackerTest, NoPacket) {
  MockSource src;
  auto tracker = std::make_unique<AirtimeTracker>(kOption);
  auto sink = std::make_unique<MockJsonlSink>();

  EXPECT_CALL(src, GetNextPacket).WillOnce(testing::Return(false));  // EOF
  EXPECT_CALL(*sink, HandleDocument).Times(0);

  tracker->Connect(std::move(sink));
  src.Connect(std::move(tracker));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
}

TEST(AirtimeTrackerTest, Present) {
  TableSource src;
  auto tracker = std::make_unique<AirtimeTracker>(kOption);
  auto sink = std::make_unique<MockJsonlSink>();

  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0003" transport_stream_id="0x0002"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012">
        <event event_id="0x0004" start_time="1970-01-01 09:00:00"
               duration="00:0:01" running_status="undefined" CA_mode="true" />
        <event event_id="0x0005" start_time="1970-01-01 09:00:01"
               duration="01:00:00" running_status="undefined" CA_mode="true" />
      </EIT>
   </tsduck>
  )");

  EXPECT_CALL(*sink, HandleDocument).WillOnce([](const rapidjson::Document& doc) {
    EXPECT_EQ(R"({"nid":1,"tsid":2,"sid":3,"eid":4,"startTime":0,"duration":1000})",
        MockJsonlSink::Stringify(doc));
    return true;
  });

  tracker->Connect(std::move(sink));
  src.Connect(std::move(tracker));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(AirtimeTrackerTest, Following) {
  TableSource src;
  auto tracker = std::make_unique<AirtimeTracker>(kOption);
  auto sink = std::make_unique<MockJsonlSink>();

  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0003" transport_stream_id="0x0002"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012">
        <event event_id="0x0003" start_time="1970-01-01 09:00:00"
               duration="00:0:01" running_status="undefined" CA_mode="true" />
        <event event_id="0x0004" start_time="1970-01-01 09:00:01"
               duration="0:00:01" running_status="undefined" CA_mode="true" />
      </EIT>
   </tsduck>
  )");

  EXPECT_CALL(*sink, HandleDocument).WillOnce([](const rapidjson::Document& doc) {
    EXPECT_EQ(R"({"nid":1,"tsid":2,"sid":3,"eid":4,"startTime":1000,"duration":1000})",
        MockJsonlSink::Stringify(doc));
    return true;
  });

  tracker->Connect(std::move(sink));
  src.Connect(std::move(tracker));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(AirtimeTrackerTest, Changes) {
  TableSource src;
  auto tracker = std::make_unique<AirtimeTracker>(kOption);
  auto sink = std::make_unique<MockJsonlSink>();

  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0003" transport_stream_id="0x0002"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012" test-cc="0">
        <event event_id="0x0003" start_time="1970-01-01 09:00:00"
               duration="00:0:01" running_status="undefined" CA_mode="true" />
        <event event_id="0x0004" start_time="1970-01-01 09:00:01"
               duration="0:00:01" running_status="undefined" CA_mode="true" />
      </EIT>
      <EIT type="pf" version="2" current="true" actual="true"
           service_id="0x0003" transport_stream_id="0x0002"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012" test-cc="1">
        <event event_id="0x0003" start_time="1970-01-01 09:00:00"
               duration="00:0:02" running_status="undefined" CA_mode="true" />
        <event event_id="0x0004" start_time="1970-01-01 09:00:02"
               duration="0:00:01" running_status="undefined" CA_mode="true" />
      </EIT>
      <EIT type="pf" version="3" current="true" actual="true"
           service_id="0x0003" transport_stream_id="0x0002"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012" test-cc="3">
        <event event_id="0x0004" start_time="1970-01-01 09:00:02"
               duration="0:00:01" running_status="undefined" CA_mode="true" />
        <event event_id="0x0005" start_time="1970-01-01 09:00:03"
               duration="00:0:01" running_status="undefined" CA_mode="true" />
      </EIT>
   </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, HandleDocument).WillOnce([](const rapidjson::Document& doc) {
      EXPECT_EQ(R"({"nid":1,"tsid":2,"sid":3,"eid":4,"startTime":1000,"duration":1000})",
          MockJsonlSink::Stringify(doc));
      return true;
    });
    EXPECT_CALL(*sink, HandleDocument).WillOnce([](const rapidjson::Document& doc) {
      EXPECT_EQ(R"({"nid":1,"tsid":2,"sid":3,"eid":4,"startTime":2000,"duration":1000})",
          MockJsonlSink::Stringify(doc));
      return true;
    });
    EXPECT_CALL(*sink, HandleDocument).WillOnce([](const rapidjson::Document& doc) {
      EXPECT_EQ(R"({"nid":1,"tsid":2,"sid":3,"eid":4,"startTime":2000,"duration":1000})",
          MockJsonlSink::Stringify(doc));
      return true;
    });
  }

  tracker->Connect(std::move(sink));
  src.Connect(std::move(tracker));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(AirtimeTrackerTest, EitSidUnmatched) {
  TableSource src;
  auto tracker = std::make_unique<AirtimeTracker>(kOption);
  auto sink = std::make_unique<MockJsonlSink>();

  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x1000" transport_stream_id="0x0002"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012">
        <event event_id="0x0003" start_time="1970-01-01 09:00:00"
               duration="00:0:01" running_status="undefined" CA_mode="true" />
        <event event_id="0x0004" start_time="1970-01-01 09:00:01"
               duration="0:00:01" running_status="undefined" CA_mode="true" />
      </EIT>
   </tsduck>
  )");

  EXPECT_CALL(*sink, HandleDocument).Times(0);

  tracker->Connect(std::move(sink));
  src.Connect(std::move(tracker));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(AirtimeTrackerTest, EventNotFound) {
  TableSource src;
  auto tracker = std::make_unique<AirtimeTracker>(kOption);
  auto sink = std::make_unique<MockJsonlSink>();

  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0003" transport_stream_id="0x0002"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012">
        <event event_id="0x1000" start_time="1970-01-01 09:00:00"
               duration="00:0:01" running_status="undefined" CA_mode="true" />
        <event event_id="0x2000" start_time="1970-01-01 09:00:01"
               duration="0:00:01" running_status="undefined" CA_mode="true" />
      </EIT>
   </tsduck>
  )");

  EXPECT_CALL(*sink, HandleDocument).Times(0);

  tracker->Connect(std::move(sink));
  src.Connect(std::move(tracker));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}
