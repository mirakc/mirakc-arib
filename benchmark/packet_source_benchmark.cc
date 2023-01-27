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

  const std::string& path() const override {
    return path_;
  }

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

  ssize_t Write(uint8_t*, size_t) override {
    return 0;
  }

  bool Sync() override {
    return true;
  }

  bool Trunc(int64_t) override {
    return true;
  }

  int64_t Seek(int64_t, SeekMode) override {
    return 0;
  }

 private:
  static constexpr size_t kBufSize = ts::PKT_SIZE * kNumPackets;
  std::string path_ = "<benchmark>";
  size_t nread_ = 0;
  uint8_t buf_[kBufSize];
};

class BenchmarkSink final : public PacketSink {
 public:
  BenchmarkSink() = default;
  ~BenchmarkSink() override = default;
  bool HandlePacket(const ts::TSPacket&) override {
    return true;
  }
};

void BM_FileSource(benchmark::State& state) {
  auto file = std::make_unique<BenchmarkFile>();
  auto sink = std::make_unique<BenchmarkSink>();
  FileSource src(std::move(file));
  src.Connect(std::move(sink));
  for (auto _ : state) {
    src.FeedPackets();
  }
  state.SetItemsProcessed(BenchmarkFile::kNumPackets * static_cast<int64_t>(state.iterations()));
}

}  // namespace

BENCHMARK(BM_FileSource);
