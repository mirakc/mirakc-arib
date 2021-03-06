#pragma once

#include <memory>

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <tsduck/tsduck.h>

#include "base.hh"
#include "jsonl_source.hh"
#include "logging.hh"
#include "packet_sink.hh"
#include "tsduck_helper.hh"

#define MIRAKC_ARIB_SERVICE_RECORDER_TRACE(...) \
  MIRAKC_ARIB_TRACE("service-recorder: " __VA_ARGS__)
#define MIRAKC_ARIB_SERVICE_RECORDER_DEBUG(...) \
  MIRAKC_ARIB_DEBUG("service-recorder: " __VA_ARGS__)
#define MIRAKC_ARIB_SERVICE_RECORDER_INFO(...) \
  MIRAKC_ARIB_INFO("service-recorder: " __VA_ARGS__)
#define MIRAKC_ARIB_SERVICE_RECORDER_WARN(...) \
  MIRAKC_ARIB_WARN("service-recorder: " __VA_ARGS__)
#define MIRAKC_ARIB_SERVICE_RECORDER_ERROR(...) \
  MIRAKC_ARIB_ERROR("service-recorder: " __VA_ARGS__)

namespace {

struct ServiceRecorderOption final {
  std::string file;
  uint16_t sid = 0;
  size_t chunk_size = 0;
  size_t num_chunks = 0;
  uint64_t start_pos = 0;
};

class ServiceRecorder final : public PacketSink,
                              public JsonlSource,
                              public PacketRingObserver,
                              public ts::TableHandlerInterface {
 public:
  explicit ServiceRecorder(const ServiceRecorderOption& option)
      : option_(option),
        demux_(context_) {
    demux_.setTableHandler(this);
    demux_.addPID(ts::PID_PAT);
    MIRAKC_ARIB_SERVICE_RECORDER_DEBUG("Demux PAT");
    demux_.addPID(ts::PID_EIT);
    MIRAKC_ARIB_SERVICE_RECORDER_DEBUG("Demux EIT");
    demux_.addPID(ts::PID_TOT);
    MIRAKC_ARIB_SERVICE_RECORDER_DEBUG("Demux TDT/TOT");
  }

  ~ServiceRecorder() override = default;

  void Connect(std::unique_ptr<PacketRingSink>&& sink) {
    sink_ = std::move(sink);
    sink_->SetObserver(this);
  }

  bool Start() override {
    if (!sink_) {
      MIRAKC_ARIB_SERVICE_RECORDER_ERROR("No sink has not been connected");
      return false;
    }
    if (!sink_->Start()) {
      return false;
    }
    if (option_.start_pos != 0) {
      if (!sink_->SetPosition(option_.start_pos)) {
        return false;
      }
    }
    SendStartMessage();
    return true;
  }

  bool End() override {
    if (!sink_) {
      MIRAKC_ARIB_SERVICE_RECORDER_ERROR("No sink has not been connected");
      return false;
    }
    bool success = sink_->End();
    SendStopMessage(success);
    return success;
  }

  bool HandlePacket(const ts::TSPacket& packet) override {
    if (!sink_) {
      MIRAKC_ARIB_SERVICE_RECORDER_ERROR("No sink has not been connected");
      return false;
    }

    auto pid = packet.getPID();
    if (clock_.HasPid() && clock_.pid() == pid && packet.hasPCR()) {
      auto pcr = packet.getPCR();
      if (pcr != ts::INVALID_PCR) {
        clock_.UpdatePcr(pcr);
      }
    }

    demux_.feedPacket(packet);

    switch (state_) {
      case State::kPreparing:
        return OnPreparing(packet);
      case State::kRecording:
        return OnRecording(packet);
      case State::kDone:
        return false;
    }
  }

  void OnEndOfChunk(uint64_t) override {
    end_of_chunk_ = true;
  }

 private:
  enum class State {
    kPreparing,
    kRecording,
    kDone,
  };

  void handleTable(ts::SectionDemux&, const ts::BinaryTable& table) override {
    switch (table.tableId()) {
      case ts::TID_PAT:
        HandlePat(table);
        break;
      case ts::TID_PMT:
        HandlePmt(table);
        break;
      case ts::TID_EIT_PF_ACT:
        HandleEit(table);
        break;
      case ts::TID_TDT:
        HandleTdt(table);
        break;
      case ts::TID_TOT:
        HandleTot(table);
        break;
      default:
        break;
    }
  }

  void HandlePat(const ts::BinaryTable& table) {
    // See comments in ProgramFiler::HandlePat().
    if (table.sourcePID() != ts::PID_PAT) {
      MIRAKC_ARIB_SERVICE_RECORDER_WARN("PAT delivered with PID#{:04X}, skip", table.sourcePID());
      return;
    }

    ts::PAT pat(context_, table);

    if (!pat.isValid()) {
      MIRAKC_ARIB_SERVICE_RECORDER_WARN("Broken PAT, skip");
      return;
    }

    if (pat.ts_id == 0) {
      MIRAKC_ARIB_SERVICE_RECORDER_WARN("PAT for TSID#0000, skip");
      return;
    }

    // The following condition is ensured by ServiceFilter.
    MIRAKC_ARIB_ASSERT(pat.pmts.find(option_.sid) != pat.pmts.end());

    auto new_pmt_pid = pat.pmts[option_.sid];

    if (pmt_pid_ != ts::PID_NULL) {
      MIRAKC_ARIB_SERVICE_RECORDER_DEBUG("Demux -= PMT#{:04X}", pmt_pid_);
      demux_.removePID(pmt_pid_);
      pmt_pid_ = ts::PID_NULL;
    }

    pmt_pid_ = new_pmt_pid;
    demux_.addPID(pmt_pid_);
    MIRAKC_ARIB_SERVICE_RECORDER_DEBUG("Demux += PMT#{:04X}", pmt_pid_);
  }

  void HandlePmt(const ts::BinaryTable& table) {
    ts::PMT pmt(context_, table);

    if (!pmt.isValid()) {
      MIRAKC_ARIB_SERVICE_RECORDER_WARN("Broken PMT, skip");
      return;
    }

    if (pmt.service_id != option_.sid) {
      MIRAKC_ARIB_SERVICE_RECORDER_WARN("PMT.SID#{} not matched, skip", pmt.service_id);
      return;
    }

    auto pcr_pid = pmt.pcr_pid;
    if (!clock_.HasPid()) {
      MIRAKC_ARIB_SERVICE_RECORDER_DEBUG("PCR#{:04X}", pcr_pid);
      clock_.SetPid(pcr_pid);
    } else if (clock_.pid() != pcr_pid) {
      MIRAKC_ARIB_SERVICE_RECORDER_WARN(
          "PCR#{:04X} -> {:04X}, need resync", clock_.pid(), pcr_pid);
      clock_.SetPid(pcr_pid);
    }
  }

  void HandleEit(const ts::BinaryTable& table) {
    std::shared_ptr<ts::EIT> eit(new ts::EIT(context_, table));

    if (!eit->isValid()) {
      MIRAKC_ARIB_SERVICE_RECORDER_WARN("Broken EIT, skip");
      return;
    }

    if (eit->service_id != option_.sid) {
      MIRAKC_ARIB_TRACE("SID#{:04X} not matched with {:04X}, skip", eit->service_id, option_.sid);
      return;
    }

    auto num_events = eit->events.size();
    if (num_events == 0) {
      MIRAKC_ARIB_SERVICE_RECORDER_WARN("No event in EIT, skip");
      return;
    }

    const auto& event = GetEvent(eit);
    auto end_time = GetEventEndTime(event);
    MIRAKC_ARIB_SERVICE_RECORDER_DEBUG(
        "Event#{:04X}: {} .. {}", event.event_id, event.start_time, end_time);

    // See SendEventMessage() for the reason why we remove eit->events[1..] here.
    for (size_t i = 1; i < num_events; ++i) {
      // ts::EIT::Event List is NOT a list.
      // It's a subclass of std::map.
      eit->events.erase(i);
    }

    // For keeping the locality of side effects, we don't update eit_ here.  It will be updated
    // in the implementation of the state machine.
    new_eit_ = std::move(eit);
  }

  void HandleTdt(const ts::BinaryTable& table) {
    ts::TDT tdt(context_, table);
    if (!tdt.isValid()) {
      MIRAKC_ARIB_SERVICE_RECORDER_WARN("Broken TDT, skip");
      return;
    }
    clock_.UpdateTime(tdt.utc_time);  // JST in ARIB
  }

  void HandleTot(const ts::BinaryTable& table) {
    ts::TOT tot(context_, table);
    if (!tot.isValid()) {
      MIRAKC_ARIB_SERVICE_RECORDER_WARN("Broken TOT, skip");
      return;
    }
    clock_.UpdateTime(tot.utc_time);  // JST in ARIB
  }

  bool OnPreparing(const ts::TSPacket& packet) {
    if (clock_.IsReady() && new_eit_) {
      eit_ = std::move(new_eit_);
      state_ = State::kRecording;
      MIRAKC_ARIB_SERVICE_RECORDER_INFO("Ready for recording");

      auto now = clock_.Now();

      SendChunkMessage(now, sink_->sync_pos());
      end_of_chunk_ = false;

      UpdateEventBoundary(now, sink_->pos());

      auto end_time = GetEventEndTime(GetEvent(eit_));
      if (now < end_time) {
        SendEventStartMessage(eit_);
        event_started_ = true;
      } else {
        event_started_ = false;
      }
      return true;
    }
    // Packets are dropped until ready.
    return true;
  }

  bool OnRecording(const ts::TSPacket& packet) {
    auto now = clock_.Now();
    if (end_of_chunk_) {
      // The `event-update` message must be sent before the `chunk` message.
      // The application may purge expired programs in the message handler for
      // the `chunk` message.  So, the program data must be updated before that.
      SendEventUpdateMessage(eit_, now, sink_->sync_pos());
      SendChunkMessage(now, sink_->sync_pos());
      end_of_chunk_ = false;
    }

    // Copy the safe pointers on the stack in order to hold EIT objects.
    auto eit = eit_;
    auto new_eit = new_eit_;
    auto event_changed = false;
    if (new_eit_) {
      if (GetEvent(eit_).event_id != GetEvent(new_eit_).event_id) {
        event_changed = true;
      } else {
        // Same EID, but event data might change.
        eit = new_eit;
      }
      eit_ = std::move(new_eit_);
    }

    if (event_started_) {
      if (event_changed) {
        MIRAKC_ARIB_SERVICE_RECORDER_WARN(
            "Event#{:04X} has started before Event#{:04X} ends",
            GetEvent(new_eit).event_id, GetEvent(eit).event_id);
        UpdateEventBoundary(now, sink_->pos());
        SendEventEndMessage(eit);
        SendEventStartMessage(new_eit);
      } else {
        auto end_time = GetEventEndTime(GetEvent(eit));
        if (now >= end_time) {
          UpdateEventBoundary(end_time, sink_->pos());
          SendEventEndMessage(eit);
          event_started_ = false;  // wait for new event
        }
      }
    } else {
      if (event_changed) {
        SendEventStartMessage(new_eit);
        event_started_ = true;
      }
    }
    return sink_->HandlePacket(packet);
  }

  void UpdateEventBoundary(const ts::Time& time, uint64_t pos) {
    MIRAKC_ARIB_SERVICE_RECORDER_DEBUG("Update event boundary with {}@{}", time, pos);
    event_boundary_time_ = time;
    event_boundary_pos_ = pos;
  }

  void SendStartMessage() {
    MIRAKC_ARIB_SERVICE_RECORDER_INFO("Started recording SID#{:04X}", option_.sid);

    rapidjson::Document doc(rapidjson::kObjectType);
    auto& allocator = doc.GetAllocator();

    doc.AddMember("type", "start", allocator);

    FeedDocument(doc);
  }

  void SendStopMessage(bool success) {
    MIRAKC_ARIB_SERVICE_RECORDER_INFO("Stopped recording SID#{:04X}", option_.sid);

    rapidjson::Document doc(rapidjson::kObjectType);
    auto& allocator = doc.GetAllocator();

    rapidjson::Value data(rapidjson::kObjectType);
    data.AddMember("reset", !success, allocator);

    doc.AddMember("type", "stop", allocator);
    doc.AddMember("data", data, allocator);

    FeedDocument(doc);
  }

  void SendChunkMessage(const ts::Time& time, int64_t pos) {
    MIRAKC_ARIB_SERVICE_RECORDER_INFO("Reached next chunk: {}@{}", time, pos);

    rapidjson::Document doc(rapidjson::kObjectType);
    auto& allocator = doc.GetAllocator();

    rapidjson::Value chunk(rapidjson::kObjectType);
    {
      const auto time_utc = time - kJstTzOffset;  // JST -> UTC
      const auto time_unix = time_utc - ts::Time::UnixEpoch;
      chunk.AddMember("timestamp", time_unix, allocator);
      chunk.AddMember("pos", static_cast<uint64_t>(pos), allocator);
    }

    rapidjson::Value data(rapidjson::kObjectType);
    data.AddMember("chunk", chunk, allocator);

    doc.AddMember("type", "chunk", allocator);
    doc.AddMember("data", data, allocator);

    FeedDocument(doc);
  }

  void SendEventStartMessage(const std::shared_ptr<ts::EIT>& eit) {
    MIRAKC_ARIB_ASSERT(eit);
    MIRAKC_ARIB_SERVICE_RECORDER_INFO(
        "Event#{:04X}: Started: {}@{}",
        GetEvent(eit).event_id, event_boundary_time_, event_boundary_pos_);
    SendEventMessage("event-start", eit, event_boundary_time_, event_boundary_pos_);
  }

  void SendEventUpdateMessage(
      const std::shared_ptr<ts::EIT>& eit, const ts::Time& time, uint64_t pos) {
    MIRAKC_ARIB_ASSERT(eit);
    MIRAKC_ARIB_SERVICE_RECORDER_INFO(
        "Event#{:04X}: Updated: {}@{}", GetEvent(eit).event_id, time, pos);
    SendEventMessage("event-update", eit, time, pos);
  }

  void SendEventEndMessage(const std::shared_ptr<ts::EIT>& eit) {
    MIRAKC_ARIB_ASSERT(eit);
    MIRAKC_ARIB_SERVICE_RECORDER_INFO(
        "Event#{:04X}: Ended: {}@{}",
        GetEvent(eit).event_id, event_boundary_time_, event_boundary_pos_);
    SendEventMessage("event-end", eit, event_boundary_time_, event_boundary_pos_);
  }

  void SendEventMessage(
      const std::string& type, const std::shared_ptr<ts::EIT>& eit,
      const ts::Time& time, uint64_t pos) {
    MIRAKC_ARIB_ASSERT(eit);

    rapidjson::Document doc(rapidjson::kObjectType);
    auto& allocator = doc.GetAllocator();

    // TODO
    // ----
    // We create a binary EIT sections in order to reuse the same JSON
    // serialization methods used in EitCollector.  However, the current
    // strategy is inefficient.
    //
    // NOTE
    // ----
    // ts::EIT::serializeContent() sorts events in order of start time.  This
    // works well in most cases, but might break the original order of the
    // events.  For example, midnight in Feb 13th, 2021, a big earthquake
    // suddenly occurred.  NHK canceled original programs and broadcast an
    // emergency program like below:
    //
    //   EIT p/f Actual, TID 78 (0x4E), PID 18 (0x0012)
    //   Version: 9, sections: 2, total size: 202 bytes
    //   - Section 0:
    //     Service Id: 1024 (0x0400)
    //     TS Id: 32736 (0x7FE0)
    //     Original Network Id: 32736 (0x7FE0)
    //     Segment last section: 1 (0x01)
    //     Last Table Id: 78 (0x4E), EIT p/f Actual
    //     - Event Id: 34584 (0x8718)
    //       Start JST: 2021/02/13 23:30:00
    //       Duration: 00:00:00
    //       Running status: undefined
    //       CA mode: free
    //   - Section 1:
    //     Service Id: 1024 (0x0400)
    //     TS Id: 32736 (0x7FE0)
    //     Original Network Id: 32736 (0x7FE0)
    //     Segment last section: 1 (0x01)
    //     Last Table Id: 78 (0x4E), EIT p/f Actual
    //     - Event Id: 65522 (0xFFF2)
    //       Start JST: 1970/01/01 00:00:00
    //       Duration: 00:00:00
    //       Running status: undefined
    //       CA mode: free
    //
    // For a workaround, we creates a temporal EIT which contains a single event
    // that we need to serialize.
    ts::BinaryTable table;
    eit->serialize(context_, table);
    auto section = table.sectionAt(0);
    EitSection eit_section(*section);
    auto events = eit_section.MakeEventsJsonValue(allocator);

    MIRAKC_ARIB_ASSERT(events.Size() > 0);
    auto event = events[0].GetObject();

    rapidjson::Value record(rapidjson::kObjectType);
    {
      const auto time_utc = time - kJstTzOffset;  // JST -> UTC
      const auto time_unix = time_utc - ts::Time::UnixEpoch;
      record.AddMember("timestamp", time_unix, allocator);
      record.AddMember("pos", static_cast<uint64_t>(pos), allocator);
    }

    rapidjson::Value data(rapidjson::kObjectType);
    data.AddMember("originalNetworkId", eit->onetw_id, allocator);
    data.AddMember("transportStreamId", eit->ts_id, allocator);
    data.AddMember("serviceId", eit->service_id, allocator);
    data.AddMember("event", event, allocator);
    data.AddMember("record", record, allocator);

    doc.AddMember("type", type, allocator);
    doc.AddMember("data", data, allocator);

    FeedDocument(doc);
  }

  const ts::EIT::Event& GetEvent(const std::shared_ptr<ts::EIT>& eit) const {
    MIRAKC_ARIB_ASSERT(eit->events.size() > 0);
    return eit->events[0];
  }

  ts::Time GetEventEndTime(const ts::EIT::Event& event) const {
    return event.start_time + (event.duration * ts::MilliSecPerSec);
  }

  const ServiceRecorderOption option_;
  ts::DuckContext context_;
  ts::SectionDemux demux_;
  std::unique_ptr<PacketRingSink> sink_;
  Clock clock_;
  ts::Time event_boundary_time_;
  uint64_t event_boundary_pos_;
  std::shared_ptr<ts::EIT> eit_;
  std::shared_ptr<ts::EIT> new_eit_;
  ts::PID pmt_pid_ = ts::PID_NULL;
  State state_ = State::kPreparing;
  bool end_of_chunk_ = false;
  bool event_started_ = false;

  MIRAKC_ARIB_NON_COPYABLE(ServiceRecorder);
};

}  // namespace
