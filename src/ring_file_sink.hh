#pragma once

#include <algorithm>
#include <limits>
#include <memory>

#include <tsduck/tsduck.h>

#include "base.hh"
#include "file.hh"
#include "logging.hh"
#include "packet_sink.hh"

namespace {

class RingFileSink final : public PacketRingSink {
 public:
  RingFileSink(std::unique_ptr<File>&& file, size_t chunk_size, size_t num_chunks)
      : file_(std::move(file)),
        chunk_size_(chunk_size),
        ring_size_(chunk_size * num_chunks) {
    MIRAKC_ARIB_ASSERT(chunk_size > 0);
    MIRAKC_ARIB_ASSERT(num_chunks > 0);
    MIRAKC_ARIB_ASSERT_MSG(
        chunk_size % kBufferSize == 0,
        "The chunk size must be a multiple of the buffer size");
  }

  ~RingFileSink() override = default;

  static constexpr size_t kBufferSize = 2 * kBlockSize;

  bool End() override {
    if (broken_) {
      return false;
    }
    if (buf_pos_ == 0 && chunk_pos_ == 0) {
      // No free space in the last chunk.
      return true;
    }
    return ZeroizeFreeSpaceInLastChunk();
  }

  bool HandlePacket(const ts::TSPacket& packet) override {
    size_t nwritten = 0;

    do {
      nwritten += FillBuffer(packet.b + nwritten, ts::PKT_SIZE - nwritten);
      if (NeedFlush()) {
        if (!Flush()) {
          broken_ = true;
          return false;
        }
      }
    } while (nwritten < ts::PKT_SIZE);
    MIRAKC_ARIB_ASSERT(nwritten == ts::PKT_SIZE);

    return true;
  }

  uint64_t pos() const override {
    return ring_pos_;
  }

  uint64_t sync_pos() const override {
    return (ring_pos_ / chunk_size_) * chunk_size_;
  }

  bool SetPosition(uint64_t pos) override {
    MIRAKC_ARIB_ASSERT(pos <= std::numeric_limits<int64_t>::max());
    MIRAKC_ARIB_ASSERT(pos % kBufferSize == 0);
    MIRAKC_ARIB_ASSERT_MSG(
        pos < ring_size_,
        "The position must be smaller than the ring buffer size");
    MIRAKC_ARIB_ASSERT_MSG(
        pos % chunk_size_ == 0,
        "The position must be a multiple of the chunk size");

    int64_t offset = static_cast<int64_t>(pos);
    if (file_->Seek(offset, SeekMode::kSet) != offset) {
      return false;
    }

    buf_pos_ = 0;
    ring_pos_ = pos;
    chunk_pos_ = 0;
    return true;
  }

  void SetObserver(PacketRingObserver* observer) override {
    MIRAKC_ARIB_ASSERT(observer != nullptr);
    observer_ = observer;
  }

 private:
  bool ZeroizeFreeSpaceInLastChunk() {
    MIRAKC_ARIB_DEBUG("Zeroize {} bytes of free space in the last chunk",
                      chunk_size_ - chunk_pos_);
    auto zero_bytes = free_bytes();

    // Zeroize free space in the buffer.
    std::memset(buf_ + buf_pos_, 0, zero_bytes);
    buf_pos_ += zero_bytes;
    MIRAKC_ARIB_ASSERT(buf_pos_ == kBufferSize);
    ring_pos_ += zero_bytes;
    MIRAKC_ARIB_ASSERT(ring_pos_ <= ring_size_);
    MIRAKC_ARIB_ASSERT(NeedFlush());
    if (!Flush()) {
      return false;
    }

    MIRAKC_ARIB_ASSERT(buf_pos_ == 0);

    // Zeroize the buffer.
    std::memset(buf_, 0, kBufferSize - zero_bytes);

    while (chunk_pos_ != 0) {
      // The buffer has already been zeroized.
      // Just move the cursors.
      buf_pos_ = kBufferSize;
      ring_pos_ += kBufferSize;
      MIRAKC_ARIB_ASSERT(ring_pos_ <= ring_size_);
      MIRAKC_ARIB_ASSERT(NeedFlush());
      if (!Flush()) {
        return false;
      }
    }

    return true;
  }

  size_t FillBuffer(const uint8_t* data, size_t size) {
    auto fill_bytes = std::min(size, free_bytes());
    std::memcpy(buf_ + buf_pos_, data, fill_bytes);
    buf_pos_ += fill_bytes;
    MIRAKC_ARIB_ASSERT(buf_pos_ <= kBufferSize);
    ring_pos_ += fill_bytes;
    MIRAKC_ARIB_ASSERT(ring_pos_ <= ring_size_);
    return fill_bytes;
  }

  bool NeedFlush() const {
    return buf_pos_ == kBufferSize;
  }

  bool Flush() {
    MIRAKC_ARIB_ASSERT(buf_pos_ == kBufferSize);

    size_t nwritten = 0;

    while (nwritten < kBufferSize) {
      MIRAKC_ARIB_TRACE("Write the buffer to {}", file_->path());
      auto result = file_->Write(buf_ + nwritten, kBufferSize - nwritten);
      if (result <= 0) {
        return false;
      }
      nwritten += result;
    }
    MIRAKC_ARIB_ASSERT(nwritten == kBufferSize);

    buf_pos_ = 0;

    chunk_pos_ += nwritten;
    MIRAKC_ARIB_ASSERT(chunk_pos_ <= chunk_size_);

    if (chunk_pos_ != 0 && chunk_pos_ == chunk_size_) {
      MIRAKC_ARIB_DEBUG("Reached the chunk boundary {}, sync {}", ring_pos_, file_->path());
      if (!file_->Sync()) {
        return false;
      }
      chunk_pos_ = 0;
      if (observer_ != nullptr) {
        observer_->OnChunkFlushed(ring_pos_, chunk_size_, ring_size_);
      }
    }

    if (ring_pos_ == ring_size_) {
      MIRAKC_ARIB_DEBUG("Reached the end of the ring buffer, truncate {} at {}",
                        file_->path(), ring_pos_);
      if (!file_->Trunc(ring_size_)) {
        return false;
      }
      MIRAKC_ARIB_DEBUG("Reset the position");
      if (file_->Seek(0, SeekMode::kSet) != 0) {
        return false;
      }
      ring_pos_ = 0;
      if (observer_ != nullptr) {
        observer_->OnWrappedAround(ring_size_);
      }
    }

    return true;
  }

  size_t free_bytes() const {
    return kBufferSize - buf_pos_;
  }

  uint8_t buf_[kBufferSize];
  std::unique_ptr<File> file_;
  PacketRingObserver* observer_ = nullptr;
  const uint64_t ring_size_;
  uint64_t ring_pos_ = 0;
  const size_t chunk_size_;
  size_t buf_pos_ = 0;
  size_t chunk_pos_ = 0;
  bool broken_ = false;

  MIRAKC_ARIB_NON_COPYABLE(RingFileSink);
};

}  // namespace
