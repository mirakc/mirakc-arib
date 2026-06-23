// SPDX-License-Identifier: GPL-2.0-or-later

// mirakc-arib
// Copyright (C) 2019 masnagam
//
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation; either version 2 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
// without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See
// the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with this program; if
// not, write to the Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
// 02110-1301, USA.

#include <cstdlib>
#include <memory>
#include <unordered_set>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tsduck/tsduck.h>

#include "service_filter.hh"

#include "test_helper.hh"

namespace {

const ServiceFilterOption kOption{0x0001};

class ServiceFilterTestAccessor final {
 public:
  static const std::unordered_set<ts::PID>& ContentFilter(const ServiceFilter& filter) {
    return filter.content_filter_;
  }

  static const std::unordered_set<ts::PID>& EmmFilter(const ServiceFilter& filter) {
    return filter.emm_filter_;
  }

  static const std::unordered_set<ts::PID>& PsiFilter(const ServiceFilter& filter) {
    return filter.psi_filter_;
  }

  static bool IsSafeDynamicPid(ts::PID pid) {
    return ServiceFilter::IsSafeDynamicPid(pid);
  }

  static const ts::SectionDemux& Demux(const ServiceFilter& filter) {
    return filter.demux_;
  }

  static bool Done(const ServiceFilter& filter) {
    return filter.done_;
  }
};

}  // namespace

TEST(ServiceFilterTest, IsSafeDynamicPid) {
  EXPECT_FALSE(ServiceFilterTestAccessor::IsSafeDynamicPid(ts::PID_NULL));
  EXPECT_FALSE(ServiceFilterTestAccessor::IsSafeDynamicPid(ts::PID_PAT));
  EXPECT_FALSE(ServiceFilterTestAccessor::IsSafeDynamicPid(ts::PID_CAT));
  EXPECT_FALSE(ServiceFilterTestAccessor::IsSafeDynamicPid(ts::PID_NIT));
  EXPECT_FALSE(ServiceFilterTestAccessor::IsSafeDynamicPid(ts::PID_SDT));
  EXPECT_FALSE(ServiceFilterTestAccessor::IsSafeDynamicPid(ts::PID_EIT));
  EXPECT_FALSE(ServiceFilterTestAccessor::IsSafeDynamicPid(ts::PID_RST));
  EXPECT_FALSE(ServiceFilterTestAccessor::IsSafeDynamicPid(ts::PID_TOT));
  EXPECT_FALSE(ServiceFilterTestAccessor::IsSafeDynamicPid(ts::PID_BIT));
  EXPECT_FALSE(ServiceFilterTestAccessor::IsSafeDynamicPid(ts::PID_CDT));

  EXPECT_TRUE(ServiceFilterTestAccessor::IsSafeDynamicPid(0x0030));
  EXPECT_TRUE(ServiceFilterTestAccessor::IsSafeDynamicPid(0x1FFE));
}

TEST(ServiceFilterTest, IgnoreUnsafePmtPidFromPat) {
  TableSource src;
  auto filter = std::make_unique<ServiceFilter>(kOption);
  auto* filter_ptr = filter.get();
  auto sink = std::make_unique<MockSink>();

  // The 1st PAT advertises ts::PID_NULL (0x1FFF) as a PMT PID.
  // The 2nd PAT advertises 0x0101 as a PMT PID.
  // Of the two advertised PMT PIDs, only 0x0101 must be added to psi_filter_ and demux_.
  // ts::PID_NULL must never be added to psi_filter_ or demux_.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x1FFF" />
      </PAT>
      <PAT version="2" current="true" transport_stream_id="0x1234"
           test-pid="0x0000" test-cc="1">
        <service service_id="0x0001" program_map_PID="0x0101" />
      </PAT>
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(ts::PID_PAT, packet.getPID());
      return true;
    });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());

  EXPECT_THAT(ServiceFilterTestAccessor::PsiFilter(*filter_ptr),
      testing::UnorderedElementsAre(ts::PID_PAT, ts::PID_CAT, ts::PID_NIT, ts::PID_SDT,
          ts::PID_EIT, ts::PID_RST, ts::PID_TOT, ts::PID_BIT, ts::PID_CDT, 0x0101));

  // demux_ must still contain the PIDs added in the constructor, in addition to the safe PMT PID
  // (0x0101).
  EXPECT_TRUE(ServiceFilterTestAccessor::Demux(*filter_ptr).hasPID(ts::PID_PAT));
  EXPECT_TRUE(ServiceFilterTestAccessor::Demux(*filter_ptr).hasPID(ts::PID_CAT));
  EXPECT_TRUE(ServiceFilterTestAccessor::Demux(*filter_ptr).hasPID(0x0101));
  // The unsafe PMT PID must never be added to demux_.
  EXPECT_FALSE(ServiceFilterTestAccessor::Demux(*filter_ptr).hasPID(ts::PID_NULL));
}

TEST(ServiceFilterTest, IgnoreUnsafeEmmPidsFromCatDescriptors) {
  TableSource src;
  auto filter = std::make_unique<ServiceFilter>(kOption);
  auto* filter_ptr = filter.get();
  auto sink = std::make_unique<MockSink>();

  // This CAT advertises 0x0101 and ts::PID_EIT (0x0012) as EMM PIDs in CA descriptors.
  // Only 0x0101 must be added to emm_filter_.
  // ts::PID_EIT must never be added to emm_filter_.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <CAT version="1" current="true" test-pid="0x0001">
        <CA_descriptor CA_system_id="0x0005" CA_PID="0x0101" />
        <CA_descriptor CA_system_id="0x0005" CA_PID="0x0012" />
      </CAT>
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  // Since no PAT is fed, psi_filter_ remains empty, so the CAT packet does not reach the sink.
  EXPECT_CALL(*sink, HandlePacket).Times(0);

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());

  EXPECT_THAT(
      ServiceFilterTestAccessor::EmmFilter(*filter_ptr), testing::UnorderedElementsAre(0x0101));
}

TEST(ServiceFilterTest, IgnoreUnsafePcrPidFromPmt) {
  TableSource src;
  auto filter = std::make_unique<ServiceFilter>(kOption);
  auto* filter_ptr = filter.get();
  auto sink = std::make_unique<MockSink>();

  // This PMT advertises ts::PID_NULL (0x1FFF) as a PCR PID and 0x0103 as a stream PID.
  // Only 0x0103 must be added to content_filter_.
  // ts::PID_NULL must never be added to content_filter_.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x1FFF"
           test-pid="0x0101">
        <component elementary_PID="0x0103" stream_type="0x02" />
      </PMT>
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(ts::PID_PAT, packet.getPID());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0101, packet.getPID());
      return true;
    });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());

  EXPECT_THAT(ServiceFilterTestAccessor::ContentFilter(*filter_ptr),
      testing::UnorderedElementsAre(0x0103));
}

TEST(ServiceFilterTest, IgnoreUnsafeEcmPidsFromPmtDescriptors) {
  TableSource src;
  auto filter = std::make_unique<ServiceFilter>(kOption);
  auto* filter_ptr = filter.get();
  auto sink = std::make_unique<MockSink>();

  // This PMT advertises 0x0103 and ts::PID_NIT (0x0010) as ECM PIDs.
  // Of the two advertised ECM PIDs, only 0x0103 must be added to content_filter_.
  // ts::PID_NIT must never be added to content_filter_.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x0102"
           test-pid="0x0101">
        <CA_descriptor CA_system_id="0x0005" CA_PID="0x0103" />
        <CA_descriptor CA_system_id="0x0005" CA_PID="0x0010" />
      </PMT>
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(ts::PID_PAT, packet.getPID());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0101, packet.getPID());
      return true;
    });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());

  EXPECT_THAT(ServiceFilterTestAccessor::ContentFilter(*filter_ptr),
      testing::UnorderedElementsAre(0x0102, 0x0103));
}

TEST(ServiceFilterTest, IgnoreUnsafeStreamPidsFromPmtStreams) {
  TableSource src;
  auto filter = std::make_unique<ServiceFilter>(kOption);
  auto* filter_ptr = filter.get();
  auto sink = std::make_unique<MockSink>();

  // This PMT advertises 0x0103 and ts::PID_NULL (0x1FFF) as stream PIDs.
  // Of the two advertised stream PIDs, only 0x0103 must be added to content_filter_.
  // ts::PID_NULL must never be added to content_filter_.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
      </PAT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x0102"
           test-pid="0x0101">
        <component elementary_PID="0x0103" stream_type="0x02" />
        <component elementary_PID="0x1FFF" stream_type="0x06" />
      </PMT>
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(ts::PID_PAT, packet.getPID());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0101, packet.getPID());
      return true;
    });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());

  EXPECT_THAT(ServiceFilterTestAccessor::ContentFilter(*filter_ptr),
      testing::UnorderedElementsAre(0x0102, 0x0103));
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
    EXPECT_CALL(src, GetNextPacket).WillOnce([](ts::TSPacket* packet) {
      *packet = ts::NullPacket;
      return true;
    });
    EXPECT_CALL(src, GetNextPacket).WillOnce(testing::Return(false));  // EOF
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

  // <generic_short_table> elements are used for emulating PES packets.
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
      <generic_short_table table_id="0xFF" test-pid="0x0301" />
      <generic_short_table table_id="0xFF" test-pid="0x0302" />
      <generic_short_table table_id="0xFF" test-pid="0x0303" />
      <generic_short_table table_id="0xFF" test-pid="0x0311" />
      <generic_short_table table_id="0xFF" test-pid="0x0312" />
      <generic_short_table table_id="0xFF" test-pid="0x0313" />
      <EIT type="pf" version="1" current="true" actual="true"
           service_id="0x0001" transport_stream_id="0x1234"
           original_network_id="0x0001" last_table_id="0x4E"
           test-pid="0x0012">
        <event event_id="0x1001" start_time="2019-01-02 03:00:00"
               duration="01:00:00" running_status="starting" CA_mode="true" />
      </EIT>
      <generic_short_table table_id="0xFF" test-pid="0x0301" test-cc="1" />
      <generic_short_table table_id="0xFF" test-pid="0x0302" test-cc="1" />
      <generic_short_table table_id="0xFF" test-pid="0x0311" test-cc="1" />
      <generic_short_table table_id="0xFF" test-pid="0x0312" test-cc="1" />
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(ts::PID_PAT, packet.getPID());
      TableValidator<ts::PAT> validator(ts::PID_PAT);
      EXPECT_CALL(validator, Validate).WillOnce([](const ts::PAT& pat) {
        EXPECT_TRUE(pat.isValid());
        EXPECT_EQ(1, pat.pmts.size());
        EXPECT_EQ(0x0101, pat.pmts.at(0x0001));
      });
      validator.FeedPacket(packet);
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0101, packet.getPID());
      TableValidator<ts::PMT> validator(0x0101);
      EXPECT_CALL(validator, Validate).WillOnce([](const ts::PMT& pmt) {
        EXPECT_TRUE(pmt.isValid());
        EXPECT_EQ(3, pmt.streams.size());
        EXPECT_TRUE(pmt.streams.find(0x0301) != pmt.streams.end());
        EXPECT_TRUE(pmt.streams.find(0x0302) != pmt.streams.end());
        EXPECT_TRUE(pmt.streams.find(0x0303) != pmt.streams.end());
      });
      validator.FeedPacket(packet);
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(ts::PID_TOT, packet.getPID());
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
      EXPECT_EQ(0x0303, packet.getPID());
      EXPECT_EQ(0, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(ts::PID_EIT, packet.getPID());
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

  // <generic_short_table> elements are used for emulating PES packets.
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
      <generic_short_table table_id="0xFF" test-pid="0x0301" />
      <generic_short_table table_id="0xFF" test-pid="0x0302" />
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
      <generic_short_table table_id="0xFF" test-pid="0x0301" test-cc="1" />
      <generic_short_table table_id="0xFF" test-pid="0x0302" test-cc="1" />
      <generic_short_table table_id="0xFF" test-pid="0x0303" />
      <generic_short_table table_id="0xFF" test-pid="0x0304" />
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(ts::PID_PAT, packet.getPID());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0101, packet.getPID());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0301, packet.getPID());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0302, packet.getPID());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(ts::PID_PAT, packet.getPID());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0102, packet.getPID());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0303, packet.getPID());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
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

  // <generic_short_table> elements are used for emulating PES packets.
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
      <generic_short_table table_id="0xFF" test-pid="0x0301" />
      <generic_short_table table_id="0xFF" test-pid="0x0302" />
      <PMT version="2" current="true" service_id="0x0001" PCR_PID="0x901"
           test-pid="0x0101" test-cc="1">
        <component elementary_PID="0x0303" stream_type="0x02" />
        <component elementary_PID="0x0304" stream_type="0x0F" />
      </PMT>
      <generic_short_table table_id="0xFF" test-pid="0x0301" test-cc="1" />
      <generic_short_table table_id="0xFF" test-pid="0x0302" test-cc="1" />
      <generic_short_table table_id="0xFF" test-pid="0x0303" />
      <generic_short_table table_id="0xFF" test-pid="0x0304" />
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(ts::PID_PAT, packet.getPID());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0101, packet.getPID());
      EXPECT_EQ(0, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0301, packet.getPID());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0302, packet.getPID());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0101, packet.getPID());
      EXPECT_EQ(1, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0303, packet.getPID());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
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
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(ts::PID_PAT, packet.getPID());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
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

TEST(ServiceFilterTest, IgnoreTotWhenTimeLimitIsUnset) {
  TableSource src;
  auto filter = std::make_unique<ServiceFilter>(kOption);
  auto* filter_ptr = filter.get();
  auto sink = std::make_unique<MockSink>();

  // When `option_.time_limit` is unset, HandleTot must ignore the TOT; done_ must
  // remain false so that streaming continues.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <!-- PID_TOT is demuxed only when a time limit is set, but a malformed TS can
           still carry a TOT on another demuxed PID. 0x0001 is PID_CAT, which is
           always demuxed, so this TOT reaches HandleTot. -->
      <TOT UTC_time="2019-01-02 03:04:05" test-pid="0x0001" />
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  // No PAT is fed, so psi_filter_ remains empty and CheckFilterForDrop drops
  // every packet, including PID_CAT (0x0001). Therefore, no packets reach the sink.
  EXPECT_CALL(*sink, HandlePacket).Times(0);

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());

  EXPECT_FALSE(ServiceFilterTestAccessor::Done(*filter_ptr));
}

TEST(ServiceFilterTest, Subtitle) {
  TableSource src;
  auto filter = std::make_unique<ServiceFilter>(kOption);
  auto sink = std::make_unique<MockSink>();

  // <generic_short_table> elements are used for emulating PES packets.
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
      <generic_short_table table_id="0xFF" test-pid="0x0300" />
      <generic_short_table table_id="0xFF" test-pid="0x0301" />
      <generic_short_table table_id="0xFF" test-pid="0x0302" />
      <generic_short_table table_id="0xFF" test-pid="0x0303" />
      <generic_short_table table_id="0xFF" test-pid="0x0304" />
      <generic_short_table table_id="0xFF" test-pid="0x0305" />
      <generic_short_table table_id="0xFF" test-pid="0x0306" />
      <generic_short_table table_id="0xFF" test-pid="0x0307" />
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(ts::PID_PAT, packet.getPID());
      TableValidator<ts::PAT> validator(ts::PID_PAT);
      EXPECT_CALL(validator, Validate).WillOnce([](const ts::PAT& pat) {
        EXPECT_TRUE(pat.isValid());
        EXPECT_EQ(1, pat.pmts.size());
        EXPECT_EQ(0x0101, pat.pmts.at(0x0001));
      });
      validator.FeedPacket(packet);
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0101, packet.getPID());
      TableValidator<ts::PMT> validator(0x0101);
      EXPECT_CALL(validator, Validate).WillOnce([](const ts::PMT& pmt) {
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
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0300, packet.getPID());
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
      EXPECT_EQ(0x0303, packet.getPID());
      EXPECT_EQ(0, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0304, packet.getPID());
      EXPECT_EQ(0, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0305, packet.getPID());
      EXPECT_EQ(0, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0306, packet.getPID());
      EXPECT_EQ(0, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
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

  // <generic_short_table> elements are used for emulating PES packets.
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
      <generic_short_table table_id="0xFF" test-pid="0x0308" />
      <generic_short_table table_id="0xFF" test-pid="0x0309" />
      <generic_short_table table_id="0xFF" test-pid="0x030A" />
      <generic_short_table table_id="0xFF" test-pid="0x030B" />
      <generic_short_table table_id="0xFF" test-pid="0x030C" />
      <generic_short_table table_id="0xFF" test-pid="0x030D" />
      <generic_short_table table_id="0xFF" test-pid="0x030E" />
      <generic_short_table table_id="0xFF" test-pid="0x030F" />
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(ts::PID_PAT, packet.getPID());
      TableValidator<ts::PAT> validator(ts::PID_PAT);
      EXPECT_CALL(validator, Validate).WillOnce([](const ts::PAT& pat) {
        EXPECT_TRUE(pat.isValid());
        EXPECT_EQ(1, pat.pmts.size());
        EXPECT_EQ(0x0101, pat.pmts.at(0x0001));
      });
      validator.FeedPacket(packet);
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0101, packet.getPID());
      TableValidator<ts::PMT> validator(0x0101);
      EXPECT_CALL(validator, Validate).WillOnce([](const ts::PMT& pmt) {
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
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0308, packet.getPID());
      EXPECT_EQ(0, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x0309, packet.getPID());
      EXPECT_EQ(0, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x030A, packet.getPID());
      EXPECT_EQ(0, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x030B, packet.getPID());
      EXPECT_EQ(0, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x030C, packet.getPID());
      EXPECT_EQ(0, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x030D, packet.getPID());
      EXPECT_EQ(0, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(0x030E, packet.getPID());
      EXPECT_EQ(0, packet.getCC());
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
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

  // <generic_short_table> elements are used for emulating PCR packets.
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
      <generic_short_table table_id="0xFF" test-pid="0x0ABC" test_pcr="0" />
    </tsduck>
  )");

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
      EXPECT_EQ(ts::PID_PAT, packet.getPID());
      TableValidator<ts::PAT> validator(ts::PID_PAT);
      EXPECT_CALL(validator, Validate).WillOnce([](const ts::PAT& pat) {
        EXPECT_TRUE(pat.isValid());
        EXPECT_EQ(1, pat.pmts.size());
        EXPECT_EQ(0x0101, pat.pmts.at(0x0001));
      });
      validator.FeedPacket(packet);
      return true;
    });
    EXPECT_CALL(*sink, HandlePacket).WillOnce([](const ts::TSPacket& packet) {
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
