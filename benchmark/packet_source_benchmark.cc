// Copyright (c) 2019 Masayuki Nagamachi <masayuki.nagamachi@gmail.com>
//
// Licensed under either of
//
//   * Apache License, Version 2.0
//     (LICENSE-APACHE or http://www.apache.org/licenses/LICENSE-2.0)
//   * MIT License
//     (LICENSE-MIT or http://opensource.org/licenses/MIT)
//
// at your option.

#include <algorithm>
#include <memory>

#include <benchmark/benchmark.h>

#include "packet_source.hh"

namespace {

class BenchmarkFile final : public File {
 public:
  static constexpr size_t kNumPackets = 10000;

  BenchmarkFile() {
    for (size_t i = 0; i < kBufSize; i += ts::PKT_SIZE) {
      ts::NullPacket.copyTo(reinterpret_cast<void*>(&buf_[i]));
    }
  }

  ~BenchmarkFile() override {}

  ssize_t Read(uint8_t* buf, size_t len) override {
    auto remaining = kNumPackets - nread_;
    if (remaining == 0) {
      return 0;
    }
    auto ncopy = std::min(len, remaining);
    std::memcpy(buf, &buf_[nread_], ncopy);
    nread_ += ncopy;
    return static_cast<ssize_t>(ncopy);
  }

 private:
  static constexpr size_t kBufSize = ts::PKT_SIZE * kNumPackets;
  size_t nread_ = 0;
  uint8_t buf_[kBufSize];
};

class BenchmarkSink final : public PacketSink {
 public:
  BenchmarkSink() = default;
  ~BenchmarkSink() override = default;
  bool HandlePacket(const ts::TSPacket&) override { return true; }
};

void BM_FileSource(benchmark::State& state) {
  auto file = std::make_unique<BenchmarkFile>();
  auto sink = std::make_unique<BenchmarkSink>();
  FileSource src(std::move(file));
  src.Connect(std::move(sink));
  for (auto _ : state) {
    src.FeedPackets();
  }
  state.SetItemsProcessed(
      BenchmarkFile::kNumPackets * static_cast<int64_t>(state.iterations()));
}

}  // namespace

BENCHMARK(BM_FileSource);
