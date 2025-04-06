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

#include <cerrno>
#include <cstdlib>
#include <cstring>

#include <tsduck/tsduck.h>

#include "base.hh"
#include "logging.hh"

namespace {

class PacketSink {
 public:
  PacketSink() = default;
  virtual ~PacketSink() = default;
  virtual bool Start() {
    return true;
  }
  virtual void End() {}
  virtual int GetExitCode() const {
    return EXIT_SUCCESS;
  }
  virtual bool HandlePacket(const ts::TSPacket& packet) = 0;

 private:
  MIRAKC_ARIB_NON_COPYABLE(PacketSink);
};

class PacketRingObserver {
 public:
  PacketRingObserver() = default;
  virtual ~PacketRingObserver() = default;
  virtual void OnEndOfChunk(uint64_t pos) = 0;

 private:
  MIRAKC_ARIB_NON_COPYABLE(PacketRingObserver);
};

class PacketRingSink : public PacketSink {
 public:
  PacketRingSink() = default;
  ~PacketRingSink() override = default;
  virtual uint64_t ring_size() const = 0;
  virtual uint64_t pos() const = 0;
  virtual bool SetPosition(uint64_t pos) = 0;
  virtual void SetObserver(PacketRingObserver* observer) = 0;
  virtual bool IsBroken() const {
    return false;
  }

 private:
  MIRAKC_ARIB_NON_COPYABLE(PacketRingSink);
};

class StdoutSink final : public PacketSink {
 public:
  StdoutSink() = default;
  ~StdoutSink() override {}

  void End() override {
    (void)Flush();
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
        MIRAKC_ARIB_ERROR("Failed to write packets: {} ({})", std::strerror(errno), errno);
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
