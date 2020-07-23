#pragma once

#include <algorithm>
#include <limits>
#include <memory>

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <tsduck/tsduck.h>

#include "base.hh"
#include "jsonl_source.hh"
#include "logging.hh"
#include "packet_source.hh"
#include "tsduck_helper.hh"

namespace {

struct PcrSynchronizerOption final {
  SidSet sids;
  SidSet xsids;
};

class PcrSynchronizer final : public PacketSink,
                              public JsonlSource,
                              public ts::TableHandlerInterface {
 public:
  PcrSynchronizer(const PcrSynchronizerOption& option)
      : option_(option),
        demux_(context_) {
    demux_.setTableHandler(this);
    demux_.addPID(ts::PID_PAT);
  }

  ~PcrSynchronizer() override {}

  bool End() override {
    if (!done_) {
      return false;
    }

    auto time = time_ - ts::Time::UnixEpoch;  // UNIX time (ms)
    time -= kJstTzOffset;  // JST -> UTC;

    rapidjson::Document json(rapidjson::kArrayType);
    auto& allocator = json.GetAllocator();
    for (const auto& [sid, pcr_pid] : pcr_pid_map_) {
      auto it = pcr_map_.find(pcr_pid);
      if (it == pcr_map_.end()) {
        continue;
      }
      rapidjson::Value clock(rapidjson::kObjectType);
      clock.AddMember("pid", pcr_pid, allocator);
      clock.AddMember("pcr", it->second, allocator);
      clock.AddMember("time", time, allocator);
      rapidjson::Value v(rapidjson::kObjectType);
      v.AddMember("nid", nid_, allocator);
      v.AddMember("tsid", tsid_, allocator);
      v.AddMember("sid", sid, allocator);
      v.AddMember("clock", clock, allocator);
      json.PushBack(v, allocator);
    }
    FeedDocument(json);
    return true;
  }

  bool HandlePacket(const ts::TSPacket& packet) override {
    auto pid = packet.getPID();
    if (pid == ts::PID_NULL) {
      return true;
    }

    demux_.feedPacket(packet);
    if (done_) {
      return false;
    }

    if (started_) {
      if (pcr_pids_.count(pid) == 1 && pcr_map_.find(pid) == pcr_map_.end()) {
        if (!packet.hasPCR() || packet.getPCR() == ts::INVALID_PCR) {
          // Many PCR packets in a specific channel have no valid PCR...
          // See https://github.com/masnagam/mirakc-arib/issues/3
          MIRAKC_ARIB_TRACE("PCR#{:04X} has no valid PCR...", pid);
        } else {
          auto pcr = static_cast<int64_t>(packet.getPCR());
          MIRAKC_ARIB_INFO("PCR#{:04X}: {}", pid, FormatPcr(pcr));
          pcr_map_[pid] = pcr;
          if (pcr_map_.size() == pcr_pids_.size()) {
            done_ = true;
            return false;
          }
        }
      }
    }

    return true;
  }

 private:
  void handleTable(ts::SectionDemux&, const ts::BinaryTable& table) override {
    switch (table.tableId()) {
      case ts::TID_PAT:
        HandlePat(table);
        break;
      case ts::TID_PMT:
        HandlePmt(table);
        break;
      case ts::TID_SDT_ACT:
        HandleSdt(table);
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
    if (table.sourcePID() != ts::PID_PAT) {
      MIRAKC_ARIB_WARN(
          "PAT delivered with PID#{:04X}, skip", table.sourcePID());
      return;
    }

    ts::PAT pat(context_, table);

    if (!pat.isValid()) {
      MIRAKC_ARIB_WARN("Broken PAT, skip");
      return;
    }

    if (pat.ts_id == 0) {
      MIRAKC_ARIB_WARN("PAT for TSID#0000, skip");
      return;
    }

    if (!pmt_pids_.empty()) {
      ResetStates();
    }

    for (const auto& [sid, pmt_pid] : pat.pmts) {
      if (!option_.sids.IsEmpty() && !option_.sids.Contain(sid)) {
        MIRAKC_ARIB_DEBUG(
            "Ignore SID#{:04X} according to the inclusion list", sid);
        continue;
      }
      if (!option_.xsids.IsEmpty() && option_.xsids.Contain(sid)) {
        MIRAKC_ARIB_DEBUG(
            "Ignore SID#{:04X} according to the exclusion list", sid);
        continue;
      }
      pmt_pids_[sid] = pmt_pid;
    }

    if (pmt_pids_.empty()) {
      done_ = true;
      MIRAKC_ARIB_WARN("No service defined in PAT, done");
    }

    demux_.addPID(ts::PID_SDT);
    MIRAKC_ARIB_DEBUG("Demux SDT");
  }

  void HandleSdt(const ts::BinaryTable& table) {
    ts::SDT sdt(context_, table);


    if (!sdt.isValid()) {
      MIRAKC_ARIB_WARN("Broken SDT, skip");
      return;
    }

    nid_ = sdt.onetw_id;
    tsid_ = sdt.ts_id;

    for (const auto& [sid, pid] : pmt_pids_) {
      const auto it = sdt.services.find(sid);
      if (it == sdt.services.end()) {
        continue;
      }
      auto service_type = it->second.serviceType(context_);
      if (service_type != 0x01 && service_type != 0x02 &&
          service_type != 0xA1 && service_type != 0xA2 &&
          service_type != 0xA5 && service_type != 0xA6) {
        continue;
      }
      ++pmt_count_;
      demux_.addPID(pid);
      MIRAKC_ARIB_DEBUG("Demux PMT#{:04X} for SID#{:04X} ServiceType({:02X})",
                        pid, sid, service_type);
    }
  }

  void HandlePmt(const ts::BinaryTable& table) {
    ts::PMT pmt(context_, table);

    if (!pmt.isValid()) {
      MIRAKC_ARIB_WARN("Broken PMT, skip");
      return;
    }

    auto it = pmt_pids_.find(pmt.service_id);
    if (it == pmt_pids_.end()) {
      MIRAKC_ARIB_WARN("PMT.SID#{} unmatched, skip", pmt.service_id);
      return;
    }
    if (it->second != table.sourcePID()) {
      MIRAKC_ARIB_WARN("PMT.PID#{} unmatched, skip", table.sourcePID());
      return;
    }

    MIRAKC_ARIB_DEBUG("PCR#{:04X} for SID#{:04X}", pmt.pcr_pid, pmt.service_id);
    pcr_pid_map_[pmt.service_id] = pmt.pcr_pid;
    if (pmt.pcr_pid != ts::PID_NULL) {
      pcr_pids_.insert(pmt.pcr_pid);
    }

    if (pcr_pid_map_.size() == pmt_count_) {
      demux_.addPID(ts::PID_TOT);
      MIRAKC_ARIB_DEBUG("Demux TDT/TOT");
    }
  }

  void HandleTdt(const ts::BinaryTable& table) {
    ts::TDT tdt(context_, table);

    if (!tdt.isValid()) {
      MIRAKC_ARIB_WARN("Broken TDT, skip");
      return;
    }

    HandleTime(tdt.utc_time);  // JST in ARIB
  }

  void HandleTot(const ts::BinaryTable& table) {
    ts::TOT tot(context_, table);

    if (!tot.isValid()) {
      MIRAKC_ARIB_WARN("Broken TOT, skip");
      return;
    }

    HandleTime(tot.utc_time);  // JST in ARIB
  }

  void HandleTime(const ts::Time& time) {
    MIRAKC_ARIB_INFO("Time: {}", time);
    time_ = time;

    started_ = true;
  }

  void ResetStates() {
    MIRAKC_ARIB_INFO("Reset states");

    demux_.removePID(ts::PID_TOT);
    for (const auto& pair : pmt_pids_) {
      demux_.removePID(pair.second);
    }
    demux_.removePID(ts::PID_SDT);

    pmt_pids_.clear();
    nid_ = 0;
    tsid_ = 0;
    pmt_count_ = 0;
    pcr_pid_map_.clear();
    pcr_pids_.clear();
    pcr_map_.clear();
    started_ = false;
    done_ = false;
  }

  const PcrSynchronizerOption option_;
  ts::DuckContext context_;
  ts::SectionDemux demux_;
  std::map<uint16_t, ts::PID> pmt_pids_;  // SID -> PID of PMT
  uint16_t nid_ = 0;
  uint16_t tsid_ = 0;
  size_t pmt_count_ = 0;
  std::map<uint16_t, ts::PID> pcr_pid_map_;  // SID -> PID of PCR
  std::set<ts::PID> pcr_pids_;
  std::map<ts::PID, int64_t> pcr_map_;  // PID of PCR -> PCR
  ts::Time time_;  // JST
  bool started_ = false;
  bool done_ = false;
};

}  // namespace
