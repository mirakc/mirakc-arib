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
#include "packet_sink.hh"
#include "packet_source.hh"
#include "pcr_synchronizer.hh"
#include "program_filter.hh"
#include "service_filter.hh"
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
  {0} filter-service --sid=<SID> [FILE]
  {0} filter-program --sid=<SID> --eid=<EID>
        --clock-pcr=<PCR> --clock-time=<UNIX-TIME-MS>
        [--start-margin=<MS>] [--end-margin=<MS>] [FILE]
  {0} sync-clock [FILE]

Options:
  -h --help                    Print help.
  --version                    Print version.
  --xsid=<SID>                 Excluded service ID.
  --sid=<SID>                  Service ID.
  --eid=<EID>                  Event ID of a TV program.
  --clock-pcr=<PCR>            27MHz, 42bits PCR value.
  --clock-time=<UNIX-TIME-MS>  UNIX time (ms) correspoinding to the PCR value.
  --start-margin=<MS>          Offset (ms) from the start time of the event
                               toward the past.
  --end-margin=<MS>            Offset (ms) from the end time of the event
                               toward the future.

Arguments:
  FILE                         Path to a TS file.

scan-services:
  `scan-services` scans services in a TS stream.  Results will be output to
  STDOUT in the following JSON format:

    $ recdvb 27 - - 2>/dev/null | {0} scan-services | jq .[0]
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

collect-eits:
  `collect-eits` collects EIT sections from a TS stream.  Results will be output
  to STDOUT in the following JSONL format:

    $ recdvb 27 10 - 2>/dev/null | {0} collect-eits | head -1 | jq .
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

filter-service:
  `filter-service` drops packets in a TS stream, which are not related to the
  specified service ID (SID).

  Packets other than listed below are dropped:

    * PAT (PID=0x0000)
    * CAT (PID=0x0001)
    * NIT (PID=0x0010)
    * SDT (PID=0x0011)
    * EIT (PID=0x0012)
    * RST (PID=0x0013)
    * TDT/TOT (PID=0x0014)
    * BIT (PID=0x0024)
    * CDT (PID=0x0029)
    * PMT (PID specified in PAT)
    * EMM (PID specified in CAT)
    * PCR (PID specified in PMT)
    * ECM (PID specified in PMT)
    * PES for video/audio/subtitles (PID specified in PMT)

  `filter-service` modifies PAT so that its service map contains only the
  specified SID.

  `filter-service` modifies PMT so that its stream map contains only PES's PIDs
  which are needed for playback.

  Unlike Mirakurun, packets listed below are always dropped:

    * SDTT (PID=0x0023,0x0028)
    * DCM-CC for BML (PID specified in PMT)
    * PES private data (PID specified in PMT)

filter-program:
  `filter-program` performs the same processing as `filter-service`, but also
  outputs packets only while a specified TV program is being broadcast.

  Unlike Mirakurun, `filter-program` calculates the start and end times of
  streaming based on PCR.  The `--start-margin` and `--end-margin` adjust these
  times like below:

          start-margin                         end-margin
    ----|<============|-----------------------|==========>|----
        |             |                       |           |
      start-time    start-time         end-time           end-time
      of streaming  of the TV program  of the TV program  of streaming

sync-clock:
  `sync-clock` synchronizes PCR for each service and TDT/TOT with accuracy
  within 1 second.

  `sync-clock` outputs the result in the following JSON format:

    $ recdvb 27 - - 2>/dev/null | {0} sync-clock | jq .[0]
    {{
      "nid": 32736,
      "tsid": 32736,
      "sid": 1024,
      "clock": {{
        "pcr": 744077003262,
        "time": 1576398518000
      }}
    }}

  where:

    clock.pcr
      27MHz, 42 bits PCR value correspoinding to `clock.time`

    clock.time
      TDT/TOT time in the 64 bits UNIX time format in milliseconds

  `sync-clock` collects PCR for each service whose type is included in the
  following list:

    * 0x01 (Digital television service)
    * 0x02 (Digital audio service)
    * 0xA1 (Special video service)
    * 0xA2 (Special audio service)
    * 0xA5 (Promotion video service)
    * 0xA6 (Promotion audio service)

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
  } else if (args.at("filter-service").asBool()) {
    InitLogger("filter-service");
  } else if (args.at("filter-program").asBool()) {
    InitLogger("filter-program");
  } else if (args.at("sync-clock").asBool()) {
    InitLogger("sync-clock");
  }

  ts::DVBCharset::EnableARIBMode();
}

std::unique_ptr<PacketSource> make_source(const Args& args) {
  static const std::string kFILE = "FILE";

  std::string path = args.at(kFILE).isString() ? args.at(kFILE).asString() : "";
  std::unique_ptr<File> file = std::make_unique<PosixFile>(path);
  return std::make_unique<FileSource>(std::move(file));
}

void set_option(const Args& args, const std::string& name, SidSet* sids) {
  if (args.at(name)) {
    auto list = args.at(name).asStringList();
    sids->Add(list);
    MIRAKC_ARIB_INFO("{} SIDs: {}", name, fmt::join(list, ", "));
  }
}

ts::Time ConvertUnixTimeToJstTime(ts::MilliSecond unix_time_ms) {
  return ts::Time::UnixEpoch + unix_time_ms + kJstTzOffset;
}

void set_options(const Args& args, ServiceFilterOption* opt) {
  static const std::string kSid = "--sid";

  if (args.at(kSid)) {
    opt->sid = static_cast<uint16_t>(args.at(kSid).asLong());
    if (opt->sid != 0) {
      MIRAKC_ARIB_INFO("Service Filter: SID#{:04X}", opt->sid);
    }
  }
}

void load_option(const Args& args, ProgramFilterOption* opt) {
  static const std::string kSid = "--sid";
  static const std::string kEid = "--eid";
  static const std::string kClockPcr = "--clock-pcr";
  static const std::string kClockTime = "--clock-time";
  static const std::string kStartMargin = "--start-margin";
  static const std::string kEndMargin = "--end-margin";

  opt->sid = static_cast<uint16_t>(args.at(kSid).asLong());
  opt->eid = static_cast<uint16_t>(args.at(kEid).asLong());
  opt->clock_pcr = static_cast<uint64_t>(args.at(kClockPcr).asLong());
  opt->clock_time = ConvertUnixTimeToJstTime(
      static_cast<ts::MilliSecond>(args.at(kClockTime).asLong()));
  if (args.at(kStartMargin)) {
    opt->start_margin =
        static_cast<ts::MilliSecond>(args.at(kStartMargin).asLong());
  }
  if (args.at(kEndMargin)) {
    opt->end_margin =
        static_cast<ts::MilliSecond>(args.at(kEndMargin).asLong());
  }
  MIRAKC_ARIB_INFO(
      "Program Filter: SID#{:04X} EID#{:04X} Clock({:011X}, {}) Margin({}, {})",
      opt->sid, opt->eid, opt->clock_pcr, opt->clock_time,
      opt->start_margin, opt->end_margin);
}

std::unique_ptr<PacketSink> make_sink(const Args& args) {
  if (args.at("scan-services").asBool()) {
    ServiceScannerOption option;
    set_option(args, "--xsid", &option.xsids);
    auto scanner = std::make_unique<ServiceScanner>(option);
    scanner->Connect(std::move(std::make_unique<StdoutJsonlSink>()));
    return scanner;
  }
  if (args.at("collect-eits").asBool()) {
    EitCollectorOption option;
    set_option(args, "--xsid", &option.xsids);
    auto collector = std::make_unique<EitCollector>(option);
    collector->Connect(std::move(std::make_unique<StdoutJsonlSink>()));
    return collector;
  }
  if (args.at("filter-service").asBool()) {
    ServiceFilterOption service_option;
    set_options(args, &service_option);
    auto filter = std::make_unique<ServiceFilter>(service_option);
    filter->Connect(std::make_unique<StdoutSink>());
    return filter;
  }
  if (args.at("filter-program").asBool()) {
    ServiceFilterOption service_option;
    set_options(args, &service_option);
    auto filter = std::make_unique<ServiceFilter>(service_option);
    ProgramFilterOption program_option;
    load_option(args, &program_option);
    auto program_filter = std::make_unique<ProgramFilter>(program_option);
    program_filter->Connect(std::make_unique<StdoutSink>());
    filter->Connect(std::move(program_filter));
    return filter;
  }
  if (args.at("sync-clock").asBool()) {
    auto sync = std::make_unique<PcrSynchronizer>();
    sync->Connect(std::move(std::make_unique<StdoutJsonlSink>()));
    return sync;
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
