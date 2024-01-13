#include <cstdlib>
#include <memory>

#include <fmt/format.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tsduck/tsduck.h>

#include "start_seeker.hh"

#include "test_helper.hh"

namespace {
const StartSeekerOption kOption{0x0001, 1000, 0};
}

TEST(StartSeekerTest, NoPacket) {
  MockSource src;
  auto filter = std::make_unique<StartSeeker>(kOption);
  auto sink = std::make_unique<MockSink>();

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(src, GetNextPacket).WillOnce(testing::Return(false));  // EOF
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode()).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  EXPECT_CALL(*sink, HandlePacket).Times(0);

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
}

TEST(StartSeekerTest, Eof) {
  TableSource src;
  auto filter = std::make_unique<StartSeeker>(kOption);
  auto sink = std::make_unique<MockSink>();

  // TDT tables are used for emulating PCR packets.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
      </PAT>
   </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(ts::PID_PAT, packet.getPID());
      EXPECT_EQ(0, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode()).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(StartSeekerTest, MaxDuration) {
  TableSource src;
  auto filter = std::make_unique<StartSeeker>(kOption);
  auto sink = std::make_unique<MockSink>();

  // TDT tables are used for emulating PCR packets.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x0901"
           test-pid="0x0101">
        <component elementary_PID="0x0301" stream_type="0x02" />
        <component elementary_PID="0x0302" stream_type="0x0F" />
      </PMT>
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0901"
           test-pcr="0" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0301" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0302" />
      <TDT UTC_time="1970-01-01 00:00:01" test-pid="0x0901" test-cc="1"
           test-pcr="27000000"/>
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0301" test-cc="1" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0302" test-cc="1" />
   </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(ts::PID_PAT, packet.getPID());
      EXPECT_EQ(0, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0101, packet.getPID());
      EXPECT_EQ(0, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0901, packet.getPID());
      EXPECT_EQ(0, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0301, packet.getPID());
      EXPECT_EQ(0, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0302, packet.getPID());
      EXPECT_EQ(0, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0901, packet.getPID());
      EXPECT_EQ(1, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0301, packet.getPID());
      EXPECT_EQ(1, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0302, packet.getPID());
      EXPECT_EQ(1, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode()).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(StartSeekerTest, MaxPackets) {
  TableSource src;
  StartSeekerOption option = kOption;
  option.max_packets = 3;
  auto filter = std::make_unique<StartSeeker>(option);
  auto sink = std::make_unique<MockSink>();

  // TDT tables are used for emulating PCR packets.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x0901"
           test-pid="0x0101">
        <component elementary_PID="0x0301" stream_type="0x02" />
        <component elementary_PID="0x0302" stream_type="0x0F" />
      </PMT>
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0901"
           test-pcr="0" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0301" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0302" />
      <TDT UTC_time="1970-01-01 00:00:01" test-pid="0x0901" test-cc="1"
           test-pcr="27000000"/>
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0301" test-cc="1" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0302" test-cc="1" />
   </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(ts::PID_PAT, packet.getPID());
      EXPECT_EQ(0, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0101, packet.getPID());
      EXPECT_EQ(0, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0901, packet.getPID());
      EXPECT_EQ(0, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0301, packet.getPID());
      EXPECT_EQ(0, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0302, packet.getPID());
      EXPECT_EQ(0, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0901, packet.getPID());
      EXPECT_EQ(1, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0301, packet.getPID());
      EXPECT_EQ(1, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0302, packet.getPID());
      EXPECT_EQ(1, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode()).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(StartSeekerTest, DetectVideoChange) {
  TableSource src;
  auto filter = std::make_unique<StartSeeker>(kOption);
  auto sink = std::make_unique<MockSink>();

  // TDT tables are used for emulating PCR packets.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x0901"
           test-pid="0x0101">
        <component elementary_PID="0x0301" stream_type="0x02" />
        <component elementary_PID="0x0302" stream_type="0x0F" />
      </PMT>
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0901"
           test-pcr="0" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0301" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0302" />
      <PAT version="2" current="true" transport_stream_id="0x1234"
           test-pid="0x0000" test-cc="1">
        <service service_id="0x0001" program_map_PID="0x0101" />
      </PAT>
      <PMT version="2" current="true" service_id="0x0001" PCR_PID="0x0901"
           test-pid="0x0101" test-cc="1">
        <component elementary_PID="0x0302" stream_type="0x0F" />
        <component elementary_PID="0x0303" stream_type="0x02" />
      </PMT>
      <TDT UTC_time="1970-01-01 00:00:01" test-pid="0x0901" test-cc="1"
           test-pcr="27000000"/>
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0301" test-cc="1" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0302" test-cc="1" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0303" test-cc="0" />
   </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(ts::PID_PAT, packet.getPID());
      EXPECT_EQ(1, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0101, packet.getPID());
      EXPECT_EQ(1, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0901, packet.getPID());
      EXPECT_EQ(1, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0301, packet.getPID());
      EXPECT_EQ(1, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0302, packet.getPID());
      EXPECT_EQ(1, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0303, packet.getPID());
      EXPECT_EQ(0, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode()).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(StartSeekerTest, DetectAudioChange) {
  TableSource src;
  auto filter = std::make_unique<StartSeeker>(kOption);
  auto sink = std::make_unique<MockSink>();

  // TDT tables are used for emulating PCR packets.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x0901"
           test-pid="0x0101">
        <component elementary_PID="0x0301" stream_type="0x02" />
        <component elementary_PID="0x0302" stream_type="0x0F" />
      </PMT>
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0901"
           test-pcr="0" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0301" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0302" />
      <PAT version="2" current="true" transport_stream_id="0x1234"
           test-pid="0x0000" test-cc="1">
        <service service_id="0x0001" program_map_PID="0x0101" />
      </PAT>
      <PMT version="2" current="true" service_id="0x0001" PCR_PID="0x0901"
           test-pid="0x0101" test-cc="1">
        <component elementary_PID="0x0301" stream_type="0x02" />
        <component elementary_PID="0x0303" stream_type="0x0F" />
      </PMT>
      <TDT UTC_time="1970-01-01 00:00:01" test-pid="0x0901" test-cc="1"
           test-pcr="27000000"/>
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0301" test-cc="1" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0302" test-cc="1" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0303" test-cc="0" />
   </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(ts::PID_PAT, packet.getPID());
      EXPECT_EQ(1, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0101, packet.getPID());
      EXPECT_EQ(1, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0901, packet.getPID());
      EXPECT_EQ(1, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0301, packet.getPID());
      EXPECT_EQ(1, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0302, packet.getPID());
      EXPECT_EQ(1, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0303, packet.getPID());
      EXPECT_EQ(0, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode()).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(StartSeekerTest, DetectMultipleAudioStreams) {
  TableSource src;
  auto filter = std::make_unique<StartSeeker>(kOption);
  auto sink = std::make_unique<MockSink>();

  // TDT tables are used for emulating PCR packets.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x0901"
           test-pid="0x0101">
        <component elementary_PID="0x0301" stream_type="0x02" />
        <component elementary_PID="0x0302" stream_type="0x0F" />
      </PMT>
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0901"
           test-pcr="0" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0301" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0302" />
      <PAT version="2" current="true" transport_stream_id="0x1234"
           test-pid="0x0000" test-cc="1">
        <service service_id="0x0001" program_map_PID="0x0101" />
      </PAT>
      <PMT version="2" current="true" service_id="0x0001" PCR_PID="0x0901"
           test-pid="0x0101" test-cc="1">
        <component elementary_PID="0x0301" stream_type="0x02" />
        <component elementary_PID="0x0302" stream_type="0x0F" />
        <component elementary_PID="0x0303" stream_type="0x0F" />
      </PMT>
      <TDT UTC_time="1970-01-01 00:00:01" test-pid="0x0901" test-cc="1"
           test-pcr="27000000"/>
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0301" test-cc="1" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0302" test-cc="1" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0303" test-cc="0" />
   </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(ts::PID_PAT, packet.getPID());
      EXPECT_EQ(1, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0101, packet.getPID());
      EXPECT_EQ(1, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0901, packet.getPID());
      EXPECT_EQ(1, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0301, packet.getPID());
      EXPECT_EQ(1, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0302, packet.getPID());
      EXPECT_EQ(1, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0303, packet.getPID());
      EXPECT_EQ(0, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode()).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(StartSeekerTest, AbnormalPcrPackets) {
  TableSource src;
  auto filter = std::make_unique<StartSeeker>(kOption);
  auto sink = std::make_unique<MockSink>();

  // TDT tables are used for emulating PCR packets.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x0901"
           test-pid="0x0101">
        <component elementary_PID="0x0301" stream_type="0x02" />
        <component elementary_PID="0x0302" stream_type="0x0F" />
      </PMT>
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0901"
           test-pcr="0" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0301" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0302" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0901" test-cc="1" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0901" test-cc="2"
           test-pcr="0xFFFFFFFFFFFFFFFF" />
      <PAT version="2" current="true" transport_stream_id="0x1234"
           test-pid="0x0000" test-cc="1">
        <service service_id="0x0001" program_map_PID="0x0101" />
      </PAT>
      <PMT version="2" current="true" service_id="0x0001" PCR_PID="0x0901"
           test-pid="0x0101" test-cc="1">
        <component elementary_PID="0x0301" stream_type="0x02" />
        <component elementary_PID="0x0302" stream_type="0x0F" />
        <component elementary_PID="0x0303" stream_type="0x0F" />
      </PMT>
      <TDT UTC_time="1970-01-01 00:00:01" test-pid="0x0901" test-cc="3"
           test-pcr="27000000" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0301" test-cc="1" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0302" test-cc="1" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0303" test-cc="0" />
   </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(ts::PID_PAT, packet.getPID());
      EXPECT_EQ(1, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0101, packet.getPID());
      EXPECT_EQ(1, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0901, packet.getPID());
      EXPECT_EQ(3, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0301, packet.getPID());
      EXPECT_EQ(1, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0302, packet.getPID());
      EXPECT_EQ(1, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0303, packet.getPID());
      EXPECT_EQ(0, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode()).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(StartSeekerTest, PmtSidUnmatched) {
  TableSource src;
  auto filter = std::make_unique<StartSeeker>(kOption);
  auto sink = std::make_unique<MockSink>();

  // TDT tables are used for emulating PCR packets.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x0901"
           test-pid="0x0101">
        <component elementary_PID="0x0301" stream_type="0x02" />
        <component elementary_PID="0x0302" stream_type="0x0F" />
      </PMT>
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0901"
           test-pcr="0" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0301" />
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0302" />
      <PAT version="2" current="true" transport_stream_id="0x1234"
           test-pid="0x0000" test-cc="1">
        <service service_id="0x0001" program_map_PID="0x0101" />
      </PAT>
      <PMT version="2" current="true" service_id="0x0000" PCR_PID="0x0ABC"
           test-pid="0x0101" test-cc="1" />
      <PMT version="3" current="true" service_id="0x0001" PCR_PID="0x0901"
           test-pid="0x0101" test-cc="2">
        <component elementary_PID="0x0301" stream_type="0x02" />
        <component elementary_PID="0x0302" stream_type="0x0F" />
        <component elementary_PID="0x0303" stream_type="0x0F" />
      </PMT>
      <TDT UTC_time="1970-01-01 00:00:00" test-pid="0x0ABC" test-pcr="0"/>
      <TDT UTC_time="1970-01-01 00:00:01" test-pid="0x0901" test-cc="1"
           test-pcr="27000000"/>
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0301" test-cc="1" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0302" test-cc="1" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0303" test-cc="0" />
   </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(ts::PID_PAT, packet.getPID());
      EXPECT_EQ(1, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0101, packet.getPID());
      EXPECT_EQ(1, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0101, packet.getPID());
      EXPECT_EQ(2, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0ABC, packet.getPID());
      EXPECT_EQ(0, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0901, packet.getPID());
      EXPECT_EQ(1, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0301, packet.getPID());
      EXPECT_EQ(1, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0302, packet.getPID());
      EXPECT_EQ(1, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0303, packet.getPID());
      EXPECT_EQ(0, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode()).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}
