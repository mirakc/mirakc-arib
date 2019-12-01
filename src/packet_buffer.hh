#pragma once

#include <memory>

#include <tsduck/tsduck.h>

#include "base.hh"
#include "logging.hh"
#include "stream_sink.hh"

namespace {

class PacketBuffer final {
 public:
  explicit PacketBuffer(size_t buffer_size)
      : buffer_(new uint8_t[buffer_size]),
        buffer_size_(buffer_size),
        start_(0),
        size_(0) {}

  ~PacketBuffer() {}

  bool Write(const uint8_t* data, size_t size) {
    MIRAKC_ARIB_ASSERT(data != nullptr);

    if (buffer_size_ == 0) {
      return true;
    }

    auto end = start_ + size_;
    auto new_end = end + size;
    if (new_end <= buffer_size_) {
      std::memcpy(buffer_.get() + end, data, size);
      size_ += size;
      return true;
    }

    end = end % buffer_size_;

    auto remaining = buffer_size_ - end;
    if (size <= remaining) {
      std::memcpy(buffer_.get() + end, data, size);
    } else {
      std::memcpy(buffer_.get() + end, data, remaining);
      std::memcpy(buffer_.get(), data + remaining, size - remaining);
    }

    start_ = new_end % buffer_size_;
    size_ = buffer_size_;
    MIRAKC_ARIB_ASSERT(start_ < buffer_size_);
    return true;
  }

  bool Flush(StreamSink* sink) {
    if (buffer_size_ == 0) {
      return true;
    }

    MIRAKC_ARIB_DEBUG("Flush buffered {} bytes data to the packet sink", size_);

    MIRAKC_ARIB_ASSERT(start_ < buffer_size_);
    if (start_ + size_ < buffer_size_) {
      return sink->Write(buffer_.get() + start_, size_);
    }
    if (!sink->Write(buffer_.get() + start_, buffer_size_ - start_)) {
      return false;
    }
    return sink->Write(buffer_.get(), start_ + size_ - buffer_size_);
  }

 private:
  std::unique_ptr<uint8_t[]> buffer_;  // ring buffer
  size_t buffer_size_;
  size_t start_;
  size_t size_;

  MIRAKC_ARIB_NON_COPYABLE(PacketBuffer);
};

}  // namespace
