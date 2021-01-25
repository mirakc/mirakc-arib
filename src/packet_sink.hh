#pragma once

#include <cerrno>
#include <cstring>

#include <tsduck/tsduck.h>

#include "base.hh"
#include "logging.hh"

namespace {

class PacketSink {
 public:
  PacketSink() = default;
  virtual ~PacketSink() = default;
  virtual bool Start() { return true; }
  virtual bool End() { return true; }
  virtual bool HandlePacket(const ts::TSPacket& packet) = 0;

 private:
  MIRAKC_ARIB_NON_COPYABLE(PacketSink);
};

class StdoutSink final : public PacketSink {
 public:
  StdoutSink() = default;
  ~StdoutSink() override {}

  bool End() override {
    return Flush();
  }

  bool HandlePacket(const ts::TSPacket& packet) override {
    if (pos_ + ts::PKT_SIZE < kBufferSize) {
      std::memcpy(buf_ + pos_, packet.b, ts::PKT_SIZE);
      pos_ += ts::PKT_SIZE;
      return true;
    }

    auto remaining = kBufferSize - pos_;
    std::memcpy(buf_ + pos_, packet.b, remaining);
    pos_ += remaining;
    MIRAKC_ARIB_ASSERT(pos_ == kBufferSize);
    if (!Flush()) {
      return false;
    }

    if (ts::PKT_SIZE > remaining) {
      MIRAKC_ARIB_ASSERT(pos_ == 0);
      std::memcpy(buf_, packet.b + remaining, ts::PKT_SIZE - remaining);
      pos_ = ts::PKT_SIZE - remaining;
    }

    return true;
  }

 private:
  static constexpr int kStdoutFd = 1;

  // 4 pages for the write buffer.
  // 16 pages for pipe in Linux by default.
  // See https://man7.org/linux/man-pages/man7/pipe.7.html
  static constexpr size_t kBufferSize = 4096 * 4;

  bool Flush() {
    size_t nwritten = 0;
    while (nwritten < pos_) {
      auto res = write(kStdoutFd, buf_ + nwritten, pos_ - nwritten);
      if (res < 0) {
        MIRAKC_ARIB_ERROR(
            "Failed to write packets: {} ({})", std::strerror(errno), errno);
        return false;
      }
      nwritten += res;
    }
    MIRAKC_ARIB_ASSERT(nwritten == pos_);
    pos_ = 0;
    return true;
  }

  uint8_t buf_[kBufferSize];
  size_t pos_ = 0;

  MIRAKC_ARIB_NON_COPYABLE(StdoutSink);
};

}  // namespace
