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

#include <fmt/format.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tsduck/tsduck.h>

#include "packet_filter.hh"

#include "test_helper.hh"

namespace {
const PacketFilterOption kServiceFilterOption { 0x0001 };
const PacketFilterOption kProgramFilterOption { 0x0001, 0x1001 };
}

TEST(PacketFilterTest, NoPacket) {
  MockSource src;
  auto filter = std::make_unique<PacketFilter>(kServiceFilterOption);
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

TEST(PacketFilterTest, NullPacket) {
  MockSource src;
  auto filter = std::make_unique<PacketFilter>(kServiceFilterOption);
  auto sink = std::make_unique<MockSink>();

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).Times(1);
    EXPECT_CALL(src, GetNextPacket).WillOnce(
        [](ts::TSPacket* packet) {
          *packet = ts::NullPacket;
          return true;
        });
    EXPECT_CALL(src, GetNextPacket).WillOnce(testing::Return(false)); // EOF
    EXPECT_CALL(*sink, End).WillOnce(testing::Return(true));
  }

  EXPECT_CALL(*sink, HandlePacket).Times(0);

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_TRUE(src.FeedPackets());
}

TEST(PacketFilterTest, NoSidInPat) {
  TableSource src;
  auto filter = std::make_unique<PacketFilter>(kServiceFilterOption);
  auto sink = std::make_unique<MockSink>();

  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x1234" program_map_PID="0x1234"/>
      </PAT>
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).Times(1);
    EXPECT_CALL(*sink, End).WillOnce(testing::Return(true));
  }

  EXPECT_CALL(*sink, HandlePacket).Times(0);

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_TRUE(src.FeedPackets());
}

TEST(PacketFilterTest, ServiceStream) {
  TableSource src;
  auto filter = std::make_unique<PacketFilter>(kServiceFilterOption);
  auto sink = std::make_unique<MockSink>();

  // TDT tables are used for emulating PES packets.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
        <service service_id="0x0002" program_map_PID="0x0102" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x901"
           test-pid="0x0101">
        <component elementary_PID="0x0301" stream_type="0x02" />
        <component elementary_PID="0x0302" stream_type="0x0F" />
        <component elementary_PID="0x0303" stream_type="0x08" />
      </PMT>
      <PMT version="1" current="true" service_id="0x0002" PCR_PID="0x902"
           test-pid="0x0102">
        <component elementary_PID="0x0311" stream_type="0x02" />
        <component elementary_PID="0x0312" stream_type="0x0F" />
        <component elementary_PID="0x0313" stream_type="0x08" />
      </PMT>
      <TOT UTC_time="2019-01-02 03:04:05" test-pid="0x0014" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0301" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0302" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0303" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0311" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0312" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0313" />
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0001" transport_stream_id="0x1234"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012">
        <event event_id="0x1001" start_time="2019-01-02 03:00:00"
               duration="01:00:00" running_status="starting" CA_mode="true" />
      </EIT>
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0301" test-cc="1" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0302" test-cc="1" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0311" test-cc="1" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0312" test-cc="1" />
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).Times(1);
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_PAT, packet.getPID());
          TableValidator<ts::PAT> validator(ts::PID_PAT);
          EXPECT_CALL(validator, Validate).WillOnce(
              [](const ts::PAT& pat) {
                EXPECT_TRUE(pat.isValid());
                EXPECT_EQ(1, pat.pmts.size());
                EXPECT_EQ(0x0101, pat.pmts.at(0x0001));
              });
          validator.FeedPacket(packet);
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0101, packet.getPID());
          TableValidator<ts::PMT> validator(0x0101);
          EXPECT_CALL(validator, Validate).WillOnce(
              [](const ts::PMT& pmt) {
                EXPECT_TRUE(pmt.isValid());
                EXPECT_EQ(2, pmt.streams.size());
                EXPECT_TRUE(pmt.streams.find(0x0301) != pmt.streams.end());
                EXPECT_TRUE(pmt.streams.find(0x0302) != pmt.streams.end());
              });
          validator.FeedPacket(packet);
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_TOT, packet.getPID());
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
}

TEST(PacketFilterTest, ProgramStream) {
  TableSource src;
  auto filter = std::make_unique<PacketFilter>(kProgramFilterOption);
  auto sink = std::make_unique<MockSink>();

  // TDT tables are used for emulating PES packets.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
        <service service_id="0x0002" program_map_PID="0x0102" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x901"
           test-pid="0x0101">
        <component elementary_PID="0x0301" stream_type="0x02" />
        <component elementary_PID="0x0302" stream_type="0x0F" />
      </PMT>
      <PMT version="1" current="true" service_id="0x0002" PCR_PID="0x902"
           test-pid="0x0102">
        <component elementary_PID="0x0311" stream_type="0x02" />
        <component elementary_PID="0x0312" stream_type="0x0F" />
      </PMT>
      <TOT UTC_time="2019-01-02 03:04:05" test-pid="0x0014" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0301" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0302" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0311" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0312" />
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0002" transport_stream_id="0x1234"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012">
        <event event_id="0x1101" start_time="2019-01-02 03:00:00"
               duration="01:00:00" running_status="starting" CA_mode="true" />
      </EIT>
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0301" test-cc="1" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0302" test-cc="1" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0311" test-cc="1" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0312" test-cc="1" />
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0001" transport_stream_id="0x1234"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012" test-cc="1">
        <event event_id="0x1001" start_time="2019-01-02 03:00:00"
               duration="01:00:00" running_status="starting" CA_mode="true" />
      </EIT>
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0301" test-cc="2" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0302" test-cc="2" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0311" test-cc="2" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0312" test-cc="2" />
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).Times(1);
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_PAT, packet.getPID());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0101, packet.getPID());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_TOT, packet.getPID());
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
    EXPECT_CALL(*sink, End).WillOnce(testing::Return(true));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_TRUE(src.FeedPackets());
}

TEST(PacketFilterTest, StopProgramStreamWhenNextProgramHasStarted) {
  TableSource src;
  auto filter = std::make_unique<PacketFilter>(kProgramFilterOption);
  auto sink = std::make_unique<MockSink>();

  // TDT tables are used for emulating PES packets.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
        <service service_id="0x0002" program_map_PID="0x0102" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x901"
           test-pid="0x0101">
        <component elementary_PID="0x0301" stream_type="0x02" />
        <component elementary_PID="0x0302" stream_type="0x0F" />
      </PMT>
      <PMT version="1" current="true" service_id="0x0002" PCR_PID="0x902"
           test-pid="0x0102">
        <component elementary_PID="0x0311" stream_type="0x02" />
        <component elementary_PID="0x0312" stream_type="0x0F" />
      </PMT>
      <TOT UTC_time="2019-01-02 03:04:05" test-pid="0x0014" />
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0001" transport_stream_id="0x1234"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012">
        <event event_id="0x1001" start_time="2019-01-02 03:00:00"
               duration="01:00:00" running_status="starting" CA_mode="true" />
      </EIT>
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0301" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0302" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0311" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0312" />
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0001" transport_stream_id="0x1234"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012" test-cc="1">
        <event event_id="0x1001" start_time="2019-01-02 03:00:00"
               duration="01:00:00" running_status="starting" CA_mode="true" />
      </EIT>
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0002" transport_stream_id="0x1234"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012" test-cc="2">
        <event event_id="0x1101" start_time="2019-01-02 03:00:00"
               duration="01:00:00" running_status="starting" CA_mode="true" />
      </EIT>
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0301" test-cc="1" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0302" test-cc="1" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0311" test-cc="1" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0312" test-cc="1" />
      <EIT type="pf" version="2" current="true" actual="true"
           service_id="0x0001" transport_stream_id="0x1234"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012" test-cc="3">
        <event event_id="0x1002" start_time="2019-01-02 04:00:00"
               duration="01:00:00" running_status="starting" CA_mode="true" />
      </EIT>
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0301" test-cc="2" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0302" test-cc="2" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0311" test-cc="2" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0312" test-cc="2" />
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).Times(1);
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_PAT, packet.getPID());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0101, packet.getPID());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_TOT, packet.getPID());
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
}

TEST(PacketFilterTest, ResetFilterDueToPatChanged) {
  TableSource src;
  auto filter = std::make_unique<PacketFilter>(kServiceFilterOption);
  auto sink = std::make_unique<MockSink>();

  // TDT tables are used for emulating PES packets.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x901"
           test-pid="0x0101">
        <component elementary_PID="0x0301" stream_type="0x02" />
        <component elementary_PID="0x0302" stream_type="0x0F" />
      </PMT>
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0301" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0302" />
      <PAT version="2" current="true" transport_stream_id="0x1234"
           test-pid="0x0000" test-cc="1">
        <service service_id="0x0001" program_map_PID="0x0102" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x901"
           test-pid="0x0101" test-cc="1">
        <component elementary_PID="0x0301" stream_type="0x02" />
        <component elementary_PID="0x0302" stream_type="0x0F" />
      </PMT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x902"
           test-pid="0x0102">
        <component elementary_PID="0x0303" stream_type="0x02" />
        <component elementary_PID="0x0304" stream_type="0x0F" />
      </PMT>
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0301" test-cc="1" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0302" test-cc="1" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0303" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0304" />
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).Times(1);
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_PAT, packet.getPID());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0101, packet.getPID());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0301, packet.getPID());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0302, packet.getPID());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_PAT, packet.getPID());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0102, packet.getPID());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0303, packet.getPID());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0304, packet.getPID());
          return true;
        });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return(true));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_TRUE(src.FeedPackets());
}

TEST(PacketFilterTest, ResetFilterDueToPmtChanged) {
  TableSource src;
  auto filter = std::make_unique<PacketFilter>(kServiceFilterOption);
  auto sink = std::make_unique<MockSink>();

  // TDT tables are used for emulating PES packets.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x901"
           test-pid="0x0101">
        <component elementary_PID="0x0301" stream_type="0x02" />
        <component elementary_PID="0x0302" stream_type="0x0F" />
      </PMT>
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0301" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0302" />
      <PMT version="2" current="true" service_id="0x0001" PCR_PID="0x901"
           test-pid="0x0101" test-cc="1">
        <component elementary_PID="0x0303" stream_type="0x02" />
        <component elementary_PID="0x0304" stream_type="0x0F" />
      </PMT>
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0301" test-cc="1" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0302" test-cc="1" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0303" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0304" />
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).Times(1);
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_PAT, packet.getPID());
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
          EXPECT_EQ(0x0301, packet.getPID());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0302, packet.getPID());
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
          EXPECT_EQ(0x0303, packet.getPID());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0304, packet.getPID());
          return true;
        });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return(true));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_TRUE(src.FeedPackets());
}

TEST(PacketFilterTest, TimeLimitTot) {
  ts::Time time_limit(2019, 1, 2, 3, 4, 5);
  auto option = kServiceFilterOption;
  option.WithTimeLimit(time_limit);

  TableSource src;
  auto filter = std::make_unique<PacketFilter>(option);
  auto sink = std::make_unique<MockSink>();

  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <TOT UTC_time="2019-01-02 03:04:04" test-pid="0x0014" test-cc="0" />
      <TOT UTC_time="2019-01-02 03:04:05" test-pid="0x0014" test-cc="1" />
      <TOT UTC_time="2019-01-02 03:04:06" test-pid="0x0014" test-cc="2" />
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).Times(1);
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_TOT, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return(true));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_TRUE(src.FeedPackets());
}

TEST(PacketFilterTest, TimeLimitTdt) {
  ts::Time time_limit(2019, 1, 2, 3, 4, 5);
  auto option = kServiceFilterOption;
  option.WithTimeLimit(time_limit);

  TableSource src;
  auto filter = std::make_unique<PacketFilter>(option);
  auto sink = std::make_unique<MockSink>();

  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <TDT UTC_time="2019-01-02 03:04:04" test-pid="0x0014" test-cc="0" />
      <TDT UTC_time="2019-01-02 03:04:05" test-pid="0x0014" test-cc="1" />
      <TDT UTC_time="2019-01-02 03:04:06" test-pid="0x0014" test-cc="2" />
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).Times(1);
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_TDT, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return(true));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_TRUE(src.FeedPackets());
}
