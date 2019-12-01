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

}  // namespace
