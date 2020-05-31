#include <memory>

#include <fmt/format.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tsduck/tsduck.h>

#include "program_filter2.hh"

#include "test_helper.hh"

namespace {
const ProgramFilter2Option kOption {
  0x0001, 0x1001
};
}

TEST(ProgramFilter2Test, NoPacket) {
  MockSource src;
  auto filter = std::make_unique<ProgramFilter2>(kOption);
  auto sink = std::make_unique<MockSink>();

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).Times(1);
    EXPECT_CALL(src, GetNextPacket).WillOnce(testing::Return(false));  // EOF
    EXPECT_CALL(*sink, End).WillOnce(testing::Return(true));
  }

  EXPECT_CALL(*sink, HandlePacket).Times(0);

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_TRUE(src.FeedPackets());
}

TEST(ProgramFilter2Test, WaitReady) {
  TableSource src;
  auto filter = std::make_unique<ProgramFilter2>(kOption);
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
    EXPECT_CALL(*sink, Start).Times(1);
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_PAT, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_EIT, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_PAT, packet.getPID());
          EXPECT_EQ(1, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return(true));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_TRUE(src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(ProgramFilter2Test, StartStreamingByNextPmt) {
  TableSource src;
  auto filter = std::make_unique<ProgramFilter2>(kOption);
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
      <PMT version="2" current="true" service_id="0x0001" PCR_PID="0x0901"
           test-pid="0x0101" test-cc="1">
        <component elementary_PID="0x0301" stream_type="0x02" />
        <component elementary_PID="0x0302" stream_type="0x0F" />
      </PMT>
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0301" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0302" />
   </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).Times(1);
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_PAT, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_EIT, packet.getPID());
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
    EXPECT_CALL(*sink, End).WillOnce(testing::Return(true));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_TRUE(src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(ProgramFilter2Test, StartStreamingByEit) {
  TableSource src;
  auto filter = std::make_unique<ProgramFilter2>(kOption);
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
      <EIT type="pf" version="2" current="true" actual="true"
           service_id="0x0001" transport_stream_id="0x1234"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012" test-cc="1">
        <event event_id="0x1001" start_time="1970-01-01 00:00:00"
               duration="01:00:00" running_status="undefined" CA_mode="true" />
        <event event_id="0x1002" start_time="1970-01-01 01:00:00"
               duration="01:00:00" running_status="undefined" CA_mode="true" />
      </EIT>
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0301" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0302" />
   </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).Times(1);
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_PAT, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_EIT, packet.getPID());
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
    EXPECT_CALL(*sink, End).WillOnce(testing::Return(true));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_TRUE(src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(ProgramFilter2Test, AlreadyStarted) {
  TableSource src;
  auto filter = std::make_unique<ProgramFilter2>(kOption);
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
    EXPECT_CALL(*sink, Start).Times(1);
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_PAT, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_EIT, packet.getPID());
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
    EXPECT_CALL(*sink, End).WillOnce(testing::Return(true));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_TRUE(src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(ProgramFilter2Test, EndStreaming) {
  TableSource src;
  auto filter = std::make_unique<ProgramFilter2>(kOption);
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
      <EIT type="pf" version="2" current="true" actual="true"
           service_id="0x0001" transport_stream_id="0x1234"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012" test-cc="1">
        <event event_id="0x1002" start_time="1970-01-01 01:00:00"
               duration="01:00:00" running_status="undefined" CA_mode="true" />
        <event event_id="0x1003" start_time="1970-01-01 02:00:00"
               duration="01:00:00" running_status="undefined" CA_mode="true" />
      </EIT>
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0901" test-cc="2"
           test-pcr="97199999999" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0301" test-cc="2" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0302" test-cc="2" />
   </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).Times(1);
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_PAT, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_EIT, packet.getPID());
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
    EXPECT_CALL(*sink, End).WillOnce(testing::Return(true));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_TRUE(src.FeedPackets());
  EXPECT_EQ(3, src.GetNumberOfRemainingPackets());
}

TEST(ProgramFilter2Test, NoEventInEit) {
  TableSource src;
  auto filter = std::make_unique<ProgramFilter2>(kOption);
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
    EXPECT_CALL(*sink, Start).Times(1);
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_PAT, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return(true));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_TRUE(src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(ProgramFilter2Test, NoFollowingEventInEit) {
  TableSource src;
  auto filter = std::make_unique<ProgramFilter2>(kOption);
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
    EXPECT_CALL(*sink, Start).Times(1);
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_PAT, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return(true));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_TRUE(src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}
