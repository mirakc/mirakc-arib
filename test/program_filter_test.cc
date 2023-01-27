#include <cstdlib>
#include <memory>

#include <fmt/format.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tsduck/tsduck.h>

#include "program_filter.hh"

#include "test_helper.hh"

namespace {
const ProgramFilterOption kOption {
  0x0001, 0x1001, 0x0901, 0, ts::Time(), {}, {}, 0, 0, std::nullopt, false
};
}

TEST(ProgramFilterTest, NoPacket) {
  MockSource src;
  auto filter = std::make_unique<ProgramFilter>(kOption);
  auto sink = std::make_unique<MockSink>();

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(src, GetNextPacket).WillOnce(testing::Return(false));  // EOF
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  EXPECT_CALL(*sink, HandlePacket).Times(0);

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
}

TEST(ProgramFilterTest, WaitReady) {
  TableSource src;
  auto filter = std::make_unique<ProgramFilter>(kOption);
  auto sink = std::make_unique<MockSink>();

  // TDT tables are used for emulating PES and PCR packets.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
        <service service_id="0x0002" program_map_PID="0x0102" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x0901"
           test-pid="0x0101">
        <component elementary_PID="0x0301" stream_type="0x02" />
        <component elementary_PID="0x0302" stream_type="0x0F" />
      </PMT>
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0001" transport_stream_id="0x1234"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012">
        <event event_id="0x1000" start_time="1970-01-01 00:00:00"
               duration="00:00:01" running_status="undefined" CA_mode="true" />
        <event event_id="0x1001" start_time="1970-01-01 00:00:01"
               duration="01:00:00" running_status="undefined" CA_mode="true" />
      </EIT>
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0901"
           test-pcr="0" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0301" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0302" />
      <TDT UTC_time="1970-01-01 00:00:01" test-pid="0x0901" test-cc="1"
           test-pcr="27000000"/>
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0301" test-cc="1" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0302" test-cc="1" />
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000" test-cc="1">
        <service service_id="0x0001" program_map_PID="0x0101" />
        <service service_id="0x0002" program_map_PID="0x0102" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x0901"
           test-pid="0x0101" test-cc="1">
        <component elementary_PID="0x0301" stream_type="0x02" />
        <component elementary_PID="0x0302" stream_type="0x0F" />
      </PMT>
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0301" test-cc="2" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0302" test-cc="2" />
   </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_PAT, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0101, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0901, packet.getPID());
          EXPECT_EQ(27000000, packet.getPCR());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0301, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0302, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_PAT, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0101, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0301, packet.getPID());
          EXPECT_EQ(2, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0302, packet.getPID());
          EXPECT_EQ(2, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(ProgramFilterTest, AlreadyStarted) {
  TableSource src;
  auto filter = std::make_unique<ProgramFilter>(kOption);
  auto sink = std::make_unique<MockSink>();

  // TDT tables are used for emulating PES and PCR packets.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
        <service service_id="0x0002" program_map_PID="0x0102" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x0901"
           test-pid="0x0101">
        <component elementary_PID="0x0301" stream_type="0x02" />
        <component elementary_PID="0x0302" stream_type="0x0F" />
      </PMT>
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0001" transport_stream_id="0x1234"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012">
        <event event_id="0x1001" start_time="1970-01-01 00:00:00"
               duration="01:00:00" running_status="undefined" CA_mode="true" />
        <event event_id="0x1002" start_time="1970-01-01 01:00:00"
               duration="01:00:00" running_status="undefined" CA_mode="true" />
      </EIT>
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0901"
           test-pcr="0" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0301" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0302" />
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000" test-cc="1">
        <service service_id="0x0001" program_map_PID="0x0101" />
        <service service_id="0x0002" program_map_PID="0x0102" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x0901"
           test-pid="0x0101" test-cc="1">
        <component elementary_PID="0x0301" stream_type="0x02" />
        <component elementary_PID="0x0302" stream_type="0x0F" />
      </PMT>
      <TDT UTC_time="1970-01-01 00:00:01" test-pid="0x0901" test-cc="1"
           test-pcr="27000000"/>
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0301" test-cc="1" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0302" test-cc="1" />
   </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_PAT, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0101, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x901, packet.getPID());
          EXPECT_EQ(0, packet.getPCR());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0301, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0302, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_PAT, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0101, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x901, packet.getPID());
          EXPECT_EQ(27000000, packet.getPCR());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0301, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0302, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(ProgramFilterTest, EndStreaming) {
  TableSource src;
  auto filter = std::make_unique<ProgramFilter>(kOption);
  auto sink = std::make_unique<MockSink>();

  // TDT tables are used for emulating PES and PCR packets.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
        <service service_id="0x0002" program_map_PID="0x0102" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x0901"
           test-pid="0x0101">
        <component elementary_PID="0x0301" stream_type="0x02" />
        <component elementary_PID="0x0302" stream_type="0x0F" />
      </PMT>
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0001" transport_stream_id="0x1234"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012">
        <event event_id="0x1001" start_time="1970-01-01 00:00:00"
               duration="01:00:00" running_status="undefined" CA_mode="true" />
        <event event_id="0x1002" start_time="1970-01-01 01:00:00"
               duration="01:00:00" running_status="undefined" CA_mode="true" />
      </EIT>
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0901"
           test-pcr="0" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0301" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0302" />
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000" test-cc="1">
        <service service_id="0x0001" program_map_PID="0x0101" />
        <service service_id="0x0002" program_map_PID="0x0102" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x0901"
           test-pid="0x0101" test-cc="1">
        <component elementary_PID="0x0301" stream_type="0x02" />
        <component elementary_PID="0x0302" stream_type="0x0F" />
      </PMT>
      <TDT UTC_time="1970-01-01 00:00:01" test-pid="0x0901" test-cc="1"
           test-pcr="27000000"/>
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0301" test-cc="1" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0302" test-cc="1" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0901" test-cc="2"
           test-pcr="97199999999" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0301" test-cc="2" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0302" test-cc="2" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0901" test-cc="3"
           test-pcr="97200000000" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0301" test-cc="3" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0302" test-cc="3" />
   </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_PAT, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0101, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x901, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0301, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0302, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_PAT, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0101, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x901, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0301, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0302, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x901, packet.getPID());
          EXPECT_EQ(2, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0301, packet.getPID());
          EXPECT_EQ(2, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0302, packet.getPID());
          EXPECT_EQ(2, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_EQ(2, src.GetNumberOfRemainingPackets());
}

TEST(ProgramFilterTest, UpdatePcrRange) {
  TableSource src;
  auto filter = std::make_unique<ProgramFilter>(kOption);
  auto sink = std::make_unique<MockSink>();

  // TDT tables are used for emulating PES and PCR packets.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
        <service service_id="0x0002" program_map_PID="0x0102" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x0901"
           test-pid="0x0101">
        <component elementary_PID="0x0301" stream_type="0x02" />
        <component elementary_PID="0x0302" stream_type="0x0F" />
      </PMT>
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0001" transport_stream_id="0x1234"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012">
        <event event_id="0x1001" start_time="1970-01-01 00:00:00"
               duration="01:00:00" running_status="undefined" CA_mode="true" />
        <event event_id="0x1002" start_time="1970-01-01 01:00:00"
               duration="01:00:00" running_status="undefined" CA_mode="true" />
      </EIT>
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0901"
           test-pcr="0" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0301" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0302" />
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000" test-cc="1">
        <service service_id="0x0001" program_map_PID="0x0101" />
        <service service_id="0x0002" program_map_PID="0x0102" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x0901"
           test-pid="0x0101" test-cc="1">
        <component elementary_PID="0x0301" stream_type="0x02" />
        <component elementary_PID="0x0302" stream_type="0x0F" />
      </PMT>
      <TDT UTC_time="1970-01-01 00:00:01" test-pid="0x0901" test-cc="1"
           test-pcr="27000000"/>
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0301" test-cc="1" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0302" test-cc="1" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0901" test-cc="2"
           test-pcr="97199999999" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0301" test-cc="2" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0302" test-cc="2" />
      <EIT type="pf" version="2" current="true" actual="true"
           service_id="0x0001" transport_stream_id="0x1234"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012" test-cc="2">
        <event event_id="0x1001" start_time="1970-01-01 00:00:00"
               duration="02:00:00" running_status="undefined" CA_mode="true" />
        <event event_id="0x1002" start_time="1970-01-01 02:00:00"
               duration="01:00:00" running_status="undefined" CA_mode="true" />
      </EIT>
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0901" test-cc="3"
           test-pcr="97200000000" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0301" test-cc="3" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0302" test-cc="3" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0901" test-cc="4"
           test-pcr="194399999999" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0301" test-cc="4" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0302" test-cc="4" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0901" test-cc="5"
           test-pcr="194400000000" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0301" test-cc="5" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0302" test-cc="5" />
   </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_PAT, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0101, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x901, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0301, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0302, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_PAT, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0101, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x901, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0301, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0302, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x901, packet.getPID());
          EXPECT_EQ(2, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0301, packet.getPID());
          EXPECT_EQ(2, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0302, packet.getPID());
          EXPECT_EQ(2, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_EIT, packet.getPID());
          EXPECT_EQ(2, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x901, packet.getPID());
          EXPECT_EQ(3, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0301, packet.getPID());
          EXPECT_EQ(3, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0302, packet.getPID());
          EXPECT_EQ(3, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x901, packet.getPID());
          EXPECT_EQ(4, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0301, packet.getPID());
          EXPECT_EQ(4, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0302, packet.getPID());
          EXPECT_EQ(4, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_EQ(2, src.GetNumberOfRemainingPackets());
}

TEST(ProgramFilterTest, NoEventInEit) {
  TableSource src;
  auto filter = std::make_unique<ProgramFilter>(kOption);
  auto sink = std::make_unique<MockSink>();

  // TDT tables are used for emulating PES and PCR packets.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
        <service service_id="0x0002" program_map_PID="0x0102" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x0901"
           test-pid="0x0101">
        <component elementary_PID="0x0301" stream_type="0x02" />
        <component elementary_PID="0x0302" stream_type="0x0F" />
      </PMT>
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0001" transport_stream_id="0x1234"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012">
      </EIT>
   </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
  }

  EXPECT_CALL(*sink, HandlePacket).Times(0);

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_RETRY, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(ProgramFilterTest, NoFollowingEventInEit) {
  TableSource src;
  auto filter = std::make_unique<ProgramFilter>(kOption);
  auto sink = std::make_unique<MockSink>();

  // TDT tables are used for emulating PES and PCR packets.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
        <service service_id="0x0002" program_map_PID="0x0102" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x0901"
           test-pid="0x0101">
        <component elementary_PID="0x0301" stream_type="0x02" />
        <component elementary_PID="0x0302" stream_type="0x0F" />
      </PMT>
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0001" transport_stream_id="0x1234"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012">
        <event event_id="0x1000" start_time="1970-01-01 00:00:00"
               duration="00:00:01" running_status="undefined" CA_mode="true" />
      </EIT>
   </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
  }

  EXPECT_CALL(*sink, HandlePacket).Times(0);

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_RETRY, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(ProgramFilterTest, StartPcrUnderflow) {
  TableSource src;
  ProgramFilterOption option = kOption;
  auto ms = 3 * ts::MilliSecPerHour;  // wrap around time: 03:00:00
  option.clock_time = ts::Time::UnixEpoch + ms;
  option.clock_pcr = 0;
  auto filter = std::make_unique<ProgramFilter>(option);
  auto sink = std::make_unique<MockSink>();

  // clang-format off
  // TDT tables are used for emulating PES and PCR packets.
  src.LoadXml(fmt::format(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x0901"
           test-pid="0x0101">
        <component elementary_PID="0x0301" stream_type="0x02" />
      </PMT>
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0001" transport_stream_id="0x1234"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012">
        <event event_id="0x1001" start_time="1970-01-01 01:00:00"
               duration="01:00:00" running_status="undefined" CA_mode="true" />
        <event event_id="0x1002" start_time="1970-01-01 02:00:00"
               duration="01:00:00" running_status="undefined" CA_mode="true" />
      </EIT>
      <TDT UTC_time="1970-01-01 01:00:00" test-pid="0x0901" test-pcr="{}" />
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000" test-cc="1">
        <service service_id="0x0001" program_map_PID="0x0101" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x0901"
           test-pid="0x0101" test-cc="1">
        <component elementary_PID="0x0301" stream_type="0x02" />
      </PMT>
      <TDT UTC_time="1970-01-01 01:00:00" test-pid="0x0301" />
    </tsduck>
    )",
    kPcrUpperBound - (2 * ts::MilliSecPerHour * kPcrTicksPerMs)  // 01:00:00
  ));
  // clang-format on

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_PAT, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0101, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0901, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_PAT, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0101, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0301, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(ProgramFilterTest, EndPcrOverflow) {
  TableSource src;
  ProgramFilterOption option = kOption;
  auto ms = 2 * ts::MilliSecPerHour;  // wrap around time: 02:00:00
  option.clock_time = ts::Time::UnixEpoch + ms;
  option.clock_pcr = 0;
  auto filter = std::make_unique<ProgramFilter>(option);
  auto sink = std::make_unique<MockSink>();

  // TDT tables are used for emulating PES and PCR packets.
  src.LoadXml(fmt::format(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x0901"
           test-pid="0x0101">
        <component elementary_PID="0x0301" stream_type="0x02" />
      </PMT>
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0001" transport_stream_id="0x1234"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012">
        <event event_id="0x1001" start_time="1970-01-01 01:00:00"
               duration="01:00:00" running_status="undefined" CA_mode="true" />
        <event event_id="0x1002" start_time="1970-01-01 02:00:00"
               duration="01:00:00" running_status="undefined" CA_mode="true" />
      </EIT>
      <TDT UTC_time="1970-01-01 01:00:00" test-pid="0x0901" test-pcr="{}" />
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000" test-cc="1">
        <service service_id="0x0001" program_map_PID="0x0101" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x0901"
           test-pid="0x0101" test-cc="1">
        <component elementary_PID="0x0301" stream_type="0x02" />
      </PMT>
      <TDT UTC_time="1970-01-01 01:00:00" test-pid="0x0301" />
   </tsduck>
  )",
  kPcrUpperBound - (1 * ts::MilliSecPerHour * kPcrTicksPerMs)  // 01:00:00
  ));

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_PAT, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0101, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0901, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_PAT, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0101, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0301, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(ProgramFilterTest, PreStreaming) {
  auto option = kOption;
  option.pre_streaming = true;
  TableSource src;
  auto filter = std::make_unique<ProgramFilter>(option);
  auto sink = std::make_unique<MockSink>();

  // TDT tables are used to generate PCR packets and special packets for sleeps.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000" test-cc="0">
        <service service_id="0x0001" program_map_PID="0x0101" />
        <service service_id="0x0002" program_map_PID="0x0102" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x0901"
           test-pid="0x0101">
        <component elementary_PID="0x0301" stream_type="0x02" />
        <component elementary_PID="0x0302" stream_type="0x0F" />
      </PMT>
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0001" transport_stream_id="0x1234"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012">
        <event event_id="0x1001" start_time="1970-01-01 00:00:00"
               duration="01:00:00" running_status="undefined" CA_mode="true" />
        <event event_id="0x1002" start_time="1970-01-01 01:00:00"
               duration="01:00:00" running_status="undefined" CA_mode="true" />
      </EIT>
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0901" test-pcr="0" />
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000" test-cc="1">
        <service service_id="0x0001" program_map_PID="0x0101" />
        <service service_id="0x0002" program_map_PID="0x0102" />
      </PAT>
   </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_PAT, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0101, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0901, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_PAT, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(ProgramFilterTest, AbnormalPcrPackets) {
  TableSource src;
  auto filter = std::make_unique<ProgramFilter>(kOption);
  auto sink = std::make_unique<MockSink>();

  // TDT tables are used for emulating PES and PCR packets.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
        <service service_id="0x0002" program_map_PID="0x0102" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x0901"
           test-pid="0x0101">
        <component elementary_PID="0x0301" stream_type="0x02" />
        <component elementary_PID="0x0302" stream_type="0x0F" />
      </PMT>
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0001" transport_stream_id="0x1234"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012">
        <event event_id="0x1000" start_time="1970-01-01 00:00:00"
               duration="00:00:01" running_status="undefined" CA_mode="true" />
        <event event_id="0x1001" start_time="1970-01-01 00:00:01"
               duration="01:00:00" running_status="undefined" CA_mode="true" />
      </EIT>
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0901"
           test-pcr="0" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0301" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0302" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0901" test-cc="1" />
      <TDT UTC_time="1970-01-01 00:00:01" test-pid="0x0901" test-cc="2"
           test-pcr="0xFFFFFFFFFFFFFFFF"/>
      <TDT UTC_time="1970-01-01 00:00:01" test-pid="0x0901" test-cc="3"
           test-pcr="27000000"/>
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0301" test-cc="1" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0302" test-cc="1" />
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000" test-cc="1">
        <service service_id="0x0001" program_map_PID="0x0101" />
        <service service_id="0x0002" program_map_PID="0x0102" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x0901"
           test-pid="0x0101" test-cc="1">
        <component elementary_PID="0x0301" stream_type="0x02" />
        <component elementary_PID="0x0302" stream_type="0x0F" />
      </PMT>
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0901" test-cc="4" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0301" test-cc="2" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0302" test-cc="2" />
   </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_PAT, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0101, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0901, packet.getPID());
          EXPECT_EQ(27000000, packet.getPCR());
          EXPECT_EQ(3, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0301, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0302, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_PAT, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0101, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0901, packet.getPID());
          EXPECT_EQ(4, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0301, packet.getPID());
          EXPECT_EQ(2, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0302, packet.getPID());
          EXPECT_EQ(2, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(ProgramFilterTest, PmtSidUnmatched) {
  TableSource src;
  auto filter = std::make_unique<ProgramFilter>(kOption);
  auto sink = std::make_unique<MockSink>();

  // TDT tables are used for emulating PES and PCR packets.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
        <service service_id="0x0002" program_map_PID="0x0102" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x0901"
           test-pid="0x0101">
        <component elementary_PID="0x0301" stream_type="0x02" />
        <component elementary_PID="0x0302" stream_type="0x0F" />
      </PMT>
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0001" transport_stream_id="0x1234"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012">
        <event event_id="0x1001" start_time="1970-01-01 00:00:00"
               duration="01:00:00" running_status="undefined" CA_mode="true" />
        <event event_id="0x1002" start_time="1970-01-01 00:00:01"
               duration="01:00:00" running_status="undefined" CA_mode="true" />
      </EIT>
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0901"
           test-pcr="0" />
      <PMT version="2" current="true" service_id="0x0000" PCR_PID="0x0ABC"
           test-pid="0x0101" test-cc="1" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0301" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0302" />
   </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_PAT, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0101, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0901, packet.getPID());
          EXPECT_EQ(0, packet.getPCR());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0101, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0301, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0302, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(ProgramFilterTest, EitSidUnmatched) {
  TableSource src;
  auto filter = std::make_unique<ProgramFilter>(kOption);
  auto sink = std::make_unique<MockSink>();

  // TDT tables are used for emulating PES and PCR packets.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
        <service service_id="0x0002" program_map_PID="0x0102" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x0901"
           test-pid="0x0101">
        <component elementary_PID="0x0301" stream_type="0x02" />
        <component elementary_PID="0x0302" stream_type="0x0F" />
      </PMT>
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0001" transport_stream_id="0x1234"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012">
        <event event_id="0x1001" start_time="1970-01-01 00:00:00"
               duration="01:00:00" running_status="undefined" CA_mode="true" />
        <event event_id="0x1002" start_time="1970-01-01 00:00:01"
               duration="01:00:00" running_status="undefined" CA_mode="true" />
      </EIT>
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0901"
           test-pcr="0" />
      <EIT type="pf" version="2" current="true" actual="true"
           service_id="0x0000" transport_stream_id="0x1234"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012" test-cc="1" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0301" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0302" />
   </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_PAT, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0101, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0901, packet.getPID());
          EXPECT_EQ(0, packet.getPCR());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_EIT, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0301, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0302, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(ProgramFilterTest, ResyncClockBeforeStreaming) {
  TableSource src;
  auto filter = std::make_unique<ProgramFilter>(kOption);
  auto sink = std::make_unique<MockSink>();

  // TDT tables are used for emulating PES and PCR packets.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
        <service service_id="0x0002" program_map_PID="0x0102" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x0902"
           test-pid="0x0101">
        <component elementary_PID="0x0301" stream_type="0x02" />
        <component elementary_PID="0x0302" stream_type="0x0F" />
      </PMT>
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0001" transport_stream_id="0x1234"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012">
        <event event_id="0x1000" start_time="1970-01-01 00:00:00"
               duration="00:00:01" running_status="undefined" CA_mode="true" />
        <event event_id="0x1001" start_time="1970-01-01 00:00:01"
               duration="01:00:00" running_status="undefined" CA_mode="true" />
      </EIT>
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0902"
           test-pcr="100000000000" />
      <TOT UTC_time="1970-01-01 00:00:00" test-pid="0x0014" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0301" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0302" />
      <TOT UTC_time="1970-01-01 01:00:00" test-pid="0x0014" test-cc="1" />
      <TDT UTC_time="1970-01-01 00:00:01" test-pid="0x0902" test-cc="1"
           test-pcr="100027000000"/>
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0301" test-cc="1" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0302" test-cc="1" />
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000" test-cc="1">
        <service service_id="0x0001" program_map_PID="0x0101" />
        <service service_id="0x0002" program_map_PID="0x0102" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x0902"
           test-pid="0x0101" test-cc="1">
        <component elementary_PID="0x0301" stream_type="0x02" />
        <component elementary_PID="0x0302" stream_type="0x0F" />
      </PMT>
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0301" test-cc="2" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0302" test-cc="2" />
   </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_PAT, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0101, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0902, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0301, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0302, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_PAT, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0101, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0301, packet.getPID());
          EXPECT_EQ(2, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0302, packet.getPID());
          EXPECT_EQ(2, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(ProgramFilterTest, ResyncClockWhileStreaming) {
  TableSource src;
  auto filter = std::make_unique<ProgramFilter>(kOption);
  auto sink = std::make_unique<MockSink>();

  // TDT tables are used for emulating PES and PCR packets.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
        <service service_id="0x0002" program_map_PID="0x0102" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x0901"
           test-pid="0x0101">
        <component elementary_PID="0x0301" stream_type="0x02" />
        <component elementary_PID="0x0302" stream_type="0x0F" />
      </PMT>
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0001" transport_stream_id="0x1234"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012">
        <event event_id="0x1001" start_time="1970-01-01 00:00:00"
               duration="01:00:00" running_status="undefined" CA_mode="true" />
        <event event_id="0x1002" start_time="1970-01-01 01:00:00"
               duration="01:00:00" running_status="undefined" CA_mode="true" />
      </EIT>
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0901" test-pcr="0" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0301" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0302" />
      <PAT version="2" current="true" transport_stream_id="0x1234"
           test-pid="0x0000" test-cc="1">
        <service service_id="0x0001" program_map_PID="0x0101" />
        <service service_id="0x0002" program_map_PID="0x0102" />
      </PAT>
      <PMT version="2" current="true" service_id="0x0001" PCR_PID="0x0902"
           test-pid="0x0101" test-cc="1">
        <component elementary_PID="0x0301" stream_type="0x02" />
        <component elementary_PID="0x0302" stream_type="0x0F" />
      </PMT>
      <TDT UTC_time="1970-01-01 00:00:01" test-pid="0x0902" test-cc="0"
           test-pcr="100027000000"/>
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0301" test-cc="1" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0302" test-cc="1" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0902" test-cc="1"
           test-pcr="197200000000" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0301" test-cc="2" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0302" test-cc="2" />
      <TOT UTC_time="1970-01-01 01:00:00" test-pid="0x0014" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0902" test-cc="2"
           test-pcr="197200000001" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0301" test-cc="3" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0302" test-cc="3" />
   </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_PAT, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0101, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x901, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0301, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0302, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_PAT, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0101, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x902, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0301, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0302, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x902, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0301, packet.getPID());
          EXPECT_EQ(2, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0302, packet.getPID());
          EXPECT_EQ(2, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_TOT, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_EQ(2, src.GetNumberOfRemainingPackets());
}

TEST(ProgramFilterTest, PesBlackList) {
  auto option = kOption;
  option.video_tags = {0};
  option.audio_tags = {1};  // PES#0302 will be dropped.
  TableSource src;
  auto filter = std::make_unique<ProgramFilter>(option);
  auto sink = std::make_unique<MockSink>();

  // TDT tables are used for emulating PES and PCR packets.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x0901"
           test-pid="0x0101">
        <component elementary_PID="0x0301" stream_type="0x02">
          <stream_identifier_descriptor component_tag="0" />
        </component>
        <component elementary_PID="0x0302" stream_type="0x0F">
          <stream_identifier_descriptor component_tag="16" />
        </component>
      </PMT>
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0001" transport_stream_id="0x1234"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012">
        <event event_id="0x1000" start_time="1970-01-01 00:00:00"
               duration="00:00:01" running_status="undefined" CA_mode="true" />
        <event event_id="0x1001" start_time="1970-01-01 00:00:01"
               duration="01:00:00" running_status="undefined" CA_mode="true" />
      </EIT>
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0901"
           test-pcr="0" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0301" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0302" />
      <TDT UTC_time="1970-01-01 00:00:01" test-pid="0x0901" test-cc="1"
           test-pcr="27000000"/>
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0301" test-cc="1" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0302" test-cc="1" />
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000" test-cc="1">
        <service service_id="0x0001" program_map_PID="0x0101" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x0901"
           test-pid="0x0101" test-cc="1">
        <component elementary_PID="0x0301" stream_type="0x02">
          <stream_identifier_descriptor component_tag="0" />
        </component>
        <component elementary_PID="0x0302" stream_type="0x0F">
          <stream_identifier_descriptor component_tag="16" />
        </component>
      </PMT>
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0301" test-cc="2" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0302" test-cc="2" />
   </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_PAT, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0101, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          // TODO: check if the packet contains only one entry for the video
          // stream.
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0901, packet.getPID());
          EXPECT_EQ(27000000, packet.getPCR());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0301, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_PAT, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0101, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          // TODO: check if the packet contains only one entry for the video
          // stream.
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0301, packet.getPID());
          EXPECT_EQ(2, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(ProgramFilterTest, WaitUntilTimedout) {
  auto option = kOption;
  option.wait_until = ts::Time::UnixEpoch + 1000;
  TableSource src;
  auto filter = std::make_unique<ProgramFilter>(option);
  auto sink = std::make_unique<MockSink>();

  // TDT tables are used for emulating PES and PCR packets.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <TOT UTC_time="1970-01-01 00:00:00" test-pid="0x0014" />
      <TOT UTC_time="1970-01-01 00:00:01" test-pid="0x0014" test-cc="1" />
      <TOT UTC_time="1970-01-01 00:00:02" test-pid="0x0014" test-cc="2" />
      <TOT UTC_time="1970-01-01 00:00:03" test-pid="0x0014" test-cc="3" />
   </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_RETRY, src.FeedPackets());
  EXPECT_EQ(1, src.GetNumberOfRemainingPackets());
}

TEST(ProgramFilterTest, ReachTimeLimitWhileStreaming) {
  auto option = kOption;
  option.wait_until = ts::Time::UnixEpoch;
  TableSource src;
  auto filter = std::make_unique<ProgramFilter>(kOption);
  auto sink = std::make_unique<MockSink>();

  // TDT tables are used for emulating PES and PCR packets.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
        <service service_id="0x0002" program_map_PID="0x0102" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x0901"
           test-pid="0x0101">
        <component elementary_PID="0x0301" stream_type="0x02" />
        <component elementary_PID="0x0302" stream_type="0x0F" />
      </PMT>
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0001" transport_stream_id="0x1234"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012">
        <event event_id="0x1001" start_time="1970-01-01 00:00:00"
               duration="01:00:00" running_status="undefined" CA_mode="true" />
        <event event_id="0x1002" start_time="1970-01-01 01:00:00"
               duration="01:00:00" running_status="undefined" CA_mode="true" />
      </EIT>
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0901"
           test-pcr="0" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0301" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0302" />
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000" test-cc="1">
        <service service_id="0x0001" program_map_PID="0x0101" />
        <service service_id="0x0002" program_map_PID="0x0102" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x0901"
           test-pid="0x0101" test-cc="1">
        <component elementary_PID="0x0301" stream_type="0x02" />
        <component elementary_PID="0x0302" stream_type="0x0F" />
      </PMT>
      <TDT UTC_time="1970-01-01 00:00:01" test-pid="0x0901" test-cc="1"
           test-pcr="27000000"/>
      <TOT UTC_time="1970-01-01 00:00:01" test-pid="0x0014" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0301" test-cc="1" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0302" test-cc="1" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0901" test-cc="2"
           test-pcr="97199999999" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0301" test-cc="2" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0302" test-cc="2" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0901" test-cc="3"
           test-pcr="97200000000" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0301" test-cc="3" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0302" test-cc="3" />
   </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_PAT, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0101, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x901, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0301, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0302, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_PAT, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0101, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x901, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0014, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0301, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0302, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x901, packet.getPID());
          EXPECT_EQ(2, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0301, packet.getPID());
          EXPECT_EQ(2, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0302, packet.getPID());
          EXPECT_EQ(2, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_EQ(2, src.GetNumberOfRemainingPackets());
}
