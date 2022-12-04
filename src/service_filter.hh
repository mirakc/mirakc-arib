#pragma once

#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>

#include <tsduck/tsduck.h>

#include "base.hh"
#include "logging.hh"
#include "packet_sink.hh"
#include "packet_source.hh"
#include "tsduck_helper.hh"

#define MIRAKC_ARIB_SERVICE_FILTER_TRACE(...) \
  MIRAKC_ARIB_TRACE("service-filter: " __VA_ARGS__)
#define MIRAKC_ARIB_SERVICE_FILTER_DEBUG(...) \
  MIRAKC_ARIB_DEBUG("service-filter: " __VA_ARGS__)
#define MIRAKC_ARIB_SERVICE_FILTER_INFO(...) \
  MIRAKC_ARIB_INFO("service-filter: " __VA_ARGS__)
#define MIRAKC_ARIB_SERVICE_FILTER_WARN(...) \
  MIRAKC_ARIB_WARN("service-filter: " __VA_ARGS__)
#define MIRAKC_ARIB_SERVICE_FILTER_ERROR(...) \
  MIRAKC_ARIB_ERROR("service-filter: " __VA_ARGS__)

namespace {

struct ServiceFilterOption final {
  uint16_t sid = 0;
  std::optional<ts::Time> time_limit = std::nullopt;  // JST
};

class ServiceFilter final : public PacketSink,
                            public ts::TableHandlerInterface {
 public:
  explicit ServiceFilter(const ServiceFilterOption& option)
      : option_(option),
        demux_(context_),
        pat_packetizer_(ts::PID_PAT, ts::CyclingPacketizer::ALWAYS) {
    demux_.setTableHandler(this);
    demux_.addPID(ts::PID_PAT);
    MIRAKC_ARIB_SERVICE_FILTER_DEBUG("Demux PAT");
    demux_.addPID(ts::PID_CAT);
    MIRAKC_ARIB_SERVICE_FILTER_DEBUG("Demux CAT for detecting EMM PIDs");
    if (option_.time_limit.has_value()) {
      demux_.addPID(ts::PID_TOT);
      MIRAKC_ARIB_SERVICE_FILTER_DEBUG("Demux TDT/TOT for checking the time limit");
    }
  }

  virtual ~ServiceFilter() override {}

  void Connect(std::unique_ptr<PacketSink>&& sink) {
    sink_ = std::move(sink);
  }

  bool Start() override {
    if (!sink_) {
      MIRAKC_ARIB_SERVICE_FILTER_ERROR("No sink connected");
      return false;
    }

    sink_->Start();
    return true;
  }

  bool End() override {
    if (!sink_) {
      MIRAKC_ARIB_SERVICE_FILTER_ERROR("No sink connected");
      return false;
    }

    return sink_->End();
  }

  int GetExitCode() const override {
    if (!sink_) {
      return EXIT_FAILURE;
    }
    return sink_->GetExitCode();
  }

  bool HandlePacket(const ts::TSPacket& packet) override {
    if (!sink_) {
      MIRAKC_ARIB_SERVICE_FILTER_ERROR("No sink connected");
      return false;
    }

    demux_.feedPacket(packet);

    if (done_) {
      return false;
    }

    auto pid = packet.getPID();

    if (CheckFilterForDrop(pid)) {
      return true;
    }

    if (pid == ts::PID_PAT) {
      // Feed a modified PAT packet
      ts::TSPacket pat_packet;
      pat_packetizer_.getNextPacket(pat_packet);
      MIRAKC_ARIB_ASSERT(pat_packet.getPID() == ts::PID_PAT);
      return sink_->HandlePacket(pat_packet);
    }

    MIRAKC_ARIB_ASSERT(pid != ts::PID_NULL);
    return sink_->HandlePacket(packet);
  }

 private:
  bool CheckFilterForDrop(ts::PID pid) const {
    if (content_filter_.find(pid) != content_filter_.end()) {
      return false;
    }
    if (psi_filter_.find(pid) != psi_filter_.end()) {
      return false;
    }
    if (emm_filter_.find(pid) != emm_filter_.end()) {
      return false;
    }
    return true;
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
      MIRAKC_ARIB_SERVICE_FILTER_WARN(
          "PAT delivered with PID#{:04X}, skip", table.sourcePID());
      return;
    }

    ts::PAT pat(context_, table);

    if (!pat.isValid()) {
      MIRAKC_ARIB_SERVICE_FILTER_WARN("Broken PAT, skip");
      return;
    }

    if (pat.ts_id == 0) {
      MIRAKC_ARIB_SERVICE_FILTER_WARN("PAT for TSID#0000, skip");
      return;
    }

    if (pat.pmts.find(option_.sid) == pat.pmts.end()) {
      MIRAKC_ARIB_SERVICE_FILTER_ERROR("SID#{:04X} not found in PAT", option_.sid);
      done_ = true;
      return;
    }

    psi_filter_.clear();
    MIRAKC_ARIB_SERVICE_FILTER_DEBUG("Clear PSI/SI filter");

    auto new_pmt_pid = pat.pmts[option_.sid];

    if (pmt_pid_ != ts::PID_NULL) {
      MIRAKC_ARIB_SERVICE_FILTER_INFO(
          "PID of PMT has been changed: {:04X} -> {:04X}", pmt_pid_, new_pmt_pid);
      demux_.removePID(pmt_pid_);
      MIRAKC_ARIB_SERVICE_FILTER_DEBUG("Stop to demux PMT#{:04X}", pmt_pid_);
      pmt_pid_ = ts::PID_NULL;

      // content_filter_ is not cleared at this point.  This will be cleared
      // when a new PMT is detected.
    }

    pmt_pid_ = new_pmt_pid;
    demux_.addPID(pmt_pid_);
    MIRAKC_ARIB_SERVICE_FILTER_DEBUG("Demux PMT#{:04X}", pmt_pid_);

    // Remove other services from PAT.
    for (auto it = pat.pmts.begin(); it != pat.pmts.end(); ) {
      if (it->first != option_.sid) {
        it = pat.pmts.erase(it);
      } else {
        ++it;
      }
    }
    MIRAKC_ARIB_ASSERT(pat.pmts.size() == 1);
    MIRAKC_ARIB_ASSERT(pat.pmts.find(option_.sid) != pat.pmts.end());

    // Prepare packetizer for modified PAT.
    pat_packetizer_.removeAll();
    pat_packetizer_.addTable(context_, pat);

    psi_filter_.insert(ts::PID_PAT);
    psi_filter_.insert(ts::PID_CAT);
    psi_filter_.insert(ts::PID_NIT);
    psi_filter_.insert(ts::PID_SDT);
    psi_filter_.insert(ts::PID_EIT);
    psi_filter_.insert(ts::PID_RST);
    psi_filter_.insert(ts::PID_TOT);
    psi_filter_.insert(ts::PID_BIT);
    psi_filter_.insert(ts::PID_CDT);
    psi_filter_.insert(pmt_pid_);
    MIRAKC_ARIB_SERVICE_FILTER_DEBUG(
        "PSI/SI filter += PAT CAT NIT SDT EIT RST TDT/TOT BIT CDT PMT#{:04X}",
        pmt_pid_);
  }

  void HandleCat(const ts::BinaryTable& table) {
    ts::CAT cat(context_, table);

    if (!cat.isValid()) {
      MIRAKC_ARIB_SERVICE_FILTER_WARN("Broken CAT, skip");
      return;
    }

    emm_filter_.clear();
    MIRAKC_ARIB_SERVICE_FILTER_DEBUG("Clear EMM filter");

    auto i = cat.descs.search(ts::DID_CA);
    while (i < cat.descs.size()) {
      ts::CADescriptor desc(context_, *cat.descs[i]);
      emm_filter_.insert(desc.ca_pid);
      MIRAKC_ARIB_SERVICE_FILTER_DEBUG("EMM filter += EMM#{:04X}", desc.ca_pid);
      i = cat.descs.search(ts::DID_CA, i + 1);
    }
  }

  void HandlePmt(const ts::BinaryTable& table) {
    ts::PMT pmt(context_, table);

    if (!pmt.isValid()) {
      MIRAKC_ARIB_SERVICE_FILTER_WARN("Broken PMT, skip");
      return;
    }

    if (pmt.service_id != option_.sid) {
      MIRAKC_ARIB_SERVICE_FILTER_WARN("PMT.SID#{} unmatched, skip", pmt.service_id);
      return;
    }

    content_filter_.clear();
    MIRAKC_ARIB_SERVICE_FILTER_DEBUG("Clear content filter");

    content_filter_.insert(pmt.pcr_pid);
    MIRAKC_ARIB_SERVICE_FILTER_DEBUG("Content filter += PCR#{:04X}", pmt.pcr_pid);

    auto i = pmt.descs.search(ts::DID_CA);
    while (i < pmt.descs.size()) {
      ts::CADescriptor desc(context_, *pmt.descs[i]);
      content_filter_.insert(desc.ca_pid);
      MIRAKC_ARIB_SERVICE_FILTER_DEBUG("Content filter += ECM#{:04X}", desc.ca_pid);
      i = pmt.descs.search(ts::DID_CA, i + 1);
    }

    for (auto it = pmt.streams.begin(); it != pmt.streams.end(); ++it) {
      ts::PID pid = it->first;
      content_filter_.insert(pid);

      const auto& stream = it->second;
      if (stream.isVideo()) {
        MIRAKC_ARIB_SERVICE_FILTER_DEBUG("Content filter += PES/Video#{:04X}", pid);
      } else if (stream.isAudio()) {
        MIRAKC_ARIB_SERVICE_FILTER_DEBUG("Content filter += PES/Audio#{:04X}", pid);
      } else if (stream.isSubtitles()) {
        MIRAKC_ARIB_SERVICE_FILTER_DEBUG("Content filter += PES/Subtitle#{:04X}", pid);
      } else if (IsAribSubtitle(stream)) {
        MIRAKC_ARIB_SERVICE_FILTER_DEBUG("Content filter += PES/ARIB-Subtitle#{:04X}", pid);
      } else if (IsAribSuperimposedText(stream)) {
        MIRAKC_ARIB_SERVICE_FILTER_DEBUG(
            "Content filter += PES/ARIB-SuperimposedText#{:04X}", pid);
      } else {
        MIRAKC_ARIB_SERVICE_FILTER_DEBUG("Content filter += Other#{:04X}", pid);
      }
    }
  }

  void HandleTdt(const ts::BinaryTable& table) {
    ts::TDT tdt(context_, table);

    if (!tdt.isValid()) {
      MIRAKC_ARIB_SERVICE_FILTER_WARN("Broken TDT, skip");
      return;
    }

    CheckTimeLimit(tdt.utc_time);  // JST in ARIB
  }

  void HandleTot(const ts::BinaryTable& table) {
    ts::TOT tot(context_, table);

    if (!tot.isValid()) {
      MIRAKC_ARIB_SERVICE_FILTER_WARN("Broken TOT, skip");
      return;
    }

    CheckTimeLimit(tot.utc_time);  // JST in ARIB
  }

  void CheckTimeLimit(const ts::Time& jst_time) {
    if (jst_time < option_.time_limit.value()) {
      return;
    }

    done_ = true;
    MIRAKC_ARIB_SERVICE_FILTER_INFO("Over the time limit, stop streaming");
  }

  const ServiceFilterOption option_;
  ts::DuckContext context_;
  ts::SectionDemux demux_;
  ts::CyclingPacketizer pat_packetizer_;
  std::unique_ptr<PacketSink> sink_;
  std::unordered_set<ts::PID> psi_filter_;
  std::unordered_set<ts::PID> content_filter_;
  std::unordered_set<ts::PID> emm_filter_;
  ts::PID pmt_pid_ = ts::PID_NULL;
  bool done_ = false;

  MIRAKC_ARIB_NON_COPYABLE(ServiceFilter);
};

}  // namespace
