#pragma once

#include <memory>
#include <string>
#include <optional>
#include <unordered_set>

#include <tsduck/tsduck.h>

#include "base.hh"
#include "logging.hh"
#include "packet_sink.hh"
#include "packet_source.hh"

namespace {

struct ProgramFilter2Option final {
  uint16_t sid = 0;
  uint16_t eid = 0;
};

class ProgramFilter2 final : public PacketSink,
                             public ts::TableHandlerInterface {
 public:
  explicit ProgramFilter2(const ProgramFilter2Option& option)
      : option_(option),
        demux_(context_) {
    demux_.setTableHandler(this);
    demux_.addPID(ts::PID_PAT);
    demux_.addPID(ts::PID_EIT);
    MIRAKC_ARIB_DEBUG("Demux PAT EIT");
  }

  virtual ~ProgramFilter2() override {}

  void Connect(std::unique_ptr<PacketSink>&& sink) {
    sink_ = std::move(sink);
  }

  bool Start() override {
    if (!sink_) {
      MIRAKC_ARIB_ERROR("No sink has not been connected");
      return false;
    }

    sink_->Start();
    return true;
  }

  bool End() override {
    if (!sink_) {
      MIRAKC_ARIB_ERROR("No sink has not been connected");
      return false;
    }

    return sink_->End();
  }

  bool HandlePacket(const ts::TSPacket& packet) override {
    if (!sink_) {
      MIRAKC_ARIB_ERROR("No sink has not been connected");
      return false;
    }

    demux_.feedPacket(packet);

    switch (state_) {
      case kWaitReady:
        return WaitReady(packet);
      case kStreaming:
        return DoStreaming(packet);
      case kDone:
        MIRAKC_ARIB_INFO("Done");
        return false;
    }
    // never reach here
  }

 private:
  bool WaitReady(const ts::TSPacket& packet) {
    auto pid = packet.getPID();

    if (pid == ts::PID_PAT || pid == ts::PID_CAT || pid == ts::PID_NIT ||
        pid == ts::PID_SDT || pid == ts::PID_EIT || pid == ts::PID_RST ||
        pid == ts::PID_TOT || pid == ts::PID_BIT || pid == ts::PID_CDT) {
      return sink_->HandlePacket(packet);
    }

    // Drop other packets.
    return true;
  }

  bool DoStreaming(const ts::TSPacket& packet) {
    return sink_->HandlePacket(packet);
  }

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

    // The following condition is ensured in ServiceFilter.
    MIRAKC_ARIB_ASSERT(pat.pmts.find(option_.sid) != pat.pmts.end());

    auto new_pmt_pid = pat.pmts[option_.sid];

    if (pmt_pid_ != ts::PID_NULL) {
      demux_.removePID(pmt_pid_);
      pmt_pid_ = ts::PID_NULL;
    }

    pmt_pid_ = new_pmt_pid;
    demux_.addPID(pmt_pid_);
    MIRAKC_ARIB_DEBUG("Demux PMT#{:04X}", pmt_pid_);
  }

  void HandlePmt(const ts::BinaryTable& table) {
    ts::PMT pmt(context_, table);

    if (state_ != kWaitReady) {
      return;
    }

    if (!pmt.isValid()) {
      MIRAKC_ARIB_WARN("Broken PMT, skip");
      return;
    }

    if (pmt_version_.has_value()) {
      if (pmt_version_.value() != pmt.version) {
        MIRAKC_ARIB_INFO("PMT version has changed, start streaming");
        state_ = kStreaming;
      }
    } else {
      MIRAKC_ARIB_DEBUG("Wait for the next version of PMT");
      pmt_version_ = pmt.version;
    }
  }

  void HandleEit(const ts::BinaryTable& table) {
    ts::EIT eit(context_, table);

    if (!eit.isValid()) {
      MIRAKC_ARIB_WARN("Broken EIT, skip");
      return;
    }

    if (eit.service_id != option_.sid) {
      return;
    }

    if (eit.events.size() == 0) {
      MIRAKC_ARIB_ERROR("No event in EIT, stop");
      state_ = kDone;
      return;
    }

    const auto& present = eit.events[0];
    if (present.event_id == option_.eid) {
      if (state_ == kWaitReady) {
        MIRAKC_ARIB_INFO("Event#{:04X} has started, start streaming",
                         option_.eid);
        state_ = kStreaming;
      }
      return;
    }

    if (eit.events.size() < 2) {
      MIRAKC_ARIB_WARN("No following event in EIT");
      if (state_ == kStreaming) {
        return;
      }
      MIRAKC_ARIB_ERROR("Event#{:04X} might have been canceled", option_.eid);
      state_ = kDone;
      return;
    }

    const auto& following = eit.events[1];
    if (following.event_id == option_.eid) {
      MIRAKC_ARIB_DEBUG("Event#{:04X} will start soon", option_.eid);
      return;
    }

    if (state_ == kStreaming) {
      MIRAKC_ARIB_INFO("Event#{:04X} has ended", option_.eid);
      state_ = kDone;
      return;
    }

    MIRAKC_ARIB_ERROR("Event#{:04X} might have been canceled", option_.eid);
    state_ = kDone;
    return;
  }

  enum State {
    kWaitReady,
    kStreaming,
    kDone,
  };

  const ProgramFilter2Option option_;
  ts::DuckContext context_;
  ts::SectionDemux demux_;
  std::unique_ptr<PacketSink> sink_;
  State state_ = kWaitReady;
  ts::PID pmt_pid_ = ts::PID_NULL;
  std::optional<uint8_t> pmt_version_ = std::nullopt;

  MIRAKC_ARIB_NON_COPYABLE(ProgramFilter2);
};

}  // namespace
