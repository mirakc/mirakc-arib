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

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include <tsduck/tsduck.h>

#include "base.hh"
#include "tsduck_helper.hh"

namespace {

enum class PacketCategory : uint8_t {
  kVideo,
  kAudio,
  kSubtitle,
  kPmt,
  kNumCategories,
};

constexpr size_t kNumPacketCategories = static_cast<size_t>(PacketCategory::kNumCategories);

constexpr std::array<const char*, kNumPacketCategories> kPacketCategoryNames = {
    "video",
    "audio",
    "subtitle",
    "pmt",
};

class PacketStatsCollector final {
 public:
  PacketStatsCollector() = default;

  // Changes the PMT PID tracked for packet statistics after a PAT update.
  void SetPmtPid(ts::PID pid) {
    if (pmt_pid_ == pid) {
      return;
    }

    const auto old_pid = pmt_pid_;
    pmt_pid_ = pid;

    if (old_pid.has_value()) {
      RemoveCategory(*old_pid);
    }

    SetCategory(pid, PacketCategory::kPmt);
  }

  // Classifies each PID carried by `pmt` into a PacketCategory.
  void UpdatePidCategories(const ts::PMT& pmt) {
    RebuildPidCategories(pmt);
  }

  void CollectPacketStats(const ts::TSPacket& packet) {
    // Packets with TEI set may have invalid PID or CC values.
    // Count them as errors and return early.
    if (packet.getTEI()) {
      ++error_packets_;
      return;
    }

    auto pid = packet.getPID();
    if (pid == ts::PID_NULL) {
      return;
    }

    if (packet.getScrambling() != 0) {
      ++scrambled_packets_;
    }

    auto& stat = stats_[pid];
    if (!stat.category.has_value()) {
      return;
    }

    auto cc = packet.getCC();

    // Although MPEG-2 TS permits a packet to be transmitted twice, we intentionally do not retain
    // the previous packet to detect duplicates.
    //
    // We are not aware of any reports of duplicate packets in Japanese digital broadcasts, and
    // we see little value in handling them.  If a duplicate payload packet is ever transmitted,
    // its repeated CC is counted as 15 dropped packets.
    if (stat.last_cc != ts::INVALID_CC && !packet.getDiscontinuityIndicator()) {
      // The continuity counter increments only when the packet has a payload.
      // Keep the expected CC unchanged for packets without payload.
      uint8_t expected_cc =
          packet.hasPayload() ? ((stat.last_cc + 1) & ts::CC_MASK) : stat.last_cc;

      uint8_t missed = (cc - expected_cc) & ts::CC_MASK;
      if (missed != 0) {
        dropped_packets_[static_cast<size_t>(*stat.category)] += missed;
      }
    }

    stat.last_cc = cc;
  }

  void ResetPacketStats() {
    error_packets_ = 0;
    scrambled_packets_ = 0;
    dropped_packets_.fill(0);
  }

  uint64_t GetErrorPackets() const {
    return error_packets_;
  }

  uint64_t GetScrambledPackets() const {
    return scrambled_packets_;
  }

  uint64_t GetDroppedPackets(PacketCategory category) const {
    MIRAKC_ARIB_ASSERT(static_cast<size_t>(category) < kNumPacketCategories);
    return dropped_packets_[static_cast<size_t>(category)];
  }

 private:
  struct PacketStat {
    uint8_t last_cc = ts::INVALID_CC;
    std::optional<PacketCategory> category;
  };

  static void ClearCategory(PacketStat& stat) {
    stat.category.reset();
    stat.last_cc = ts::INVALID_CC;
  }

  void SetCategory(ts::PID pid, PacketCategory category) {
    auto& stat = stats_[pid];
    if (!stat.category.has_value()) {
      categorized_pids_.push_back(pid);
    }
    stat.category = category;
  }

  void RemoveCategory(ts::PID pid) {
    ClearCategory(stats_[pid]);
    const auto it = std::find(categorized_pids_.begin(), categorized_pids_.end(), pid);
    if (it != categorized_pids_.end()) {
      categorized_pids_.erase(it);
    }
  }

  // Rebuilds packet-statistics PID categories from the tracked PMT PID and PMT.
  void RebuildPidCategories(const ts::PMT& pmt) {
    std::vector<ts::PID> previous_pids;
    previous_pids.swap(categorized_pids_);

    // Clear the previous category assignments before rebuilding them from the current PMT.
    // Do not clear the last_cc of existing stats here, as it may still be used by the current PMT.
    for (auto pid : previous_pids) {
      stats_[pid].category.reset();
    }

    if (pmt_pid_.has_value()) {
      SetCategory(*pmt_pid_, PacketCategory::kPmt);
    }

    RebuildStreamPidCategories(pmt);

    // Clear both the category and saved CC for PIDs that are no longer tracked.
    for (auto pid : previous_pids) {
      if (!stats_[pid].category.has_value()) {
        ClearCategory(stats_[pid]);
      }
    }
  }

  // Assigns categories to the PIDs referenced by `pmt`.
  void RebuildStreamPidCategories(const ts::PMT& pmt) {
    for (const auto& [pid, stream] : pmt.streams) {
      // Do not classify the PMT PID as other categories.
      if (pmt_pid_.has_value() && pid == *pmt_pid_) {
        continue;
      }

      if (stream.isVideo()) {
        SetCategory(pid, PacketCategory::kVideo);
      } else if (stream.isAudio()) {
        SetCategory(pid, PacketCategory::kAudio);
      } else if (stream.isSubtitles() || IsAribSubtitle(stream) ||
          IsAribSuperimposedText(stream)) {
        SetCategory(pid, PacketCategory::kSubtitle);
      }
    }
  }

  MIRAKC_ARIB_NON_COPYABLE(PacketStatsCollector);
  std::array<PacketStat, ts::PID_MAX> stats_;
  std::vector<ts::PID> categorized_pids_;
  std::optional<ts::PID> pmt_pid_;
  uint64_t error_packets_ = 0;
  uint64_t scrambled_packets_ = 0;
  std::array<uint64_t, kNumPacketCategories> dropped_packets_{};
};

}  // namespace
