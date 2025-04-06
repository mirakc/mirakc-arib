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

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tsduck/tsduck.h>

#include "pcr_synchronizer.hh"

#include "test_helper.hh"

namespace {
const PcrSynchronizerOption kEmptyOption{};
}

TEST(PcrSynchronizerTest, NoPacket) {
  MockSource src;
  auto sync = std::make_unique<PcrSynchronizer>(kEmptyOption);
  auto sink = std::make_unique<MockJsonlSink>();

  EXPECT_CALL(src, GetNextPacket).WillOnce(testing::Return(false));  // EOF
  EXPECT_CALL(*sink, HandleDocument).Times(0);                       // Never called

  sync->Connect(std::move(sink));
  src.Connect(std::move(sync));
  EXPECT_EQ(EXIT_FAILURE, src.FeedPackets());
}

TEST(PcrSynchronizerTest, Successful) {
  TableSource src;
  auto sync = std::make_unique<PcrSynchronizer>(kEmptyOption);
  auto sink = std::make_unique<MockJsonlSink>();

  // <generic_short_table> elements are used for emulating PCR packets.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
        <service service_id="0x0002" program_map_PID="0x0102" />
      </PAT>
      <SDT version="1" current="true" actual="true" transport_stream_id="0x0003"
           original_network_id="0x0002" test-pid="0x0011">
        <service service_id="0x0001" EIT_schedule="false"
                 EIT_present_following="true" CA_mode="false"
                 running_status="undefined">
          <service_descriptor service_type="0x01"
                              service_provider_name="test"
                              service_name="service-1" />
        </service>
        <service service_id="0x0002" EIT_schedule="false"
                 EIT_present_following="true" CA_mode="false"
                 running_status="undefined">
          <service_descriptor service_type="0x01"
                              service_provider_name="test"
                              service_name="service-2" />
        </service>
      </SDT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x901"
           test-pid="0x0101" />
      <PMT version="1" current="true" service_id="0x0002" PCR_PID="0x902"
           test-pid="0x0102" />
      <generic_short_table table_id="0xFF" test-pid="0x0901" test-pcr="101" />
      <generic_short_table table_id="0xFF" test-pid="0x0902" test-pcr="201" />
      <TOT UTC_time="2019-01-02 03:04:05" test-pid="0x0014" test-cc="0" />
      <generic_short_table table_id="0xFF" test-pid="0x0901" test-pcr="102" />
      <generic_short_table table_id="0xFF" test-pid="0x0902" test-pcr="202" />
      <generic_short_table table_id="0xFF" test-pid="0x0901" test-pcr="103" />
      <generic_short_table table_id="0xFF" test-pid="0x0902" test-pcr="203" />
    </tsduck>
  )");

  EXPECT_CALL(*sink, HandleDocument).WillOnce([](const rapidjson::Document& doc) {
    EXPECT_EQ(
        "[{"
        R"("nid":2,)"
        R"("tsid":3,)"
        R"("sid":1,)"
        R"("clock":{)"
        R"("pid":2305,)"
        R"("pcr":102,)"
        R"("time":1546365845000)"
        R"(})"
        "},{"
        R"("nid":2,)"
        R"("tsid":3,)"
        R"("sid":2,)"
        R"("clock":{)"
        R"("pid":2306,)"
        R"("pcr":202,)"
        R"("time":1546365845000)"
        R"(})"
        "}]",
        MockJsonlSink::Stringify(doc));
    return true;
  });

  sync->Connect(std::move(sink));
  src.Connect(std::move(sync));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_EQ(2, src.GetNumberOfRemainingPackets());
}

TEST(PcrSynchronizerTest, Reset) {
  TableSource src;
  auto sync = std::make_unique<PcrSynchronizer>(kEmptyOption);
  auto sink = std::make_unique<MockJsonlSink>();

  // <generic_short_table> elements are used for emulating PCR packets.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
        <service service_id="0x0002" program_map_PID="0x0102" />
      </PAT>
      <SDT version="1" current="true" actual="true" transport_stream_id="0x0003"
           original_network_id="0x0002" test-pid="0x0011">
        <service service_id="0x0001" EIT_schedule="false"
                 EIT_present_following="true" CA_mode="false"
                 running_status="undefined">
          <service_descriptor service_type="0x01"
                              service_provider_name="test"
                              service_name="service-1" />
        </service>
        <service service_id="0x0002" EIT_schedule="false"
                 EIT_present_following="true" CA_mode="false"
                 running_status="undefined">
          <service_descriptor service_type="0x01"
                              service_provider_name="test"
                              service_name="service-2" />
        </service>
      </SDT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x901"
           test-pid="0x0101" />
      <PMT version="1" current="true" service_id="0x0002" PCR_PID="0x902"
           test-pid="0x0102" />
      <generic_short_table table_id="0xFF" test-pid="0x0901" test-pcr="101" />
      <generic_short_table table_id="0xFF" test-pid="0x0902" test-pcr="201" />
      <TOT UTC_time="2019-01-02 03:04:05" test-pid="0x0014" test-cc="0" />
      <generic_short_table table_id="0xFF" test-pid="0x0901" test-pcr="102" />
      <PAT version="2" current="true" transport_stream_id="0x1234"
           test-pid="0x0000" test-cc="2">
        <service service_id="0x0001" program_map_PID="0x0101" />
        <service service_id="0x0002" program_map_PID="0x0102" />
      </PAT>
      <SDT version="2" current="true" actual="true" transport_stream_id="0x0003"
           original_network_id="0x0002" test-pid="0x0011" test-cc="2">
        <service service_id="0x0001" EIT_schedule="false"
                 EIT_present_following="true" CA_mode="false"
                 running_status="undefined">
          <service_descriptor service_type="0x01"
                              service_provider_name="test"
                              service_name="service-1" />
        </service>
        <service service_id="0x0002" EIT_schedule="false"
                 EIT_present_following="true" CA_mode="false"
                 running_status="undefined">
          <service_descriptor service_type="0x01"
                              service_provider_name="test"
                              service_name="service-2" />
        </service>
      </SDT>
      <PMT version="2" current="true" service_id="0x0001" PCR_PID="0x901"
           test-pid="0x0101" test-cc="2" />
      <PMT version="2" current="true" service_id="0x0002" PCR_PID="0x902"
           test-pid="0x0102" test-cc="2" />
      <generic_short_table table_id="0xFF" test-pid="0x0902" test-pcr="202" />
      <TOT UTC_time="2019-01-02 03:04:10" test-pid="0x0014" test-cc="1" />
      <generic_short_table table_id="0xFF" test-pid="0x0901" test-pcr="103" />
      <generic_short_table table_id="0xFF" test-pid="0x0902" test-pcr="203" />
    </tsduck>
  )");

  EXPECT_CALL(*sink, HandleDocument).WillOnce([](const rapidjson::Document& doc) {
    EXPECT_EQ(
        "[{"
        R"("nid":2,)"
        R"("tsid":3,)"
        R"("sid":1,)"
        R"("clock":{)"
        R"("pid":2305,)"
        R"("pcr":103,)"
        R"("time":1546365850000)"
        R"(})"
        "},{"
        R"("nid":2,)"
        R"("tsid":3,)"
        R"("sid":2,)"
        R"("clock":{)"
        R"("pid":2306,)"
        R"("pcr":203,)"
        R"("time":1546365850000)"
        R"(})"
        "}]",
        MockJsonlSink::Stringify(doc));
    return true;
  });

  sync->Connect(std::move(sink));
  src.Connect(std::move(sync));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(PcrSynchronizerTest, NoPcr) {
  TableSource src;
  auto sync = std::make_unique<PcrSynchronizer>(kEmptyOption);
  auto sink = std::make_unique<MockJsonlSink>();

  // <generic_short_table> elements are used for emulating PCR packets.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
        <service service_id="0x0002" program_map_PID="0x0102" />
      </PAT>
      <SDT version="1" current="true" actual="true" transport_stream_id="0x0003"
           original_network_id="0x0002" test-pid="0x0011">
        <service service_id="0x0001" EIT_schedule="false"
                 EIT_present_following="true" CA_mode="false"
                 running_status="undefined">
          <service_descriptor service_type="0x01"
                              service_provider_name="test"
                              service_name="service-1" />
        </service>
        <service service_id="0x0002" EIT_schedule="false"
                 EIT_present_following="true" CA_mode="false"
                 running_status="undefined">
          <service_descriptor service_type="0x01"
                              service_provider_name="test"
                              service_name="service-2" />
        </service>
      </SDT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x901"
           test-pid="0x0101" />
      <PMT version="1" current="true" service_id="0x0002" PCR_PID="0x902"
           test-pid="0x0102" />
      <TOT UTC_time="2019-01-02 03:04:05" test-pid="0x0014" test-cc="0" />
      <generic_short_table table_id="0xFF" test-pid="0x0903" test-pcr="302" />
      <generic_short_table table_id="0xFF" test-pid="0x0903" test-pcr="303" />
    </tsduck>
  )");

  EXPECT_CALL(*sink, HandleDocument).Times(0);  // Never called

  sync->Connect(std::move(sink));
  src.Connect(std::move(sync));
  EXPECT_EQ(EXIT_FAILURE, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(PcrSynchronizerTest, InclusionList) {
  PcrSynchronizerOption option;
  option.sids.Add(0x0001);

  TableSource src;
  auto sync = std::make_unique<PcrSynchronizer>(option);
  auto sink = std::make_unique<MockJsonlSink>();

  // <generic_short_table> elements are used for emulating PCR packets.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
        <service service_id="0x0002" program_map_PID="0x0102" />
      </PAT>
      <SDT version="1" current="true" actual="true" transport_stream_id="0x0003"
           original_network_id="0x0002" test-pid="0x0011">
        <service service_id="0x0001" EIT_schedule="false"
                 EIT_present_following="true" CA_mode="false"
                 running_status="undefined">
          <service_descriptor service_type="0x01"
                              service_provider_name="test"
                              service_name="service-1" />
        </service>
        <service service_id="0x0002" EIT_schedule="false"
                 EIT_present_following="true" CA_mode="false"
                 running_status="undefined">
          <service_descriptor service_type="0x01"
                              service_provider_name="test"
                              service_name="service-2" />
        </service>
      </SDT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x901"
           test-pid="0x0101" />
      <PMT version="1" current="true" service_id="0x0002" PCR_PID="0x902"
           test-pid="0x0102" />
      <generic_short_table table_id="0xFF" test-pid="0x0901" test-pcr="101" />
      <generic_short_table table_id="0xFF" test-pid="0x0902" test-pcr="201" />
      <TOT UTC_time="2019-01-02 03:04:05" test-pid="0x0014" test-cc="0" />
      <generic_short_table table_id="0xFF" test-pid="0x0901" test-pcr="102" />
      <generic_short_table table_id="0xFF" test-pid="0x0902" test-pcr="202" />
      <generic_short_table table_id="0xFF" test-pid="0x0901" test-pcr="103" />
      <generic_short_table table_id="0xFF" test-pid="0x0902" test-pcr="203" />
    </tsduck>
  )");

  EXPECT_CALL(*sink, HandleDocument).WillOnce([](const rapidjson::Document& doc) {
    EXPECT_EQ(
        "[{"
        R"("nid":2,)"
        R"("tsid":3,)"
        R"("sid":1,)"
        R"("clock":{)"
        R"("pid":2305,)"
        R"("pcr":102,)"
        R"("time":1546365845000)"
        R"(})"
        "}]",
        MockJsonlSink::Stringify(doc));
    return true;
  });

  sync->Connect(std::move(sink));
  src.Connect(std::move(sync));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_EQ(3, src.GetNumberOfRemainingPackets());
}

TEST(PcrSynchronizerTest, ExclusionList) {
  PcrSynchronizerOption option;
  option.xsids.Add(0x0001);

  TableSource src;
  auto sync = std::make_unique<PcrSynchronizer>(option);
  auto sink = std::make_unique<MockJsonlSink>();

  // <generic_short_table> elements are used for emulating PCR packets.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
        <service service_id="0x0002" program_map_PID="0x0102" />
      </PAT>
      <SDT version="1" current="true" actual="true" transport_stream_id="0x0003"
           original_network_id="0x0002" test-pid="0x0011">
        <service service_id="0x0001" EIT_schedule="false"
                 EIT_present_following="true" CA_mode="false"
                 running_status="undefined">
          <service_descriptor service_type="0x01"
                              service_provider_name="test"
                              service_name="service-1" />
        </service>
        <service service_id="0x0002" EIT_schedule="false"
                 EIT_present_following="true" CA_mode="false"
                 running_status="undefined">
          <service_descriptor service_type="0x01"
                              service_provider_name="test"
                              service_name="service-2" />
        </service>
      </SDT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x901"
           test-pid="0x0101" />
      <PMT version="1" current="true" service_id="0x0002" PCR_PID="0x902"
           test-pid="0x0102" />
      <generic_short_table table_id="0xFF" test-pid="0x0901" test-pcr="101" />
      <generic_short_table table_id="0xFF" test-pid="0x0902" test-pcr="201" />
      <TOT UTC_time="2019-01-02 03:04:05" test-pid="0x0014" test-cc="0" />
      <generic_short_table table_id="0xFF" test-pid="0x0901" test-pcr="102" />
      <generic_short_table table_id="0xFF" test-pid="0x0902" test-pcr="202" />
      <generic_short_table table_id="0xFF" test-pid="0x0901" test-pcr="103" />
      <generic_short_table table_id="0xFF" test-pid="0x0902" test-pcr="203" />
    </tsduck>
  )");

  EXPECT_CALL(*sink, HandleDocument).WillOnce([](const rapidjson::Document& doc) {
    EXPECT_EQ(
        "[{"
        R"("nid":2,)"
        R"("tsid":3,)"
        R"("sid":2,)"
        R"("clock":{)"
        R"("pid":2306,)"
        R"("pcr":202,)"
        R"("time":1546365845000)"
        R"(})"
        "}]",
        MockJsonlSink::Stringify(doc));
    return true;
  });

  sync->Connect(std::move(sink));
  src.Connect(std::move(sync));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_EQ(2, src.GetNumberOfRemainingPackets());
}

TEST(PcrSynchronizerTest, AbnormalPcrPackets) {
  TableSource src;
  auto sync = std::make_unique<PcrSynchronizer>(kEmptyOption);
  auto sink = std::make_unique<MockJsonlSink>();

  // <generic_short_table> elements are used for emulating PCR packets.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
        <service service_id="0x0002" program_map_PID="0x0102" />
      </PAT>
      <SDT version="1" current="true" actual="true" transport_stream_id="0x0003"
           original_network_id="0x0002" test-pid="0x0011">
        <service service_id="0x0001" EIT_schedule="false"
                 EIT_present_following="true" CA_mode="false"
                 running_status="undefined">
          <service_descriptor service_type="0x01"
                              service_provider_name="test"
                              service_name="service-1" />
        </service>
        <service service_id="0x0002" EIT_schedule="false"
                 EIT_present_following="true" CA_mode="false"
                 running_status="undefined">
          <service_descriptor service_type="0x01"
                              service_provider_name="test"
                              service_name="service-2" />
        </service>
      </SDT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x901"
           test-pid="0x0101" />
      <PMT version="1" current="true" service_id="0x0002" PCR_PID="0x902"
           test-pid="0x0102" />
      <generic_short_table table_id="0xFF" test-pid="0x0901"
           test-pcr="101" />
      <generic_short_table table_id="0xFF" test-pid="0x0902"
           test-pcr="201" />
      <TOT UTC_time="2019-01-02 03:04:05" test-pid="0x0014" test-cc="0" />
      <generic_short_table table_id="0xFF" test-pid="0x0901"
           test-cc="1" />
      <generic_short_table table_id="0xFF" test-pid="0x0902"
           test-cc="1" />
      <generic_short_table table_id="0xFF" test-pid="0x0901"
           test-pcr="0xFFFFFFFFFFFFFFFF" test-cc="2" />
      <generic_short_table table_id="0xFF" test-pid="0x0902"
           test-pcr="0xFFFFFFFFFFFFFFFF" test-cc="2" />
      <generic_short_table table_id="0xFF" test-pid="0x0901"
           test-pcr="102" test-cc="3" />
      <generic_short_table table_id="0xFF" test-pid="0x0902"
           test-pcr="202" test-cc="3" />
      <generic_short_table table_id="0xFF" test-pid="0x0901"
           test-pcr="103" test-cc="4" />
      <generic_short_table table_id="0xFF" test-pid="0x0902"
           test-pcr="203" test-cc="4" />
    </tsduck>
  )");

  EXPECT_CALL(*sink, HandleDocument).WillOnce([](const rapidjson::Document& doc) {
    EXPECT_EQ(
        "[{"
        R"("nid":2,)"
        R"("tsid":3,)"
        R"("sid":1,)"
        R"("clock":{)"
        R"("pid":2305,)"
        R"("pcr":102,)"
        R"("time":1546365845000)"
        R"(})"
        "},{"
        R"("nid":2,)"
        R"("tsid":3,)"
        R"("sid":2,)"
        R"("clock":{)"
        R"("pid":2306,)"
        R"("pcr":202,)"
        R"("time":1546365845000)"
        R"(})"
        "}]",
        MockJsonlSink::Stringify(doc));
    return true;
  });

  sync->Connect(std::move(sink));
  src.Connect(std::move(sync));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_EQ(2, src.GetNumberOfRemainingPackets());
}

TEST(PcrSynchronizerTest, PmtSidUnmatched) {
  TableSource src;
  auto sync = std::make_unique<PcrSynchronizer>(kEmptyOption);
  auto sink = std::make_unique<MockJsonlSink>();

  // <generic_short_table> elements are used for emulating PCR packets.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
        <service service_id="0x0002" program_map_PID="0x0102" />
      </PAT>
      <SDT version="1" current="true" actual="true" transport_stream_id="0x0003"
           original_network_id="0x0002" test-pid="0x0011">
        <service service_id="0x0001" EIT_schedule="false"
                 EIT_present_following="true" CA_mode="false"
                 running_status="undefined">
          <service_descriptor service_type="0x01"
                              service_provider_name="test"
                              service_name="service-1" />
        </service>
        <service service_id="0x0002" EIT_schedule="false"
                 EIT_present_following="true" CA_mode="false"
                 running_status="undefined">
          <service_descriptor service_type="0x01"
                              service_provider_name="test"
                              service_name="service-2" />
        </service>
      </SDT>
      <PMT version="1" current="true" service_id="0x1000" PCR_PID="0x0ABC"
           test-pid="0x0101" />
      <TOT UTC_time="2019-01-02 03:04:05" test-pid="0x0014" test-cc="0" />
      <generic_short_table table_id="0xFF" test-pid="0x0ABC" test-pcr="302" />
    </tsduck>
  )");

  EXPECT_CALL(*sink, HandleDocument).Times(0);  // Never called

  sync->Connect(std::move(sink));
  src.Connect(std::move(sync));
  EXPECT_EQ(EXIT_FAILURE, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(PcrSynchronizerTest, PmtPidUnmatched) {
  TableSource src;
  auto sync = std::make_unique<PcrSynchronizer>(kEmptyOption);
  auto sink = std::make_unique<MockJsonlSink>();

  // <generic_short_table> elements are used for emulating PCR packets.
  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
        <service service_id="0x0002" program_map_PID="0x0102" />
      </PAT>
      <SDT version="1" current="true" actual="true" transport_stream_id="0x0003"
           original_network_id="0x0002" test-pid="0x0011">
        <service service_id="0x0001" EIT_schedule="false"
                 EIT_present_following="true" CA_mode="false"
                 running_status="undefined">
          <service_descriptor service_type="0x01"
                              service_provider_name="test"
                              service_name="service-1" />
        </service>
        <service service_id="0x0002" EIT_schedule="false"
                 EIT_present_following="true" CA_mode="false"
                 running_status="undefined">
          <service_descriptor service_type="0x01"
                              service_provider_name="test"
                              service_name="service-2" />
        </service>
      </SDT>
      <PMT version="1" current="true" service_id="0x0001" PCR_PID="0x0ABC"
           test-pid="0x0103" />
      <TOT UTC_time="2019-01-02 03:04:05" test-pid="0x0014" test-cc="0" />
      <generic_short_table table_id="0xFF" test-pid="0x0ABC" test-pcr="302" />
    </tsduck>
  )");

  EXPECT_CALL(*sink, HandleDocument).Times(0);  // Never called

  sync->Connect(std::move(sink));
  src.Connect(std::move(sync));
  EXPECT_EQ(EXIT_FAILURE, src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}
