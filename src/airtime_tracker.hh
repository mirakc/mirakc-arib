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

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <tsduck/tsduck.h>

#include "base.hh"
#include "jsonl_source.hh"
#include "logging.hh"
#include "packet_sink.hh"

namespace {

struct AirtimeTrackerOption final {
  uint16_t sid = 0;
  uint16_t eid = 0;
};

class AirtimeTracker final : public PacketSink,
                             public JsonlSource,
                             public ts::TableHandlerInterface {
 public:
  explicit AirtimeTracker(const AirtimeTrackerOption& option) : option_(option), demux_(context_) {
    demux_.setTableHandler(this);
    demux_.addPID(ts::PID_EIT);
    MIRAKC_ARIB_DEBUG("Demux EIT");
  }

  virtual ~AirtimeTracker() override {}

  bool HandlePacket(const ts::TSPacket& packet) override {
    demux_.feedPacket(packet);
    if (done_) {
      return false;
    }
    return true;
  }

 private:
  void handleTable(ts::SectionDemux&, const ts::BinaryTable& table) override {
    switch (table.tableId()) {
      case ts::TID_EIT_PF_ACT:
        HandleEit(table);
        break;
      default:
        break;
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
      MIRAKC_ARIB_ERROR("No event in EIT");
      done_ = true;
      return;
    }

    const auto& present = eit.events[0];
    if (present.event_id == option_.eid) {
      MIRAKC_ARIB_DEBUG("Event#{:04X} has started", option_.eid);
      WriteEventInfo(eit, present);
      return;
    }

    if (eit.events.size() < 2) {
      MIRAKC_ARIB_WARN("No following event in EIT");
      done_ = true;
      return;
    }

    const auto& following = eit.events[1];
    if (following.event_id == option_.eid) {
      MIRAKC_ARIB_DEBUG("Event#{:04X} will start soon", option_.eid);
      WriteEventInfo(eit, following);
      return;
    }

    MIRAKC_ARIB_ERROR("Event#{:04X} might have been canceled", option_.eid);
    done_ = true;
    return;
  }

  void WriteEventInfo(const ts::EIT& eit, const ts::EIT::Event& event) {
    ts::MilliSecond start_time_unix = ConvertJstTimeToUnixTime(event.start_time);
    ts::MilliSecond duration = event.duration * ts::MilliSecPerSec;

    rapidjson::Document json(rapidjson::kObjectType);
    auto& allocator = json.GetAllocator();
    json.AddMember("nid", eit.onetw_id, allocator);
    json.AddMember("tsid", eit.ts_id, allocator);
    json.AddMember("sid", eit.service_id, allocator);
    json.AddMember("eid", event.event_id, allocator);
    json.AddMember("startTime", start_time_unix, allocator);
    json.AddMember("duration", duration, allocator);

    FeedDocument(json);
  }

  const AirtimeTrackerOption option_;
  ts::DuckContext context_;
  ts::SectionDemux demux_;
  bool done_ = false;
};

}  // namespace
