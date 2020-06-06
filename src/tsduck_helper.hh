#pragma once

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

}  // namespace
