#pragma once

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

#include <tsduck/tsduck.h>

#include "base.hh"
#include "logging.hh"
#include "packet_sink.hh"
#include "packet_source.hh"
#include "tsduck_helper.hh"

namespace {

class PesPrinter final : public PacketSink,
                         public ts::TableHandlerInterface {
 public:
  PesPrinter()
      : demux_(context_) {
    demux_.setTableHandler(this);
    demux_.addPID(ts::PID_PAT);
    demux_.addPID(ts::PID_CAT);
    demux_.addPID(ts::PID_EIT);
    demux_.addPID(ts::PID_TOT);
  }

  ~PesPrinter() override {}

  bool HandlePacket(const ts::TSPacket& packet) override {
    auto pid = packet.getPID();
    auto it = clock_map_.find(pid);
    if (packet.hasPCR() && packet.getPCR() != ts::INVALID_PCR) {
      if (it != clock_map_.end()) {
        auto pcr = static_cast<int64_t>(packet.getPCR());
        Print(pid, pcr, fmt::format("PCR#{:04X}", pid));
        it->second.UpdatePcr(pcr);
      }
    }
    if (packet.hasPTS() && packet.getPTS() != ts::INVALID_PTS) {
      auto pcr = static_cast<int64_t>(packet.getPTS()) * kMaxPcrExt;
      MIRAKC_ARIB_ASSERT(IsValidPcr(pcr));
      const auto it = stream_map_.find(pid);
      if (it != stream_map_.end()) {
        Print(it->second.pcr_pid, pcr, fmt::format("{}#{:04X} PTS", it->second.type, pid));
      } else {
        Print(ts::PID_NULL, pcr, fmt::format("PES#{:04X} PTS", pid));
      }
    }
    if (packet.hasDTS() && packet.getDTS() != ts::INVALID_DTS) {
      auto pcr = static_cast<int64_t>(packet.getDTS()) * kMaxPcrExt;
      MIRAKC_ARIB_ASSERT(IsValidPcr(pcr));
      const auto it = stream_map_.find(pid);
      if (it != stream_map_.end()) {
        Print(it->second.pcr_pid, pcr, fmt::format("{}#{:04X} DTS", it->second.type, pid));
      } else {
        Print(ts::PID_NULL, pcr, fmt::format("PES#{:04X} DTS", pid));
      }
    }
    demux_.feedPacket(packet);
    return done_ ? false : true;
  }

 private:
  void Print(const std::string& msg) {
    fmt::print("                       |              |{}\n", msg);
  }

  void Print(ts::PID pcr_pid, int64_t pcr, const std::string& msg) {
    const auto it = clock_map_.find(pcr_pid);
    if (it != clock_map_.end() && it->second.IsReady()) {
      auto time = it->second.PcrToTime(pcr);
      fmt::print("{}|{}|{}\n", time, FormatPcr(pcr), msg);
    } else {
      fmt::print("                       |{}|{}\n", FormatPcr(pcr), msg);
    }
  }

  void Print(const ts::Time& time, const std::string& msg) {
    fmt::print("{}|              |{}\n", time, msg);
  }

  void handleTable(ts::SectionDemux&, const ts::BinaryTable& table) override {
    switch (table.tableId()) {
      case ts::TID_PAT:
        HandlePat(table);
        break;
      case ts::TID_CAT:
        HandleCat(table);
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
    ts::PAT pat(context_, table);

    if (!pat.isValid()) {
      MIRAKC_ARIB_WARN("Broken PAT, skip");
      return;
    }

    ResetStates();

    Print(fmt::format("PAT: V#{} PID#{:04X}", pat.version, table.sourcePID()));

    if (table.sourcePID() == ts::PID_PAT) {
      for (const auto& [sid, pmt_pid] : pat.pmts) {
        Print(fmt::format("  SID#{:04X} => PMT#{:04X}", sid, pmt_pid));
        demux_.addPID(pmt_pid);
        sids_.insert(sid);
        pmt_pids_.push_back(pmt_pid);
      }

      if (pmt_pids_.empty()) {
        done_ = true;
        MIRAKC_ARIB_WARN("No service defined in PAT, done");
      }
    } else {
      // Strange PAT
      for (const auto& [sid, pmt_pid] : pat.pmts) {
        Print(fmt::format("  SID#{:04X} => PMT#{:04X}", sid, pmt_pid));
      }
    }
  }

  void HandleCat(const ts::BinaryTable& table) {
    ts::CAT cat(context_, table);

    if (!cat.isValid()) {
      MIRAKC_ARIB_WARN("Broken CAT, skip");
      return;
    }

    Print(fmt::format("CAT: V#{}", cat.version));
  }

  void HandlePmt(const ts::BinaryTable& table) {
    ts::PMT pmt(context_, table);

    if (!pmt.isValid()) {
      MIRAKC_ARIB_WARN("Broken PMT, skip");
      return;
    }

    Print(fmt::format("PMT: SID#{:04X} PCR#{:04X} V#{}",
                      pmt.service_id, pmt.pcr_pid, pmt.version));
    if (pmt.pcr_pid != ts::PID_NULL) {
      Clock clock;
      clock.SetPid(pmt.pcr_pid);
      clock_map_[pmt.pcr_pid] = clock;
    }

    for (const auto& [pid, stream] : pmt.streams) {
      if (stream.isAudio()) {
        stream_map_[pid] = {"Audio", pmt.pcr_pid};
        Print(fmt::format("  PES#{:04X} => Audio#{:02X}", pid, stream.stream_type));
      } else if (stream.isVideo()) {
        stream_map_[pid] = {"Video", pmt.pcr_pid};
        Print(fmt::format("  PES#{:04X} => Video#{:02X}", pid, stream.stream_type));
      } else if (stream.isSubtitles()) {
        stream_map_[pid] = {"Subtitle", pmt.pcr_pid};
        Print(fmt::format("  PES#{:04X} => Subtitle#{:02X}", pid, stream.stream_type));
      } else if (IsAribSubtitle(stream)) {
        stream_map_[pid] = {"ARIB-Subtitle", pmt.pcr_pid};
        Print(fmt::format("  PES#{:04X} => ARIB-Subtitle#{:02X}", pid, stream.stream_type));
      } else if (IsAribSuperimposedText(stream)) {
        stream_map_[pid] = {"ARIB-SuperimposedText", pmt.pcr_pid};
        Print(fmt::format("  PES#{:04X} => ARIB-SuperimposedText#{:02X}",
                          pid, stream.stream_type));
      } else {
        stream_map_[pid] = {"Other", pmt.pcr_pid};
        Print(fmt::format("  PES#{:04X} => Other#{:02X}", pid, stream.stream_type));
      }
    }
  }

  void HandleEit(const ts::BinaryTable& table) {
    ts::EIT eit(context_, table);

    if (!eit.isValid()) {
      MIRAKC_ARIB_WARN("Broken EIT, skip");
      return;
    }

    if (sids_.find(eit.service_id) == sids_.end()) {
      return;
    }

    Print(fmt::format("EIT p/f Actual: SID#{:04X} V#{}",
                      eit.service_id, eit.version));
    for (size_t i = 0; i < eit.events.size(); ++i) {
      const auto& event = eit.events[i];
      Print(fmt::format("  Event[{}]: EID#{:04X} {} - {} ({}m)",
                        i, event.event_id, event.start_time,
                        event.start_time + event.duration * ts::MilliSecPerSec,
                        event.duration / 60));
    }
  }

  void HandleTdt(const ts::BinaryTable& table) {
    ts::TDT tdt(context_, table);

    if (!tdt.isValid()) {
      MIRAKC_ARIB_WARN("Broken TDT, skip");
      return;
    }

    Print(tdt.utc_time, "TDT");  // JST in ARIB

    for (auto& [pid, clock] : clock_map_) {
      clock.UpdateTime(tdt.utc_time);
    }
  }

  void HandleTot(const ts::BinaryTable& table) {
    ts::TOT tot(context_, table);

    if (!tot.isValid()) {
      MIRAKC_ARIB_WARN("Broken TOT, skip");
      return;
    }

    Print(tot.utc_time, "TOT");  // JST in ARIB

    for (auto& [pid, clock] : clock_map_) {
      clock.UpdateTime(tot.utc_time);
    }
  }

  void ResetStates() {
    MIRAKC_ARIB_DEBUG("Reset states");
    for (const auto& pid : pmt_pids_) {
      demux_.removePID(pid);
    }
    sids_.clear();
    pmt_pids_.clear();
    pcr_pids_.clear();
    done_ = false;
  }

  struct StreamInfo {
    std::string type;
    ts::PID pcr_pid;
  };

  ts::DuckContext context_;
  ts::SectionDemux demux_;
  std::set<uint16_t> sids_;
  std::vector<ts::PID> pmt_pids_;
  std::set<ts::PID> pcr_pids_;
  std::map<ts::PID, Clock> clock_map_;
  std::map<ts::PID, StreamInfo> stream_map_;
  bool done_ = false;
};

}  // namespace
