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

#include <cstring>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tsduck/tsduck.h>

#include "packet_stats_collector.hh"

namespace {

constexpr ts::PID kPid = 0x0100;

ts::TSPacket MakePacket(
    ts::PID pid, uint8_t cc, uint8_t data = 0xFF, bool tei = false, bool discontinuity = false) {
  ts::TSPacket packet;
  packet.init(pid, cc, data);
  packet.setTEI(tei);
  if (discontinuity) {
    packet.setDiscontinuityIndicator(/*shift_payload=*/true);
  }
  return packet;
}

// Builds a packet containing only an adaptation field, i.e. hasPayload() == false.
ts::TSPacket MakeNoPayloadPacket(ts::PID pid, uint8_t cc) {
  ts::TSPacket packet;
  packet.b[0] = 0x47;
  packet.b[1] = static_cast<uint8_t>((pid >> 8) & 0x1F);
  packet.b[2] = static_cast<uint8_t>(pid);
  packet.b[3] = 0x20 | (cc & ts::CC_MASK);  // 0x20: adaptation field only, no payload
  packet.b[4] = ts::PKT_SIZE - 5;           // Number of bytes after packet.b[4]
  packet.b[5] = 0x00;  // 0x00: adaptation field flags (no discontinuity, no PCR, etc.)
  ::memset(packet.b + 6, 0xFF, ts::PKT_SIZE - 6);
  return packet;
}

// Configures kPid via the selected service's PMT so that its dropped packets are counted in the
// kVideo category.
void ConfigureVideoPid(PacketStatsCollector& collector) {
  ts::PMT pmt;
  pmt.streams.try_emplace(kPid, &pmt, ts::ST_MPEG2_VIDEO);
  collector.UpdatePidCategories(pmt);
}

void ExpectNoDroppedPackets(const PacketStatsCollector& collector) {
  for (size_t i = 0; i < kNumPacketCategories; ++i) {
    EXPECT_EQ(0, collector.GetDroppedPackets(static_cast<PacketCategory>(i)));
  }
}

}  // namespace

// -----------------------------------------------------------------------------------------------
// TEI / error-packet handling
// -----------------------------------------------------------------------------------------------

TEST(PacketStatsCollectorTest, TeiPacketIsCountedAsErrorAndExcludedFromContinuity) {
  PacketStatsCollector collector;
  ConfigureVideoPid(collector);
  collector.CollectPacketStats(MakePacket(kPid, 0));
  // A packet with TEI set may have a corrupted PID/CC, so it must not become the baseline for
  // the next continuity check.
  ts::TSPacket tei_packet = MakePacket(kPid, 5, 0xFF, /*tei=*/true);
  tei_packet.setScrambling(1);
  collector.CollectPacketStats(tei_packet);

  EXPECT_EQ(1, collector.GetErrorPackets());
  // The collector should not count a packet with TEI set as scrambled even if the scrambling bits
  // happen to be set.
  EXPECT_EQ(0, collector.GetScrambledPackets());

  // The next regular packet must still be checked against the first packet's CC 0, not against
  // the TEI packet's CC 5.
  collector.CollectPacketStats(MakePacket(kPid, 1));
  EXPECT_EQ(0, collector.GetDroppedPackets(PacketCategory::kVideo));
}

TEST(PacketStatsCollectorTest, TeiWithDiscontinuityIndicatorIsStillCountedAsError) {
  PacketStatsCollector collector;
  collector.CollectPacketStats(MakePacket(kPid, 0, 0xFF, /*tei=*/true, /*discontinuity=*/true));

  EXPECT_EQ(1, collector.GetErrorPackets());
}

TEST(PacketStatsCollectorTest, TeiPacketIsCountedEvenWhenPidIsIgnored) {
  PacketStatsCollector collector;
  ts::TSPacket packet = MakePacket(kPid, 0, 0xFF, /*tei=*/true);
  packet.setScrambling(1);
  collector.CollectPacketStats(packet);

  EXPECT_EQ(1, collector.GetErrorPackets());
  EXPECT_EQ(0, collector.GetScrambledPackets());
  ExpectNoDroppedPackets(collector);
}

// -----------------------------------------------------------------------------------------------
// Scrambled-packet handling
// -----------------------------------------------------------------------------------------------

TEST(PacketStatsCollectorTest, ScrambledPacketIsCounted) {
  PacketStatsCollector collector;
  ConfigureVideoPid(collector);
  ts::TSPacket packet = MakePacket(kPid, 0);
  packet.setScrambling(1);
  collector.CollectPacketStats(packet);

  EXPECT_EQ(1, collector.GetScrambledPackets());
}

TEST(PacketStatsCollectorTest, ScrambledPacketIsCountedEvenWhenPidIsUnclassified) {
  PacketStatsCollector collector;
  ts::TSPacket packet = MakePacket(kPid, 0);
  packet.setScrambling(1);
  collector.CollectPacketStats(packet);

  EXPECT_EQ(1, collector.GetScrambledPackets());
}

TEST(PacketStatsCollectorTest, ScrambledNullPacketIsNotCounted) {
  PacketStatsCollector collector;
  ts::TSPacket packet = MakePacket(ts::PID_NULL, 0);
  packet.setScrambling(1);
  collector.CollectPacketStats(packet);

  EXPECT_EQ(0, collector.GetScrambledPackets());
}

// -----------------------------------------------------------------------------------------------
// PID classification via PMT
// -----------------------------------------------------------------------------------------------

TEST(PacketStatsCollectorTest, PmtPidIsClassifiedAsPmt) {
  constexpr ts::PID kPmtPid = 0x0030;
  PacketStatsCollector collector;
  collector.SetPmtPid(kPmtPid);
  collector.CollectPacketStats(MakePacket(kPmtPid, 0));
  collector.CollectPacketStats(MakePacket(kPmtPid, 5));

  EXPECT_EQ(4, collector.GetDroppedPackets(PacketCategory::kPmt));
}

TEST(PacketStatsCollectorTest, UnclassifiedPidIsIgnored) {
  PacketStatsCollector collector;
  collector.CollectPacketStats(MakePacket(kPid, 0));
  collector.CollectPacketStats(MakePacket(kPid, 5));

  ExpectNoDroppedPackets(collector);
}

TEST(PacketStatsCollectorTest, PmtClassifiesStreamsByCategory) {
  constexpr ts::PID kVideoPid = 0x0101;
  constexpr ts::PID kAudioPid = 0x0102;
  constexpr ts::PID kSubtitlePid = 0x0103;

  ts::DuckContext context;
  ts::PMT pmt;
  pmt.streams.try_emplace(kVideoPid, &pmt, ts::ST_MPEG2_VIDEO);
  pmt.streams.try_emplace(kAudioPid, &pmt, ts::ST_MPEG1_AUDIO);
  pmt.streams.try_emplace(kSubtitlePid, &pmt, ts::ST_PES_PRIV);
  pmt.streams[kSubtitlePid].descs.add(context, ts::StreamIdentifierDescriptor(0x30));

  PacketStatsCollector collector;
  collector.UpdatePidCategories(pmt);

  collector.CollectPacketStats(MakePacket(kVideoPid, 0));
  collector.CollectPacketStats(MakePacket(kVideoPid, 5));
  EXPECT_EQ(4, collector.GetDroppedPackets(PacketCategory::kVideo));
  collector.CollectPacketStats(MakePacket(kAudioPid, 0));
  collector.CollectPacketStats(MakePacket(kAudioPid, 3));
  EXPECT_EQ(2, collector.GetDroppedPackets(PacketCategory::kAudio));
  collector.CollectPacketStats(MakePacket(kSubtitlePid, 0));
  collector.CollectPacketStats(MakePacket(kSubtitlePid, 2));
  EXPECT_EQ(1, collector.GetDroppedPackets(PacketCategory::kSubtitle));
}

TEST(PacketStatsCollectorTest, PmtPidCategoryIsNotUpdatedByInvalidPmt) {
  constexpr ts::PID kPmtPid = 0x0100;

  ts::PMT pmt;
  // This invalid PMT references the selected service's PMT PID as its PCR and video stream.
  pmt.pcr_pid = kPmtPid;
  pmt.streams.try_emplace(kPmtPid, &pmt, ts::ST_MPEG2_VIDEO);

  PacketStatsCollector collector;
  collector.SetPmtPid(kPmtPid);
  collector.UpdatePidCategories(pmt);

  collector.CollectPacketStats(MakePacket(kPmtPid, 0));
  collector.CollectPacketStats(MakePacket(kPmtPid, 5));

  // The PMT PID must remain classified as PMT, not video, even when the invalid PMT specifies it
  // as a video stream.
  EXPECT_EQ(4, collector.GetDroppedPackets(PacketCategory::kPmt));
  EXPECT_EQ(0, collector.GetDroppedPackets(PacketCategory::kVideo));
}

TEST(PacketStatsCollectorTest, PmtUpdateClassifiesPreviouslyIgnoredPid) {
  PacketStatsCollector collector;

  // This packet is not part of the selected service yet.
  collector.CollectPacketStats(MakePacket(kPid, 0));

  ts::PMT pmt;
  pmt.streams.try_emplace(kPid, &pmt, ts::ST_MPEG2_VIDEO);
  collector.UpdatePidCategories(pmt);

  // The collector must not record CC 0 from the kPid packet received before kPid
  // was classified as a video PID.
  // Therefore, the next kPid packet with CC 2 must be treated as the first collected video
  // packet, and must not add any dropped video packets.
  collector.CollectPacketStats(MakePacket(kPid, 2));
  EXPECT_EQ(0, collector.GetDroppedPackets(PacketCategory::kVideo));
}

TEST(PacketStatsCollectorTest, PmtUpdateResetsCategoryOfPidNoLongerReferenced) {
  ts::PMT pmt;
  pmt.streams.try_emplace(kPid, &pmt, ts::ST_MPEG2_VIDEO);

  PacketStatsCollector collector;
  collector.UpdatePidCategories(pmt);
  collector.CollectPacketStats(MakePacket(kPid, 0));
  collector.CollectPacketStats(MakePacket(kPid, 5));
  EXPECT_EQ(4, collector.GetDroppedPackets(PacketCategory::kVideo));

  // Because the new PMT no longer references kPid, subsequent kPid packets must be ignored.
  ts::PMT next_pmt;
  collector.UpdatePidCategories(next_pmt);
  collector.CollectPacketStats(MakePacket(kPid, 6));
  collector.CollectPacketStats(MakePacket(kPid, 9));
  EXPECT_EQ(4, collector.GetDroppedPackets(PacketCategory::kVideo));
}

// -----------------------------------------------------------------------------------------------
// PMT PID switching (SetPmtPid)
// -----------------------------------------------------------------------------------------------

TEST(PacketStatsCollectorTest, SetPmtPidResetsCategoryOfPreviousPmtPid) {
  constexpr ts::PID kOldPmtPid = 0x0100;
  constexpr ts::PID kNewPmtPid = 0x0200;

  PacketStatsCollector collector;
  collector.SetPmtPid(kOldPmtPid);
  collector.SetPmtPid(kNewPmtPid);

  // The old PMT PID is no longer part of the selected service and must be ignored.
  collector.CollectPacketStats(MakePacket(kOldPmtPid, 0));
  collector.CollectPacketStats(MakePacket(kOldPmtPid, 5));
  ExpectNoDroppedPackets(collector);

  collector.CollectPacketStats(MakePacket(kNewPmtPid, 0));
  collector.CollectPacketStats(MakePacket(kNewPmtPid, 5));
  EXPECT_EQ(4, collector.GetDroppedPackets(PacketCategory::kPmt));
}

TEST(PacketStatsCollectorTest, ChangingPmtPidKeepsStreamClassificationUntilNewPmt) {
  constexpr ts::PID kNewPmtPid = 0x0200;

  ts::PMT pmt;
  pmt.streams.try_emplace(kPid, &pmt, ts::ST_MPEG2_VIDEO);

  PacketStatsCollector collector;
  collector.UpdatePidCategories(pmt);

  // Simulate a PAT update that advertises kNewPmtPid as the PMT PID for the selected service.
  // No PMT has arrived on kNewPmtPid yet.
  collector.SetPmtPid(kNewPmtPid);

  collector.CollectPacketStats(MakePacket(kPid, 0));
  collector.CollectPacketStats(MakePacket(kPid, 5));

  // Until a PMT arrives on kNewPmtPid, kPid must remain classified as video using the last PMT.
  EXPECT_EQ(4, collector.GetDroppedPackets(PacketCategory::kVideo));
}

// -----------------------------------------------------------------------------------------------
// PCR PID handling
// -----------------------------------------------------------------------------------------------

TEST(PacketStatsCollectorTest, PcrPidIsIgnored) {
  constexpr ts::PID kVideoPid = 0x0101;
  constexpr ts::PID kPcrPid = 0x0102;

  ts::PMT pmt;
  pmt.pcr_pid = kPcrPid;
  pmt.streams.try_emplace(kVideoPid, &pmt, ts::ST_MPEG2_VIDEO);

  PacketStatsCollector collector;
  collector.UpdatePidCategories(pmt);

  collector.CollectPacketStats(MakePacket(kPcrPid, 0));
  collector.CollectPacketStats(MakePacket(kPcrPid, 5));

  // A PCR-only PID, i.e. a PCR PID that is not referenced by any stream in the PMT, must not be
  // classified into any category.
  ExpectNoDroppedPackets(collector);
}

TEST(PacketStatsCollectorTest, PcrPidSharedWithVideoStreamIsClassifiedAsVideo) {
  constexpr ts::PID kVideoPid = 0x0101;

  ts::PMT pmt;
  pmt.pcr_pid = kVideoPid;
  pmt.streams.try_emplace(kVideoPid, &pmt, ts::ST_MPEG2_VIDEO);

  PacketStatsCollector collector;
  collector.UpdatePidCategories(pmt);

  collector.CollectPacketStats(MakePacket(kVideoPid, 0));
  collector.CollectPacketStats(MakePacket(kVideoPid, 5));
  EXPECT_EQ(4, collector.GetDroppedPackets(PacketCategory::kVideo));
}

// -----------------------------------------------------------------------------------------------
// NULL PID handling
// -----------------------------------------------------------------------------------------------

TEST(PacketStatsCollectorTest, NullPidInPmtIsIgnored) {
  ts::PMT pmt;
  // The collector must not track the NULL PID specified in any PMT field, because the NULL PID's
  // CC is meaningless.
  pmt.pcr_pid = ts::PID_NULL;
  pmt.streams.try_emplace(ts::PID_NULL, &pmt, ts::ST_MPEG2_VIDEO);

  PacketStatsCollector collector;
  collector.UpdatePidCategories(pmt);

  collector.CollectPacketStats(MakePacket(ts::PID_NULL, 0));
  collector.CollectPacketStats(MakePacket(ts::PID_NULL, 5));

  ExpectNoDroppedPackets(collector);
}

// -----------------------------------------------------------------------------------------------
// Continuity counter / dropped-packet counting
// -----------------------------------------------------------------------------------------------

TEST(PacketStatsCollectorTest, RegularPacketsProduceZeroErrorStatistics) {
  PacketStatsCollector collector;
  ConfigureVideoPid(collector);
  collector.CollectPacketStats(MakePacket(kPid, 0));
  collector.CollectPacketStats(MakePacket(kPid, 1));
  collector.CollectPacketStats(MakePacket(kPid, 2));

  EXPECT_EQ(0, collector.GetErrorPackets());
  EXPECT_EQ(0, collector.GetScrambledPackets());
  ExpectNoDroppedPackets(collector);
}

TEST(PacketStatsCollectorTest, SameCcRecordsDroppedPackets) {
  PacketStatsCollector collector;
  ConfigureVideoPid(collector);
  collector.CollectPacketStats(MakePacket(kPid, 0, 0xAA));
  collector.CollectPacketStats(MakePacket(kPid, 0, 0xBB));

  // A 0-to-0 CC transition must be counted as 15 dropped packets.
  EXPECT_EQ(15, collector.GetDroppedPackets(PacketCategory::kVideo));
}

TEST(PacketStatsCollectorTest, MultipleMissingPacketsIncreasesDropped) {
  PacketStatsCollector collector;
  ConfigureVideoPid(collector);
  collector.CollectPacketStats(MakePacket(kPid, 5));
  collector.CollectPacketStats(MakePacket(kPid, 9));

  EXPECT_EQ(3, collector.GetDroppedPackets(PacketCategory::kVideo));
}

TEST(PacketStatsCollectorTest, WrapAroundMissingPacketsIncreasesDropped) {
  PacketStatsCollector collector;
  ConfigureVideoPid(collector);
  collector.CollectPacketStats(MakePacket(kPid, 14));
  collector.CollectPacketStats(MakePacket(kPid, 2));

  EXPECT_EQ(3, collector.GetDroppedPackets(PacketCategory::kVideo));
}

TEST(PacketStatsCollectorTest, NoPayloadSameCcDoesNotIncreaseDropped) {
  PacketStatsCollector collector;
  ConfigureVideoPid(collector);
  collector.CollectPacketStats(MakeNoPayloadPacket(kPid, 3));
  collector.CollectPacketStats(MakeNoPayloadPacket(kPid, 3));

  EXPECT_EQ(0, collector.GetDroppedPackets(PacketCategory::kVideo));
}

TEST(PacketStatsCollectorTest, NoPayloadCcChangedIncreasesDropped) {
  PacketStatsCollector collector;
  ConfigureVideoPid(collector);
  collector.CollectPacketStats(MakeNoPayloadPacket(kPid, 3));
  collector.CollectPacketStats(MakeNoPayloadPacket(kPid, 4));

  EXPECT_EQ(1, collector.GetDroppedPackets(PacketCategory::kVideo));
}

TEST(PacketStatsCollectorTest, DiscontinuityIndicatorDoesNotIncreaseDropped) {
  PacketStatsCollector collector;
  ConfigureVideoPid(collector);
  collector.CollectPacketStats(MakePacket(kPid, 0));
  collector.CollectPacketStats(MakePacket(kPid, 8, 0xFF, /*tei=*/false, /*discontinuity=*/true));

  EXPECT_EQ(0, collector.GetDroppedPackets(PacketCategory::kVideo));
}

// -----------------------------------------------------------------------------------------------
// CC state across PMT re-categorization
// -----------------------------------------------------------------------------------------------

TEST(PacketStatsCollectorTest, RecategorizedPidDropsItsCcState) {
  PacketStatsCollector collector;
  ConfigureVideoPid(collector);
  collector.CollectPacketStats(MakePacket(kPid, 0));

  // This PMT update leaves kPid uncategorized.  Packets received on kPid are ignored and do not
  // update its CC state.
  collector.UpdatePidCategories(ts::PMT());
  collector.CollectPacketStats(MakePacket(kPid, 1));
  collector.CollectPacketStats(MakePacket(kPid, 2));

  // A later PMT update categorizes kPid again.  CC=3 must not be compared with the earlier CC=0.
  ConfigureVideoPid(collector);
  collector.CollectPacketStats(MakePacket(kPid, 3));
  collector.CollectPacketStats(MakePacket(kPid, 4));

  ExpectNoDroppedPackets(collector);
}

TEST(PacketStatsCollectorTest, RepeatedPmtKeepsCcState) {
  PacketStatsCollector collector;
  ConfigureVideoPid(collector);
  collector.CollectPacketStats(MakePacket(kPid, 0));

  // Processing the same PMT again must not reset CC state.
  // The CC gap (0 to 2) must still be counted.
  ConfigureVideoPid(collector);
  collector.CollectPacketStats(MakePacket(kPid, 2));

  EXPECT_EQ(1, collector.GetDroppedPackets(PacketCategory::kVideo));
}

// -----------------------------------------------------------------------------------------------
// Reset behavior
// -----------------------------------------------------------------------------------------------

TEST(PacketStatsCollectorTest, ResetPacketStatsClearsAllCounters) {
  PacketStatsCollector collector;
  ConfigureVideoPid(collector);

  collector.CollectPacketStats(MakePacket(kPid, 0));
  collector.CollectPacketStats(MakePacket(kPid, 5));
  ts::TSPacket tei_packet = MakePacket(kPid, 8, 0xFF, /*tei=*/true);
  collector.CollectPacketStats(tei_packet);
  ts::TSPacket scrambled_packet = MakePacket(kPid, 9);
  scrambled_packet.setScrambling(1);
  collector.CollectPacketStats(scrambled_packet);

  EXPECT_EQ(1, collector.GetErrorPackets());
  EXPECT_EQ(1, collector.GetScrambledPackets());
  EXPECT_EQ(7, collector.GetDroppedPackets(PacketCategory::kVideo));

  collector.ResetPacketStats();

  EXPECT_EQ(0, collector.GetErrorPackets());
  EXPECT_EQ(0, collector.GetScrambledPackets());
  ExpectNoDroppedPackets(collector);
}
