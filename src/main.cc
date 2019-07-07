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

#include <cerrno>
#include <cstring>
#include <map>
#include <string>

#include <docopt/docopt.h>
#include <fmt/format.h>
#include <tsduck/tsduck.h>

#include "base.hh"
#include "eit_collector.hh"
#include "file.hh"
#include "jsonl_sink.hh"
#include "logging.hh"
#include "packet_filter.hh"
#include "packet_sink.hh"
#include "packet_source.hh"
#include "service_scanner.hh"

namespace {

static const std::string kVersion = "0.1.0";

static const char kUsageTemplate[] = R"(
Tools to process ARIB TS streams.

Usage:
  {0} -h | --help
  {0} --version
  {0} scan-services [--xsid=<SID>...] [FILE]
  {0} collect-eits [--xsid=<SID>...] [FILE]
  {0} filter-packets --sid=<SID> [--eid=<EID>] [--until=<UNIX-TIME>] [FILE]

Options:
  -h --help            Print help.
  --version            Print version.
  --xsid=<SID>         Excluded service ID.
  --sid=<SID>          Service ID.
  --eid=<EID>          Event ID.
  --until=<UNIX-TIME>  Time to stop streaming.

Arguments:
  FILE                 Path to a TS file.

Commands:
  `scan-services` scans services in a TS stream.  Results will be output to
  STDOUT in the following JSON format:

    $ recdvb 27 - - 2>/dev/null | {0} scan-services 2>/dev/null | jq .[0]
    {{
      "nid": 32736,
      "tsid": 32736,
      "sid": 1024,
      "name": "ＮＨＫ総合１・東京",
      "type": 1,
      "logoId": 0,
      "remoteControlKeyId": 1
    }}

  Scanning logo data has not been supported at this moment.  So, values of the
  `logoId` and `hasLogoData` are always `-1` and `false` respectively.

  `collect-eits` collects EIT sections from a TS stream.  Results will be output
  to STDOUT in the following JSONL format:

    $ recdvb 27 10 - 2>/dev/null | {0} collect-eits 2>/dev/null | head -1 | jq .
    {{
      "originalNetworkId": 32736,
      "transportStreamId": 32736,
      "serviceId": 1024,
      "tableId": 80,
      "sectionNumber": 144,
      "lastSectionNumber": 248,
      "segmentLastSectionNumber": 144,
      "versionNumber": 6,
      "events": [
        {{
          "eventId": 12250,
          "startTime": 1570917180000,
          "duration": 420000,
          "scrambled": false,
          "descriptors": [
            {{
              "$type": "ShortEvent",
              "eventName": "気象情報・ニュース",
              "text": ""
            }},
            {{
              "$type": "Component",
              "streamContent": 1,
              "componentType": 179
            }},
            {{
              "$type": "AudioComponent",
              "componentType": 1,
              "samplingRate": 7
            }},
            {{
              "$type": "Content",
              "nibbles": [
                [
                  0,
                  1,
                  15,
                  15
                ]
              ]
            }}
          ]
        }},
        ...
      ]
    }}
    {{
      "originalNetworkId": 32736,
      "transportStreamId": 32736,
      "serviceId": 1024,
      "tableId": 89,
      "sectionNumber": 224,
      "lastSectionNumber": 248,
      "segmentLastSectionNumber": 224,
      "versionNumber": 9,
      "events": [
        {{
          "eventId": 15336,
          "startTime": 1571367600000,
          "duration": 1200000,
          "scrambled": false,
          "descriptors": [
            {{
              "$type": "ExtendedEvent",
              "items": [
                [
                  "出演者",
                  "【キャスター】三條雅幸"
                ]
              ]
            }}
          ]
        }},
        ...
      ]
    }}

  `filter-packets` drops packets in a TS stream, which are not related to the
  specified service ID (SID) and event ID (EID).

  Packets other than listed below are dropped:

    * PAT (PID=0x0000)
    * CAT (PID=0x0001)
    * TOT/TDT (PID=0x0014)
    * PMT (PID specified in PAT)
    * EMM (PID specified in CAT)
    * PCR (PID specified in PMT)
    * ECM (PID specified in PMT)
    * PES for video/audio/subtitles (PID specified in PMT)

  `filter-packets` modifies PAT so that its service map contains only the
  specified SID.

  `filter-packets` modifies PMT so that its stream map contains only PES's PIDs
  which are needed for playback.

  Unlike Mirakurun, packets listed below are always dropped:

    * NIT (PID=0x0010)
    * SDT (PID=0x0011)
    * EIT (PID=0x0012)
    * RST (PID=0x0013)
    * SDTT (PID=0x0023,0x0028)
    * BIT (PID=0x0024)
    * CDT (PID=0x0029)
    * DCM-CC for BML (PID specified in PMT)
    * PES private data (PID specified in PMT)

  Same as Mirakurun, PES packets are dropped until EID matches the event ID of
  the first event in EIT (TID=0x4E) when EID is specified.

  The streaming is stopped when any of the following conditions are met:

    * The input TS stream reaches EOF.
    * The event ID of the first event in EIT (TID=0x4E) is changed from EID to
      another (only when EID is specified).
    * The time in TOT/TDT is over a time limit which is specified in the
      `--until` option.

  TOT seems to be sent every 5 seconds in Japan.  Therefore, the maximum delay
  between the time limit and an actual stop time is about 5 seconds.

Logging:
  {0} doesn't output any log message by default.  The MIRAKC_ARIB_LOG
  environment variable is used for changing the logging level.

  The following command outputs info-level log messages to STDERR:

    $ recdvb 26 - - 2>/dev/null | \
        MIRAKC_ARIB_LOG=info {0} scan-services >/dev/null
    [2019-08-11 22:58:31.989] [scan-services] [info] Read packets from STDIN...
    [2019-08-11 22:58:31.990] [scan-services] [info] Feed packets...
    [2019-08-11 22:58:34.840] [scan-services] [info] PAT ready
    [2019-08-11 22:58:35.574] [scan-services] [info] SDT ready
    [2019-08-11 22:58:35.709] [scan-services] [info] NIT ready
    [2019-08-11 22:58:35.709] [scan-services] [info] Ready to collect services

  {0} uses spdlog for logging.  See the document of spdlog for details about
  log levels.
)";

class PosixFile final : public File {
 public:
  PosixFile(const std::string& path) {
    if  (path.empty()) {
      fd_ = 0;
      MIRAKC_ARIB_INFO("Read packets from STDIN...");
    } else {
      fd_ = open(path.c_str(), O_RDONLY);
      if (fd_ > 0) {
        need_close_ = true;
        MIRAKC_ARIB_INFO("Read packets from {}...", path);
      } else {
        MIRAKC_ARIB_ERROR(
            "Failed to open {}: {} ({})", path, std::strerror(errno), errno);
      }
    }
  }

  ~PosixFile() override {
    if (need_close_) {
      close(fd_);
    }
  }

  ssize_t Read(uint8_t* buf, size_t len) override {
    if (fd_ < 0) {
      return 0;
    }
    return read(fd_, reinterpret_cast<void*>(buf), len);
  }

 private:
  int fd_ = -1;
  bool need_close_ = false;
};

void init(const Args& args) {
  if (args.at("scan-services").asBool()) {
    InitLogger("scan-services");
  } else if (args.at("collect-eits").asBool()) {
    InitLogger("collect-eits");
  } else if (args.at("filter-packets").asBool()) {
    InitLogger("filter-packets");
  }

  ts::DVBCharset::EnableARIBMode();
}

std::unique_ptr<PacketSource> make_source(const Args& args) {
  static const std::string kFILE = "FILE";

  std::string path = args.at(kFILE).isString() ? args.at(kFILE).asString() : "";
  std::unique_ptr<File> file = std::make_unique<PosixFile>(path);
  return std::make_unique<FileSource>(std::move(file));
}

void set_option(const Args& args, ExcludedSidSet* xsids) {
  static const std::string kXsid = "--xsid";

  if (args.at(kXsid)) {
    auto sids = args.at(kXsid).asStringList();
    xsids->Add(sids);
    MIRAKC_ARIB_INFO("Excluded SIDs: {}", fmt::join(sids, ", "));
  }
}

void set_options(const Args& args, PacketFilterOption* opt) {
  static const std::string kSid = "--sid";
  static const std::string kEid = "--eid";
  static const std::string kUntil = "--until";

  if (args.at(kSid)) {
    opt->sid = static_cast<uint16_t>(args.at(kSid).asLong());
    if (opt->sid != 0) {
      MIRAKC_ARIB_INFO("Filter packets by sid#{}", opt->sid);
    }
  }

  if (args.at(kEid)) {
    opt->eid = static_cast<uint16_t>(args.at(kEid).asLong());
    if (opt->sid != 0) {
      MIRAKC_ARIB_INFO("Filter packets by eid#{}", opt->eid);
    }
  }

  if (args.at(kUntil)) {
    opt->has_time_limit = true;
    opt->time_limit = ts::Time::UnixTimeToUTC(
        static_cast<uint64_t>(args.at(kUntil).asLong()));
    opt->time_limit += kJstTzOffset;  // UTC -> JST
    MIRAKC_ARIB_INFO(
        "Filter packets until {} in JST", opt->time_limit.format().toUTF8());
  }
}

std::unique_ptr<PacketSink> make_sink(const Args& args) {
  if (args.at("scan-services").asBool()) {
    ServiceScannerOption option;
    set_option(args, &option.xsids);
    auto scanner = std::make_unique<ServiceScanner>(option);
    scanner->Connect(std::move(std::make_unique<StdoutJsonlSink>()));
    return scanner;
  }
  if (args.at("collect-eits").asBool()) {
    EitCollectorOption option;
    set_option(args, &option.xsids);
    auto collector = std::make_unique<EitCollector>(option);
    collector->Connect(std::move(std::make_unique<StdoutJsonlSink>()));
    return collector;
  }
  if (args.at("filter-packets").asBool()) {
    PacketFilterOption option;
    set_options(args, &option);
    auto filter = std::make_unique<PacketFilter>(option);
    filter->Connect(std::make_unique<StdoutSink>());
    return filter;
  }
  return std::unique_ptr<PacketSink>();
}

}  // namespace

int main(int argc, char* argv[]) {
  auto usage = fmt::format(kUsageTemplate, argv[0]);

  auto args =
      docopt::docopt(trim(usage), { argv + 1, argv + argc }, true, kVersion);

  init(args);

  auto src = make_source(args);
  src->Connect(make_sink(args));
  auto success = src->FeedPackets();

  return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
