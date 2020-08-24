#pragma once

#include <memory>
#include <string>
#include <unordered_set>

#include <tsduck/tsduck.h>

#include "base.hh"
#include "logging.hh"
#include "packet_sink.hh"
#include "packet_source.hh"
#include "tsduck_helper.hh"

namespace {

struct ProgramFilterOption final {
  uint16_t sid = 0;
  uint16_t eid = 0;
  ts::PID clock_pid = ts::PID_NULL;
  int64_t clock_pcr = 0;
  ts::Time clock_time;  // JST
  ts::MilliSecond start_margin = 0;  // ms
  ts::MilliSecond end_margin = 0;  // ms
  bool pre_streaming = false;  // disabled
};

class ProgramFilter final : public PacketSink,
                            public ts::TableHandlerInterface {
 public:
  explicit ProgramFilter(const ProgramFilterOption& option)
      : option_(option),
        demux_(context_) {
    clock_pid_ = option_.clock_pid;
    clock_pcr_ = option_.clock_pcr;
    clock_time_ = option_.clock_time;
    clock_pcr_ready_ = true;
    clock_time_ready_ = true;
    MIRAKC_ARIB_DEBUG("Initial clock: PCR#{:04X}, {:011X} ({})",
                      clock_pid_, clock_pcr_, clock_time_);

    demux_.setTableHandler(this);
    demux_.addPID(ts::PID_PAT);
    demux_.addPID(ts::PID_EIT);
    demux_.addPID(ts::PID_TOT);
    MIRAKC_ARIB_DEBUG("Demux += PAT EIT TDT/TOT");
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
      case kStreaming:
        return DoStreaming(packet);
    }
    // never reach here
  }

 private:
  enum State {
    kWaitReady,
    kStreaming,
  };

  bool WaitReady(const ts::TSPacket& packet) {
    if (stop_) {
      MIRAKC_ARIB_WARN("Canceled");
      return false;
    }

    auto pid = packet.getPID();

    if (pid == ts::PID_PAT) {
      if (option_.pre_streaming) {
        return sink_->HandlePacket(packet);
      }
      // Save packets of the last PAT.
      if (packet.getPUSI()) {
        last_pat_packets_.clear();
      }
      last_pat_packets_.push_back(packet);
    } else if (pmt_pid_ != ts::PID_NULL && pid == pmt_pid_) {
      // Save packets of the last PMT.
      if (packet.getPUSI()) {
        last_pmt_packets_.clear();
      }
      last_pmt_packets_.push_back(packet);
    } else {
      // Drop other packets.
    }

    if (!pcr_pid_ready_ || !event_time_ready_) {
      return true;
    }

    if (pid != pcr_pid_) {
      return true;
    }

    if (!packet.hasPCR() || packet.getPCR() == ts::INVALID_PCR) {
      // Many PCR packets in a specific channel have no valid PCR...
      // See https://github.com/mirakc/mirakc-arib/issues/3
      MIRAKC_ARIB_TRACE("PCR#{:04X} has no valid PCR...", pid);
      return true;
    }

    auto pcr = packet.getPCR();

    if (NeedClockSync()) {
      UpdateClockPcr(pcr);
    }

    if (NeedClockSync()) {
      // wait for clock_time_ready_
      return true;
    }

    // We can implement the comparison below using operator>=() defined in the
    // Pcr class.  This coding style looks elegant, but requires more typing.
    if (ComparePcr(pcr, end_pcr_) >= 0) {  // pcr >= end_pcr_
      MIRAKC_ARIB_INFO("Reached the end PCR");
      return false;
    }

    if (ComparePcr(pcr, start_pcr_) < 0) {  // pcr < start_pcr_
      return true;
    }

    MIRAKC_ARIB_INFO("Reached the start PCR");

    // Send pending packets.
    if (!option_.pre_streaming) {
      MIRAKC_ARIB_ASSERT(!last_pat_packets_.empty());
      for (const auto& pat : last_pat_packets_) {
        if (!sink_->HandlePacket(pat)) {
          return false;
        }
      }
      last_pat_packets_.clear();
    }
    for (const auto& pmt : last_pmt_packets_) {
      if (!sink_->HandlePacket(pmt)) {
        return false;
      }
      last_pmt_packets_.clear();
    }

    state_ = kStreaming;
    return sink_->HandlePacket(packet);
  }

  bool DoStreaming(const ts::TSPacket& packet) {
    if (stop_) {
      MIRAKC_ARIB_INFO("Done");
      return false;
    }

    auto pid = packet.getPID();

    if (pid == pcr_pid_) {
      if (!packet.hasPCR() || packet.getPCR() == ts::INVALID_PCR) {
        // Many PCR packets in a specific channel have no valid PCR...
        // See https://github.com/mirakc/mirakc-arib/issues/3
        MIRAKC_ARIB_TRACE("PCR#{:04X} has no valid PCR...", pid);
        return sink_->HandlePacket(packet);
      }

      auto pcr = packet.getPCR();

      if (NeedClockSync()) {
        UpdateClockPcr(pcr);
      }

      if (NeedClockSync()) {
        // Postpone the stop until the clock synchronization is done.
        return sink_->HandlePacket(packet);
      }

      if (ComparePcr(pcr, end_pcr_) >= 0) {  // pcr >= end_pcr_
        MIRAKC_ARIB_INFO("Reached the end PCR");
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
    // Ignore a strange PAT delivered with PID#0012 around midnight at least on
    // BS-NTV and BS11 channels.
    //
    // This PAT has no PID of NIT and its ts_id is 0 like below:
    //
    //   * PAT, TID 0 (0x00), PID 18 (0x0012)
    //     Short section, total size: 179 bytes
    //     - Section 0:
    //       TS id:       0 (0x0000)
    //       Program: 19796 (0x4D54)  PID: 2672 (0x0A70)
    //       Program: 28192 (0x6E20)  PID: 6205 (0x183D)
    //       ...
    //
    if (table.sourcePID() != ts::PID_PAT) {
      MIRAKC_ARIB_WARN("PAT delivered with PID#{:04X}, skip",
                       table.sourcePID());
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

    // The following condition is ensured by ServiceFilter.
    MIRAKC_ARIB_ASSERT(pat.pmts.find(option_.sid) != pat.pmts.end());

    auto new_pmt_pid = pat.pmts[option_.sid];

    if (pmt_pid_ != ts::PID_NULL) {
      MIRAKC_ARIB_DEBUG("Demux -= PMT#{:04X}", pmt_pid_);
      demux_.removePID(pmt_pid_);
      pmt_pid_ = ts::PID_NULL;
    }

    pmt_pid_ = new_pmt_pid;
    demux_.addPID(pmt_pid_);
    MIRAKC_ARIB_DEBUG("Demux += PMT#{:04X}", pmt_pid_);
  }

  void HandlePmt(const ts::BinaryTable& table) {
    ts::PMT pmt(context_, table);

    if (!pmt.isValid()) {
      MIRAKC_ARIB_WARN("Broken PMT, skip");
      return;
    }

    if (pmt.service_id != option_.sid) {
      MIRAKC_ARIB_WARN("PMT.SID#{} unmatched, skip", pmt.service_id);
      return;
    }

    pcr_pid_ = pmt.pcr_pid;
    MIRAKC_ARIB_DEBUG("PCR#{:04X}", pcr_pid_);

    pcr_pid_ready_ = true;

    if (clock_pid_ != pcr_pid_) {
      MIRAKC_ARIB_WARN(
          "PID of PCR has been changed: {:04X} -> {:04X}, need resync",
          clock_pid_, pcr_pid_);
      clock_pid_ = pcr_pid_;
      clock_pcr_ready_ = false;
      clock_time_ready_ = false;
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
      stop_ = true;
      return;
    }

    const auto& present = eit.events[0];
    if (present.event_id == option_.eid) {
      MIRAKC_ARIB_DEBUG("Event#{:04X} has started", option_.eid);
      UpdateEventTime(present);
      return;
    }

    if (eit.events.size() < 2) {
      MIRAKC_ARIB_WARN("No following event in EIT");
      if (state_ == kStreaming) {
        // Continue streaming until PCR reaches `end_pcr_`.
        return;
      }
      MIRAKC_ARIB_ERROR("Event#{:04X} might have been canceled", option_.eid);
      stop_ = true;
      return;
    }

    const auto& following = eit.events[1];
    if (following.event_id == option_.eid) {
      MIRAKC_ARIB_DEBUG("Event#{:04X} will start soon", option_.eid);
      UpdateEventTime(following);
      return;
    }

    // The specified event is not included in EIT.

    if (state_ == kStreaming) {
      // Continue streaming until PCR reaches `end_pcr_`.
      return;
    }

    MIRAKC_ARIB_ERROR("Event#{:04X} might have been canceled", option_.eid);
    stop_ = true;
    return;
  }

  void HandleTdt(const ts::BinaryTable& table) {
    ts::TDT tdt(context_, table);

    if (!tdt.isValid()) {
      MIRAKC_ARIB_WARN("Broken TDT, skip");
      return;
    }

    if (clock_time_ready_) {
      return;
    }

    UpdateClockTime(tdt.utc_time);  // JST in ARIB
  }

  void HandleTot(const ts::BinaryTable& table) {
    ts::TOT tot(context_, table);

    if (!tot.isValid()) {
      MIRAKC_ARIB_WARN("Broken TOT, skip");
      return;
    }

    if (clock_time_ready_) {
      return;
    }

    UpdateClockTime(tot.utc_time);  // JST in ARIB
  }

  void UpdateEventTime(const ts::EIT::Event& event) {
    auto duration = event.duration * ts::MilliSecPerSec + option_.end_margin;

    event_start_time_ = event.start_time - option_.start_margin;
    event_end_time_ = event.start_time + duration;
    MIRAKC_ARIB_INFO("Updated event time: ({}) .. ({})",
                     event_start_time_, event_end_time_);

    event_time_ready_ = true;

    if (clock_time_ready_ && clock_pcr_ready_) {
      UpdatePcrRange();
    }
  }

  void UpdateClockPcr(int64_t pcr) {
    MIRAKC_ARIB_ASSERT(NeedClockSync());

    clock_pcr_ = pcr;
    MIRAKC_ARIB_TRACE("Updated clock PCR: {:011X}", pcr);

    clock_pcr_ready_ = true;

    if (event_time_ready_ && clock_time_ready_) {
      UpdatePcrRange();
    }
  }

  void UpdateClockTime(const ts::Time& time) {
    MIRAKC_ARIB_ASSERT(!clock_time_ready_);

    clock_time_ = time;
    MIRAKC_ARIB_TRACE("Updated clock time: {}", time);

    clock_time_ready_ = true;

    if (event_time_ready_ && clock_pcr_ready_) {
      UpdatePcrRange();
    }
  }

  bool NeedClockSync() const {
    return !clock_time_ready_ || !clock_pcr_ready_;
  }

  void UpdatePcrRange() {
    MIRAKC_ARIB_ASSERT(event_time_ready_);
    MIRAKC_ARIB_ASSERT(clock_pcr_ready_);
    MIRAKC_ARIB_ASSERT(clock_time_ready_);

    start_pcr_ = ConvertTimeToPcr(event_start_time_);
    end_pcr_ = ConvertTimeToPcr(event_end_time_);
    MIRAKC_ARIB_INFO("Updated PCR range: {:011X} ({}) .. {:011X} ({})",
                     start_pcr_, event_start_time_, end_pcr_, event_end_time_);
  }

  int64_t ConvertTimeToPcr(const ts::Time& time) {
    MIRAKC_ARIB_ASSERT(clock_pcr_ready_);
    MIRAKC_ARIB_ASSERT(clock_time_ready_);
    MIRAKC_ARIB_ASSERT(IsValidPcr(clock_pcr_));

    auto ms = time - clock_time_;  // may be a negative value
    auto pcr = clock_pcr_ + ms * kPcrTicksPerMs;
    while (pcr < 0) {
      pcr += kPcrUpperBound;
    }
    MIRAKC_ARIB_ASSERT(pcr >= 0);
    return pcr % kPcrUpperBound;
  }

  const ProgramFilterOption option_;
  ts::DuckContext context_;
  ts::SectionDemux demux_;
  std::unique_ptr<PacketSink> sink_;
  State state_ = kWaitReady;
  ts::TSPacketVector last_pat_packets_;
  ts::TSPacketVector last_pmt_packets_;
  ts::PID clock_pid_ = ts::PID_NULL;
  int64_t clock_pcr_ = 0;
  ts::Time clock_time_;
  ts::PID pmt_pid_ = ts::PID_NULL;
  ts::PID pcr_pid_ = ts::PID_NULL;
  ts::Time event_start_time_;
  ts::Time event_end_time_;
  int64_t start_pcr_ = 0;
  int64_t end_pcr_ = 0;
  bool pcr_pid_ready_ = false;
  bool event_time_ready_ = false;
  bool clock_pcr_ready_ = false;
  bool clock_time_ready_ = false;
  bool stop_ = false;

  MIRAKC_ARIB_NON_COPYABLE(ProgramFilter);
};

}  // namespace
