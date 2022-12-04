#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tsduck/tsduck.h>

#include "service_scanner.hh"

#include "test_helper.hh"

namespace {
const ServiceScannerOption kEmptyOption {};
}

TEST(ServiceScannerTest, NoPacket) {
  MockSource src;
  auto scanner = std::make_unique<ServiceScanner>(kEmptyOption);
  auto sink = std::make_unique<MockJsonlSink>();

  EXPECT_CALL(src, GetNextPacket).WillOnce(testing::Return(false));  // EOF
  EXPECT_CALL(*sink, HandleDocument).Times(0);  // Never called

  scanner->Connect(std::move(sink));
  src.Connect(std::move(scanner));
  EXPECT_FALSE(src.FeedPackets());
}

TEST(ServiceScannerTest, Complete) {
  TableSource src;
  auto scanner = std::make_unique<ServiceScanner>(kEmptyOption);
  auto sink = std::make_unique<MockJsonlSink>();

  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x0003"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
        <service service_id="0x0002" program_map_PID="0x0102" />
      </PAT>
      <NIT version="1" current="true" actual="true" network_id="0x0001"
           test-pid="0x0010">
        <transport_stream transport_stream_id="0x1234"
                          original_network_id="0x0002" />
      </NIT>
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
    </tsduck>
  )");

  EXPECT_CALL(*sink, HandleDocument).WillOnce(
      [](const rapidjson::Document& doc) {
        EXPECT_EQ(
            "[{"
              R"("nid":2,)"
              R"("tsid":3,)"
              R"("sid":1,)"
              R"("name":"service-1",)"
              R"("type":1,)"
              R"("logoId":-1)"
            "},{"
              R"("nid":2,)"
              R"("tsid":3,)"
              R"("sid":2,)"
              R"("name":"service-2",)"
              R"("type":1,)"
              R"("logoId":-1)"
            "}]",
            MockJsonlSink::Stringify(doc));
        return true;
      });

  scanner->Connect(std::move(sink));
  src.Connect(std::move(scanner));
  EXPECT_TRUE(src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
  EXPECT_EQ(EXIT_SUCCESS, src.GetExitCode());
}

TEST(ServiceScannerTest, NoPat) {
  TableSource src;
  auto scanner = std::make_unique<ServiceScanner>(kEmptyOption);
  auto sink = std::make_unique<MockJsonlSink>();

  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <NIT version="1" current="true" actual="true" network_id="0x0001"
           test-pid="0x0010">
        <transport_stream transport_stream_id="0x1234"
                          original_network_id="0x0002" />
      </NIT>
      <SDT version="1" current="true" actual="true" transport_stream_id="0x1234"
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
    </tsduck>
  )");

  EXPECT_CALL(*sink, HandleDocument).Times(0);  // Never called

  scanner->Connect(std::move(sink));
  src.Connect(std::move(scanner));
  EXPECT_FALSE(src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(ServiceScannerTest, NoNit) {
  TableSource src;
  auto scanner = std::make_unique<ServiceScanner>(kEmptyOption);
  auto sink = std::make_unique<MockJsonlSink>();

  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
        <service service_id="0x0002" program_map_PID="0x0102" />
      </PAT>
      <SDT version="1" current="true" actual="true" transport_stream_id="0x1234"
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
    </tsduck>
  )");

  EXPECT_CALL(*sink, HandleDocument).Times(0);  // Never called

  scanner->Connect(std::move(sink));
  src.Connect(std::move(scanner));
  EXPECT_FALSE(src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(ServiceScannerTest, NoSdt) {
  TableSource src;
  auto scanner = std::make_unique<ServiceScanner>(kEmptyOption);
  auto sink = std::make_unique<MockJsonlSink>();

  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x1234"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
        <service service_id="0x0002" program_map_PID="0x0102" />
      </PAT>
      <NIT version="1" current="true" actual="true" network_id="0x0001"
           test-pid="0x0010">
        <transport_stream transport_stream_id="0x1234"
                          original_network_id="0x0002" />
      </NIT>
    </tsduck>
  )");

  EXPECT_CALL(*sink, HandleDocument).Times(0);  // Never called

  scanner->Connect(std::move(sink));
  src.Connect(std::move(scanner));
  EXPECT_FALSE(src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
}

TEST(ServiceScannerTest, NonStandardNit) {
  TableSource src;
  auto scanner = std::make_unique<ServiceScanner>(kEmptyOption);
  auto sink = std::make_unique<MockJsonlSink>();

  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x0003"
           network_PID="0x1010" test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
        <service service_id="0x0002" program_map_PID="0x0102" />
      </PAT>
      <NIT version="1" current="true" actual="true" network_id="0x0001"
           test-pid="0x1010">
        <transport_stream transport_stream_id="0x1234"
                          original_network_id="0x0002" />
      </NIT>
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
    </tsduck>
  )");

  EXPECT_CALL(*sink, HandleDocument).WillOnce(
      [](const rapidjson::Document& doc) {
        EXPECT_EQ(
            "[{"
              R"("nid":2,)"
              R"("tsid":3,)"
              R"("sid":1,)"
              R"("name":"service-1",)"
              R"("type":1,)"
              R"("logoId":-1)"
            "},{"
              R"("nid":2,)"
              R"("tsid":3,)"
              R"("sid":2,)"
              R"("name":"service-2",)"
              R"("type":1,)"
              R"("logoId":-1)"
            "}]",
            MockJsonlSink::Stringify(doc));
        return true;
      });

  scanner->Connect(std::move(sink));
  src.Connect(std::move(scanner));
  EXPECT_TRUE(src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
  EXPECT_EQ(EXIT_SUCCESS, src.GetExitCode());
}

TEST(ServiceScannerTest, ServiceTypes) {
  TableSource src;
  auto scanner = std::make_unique<ServiceScanner>(kEmptyOption);
  auto sink = std::make_unique<MockJsonlSink>();

  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x0003"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
        <service service_id="0x0002" program_map_PID="0x0102" />
        <service service_id="0x0003" program_map_PID="0x0103" />
      </PAT>
      <NIT version="1" current="true" actual="true" network_id="0x0001"
           test-pid="0x0010">
        <transport_stream transport_stream_id="0x1234"
                          original_network_id="0x0002" />
      </NIT>
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
          <service_descriptor service_type="0x02"
                              service_provider_name="test"
                              service_name="service-2" />
        </service>
        <service service_id="0x0003" EIT_schedule="false"
                 EIT_present_following="true" CA_mode="false"
                 running_status="undefined">
          <service_descriptor service_type="0x03"
                              service_provider_name="test"
                              service_name="service-3" />
        </service>
      </SDT>
    </tsduck>
  )");

  EXPECT_CALL(*sink, HandleDocument).WillOnce(
      [](const rapidjson::Document& doc) {
        EXPECT_EQ(
            "[{"
              R"("nid":2,)"
              R"("tsid":3,)"
              R"("sid":1,)"
              R"("name":"service-1",)"
              R"("type":1,)"
              R"("logoId":-1)"
            "},{"
              R"("nid":2,)"
              R"("tsid":3,)"
              R"("sid":2,)"
              R"("name":"service-2",)"
              R"("type":2,)"
              R"("logoId":-1)"
            "}]",
            MockJsonlSink::Stringify(doc));
        return true;
      });

  scanner->Connect(std::move(sink));
  src.Connect(std::move(scanner));
  EXPECT_TRUE(src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
  EXPECT_EQ(EXIT_SUCCESS, src.GetExitCode());
}

TEST(ServiceScannerTest, InclusionList) {
  ServiceScannerOption option;
  option.sids.Add(0x0001);

  TableSource src;
  auto scanner = std::make_unique<ServiceScanner>(option);
  auto sink = std::make_unique<MockJsonlSink>();

  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x0003"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
        <service service_id="0x0002" program_map_PID="0x0102" />
      </PAT>
      <NIT version="1" current="true" actual="true" network_id="0x0001"
           test-pid="0x0010">
        <transport_stream transport_stream_id="0x1234"
                          original_network_id="0x0002" />
      </NIT>
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
    </tsduck>
  )");

  EXPECT_CALL(*sink, HandleDocument).WillOnce(
      [](const rapidjson::Document& doc) {
        EXPECT_EQ(
            "[{"
              R"("nid":2,)"
              R"("tsid":3,)"
              R"("sid":1,)"
              R"("name":"service-1",)"
              R"("type":1,)"
              R"("logoId":-1)"
            "}]",
            MockJsonlSink::Stringify(doc));
        return true;
      });

  scanner->Connect(std::move(sink));
  src.Connect(std::move(scanner));
  EXPECT_TRUE(src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
  EXPECT_EQ(EXIT_SUCCESS, src.GetExitCode());
}

TEST(ServiceScannerTest, ExclusionList) {
  ServiceScannerOption option;
  option.xsids.Add(0x0001);

  TableSource src;
  auto scanner = std::make_unique<ServiceScanner>(option);
  auto sink = std::make_unique<MockJsonlSink>();

  src.LoadXml(R"(
    <?xml version="1.0" encoding="utf-8"?>
    <tsduck>
      <PAT version="1" current="true" transport_stream_id="0x0003"
           test-pid="0x0000">
        <service service_id="0x0001" program_map_PID="0x0101" />
        <service service_id="0x0002" program_map_PID="0x0102" />
      </PAT>
      <NIT version="1" current="true" actual="true" network_id="0x0001"
           test-pid="0x0010">
        <transport_stream transport_stream_id="0x1234"
                          original_network_id="0x0002" />
      </NIT>
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
    </tsduck>
  )");

  EXPECT_CALL(*sink, HandleDocument).WillOnce(
      [](const rapidjson::Document& doc) {
        EXPECT_EQ(
            "[{"
              R"("nid":2,)"
              R"("tsid":3,)"
              R"("sid":2,)"
              R"("name":"service-2",)"
              R"("type":1,)"
              R"("logoId":-1)"
            "}]",
            MockJsonlSink::Stringify(doc));
        return true;
      });

  scanner->Connect(std::move(sink));
  src.Connect(std::move(scanner));
  EXPECT_TRUE(src.FeedPackets());
  EXPECT_TRUE(src.IsEmpty());
  EXPECT_EQ(EXIT_SUCCESS, src.GetExitCode());
}
