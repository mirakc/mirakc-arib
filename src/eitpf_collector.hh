#pragma once

#include <map>
#include <memory>

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

struct EitpfCollectorOption final {
  SidSet sids;
  bool streaming = false;
  bool present = true;
  bool following = true;
};

class EitpfCollector final : public PacketSink,
                             public JsonlSource,
                             public ts::SectionHandlerInterface {
 public:
  explicit EitpfCollector(const EitpfCollectorOption& option)
      : option_(option),
        demux_(context_) {
    demux_.setSectionHandler(this);
    demux_.addPID(ts::PID_EIT);
    MIRAKC_ARIB_DEBUG("Demux EIT");
  }

  virtual ~EitpfCollector() override {}

  bool HandlePacket(const ts::TSPacket& packet) override {
    demux_.feedPacket(packet);
    if (Done()) {
      return false;
    }
    return true;
  }

 private:
  static bool IsCollected(
      EitSection& eit, const std::map<uint64_t, uint8_t>& versions) {
      const auto it = versions.find(eit.service_triple());
      if (it == versions.end()) {
        return false;
      }
      return it->second == eit.version;
  }

  bool Done() const {
    if (option_.streaming) {
      return false;
    }
    if (option_.present && present_versions_.size() != option_.sids.size()) {
      return false;
    }
    if (option_.following && following_versions_.size() != option_.sids.size()) {
      return false;
    }
    MIRAKC_ARIB_INFO("Collected all sections");
    return true;
  }

  void handleSection(ts::SectionDemux&, const ts::Section& section) override {
    if (!section.isValid()) {
      MIRAKC_ARIB_WARN("Broken EIT, skip");
      return;
    }

    const auto tid = section.tableId();
    if (tid != ts::TID_EIT_PF_ACT) {
      return;
    }

    if (section.payloadSize() < EitSection::EIT_PAYLOAD_FIXED_SIZE) {
      MIRAKC_ARIB_WARN("Too short payload, skip");
      return;
    }

    if (section.isNext()) {
      return;
    }

    EitSection eit(section);
    if (!option_.sids.IsEmpty() && !option_.sids.Contain(eit.sid)) {
      MIRAKC_ARIB_DEBUG(
          "Ignore SID#{:04X} according to the inclusion list", eit.sid);
      return;
    }

    if (eit.section_number == 0) {
      if (IsCollected(eit, present_versions_)) {
        return;
      }
      MIRAKC_ARIB_INFO(
          "EIT[p]: onid({:04X}) tsid({:04X}) sid({:04X}) tid({:04X}/{:02X})"
          " sec({:02X}:{:02X}/{:02X}) ver({:02d})",
          eit.nid, eit.tsid, eit.sid, eit.tid, eit.last_table_id,
          eit.section_number, eit.segment_last_section_number,
          eit.last_section_number, eit.version);
      if (option_.present) {
        WriteEitSection(eit);
      }
      present_versions_[eit.service_triple()] = eit.version;
    } else if (eit.section_number == 1) {
      if (IsCollected(eit, following_versions_)) {
        return;
      }
      MIRAKC_ARIB_INFO(
          "EIT[f]: onid({:04X}) tsid({:04X}) sid({:04X}) tid({:04X}/{:02X})"
          " sec({:02X}:{:02X}/{:02X}) ver({:02d})",
          eit.nid, eit.tsid, eit.sid, eit.tid, eit.last_table_id,
          eit.section_number, eit.segment_last_section_number,
          eit.last_section_number, eit.version);
      if (option_.following) {
        WriteEitSection(eit);
      }
      following_versions_[eit.service_triple()] = eit.version;
    } else {
      MIRAKC_ARIB_DEBUG("Ignore unknown section#{:02X}", eit.section_number);
    }
  }

  void WriteEitSection(EitSection& eit) {
    FeedDocument(MakeJsonValue(eit));
  }

  const EitpfCollectorOption option_;
  ts::DuckContext context_;
  ts::SectionDemux demux_;
  std::map<uint64_t, uint8_t> present_versions_;
  std::map<uint64_t, uint8_t> following_versions_;
};

}  // namespace
