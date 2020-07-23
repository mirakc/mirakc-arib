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

class TimetablePrinter final : public PacketSink,
                               public ts::TableHandlerInterface {
 public:
  TimetablePrinter()
      : demux_(context_) {
    demux_.setTableHandler(this);
    demux_.addPID(ts::PID_PAT);
    demux_.addPID(ts::PID_CAT);
    demux_.addPID(ts::PID_EIT);
    demux_.addPID(ts::PID_TOT);
  }

  ~TimetablePrinter() override {}

  bool HandlePacket(const ts::TSPacket& packet) override {
    auto pid = packet.getPID();
    if (packet.hasPCR() && packet.getPCR() != ts::INVALID_PCR) {
      Print(packet.getPCR(), fmt::format("PCR#{:04X}", pid));
      last_pcr_ = packet.getPCR();
    }
    if (packet.hasPTS() && packet.getPTS() != ts::INVALID_PTS) {
      const auto it = stream_type_map_.find(pid);
      if (it != stream_type_map_.end()) {
        Print(packet.getPTS() * 300,
              fmt::format("{}#{:04X} PTS", it->second, pid));
      } else {
        Print(packet.getPTS() * 300, fmt::format("PES#{:04X} PTS", pid));
      }
    }
    if (packet.hasDTS() && packet.getDTS() != ts::INVALID_DTS) {
      const auto it = stream_type_map_.find(pid);
      if (it != stream_type_map_.end()) {
        Print(packet.getDTS() * 300,
              fmt::format("{}#{:04X} DTS", it->second, pid));
      } else {
        Print(packet.getDTS() * 300, fmt::format("PES#{:04X} DTS", pid));
      }
    }
    demux_.feedPacket(packet);
    return done_ ? false : true;
  }

 private:
  void Print(const std::string& msg) {
    fmt::print("                       |              |{}\n", msg);
  }

  void Print(int64_t clock, const std::string& msg) {
    if (synced_) {
      auto delta_ms = (clock - last_sync_pcr_) / kPcrTicksPerMs;
      auto time = last_sync_time_ + delta_ms;
      fmt::print("{}|{}|{}\n", time, FormatPcr(clock), msg);
    } else {
      fmt::print("                       |{}|{}\n", FormatPcr(clock), msg);
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
      pcr_pids_.insert(pmt.pcr_pid);
    }

    for (const auto& [pid, stream] : pmt.streams) {
      if (stream.isAudio()) {
        stream_type_map_[pid] = "Audio";
        Print(fmt::format(
            "  PES#{:04X} => Audio#{:02X}", pid, stream.stream_type));
      } else if (stream.isVideo()) {
        stream_type_map_[pid] = "Video";
        Print(fmt::format(
            "  PES#{:04X} => Video#{:02X}", pid, stream.stream_type));
      } else if (stream.isSubtitles()) {
        stream_type_map_[pid] = "Subtitle";
        Print(fmt::format(
            "  PES#{:04X} => Subtitle#{:02X}", pid, stream.stream_type));
      } else if (IsAribSubtitle(stream)) {
        stream_type_map_[pid] = "ARIB-Subtitle";
        Print(fmt::format(
            "  PES#{:04X} => ARIB-Subtitle#{:02X}", pid, stream.stream_type));
      } else if (IsAribSuperimposedText(stream)) {
        stream_type_map_[pid] = "ARIB-SuperimposedText";
        Print(fmt::format(
            "  PES#{:04X} => ARIB-SuperimposedText#{:02X}",
            pid, stream.stream_type));
      } else {
        stream_type_map_[pid] = "Other";
        Print(fmt::format(
            "  PES#{:04X} => Other#{:02X}", pid, stream.stream_type));
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

    synced_ = true;
    last_sync_pcr_ = last_pcr_;
    last_sync_time_ = tdt.utc_time;
  }

  void HandleTot(const ts::BinaryTable& table) {
    ts::TOT tot(context_, table);

    if (!tot.isValid()) {
      MIRAKC_ARIB_WARN("Broken TOT, skip");
      return;
    }

    Print(tot.utc_time, "TOT");  // JST in ARIB

    synced_ = true;
    last_sync_pcr_ = last_pcr_;
    last_sync_time_ = tot.utc_time;
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

  ts::DuckContext context_;
  ts::SectionDemux demux_;
  int64_t last_pcr_ = 0;
  bool synced_ = false;
  int64_t last_sync_pcr_ = 0;
  ts::Time last_sync_time_;  // JST
  std::set<uint16_t> sids_;
  std::vector<ts::PID> pmt_pids_;
  std::set<ts::PID> pcr_pids_;
  std::map<ts::PID, std::string> stream_type_map_;
  bool done_ = false;
};

}  // namespace
