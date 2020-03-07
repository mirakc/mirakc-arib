#include <cerrno>
#include <cstring>
#include <map>
#include <string>

#include <docopt/docopt.h>
#include <fmt/format.h>
#include <tsduck/tsduck.h>

#include "airtime_tracker.hh"
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

static const std::string kUsage = R"(
Tools to process ARIB TS streams.

Usage:
  mirakc-arib (-h | --help) [(scan-services | sync-clocks | collect-eits |
                              filter-service | filter-program |
                              track-airtime)]
  mirakc-arib --version
  mirakc-arib scan-services [--sids=<SID>...] [--xsids=<SID>...] [FILE]
  mirakc-arib sync-clocks [--sids=<SID>...] [--xsids=<SID>...] [FILE]
  mirakc-arib collect-eits [--sids=<SID>...] [--xsids=<SID>...]
                           [--time-limit=<MS>] [--streaming] [FILE]
  mirakc-arib filter-service --sid=<SID> [FILE]
  mirakc-arib filter-program --sid=<SID> --eid=<EID>
              --clock-pcr=<PCR> --clock-time=<UNIX-TIME-MS>
              [--start-margin=<MS>] [--end-margin=<MS>] [--pre-streaming] [FILE]
  mirakc-arib track-airtime --sid=<SID> --eid=<EID> [FILE]

Description:
  `mirakc-arib <sub-command> -h` shows help for each sub-command.

Logging:
  mirakc-arib doesn't output any log message by default.  The MIRAKC_ARIB_LOG
  environment variable is used for changing the logging level.

  The following command outputs info-level log messages to STDERR:

    $ recdvb 26 - - 2>/dev/null | \
        MIRAKC_ARIB_LOG=info mirakc-arib scan-services >/dev/null
    [2019-08-11 22:58:31.989] [scan-services] [info] Read packets from STDIN...
    [2019-08-11 22:58:31.990] [scan-services] [info] Feed packets...
    [2019-08-11 22:58:34.840] [scan-services] [info] PAT ready
    [2019-08-11 22:58:35.574] [scan-services] [info] SDT ready
    [2019-08-11 22:58:35.709] [scan-services] [info] NIT ready
    [2019-08-11 22:58:35.709] [scan-services] [info] Ready to collect services

  mirakc-arib uses spdlog for logging.  See the document of spdlog for details
  about log levels.
)";

static const std::string kScanServices = "scan-services";

static const std::string kScanServicesHelp = R"(
Scan services

Usage:
  mirakc-arib scan-services [--sids=<SID>...] [--xsids=<SID>...] [FILE]

Options:
  -h --help
    Print help.

  --sids=<SID>
    Service ID which must be included.

  --xsids=<SID>
    Service ID which must be excluded.

Arguments:
  FILE
    Path to a TS file.

Description:
  `scan-services` scans services in a TS stream.  Results will be output to
  STDOUT in the following JSON format:

    $ recdvb 27 - - 2>/dev/null | mirakc-arib scan-services | jq .[0]
    {{
      "nid": 32736,
      "tsid": 32736,
      "sid": 1024,
      "name": "ＮＨＫ総合１・東京",
      "type": 1,
      "logoId": 0,
      "remoteControlKeyId": 1
    }}

  `scan-services` collects services whose type is included in the following
  list:

    * 0x01 (Digital television service)
    * 0x02 (Digital audio service)
    * 0xA1 (Special video service)
    * 0xA2 (Special audio service)
    * 0xA5 (Promotion video service)
    * 0xA6 (Promotion audio service)

  Scanning logo data has not been supported at this moment.  So, values of the
  `logoId` and `hasLogoData` are always `-1` and `false` respectively.

)";

static const std::string kSyncClocks = "sync-clocks";

static const std::string kSyncClocksHelp = R"(
Synchrohize PCR and TOT/TDT

Usage:
  mirakc-arib sync-clocks [--sids=<SID>...] [--xsids=<SID>...] [FILE]

Options:
  -h --help
    Print help.

  --sids=<SID>
    Service ID which must be included.

  --xsids=<SID>
    Service ID which must be excluded.

Arguments:
  FILE
    Path to a TS file.

Description:
  `sync-clocks` synchronizes PCR for each service and TDT/TOT with accuracy
  within 1 second.

  `sync-clocks` outputs the result in the following JSON format:

    $ recdvb 27 - - 2>/dev/null | mirakc-arib sync-clocks | jq .[0]
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

  `sync-clocks` collects PCR for each service whose type is included in the
  following list:

    * 0x01 (Digital television service)
    * 0x02 (Digital audio service)
    * 0xA1 (Special video service)
    * 0xA2 (Special audio service)
    * 0xA5 (Promotion video service)
    * 0xA6 (Promotion audio service)
)";

static const std::string kCollectEits = "collect-eits";

static const std::string kCollectEitsHelp = R"(
Collect EIT sections

Usage:
  mirakc-arib collect-eits [--sids=<SID>...] [--xsids=<SID>...]
                           [--time-limit=<MS>] [--streaming] [FILE]

Options:
  -h --help
    Print help.

  --sids=<SID>
    Service ID which must be included.

  --xsids=<SID>
    Service ID which must be excluded.

  --time-limit=<MS> [default: 30000]
    Stop collecting if there is no progress for the specified time (ms).
    Elapsed time is computed using TDT/TOT.

    It makes no sence to specify a time limit less than 5 seconds.  Because TOT
    comes every 5 seconds in Japan.

  --streaming
    Streaming mode.

    In the streaming mode, the program never stops until killed.  The progress
    status will be updated in order to drop EIT sections which have already been
    collected.

Arguments:
  FILE
    Path to a TS file.

Description:
  `collect-eits` collects EIT sections from a TS stream.  Results will be output
  to STDOUT in the following JSONL format:

    $ recdvb 27 10 - 2>/dev/null | mirakc-arib collect-eits | head -1 | jq .
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
)";

static const std::string kFilterService = "filter-service";

static const std::string kFilterServiceHelp = R"(
Service filter

Usage:
  mirakc-arib filter-service --sid=<SID> [FILE]

Options:
  -h --help
    Print help.

  --sid=<SID>
    Service ID.

Arguments:
  FILE
    Path to a TS file.

Description:
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
    * PES private data that doesn't contain subtitles and superimposed texts
      (PID specified in PMT)
)";

static const std::string kFilterProgram = "filter-program";

static const std::string kFilterProgramHelp = R"(
Program filter

Usage:
  mirakc-arib filter-program --sid=<SID> --eid=<EID>
              --clock-pcr=<PCR> --clock-time=<UNIX-TIME-MS>
              [--start-margin=<MS>] [--end-margin=<MS>] [--pre-streaming] [FILE]

Options:
  -h --help
    Print help.

  --sid=<SID>
    Service ID.

  --eid=<EID>
    Event ID of a TV program.

  --clock-pcr=<PCR>
    27MHz, 42bits PCR value.

  --clock-time=<UNIX-TIME-MS>
    UNIX time (ms) correspoinding to the PCR value.

  --start-margin=<MS> [default: 0]
    Offset (ms) from the start time of the event toward the past.

  --end-margin=<MS> [default: 0]
    Offset (ms) from the end time of the event toward the future.

  --pre-streaming
    Output PSI/SI packets before start.

Arguments:
  FILE
    Path to a TS file.

Description:
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
)";

static const std::string kTrackAirtime = "track-airtime";

static const std::string kTrackAirtimeHelp = R"(
Track changes of an event

Usage:
  mirakc-arib track-airtime --sid=<SID> --eid=<EID> [FILE]

Options:
  -h --help
    Print help.

  --sid=<SID>
    Service ID.

  --eid=<EID>
    Event ID of a TV program.

Arguments:
  FILE
    Path to a TS file.

Description:
  `track-airtime` tracks changes of a specified event.

  `track-airtime` outputs event information when changes are detected.  Results
  will be output to STDOUT in the following JSONL format:

    $ recdvb 27 10 - 2>/dev/null | \
        mirakc-arib track-airtime --sid=102 | head -1 | jq .
    {{
      "nid": 32736,
      "tsid": 32736,
      "sid": 1024,
      "eid": 31887,
      "startTime": 1581596400000,
      "duration": 1500000
    }}
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

void Init(const Args& args) {
  if (args.at(kScanServices).asBool()) {
    InitLogger(kScanServices);
  } else if (args.at(kSyncClocks).asBool()) {
    InitLogger(kSyncClocks);
  } else if (args.at(kCollectEits).asBool()) {
    InitLogger(kCollectEits);
  } else if (args.at(kFilterService).asBool()) {
    InitLogger(kFilterService);
  } else if (args.at(kFilterProgram).asBool()) {
    InitLogger(kFilterProgram);
  } else if (args.at(kTrackAirtime).asBool()) {
    InitLogger(kTrackAirtime);
  }

  ts::DVBCharset::EnableARIBMode();
}

std::unique_ptr<PacketSource> MakePacketSource(const Args& args) {
  static const std::string kFILE = "FILE";

  std::string path = args.at(kFILE).isString() ? args.at(kFILE).asString() : "";
  std::unique_ptr<File> file = std::make_unique<PosixFile>(path);
  return std::make_unique<FileSource>(std::move(file));
}

ts::Time ConvertUnixTimeToJstTime(ts::MilliSecond unix_time_ms) {
  return ts::Time::UnixEpoch + unix_time_ms + kJstTzOffset;
}

void LoadSidSet(const Args& args, const std::string& name, SidSet* sids) {
  if (args.at(name)) {
    auto list = args.at(name).asStringList();
    sids->Add(list);
    MIRAKC_ARIB_INFO("{} SIDs: {}", name, fmt::join(list, ", "));
  }
}

void LoadOption(const Args& args, ServiceFilterOption* opt) {
  static const std::string kSid = "--sid";

  if (args.at(kSid)) {
    opt->sid = static_cast<uint16_t>(args.at(kSid).asLong());
    if (opt->sid != 0) {
      MIRAKC_ARIB_INFO("Service Filter: SID#{:04X}", opt->sid);
    }
  }
}

void LoadOption(const Args& args, EitCollectorOption* opt) {
  static const std::string kTimeLimit = "--time-limit";
  static const std::string kStreaming = "--streaming";

  LoadSidSet(args, "--sids", &opt->sids);
  LoadSidSet(args, "--xsids", &opt->xsids);
  if (args.at(kTimeLimit)) {
    opt->time_limit =
        static_cast<ts::MilliSecond>(args.at(kTimeLimit).asLong());
  }
  opt->streaming = args.at(kStreaming).asBool();
  MIRAKC_ARIB_INFO("Time-Limit({}), Streaming({})",
                   opt->time_limit, opt->streaming);
}

void LoadOption(const Args& args, AirtimeTrackerOption* opt) {
  static const std::string kSid = "--sid";
  static const std::string kEid = "--eid";

  opt->sid = static_cast<uint16_t>(args.at(kSid).asLong());
  opt->eid = static_cast<uint16_t>(args.at(kEid).asLong());
  MIRAKC_ARIB_INFO("Event Tracker: SID#{:04X} EID#{:04X}", opt->sid, opt->eid);
}

void LoadOption(const Args& args, ProgramFilterOption* opt) {
  static const std::string kSid = "--sid";
  static const std::string kEid = "--eid";
  static const std::string kClockPcr = "--clock-pcr";
  static const std::string kClockTime = "--clock-time";
  static const std::string kStartMargin = "--start-margin";
  static const std::string kEndMargin = "--end-margin";
  static const std::string kPreStreaming = "--pre-streaming";

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
  opt->pre_streaming = args.at(kPreStreaming).asBool();
  MIRAKC_ARIB_INFO(
      "Program Filter: SID#{:04X} EID#{:04X} Clock({:011X}, {}) Margin({}, {})"
      " Pre-Streaming({})",
      opt->sid, opt->eid, opt->clock_pcr, opt->clock_time, opt->start_margin,
      opt->end_margin, opt->pre_streaming);
}

std::unique_ptr<PacketSink> MakePacketSink(const Args& args) {
  if (args.at(kScanServices).asBool()) {
    ServiceScannerOption option;
    LoadSidSet(args, "--sids", &option.sids);
    LoadSidSet(args, "--xsids", &option.xsids);
    auto scanner = std::make_unique<ServiceScanner>(option);
    scanner->Connect(std::move(std::make_unique<StdoutJsonlSink>()));
    return scanner;
  }
  if (args.at(kSyncClocks).asBool()) {
    PcrSynchronizerOption option;
    LoadSidSet(args, "--sids", &option.sids);
    LoadSidSet(args, "--xsids", &option.xsids);
    auto sync = std::make_unique<PcrSynchronizer>(option);
    sync->Connect(std::move(std::make_unique<StdoutJsonlSink>()));
    return sync;
  }
  if (args.at(kCollectEits).asBool()) {
    EitCollectorOption option;
    LoadOption(args, &option);
    auto collector = std::make_unique<EitCollector>(option);
    collector->Connect(std::move(std::make_unique<StdoutJsonlSink>()));
    return collector;
  }
  if (args.at(kFilterService).asBool()) {
    ServiceFilterOption service_option;
    LoadOption(args, &service_option);
    auto filter = std::make_unique<ServiceFilter>(service_option);
    filter->Connect(std::make_unique<StdoutSink>());
    return filter;
  }
  if (args.at(kFilterProgram).asBool()) {
    ServiceFilterOption service_option;
    LoadOption(args, &service_option);
    auto filter = std::make_unique<ServiceFilter>(service_option);
    ProgramFilterOption program_option;
    LoadOption(args, &program_option);
    auto program_filter = std::make_unique<ProgramFilter>(program_option);
    program_filter->Connect(std::make_unique<StdoutSink>());
    filter->Connect(std::move(program_filter));
    return filter;
  }
  if (args.at(kTrackAirtime).asBool()) {
    AirtimeTrackerOption option;
    LoadOption(args, &option);
    auto tracker = std::make_unique<AirtimeTracker>(option);
    tracker->Connect(std::move(std::make_unique<StdoutJsonlSink>()));
    return tracker;
  }
  return std::unique_ptr<PacketSink>();
}

void ShowHelp(const Args& args) {
  if (args.at(kScanServices).asBool()) {
    fmt::print(kScanServicesHelp);
  } else if (args.at(kSyncClocks).asBool()) {
    fmt::print(kSyncClocksHelp);
  } else if (args.at(kCollectEits).asBool()) {
    fmt::print(kCollectEitsHelp);
  } else if (args.at(kFilterService).asBool()) {
    fmt::print(kFilterServiceHelp);
  } else if (args.at(kFilterProgram).asBool()) {
    fmt::print(kFilterProgramHelp);
  } else if (args.at(kTrackAirtime).asBool()) {
    fmt::print(kTrackAirtimeHelp);
  } else {
    fmt::print(kUsage);
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  auto usage = fmt::format(kUsage);

  auto args =
      docopt::docopt(usage, { argv + 1, argv + argc }, false, kVersion);

  if (args.at("-h").asBool() || args.at("--help").asBool()) {
    ShowHelp(args);
    return EXIT_SUCCESS;
  }

  Init(args);

  auto src = MakePacketSource(args);
  src->Connect(MakePacketSink(args));
  auto success = src->FeedPackets();

  return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
