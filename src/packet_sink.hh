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
  virtual void Start() {}
  virtual bool End() { return true; }
  virtual bool HandlePacket(const ts::TSPacket& packet) = 0;

 private:
  MIRAKC_ARIB_NON_COPYABLE(PacketSink);
};

class StdoutSink final : public PacketSink {
 public:
  StdoutSink() = default;
  ~StdoutSink() override {}

  bool HandlePacket(const ts::TSPacket& packet) override {
    size_t nwritten = 0;
    while (nwritten < ts::PKT_SIZE) {
      ssize_t res =
          write(kStdoutFd, packet.b + nwritten, ts::PKT_SIZE - nwritten);
      if (res < 0) {
        MIRAKC_ARIB_ERROR(
            "Failed to write packet: {} ({})", std::strerror(errno), errno);
        return false;
      }
      nwritten += res;
    }
    return true;
  }

 private:
  static constexpr int kStdoutFd = 1;

  MIRAKC_ARIB_NON_COPYABLE(StdoutSink);
};

}  // namespace
