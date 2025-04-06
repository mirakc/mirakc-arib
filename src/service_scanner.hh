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

#include <cstdlib>
#include <memory>

#include <fmt/format.h>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <tsduck/tsduck.h>

#include "base.hh"
#include "jsonl_source.hh"
#include "logging.hh"
#include "packet_source.hh"
#include "tsduck_helper.hh"

namespace {

struct ServiceScannerOption final {
  SidSet sids;
  SidSet xsids;
};

// The implementation is based on tsTSScanner.cpp.  Unlike the ts::TSScanner
// class, this class reads data from the PacketSink class.
class ServiceScanner final : public PacketSink,
                             public JsonlSource,
                             public ts::TableHandlerInterface {
 public:
  explicit ServiceScanner(const ServiceScannerOption& option) : option_(option), demux_(context_) {
    demux_.setTableHandler(this);
    demux_.addPID(ts::PID_PAT);
    demux_.addPID(ts::PID_NIT);
    demux_.addPID(ts::PID_SDT);
    // TODO: demux_.addPID(ts::PID_ARIB_CDT);
  }

  ~ServiceScanner() override {}

  void End() override {
    if (!completed()) {
      return;
    }

    // Convert into a JSON object
    rapidjson::Document doc(rapidjson::kArrayType);
    CollectServices(&doc);
    FeedDocument(doc);
  }

  int GetExitCode() const override {
    if (!completed()) {
      return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
  }

  bool HandlePacket(const ts::TSPacket& packet) override {
    demux_.feedPacket(packet);
    if (completed()) {
      MIRAKC_ARIB_INFO("Ready to collect services");
      return false;
    }
    return true;
  }

 private:
  bool completed() const {
    // Stop when all tables are ready.
    return pat_ && sdt_ && nit_;
  }

  void handleTable(ts::SectionDemux&, const ts::BinaryTable& table) override {
    switch (table.tableId()) {
      case ts::TID_PAT:
        HandlePat(table);
        break;
      case ts::TID_NIT_ACT:
        HandleNit(table);
        break;
      case ts::TID_SDT_ACT:
        HandleSdt(table);
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

    std::unique_ptr<ts::PAT> pat = std::make_unique<ts::PAT>(context_, table);

    if (!pat->isValid()) {
      MIRAKC_ARIB_WARN("Broken PAT, skip");
      return;
    }

    if (pat->ts_id == 0) {
      MIRAKC_ARIB_WARN("PAT for TSID#0000, skip");
      return;
    }

    if (pat->nit_pid != ts::PID_NULL && pat->nit_pid != ts::PID_NIT) {
      MIRAKC_ARIB_INFO("Non-standard NIT#{:04X}, reset NIT", pat->nit_pid);
      nit_.reset();
      demux_.removePID(ts::PID_NIT);
      demux_.addPID(pat->nit_pid);
    }

    pat_ = std::move(pat);
    MIRAKC_ARIB_INFO("PAT ready");
  }

  void HandleNit(const ts::BinaryTable& table) {
    std::unique_ptr<ts::NIT> nit = std::make_unique<ts::NIT>(context_, table);

    if (!nit->isValid()) {
      MIRAKC_ARIB_WARN("Broken NIT, skip");
      return;
    }

    nit_ = std::move(nit);
    MIRAKC_ARIB_INFO("NIT ready");
  }

  void HandleSdt(const ts::BinaryTable& table) {
    std::unique_ptr<ts::SDT> sdt = std::make_unique<ts::SDT>(context_, table);

    if (!sdt->isValid()) {
      MIRAKC_ARIB_WARN("Broken SDT, skip");
      return;
    }

    if (sdt->ts_id == 0) {
      MIRAKC_ARIB_WARN("SDT for TSID#0000, skip");
      return;
    }

    sdt_ = std::move(sdt);
    MIRAKC_ARIB_INFO("SDT ready");
  }

  void CollectServices(rapidjson::Document* doc) {
    if (!pat_) {
      MIRAKC_ARIB_ERROR("No PAT found");
      return;
    }

    if (!sdt_) {
      MIRAKC_ARIB_ERROR("No SDT found");
      return;
    }

    auto& allocator = doc->GetAllocator();
    for (auto it = pat_->pmts.begin(); it != pat_->pmts.end(); ++it) {
      uint16_t sid = it->first;

      if (!option_.sids.IsEmpty() && !option_.sids.Contain(sid)) {
        MIRAKC_ARIB_DEBUG("Ignore SID#{:04X} according to the inclusion list", sid);
        continue;
      }

      if (!option_.xsids.IsEmpty() && option_.xsids.Contain(sid)) {
        MIRAKC_ARIB_DEBUG("Ignore SID#{:04X} according to the exclusion list", sid);
        continue;
      }

      const auto svit = sdt_->services.find(sid);
      if (svit == sdt_->services.end()) {
        continue;
      }

      const uint8_t type = svit->second.serviceType(context_);
      if (!IsAudioVideoService(type)) {
        continue;
      }

      int logo_id = -1;
      auto i = svit->second.descs.search(ts::DID_ARIB_LOGO_TRANSMISSION);
      if (i < svit->second.descs.count()) {
        const auto& dp = svit->second.descs[i];
        ts::ARIBLogoTransmissionDescriptor desc(context_, *dp);
        if (desc.logo_transmission_type == 1 || desc.logo_transmission_type == 2) {
          logo_id = desc.logo_id;
        }
      }

      uint16_t nid = sdt_->onetw_id;
      uint16_t tsid = sdt_->ts_id;

      std::string name(svit->second.serviceName(context_).toUTF8());
      uint8_t remote_control_key_id = GetRemoteControlKeyId();

      rapidjson::Value v(rapidjson::kObjectType);
      v.AddMember("nid", nid, allocator);
      v.AddMember("tsid", tsid, allocator);
      v.AddMember("sid", sid, allocator);
      v.AddMember("name", name, allocator);
      v.AddMember("type", type, allocator);
      v.AddMember("logoId", logo_id, allocator);
      if (remote_control_key_id != 0) {
        v.AddMember("remoteControlKeyId", remote_control_key_id, allocator);
      }
      doc->PushBack(v, allocator);
    }
  }

  uint8_t GetRemoteControlKeyId() {
    if (!nit_) {
      return 0;
    }

    const ts::TransportStreamId ts(sdt_->ts_id, sdt_->onetw_id);
    const auto tsit = nit_->transports.find(ts);
    if (tsit == nit_->transports.end()) {
      return 0;
    }

    const ts::DescriptorList& descs(tsit->second.descs);
    for (size_t i = 0; i < descs.size(); ++i) {
      auto& dp = descs[i];
      if (dp->tag() == ts::DID_ARIB_TS_INFORMATION) {
        ts::ARIBTSInformationDescriptor desc(context_, *dp);
        return desc.remote_control_key_id;
      }
    }

    return 0;
  }

  const ServiceScannerOption option_;
  ts::DuckContext context_;
  ts::SectionDemux demux_;
  std::unique_ptr<ts::PAT> pat_;
  std::unique_ptr<ts::SDT> sdt_;
  std::unique_ptr<ts::NIT> nit_;

  MIRAKC_ARIB_NON_COPYABLE(ServiceScanner);
};

}  // namespace
