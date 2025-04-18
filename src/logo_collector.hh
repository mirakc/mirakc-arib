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
#include <sstream>
#include <tuple>
#include <unordered_set>

#include <LibISDB/LibISDB.hpp>
#include <LibISDB/Engine/StreamSourceEngine.hpp>
#include <LibISDB/Filters/LogoDownloaderFilter.hpp>
#include <LibISDB/Filters/SourceFilter.hpp>
#include <LibISDB/Filters/TSPacketParserFilter.hpp>
#include <cppcodec/base64_rfc4648.hpp>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <tsduck/tsduck.h>

#include "base.hh"
#include "jsonl_source.hh"
#include "logging.hh"
#include "packet_source.hh"

namespace {

inline std::tuple<std::unique_ptr<uint8_t[]>, size_t> InsertPngChunks(
    const uint8_t* data, size_t size) {
  // clang-format off
  // PLTE and tRNS chunks extracted from a logo data downloaded from Mirakurun.
  //
  //   curl -fsSL http://mirakurun:40772/services/{id}/logo \
  //     | tail -c +34 | head -c 540 | xxd -i
  //
  static const uint8_t kChunks[540] = {
    0x00, 0x00, 0x01, 0x83, 0x50, 0x4c, 0x54, 0x45, 0x00, 0x00, 0x00, 0xff,
    0x00, 0x00, 0x00, 0xff, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0xff, 0xff,
    0x00, 0xff, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0xaa,
    0x00, 0x00, 0x00, 0xaa, 0x00, 0xaa, 0xaa, 0x00, 0x00, 0x00, 0xaa, 0xaa,
    0x00, 0xaa, 0x00, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0x00, 0x00, 0x55, 0x00,
    0x55, 0x00, 0x00, 0x55, 0x55, 0x00, 0x55, 0xaa, 0x00, 0x55, 0xff, 0x00,
    0xaa, 0x55, 0x00, 0xaa, 0xff, 0x00, 0xff, 0x55, 0x00, 0xff, 0xaa, 0x55,
    0x00, 0x00, 0x55, 0x00, 0x55, 0x55, 0x00, 0xaa, 0x55, 0x00, 0xff, 0x55,
    0x55, 0x00, 0x55, 0x55, 0x55, 0x55, 0x55, 0xaa, 0x55, 0x55, 0xff, 0x55,
    0xaa, 0x00, 0x55, 0xaa, 0x55, 0x55, 0xaa, 0xaa, 0x55, 0xaa, 0xff, 0x55,
    0xff, 0x00, 0x55, 0xff, 0x55, 0x55, 0xff, 0xaa, 0x55, 0xff, 0xff, 0xaa,
    0x00, 0x55, 0xaa, 0x00, 0xff, 0xaa, 0x55, 0x00, 0xaa, 0x55, 0x55, 0xaa,
    0x55, 0xaa, 0xaa, 0x55, 0xff, 0xaa, 0xaa, 0x55, 0xaa, 0xaa, 0xff, 0xaa,
    0xff, 0x00, 0xaa, 0xff, 0x55, 0xaa, 0xff, 0xaa, 0xaa, 0xff, 0xff, 0xff,
    0x00, 0x55, 0xff, 0x00, 0xff, 0xff, 0x55, 0x00, 0xff, 0x55, 0x55, 0xff,
    0x55, 0xaa, 0xff, 0x55, 0xff, 0xff, 0xaa, 0x00, 0xff, 0xaa, 0x55, 0xff,
    0xaa, 0xaa, 0xff, 0xaa, 0xff, 0xff, 0xff, 0x55, 0xff, 0xff, 0xff, 0x00,
    0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0xff, 0x00, 0xff, 0xff, 0x00, 0x00,
    0x00, 0xff, 0xff, 0x00, 0xff, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xaa,
    0x00, 0x00, 0x00, 0xaa, 0x00, 0xaa, 0xaa, 0x00, 0x00, 0x00, 0xaa, 0xaa,
    0x00, 0xaa, 0x00, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0x00, 0x00, 0x55, 0x00,
    0x55, 0x00, 0x00, 0x55, 0x55, 0x00, 0x55, 0xaa, 0x00, 0x55, 0xff, 0x00,
    0xaa, 0x55, 0x00, 0xaa, 0xff, 0x00, 0xff, 0x55, 0x00, 0xff, 0xaa, 0x55,
    0x00, 0x00, 0x55, 0x00, 0x55, 0x55, 0x00, 0xaa, 0x55, 0x00, 0xff, 0x55,
    0x55, 0x00, 0x55, 0x55, 0x55, 0x55, 0x55, 0xaa, 0x55, 0x55, 0xff, 0x55,
    0xaa, 0x00, 0x55, 0xaa, 0x55, 0x55, 0xaa, 0xaa, 0x55, 0xaa, 0xff, 0x55,
    0xff, 0x00, 0x55, 0xff, 0x55, 0x55, 0xff, 0xaa, 0x55, 0xff, 0xff, 0xaa,
    0x00, 0x55, 0xaa, 0x00, 0xff, 0xaa, 0x55, 0x00, 0xaa, 0x55, 0x55, 0xaa,
    0x55, 0xaa, 0xaa, 0x55, 0xff, 0xaa, 0xaa, 0x55, 0xaa, 0xaa, 0xff, 0xaa,
    0xff, 0x00, 0xaa, 0xff, 0x55, 0xaa, 0xff, 0xaa, 0xaa, 0xff, 0xff, 0xff,
    0x00, 0x55, 0xff, 0x00, 0xff, 0xff, 0x55, 0x00, 0xff, 0x55, 0x55, 0xff,
    0x55, 0xaa, 0xff, 0x55, 0xff, 0xff, 0xaa, 0x00, 0xff, 0xaa, 0x55, 0xff,
    0xaa, 0xaa, 0xff, 0xaa, 0xff, 0xff, 0xff, 0x55, 0xff, 0xff, 0xff, 0x06,
    0xdd, 0x27, 0x7b, 0x00, 0x00, 0x00, 0x81, 0x74, 0x52, 0x4e, 0x53, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x7b, 0x70, 0xf7, 0x6f
  };
  // clang-format on

  constexpr size_t kIdatOffset = 33;

  MIRAKC_ARIB_ASSERT(size > kIdatOffset);

  auto png = std::make_unique<uint8_t[]>(size + sizeof(kChunks));
  auto* p = png.get();

  std::memcpy(p, data, kIdatOffset);
  p += kIdatOffset;

  std::memcpy(p, kChunks, sizeof(kChunks));
  p += sizeof(kChunks);

  std::memcpy(p, data + kIdatOffset, size - kIdatOffset);

  return std::make_tuple(std::move(png), size + sizeof(kChunks));
}

inline std::string MakeBase64Png(const uint8_t* data, size_t size) {
  auto [png, png_size] = InsertPngChunks(data, size);
  auto base64 = cppcodec::base64_rfc4648::encode(png.get(), png_size);
  return std::string("data:image/png;base64,") + base64;
}

class LibISDBLogger : public LibISDB::Logger {
 public:
  LibISDBLogger() = default;
  ~LibISDBLogger() = default;

 private:
  void OnLog(LibISDB::Logger::LogType type, const LibISDB::CharType* text) override {
    switch (type) {
      case LibISDB::Logger::LogType::Verbose:
        MIRAKC_ARIB_DEBUG("LibISDB: {}", text);
        break;
      case LibISDB::Logger::LogType::Information:
        MIRAKC_ARIB_INFO("LibISDB: {}", text);
        break;
      case LibISDB::Logger::LogType::Warning:
        MIRAKC_ARIB_WARN("LibISDB: {}", text);
        break;
      case LibISDB::Logger::LogType::Error:
        MIRAKC_ARIB_ERROR("LibISDB: {}", text);
        break;
    }
  }

  MIRAKC_ARIB_NON_COPYABLE(LibISDBLogger);
};

class LibISDBSourceBridge : public PacketSink, public LibISDB::SourceFilter {
 public:
  LibISDBSourceBridge() : LibISDB::SourceFilter(LibISDB::SourceFilter::SourceMode::Push) {}
  ~LibISDBSourceBridge() override {}

  bool HandlePacket(const ts::TSPacket& packet) override {
    LibISDB::DataBuffer data(packet.b, sizeof(packet.b));
    return OutputData(&data);
  }

  // LibISDB::ObjectBase

  const LibISDB::CharType* GetObjectName() const noexcept override {
    return LIBISDB_STR("LibISDBSourceBridge");
  }

  // LibISDB::SourceFilter

  bool OpenSource(const LibISDB::CStringView&) override {
    return true;
  }

  bool CloseSource() override {
    return true;
  }

  bool IsSourceOpen() const override {
    return true;
  }

  LibISDB::SourceFilter::SourceMode GetAvailableSourceModes() const noexcept override {
    return LibISDB::SourceFilter::SourceMode::Push;
  }

  MIRAKC_ARIB_NON_COPYABLE(LibISDBSourceBridge);
};

class LogoCollector final : public PacketSink,
                            public JsonlSource,
                            public LibISDB::StreamSourceEngine,
                            public LibISDB::LogoDownloaderFilter::LogoHandler {
 public:
  explicit LogoCollector() {
    SetLogger(&logger_);
    SetStartStreamingOnSourceOpen(true);
  }

  ~LogoCollector() override {}

  bool Start() override {
    source_bridge_ = new LibISDBSourceBridge;
    auto* parser = new LibISDB::TSPacketParserFilter;
    auto* logo_downloader = new LibISDB::LogoDownloaderFilter;
    logo_downloader->SetLogoHandler(this);
    BuildEngine({source_bridge_, parser, logo_downloader});
    OpenSource(LIBISDB_STR(""));
    return true;
  }

  void End() override {
    CloseEngine();
    source_bridge_ = nullptr;
  }

  bool HandlePacket(const ts::TSPacket& packet) override {
    return source_bridge_->HandlePacket(packet);
  }

 private:
  // LibISDB::LogoDownloaderFilter::LogoHandler
  void OnLogoDownloaded(const LibISDB::LogoDownloaderFilter::LogoData& logo) override {
    if (logo.DataSize <= 93) {  // transparent logos
      MIRAKC_ARIB_DEBUG("Logo(transparent): type({}) id({}) version({}) size({}) nid({})",
          logo.LogoType, logo.LogoID, logo.LogoVersion, logo.DataSize, logo.NetworkID);
      return;
    }

    MIRAKC_ARIB_INFO("Logo: type({}) id({}) version({}) size({}) nid({})", logo.LogoType,
        logo.LogoID, logo.LogoVersion, logo.DataSize, logo.NetworkID);
    if (logo.ServiceList.size() > 0) {
      for (const auto& sv : logo.ServiceList) {
        MIRAKC_ARIB_INFO(
            "Service: nid({}) tsid({}) sid({})", sv.NetworkID, sv.TransportStreamID, sv.ServiceID);
      }
    }

    auto json = MakeJsonValue(logo);
    FeedDocument(json);
  }

  rapidjson::Document MakeJsonValue(const LibISDB::LogoDownloaderFilter::LogoData& logo) {
    std::string data = MakeBase64Png(logo.pData, logo.DataSize);

    rapidjson::Document json(rapidjson::kObjectType);
    auto& allocator = json.GetAllocator();

    json.AddMember("type", logo.LogoType, allocator);
    json.AddMember("id", logo.LogoID, allocator);
    json.AddMember("version", logo.LogoVersion, allocator);
    json.AddMember("data", data, allocator);
    json.AddMember("nid", logo.NetworkID, allocator);

    if (logo.ServiceList.size() > 0) {
      rapidjson::Value services(rapidjson::kArrayType);
      for (const auto& sv : logo.ServiceList) {
        rapidjson::Value value(rapidjson::kObjectType);
        value.AddMember("nid", sv.NetworkID, allocator);
        value.AddMember("tsid", sv.TransportStreamID, allocator);
        value.AddMember("sid", sv.ServiceID, allocator);
        services.PushBack(value, allocator);
      }
      json.AddMember("services", services, allocator);
    }

    return json;
  }

  LibISDBLogger logger_;
  LibISDBSourceBridge* source_bridge_ = nullptr;  // not owned

  MIRAKC_ARIB_NON_COPYABLE(LogoCollector);
};

}  // namespace
