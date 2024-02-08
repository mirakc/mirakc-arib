#pragma once

#include <string.h>
#include <array>
#include <cstdint>
#include "base.hh"

namespace {

class PacketStatsCollector final {
 public:
  PacketStatsCollector() {}
  ~PacketStatsCollector() {}

  void CollectPacketStats(const ts::TSPacket& packet) {
    auto pid = packet.getPID();
    auto last_cc = stats[pid].last_cc;
    auto cc = packet.getCC();
    auto has_payload = packet.hasPayload();
    auto tei = packet.getTEI();
    stats[pid].last_cc = cc;
    stats[pid].last_packet = packet;

    if (packet.getScrambling()) {
      ++scrambled_packets_;
    }

    auto duplicate_found = false;

    if (packet.getDiscontinuityIndicator() || packet.getPID() == ts::PID_NULL) {
      // do nothing
    } else if (tei) {
      ++error_packets_;
    } else if (last_cc != ts::INVALID_CC) {
      if (has_payload) {
        if (last_cc == cc) {
          if (stats[pid].last_packet != packet) {
            // non-duplicate packet
            ++dropped_packets_;
          } else {
            // duplicate packet
            duplicate_found = true;
          }
        } else {
          // regular packet
          uint8_t expectedCC = (last_cc + 1) & ts::CC_MASK;
          if (expectedCC != cc) {
            ++dropped_packets_;
          }
        }
      } else {
        // Continuity counter should not increment if packet has no payload
        if (last_cc != cc) {
          ++dropped_packets_;
        }
      }
    }

    if (duplicate_found) {
      // duplicate packet is only allowed once
      ++stats[pid].duplicate_packets;
      if (stats[pid].duplicate_packets > 1) {
        ++dropped_packets_;
      }
    } else {
      stats[pid].duplicate_packets = 0;
    }
  }

  void ResetPacketStats() {
    error_packets_ = 0;
    dropped_packets_ = 0;
    scrambled_packets_ = 0;
  }

  uint64_t GetErrorPackets() const {
    return error_packets_;
  }

  uint64_t GetDroppedPackets() const {
    return dropped_packets_;
  }

  uint64_t GetScrambledPackets() const {
    return scrambled_packets_;
  }

 private:
  struct PacketStat {
    uint8_t last_cc = ts::INVALID_CC;
    uint8_t duplicate_packets = 0;
    ts::TSPacket last_packet;
  };

  MIRAKC_ARIB_NON_COPYABLE(PacketStatsCollector);
  std::array<PacketStat, ts::PID_MAX> stats;
  uint64_t error_packets_ = 0;
  uint64_t dropped_packets_ = 0;
  uint64_t scrambled_packets_ = 0;
};

}  // namespace
