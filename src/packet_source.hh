// SPDX-License-Identifier: GPL-2.0-or-later

// mirakc-arib
// Copyright (C) 2019 masnagam
//
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation; either version 2 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
// without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See
// the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with this program; if
// not, write to the Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
// 02110-1301, USA.

#pragma once

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include <tsduck/tsduck.h>

#include "base.hh"
#include "file.hh"
#include "logging.hh"
#include "packet_sink.hh"

namespace {

class PacketSource {
 public:
  PacketSource() = default;
  virtual ~PacketSource() = default;

  void Connect(std::unique_ptr<PacketSink>&& sink) {
    sink_ = std::move(sink);
  }

  int FeedPackets() {
    MIRAKC_ARIB_ASSERT(sink_ != nullptr);
    MIRAKC_ARIB_INFO("Feed packets...");
    if (!sink_->Start()) {
      MIRAKC_ARIB_ERROR("Failed to start");
      return EXIT_FAILURE;
    }
    ts::TSPacket packet;
    while (GetNextPacket(&packet)) {
      if (!sink_->HandlePacket(packet)) {
        break;
      }
    }
    sink_->End();
    auto exit_code = sink_->GetExitCode();
    MIRAKC_ARIB_INFO("Ended with exit-code({})", exit_code);
    return exit_code;
  }

 private:
  virtual bool GetNextPacket(ts::TSPacket* packet) = 0;

  std::unique_ptr<PacketSink> sink_;

  MIRAKC_ARIB_NON_COPYABLE(PacketSource);
};

// The ts::TSFileInput class works well on many platforms including Window.  But
// it doesn't support resync when synchronization is lost, unfortunately.
//
// Unlike the ts::TSFileInput class, the FileSource implement the resync.
class FileSource final : public PacketSource {
 public:
  static constexpr size_t kMaxDropBytes = 2 * ts::PKT_SIZE;
  static constexpr size_t kMaxResyncBytes = kMaxDropBytes + 3 * ts::PKT_SIZE;
  static constexpr size_t kReadChunkSize = 4 * kBlockSize;
  // TODO:
  // Constants above should be private, but doing that causes compile errors
  // like below at least on macOS:
  //
  //   ../src/packet_source.hh:58:41: error: use of undeclared identifier
  //   'kReadChunkSize'
  //
  static constexpr size_t kBufferSize = kReadChunkSize + kMaxResyncBytes;

  explicit FileSource(std::unique_ptr<File>&& file) : file_(std::move(file)) {}
  ~FileSource() override {}

 private:
  bool GetNextPacket(ts::TSPacket* packet) override {
    if (!FillBuffer(ts::PKT_SIZE)) {
      return false;
    }

    if (buf_[pos_] != ts::SYNC_BYTE) {
      MIRAKC_ARIB_WARN("Synchronization was lost");
      if (!Resync()) {
        return false;
      }
      MIRAKC_ARIB_ASSERT(buf_[pos_] == ts::SYNC_BYTE);
    }

    std::memcpy(packet->b, &buf_[pos_], ts::PKT_SIZE);
    pos_ += ts::PKT_SIZE;

    MIRAKC_ARIB_ASSERT(packet->hasValidSync());
    return true;
  }

  inline bool FillBuffer(size_t min_bytes) {
    MIRAKC_ARIB_ASSERT(min_bytes <= kMaxResyncBytes);
    MIRAKC_ARIB_ASSERT(!eof_);
    MIRAKC_ARIB_ASSERT(pos_ <= end_);
    MIRAKC_ARIB_ASSERT(end_ <= kBufferSize);

    auto avail_bytes = available_bytes();
    if (avail_bytes >= min_bytes) {
      return true;
    }

    std::memmove(&buf_[0], &buf_[pos_], avail_bytes);
    pos_ = 0;
    end_ = avail_bytes;

    do {
      MIRAKC_ARIB_ASSERT(free_bytes() >= kReadChunkSize);
      auto nread = file_->Read(&buf_[end_], kReadChunkSize);
      if (nread <= 0) {
        eof_ = true;
        MIRAKC_ARIB_INFO("EOF reached");
        return false;
      }
      end_ += nread;
    } while (end_ < min_bytes);

    return true;
  }

  inline bool Resync() {
    MIRAKC_ARIB_WARN("Resync...");

    if (!FillBuffer(kMaxResyncBytes)) {
      return false;
    }

    size_t resync_start = pos_;
    size_t resync_end = pos_ + kMaxDropBytes;

    while (pos_ < resync_end) {
      if (buf_[pos_] != ts::SYNC_BYTE) {
        pos_++;
        continue;
      }
      if (ValidateResync()) {
        MIRAKC_ARIB_WARN("Resynced, {} bytes dropped", pos_ - resync_start);
        return true;
      }
      pos_++;
    }

    MIRAKC_ARIB_ERROR("Resync failed");
    return false;
  }

  inline bool ValidateResync() const {
    return buf_[pos_ + 1 * ts::PKT_SIZE] == ts::SYNC_BYTE &&
        buf_[pos_ + 2 * ts::PKT_SIZE] == ts::SYNC_BYTE &&
        buf_[pos_ + 3 * ts::PKT_SIZE] == ts::SYNC_BYTE;
  }

  inline size_t available_bytes() const {
    return end_ - pos_;
  }

  inline size_t free_bytes() const {
    return kBufferSize - end_;
  }

  std::unique_ptr<File> file_;
  bool eof_ = false;
  uint8_t buf_[kBufferSize];
  size_t pos_ = 0;
  size_t end_ = 0;

  MIRAKC_ARIB_NON_COPYABLE(FileSource);
};

}  // namespace
