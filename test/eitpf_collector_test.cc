#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tsduck/tsduck.h>

#include "eitpf_collector.hh"

#include "test_helper.hh"

namespace {
const EitpfCollectorOption kOption{};
}

TEST(EitpfCollectorTest, NoPacket) {
  MockSource src;

  auto collector = std::make_unique<EitpfCollector>(kOption);
  auto sink = std::make_unique<MockJsonlSink>();

  EXPECT_CALL(src, GetNextPacket).WillOnce(testing::Return(false));  // EOF
  EXPECT_CALL(*sink, HandleDocument).Times(0);

  collector->Connect(std::move(sink));
  src.Connect(std::move(collector));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
}

TEST(EitpfCollectorTest, PresentFollowing) {
  TableSource src;
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0003" transport_stream_id="0x0002"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012">
        <event event_id="0x0004" start_time="1970-01-01 09:00:00"
               duration="00:0:01" running_status="undefined" CA_mode="false" />
        <event event_id="0x0005" start_time="1970-01-01 09:00:01"
               duration="0:00:01" running_status="undefined" CA_mode="false" />
      </EIT>
      <EIT type="pf" version="2" current="true" actual="true"
           service_id="0x0003" transport_stream_id="0x0002"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012" test-cc="1">
        <event event_id="0x0004" start_time="1970-01-01 09:00:00"
               duration="00:0:01" running_status="undefined" CA_mode="false" />
        <event event_id="0x0005" start_time="1970-01-01 09:00:01"
               duration="0:00:01" running_status="undefined" CA_mode="false" />
      </EIT>
   </tsduck>
  )");

  EitpfCollectorOption option(kOption);
  option.sids.Add(3);

  auto collector = std::make_unique<EitpfCollector>(option);
  auto sink = std::make_unique<MockJsonlSink>();

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, HandleDocument).WillOnce([](const rapidjson::Document& doc) {
      EXPECT_EQ(1, doc["originalNetworkId"]);
      EXPECT_EQ(2, doc["transportStreamId"]);
      EXPECT_EQ(3, doc["serviceId"]);
      EXPECT_EQ(78, doc["tableId"]);
      EXPECT_EQ(0, doc["sectionNumber"]);
      EXPECT_EQ(1, doc["lastSectionNumber"]);
      EXPECT_EQ(1, doc["segmentLastSectionNumber"]);
      EXPECT_EQ(1, doc["versionNumber"]);
      EXPECT_EQ(1, doc["events"].Size());
      EXPECT_EQ(4, doc["events"][0]["eventId"]);
      EXPECT_EQ(0, doc["events"][0]["startTime"]);
      EXPECT_EQ(1000, doc["events"][0]["duration"]);
      EXPECT_EQ(false, doc["events"][0]["scrambled"]);
      EXPECT_TRUE(doc["events"][0]["descriptors"].Empty());
      return true;
    });
    EXPECT_CALL(*sink, HandleDocument).WillOnce([](const rapidjson::Document& doc) {
      EXPECT_EQ(1, doc["originalNetworkId"]);
      EXPECT_EQ(2, doc["transportStreamId"]);
      EXPECT_EQ(3, doc["serviceId"]);
      EXPECT_EQ(78, doc["tableId"]);
      EXPECT_EQ(1, doc["sectionNumber"]);
      EXPECT_EQ(1, doc["lastSectionNumber"]);
      EXPECT_EQ(1, doc["segmentLastSectionNumber"]);
      EXPECT_EQ(1, doc["versionNumber"]);
      EXPECT_EQ(1, doc["events"].Size());
      EXPECT_EQ(5, doc["events"][0]["eventId"]);
      EXPECT_EQ(1000, doc["events"][0]["startTime"]);
      EXPECT_EQ(1000, doc["events"][0]["duration"]);
      EXPECT_EQ(false, doc["events"][0]["scrambled"]);
      EXPECT_TRUE(doc["events"][0]["descriptors"].Empty());
      return true;
    });
  }

  collector->Connect(std::move(sink));
  src.Connect(std::move(collector));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_FALSE(src.IsEmpty());
}

TEST(EitpfCollectorTest, Present) {
  TableSource src;
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0003" transport_stream_id="0x0002"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012">
        <event event_id="0x0004" start_time="1970-01-01 09:00:00"
               duration="00:0:01" running_status="undefined" CA_mode="false" />
        <event event_id="0x0005" start_time="1970-01-01 09:00:01"
               duration="0:00:01" running_status="undefined" CA_mode="false" />
      </EIT>
      <EIT type="pf" version="2" current="true" actual="true"
           service_id="0x0003" transport_stream_id="0x0002"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012" test-cc="1">
        <event event_id="0x0004" start_time="1970-01-01 09:00:00"
               duration="00:0:01" running_status="undefined" CA_mode="false" />
        <event event_id="0x0005" start_time="1970-01-01 09:00:01"
               duration="0:00:01" running_status="undefined" CA_mode="false" />
      </EIT>
   </tsduck>
  )");

  EitpfCollectorOption option(kOption);
  option.sids.Add(3);
  option.following = false;

  auto collector = std::make_unique<EitpfCollector>(option);
  auto sink = std::make_unique<MockJsonlSink>();

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, HandleDocument).WillOnce([](const rapidjson::Document& doc) {
      EXPECT_EQ(1, doc["originalNetworkId"]);
      EXPECT_EQ(2, doc["transportStreamId"]);
      EXPECT_EQ(3, doc["serviceId"]);
      EXPECT_EQ(78, doc["tableId"]);
      EXPECT_EQ(0, doc["sectionNumber"]);
      EXPECT_EQ(1, doc["lastSectionNumber"]);
      EXPECT_EQ(1, doc["segmentLastSectionNumber"]);
      EXPECT_EQ(1, doc["versionNumber"]);
      EXPECT_EQ(1, doc["events"].Size());
      EXPECT_EQ(4, doc["events"][0]["eventId"]);
      EXPECT_EQ(0, doc["events"][0]["startTime"]);
      EXPECT_EQ(1000, doc["events"][0]["duration"]);
      EXPECT_EQ(false, doc["events"][0]["scrambled"]);
      EXPECT_TRUE(doc["events"][0]["descriptors"].Empty());
      return true;
    });
  }

  collector->Connect(std::move(sink));
  src.Connect(std::move(collector));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_FALSE(src.IsEmpty());
}

TEST(EitpfCollectorTest, Following) {
  TableSource src;
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0003" transport_stream_id="0x0002"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012">
        <event event_id="0x0004" start_time="1970-01-01 09:00:00"
               duration="00:0:01" running_status="undefined" CA_mode="false" />
        <event event_id="0x0005" start_time="1970-01-01 09:00:01"
               duration="0:00:01" running_status="undefined" CA_mode="false" />
      </EIT>
      <EIT type="pf" version="2" current="true" actual="true"
           service_id="0x0003" transport_stream_id="0x0002"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012" test-cc="1">
        <event event_id="0x0004" start_time="1970-01-01 09:00:00"
               duration="00:0:01" running_status="undefined" CA_mode="false" />
        <event event_id="0x0005" start_time="1970-01-01 09:00:01"
               duration="0:00:01" running_status="undefined" CA_mode="false" />
      </EIT>
   </tsduck>
  )");

  EitpfCollectorOption option(kOption);
  option.sids.Add(3);
  option.present = false;

  auto collector = std::make_unique<EitpfCollector>(option);
  auto sink = std::make_unique<MockJsonlSink>();

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, HandleDocument).WillOnce([](const rapidjson::Document& doc) {
      EXPECT_EQ(1, doc["originalNetworkId"]);
      EXPECT_EQ(2, doc["transportStreamId"]);
      EXPECT_EQ(3, doc["serviceId"]);
      EXPECT_EQ(78, doc["tableId"]);
      EXPECT_EQ(1, doc["sectionNumber"]);
      EXPECT_EQ(1, doc["lastSectionNumber"]);
      EXPECT_EQ(1, doc["segmentLastSectionNumber"]);
      EXPECT_EQ(1, doc["versionNumber"]);
      EXPECT_EQ(1, doc["events"].Size());
      EXPECT_EQ(5, doc["events"][0]["eventId"]);
      EXPECT_EQ(1000, doc["events"][0]["startTime"]);
      EXPECT_EQ(1000, doc["events"][0]["duration"]);
      EXPECT_EQ(false, doc["events"][0]["scrambled"]);
      EXPECT_TRUE(doc["events"][0]["descriptors"].Empty());
      return true;
    });
  }

  collector->Connect(std::move(sink));
  src.Connect(std::move(collector));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_FALSE(src.IsEmpty());
}

TEST(EitpfCollectorTest, PresentFollowingStreaming) {
  TableSource src;
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0003" transport_stream_id="0x0002"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012">
        <event event_id="0x0004" start_time="1970-01-01 09:00:00"
               duration="00:0:01" running_status="undefined" CA_mode="false" />
        <event event_id="0x0005" start_time="1970-01-01 09:00:01"
               duration="0:00:01" running_status="undefined" CA_mode="false" />
      </EIT>
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0003" transport_stream_id="0x0002"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012" test-cc="1">
        <event event_id="0x0004" start_time="1970-01-01 09:00:00"
               duration="00:0:01" running_status="undefined" CA_mode="false" />
        <event event_id="0x0005" start_time="1970-01-01 09:00:01"
               duration="0:00:01" running_status="undefined" CA_mode="false" />
      </EIT>
      <EIT type="pf" version="2" current="true" actual="true"
           service_id="0x0003" transport_stream_id="0x0002"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012" test-cc="2">
        <event event_id="0x0004" start_time="1970-01-01 09:00:00"
               duration="00:0:01" running_status="undefined" CA_mode="false" />
        <event event_id="0x0005" start_time="1970-01-01 09:00:01"
               duration="0:00:01" running_status="undefined" CA_mode="false" />
      </EIT>
   </tsduck>
  )");

  EitpfCollectorOption option(kOption);
  option.streaming = true;

  auto collector = std::make_unique<EitpfCollector>(option);
  auto sink = std::make_unique<MockJsonlSink>();

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, HandleDocument).WillOnce([](const rapidjson::Document& doc) {
      EXPECT_EQ(1, doc["originalNetworkId"]);
      EXPECT_EQ(2, doc["transportStreamId"]);
      EXPECT_EQ(3, doc["serviceId"]);
      EXPECT_EQ(78, doc["tableId"]);
      EXPECT_EQ(0, doc["sectionNumber"]);
      EXPECT_EQ(1, doc["lastSectionNumber"]);
      EXPECT_EQ(1, doc["segmentLastSectionNumber"]);
      EXPECT_EQ(1, doc["versionNumber"]);
      EXPECT_EQ(1, doc["events"].Size());
      EXPECT_EQ(4, doc["events"][0]["eventId"]);
      EXPECT_EQ(0, doc["events"][0]["startTime"]);
      EXPECT_EQ(1000, doc["events"][0]["duration"]);
      EXPECT_EQ(false, doc["events"][0]["scrambled"]);
      EXPECT_TRUE(doc["events"][0]["descriptors"].Empty());
      return true;
    });
    EXPECT_CALL(*sink, HandleDocument).WillOnce([](const rapidjson::Document& doc) {
      EXPECT_EQ(1, doc["originalNetworkId"]);
      EXPECT_EQ(2, doc["transportStreamId"]);
      EXPECT_EQ(3, doc["serviceId"]);
      EXPECT_EQ(78, doc["tableId"]);
      EXPECT_EQ(1, doc["sectionNumber"]);
      EXPECT_EQ(1, doc["lastSectionNumber"]);
      EXPECT_EQ(1, doc["segmentLastSectionNumber"]);
      EXPECT_EQ(1, doc["versionNumber"]);
      EXPECT_EQ(1, doc["events"].Size());
      EXPECT_EQ(5, doc["events"][0]["eventId"]);
      EXPECT_EQ(1000, doc["events"][0]["startTime"]);
      EXPECT_EQ(1000, doc["events"][0]["duration"]);
      EXPECT_EQ(false, doc["events"][0]["scrambled"]);
      EXPECT_TRUE(doc["events"][0]["descriptors"].Empty());
      return true;
    });
    EXPECT_CALL(*sink, HandleDocument).WillOnce([](const rapidjson::Document& doc) {
      EXPECT_EQ(1, doc["originalNetworkId"]);
      EXPECT_EQ(2, doc["transportStreamId"]);
      EXPECT_EQ(3, doc["serviceId"]);
      EXPECT_EQ(78, doc["tableId"]);
      EXPECT_EQ(0, doc["sectionNumber"]);
      EXPECT_EQ(1, doc["lastSectionNumber"]);
      EXPECT_EQ(1, doc["segmentLastSectionNumber"]);
      EXPECT_EQ(2, doc["versionNumber"]);
      EXPECT_EQ(1, doc["events"].Size());
      EXPECT_EQ(4, doc["events"][0]["eventId"]);
      EXPECT_EQ(0, doc["events"][0]["startTime"]);
      EXPECT_EQ(1000, doc["events"][0]["duration"]);
      EXPECT_EQ(false, doc["events"][0]["scrambled"]);
      EXPECT_TRUE(doc["events"][0]["descriptors"].Empty());
      return true;
    });
    EXPECT_CALL(*sink, HandleDocument).WillOnce([](const rapidjson::Document& doc) {
      EXPECT_EQ(1, doc["originalNetworkId"]);
      EXPECT_EQ(2, doc["transportStreamId"]);
      EXPECT_EQ(3, doc["serviceId"]);
      EXPECT_EQ(78, doc["tableId"]);
      EXPECT_EQ(1, doc["sectionNumber"]);
      EXPECT_EQ(1, doc["lastSectionNumber"]);
      EXPECT_EQ(1, doc["segmentLastSectionNumber"]);
      EXPECT_EQ(2, doc["versionNumber"]);
      EXPECT_EQ(1, doc["events"].Size());
      EXPECT_EQ(5, doc["events"][0]["eventId"]);
      EXPECT_EQ(1000, doc["events"][0]["startTime"]);
      EXPECT_EQ(1000, doc["events"][0]["duration"]);
      EXPECT_EQ(false, doc["events"][0]["scrambled"]);
      EXPECT_TRUE(doc["events"][0]["descriptors"].Empty());
      return true;
    });
  }

  collector->Connect(std::move(sink));
  src.Connect(std::move(collector));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(EitpfCollectorTest, PresentStreaming) {
  TableSource src;
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0003" transport_stream_id="0x0002"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012">
        <event event_id="0x0004" start_time="1970-01-01 09:00:00"
               duration="00:0:01" running_status="undefined" CA_mode="false" />
        <event event_id="0x0005" start_time="1970-01-01 09:00:01"
               duration="0:00:01" running_status="undefined" CA_mode="false" />
      </EIT>
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0003" transport_stream_id="0x0002"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012" test-cc="1">
        <event event_id="0x0004" start_time="1970-01-01 09:00:00"
               duration="00:0:01" running_status="undefined" CA_mode="false" />
        <event event_id="0x0005" start_time="1970-01-01 09:00:01"
               duration="0:00:01" running_status="undefined" CA_mode="false" />
      </EIT>
      <EIT type="pf" version="2" current="true" actual="true"
           service_id="0x0003" transport_stream_id="0x0002"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012" test-cc="2">
        <event event_id="0x0004" start_time="1970-01-01 09:00:00"
               duration="00:0:01" running_status="undefined" CA_mode="false" />
        <event event_id="0x0005" start_time="1970-01-01 09:00:01"
               duration="0:00:01" running_status="undefined" CA_mode="false" />
      </EIT>
   </tsduck>
  )");

  EitpfCollectorOption option(kOption);
  option.streaming = true;
  option.following = false;

  auto collector = std::make_unique<EitpfCollector>(option);
  auto sink = std::make_unique<MockJsonlSink>();

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, HandleDocument).WillOnce([](const rapidjson::Document& doc) {
      EXPECT_EQ(1, doc["originalNetworkId"]);
      EXPECT_EQ(2, doc["transportStreamId"]);
      EXPECT_EQ(3, doc["serviceId"]);
      EXPECT_EQ(78, doc["tableId"]);
      EXPECT_EQ(0, doc["sectionNumber"]);
      EXPECT_EQ(1, doc["lastSectionNumber"]);
      EXPECT_EQ(1, doc["segmentLastSectionNumber"]);
      EXPECT_EQ(1, doc["versionNumber"]);
      EXPECT_EQ(1, doc["events"].Size());
      EXPECT_EQ(4, doc["events"][0]["eventId"]);
      EXPECT_EQ(0, doc["events"][0]["startTime"]);
      EXPECT_EQ(1000, doc["events"][0]["duration"]);
      EXPECT_EQ(false, doc["events"][0]["scrambled"]);
      EXPECT_TRUE(doc["events"][0]["descriptors"].Empty());
      return true;
    });
    EXPECT_CALL(*sink, HandleDocument).WillOnce([](const rapidjson::Document& doc) {
      EXPECT_EQ(1, doc["originalNetworkId"]);
      EXPECT_EQ(2, doc["transportStreamId"]);
      EXPECT_EQ(3, doc["serviceId"]);
      EXPECT_EQ(78, doc["tableId"]);
      EXPECT_EQ(0, doc["sectionNumber"]);
      EXPECT_EQ(1, doc["lastSectionNumber"]);
      EXPECT_EQ(1, doc["segmentLastSectionNumber"]);
      EXPECT_EQ(2, doc["versionNumber"]);
      EXPECT_EQ(1, doc["events"].Size());
      EXPECT_EQ(4, doc["events"][0]["eventId"]);
      EXPECT_EQ(0, doc["events"][0]["startTime"]);
      EXPECT_EQ(1000, doc["events"][0]["duration"]);
      EXPECT_EQ(false, doc["events"][0]["scrambled"]);
      EXPECT_TRUE(doc["events"][0]["descriptors"].Empty());
      return true;
    });
  }

  collector->Connect(std::move(sink));
  src.Connect(std::move(collector));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(EitpfCollectorTest, FollowingStreaming) {
  TableSource src;
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0003" transport_stream_id="0x0002"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012">
        <event event_id="0x0004" start_time="1970-01-01 09:00:00"
               duration="00:0:01" running_status="undefined" CA_mode="false" />
        <event event_id="0x0005" start_time="1970-01-01 09:00:01"
               duration="0:00:01" running_status="undefined" CA_mode="false" />
      </EIT>
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0003" transport_stream_id="0x0002"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012" test-cc="1">
        <event event_id="0x0004" start_time="1970-01-01 09:00:00"
               duration="00:0:01" running_status="undefined" CA_mode="false" />
        <event event_id="0x0005" start_time="1970-01-01 09:00:01"
               duration="0:00:01" running_status="undefined" CA_mode="false" />
      </EIT>
      <EIT type="pf" version="2" current="true" actual="true"
           service_id="0x0003" transport_stream_id="0x0002"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012" test-cc="2">
        <event event_id="0x0004" start_time="1970-01-01 09:00:00"
               duration="00:0:01" running_status="undefined" CA_mode="false" />
        <event event_id="0x0005" start_time="1970-01-01 09:00:01"
               duration="0:00:01" running_status="undefined" CA_mode="false" />
      </EIT>
   </tsduck>
  )");

  EitpfCollectorOption option(kOption);
  option.streaming = true;
  option.present = false;

  auto collector = std::make_unique<EitpfCollector>(option);
  auto sink = std::make_unique<MockJsonlSink>();

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, HandleDocument).WillOnce([](const rapidjson::Document& doc) {
      EXPECT_EQ(1, doc["originalNetworkId"]);
      EXPECT_EQ(2, doc["transportStreamId"]);
      EXPECT_EQ(3, doc["serviceId"]);
      EXPECT_EQ(78, doc["tableId"]);
      EXPECT_EQ(1, doc["sectionNumber"]);
      EXPECT_EQ(1, doc["lastSectionNumber"]);
      EXPECT_EQ(1, doc["segmentLastSectionNumber"]);
      EXPECT_EQ(1, doc["versionNumber"]);
      EXPECT_EQ(1, doc["events"].Size());
      EXPECT_EQ(5, doc["events"][0]["eventId"]);
      EXPECT_EQ(1000, doc["events"][0]["startTime"]);
      EXPECT_EQ(1000, doc["events"][0]["duration"]);
      EXPECT_EQ(false, doc["events"][0]["scrambled"]);
      EXPECT_TRUE(doc["events"][0]["descriptors"].Empty());
      return true;
    });
    EXPECT_CALL(*sink, HandleDocument).WillOnce([](const rapidjson::Document& doc) {
      EXPECT_EQ(1, doc["originalNetworkId"]);
      EXPECT_EQ(2, doc["transportStreamId"]);
      EXPECT_EQ(3, doc["serviceId"]);
      EXPECT_EQ(78, doc["tableId"]);
      EXPECT_EQ(1, doc["sectionNumber"]);
      EXPECT_EQ(1, doc["lastSectionNumber"]);
      EXPECT_EQ(1, doc["segmentLastSectionNumber"]);
      EXPECT_EQ(2, doc["versionNumber"]);
      EXPECT_EQ(1, doc["events"].Size());
      EXPECT_EQ(5, doc["events"][0]["eventId"]);
      EXPECT_EQ(1000, doc["events"][0]["startTime"]);
      EXPECT_EQ(1000, doc["events"][0]["duration"]);
      EXPECT_EQ(false, doc["events"][0]["scrambled"]);
      EXPECT_TRUE(doc["events"][0]["descriptors"].Empty());
      return true;
    });
  }

  collector->Connect(std::move(sink));
  src.Connect(std::move(collector));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(EitpfCollectorTest, Sids) {
  TableSource src;
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0003" transport_stream_id="0x0002"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012">
        <event event_id="0x0004" start_time="1970-01-01 09:00:00"
               duration="00:0:01" running_status="undefined" CA_mode="false" />
        <event event_id="0x0005" start_time="1970-01-01 09:00:01"
               duration="0:00:01" running_status="undefined" CA_mode="false" />
      </EIT>
      <EIT type="pf" version="2" current="true" actual="true"
           service_id="0x0010" transport_stream_id="0x0002"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012" test-cc="1">
        <event event_id="0x0004" start_time="1970-01-01 09:00:00"
               duration="00:0:01" running_status="undefined" CA_mode="false" />
        <event event_id="0x0005" start_time="1970-01-01 09:00:01"
               duration="0:00:01" running_status="undefined" CA_mode="false" />
      </EIT>
   </tsduck>
  )");

  EitpfCollectorOption option(kOption);
  option.sids.Add(3);
  option.streaming = true;

  auto collector = std::make_unique<EitpfCollector>(option);
  auto sink = std::make_unique<MockJsonlSink>();

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, HandleDocument).WillOnce([](const rapidjson::Document& doc) {
      EXPECT_EQ(1, doc["originalNetworkId"]);
      EXPECT_EQ(2, doc["transportStreamId"]);
      EXPECT_EQ(3, doc["serviceId"]);
      EXPECT_EQ(78, doc["tableId"]);
      EXPECT_EQ(0, doc["sectionNumber"]);
      EXPECT_EQ(1, doc["lastSectionNumber"]);
      EXPECT_EQ(1, doc["segmentLastSectionNumber"]);
      EXPECT_EQ(1, doc["versionNumber"]);
      EXPECT_EQ(1, doc["events"].Size());
      EXPECT_EQ(4, doc["events"][0]["eventId"]);
      EXPECT_EQ(0, doc["events"][0]["startTime"]);
      EXPECT_EQ(1000, doc["events"][0]["duration"]);
      EXPECT_EQ(false, doc["events"][0]["scrambled"]);
      EXPECT_TRUE(doc["events"][0]["descriptors"].Empty());
      return true;
    });
    EXPECT_CALL(*sink, HandleDocument).WillOnce([](const rapidjson::Document& doc) {
      EXPECT_EQ(1, doc["originalNetworkId"]);
      EXPECT_EQ(2, doc["transportStreamId"]);
      EXPECT_EQ(3, doc["serviceId"]);
      EXPECT_EQ(78, doc["tableId"]);
      EXPECT_EQ(1, doc["sectionNumber"]);
      EXPECT_EQ(1, doc["lastSectionNumber"]);
      EXPECT_EQ(1, doc["segmentLastSectionNumber"]);
      EXPECT_EQ(1, doc["versionNumber"]);
      EXPECT_EQ(1, doc["events"].Size());
      EXPECT_EQ(5, doc["events"][0]["eventId"]);
      EXPECT_EQ(1000, doc["events"][0]["startTime"]);
      EXPECT_EQ(1000, doc["events"][0]["duration"]);
      EXPECT_EQ(false, doc["events"][0]["scrambled"]);
      EXPECT_TRUE(doc["events"][0]["descriptors"].Empty());
      return true;
    });
  }

  collector->Connect(std::move(sink));
  src.Connect(std::move(collector));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}
