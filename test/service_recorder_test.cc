#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tsduck/tsduck.h>

#include "ring_file_sink.hh"
#include "service_recorder.hh"

#include "test_helper.hh"

namespace {
constexpr size_t kNumBuffers = 2;
constexpr size_t kNumChunks = 2;
constexpr size_t kChunkSize = RingFileSink::kBufferSize * kNumBuffers;
constexpr uint64_t kRingSize = kChunkSize * kNumChunks;
const ServiceRecorderOption kOption { "/dev/null", 3, kChunkSize, kNumChunks };
}

TEST(ServiceRecorderTest, NoPacket) {
  ServiceRecorderOption option = kOption;

  MockSource src;
  auto file = std::make_unique<MockFile>();
  auto json_sink = std::make_unique<MockJsonlSink>();

  {
    testing::InSequence seq;
    EXPECT_CALL(*json_sink, HandleDocument)
        .WillOnce([](const rapidjson::Document& doc) {
          EXPECT_EQ(
              R"({"type":"start"})",
              MockJsonlSink::Stringify(doc));
          return true;
        });
    EXPECT_CALL(*json_sink, HandleDocument)
        .WillOnce([](const rapidjson::Document& doc) {
          EXPECT_EQ(
              R"({"type":"stop","data":{"reset":false}})",
              MockJsonlSink::Stringify(doc));
          return true;
        });
  }

  EXPECT_CALL(src, GetNextPacket).WillOnce(testing::Return(false));  // EOF

  auto ring = std::make_unique<RingFileSink>(
      std::move(file), option.chunk_size, option.num_chunks);
  auto recorder = std::make_unique<ServiceRecorder>(kOption);
  recorder->ServiceRecorder::Connect(std::move(ring));
  recorder->JsonlSource::Connect(std::move(json_sink));
  src.Connect(std::move(recorder));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
}

TEST(ServiceRecorderTest, EventStart) {
  ServiceRecorderOption option = kOption;

  TableSource src;
  auto file = std::make_unique<MockFile>();
  auto json_sink = std::make_unique<MockJsonlSink>();

  // TDT tables are used for emulating PES and PCR packets.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x0002"
           test-pid="0x0000">
        <service service_id="0x0003" program_map_PID="0x0101" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0003" PCR_PID="0x901"
           test-pid="0x0101">
        <component elementary_PID="0x0301" stream_type="0x02" />
      </PMT>
      <TOT UTC_time="2021-01-01 00:00:00" test-pid="0x0014" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0901"
           test-pcr="0" />
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0003" transport_stream_id="0x0002"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012">
        <event event_id="4" start_time="2021-01-01 00:00:00"
               duration="00:00:01" running_status="undefined" CA_mode="true" />
        <event event_id="5" start_time="2021-01-01 00:00:01"
               duration="01:00:00" running_status="undefined" CA_mode="true" />
      </EIT>
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0301" />
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*json_sink, HandleDocument)
        .WillOnce([](const rapidjson::Document& doc) {
          EXPECT_EQ(
              R"({"type":"start"})",
              MockJsonlSink::Stringify(doc));
          return true;
        });
    EXPECT_CALL(*json_sink, HandleDocument)
        .WillOnce([](const rapidjson::Document& doc) {
          EXPECT_EQ(
              R"({"type":"chunk","data":{"chunk":{)"
                R"("timestamp":1609426800000,"pos":0)"
              R"(}}})",
              MockJsonlSink::Stringify(doc));
          return true;
        });
    EXPECT_CALL(*json_sink, HandleDocument)
        .WillOnce([](const rapidjson::Document& doc) {
          EXPECT_EQ(
              R"({"type":"event-start","data":{)"
                R"("originalNetworkId":1,)"
                R"("transportStreamId":2,)"
                R"("serviceId":3,)"
                R"("event":{)"
                  R"("eventId":4,)"
                  R"("startTime":1609426800000,)"
                  R"("duration":1000,)"
                  R"("scrambled":true,)"
                  R"("descriptors":[])"
                R"(},)"
                R"("record":{)"
                  R"("timestamp":1609426800000,)"
                  R"("pos":0)"
                R"(})"
              R"(}})",
              MockJsonlSink::Stringify(doc));
          return true;
        });
    EXPECT_CALL(*json_sink, HandleDocument)
        .WillOnce([](const rapidjson::Document& doc) {
          EXPECT_EQ(
              R"({"type":"stop","data":{"reset":false}})",
              MockJsonlSink::Stringify(doc));
          return true;
        });
  }

  EXPECT_CALL(*file, Write).WillRepeatedly(testing::Return(RingFileSink::kBufferSize));
  EXPECT_CALL(*file, Sync).WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*file, Trunc).WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*file, Seek).WillRepeatedly(testing::Return(0));

  auto ring = std::make_unique<RingFileSink>(
      std::move(file), option.chunk_size, option.num_chunks);
  auto recorder = std::make_unique<ServiceRecorder>(kOption);
  recorder->ServiceRecorder::Connect(std::move(ring));
  recorder->JsonlSource::Connect(std::move(json_sink));
  src.Connect(std::move(recorder));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(ServiceRecorderTest, EventProgress) {
  ServiceRecorderOption option = kOption;

  TableSource src;
  auto ring_sink = std::make_unique<MockRingSink>(option.chunk_size, option.num_chunks);
  auto json_sink = std::make_unique<MockJsonlSink>();

  // TDT tables are used for emulating PES and PCR packets.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x0002"
           test-pid="0x0000">
        <service service_id="0x0003" program_map_PID="0x0101" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0003" PCR_PID="0x901"
           test-pid="0x0101">
        <component elementary_PID="0x0301" stream_type="0x02" />
      </PMT>
      <TOT UTC_time="2021-01-01 00:00:00" test-pid="0x0014" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0901"
           test-pcr="0" />
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0003" transport_stream_id="0x0002"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012">
        <event event_id="4" start_time="2021-01-01 00:00:00"
               duration="00:00:01" running_status="undefined" CA_mode="true" />
        <event event_id="5" start_time="2021-01-01 00:00:01"
               duration="01:00:00" running_status="undefined" CA_mode="true" />
      </EIT>
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0FFD" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0301" />
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*ring_sink, Start)
        .WillOnce(testing::Return(true));
    EXPECT_CALL(*json_sink, HandleDocument)
        .WillOnce([](const rapidjson::Document& doc) {
          EXPECT_EQ(
              R"({"type":"start"})",
              MockJsonlSink::Stringify(doc));
          return true;
        });
    EXPECT_CALL(*json_sink, HandleDocument)
        .WillOnce([](const rapidjson::Document& doc) {
          EXPECT_EQ(
              R"({"type":"chunk","data":{"chunk":{)"
                R"("timestamp":1609426800000,"pos":0)"
              R"(}}})",
              MockJsonlSink::Stringify(doc));
          return true;
        });
    EXPECT_CALL(*json_sink, HandleDocument)
        .WillOnce([](const rapidjson::Document& doc) {
          EXPECT_EQ(
              R"({"type":"event-start","data":{)"
                R"("originalNetworkId":1,)"
                R"("transportStreamId":2,)"
                R"("serviceId":3,)"
                R"("event":{)"
                  R"("eventId":4,)"
                  R"("startTime":1609426800000,)"
                  R"("duration":1000,)"
                  R"("scrambled":true,)"
                  R"("descriptors":[])"
                R"(},)"
                R"("record":{)"
                  R"("timestamp":1609426800000,)"
                  R"("pos":0)"
                R"(})"
              R"(}})",
              MockJsonlSink::Stringify(doc));
          return true;
        });
    EXPECT_CALL(*json_sink, HandleDocument)
        .WillOnce([](const rapidjson::Document& doc) {
          EXPECT_EQ(
              R"({"type":"event-update","data":{)"
                R"("originalNetworkId":1,)"
                R"("transportStreamId":2,)"
                R"("serviceId":3,)"
                R"("event":{)"
                  R"("eventId":4,)"
                  R"("startTime":1609426800000,)"
                  R"("duration":1000,)"
                  R"("scrambled":true,)"
                  R"("descriptors":[])"
                R"(},)"
                R"("record":{)"
                  R"("timestamp":1609426800000,)"
                  R"("pos":16384)"
                R"(})"
              R"(}})",
              MockJsonlSink::Stringify(doc));
          return true;
        });
    EXPECT_CALL(*json_sink, HandleDocument)
        .WillOnce([](const rapidjson::Document& doc) {
          EXPECT_EQ(
              R"({"type":"chunk","data":{"chunk":{)"
                R"("timestamp":1609426800000,"pos":16384)"
              R"(}}})",
              MockJsonlSink::Stringify(doc));
          return true;
        });
    EXPECT_CALL(*json_sink, HandleDocument)
        .WillOnce([](const rapidjson::Document& doc) {
          EXPECT_EQ(
              R"({"type":"event-update","data":{)"
                R"("originalNetworkId":1,)"
                R"("transportStreamId":2,)"
                R"("serviceId":3,)"
                R"("event":{)"
                  R"("eventId":4,)"
                  R"("startTime":1609426800000,)"
                  R"("duration":1000,)"
                  R"("scrambled":true,)"
                  R"("descriptors":[])"
                R"(},)"
                R"("record":{)"
                  R"("timestamp":1609426800000,)"
                  R"("pos":0)"
                R"(})"
              R"(}})",
              MockJsonlSink::Stringify(doc));
          return true;
        });
    EXPECT_CALL(*json_sink, HandleDocument)
        .WillOnce([](const rapidjson::Document& doc) {
          EXPECT_EQ(
              R"({"type":"chunk","data":{"chunk":{)"
                R"("timestamp":1609426800000,"pos":0)"
              R"(}}})",
              MockJsonlSink::Stringify(doc));
          return true;
        });
    EXPECT_CALL(*json_sink, HandleDocument)
        .WillOnce([](const rapidjson::Document& doc) {
          EXPECT_EQ(
              R"({"type":"stop","data":{"reset":false}})",
              MockJsonlSink::Stringify(doc));
          return true;
        });
    EXPECT_CALL(*ring_sink, End).WillOnce(testing::Return());
  }

  auto recorder = std::make_unique<ServiceRecorder>(kOption);
  recorder->ServiceRecorder::Connect(std::move(ring_sink));
  recorder->JsonlSource::Connect(std::move(json_sink));
  src.Connect(std::move(recorder));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(ServiceRecorderTest, EventEnd) {
  ServiceRecorderOption option = kOption;

  TableSource src;
  auto file = std::make_unique<MockFile>();
  auto json_sink = std::make_unique<MockJsonlSink>();

  // TDT tables are used for emulating PES and PCR packets.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x0002"
           test-pid="0x0000">
        <service service_id="0x0003" program_map_PID="0x0101" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0003" PCR_PID="0x901"
           test-pid="0x0101">
        <component elementary_PID="0x0301" stream_type="0x02" />
      </PMT>
      <TOT UTC_time="2021-01-01 00:00:00" test-pid="0x0014" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0901" test-pcr="0" />
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0003" transport_stream_id="0x0002"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012">
        <event event_id="4" start_time="2021-01-01 00:00:00"
               duration="00:00:01" running_status="undefined" CA_mode="true" />
        <event event_id="5" start_time="2021-01-01 00:00:01"
               duration="00:00:01" running_status="undefined" CA_mode="true" />
      </EIT>
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0901" test-pcr="1" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0301" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0901" test-pcr="27000000" />
      <EIT type="pf" version="2" current="true" actual="true"
           service_id="0x0003" transport_stream_id="0x0002"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012" test-cc="1">
        <event event_id="5" start_time="2021-01-01 00:00:01"
               duration="00:00:01" running_status="undefined" CA_mode="true" />
        <event event_id="6" start_time="2021-01-01 00:00:02"
               duration="00:00:01" running_status="undefined" CA_mode="true" />
      </EIT>
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*json_sink, HandleDocument)
        .WillOnce([](const rapidjson::Document& doc) {
          EXPECT_EQ(
              R"({"type":"start"})",
              MockJsonlSink::Stringify(doc));
          return true;
        });
    EXPECT_CALL(*json_sink, HandleDocument)
        .WillOnce([](const rapidjson::Document& doc) {
          EXPECT_EQ(
              R"({"type":"chunk","data":{"chunk":{)"
                R"("timestamp":1609426800000,"pos":0)"
              R"(}}})",
              MockJsonlSink::Stringify(doc));
          return true;
        });
    EXPECT_CALL(*json_sink, HandleDocument)
        .WillOnce([](const rapidjson::Document& doc) {
          EXPECT_EQ(
              R"({"type":"event-start","data":{)"
                R"("originalNetworkId":1,)"
                R"("transportStreamId":2,)"
                R"("serviceId":3,)"
                R"("event":{)"
                  R"("eventId":4,)"
                  R"("startTime":1609426800000,)"
                  R"("duration":1000,)"
                  R"("scrambled":true,)"
                  R"("descriptors":[])"
                R"(},)"
                R"("record":{)"
                  R"("timestamp":1609426800000,)"
                  R"("pos":0)"
                R"(})"
              R"(}})",
              MockJsonlSink::Stringify(doc));
          return true;
        });
    EXPECT_CALL(*json_sink, HandleDocument)
        .WillOnce([](const rapidjson::Document& doc) {
          EXPECT_EQ(
              R"({"type":"event-end","data":{)"
                R"("originalNetworkId":1,)"
                R"("transportStreamId":2,)"
                R"("serviceId":3,)"
                R"("event":{)"
                  R"("eventId":4,)"
                  R"("startTime":1609426800000,)"
                  R"("duration":1000,)"
                  R"("scrambled":true,)"
                  R"("descriptors":[])"
                R"(},)"
                R"("record":{)"
                  R"("timestamp":1609426801000,)"
                  R"("pos":376)"
                R"(})"
              R"(}})",
              MockJsonlSink::Stringify(doc));
          return true;
        });
    EXPECT_CALL(*json_sink, HandleDocument)
        .WillOnce([](const rapidjson::Document& doc) {
          EXPECT_EQ(
              R"({"type":"event-start","data":{)"
                R"("originalNetworkId":1,)"
                R"("transportStreamId":2,)"
                R"("serviceId":3,)"
                R"("event":{)"
                  R"("eventId":5,)"
                  R"("startTime":1609426801000,)"
                  R"("duration":1000,)"
                  R"("scrambled":true,)"
                  R"("descriptors":[])"
                R"(},)"
                R"("record":{)"
                  R"("timestamp":1609426801000,)"
                  R"("pos":376)"
                R"(})"
              R"(}})",
              MockJsonlSink::Stringify(doc));
          return true;
        });
    EXPECT_CALL(*json_sink, HandleDocument)
        .WillOnce([](const rapidjson::Document& doc) {
          EXPECT_EQ(
              R"({"type":"stop","data":{"reset":false}})",
              MockJsonlSink::Stringify(doc));
          return true;
        });
  }

  EXPECT_CALL(*file, Write).WillRepeatedly(testing::Return(RingFileSink::kBufferSize));
  EXPECT_CALL(*file, Sync).WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*file, Trunc).WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*file, Seek).WillRepeatedly(testing::Return(0));

  auto ring = std::make_unique<RingFileSink>(
      std::move(file), option.chunk_size, option.num_chunks);
  auto recorder = std::make_unique<ServiceRecorder>(kOption);
  recorder->ServiceRecorder::Connect(std::move(ring));
  recorder->JsonlSource::Connect(std::move(json_sink));
  src.Connect(std::move(recorder));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(ServiceRecorderTest, EventStartBeforeEventEnd) {
  ServiceRecorderOption option = kOption;

  TableSource src;
  auto file = std::make_unique<MockFile>();
  auto json_sink = std::make_unique<MockJsonlSink>();

  // TDT tables are used for emulating PES and PCR packets.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x0002"
           test-pid="0x0000">
        <service service_id="0x0003" program_map_PID="0x0101" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0003" PCR_PID="0x901"
           test-pid="0x0101">
        <component elementary_PID="0x0301" stream_type="0x02" />
      </PMT>
      <TOT UTC_time="2021-01-01 00:00:00" test-pid="0x0014" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0901"
           test-pcr="0" />
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0003" transport_stream_id="0x0002"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012">
        <event event_id="4" start_time="2021-01-01 00:00:00"
               duration="00:00:01" running_status="undefined" CA_mode="true" />
        <event event_id="5" start_time="2021-01-01 00:00:01"
               duration="01:00:00" running_status="undefined" CA_mode="true" />
      </EIT>
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0301" />
      <EIT type="pf" version="2" current="true" actual="true"
           service_id="0x0003" transport_stream_id="0x0002"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012" test-cc="1">
        <event event_id="6" start_time="2021-01-01 00:00:00"
               duration="00:00:01" running_status="undefined" CA_mode="true" />
        <event event_id="7" start_time="2021-01-01 00:00:01"
               duration="01:00:00" running_status="undefined" CA_mode="true" />
      </EIT>
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*json_sink, HandleDocument)
        .WillOnce([](const rapidjson::Document& doc) {
          EXPECT_EQ(
              R"({"type":"start"})",
              MockJsonlSink::Stringify(doc));
          return true;
        });
    EXPECT_CALL(*json_sink, HandleDocument)
        .WillOnce([](const rapidjson::Document& doc) {
          EXPECT_EQ(
              R"({"type":"chunk","data":{"chunk":{)"
                R"("timestamp":1609426800000,"pos":0)"
              R"(}}})",
              MockJsonlSink::Stringify(doc));
          return true;
        });
    EXPECT_CALL(*json_sink, HandleDocument)
        .WillOnce([](const rapidjson::Document& doc) {
          EXPECT_EQ(
              R"({"type":"event-start","data":{)"
                R"("originalNetworkId":1,)"
                R"("transportStreamId":2,)"
                R"("serviceId":3,)"
                R"("event":{)"
                  R"("eventId":4,)"
                  R"("startTime":1609426800000,)"
                  R"("duration":1000,)"
                  R"("scrambled":true,)"
                  R"("descriptors":[])"
                R"(},)"
                R"("record":{)"
                  R"("timestamp":1609426800000,)"
                  R"("pos":0)"
                R"(})"
              R"(}})",
              MockJsonlSink::Stringify(doc));
          return true;
        });
    EXPECT_CALL(*json_sink, HandleDocument)
        .WillOnce([](const rapidjson::Document& doc) {
          EXPECT_EQ(
              R"({"type":"event-end","data":{)"
                R"("originalNetworkId":1,)"
                R"("transportStreamId":2,)"
                R"("serviceId":3,)"
                R"("event":{)"
                  R"("eventId":4,)"
                  R"("startTime":1609426800000,)"
                  R"("duration":1000,)"
                  R"("scrambled":true,)"
                  R"("descriptors":[])"
                R"(},)"
                R"("record":{)"
                  R"("timestamp":1609426800000,)"
                  R"("pos":188)"
                R"(})"
              R"(}})",
              MockJsonlSink::Stringify(doc));
          return true;
        });
    EXPECT_CALL(*json_sink, HandleDocument)
        .WillOnce([](const rapidjson::Document& doc) {
          EXPECT_EQ(
              R"({"type":"event-start","data":{)"
                R"("originalNetworkId":1,)"
                R"("transportStreamId":2,)"
                R"("serviceId":3,)"
                R"("event":{)"
                  R"("eventId":6,)"
                  R"("startTime":1609426800000,)"
                  R"("duration":1000,)"
                  R"("scrambled":true,)"
                  R"("descriptors":[])"
                R"(},)"
                R"("record":{)"
                  R"("timestamp":1609426800000,)"
                  R"("pos":188)"
                R"(})"
              R"(}})",
              MockJsonlSink::Stringify(doc));
          return true;
        });
    EXPECT_CALL(*json_sink, HandleDocument)
        .WillOnce([](const rapidjson::Document& doc) {
          EXPECT_EQ(
              R"({"type":"stop","data":{"reset":false}})",
              MockJsonlSink::Stringify(doc));
          return true;
        });
  }

  EXPECT_CALL(*file, Write).WillRepeatedly(testing::Return(RingFileSink::kBufferSize));
  EXPECT_CALL(*file, Sync).WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*file, Trunc).WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*file, Seek).WillRepeatedly(testing::Return(0));

  auto ring = std::make_unique<RingFileSink>(
      std::move(file), option.chunk_size, option.num_chunks);
  auto recorder = std::make_unique<ServiceRecorder>(kOption);
  recorder->ServiceRecorder::Connect(std::move(ring));
  recorder->JsonlSource::Connect(std::move(json_sink));
  src.Connect(std::move(recorder));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(ServiceRecorderTest, FirstEventAlreadyEnded) {
  ServiceRecorderOption option = kOption;

  TableSource src;
  auto file = std::make_unique<MockFile>();
  auto json_sink = std::make_unique<MockJsonlSink>();

  // TDT tables are used for emulating PES and PCR packets.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x0002"
           test-pid="0x0000">
        <service service_id="0x0003" program_map_PID="0x0101" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0003" PCR_PID="0x901"
           test-pid="0x0101">
        <component elementary_PID="0x0301" stream_type="0x02" />
      </PMT>
      <TOT UTC_time="2021-01-01 00:00:01" test-pid="0x0014" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0901"
           test-pcr="0" />
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0003" transport_stream_id="0x0002"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012">
        <event event_id="4" start_time="2021-01-01 00:00:00"
               duration="00:00:01" running_status="undefined" CA_mode="true" />
        <event event_id="5" start_time="2021-01-01 00:00:01"
               duration="00:00:01" running_status="undefined" CA_mode="true" />
      </EIT>
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0301" />
      <EIT type="pf" version="2" current="true" actual="true"
           service_id="0x0003" transport_stream_id="0x0002"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012" test-cc="1">
        <event event_id="5" start_time="2021-01-01 00:00:01"
               duration="00:00:01" running_status="undefined" CA_mode="true" />
        <event event_id="6" start_time="2021-01-01 00:00:02"
               duration="00:00:01" running_status="undefined" CA_mode="true" />
      </EIT>
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*json_sink, HandleDocument)
        .WillOnce([](const rapidjson::Document& doc) {
          EXPECT_EQ(
              R"({"type":"start"})",
              MockJsonlSink::Stringify(doc));
          return true;
        });
    EXPECT_CALL(*json_sink, HandleDocument)
        .WillOnce([](const rapidjson::Document& doc) {
          EXPECT_EQ(
              R"({"type":"chunk","data":{"chunk":{)"
                R"("timestamp":1609426801000,"pos":0)"
              R"(}}})",
              MockJsonlSink::Stringify(doc));
          return true;
        });
    EXPECT_CALL(*json_sink, HandleDocument)
        .WillOnce([](const rapidjson::Document& doc) {
          EXPECT_EQ(
              R"({"type":"event-start","data":{)"
                R"("originalNetworkId":1,)"
                R"("transportStreamId":2,)"
                R"("serviceId":3,)"
                R"("event":{)"
                  R"("eventId":5,)"
                  R"("startTime":1609426801000,)"
                  R"("duration":1000,)"
                  R"("scrambled":true,)"
                  R"("descriptors":[])"
                R"(},)"
                R"("record":{)"
                  R"("timestamp":1609426801000,)"
                  R"("pos":0)"
                R"(})"
              R"(}})",
              MockJsonlSink::Stringify(doc));
          return true;
        });
    EXPECT_CALL(*json_sink, HandleDocument)
        .WillOnce([](const rapidjson::Document& doc) {
          EXPECT_EQ(
              R"({"type":"stop","data":{"reset":false}})",
              MockJsonlSink::Stringify(doc));
          return true;
        });
  }

  EXPECT_CALL(*file, Write).WillRepeatedly(testing::Return(RingFileSink::kBufferSize));
  EXPECT_CALL(*file, Sync).WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*file, Trunc).WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*file, Seek).WillRepeatedly(testing::Return(0));

  auto ring = std::make_unique<RingFileSink>(
      std::move(file), option.chunk_size, option.num_chunks);
  auto recorder = std::make_unique<ServiceRecorder>(kOption);
  recorder->ServiceRecorder::Connect(std::move(ring));
  recorder->JsonlSource::Connect(std::move(json_sink));
  src.Connect(std::move(recorder));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(ServiceRecorderTest, UnspecifiedEventEnd) {
  ServiceRecorderOption option = kOption;

  TableSource src;
  auto file = std::make_unique<MockFile>();
  auto json_sink = std::make_unique<MockJsonlSink>();

  // TDT tables are used for emulating PES and PCR packets.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x0002"
           test-pid="0x0000">
        <service service_id="0x0003" program_map_PID="0x0101" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0003" PCR_PID="0x901"
           test-pid="0x0101">
        <component elementary_PID="0x0301" stream_type="0x02" />
      </PMT>
      <TOT UTC_time="2021-01-01 00:00:00" test-pid="0x0014" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0901"
           test-pcr="0" />
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0003" transport_stream_id="0x0002"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012">
        <event event_id="4" start_time="2021-01-01 00:00:00"
               duration="00:00:00" running_status="undefined" CA_mode="true" />
        <event event_id="5" start_time="2021-01-01 00:00:00"
               duration="00:00:00" running_status="undefined" CA_mode="true" />
      </EIT>
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0301" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0901"
           test-pcr="27000000" test-cc="1" />
      <TOT UTC_time="2021-01-01 00:00:01" test-pid="0x0014" test-cc="1" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0301" test-cc="1" />
      <EIT type="pf" version="2" current="true" actual="true"
           service_id="0x0003" transport_stream_id="0x0002"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012" test-cc="1">
        <event event_id="5" start_time="2021-01-01 00:00:00"
               duration="00:00:00" running_status="undefined" CA_mode="true" />
        <event event_id="6" start_time="2021-01-01 00:00:00"
               duration="00:00:00" running_status="undefined" CA_mode="true" />
      </EIT>
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*json_sink, HandleDocument)
        .WillOnce([](const rapidjson::Document& doc) {
          EXPECT_EQ(
              R"({"type":"start"})",
              MockJsonlSink::Stringify(doc));
          return true;
        });
    EXPECT_CALL(*json_sink, HandleDocument)
        .WillOnce([](const rapidjson::Document& doc) {
          EXPECT_EQ(
              R"({"type":"chunk","data":{"chunk":{)"
                R"("timestamp":1609426800000,"pos":0)"
              R"(}}})",
              MockJsonlSink::Stringify(doc));
          return true;
        });
    EXPECT_CALL(*json_sink, HandleDocument)
        .WillOnce([](const rapidjson::Document& doc) {
          EXPECT_EQ(
              R"({"type":"event-start","data":{)"
                R"("originalNetworkId":1,)"
                R"("transportStreamId":2,)"
                R"("serviceId":3,)"
                R"("event":{)"
                  R"("eventId":4,)"
                  R"("startTime":1609426800000,)"
                  R"("duration":0,)"
                  R"("scrambled":true,)"
                  R"("descriptors":[])"
                R"(},)"
                R"("record":{)"
                  R"("timestamp":1609426800000,)"
                  R"("pos":0)"
                R"(})"
              R"(}})",
              MockJsonlSink::Stringify(doc));
          return true;
        });
    EXPECT_CALL(*json_sink, HandleDocument)
        .WillOnce([](const rapidjson::Document& doc) {
          EXPECT_EQ(
              R"({"type":"event-end","data":{)"
                R"("originalNetworkId":1,)"
                R"("transportStreamId":2,)"
                R"("serviceId":3,)"
                R"("event":{)"
                  R"("eventId":4,)"
                  R"("startTime":1609426800000,)"
                  R"("duration":0,)"
                  R"("scrambled":true,)"
                  R"("descriptors":[])"
                R"(},)"
                R"("record":{)"
                  R"("timestamp":1609426801000,)"
                  R"("pos":752)"  // 188 * 4
                R"(})"
              R"(}})",
              MockJsonlSink::Stringify(doc));
          return true;
        });
    EXPECT_CALL(*json_sink, HandleDocument)
        .WillOnce([](const rapidjson::Document& doc) {
          EXPECT_EQ(
              R"({"type":"event-start","data":{)"
                R"("originalNetworkId":1,)"
                R"("transportStreamId":2,)"
                R"("serviceId":3,)"
                R"("event":{)"
                  R"("eventId":5,)"
                  R"("startTime":1609426800000,)"
                  R"("duration":0,)"
                  R"("scrambled":true,)"
                  R"("descriptors":[])"
                R"(},)"
                R"("record":{)"
                  R"("timestamp":1609426801000,)"
                  R"("pos":752)"  // 188 * 4
                R"(})"
              R"(}})",
              MockJsonlSink::Stringify(doc));
          return true;
        });
    EXPECT_CALL(*json_sink, HandleDocument)
        .WillOnce([](const rapidjson::Document& doc) {
          EXPECT_EQ(
              R"({"type":"stop","data":{"reset":false}})",
              MockJsonlSink::Stringify(doc));
          return true;
        });
  }

  EXPECT_CALL(*file, Write).WillRepeatedly(testing::Return(RingFileSink::kBufferSize));
  EXPECT_CALL(*file, Sync).WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*file, Trunc).WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*file, Seek).WillRepeatedly(testing::Return(0));

  auto ring = std::make_unique<RingFileSink>(
      std::move(file), option.chunk_size, option.num_chunks);
  auto recorder = std::make_unique<ServiceRecorder>(kOption);
  recorder->ServiceRecorder::Connect(std::move(ring));
  recorder->JsonlSource::Connect(std::move(json_sink));
  src.Connect(std::move(recorder));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}
