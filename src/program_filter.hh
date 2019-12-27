#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_set>

#include <tsduck/tsduck.h>

#include "base.hh"
#include "logging.hh"
#include "packet_sink.hh"
#include "packet_source.hh"

namespace {

struct ProgramFilterOption final {
  uint16_t sid = 0;
  uint16_t eid = 0;
  int64_t clock_pcr = 0;
  ts::Time clock_time;  // JST
  ts::MilliSecond start_margin = 0;  // ms
  ts::MilliSecond end_margin = 0;  // ms
};

class ProgramFilter final : public PacketSink,
                            public ts::TableHandlerInterface {
 public:
  explicit ProgramFilter(const ProgramFilterOption& option)
      : option_(option),
        demux_(context_) {
    demux_.setTableHandler(this);
    demux_.addPID(ts::PID_PAT);
    demux_.addPID(ts::PID_EIT);
    MIRAKC_ARIB_DEBUG("Demux PAT EIT");
  }

  virtual ~ProgramFilter() override {}

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
      case kSeekPat:
        return SeekPat(packet);
      case kStreaming:
        return DoStreaming(packet);
      case kDone:
        MIRAKC_ARIB_INFO("Done");
        return false;
    }
    // never reach here
  }

 private:
  // Compares two PCR values taking into account the PCR wrap around.
  //
  // Assumed that the real interval time between the PCR values is less than
  // half of kPcrUpperBound.
  int64_t ComparePcr(int64_t lhs, int64_t rhs) {
    auto a = lhs - rhs;
    auto b = lhs - (kPcrUpperBound + rhs);
    if (std::abs(a) < std::abs(b)) {
      return a;
    }
    return b;
  }

  bool WaitReady(const ts::TSPacket& packet) {
    if (!pcr_pid_ready_ || !pcr_range_ready_) {
      return true;
    }

    auto pid = packet.getPID();

    if (pid != pcr_pid_) {
      return true;
    }

    if (!packet.hasPCR()) {
      MIRAKC_ARIB_ERROR("No PCR value in PCR#{:04X}", pid);
      state_ = kDone;
      return false;
    }

    auto pcr = packet.getPCR();

    // We can implement the comparison below using operator>=() defined in the
    // Pcr class.  This coding style looks elegant, but requires more typing.
    if (ComparePcr(pcr, end_pcr_) >= 0) {  // pcr >= end_pcr_
      MIRAKC_ARIB_INFO("Reached the end PCR");
      state_ = kDone;
      return false;
    }

    if (ComparePcr(pcr, start_pcr_) >= 0) {  // pcr >= start_pcr_
      MIRAKC_ARIB_INFO("Reached the start PCR");
      state_ = kSeekPat;
      return true;
    }

    return true;
  }

  bool SeekPat(const ts::TSPacket& packet) {
    auto pid = packet.getPID();

    if (pid == pcr_pid_) {
      if (!packet.hasPCR()) {
        MIRAKC_ARIB_ERROR("No PCR value in PCR#{:04X}", pid);
        state_ = kDone;
        return false;
      }

      auto pcr = packet.getPCR();

      if (pcr >= end_pcr_) {
        MIRAKC_ARIB_INFO("Reached the end PCR");
        state_ = kDone;
        return false;
      }

      return true;
    }

    if (pid != ts::PID_PAT) {
      return true;
    }

    MIRAKC_ARIB_INFO("Start streaming");
    state_ = kStreaming;

    return sink_->HandlePacket(packet);
  }

  bool DoStreaming(const ts::TSPacket& packet) {
    auto pid = packet.getPID();

    if (pid == pcr_pid_) {
      if (!packet.hasPCR()) {
        MIRAKC_ARIB_ERROR("No PCR value in PCR#{:04X}", pid);
        state_ = kDone;
        return false;
      }

      auto pcr = packet.getPCR();

      if (pcr >= end_pcr_) {
        MIRAKC_ARIB_INFO("Reached the end PCR");
        state_ = kDone;
        return false;
      }
    }

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

    if (!pmt.isValid()) {
      MIRAKC_ARIB_WARN("Broken PMT, skip");
      return;
    }

    pcr_pid_ = pmt.pcr_pid;
    MIRAKC_ARIB_DEBUG("PCR#{:04X}", pcr_pid_);

    pcr_pid_ready_ = true;
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
      MIRAKC_ARIB_ERROR("No event in EIT");
      state_ = kDone;
      return;
    }

    const auto& present = eit.events[0];
    if (present.event_id == option_.eid) {
      MIRAKC_ARIB_DEBUG("Event#{:04X} have started", option_.eid);
      UpdatePcrRange(present);
      return;
    }

    if (eit.events.size() < 2) {
      MIRAKC_ARIB_WARN("No following event in EIT");
      if (state_ == kStreaming) {
        // Continue streaming until PCR reaches `end_pcr_`.
        return;
      }
      MIRAKC_ARIB_ERROR("Event#{:04X} might have been canceled", option_.eid);
      state_ = kDone;
      return;
    }

    const auto& following = eit.events[1];
    if (following.event_id == option_.eid) {
      MIRAKC_ARIB_DEBUG("Event#{:04X} will start soon", option_.eid);
      UpdatePcrRange(following);
      return;
    }

    // The specified event is not included in EIT.

    if (state_ == kStreaming) {
      // Continue streaming until PCR reaches `end_pcr_`.
      return;
    }

    MIRAKC_ARIB_ERROR("Event#{:04X} might have been canceled", option_.eid);
    state_ = kDone;
    return;
  }

  void UpdatePcrRange(const ts::EIT::Event& event) {
    auto start_time = event.start_time - option_.start_margin;
    auto duration = event.duration * ts::MilliSecPerSec + option_.end_margin;
    auto end_time = event.start_time + duration;
    start_pcr_ = ConvertTimeToPcr(start_time);
    end_pcr_ = ConvertTimeToPcr(end_time);
    MIRAKC_ARIB_INFO("Updated PCR range: {:011X} ({}) .. {:011X} ({})",
                     start_pcr_, start_time, end_pcr_, end_time);
    pcr_range_ready_ = true;
  }

  int64_t ConvertTimeToPcr(const ts::Time& time) {
    auto ms = time - option_.clock_time;  // may be a negative value
    auto pcr = option_.clock_pcr + ms * kPcrTicksPerMs;
    while (pcr < 0) {
      pcr += kPcrUpperBound;
    }
    return pcr % kPcrUpperBound;
  }

  enum State {
    kWaitReady,
    kSeekPat,
    kStreaming,
    kDone,
  };

  const ProgramFilterOption option_;
  ts::DuckContext context_;
  ts::SectionDemux demux_;
  std::unique_ptr<PacketSink> sink_;
  State state_ = kWaitReady;
  ts::PID pmt_pid_ = ts::PID_NULL;
  ts::PID pcr_pid_ = ts::PID_NULL;
  int64_t start_pcr_ = 0;
  int64_t end_pcr_ = 0;
  bool pcr_pid_ready_ = false;
  bool pcr_range_ready_ = false;

  MIRAKC_ARIB_NON_COPYABLE(ProgramFilter);
};

}  // namespace
