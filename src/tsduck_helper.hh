#pragma once

#include <string>

#include <LibISDB/LibISDB.hpp>
#include <LibISDB/EPG/EventInfo.hpp>
#include <fmt/format.h>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <tsduck/tsduck.h>

#include "logging.hh"

namespace {

static bool g_KeepUnicodeSymbols = false;

constexpr ts::MilliSecond kJstTzOffset = 9 * ts::MilliSecPerHour;

constexpr int64_t kMaxPcrExt = 300;
constexpr int64_t kMaxPcr =
    ((static_cast<int64_t>(1) << 33) - 1) * kMaxPcrExt + (kMaxPcrExt - 1);
static_assert(kMaxPcr == static_cast<int64_t>(2576980377599));
constexpr int64_t kPcrUpperBound = kMaxPcr + 1;
constexpr int64_t kPcrTicksPerSec = 27 * 1000 * 1000;  // 27MHz
constexpr int64_t kPcrTicksPerMs = kPcrTicksPerSec / ts::MilliSecPerSec;

inline bool CheckComponentTagByRange(
    const ts::PMT::Stream& stream, uint8_t min, uint8_t max) {
  uint8_t tag;
  if (stream.getComponentTag(tag)) {
    if (tag >= min && tag <= max) {
      return true;
    }
  }
  return false;
}

inline bool IsAribSubtitle(const ts::PMT::Stream& stream) {
  return CheckComponentTagByRange(stream, 0x30, 0x37);
}

inline bool IsAribSuperimposedText(const ts::PMT::Stream& stream) {
  return CheckComponentTagByRange(stream, 0x38, 0x3F);
}

inline bool IsValidPcr(int64_t pcr) {
  return pcr >= 0 && pcr <= kMaxPcr;
}

std::string FormatPcr(int64_t pcr) {
  MIRAKC_ARIB_ASSERT(IsValidPcr(pcr));

  auto base = pcr / kMaxPcrExt;
  auto ext = pcr % kMaxPcrExt;
  return fmt::format("{:010d}+{:03d}", base, ext);
}

// Compares two PCR values taking into account the PCR wrap around.
//
// Assumed that the real interval time between the PCR values is less than half
// of kPcrUpperBound.
inline int64_t ComparePcr(int64_t lhs, int64_t rhs) {
  MIRAKC_ARIB_ASSERT(IsValidPcr(lhs));
  MIRAKC_ARIB_ASSERT(IsValidPcr(rhs));

  auto a = lhs - rhs;
  auto b = lhs - (kPcrUpperBound + rhs);
  if (std::abs(a) < std::abs(b)) {
    return a;
  }
  return b;
}

struct EitSection {
  uint16_t pid;
  uint16_t sid;
  uint16_t tid;
  uint16_t nid;
  uint16_t tsid;
  uint8_t last_table_id;
  uint8_t section_number;
  uint8_t last_section_number;
  uint8_t segment_last_section_number;
  uint8_t version;
  const uint8_t* events_data;
  size_t events_size;

  static constexpr size_t EIT_PAYLOAD_FIXED_SIZE = 6;
  static constexpr size_t EIT_EVENT_FIXED_SIZE = 12;

  EitSection(const ts::Section& section) {
    const auto* data = section.payload();
    auto size = section.payloadSize();

    pid = section.sourcePID();
    sid = section.tableIdExtension();
    tid = section.tableId();
    nid = ts::GetUInt16(data + 2);
    tsid = ts::GetUInt16(data);
    last_table_id = ts::GetUInt8(data + 5);
    section_number = section.sectionNumber();
    last_section_number = section.lastSectionNumber();
    segment_last_section_number = ts::GetUInt8(data + 4);
    version = section.version();
    events_data = data + EIT_PAYLOAD_FIXED_SIZE;
    events_size = size - EIT_PAYLOAD_FIXED_SIZE;
  }

  inline uint64_t service_triple() const {
    // service triple
    return
        static_cast<uint64_t>(nid)  << 48 |
        static_cast<uint64_t>(tsid) << 32 |
        static_cast<uint32_t>(sid)  << 16;
  }

  inline size_t table_index() const {
    return tid & 0x07;
  }

  inline size_t last_table_index() const {
    return last_table_id & 0x07;
  }

  inline size_t segment_index() const {
    return section_number >> 3;
  }

  inline size_t section_index() const {
    return section_number & 0x07;
  }

  inline size_t last_segment_index() const {
    return last_section_number >> 3;
  }

  inline size_t last_section_index() const {
    return segment_last_section_number & 0x07;
  }

  inline bool IsBasic() const {
    return (tid & 0x0F) < 8;
  }

  rapidjson::Document MakeJsonValue() const {
    rapidjson::Document eit_json(rapidjson::kObjectType);
    auto& allocator = eit_json.GetAllocator();

    auto events = MakeEventsJsonValue(allocator);

    eit_json.AddMember("originalNetworkId", nid, allocator);
    eit_json.AddMember("transportStreamId", tsid, allocator);
    eit_json.AddMember("serviceId", sid, allocator);
    eit_json.AddMember("tableId", tid, allocator);
    eit_json.AddMember("sectionNumber", section_number, allocator);
    eit_json.AddMember("lastSectionNumber", last_section_number, allocator);
    eit_json.AddMember("segmentLastSectionNumber", segment_last_section_number, allocator);
    eit_json.AddMember("versionNumber", version, allocator);
    eit_json.AddMember("events", events, allocator);

    return eit_json;
  }

  template <typename Allocator>
  rapidjson::Value MakeEventsJsonValue(Allocator& allocator) const {
    const auto* data = events_data;
    auto remain = events_size;

    // At least, one event must be contained.
    MIRAKC_ARIB_ASSERT(remain >= EitSection::EIT_EVENT_FIXED_SIZE);

    rapidjson::Value events(rapidjson::kArrayType);

    while (remain >= EitSection::EIT_EVENT_FIXED_SIZE) {
      const auto eid = ts::GetUInt16(data);

      ts::Time start_time;
      ts::DecodeMJD(data + 2, 5, start_time);
      start_time -= kJstTzOffset;  // JST -> UTC
      const auto start_time_unix = start_time - ts::Time::UnixEpoch;

      const auto hour = ts::DecodeBCD(data[7]);
      const auto min = ts::DecodeBCD(data[8]);
      const auto sec = ts::DecodeBCD(data[9]);
      const ts::MilliSecond duration =
          hour * ts::MilliSecPerHour + min * ts::MilliSecPerMin +
          sec * ts::MilliSecPerSec;

      const uint8_t running_status = (data[10] >> 5) & 0x07;

      const bool ca_controlled = (data[10] >> 4) & 0x01;

      size_t info_length = ts::GetUInt16(data + 10) & 0x0FFF;
      data += EitSection::EIT_EVENT_FIXED_SIZE;
      remain -= EitSection::EIT_EVENT_FIXED_SIZE;

      LibISDB::DescriptorBlock desc_block;
      desc_block.ParseBlock(data, info_length);

      rapidjson::Value descriptors(rapidjson::kArrayType);

      for (int i = 0; i < desc_block.GetDescriptorCount(); ++i) {
        const auto* dp = desc_block.GetDescriptorByIndex(i);
        if (!dp->IsValid()) {
          continue;
        }
        switch (dp->GetTag()) {
          case LibISDB::ShortEventDescriptor::TAG: {
            const auto* desc =
                static_cast<const LibISDB::ShortEventDescriptor*>(dp);
            auto json = MakeJsonValue(desc, allocator);
            descriptors.PushBack(json, allocator);
            break;
          }
          case LibISDB::ComponentDescriptor::TAG: {
            const auto* desc =
                static_cast<const LibISDB::ComponentDescriptor*>(dp);
            auto json = MakeJsonValue(desc, allocator);
            descriptors.PushBack(json, allocator);
            break;
          }
          case LibISDB::ContentDescriptor::TAG: {
            const auto* desc =
                static_cast<const LibISDB::ContentDescriptor*>(dp);
            auto json = MakeJsonValue(desc, allocator);
            descriptors.PushBack(json, allocator);
            break;
          }
          case LibISDB::AudioComponentDescriptor::TAG: {
            const auto* desc =
                static_cast<const LibISDB::AudioComponentDescriptor*>(dp);
            auto json = MakeJsonValue(desc, allocator);
            descriptors.PushBack(json, allocator);
            break;
          }
          default:
            break;
        }
      }

      {
        auto json = MakeExtendedEventJsonValue(desc_block, allocator);
        if (!json.IsNull()) {
          descriptors.PushBack(json, allocator);
        }
      }

      rapidjson::Value event(rapidjson::kObjectType);
      event.AddMember("eventId", eid, allocator);
      event.AddMember("startTime", start_time_unix, allocator);
      event.AddMember("duration", duration, allocator);
      event.AddMember("scrambled", ca_controlled, allocator);
      event.AddMember("descriptors", descriptors, allocator);

      events.PushBack(event, allocator);

      data += info_length;
      remain -= info_length;
    }

    return events;
  }

  rapidjson::Value MakeJsonValue(
      const LibISDB::ShortEventDescriptor* desc,
      rapidjson::Document::AllocatorType& allocator) const {
    rapidjson::Value json(rapidjson::kObjectType);
    json.AddMember("$type", "ShortEvent", allocator);
    LibISDB::ARIBString event_name;
    if (desc->GetEventName(&event_name)) {
      json.AddMember("eventName", DecodeAribString(event_name), allocator);
    }
    LibISDB::ARIBString text;
    if (desc->GetEventDescription(&text)) {
      json.AddMember("text", DecodeAribString(text), allocator);
    }
    return json;
  }

  rapidjson::Value MakeJsonValue(
      const LibISDB::ComponentDescriptor* desc,
      rapidjson::Document::AllocatorType& allocator) const {
    rapidjson::Value json(rapidjson::kObjectType);
    json.AddMember("$type", "Component", allocator);
    json.AddMember("streamContent", desc->GetStreamContent(), allocator);
    json.AddMember("componentType", desc->GetComponentType(), allocator);
    json.AddMember("componentTag", desc->GetComponentTag(), allocator);
    json.AddMember("languageCode", desc->GetLanguageCode(), allocator);
    LibISDB::ARIBString text;
    if (desc->GetText(&text)) {
      json.AddMember("text", DecodeAribString(text), allocator);
    }
    return json;
  }

  rapidjson::Value MakeJsonValue(
      const LibISDB::ContentDescriptor* desc,
      rapidjson::Document::AllocatorType& allocator) const {
    rapidjson::Value nibbles(rapidjson::kArrayType);
    for (int i = 0; i < desc->GetNibbleCount(); ++i) {
      LibISDB::ContentDescriptor::NibbleInfo info;
      desc->GetNibble(i, &info);
      rapidjson::Value nibble(rapidjson::kArrayType);
      nibble.PushBack(info.ContentNibbleLevel1, allocator);
      nibble.PushBack(info.ContentNibbleLevel2, allocator);
      nibble.PushBack(info.UserNibble1, allocator);
      nibble.PushBack(info.UserNibble2, allocator);
      nibbles.PushBack(nibble, allocator);
    }
    rapidjson::Value json(rapidjson::kObjectType);
    json.AddMember("$type", "Content", allocator);
    json.AddMember("nibbles", nibbles, allocator);
    return json;
  }

  rapidjson::Value MakeJsonValue(
      const LibISDB::AudioComponentDescriptor* desc,
      rapidjson::Document::AllocatorType& allocator) const {
    rapidjson::Value json(rapidjson::kObjectType);
    json.AddMember("$type", "AudioComponent", allocator);
    json.AddMember("streamContent", desc->GetStreamContent(), allocator);
    json.AddMember("componentType", desc->GetComponentType(), allocator);
    json.AddMember("componentTag", desc->GetComponentTag(), allocator);
    json.AddMember("simulcastGroupTag", desc->GetSimulcastGroupTag(), allocator);
    json.AddMember(
        "esMultiLingualFlag", desc->GetESMultiLingualFlag(), allocator);
    json.AddMember(
        "mainComponentFlag", desc->GetMainComponentFlag(), allocator);
    json.AddMember("qualityIndicator", desc->GetQualityIndicator(), allocator);
    json.AddMember("samplingRate", desc->GetSamplingRate(), allocator);
    json.AddMember("languageCode", desc->GetLanguageCode(), allocator);
    if (desc->GetESMultiLingualFlag()) {
      json.AddMember("languageCode2", desc->GetLanguageCode2(), allocator);
    }
    LibISDB::ARIBString text;
    if (desc->GetText(&text)) {
      json.AddMember("text", DecodeAribString(text), allocator);
    }
    return json;
  }

  rapidjson::Value MakeJsonValue(
      const LibISDB::String& desc, const LibISDB::String& item,
      rapidjson::Document::AllocatorType& allocator) const {
    rapidjson::Value json(rapidjson::kArrayType);
    json.PushBack(rapidjson::Value().SetString(desc, allocator), allocator);
    json.PushBack(rapidjson::Value().SetString(item, allocator), allocator);
    return json;
  }

  rapidjson::Value MakeExtendedEventJsonValue(
      const LibISDB::DescriptorBlock& desc_block,
      rapidjson::Document::AllocatorType& allocator) const {
    LibISDB::ARIBStringDecoder decoder;
    auto flags = GetAribStringDecodeFlag();
    LibISDB::EventInfo::ExtendedTextInfoList ext_list;
    if (!LibISDB::GetEventExtendedTextList(
            &desc_block, decoder, flags, &ext_list)) {
      return rapidjson::Value();
    }

    rapidjson::Value items(rapidjson::kArrayType);
    for (const auto& ext : ext_list) {
      auto json = MakeJsonValue(ext.Description, ext.Text, allocator);
      items.PushBack(json, allocator);
    }

    rapidjson::Value json(rapidjson::kObjectType);
    json.AddMember("$type", "ExtendedEvent", allocator);
    json.AddMember("items", items, allocator);
    return json;
  }

  inline LibISDB::String DecodeAribString(
      const LibISDB::ARIBString& str) const {
    LibISDB::ARIBStringDecoder decoder;
    LibISDB::String utf8;
    decoder.Decode(str, &utf8, GetAribStringDecodeFlag());
    return std::move(utf8);
  }

  inline LibISDB::ARIBStringDecoder::DecodeFlag GetAribStringDecodeFlag()
      const {
    auto flags = LibISDB::ARIBStringDecoder::DecodeFlag::UseCharSize;
    if (g_KeepUnicodeSymbols) {
      flags |= LibISDB::ARIBStringDecoder::DecodeFlag::UnicodeSymbol;
    }
    return flags;
  }
};

}  // namespace

// Including <fmt/ostream.h> doesn't work as we expected...
template <>
struct fmt::formatter<ts::Time> {
  constexpr auto parse(format_parse_context& ctx) {
    return ctx.end();
  }

  template <typename Context>
  auto format(const ts::Time& time, Context& ctx) {
    return format_to(ctx.out(), "{}", time.format().toUTF8());
  }
};
