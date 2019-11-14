#pragma once

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

  MOCK_METHOD(ssize_t, Read, (uint8_t* buf, size_t len), (override));
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

  MOCK_METHOD(void, Start, (), (override));
  MOCK_METHOD(bool, End, (), (override));
  MOCK_METHOD(bool, HandlePacket, (const ts::TSPacket&), (override));
};

class TableSource final : public PacketSource {
 public:
  TableSource() : section_file_(context_) {}
  ~TableSource() override {}

  void LoadXml(const std::string& xml) {
    ts::xml::Document doc(CERR);
    doc.parse(ts::UString::FromUTF8(xml));

    const auto* root = doc.rootElement();
    for (const auto* node = root->firstChildElement();
         node != nullptr;
         node =node->nextSiblingElement()) {
      ts::PID pid;
      node->getIntAttribute<ts::PID>(pid, u"test-pid", true, 0, 0x0000, 0x1FFF);

      uint8_t cc;
      node->getIntAttribute<uint8_t>(cc, u"test-cc", false, 0, 0x0000, 0x000F);

      ts::BinaryTable table;
      table.fromXML(context_, node);
      table.setSourcePID(pid);

      auto packetizer = std::make_unique<ts::CyclingPacketizer>(pid);
      packetizer->addTable(table);
      packetizer->setNextContinuityCounter(cc);

      packetizers_.push(std::move(packetizer));
    }
  }

  bool GetNextPacket(ts::TSPacket* packet) override {
    if (packetizers_.empty()) {
      return false;
    }

    auto& packetizer = packetizers_.front();
    packetizer->getNextPacket(*packet);
    packetizers_.pop();
    return true;
  }

 private:
  ts::DuckContext context_;
  ts::SectionFile section_file_;
  std::queue<std::unique_ptr<ts::Packetizer>> packetizers_;
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

}  // namespace
