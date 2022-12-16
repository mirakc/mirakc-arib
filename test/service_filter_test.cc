#include <cstdlib>
#include <memory>

#include <fmt/format.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tsduck/tsduck.h>

#include "service_filter.hh"

#include "test_helper.hh"

namespace {
const ServiceFilterOption kOption { 0x0001 };
}

TEST(ServiceFilterTest, NoPacket) {
  MockSource src;
  auto filter = std::make_unique<ServiceFilter>(kOption);
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

TEST(ServiceFilterTest, NullPacket) {
  MockSource src;
  auto filter = std::make_unique<ServiceFilter>(kOption);
  auto sink = std::make_unique<MockSink>();

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(src, GetNextPacket).WillOnce(
        [](ts::TSPacket* packet) {
          *packet = ts::NullPacket;
          return true;
        });
    EXPECT_CALL(src, GetNextPacket).WillOnce(testing::Return(false)); // EOF
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  EXPECT_CALL(*sink, HandlePacket).Times(0);

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
}

TEST(ServiceFilterTest, NoSidInPat) {
  TableSource src;
  auto filter = std::make_unique<ServiceFilter>(kOption);
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
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  EXPECT_CALL(*sink, HandlePacket).Times(0);

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_FAILURE, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(ServiceFilterTest, ServiceStream) {
  TableSource src;
  auto filter = std::make_unique<ServiceFilter>(kOption);
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
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
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
                EXPECT_EQ(3, pmt.streams.size());
                EXPECT_TRUE(pmt.streams.find(0x0301) != pmt.streams.end());
                EXPECT_TRUE(pmt.streams.find(0x0302) != pmt.streams.end());
                EXPECT_TRUE(pmt.streams.find(0x0303) != pmt.streams.end());
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
          EXPECT_EQ(0x0303, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_EIT, packet.getPID());
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

TEST(ServiceFilterTest, ResetFilterDueToPatChanged) {
  TableSource src;
  auto filter = std::make_unique<ServiceFilter>(kOption);
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
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
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
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(ServiceFilterTest, ResetFilterDueToPmtChanged) {
  TableSource src;
  auto filter = std::make_unique<ServiceFilter>(kOption);
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
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
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
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(ServiceFilterTest, TimeLimitTot) {
  auto option = kOption;
  option.time_limit = ts::Time(2019, 1, 2, 3, 4, 5);

  TableSource src;
  auto filter = std::make_unique<ServiceFilter>(option);
  auto sink = std::make_unique<MockSink>();

  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0001" transport_stream_id="0x1234"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012" test-cc="1">
        <event event_id="0x1001" start_time="2019-01-02 03:00:00"
               duration="01:00:00" running_status="starting" CA_mode="true" />
      </EIT>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
      </PAT>
      <TOT UTC_time="2019-01-02 03:04:04" test-pid="0x0014" test-cc="0" />
      <TOT UTC_time="2019-01-02 03:04:05" test-pid="0x0014" test-cc="1" />
      <TOT UTC_time="2019-01-02 03:04:06" test-pid="0x0014" test-cc="2" />
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_PAT, packet.getPID());
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
  EXPECT_EQ(1, src.GetNumberOfRemainingPackets());
}

TEST(ServiceFilterTest, TimeLimitTdt) {
  auto option = kOption;
  option.time_limit = ts::Time(2019, 1, 2, 3, 4, 5);

  TableSource src;
  auto filter = std::make_unique<ServiceFilter>(option);
  auto sink = std::make_unique<MockSink>();

  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0001" transport_stream_id="0x1234"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012" test-cc="1">
        <event event_id="0x1001" start_time="2019-01-02 03:00:00"
               duration="01:00:00" running_status="starting" CA_mode="true" />
      </EIT>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
      </PAT>
      <TDT UTC_time="2019-01-02 03:04:04" test-pid="0x0014" test-cc="0" />
      <TDT UTC_time="2019-01-02 03:04:05" test-pid="0x0014" test-cc="1" />
      <TDT UTC_time="2019-01-02 03:04:06" test-pid="0x0014" test-cc="2" />
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_PAT, packet.getPID());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(ts::PID_TDT, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_EQ(1, src.GetNumberOfRemainingPackets());
}

TEST(ServiceFilterTest, Subtitle) {
  TableSource src;
  auto filter = std::make_unique<ServiceFilter>(kOption);
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
        <component elementary_PID="0x0300" stream_type="0x06">
          <stream_identifier_descriptor component_tag="0x30" />
        </component>
        <component elementary_PID="0x0301" stream_type="0x06">
          <stream_identifier_descriptor component_tag="0x31" />
        </component>
        <component elementary_PID="0x0302" stream_type="0x06">
          <stream_identifier_descriptor component_tag="0x32" />
        </component>
        <component elementary_PID="0x0303" stream_type="0x06">
          <stream_identifier_descriptor component_tag="0x33" />
        </component>
        <component elementary_PID="0x0304" stream_type="0x06">
          <stream_identifier_descriptor component_tag="0x34" />
        </component>
        <component elementary_PID="0x0305" stream_type="0x06">
          <stream_identifier_descriptor component_tag="0x35" />
        </component>
        <component elementary_PID="0x0306" stream_type="0x06">
          <stream_identifier_descriptor component_tag="0x36" />
        </component>
        <component elementary_PID="0x0307" stream_type="0x06">
          <stream_identifier_descriptor component_tag="0x37" />
        </component>

      </PMT>
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0300" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0301" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0302" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0303" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0304" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0305" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0306" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0307" />
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
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
                EXPECT_EQ(8, pmt.streams.size());
                EXPECT_TRUE(pmt.streams.find(0x0300) != pmt.streams.end());
                EXPECT_TRUE(pmt.streams.find(0x0301) != pmt.streams.end());
                EXPECT_TRUE(pmt.streams.find(0x0302) != pmt.streams.end());
                EXPECT_TRUE(pmt.streams.find(0x0303) != pmt.streams.end());
                EXPECT_TRUE(pmt.streams.find(0x0304) != pmt.streams.end());
                EXPECT_TRUE(pmt.streams.find(0x0305) != pmt.streams.end());
                EXPECT_TRUE(pmt.streams.find(0x0306) != pmt.streams.end());
                EXPECT_TRUE(pmt.streams.find(0x0307) != pmt.streams.end());
              });
          validator.FeedPacket(packet);
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0300, packet.getPID());
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
          EXPECT_EQ(0x0303, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0304, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0305, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0306, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0307, packet.getPID());
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

TEST(ServiceFilterTest, SuperimposedText) {
  TableSource src;
  auto filter = std::make_unique<ServiceFilter>(kOption);
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
        <component elementary_PID="0x0308" stream_type="0x06">
          <stream_identifier_descriptor component_tag="0x38" />
        </component>
        <component elementary_PID="0x0309" stream_type="0x06">
          <stream_identifier_descriptor component_tag="0x39" />
        </component>
        <component elementary_PID="0x030A" stream_type="0x06">
          <stream_identifier_descriptor component_tag="0x3A" />
        </component>
        <component elementary_PID="0x030B" stream_type="0x06">
          <stream_identifier_descriptor component_tag="0x3B" />
        </component>
        <component elementary_PID="0x030C" stream_type="0x06">
          <stream_identifier_descriptor component_tag="0x3C" />
        </component>
        <component elementary_PID="0x030D" stream_type="0x06">
          <stream_identifier_descriptor component_tag="0x3D" />
        </component>
        <component elementary_PID="0x030E" stream_type="0x06">
          <stream_identifier_descriptor component_tag="0x3E" />
        </component>
        <component elementary_PID="0x030F" stream_type="0x06">
          <stream_identifier_descriptor component_tag="0x3F" />
        </component>

      </PMT>
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0308" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0309" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x030A" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x030B" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x030C" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x030D" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x030E" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x030F" />
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
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
                EXPECT_EQ(8, pmt.streams.size());
                EXPECT_TRUE(pmt.streams.find(0x0308) != pmt.streams.end());
                EXPECT_TRUE(pmt.streams.find(0x0309) != pmt.streams.end());
                EXPECT_TRUE(pmt.streams.find(0x030A) != pmt.streams.end());
                EXPECT_TRUE(pmt.streams.find(0x030B) != pmt.streams.end());
                EXPECT_TRUE(pmt.streams.find(0x030C) != pmt.streams.end());
                EXPECT_TRUE(pmt.streams.find(0x030D) != pmt.streams.end());
                EXPECT_TRUE(pmt.streams.find(0x030E) != pmt.streams.end());
                EXPECT_TRUE(pmt.streams.find(0x030F) != pmt.streams.end());
              });
          validator.FeedPacket(packet);
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0308, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x0309, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x030A, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x030B, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x030C, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x030D, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x030E, packet.getPID());
          EXPECT_EQ(0, packet.getCC());
          return true;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(
        [](const ts::TSPacket& packet) {
          EXPECT_EQ(0x030F, packet.getPID());
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

TEST(ServiceFilterTest, PmtSidUnmatched) {
  TableSource src;
  auto filter = std::make_unique<ServiceFilter>(kOption);
  auto sink = std::make_unique<MockSink>();

  // TDT tables are used for emulating PCR packets.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
        <service service_id="0x0002" program_map_PID="0x0102" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0000" PCR_PID="0x0ABC"
           test-pid="0x0101" />
      <TDT UTC_time="1975-01-01 00:00:00" test-pid="0x0ABC" test_pcr="0" />
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
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
