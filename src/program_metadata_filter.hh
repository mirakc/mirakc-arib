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
#include "tsduck_helper.hh"

namespace {

struct ProgramMetadataFilterOption final {
  uint16_t sid = 0;
};

class ProgramMetadataFilter final : public PacketSink,
                                    public JsonlSource,
                                    public ts::TableHandlerInterface {
 public:
  explicit ProgramMetadataFilter(const ProgramMetadataFilterOption& option)
      : option_(option), demux_(context_) {
    demux_.setTableHandler(this);
    demux_.addPID(ts::PID_EIT);
    MIRAKC_ARIB_DEBUG("Demux EIT");
  }

  virtual ~ProgramMetadataFilter() override {}

  bool HandlePacket(const ts::TSPacket& packet) override {
    demux_.feedPacket(packet);
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

    if (option_.sid != 0 && eit.service_id != option_.sid) {
      return;
    }

    if (eit.events.size() == 0) {
      MIRAKC_ARIB_WARN("No event in EIT");
      return;
    }

    WriteEvents(eit);
  }

  void WriteEvents(const ts::EIT& eit) {
    rapidjson::Document json(rapidjson::kObjectType);
    auto& allocator = json.GetAllocator();
    rapidjson::Value events_json(rapidjson::kArrayType);
    for (size_t i = 0; i < eit.events.size(); ++i) {
      auto event_json = MakeJsonValue(eit.events[i], allocator);
      events_json.PushBack(event_json, allocator);
    }
    json.AddMember("nid", eit.onetw_id, allocator);
    json.AddMember("tsid", eit.ts_id, allocator);
    json.AddMember("sid", eit.service_id, allocator);
    json.AddMember("events", events_json, allocator);

    FeedDocument(json);
  }

  const ProgramMetadataFilterOption option_;
  ts::DuckContext context_;
  ts::SectionDemux demux_;
};

}  // namespace
