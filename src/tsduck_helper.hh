#pragma once

#include <string>

#include <fmt/format.h>
#include <tsduck/tsduck.h>

#include "logging.hh"

namespace {

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
