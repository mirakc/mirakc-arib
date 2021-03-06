#define _FILE_OFFSET_BITS 64

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <map>
#include <memory>
#include <string>

#include <unistd.h>
#include <sys/types.h>

#include <docopt/docopt.h>
#include <fmt/format.h>
#include <spdlog/cfg/env.h>
#include <tsduck/tsduck.h>

#include "airtime_tracker.hh"
#include "base.hh"
#include "eit_collector.hh"
#include "file.hh"
#include "jsonl_sink.hh"
#include "logging.hh"
#include "logo_collector.hh"
#include "packet_sink.hh"
#include "packet_source.hh"
#include "pcr_synchronizer.hh"
#include "program_filter.hh"
#include "ring_file_sink.hh"
#include "service_filter.hh"
#include "service_recorder.hh"
#include "service_scanner.hh"
#include "start_seeker.hh"
#include "pes_printer.hh"

namespace {

static const std::string kVersion = R"(mirakc-arib {}
* mirakc/docopt.cpp {}
* fmtlib/fmt {}
* gabime/spdlog {} w/ patches/spdlog.patch
* Tencent/rapidjson {}
* tplgy/cppcodec {}
* mirakc/aribb24 {}
* mirakc/tsduck-arib {}
* DBCTRADO/LibISDB {})";

static const std::string kUsage = R"(
Tools to process ARIB TS streams.

Usage:
  mirakc-arib (-h | --help)
    [(scan-services | sync-clocks | collect-eits | collect-logos |
      filter-service | filter-program | record-service | track-airtime |
      seek-start | print-pes)]
  mirakc-arib --version
  mirakc-arib scan-services [--sids=<sid>...] [--xsids=<sid>...] [<file>]
  mirakc-arib sync-clocks [--sids=<sid>...] [--xsids=<sid>...] [<file>]
  mirakc-arib collect-eits [--sids=<sid>...] [--xsids=<sid>...]
                           [--time-limit=<ms>] [--streaming]
                           [--use-unicode-symbol] [<file>]
  mirakc-arib collect-logos [<file>]
  mirakc-arib filter-service --sid=<sid> [<file>]
  mirakc-arib filter-program --sid=<sid> --eid=<eid>
    --clock-pid=<pid> --clock-pcr=<pcr> --clock-time=<unix-time-ms>
    [--audio-tags=<tag>...] [--video-tags=<tag>...]
    [--start-margin=<ms>] [--end-margin=<ms>] [--pre-streaming] [<file>]
  mirakc-arib record-service --sid=<sid> --file=<file>
    --chunk-size=<bytes> --num-chunks=<num> [--start-pos=<pos>] [<file>]
  mirakc-arib track-airtime --sid=<sid> --eid=<eid> [<file>]
  mirakc-arib seek-start --sid=<sid>
    [--max-duration=<ms>] [--max-packets=<num>] [<file>]
  mirakc-arib print-pes [<file>]

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
  mirakc-arib scan-services [--sids=<sid>...] [--xsids=<sid>...] [<file>]

Options:
  -h --help
    Print help.

  --sids=<sid>
    Service ID which must be included.

  --xsids=<sid>
    Service ID which must be excluded.

Arguments:
  <file>
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
  mirakc-arib sync-clocks [--sids=<sid>...] [--xsids=<sid>...] [<file>]

Options:
  -h --help
    Print help.

  --sids=<sid>
    Service ID which must be included.

  --xsids=<sid>
    Service ID which must be excluded.

Arguments:
  <file>
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
        "pid": 511,
        "pcr": 744077003262,
        "time": 1576398518000
      }}
    }}

  where:

    clock.pid
      PID of the PCR packet for the service

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
  mirakc-arib collect-eits [--sids=<sid>...] [--xsids=<sid>...]
                           [--time-limit=<ms>] [--streaming]
                           [--use-unicode-symbol] [<file>]

Options:
  -h --help
    Print help.

  --sids=<sid>
    Service ID which must be included.

  --xsids=<sid>
    Service ID which must be excluded.

  --time-limit=<ms>  [default: 30000]
    Stop collecting if there is no progress for the specified time (ms).
    Elapsed time is computed using TDT/TOT.

    It makes no sence to specify a time limit less than 5 seconds.  Because TOT
    comes every 5 seconds in Japan.

  --streaming
    Streaming mode.

    In the streaming mode, the program never stops until killed.  The progress
    status will be updated in order to drop EIT sections which have already been
    collected.

Obsoleted Options:
  --use-unicode-symbol
    Use the `MIRAKC_ARIB_KEEP_UNICODE_SYMBOLS` environment variable instead of
    this option.

Arguments:
  <file>
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

Environment Variables:
  MIRAKC_ARIB_KEEP_UNICODE_SYMBOLS
    Set `1` if you like to keep Unicode symbols like enclosed ideographic
    supplement characters.

    This option is added just for backword-compatibility.  It's not recommended
    to use this option in normal use cases.  Because some functions of
    EPGStation like the de-duplication of recorded programs won't work properly
    if this option is specified.
)";

static const std::string kCollectLogos = "collect-logos";

static const std::string kCollectLogosHelp = R"(
Collect logos

Usage:
  mirakc-arib collect-logos [<file>]

Options:
  -h --help
    Print help.

Arguments:
  <file>
    Path to a TS file.

Description:
  `collect-logos` collects logos from a TS stream.  Results will be output
  to STDOUT in the following JSONL format:

    $ recdvb 27 - - 2>/dev/null | mirakc-arib collect-logos | head -1 | jq .
    {{
      "nid": 32736,
      "ddid": 1024,
      "logo": {{
        "type": 0,
        "id": 0,
        "version": 0,
        "data": "base64-encoded-png"
      }}
    }}

  Currently, `collect-logos` never stops even after all logos have been
  collected.

  Transmission frequency of CDT section and the number of logos are different
  for each broadcaster:

    CHANNEL  ENOUGH TIME TO COLLECT ALL LOGOS  #LOGOS
    -------  --------------------------------  ------
    MX       10 minutes                        12
    CX       10 minutes                         6
    TBS       5 minutes                         6
    TX       10 minutes                         6
    EX       10 minutes                        18
    NTV      10 minutes                         6
    ETV      10 minutes                         6
    NHK      10 minutes                         6

  You can collect logos from a TS files recorded using `filter-service` or
  `filter-program` if it contains CDT sections.
)";

static const std::string kFilterService = "filter-service";

static const std::string kFilterServiceHelp = R"(
Service filter

Usage:
  mirakc-arib filter-service --sid=<sid> [<file>]

Options:
  -h --help
    Print help.

  --sid=<sid>
    Service ID.

Arguments:
  <file>
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
    * PES (PID specified in PMT)

  `filter-service` modifies PAT so that its service map contains only the
  specified SID.

  Unlike Mirakurun, packets listed below are always dropped:

    * SDTT (PID=0x0023,0x0028)
)";

static const std::string kFilterProgram = "filter-program";

static const std::string kFilterProgramHelp = R"(
Program filter

Usage:
  mirakc-arib filter-program --sid=<sid> --eid=<eid>
    --clock-pid=<pid> --clock-pcr=<pcr> --clock-time=<unix-time-ms>
    [--audio-tags=<tag>...] [--video-tags=<tag>...]
    [--start-margin=<ms>] [--end-margin=<ms>] [--pre-streaming] [<file>]

Options:
  -h --help
    Print help.

  --sid=<sid>
    Service ID.

  --eid=<eid>
    Event ID of a TV program.

  --clock-pid=<pid>
    PID of PCR for the service.

  --clock-pcr=<pcr>
    27MHz, 42bits PCR value.

  --clock-time=<unix-time-ms>
    UNIX time (ms) correspoinding to the PCR value.

  --audio-tags=<tag>
    Only audio streams matching with specified tags will be included.  All audio
    streams will be included if this option is not specified.

    TAG is a 1-byte unsgined integer value which is specified in the
    component_tag field in the Audio Component Description.

  --video-tags=<tag>
    Only video streams matching with specified tags will be included.  All video
    streams will be included if this option is not specified.

    TAG is a 1-byte unsgined integer value which is specified in the
    component_tag field in the Component Description.

  --start-margin=<ms>  [default: 0]
    Offset (ms) from the start time of the event toward the past.

  --end-margin=<ms>  [default: 0]
    Offset (ms) from the end time of the event toward the future.

  --pre-streaming
    Output PAT packets before start.

Arguments:
  <file>
    Path to a TS file.

Description:
  `filter-program` outputs packets only while a specified TV program is being
  broadcasted.

  Unlike Mirakurun, `filter-program` determines the start and end times of the
  TV program by using PCR values synchronized with TDT/TOT.  The
  `--start-margin` and `--end-margin` adjust these times like below:

          start-margin                         end-margin
    ----|<============|-----------------------|==========>|----
        |             |                       |           |
      start-time    start-time         end-time           end-time
      of streaming  of the TV program  of the TV program  of streaming

  When the PCR for the service is changed while filtering packets,
  `filter-program` resynchronize the clock automatically.  In this case, actual
  start and end times may be delayed about 5 seconds due to the clock
  synchronization.
)";

static const std::string kRecordService = "record-service";

static const std::string kRecordServiceHelp = R"(
Record a service stream into a ring buffer file

Usage:
  mirakc-arib record-service --sid=<sid> --file=<file>
    --chunk-size=<bytes> --num-chunks=<num> [--start-pos=<pos>] [<file>]

Options:
  -h --help
    Print help.

  --sid=<sid>
    Service ID.

  --file=<file>
    Path to the ring buffer file.

  --chunk-size=<bytes>
    Chunk size of the ring buffer file.
    The chunk size must be a multiple of 8192.

  --num-chunks=<num>
    The number of chunks in the ring buffer file.

  --start-pos=<pos>  [default: 0]
    A file position to start recoring.
    The value must be a multiple of the chunk size.

Arguments:
  <file>
    Path to a TS file.

Description:
  `record-service` records a service stream using a ring buffer file.

JSON Messages:
  start
    The `start` message is sent when `record-service` starts.  The message
    structure is like below:

      {{
        "type": "start"
      }}

  end
    The `end` message is sent when `record-service` ends.  The message structure
    is like below:

      {{
        "type": "end",
        "data": {{
          "reset": false,
        }}
      }}

    where:
      reset
        Application using `record-service` needs to reset data regarding this
        record before restarting new recording using the same record file.

  chunk
    The `chunk` message is sent when the next chunk is reached.  The message
    structure is like below:

      {{
        "type": "chunk",
        "data": {{
          "chunk": {{
            "timestamp": <unix-time-ms>,
            "pos": 0,
          }}
        }}
      }}

    where:
      timestamp
        Unix time value in ms when started recording data in this chunk.  The
        Unix time value is calculated using TOT/TDT packets and PCR values.

      pos
        File position in bytes.  The value is a multiple of the chunk size.

  event-start
    The `event-start` message is sent when started recoring a program.  The
    message structure is like below:

      {{
        "type": "event-start",
        "data": {{
          "originalNetworkId": 1,
          "transportStreamId": 2,
          "serviceId": 3,
          "event": {{ ... }},
          "record": {{ ... }}
        }}
      }}

    where:
      event
        Information about the program.  It's the same structure as the `events`
        property output from `collect-eits`.

      record
        Unix time value and file offset when started recording the program.
        It's the same structure as the `chunk` property in the `chunk-timestamp`
        message.

  event-update
    The `event-update` message is sent when flushed a chunk.  The message
    structure is the same as the `event-start` message.

  event-end
    The `event-end` message is sent when ended recoring a program.  The message
    structure is the same as the `event-start` message.

Environment Variables:
  MIRAKC_ARIB_KEEP_UNICODE_SYMBOLS
    Set `1` if you like to keep Unicode symbols like enclosed ideographic
    supplement characters.

    This option is added just for backword-compatibility.  It's not recommended
    to use this option in normal use cases.  Because some functions of
    EPGStation like the de-duplication of recorded programs won't work properly
    if this option is specified.
)";

static const std::string kTrackAirtime = "track-airtime";

static const std::string kTrackAirtimeHelp = R"(
Track changes of an event

Usage:
  mirakc-arib track-airtime --sid=<sid> --eid=<eid> [<file>]

Options:
  -h --help
    Print help.

  --sid=<sid>
    Service ID.

  --eid=<eid>
    Event ID of a TV program.

Arguments:
  <file>
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

static const std::string kSeekStart = "seek-start";

static const std::string kSeekStartHelp = R"(
Seek the start position of a TV program

Usage:
  mirakc-arib seek-start --sid=<sid>
    [--max-duration=<ms>] [--max-packets=<num>] [<file>]

Options:
  -h --help
    Print help.

  --sid=<sid>
    Service ID.

  --max-duration=<ms>
    The maximum duration used for detecting a stream transition point.

  --max-packets=<num>
    The maximum number of packets used for detecting a stream transion point.

Arguments:
  <file>
    Path to a TS file.

Description:
  `seek-start` checks the leading packets in the TS stream and start streaming
  from the start position of a TV program.

  Currently, `seek-start` checks only the change of the number of audio streams
  for detecting a stream transition point.  This is not a perfect solution, but
  works well in most cases.

  When a stream transition is detected, `seek-start` start streaming from a PSUI
  packet of a PAT just before the transition point.  Otherwise, `seek-start`
  outputs all packets in the TS stream.

  One of --max-duration and --max-packets must be specified.  Usually, it's
  enough to specify only --max-duration.  --max-packets can be used for
  limitting the memory usage.
)";

static const std::string kPrintPes = "print-pes";

static const std::string kPrintPesHelp = R"(
Print ES packets in a TS stream

Usage:
  mirakc-arib print-pes [<file>]

Options:
  -h --help
    Print help.

Arguments:
  <file>
    Path to a TS file.

Description:
  `print-pes` prints ES packets in a TS stream.  Each line is formatted like
  below:

    [DATETIME]|[CLOCK]|<MESSAGE>

  where '[...]' means that the field is optional.

  The DATETIME is NOT based on the system clock.  It's computed from PCR and
  TDT/TOT included in the TS stream.

  The CLOCK is one of PCR, DTS or PTS.  It's formatted like below:

    <decimal integer of PCR base>+<decimal integer of PCR extention>

  Currently, the following packets and tables are shown:

    * Packets having PCR, DTS and/or PTS
    * PAT
    * CAT
    * PMT
    * EIT p/f Actual
    * TDT/TOT

  At this moment, `print-pes` doens't support a TS stream which includes
  multiple service streams.

Examples:
  Show ES packets in a specific service stream:

    $ cat nhk.ts | mirakc-arib filter-service --sid=1024 | \
        mirakc-arib print-pes
                           |              |PAT: V#7
                           |              |  SID#0400 => PMT#01F0
                           |3172531391+124|PCR#01FF
                           |3172536790+227|PCR#01FF
                           |              |PMT: SID#0400 PCR#01FF V#9
                           |              |  PES#0100 => Video#02
                           |              |  PES#0110 => Audio#0F
    ...
    2020/06/02 22:29:03.000|              |TOT
    2020/06/02 22:29:03.060|3172585068+178|PCR#01FF
    2020/06/02 22:29:03.119|3172590391+038|PCR#01FF
    ...
)";

class PosixFile final : public File {
 public:
  enum class Mode { kWrite };

  PosixFile(const std::string& path)
      : path_(path) {
    if  (path.empty()) {
      stdio_ = true;
      path_ = "<stdin>";
      fd_ = STDIN_FILENO;
      MIRAKC_ARIB_INFO("Read packets from STDIN...");
    } else {
      fd_ = open(path.c_str(), O_RDONLY);
      if (fd_ > 0) {
        MIRAKC_ARIB_INFO("Read packets from {}...", path);
      } else {
        MIRAKC_ARIB_ERROR(
            "Failed to open {}: {} ({})", path, std::strerror(errno), errno);
      }
    }
  }

  PosixFile(const std::string& path, Mode)
      : path_(path) {
    if (path.empty()) {
      stdio_ = true;
      path_ = "<stdout>";
      fd_ = STDOUT_FILENO;
      MIRAKC_ARIB_INFO("Write packets to STDOUT...");
    } else {
      fd_ = open(path.c_str(), O_CREAT | O_RDWR, 0644);
      if (fd_ > 0) {
        MIRAKC_ARIB_INFO("Write packets to {}...", path);
      } else {
        MIRAKC_ARIB_ERROR(
            "Failed to open {}: {} ({})", path, std::strerror(errno), errno);
      }
    }
  }

  ~PosixFile() override {
    if (!stdio_) {
      close(fd_);
    }
  }

  const std::string& path() const override {
    return path_;
  }

  ssize_t Read(uint8_t* buf, size_t len) override {
    auto result = read(fd_, reinterpret_cast<void*>(buf), len);
    if (result < 0) {
      MIRAKC_ARIB_ERROR("Failed to read from {}: {} ({})", path_, std::strerror(errno), errno);
    }
    return result;
  }

  ssize_t Write(uint8_t* buf, size_t len) override {
    auto result = write(fd_, reinterpret_cast<void*>(buf), len);
    if (result < 0) {
      MIRAKC_ARIB_ERROR("Failed to write to {}: {} ({})", path_, std::strerror(errno), errno);
    }
    return result;
  }

  bool Sync() override {
    MIRAKC_ARIB_ASSERT(!stdio_);
    if (fsync(fd_) < 0) {
      MIRAKC_ARIB_ERROR("Failed to sync {}: {} ({})", path_, std::strerror(errno), errno);
      return false;
    }
    return true;
  }

  bool Trunc(int64_t size) override {
    MIRAKC_ARIB_ASSERT(!stdio_);
    auto result = ftruncate(fd_, static_cast<off_t>(size));
    if (result < 0) {
      MIRAKC_ARIB_ERROR("Failed to truncate {} to {}: {} ({})",
          path_, size, std::strerror(errno), errno);
      return false;
    }
    return true;
  }

  int64_t Seek(int64_t offset, SeekMode mode) override {
    MIRAKC_ARIB_ASSERT(!stdio_);
    int whence;
    switch (mode) {
      case SeekMode::kSet:
        whence = SEEK_SET;
        break;
      case SeekMode::kCur:
        whence = SEEK_CUR;
        break;
      case SeekMode::kEnd:
        whence = SEEK_END;
        break;
    }

    auto result = lseek(fd_, static_cast<off_t>(offset), whence);
    if (result < 0) {
      MIRAKC_ARIB_ERROR("Failed to seek {}: {} ({})", path_, std::strerror(errno), errno);
      return -1;
    }

    return static_cast<int64_t>(result);
  }

 private:
  std::string path_;
  int fd_ = -1;
  bool stdio_ = false;
};

void Init(const Args& args) {
  if (args.at(kScanServices).asBool()) {
    InitLogger(kScanServices);
  } else if (args.at(kSyncClocks).asBool()) {
    InitLogger(kSyncClocks);
  } else if (args.at(kCollectEits).asBool()) {
    InitLogger(kCollectEits);
  } else if (args.at(kCollectLogos).asBool()) {
    InitLogger(kCollectLogos);
  } else if (args.at(kFilterService).asBool()) {
    InitLogger(kFilterService);
  } else if (args.at(kFilterProgram).asBool()) {
    InitLogger(kFilterProgram);
  } else if (args.at(kRecordService).asBool()) {
    InitLogger(kRecordService);
  } else if (args.at(kTrackAirtime).asBool()) {
    InitLogger(kTrackAirtime);
  } else if (args.at(kSeekStart).asBool()) {
    InitLogger(kSeekStart);
  } else if (args.at(kPrintPes).asBool()) {
    InitLogger(kPrintPes);
  }

  ts::DVBCharset::EnableARIBMode();
}

std::unique_ptr<PacketSource> MakePacketSource(const Args& args) {
  static const std::string kFile = "<file>";

  std::string path = args.at(kFile).isString() ? args.at(kFile).asString() : "";
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

void LoadClockBaseline(const Args& args, ClockBaseline* cbl) {
  static const std::string kClockPid = "--clock-pid";
  static const std::string kClockPcr = "--clock-pcr";
  static const std::string kClockTime = "--clock-time";

  ts::PID pid = static_cast<uint16_t>(args.at(kClockPid).asLong());
  auto pcr = args.at(kClockPcr).asInt64();
  auto time = ConvertUnixTimeToJstTime(
      static_cast<ts::MilliSecond>(args.at(kClockTime).asInt64()));

  // Don't change the order of the following method calls.
  cbl->SetPid(pid);
  cbl->SetPcr(pcr);
  cbl->SetTime(time);

  MIRAKC_ARIB_INFO("Clock: PID={:04X} PCR={:011X} Time={}", pid, pcr, time);
}

void LoadComponentTags(const Args& args, const std::string& name,
    std::unordered_set<uint8_t>* tags) {
  if (!args.at(name)) {
    return;
  }

  auto list = args.at(name).asStringList();
  for (const auto& str : list) {
    size_t pos;
    // NOTE
    // ----
    // std::stoul() does NOT throw a std::out_of_range for negative values as described in:
    // https://stackoverflow.com/questions/19327845/why-does-stdstoul-convert-negative-numbers
    //
    // As a workaround, the program aborts if conditions are not met.  Don't use assertion macros
    // to ensure the conditions.  Assertion macros may be disabled.
    auto val = std::stoi(str, &pos);
    if (pos != str.length()) {
      MIRAKC_ARIB_ERROR("{}: must be a number: {}", name, str);
      std::abort();
    }
    if (val < 0) {
      MIRAKC_ARIB_ERROR("{}: must be zero or a positive number: {}", name, str);
      std::abort();
    }
    if (val >= 256) {
      MIRAKC_ARIB_ERROR("{}: must be smaller than 256: {}", name, str);
      std::abort();
    }
    tags->insert(static_cast<uint8_t>(val));
  }

  MIRAKC_ARIB_INFO("{}: {}", name, fmt::join(*tags, ", "));
}

void LoadOption(const Args& args, EitCollectorOption* opt) {
  static const std::string kTimeLimit = "--time-limit";
  static const std::string kStreaming = "--streaming";
  static const std::string kUseUnicodeSymbol = "--use-unicode-symbol";

  LoadSidSet(args, "--sids", &opt->sids);
  LoadSidSet(args, "--xsids", &opt->xsids);
  if (args.at(kTimeLimit)) {
    opt->time_limit =
        static_cast<ts::MilliSecond>(args.at(kTimeLimit).asInt64());
  }
  opt->streaming = args.at(kStreaming).asBool();
  auto use_unicode_symbol = args.at(kUseUnicodeSymbol).asBool();
  if (use_unicode_symbol) {
    g_KeepUnicodeSymbols = true;
  }
  MIRAKC_ARIB_INFO("Options: time-limit={}, streaming={} use-unicode-symbol={}",
                   opt->time_limit, opt->streaming, use_unicode_symbol);
}

void LoadOption(const Args& args, ServiceFilterOption* opt) {
  static const std::string kSid = "--sid";

  if (args.at(kSid)) {
    opt->sid = static_cast<uint16_t>(args.at(kSid).asLong());
    if (opt->sid != 0) {
      MIRAKC_ARIB_INFO("ServiceFilterOptions: sid=#{:04X}", opt->sid);
    }
  }
}

void LoadOption(const Args& args, ProgramFilterOption* opt) {
  static const std::string kSid = "--sid";
  static const std::string kEid = "--eid";
  static const std::string kClockPid = "--clock-pid";
  static const std::string kClockPcr = "--clock-pcr";
  static const std::string kClockTime = "--clock-time";
  static const std::string kAudioTags = "--audio-tags";
  static const std::string kVideoTags = "--video-tags";
  static const std::string kStartMargin = "--start-margin";
  static const std::string kEndMargin = "--end-margin";
  static const std::string kPreStreaming = "--pre-streaming";

  opt->sid = static_cast<uint16_t>(args.at(kSid).asLong());
  opt->eid = static_cast<uint16_t>(args.at(kEid).asLong());
  opt->clock_pid = static_cast<uint16_t>(args.at(kClockPid).asLong());
  opt->clock_pcr = args.at(kClockPcr).asInt64();
  opt->clock_time = ConvertUnixTimeToJstTime(
      static_cast<ts::MilliSecond>(args.at(kClockTime).asInt64()));
  LoadComponentTags(args, kAudioTags, &opt->audio_tags);
  LoadComponentTags(args, kVideoTags, &opt->video_tags);
  if (args.at(kStartMargin)) {
    opt->start_margin =
        static_cast<ts::MilliSecond>(args.at(kStartMargin).asInt64());
  }
  if (args.at(kEndMargin)) {
    opt->end_margin =
        static_cast<ts::MilliSecond>(args.at(kEndMargin).asInt64());
  }
  opt->pre_streaming = args.at(kPreStreaming).asBool();
  MIRAKC_ARIB_INFO(
      "ProgramFilterOptions: sid={:04X} eid={:04X} clock=({:04X}, {:011X}, {})"
      " margin=({}, {}) pre-streaming={}",
      opt->sid, opt->eid, opt->clock_pid, opt->clock_pcr, opt->clock_time,
      opt->start_margin, opt->end_margin, opt->pre_streaming);
}

void LoadOption(const Args& args, ServiceRecorderOption* opt) {
  static const std::string kSid = "--sid";
  static const std::string kFile = "--file";
  static const std::string kChunkSize = "--chunk-size";
  static const std::string kNumChunks = "--num-chunks";
  static const std::string kStartPos = "--start-pos";

  opt->sid = static_cast<uint16_t>(args.at(kSid).asLong());
  opt->file = args.at(kFile).asString();
  opt->chunk_size = static_cast<size_t>(args.at(kChunkSize).asLong());
  if (opt->chunk_size == 0) {
    MIRAKC_ARIB_ERROR("chunk-size must be a positive integer");
    std::abort();
  }
  if (opt->chunk_size % RingFileSink::kBufferSize != 0) {
    MIRAKC_ARIB_ERROR("chunk-size must be a multiple of {}", RingFileSink::kBufferSize);
    std::abort();
  }
  if (opt->chunk_size > RingFileSink::kMaxChunkSize) {
    MIRAKC_ARIB_ERROR("chunk-size must be less than or equal to {}", RingFileSink::kMaxChunkSize);
    std::abort();
  }
  opt->num_chunks = static_cast<size_t>(args.at(kNumChunks).asLong());
  if (opt->num_chunks == 0) {
    MIRAKC_ARIB_ERROR("chunk-size must be a positive integer");
    std::abort();
  }
  if (opt->num_chunks > RingFileSink::kMaxNumChunks) {
    MIRAKC_ARIB_ERROR("chunk-size must be less than or equal to {}", RingFileSink::kMaxNumChunks);
    std::abort();
  }
  if (args.at(kStartPos)) {
    opt->start_pos = args.at(kStartPos).asUint64();
    if (opt->start_pos % static_cast<uint64_t>(opt->chunk_size) != 0) {
      MIRAKC_ARIB_ERROR("start-pos must be a multiple of chunk-size");
      std::abort();
    }
    if (opt->start_pos >=
        static_cast<uint64_t>(opt->chunk_size) * static_cast<uint64_t>(opt->num_chunks)) {
      MIRAKC_ARIB_ERROR("start-pos must be a less than the maximum file size");
      std::abort();
    }
  }
  MIRAKC_ARIB_INFO(
      "ServiceRecorderOptions: sid={:04X} file={} chunk-size={} num-chunks={} start-pos={}",
      opt->sid, opt->file, opt->chunk_size, opt->num_chunks, opt->start_pos);
}

void LoadOption(const Args& args, AirtimeTrackerOption* opt) {
  static const std::string kSid = "--sid";
  static const std::string kEid = "--eid";

  opt->sid = static_cast<uint16_t>(args.at(kSid).asLong());
  opt->eid = static_cast<uint16_t>(args.at(kEid).asLong());
  MIRAKC_ARIB_INFO("Options: sid={:04X} eid={:04X}", opt->sid, opt->eid);
}

void LoadOption(const Args& args, StartSeekerOption* opt) {
  static const std::string kSid = "--sid";
  static const std::string kMaxDuration = "--max-duration";
  static const std::string kMaxPackets = "--max-packets";

  opt->sid = static_cast<uint16_t>(args.at(kSid).asLong());
  if (args.at(kMaxDuration)) {
    opt->max_duration =
        static_cast<ts::MilliSecond>(args.at(kMaxDuration).asInt64());
  }
  if (args.at(kMaxPackets)) {
    opt->max_packets = static_cast<size_t>(args.at(kMaxPackets).asLong());
  }
  if (opt->max_duration == 0 && opt->max_packets == 0) {
    fmt::print(kSeekStartHelp);
    exit(EXIT_FAILURE);
  }
  MIRAKC_ARIB_INFO("Options: sid={:04X} max-duration={} max-packets={}",
                   opt->sid, opt->max_duration, opt->max_packets);
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
  if (args.at(kCollectLogos).asBool()) {
    auto collector = std::make_unique<LogoCollector>();
    collector->Connect(std::move(std::make_unique<StdoutJsonlSink>()));
    return collector;
  }
  if (args.at(kFilterService).asBool()) {
    ServiceFilterOption option;
    LoadOption(args, &option);
    auto filter = std::make_unique<ServiceFilter>(option);
    filter->Connect(std::make_unique<StdoutSink>());
    return filter;
  }
  if (args.at(kFilterProgram).asBool()) {
    ProgramFilterOption program_filter_option;
    LoadOption(args, &program_filter_option);
    auto program_filter = std::make_unique<ProgramFilter>(program_filter_option);
    program_filter->Connect(std::make_unique<StdoutSink>());
    ServiceFilterOption service_filter_option;
    LoadOption(args, &service_filter_option);
    auto service_filter = std::make_unique<ServiceFilter>(service_filter_option);
    service_filter->Connect(std::move(program_filter));
    return service_filter;
  }
  if (args.at(kRecordService).asBool()) {
    ServiceRecorderOption recorder_option;
    LoadOption(args, &recorder_option);
    auto file = std::make_unique<PosixFile>(recorder_option.file, PosixFile::Mode::kWrite);
    auto sink = std::make_unique<RingFileSink>(
        std::move(file), recorder_option.chunk_size, recorder_option.num_chunks);
    auto recorder = std::make_unique<ServiceRecorder>(recorder_option);
    recorder->ServiceRecorder::Connect(std::move(sink));
    recorder->JsonlSource::Connect(std::move(std::make_unique<StdoutJsonlSink>()));
    ServiceFilterOption filter_option;
    LoadOption(args, &filter_option);
    auto filter = std::make_unique<ServiceFilter>(filter_option);
    filter->Connect(std::move(recorder));
    return filter;
  }
  if (args.at(kTrackAirtime).asBool()) {
    AirtimeTrackerOption option;
    LoadOption(args, &option);
    auto tracker = std::make_unique<AirtimeTracker>(option);
    tracker->Connect(std::move(std::make_unique<StdoutJsonlSink>()));
    return tracker;
  }
  if (args.at(kSeekStart).asBool()) {
    StartSeekerOption option;
    LoadOption(args, &option);
    auto seeker = std::make_unique<StartSeeker>(option);
    seeker->Connect(std::make_unique<StdoutSink>());
    return seeker;
  }
  if (args.at(kPrintPes).asBool()) {
    return std::make_unique<PesPrinter>();
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
  } else if (args.at(kCollectLogos).asBool()) {
    fmt::print(kCollectLogosHelp);
  } else if (args.at(kFilterService).asBool()) {
    fmt::print(kFilterServiceHelp);
  } else if (args.at(kFilterProgram).asBool()) {
    fmt::print(kFilterProgramHelp);
  } else if (args.at(kRecordService).asBool()) {
    fmt::print(kRecordServiceHelp);
  } else if (args.at(kTrackAirtime).asBool()) {
    fmt::print(kTrackAirtimeHelp);
  } else if (args.at(kSeekStart).asBool()) {
    fmt::print(kSeekStartHelp);
  } else if (args.at(kPrintPes).asBool()) {
    fmt::print(kPrintPesHelp);
  } else {
    fmt::print(kUsage);
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  spdlog::cfg::load_env_levels("MIRAKC_ARIB_LOG");

  auto keep_unicode_symbols = std::getenv("MIRAKC_ARIB_KEEP_UNICODE_SYMBOLS");
  if (keep_unicode_symbols != nullptr && std::string(keep_unicode_symbols) == "1") {
    g_KeepUnicodeSymbols = true;
  }

  auto version = fmt::format(kVersion,
                             MIRAKC_ARIB_VERSION,
                             MIRAKC_ARIB_DOCOPT_VERSION,
                             MIRAKC_ARIB_FMT_VERSION,
                             MIRAKC_ARIB_SPDLOG_VERSION,
                             MIRAKC_ARIB_RAPIDJSON_VERSION,
                             MIRAKC_ARIB_CPPCODEC_VERSION,
                             MIRAKC_ARIB_ARIBB24_VERSION,
                             MIRAKC_ARIB_TSDUCK_ARIB_VERSION,
                             MIRAKC_ARIB_LIBISDB_VERSION);

  auto args =
      docopt::docopt(kUsage, { argv + 1, argv + argc }, false, version);

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
