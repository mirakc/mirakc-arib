// SPDX-License-Identifier: GPL-2.0-or-later

// mirakc-arib
// Copyright (C) 2019 masnagam
//
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation; either version 2 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
// without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See
// the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with this program; if
// not, write to the Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
// 02110-1301, USA.

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

struct StartSeekerOption final {
  uint16_t sid = 0;
  ts::MilliSecond max_duration = 0;
  uint32_t max_packets = 0;
};

class StartSeeker final : public PacketSink, public ts::TableHandlerInterface {
 public:
  explicit StartSeeker(const StartSeekerOption& option) : option_(option), demux_(context_) {
    demux_.setTableHandler(this);
    demux_.addPID(ts::PID_PAT);
    MIRAKC_ARIB_DEBUG("Demux += PAT");
  }

  virtual ~StartSeeker() override {}

  void Connect(std::unique_ptr<PacketSink>&& sink) {
    sink_ = std::move(sink);
  }

  bool Start() override {
    MIRAKC_ARIB_ASSERT(sink_ != nullptr);
    return sink_->Start();
  }

  void End() override {
    MIRAKC_ARIB_ASSERT(sink_ != nullptr);
    (void)SendPackets();  // ignore the error
    sink_->End();
  }

  int GetExitCode() const override {
    MIRAKC_ARIB_ASSERT(sink_ != nullptr);
    return sink_->GetExitCode();
  }

  bool HandlePacket(const ts::TSPacket& packet) override {
    MIRAKC_ARIB_ASSERT(sink_ != nullptr);

    demux_.feedPacket(packet);

    switch (state_) {
      case kSeek:
        return Seek(packet);
      case kStreaming:
        return DoStreaming(packet);
    }

    MIRAKC_ARIB_NEVER_REACH("state_ was broken");
    return false;
  }

 private:
  enum State {
    kSeek,
    kStreaming,
  };

  bool Seek(const ts::TSPacket& packet) {
    auto pid = packet.getPID();

    packets_.push_back(packet);

    if (transition_index_ > 0) {
      MIRAKC_ARIB_INFO("Found transition point, start streaming");
      SendPacket(pat_index_);
      SendPackets(transition_index_);
      state_ = kStreaming;
      return true;
    }

    if (option_.max_packets != 0) {
      if (option_.max_packets == packets_.size()) {
        MIRAKC_ARIB_INFO("The number of packets reached the limit, start streaming");
        SendPackets();
        state_ = kStreaming;
        return true;
      }
      MIRAKC_ARIB_ASSERT(packets_.size() < option_.max_packets);
    } else {
      // no limit on packets_.size()
    }

    if (pcr_pid_ == ts::PID_NULL || pcr_pid_ != pid) {
      return true;
    }

    if (!packet.hasPCR() || packet.getPCR() == ts::INVALID_PCR) {
      // Many PCR packets in a specific channel have no valid PCR...
      // See https://github.com/mirakc/mirakc-arib/issues/3
      MIRAKC_ARIB_TRACE("PCR#{:04X} has no valid PCR...", pid);
      return true;
    }

    auto pcr = packet.getPCR();

    if (end_pcr_ < 0) {
      end_pcr_ = (pcr + option_.max_duration * kPcrTicksPerMs) % kPcrUpperBound;
      MIRAKC_ARIB_DEBUG("End PCR: {:010d}+{:03d}", end_pcr_ / 300, end_pcr_ % 300);
      return true;
    }

    // We can implement the comparison below using operator>=() defined in the
    // Pcr class.  This coding style looks elegant, but requires more typing.
    if (ComparePcr(pcr, end_pcr_) < 0) {  // pcr < end_pcr_
      return true;
    }

    MIRAKC_ARIB_INFO("The duration reached the limit, start streaming");
    SendPackets();
    state_ = kStreaming;
    return true;
  }

  bool SendPacket(size_t index) {
    return sink_->HandlePacket(packets_[index]);
  }

  bool SendPackets(size_t index = 0) {
    bool ok = true;
    for (auto i = index; i < packets_.size(); ++i) {
      ok = sink_->HandlePacket(packets_[i]);
      if (!ok) {
        break;
      }
    }
    packets_.clear();
    return ok;
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
      default:
        break;
    }
  }

  void HandlePat(const ts::BinaryTable& table) {
    if (table.sourcePID() != ts::PID_PAT) {
      MIRAKC_ARIB_WARN("PAT delivered with PID#{:04X}, skip", table.sourcePID());
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

    // We assume that the PAT consists of a single packet.
    pat_index_ = table.getFirstTSPacketIndex();
    MIRAKC_ARIB_DEBUG("PAT packet#{}", pat_index_);
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

    std::unordered_set<ts::PID> video_pids;
    std::unordered_set<ts::PID> audio_pids;
    for (const auto& [pid, stream] : pmt.streams) {
      if (stream.isVideo()) {
        MIRAKC_ARIB_DEBUG("Found video#{:04X}", pid);
        video_pids.insert(pid);
      }
      if (stream.isAudio()) {
        MIRAKC_ARIB_DEBUG("Found audio#{:04X}", pid);
        audio_pids.insert(pid);
      }
    }

    bool changed = false;
    if (!video_pids_.empty() && video_pids_ != video_pids) {
      changed = true;
      MIRAKC_ARIB_DEBUG("video streams change");
    }
    if (!audio_pids_.empty() && audio_pids_ != audio_pids) {
      changed = true;
      MIRAKC_ARIB_DEBUG("audio streams change");
    }

    video_pids_ = video_pids;
    audio_pids_ = audio_pids;

    if (changed) {
      transition_index_ = table.getFirstTSPacketIndex();
      MIRAKC_ARIB_DEBUG("The content changes at packet#{}", transition_index_);
      MIRAKC_ARIB_DEBUG("Demux -= PAT PMT#{:04X}", pmt_pid_);
      demux_.removePID(pmt_pid_);
      demux_.removePID(ts::PID_PAT);
      pmt_pid_ = ts::PID_NULL;
    }
  }

  const StartSeekerOption option_;
  ts::DuckContext context_;
  ts::SectionDemux demux_;
  std::unique_ptr<PacketSink> sink_;
  State state_ = kSeek;
  ts::TSPacketVector packets_;
  ts::PID pmt_pid_ = ts::PID_NULL;
  ts::PID pcr_pid_ = ts::PID_NULL;
  std::unordered_set<ts::PID> video_pids_;
  std::unordered_set<ts::PID> audio_pids_;
  int64_t end_pcr_ = -1;
  size_t transition_index_ = 0;
  size_t pat_index_ = 0;  // index of a PAT packet

  MIRAKC_ARIB_NON_COPYABLE(StartSeeker);
};

}  // namespace
