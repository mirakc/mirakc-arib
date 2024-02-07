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
constexpr int64_t kMaxPcr = ((static_cast<int64_t>(1) << 33) - 1) * kMaxPcrExt + (kMaxPcrExt - 1);
static_assert(kMaxPcr == static_cast<int64_t>(2576980377599));
constexpr int64_t kPcrUpperBound = kMaxPcr + 1;
constexpr int64_t kPcrTicksPerSec = 27 * 1000 * 1000;  // 27MHz
constexpr int64_t kPcrTicksPerMs = kPcrTicksPerSec / ts::MilliSecPerSec;

inline ts::Time ConvertUnixTimeToJstTime(ts::MilliSecond unix_time_ms) {
  return ts::Time::UnixEpoch + (kJstTzOffset + unix_time_ms);
}

inline ts::MilliSecond ConvertJstTimeToUnixTime(ts::Time jst_time) {
  return jst_time - ts::Time::UnixEpoch - kJstTzOffset;
}

inline bool CheckComponentTagByRange(const ts::PMT::Stream& stream, uint8_t min, uint8_t max) {
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

  EitSection() = default;

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
    return static_cast<uint64_t>(nid) << 48 | static_cast<uint64_t>(tsid) << 32 |
        static_cast<uint32_t>(sid) << 16;
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
};

inline LibISDB::ARIBStringDecoder::DecodeFlag GetAribStringDecodeFlag() {
  auto flags = LibISDB::ARIBStringDecoder::DecodeFlag::UseCharSize;
  if (g_KeepUnicodeSymbols) {
    flags |= LibISDB::ARIBStringDecoder::DecodeFlag::UnicodeSymbol;
  }
  return flags;
}

inline LibISDB::String DecodeAribString(const LibISDB::ARIBString& str) {
  LibISDB::ARIBStringDecoder decoder;
  LibISDB::String utf8;
  decoder.Decode(str, &utf8, GetAribStringDecodeFlag());
  return std::move(utf8);
}

rapidjson::Value MakeJsonValue(
    const LibISDB::ShortEventDescriptor* desc, rapidjson::Document::AllocatorType& allocator) {
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
    const LibISDB::ComponentDescriptor* desc, rapidjson::Document::AllocatorType& allocator) {
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
    const LibISDB::ContentDescriptor* desc, rapidjson::Document::AllocatorType& allocator) {
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
    const LibISDB::AudioComponentDescriptor* desc, rapidjson::Document::AllocatorType& allocator) {
  rapidjson::Value json(rapidjson::kObjectType);
  json.AddMember("$type", "AudioComponent", allocator);
  json.AddMember("streamContent", desc->GetStreamContent(), allocator);
  json.AddMember("componentType", desc->GetComponentType(), allocator);
  json.AddMember("componentTag", desc->GetComponentTag(), allocator);
  json.AddMember("simulcastGroupTag", desc->GetSimulcastGroupTag(), allocator);
  json.AddMember("esMultiLingualFlag", desc->GetESMultiLingualFlag(), allocator);
  json.AddMember("mainComponentFlag", desc->GetMainComponentFlag(), allocator);
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
    const LibISDB::SeriesDescriptor* desc, rapidjson::Document::AllocatorType& allocator) {
  rapidjson::Value json(rapidjson::kObjectType);
  json.AddMember("$type", "Series", allocator);
  json.AddMember("seriesId", desc->GetSeriesID(), allocator);
  json.AddMember("repeatLabel", desc->GetRepeatLabel(), allocator);
  json.AddMember("programPattern", desc->GetProgramPattern(), allocator);
  LibISDB::DateTime expire_date;
  if (desc->GetExpireDate(&expire_date)) {
    json.AddMember(
        "expireDate", static_cast<uint64_t>(expire_date.GetLinearMilliseconds()), allocator);
  }
  json.AddMember("episodeNumber", desc->GetEpisodeNumber(), allocator);
  json.AddMember("lastEpisodeNumber", desc->GetLastEpisodeNumber(), allocator);
  LibISDB::ARIBString series_name;
  if (desc->GetSeriesName(&series_name)) {
    json.AddMember("seriesName", DecodeAribString(series_name), allocator);
  }
  return json;
}

rapidjson::Value MakeJsonValue(uint8_t group_type,
    const LibISDB::EventGroupDescriptor::EventInfo& info,
    rapidjson::Document::AllocatorType& allocator) {
  rapidjson::Value json(rapidjson::kObjectType);
  switch (group_type) {
    case LibISDB::EventGroupDescriptor::GROUP_TYPE_RELAY_TO_OTHER_NETWORK:
    case LibISDB::EventGroupDescriptor::GROUP_TYPE_MOVEMENT_FROM_OTHER_NETWORK:
      json.AddMember("originalNetworkId", info.NetworkID, allocator);
      json.AddMember("transportStreamId", info.TransportStreamID, allocator);
      break;
    default:
      break;
  }
  json.AddMember("serviceId", info.ServiceID, allocator);
  json.AddMember("eventId", info.EventID, allocator);
  return json;
}

rapidjson::Value MakeJsonValue(
    const LibISDB::EventGroupDescriptor* desc, rapidjson::Document::AllocatorType& allocator) {
  rapidjson::Value json(rapidjson::kObjectType);
  json.AddMember("$type", "EventGroup", allocator);
  json.AddMember("groupType", desc->GetGroupType(), allocator);
  rapidjson::Value events(rapidjson::kArrayType);
  for (int i = 0; i < desc->GetEventCount(); ++i) {
    LibISDB::EventGroupDescriptor::EventInfo info;
    (void)desc->GetEventInfo(i, &info);
    auto event = MakeJsonValue(desc->GetGroupType(), info, allocator);
    events.PushBack(event, allocator);
  }
  json.AddMember("events", events, allocator);
  return json;
}

rapidjson::Value MakeJsonValue(const LibISDB::String& desc, const LibISDB::String& item,
    rapidjson::Document::AllocatorType& allocator) {
  rapidjson::Value json(rapidjson::kArrayType);
  json.PushBack(rapidjson::Value().SetString(desc, allocator), allocator);
  json.PushBack(rapidjson::Value().SetString(item, allocator), allocator);
  return json;
}

rapidjson::Value MakeJsonValue(const LibISDB::ARIBString& desc, const LibISDB::ARIBString& item,
    rapidjson::Document::AllocatorType& allocator) {
  return MakeJsonValue(DecodeAribString(desc), DecodeAribString(item), allocator);
}

rapidjson::Value MakeJsonValue(const ts::ByteBlock& desc, const ts::ByteBlock& item,
    rapidjson::Document::AllocatorType& allocator) {
  return MakeJsonValue(LibISDB::ARIBString(desc.data(), desc.size()),
      LibISDB::ARIBString(item.data(), item.size()), allocator);
}

inline bool HasExtendedEventItems(const ts::DescriptorList& descs) {
  return descs.search(ts::DID_EXTENDED_EVENT) != descs.count();
}

rapidjson::Value MakeExtendedEventJsonValue(
    const ts::DescriptorList& descs, rapidjson::Document::AllocatorType& allocator) {
  rapidjson::Value items(rapidjson::kArrayType);

  ts::ByteBlock eed_desc;
  ts::ByteBlock eed_item;

  for (size_t i = 0; i < descs.size(); ++i) {
    auto& dp = descs[i];
    if (!dp->isValid()) {
      continue;
    }
    if (dp->tag() != ts::DID_EXTENDED_EVENT) {
      continue;
    }

    // Extract metadata from `dp` directly without using
    // ts::ExtendedEventdescriptor.  Because we need to decode a string
    // after concatenating subsequent fragments of the string.

    if (dp->payloadSize() < 5) {
      continue;
    }

    const uint8_t* data = dp->payload();
    size_t remaining = data[4];
    data += 5;
    while (remaining >= 2) {
      size_t desc_len = std::min<size_t>(data[0], remaining - 1);
      data += 1;
      remaining -= 1;
      if (desc_len > 0) {
        if (!eed_desc.empty()) {
          auto json = MakeJsonValue(eed_desc, eed_item, allocator);
          items.PushBack(json, allocator);
          eed_desc.clear();
          eed_item.clear();
        }
        eed_desc.append(data, desc_len);
        data += desc_len;
        remaining -= desc_len;
      }
      if (remaining <= 0) {
        break;
      }
      size_t item_len = std::min<size_t>(data[0], remaining - 1);
      data += 1;
      remaining -= 1;
      if (item_len > 0) {
        eed_item.append(data, item_len);
        data += item_len;
        remaining -= item_len;
      }
    }
  }

  if (!eed_desc.empty()) {
    auto json = MakeJsonValue(eed_desc, eed_item, allocator);
    items.PushBack(json, allocator);
  }

  rapidjson::Value json(rapidjson::kObjectType);
  json.AddMember("$type", "ExtendedEvent", allocator);
  json.AddMember("items", items, allocator);
  return json;
}

rapidjson::Value MakeExtendedEventJsonValue(
    const LibISDB::DescriptorBlock& desc_block, rapidjson::Document::AllocatorType& allocator) {
  LibISDB::ARIBStringDecoder decoder;
  auto flags = GetAribStringDecodeFlag();
  LibISDB::EventInfo::ExtendedTextInfoList ext_list;
  if (!LibISDB::GetEventExtendedTextList(&desc_block, decoder, flags, &ext_list)) {
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

rapidjson::Value MakeJsonValue(
    const ts::DescriptorList& descs, rapidjson::Document::AllocatorType& allocator) {
  rapidjson::Value descriptors(rapidjson::kArrayType);
  for (size_t i = 0; i < descs.size(); ++i) {
    const auto& dp = descs[i];
    if (!dp->isValid()) {
      continue;
    }
    switch (dp->tag()) {
      case LibISDB::ShortEventDescriptor::TAG: {
        LibISDB::ShortEventDescriptor desc;
        if (desc.Parse(dp->content(), dp->size())) {
          auto json = MakeJsonValue(&desc, allocator);
          descriptors.PushBack(json, allocator);
        }
        break;
      }
      case LibISDB::ComponentDescriptor::TAG: {
        LibISDB::ComponentDescriptor desc;
        if (desc.Parse(dp->content(), dp->size())) {
          auto json = MakeJsonValue(&desc, allocator);
          descriptors.PushBack(json, allocator);
        }
        break;
      }
      case LibISDB::ContentDescriptor::TAG: {
        LibISDB::ContentDescriptor desc;
        if (desc.Parse(dp->content(), dp->size())) {
          auto json = MakeJsonValue(&desc, allocator);
          descriptors.PushBack(json, allocator);
        }
        break;
      }
      case LibISDB::AudioComponentDescriptor::TAG: {
        LibISDB::AudioComponentDescriptor desc;
        if (desc.Parse(dp->content(), dp->size())) {
          auto json = MakeJsonValue(&desc, allocator);
          descriptors.PushBack(json, allocator);
        }
        break;
      }
      case LibISDB::SeriesDescriptor::TAG: {
        LibISDB::SeriesDescriptor desc;
        if (desc.Parse(dp->content(), dp->size())) {
          auto json = MakeJsonValue(&desc, allocator);
          descriptors.PushBack(json, allocator);
        }
        break;
      }
      case LibISDB::EventGroupDescriptor::TAG: {
        LibISDB::EventGroupDescriptor desc;
        if (desc.Parse(dp->content(), dp->size())) {
          auto json = MakeJsonValue(&desc, allocator);
          descriptors.PushBack(json, allocator);
        }
        break;
      }
      default:
        break;
    }
  }
  return descriptors;
}

rapidjson::Value MakeJsonValue(
    const ts::EIT::Event& event, rapidjson::Document::AllocatorType& allocator) {
  const auto eid = event.event_id;
  const auto start_time_unix = ConvertJstTimeToUnixTime(event.start_time);
  const ts::MilliSecond duration = event.duration * ts::MilliSecPerSec;
  const bool ca_controlled = event.CA_controlled;
  auto descriptors = MakeJsonValue(event.descs, allocator);
  rapidjson::Value value(rapidjson::kObjectType);
  value.AddMember("eventId", eid, allocator);
  value.AddMember("startTime", start_time_unix, allocator);
  value.AddMember("duration", duration, allocator);
  value.AddMember("scrambled", ca_controlled, allocator);
  value.AddMember("descriptors", descriptors, allocator);
  return value;
}

template <typename Allocator>
rapidjson::Value MakeEventsJsonValue(const EitSection& eit, Allocator& allocator) {
  const auto* data = eit.events_data;
  auto remain = eit.events_size;

  rapidjson::Value events(rapidjson::kArrayType);

  while (remain >= EitSection::EIT_EVENT_FIXED_SIZE) {
    const auto eid = ts::GetUInt16(data);

    ts::Time start_time;
    const auto start_time_undefined = !ts::DecodeMJD(data + 2, 5, start_time);
    const auto start_time_unix = ConvertJstTimeToUnixTime(start_time);

    bool duration_undefined = false;
    duration_undefined = duration_undefined || !ts::IsValidBCD(data[7]);
    duration_undefined = duration_undefined || !ts::IsValidBCD(data[8]);
    duration_undefined = duration_undefined || !ts::IsValidBCD(data[9]);
    const auto hour = ts::DecodeBCD(data[7]);
    const auto min = ts::DecodeBCD(data[8]);
    const auto sec = ts::DecodeBCD(data[9]);
    const ts::MilliSecond duration =
        hour * ts::MilliSecPerHour + min * ts::MilliSecPerMin + sec * ts::MilliSecPerSec;

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
          const auto* desc = static_cast<const LibISDB::ShortEventDescriptor*>(dp);
          auto json = MakeJsonValue(desc, allocator);
          descriptors.PushBack(json, allocator);
          break;
        }
        case LibISDB::ComponentDescriptor::TAG: {
          const auto* desc = static_cast<const LibISDB::ComponentDescriptor*>(dp);
          auto json = MakeJsonValue(desc, allocator);
          descriptors.PushBack(json, allocator);
          break;
        }
        case LibISDB::ContentDescriptor::TAG: {
          const auto* desc = static_cast<const LibISDB::ContentDescriptor*>(dp);
          auto json = MakeJsonValue(desc, allocator);
          descriptors.PushBack(json, allocator);
          break;
        }
        case LibISDB::AudioComponentDescriptor::TAG: {
          const auto* desc = static_cast<const LibISDB::AudioComponentDescriptor*>(dp);
          auto json = MakeJsonValue(desc, allocator);
          descriptors.PushBack(json, allocator);
          break;
        }
        case LibISDB::SeriesDescriptor::TAG: {
          const auto* desc = static_cast<const LibISDB::SeriesDescriptor*>(dp);
          auto json = MakeJsonValue(desc, allocator);
          descriptors.PushBack(json, allocator);
          break;
        }
        case LibISDB::EventGroupDescriptor::TAG: {
          const auto* desc = static_cast<const LibISDB::EventGroupDescriptor*>(dp);
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
    if (start_time_undefined) {
      // null
      event.AddMember("startTime", rapidjson::Value(), allocator);
    } else {
      event.AddMember("startTime", start_time_unix, allocator);
    }
    if (duration_undefined) {
      // null
      event.AddMember("duration", rapidjson::Value(), allocator);
    } else {
      event.AddMember("duration", duration, allocator);
    }
    event.AddMember("scrambled", ca_controlled, allocator);
    event.AddMember("descriptors", descriptors, allocator);

    events.PushBack(event, allocator);

    data += info_length;
    remain -= info_length;
  }

  return events;
}

rapidjson::Document MakeJsonValue(const EitSection& eit) {
  rapidjson::Document eit_json(rapidjson::kObjectType);
  auto& allocator = eit_json.GetAllocator();

  auto events = MakeEventsJsonValue(eit, allocator);

  eit_json.AddMember("originalNetworkId", eit.nid, allocator);
  eit_json.AddMember("transportStreamId", eit.tsid, allocator);
  eit_json.AddMember("serviceId", eit.sid, allocator);
  eit_json.AddMember("tableId", eit.tid, allocator);
  eit_json.AddMember("sectionNumber", eit.section_number, allocator);
  eit_json.AddMember("lastSectionNumber", eit.last_section_number, allocator);
  eit_json.AddMember("segmentLastSectionNumber", eit.segment_last_section_number, allocator);
  eit_json.AddMember("versionNumber", eit.version, allocator);
  eit_json.AddMember("events", events, allocator);

  return eit_json;
}

bool IsAudioVideoService(uint8_t service_type) {
  switch (service_type) {
    case 0x01:
    case 0x02:
    case 0xA1:
    case 0xA2:
    case 0xA5:
    case 0xA6:
    case 0xAD:
      return true;
    default:
      return false;
  }
}

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
