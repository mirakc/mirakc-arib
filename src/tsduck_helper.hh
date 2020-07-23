#pragma once

#include <string>

#include <fmt/format.h>
#include <tsduck/tsduck.h>

namespace {

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

std::string FormatPcr(int64_t pcr) {
  auto base = pcr / 300;
  auto ext = pcr % 300;
  return fmt::format("{:010d}+{:03d}", base, ext);
}

// Compares two PCR values taking into account the PCR wrap around.
//
// Assumed that the real interval time between the PCR values is less than half
// of kPcrUpperBound.
inline int64_t ComparePcr(int64_t lhs, int64_t rhs) {
  auto a = lhs - rhs;
  auto b = lhs - (kPcrUpperBound + rhs);
  if (std::abs(a) < std::abs(b)) {
    return a;
  }
  return b;
}

}  // namespace
