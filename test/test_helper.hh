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

#include <cassert>
#include <limits>
#include <memory>
#include <queue>

#include <gmock/gmock.h>
#include <tsduck/tsduck.h>

#include "file.hh"
#include "jsonl_sink.hh"
#include "packet_sink.hh"
#include "packet_source.hh"

namespace {

class MockFile final : public File {
 public:
  MockFile() = default;
  ~MockFile() override = default;

  const std::string& path() const override {
    return path_;
  }

  MOCK_METHOD(ssize_t, Read, (uint8_t* buf, size_t len), (override));
  MOCK_METHOD(ssize_t, Write, (uint8_t* buf, size_t len), (override));
  MOCK_METHOD(bool, Sync, (), (override));
  MOCK_METHOD(bool, Trunc, (int64_t), (override));
  MOCK_METHOD(int64_t, Seek, (int64_t, SeekMode), (override));

 private:
  std::string path_ = "<mock>";
};

class MockSource final : public PacketSource {
 public:
  MockSource() {}
  ~MockSource() override {}

  MOCK_METHOD(bool, GetNextPacket, (ts::TSPacket*), (override));
};

class MockSink final : public PacketSink {
 public:
  MockSink() {}
  ~MockSink() override {}

  MOCK_METHOD(bool, Start, (), (override));
  MOCK_METHOD(void, End, (), (override));
  MOCK_METHOD(int, GetExitCode, (), (const override));
  MOCK_METHOD(bool, HandlePacket, (const ts::TSPacket&), (override));
};

class MockRingSink final : public PacketRingSink {
 public:
  MockRingSink(size_t chunk_size, size_t num_chunks)
      : chunk_size_(chunk_size), ring_size_(chunk_size * num_chunks) {}
  ~MockRingSink() override = default;

  MOCK_METHOD(bool, Start, (), (override));
  MOCK_METHOD(void, End, (), (override));

  bool HandlePacket(const ts::TSPacket& packet) override {
    switch (packet.getPID()) {
      case 0x0FFF:
        return false;
      case 0x0FFE:
        pos_ = ((pos_ + chunk_size_) / chunk_size_) * chunk_size_;
        observer_->OnEndOfChunk(pos_);
        return true;
      case 0x0FFD:
        while (pos_ < ring_size_) {
          pos_ = ((pos_ + chunk_size_) / chunk_size_) * chunk_size_;
          observer_->OnEndOfChunk(pos_);
        }
        pos_ = 0;
        return true;
      default:
        pos_ += ts::PKT_SIZE;
        return true;
    }
  }

  uint64_t ring_size() const override {
    return ring_size_;
  }

  uint64_t pos() const override {
    return pos_;
  }

  bool SetPosition(uint64_t pos) override {
    if (pos > std::numeric_limits<int64_t>::max()) {
      return false;
    }
    pos_ = pos;
    return true;
  }

  void SetObserver(PacketRingObserver* observer) override {
    observer_ = observer;
  }

 private:
  uint64_t ring_size_;
  uint64_t pos_ = 0;
  size_t chunk_size_;
  size_t sync_pos_ = 0;
  PacketRingObserver* observer_ = nullptr;
};

class TableSource final : public PacketSource {
 public:
  TableSource() : section_file_(context_) {}
  ~TableSource() override {}

  void LoadXml(const std::string& xml) {
    ts::xml::Document doc(CERR);
    doc.parse(ts::UString::FromUTF8(xml));

    const auto* root = doc.rootElement();
    for (const auto* node = root->firstChildElement(); node != nullptr;
        node = node->nextSiblingElement()) {
      ts::PID pid;
      node->getIntAttribute<ts::PID>(pid, u"test-pid", true, 0, 0x0000, 0x1FFF);

      ts::BinaryTable table;
      table.fromXML(context_, node);
      table.setSourcePID(pid);

      auto packetizer = std::make_unique<ts::CyclingPacketizer>(pid);
      packetizer->addTable(table);

      ts::TSPacket packet;
      packetizer->getNextPacket(packet);

      if (node->hasAttribute(u"test-cc")) {
        uint8_t cc;
        node->getIntAttribute<uint8_t>(cc, u"test-cc", false, 0, 0x00, 0x0F);
        packet.setCC(cc);
      }

      if (node->hasAttribute(u"test-pcr")) {
        uint64_t pcr;
        node->getIntAttribute<uint64_t>(pcr, u"test-pcr", false);
        if (pcr == ts::INVALID_PCR) {
          packet.b[3] |= 0x20;
          packet.b[4] = 1;
          packet.b[5] |= 0x10;
          assert(packet.hasPCR());
          assert(packet.getPCR() == ts::INVALID_PCR);
        } else {
          packet.setPayloadSize(0);
          packet.setPCR(pcr);
        }
      }

      if (node->hasAttribute(u"test-sleep")) {
        uint8_t sleep_ms;
        node->getIntAttribute<uint8_t>(sleep_ms, u"test-sleep", false);
        packet.setPayloadSize(0);
        packet.setPrivateData(&sleep_ms, 1);
      }

      packets_.push(std::move(packet));
    }
  }

  bool GetNextPacket(ts::TSPacket* packet) override {
    for (;;) {
      if (packets_.empty()) {
        return false;
      }

      if (packets_.front().hasPrivateData()) {
        auto sleep_ms = *packets_.front().getPrivateData();
        ts::SleepThread(sleep_ms);
        packets_.pop();
        continue;
      }

      ts::TSPacket::Copy(packet, &packets_.front());
      packets_.pop();
      return true;
    }
  }

  bool IsEmpty() const {
    return packets_.empty();
  }

  size_t GetNumberOfRemainingPackets() const {
    return packets_.size();
  }

 private:
  ts::DuckContext context_;
  ts::SectionFile section_file_;
  std::queue<ts::TSPacket> packets_;
};

template <class T>
class TableValidator : public ts::TableHandlerInterface {
 public:
  TableValidator(ts::PID pid) : demux_(context_) {
    demux_.setTableHandler(this);
    demux_.addPID(pid);
  }

  ~TableValidator() override {}

  void FeedPacket(const ts::TSPacket& packet) {
    demux_.feedPacket(packet);
  }

  MOCK_METHOD(void, Validate, (const T&));

 private:
  void handleTable(ts::SectionDemux&, const ts::BinaryTable& table) override {
    T t(context_, table);
    Validate(t);
  }

  ts::DuckContext context_;
  ts::SectionDemux demux_;
};

class MockJsonlSink final : public JsonlSink {
 public:
  MockJsonlSink() {}
  ~MockJsonlSink() override {}

  MOCK_METHOD(bool, HandleDocument, (const rapidjson::Document&), (override));

  static std::string Stringify(const rapidjson::Document& doc) {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    return std::string(buffer.GetString());
  }
};

// Workaround for tsduck/tsduck/issues/549.
#define MY_XML_NAME u"stream_identifier_descriptor"
#define MY_DID ts::DID_STREAM_ID
#define MY_STD ts::STD_DVB
TS_XML_DESCRIPTOR_FACTORY(ts::StreamIdentifierDescriptor, MY_XML_NAME);
TS_ID_DESCRIPTOR_FACTORY(ts::StreamIdentifierDescriptor, ts::EDID::Standard(MY_DID));
TS_FACTORY_REGISTER(ts::StreamIdentifierDescriptor::DisplayDescriptor, ts::EDID::Standard(MY_DID));

}  // namespace
