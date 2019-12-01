#pragma once

#include <cerrno>
#include <cstring>

#include "base.hh"
#include "logging.hh"

namespace {

class StreamSink {
 public:
  StreamSink() = default;
  virtual ~StreamSink() = default;
  virtual void Start() {}
  virtual bool End() { return true; }
  virtual bool Write(const uint8_t* data, size_t size) = 0;

 private:
  MIRAKC_ARIB_NON_COPYABLE(StreamSink);
};

class StdoutSink final : public StreamSink {
 public:
  StdoutSink() = default;
  ~StdoutSink() override {}

  bool Write(const uint8_t* data, size_t size) override {
    size_t nwritten = 0;
    do {
      ssize_t res = write(kStdoutFd, data + nwritten, size - nwritten);
      if (res < 0) {
        MIRAKC_ARIB_ERROR(
            "Failed to write data: {} ({})", std::strerror(errno), errno);
        return false;
      }
      nwritten += res;
    } while (nwritten < size);
    return true;
  }

 private:
  static constexpr int kStdoutFd = 1;

  MIRAKC_ARIB_NON_COPYABLE(StdoutSink);
};

}  // namespace
