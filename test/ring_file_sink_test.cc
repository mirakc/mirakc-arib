#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "packet_source.hh"
#include "ring_file_sink.hh"

#include "test_helper.hh"

namespace {

constexpr size_t kNumBuffers = 2;
constexpr size_t kNumChunks = 2;
constexpr size_t kChunkSize = RingFileSink::kBufferSize * kNumBuffers;
constexpr uint64_t kRingSize = kChunkSize * kNumChunks;

class MockPacketRingObserver final : public PacketRingObserver {
 public:
  MockPacketRingObserver() = default;
  ~MockPacketRingObserver() override = default;
  MOCK_METHOD(void, OnChunkFlushed, (uint64_t, size_t, uint64_t), (override));
  MOCK_METHOD(void, OnWrappedAround, (uint64_t), (override));
};

}  // namespace

TEST(RingFileSinkTest, EmptyFile) {
  auto file = std::make_unique<MockFile>();
  auto ring = std::make_unique<MockFile>();
  MockPacketRingObserver observer;

  {
    testing::InSequence seq;
    EXPECT_CALL(*file, Read).WillOnce(testing::Return(0));  // EOF
  }

  EXPECT_CALL(*ring, Write).Times(0);  // never called
  EXPECT_CALL(*ring, Sync).Times(0);  // never called
  EXPECT_CALL(*ring, Trunc).Times(0);  // never called
  EXPECT_CALL(*ring, Seek).Times(0);   // never called
  EXPECT_CALL(observer, OnChunkFlushed).Times(0);  // never called
  EXPECT_CALL(observer, OnWrappedAround).Times(0);  // never called

  FileSource src(std::move(file));
  auto sink = std::make_unique<RingFileSink>(std::move(ring), kChunkSize, kNumChunks);
  sink->SetObserver(&observer);
  src.Connect(std::move(sink));
  EXPECT_TRUE(src.FeedPackets());
}

TEST(RingFileSinkTest, OnePacket) {
  auto file = std::make_unique<MockFile>();
  auto ring = std::make_unique<MockFile>();
  MockPacketRingObserver observer;

  {
    testing::InSequence seq;
    EXPECT_CALL(*file, Read)
        .WillOnce([](uint8_t* buf, size_t) {
          ts::NullPacket.copyTo(buf);
          return ts::PKT_SIZE;
        });
    EXPECT_CALL(*file, Read)
        .WillOnce(testing::Return(0));  // EOF
    EXPECT_CALL(*ring, Write)
        .WillOnce([](uint8_t* buf, size_t size) {
          EXPECT_EQ(RingFileSink::kBufferSize, size);
          EXPECT_EQ(0, std::memcmp(buf, ts::NullPacket.b, ts::PKT_SIZE));
          return size;
        });
    EXPECT_CALL(*ring, Write)
        .Times(kNumBuffers - 1)
        .WillRepeatedly(testing::Return(RingFileSink::kBufferSize));
    EXPECT_CALL(*ring, Sync)
        .WillOnce(testing::Return(true));
    EXPECT_CALL(observer, OnChunkFlushed)
        .WillOnce([](uint64_t pos, size_t chunk_size, uint64_t ring_size) {
          EXPECT_EQ(kChunkSize, pos);
          EXPECT_EQ(kChunkSize, chunk_size);
          EXPECT_EQ(kRingSize, ring_size);
        });
  }

  EXPECT_CALL(*ring, Trunc).Times(0);  // never called
  EXPECT_CALL(*ring, Seek).Times(0);  // never called
  EXPECT_CALL(observer, OnWrappedAround).Times(0);  // never called

  FileSource src(std::move(file));
  auto sink = std::make_unique<RingFileSink>(std::move(ring), kChunkSize, kNumChunks);
  sink->SetObserver(&observer);
  src.Connect(std::move(sink));
  EXPECT_TRUE(src.FeedPackets());
}

TEST(RingFileSinkTest, TwoPackets) {
  auto file = std::make_unique<MockFile>();
  auto ring = std::make_unique<MockFile>();
  MockPacketRingObserver observer;

  {
    testing::InSequence seq;
    EXPECT_CALL(*file, Read)
        .Times(2)
        .WillRepeatedly([](uint8_t* buf, size_t) {
          ts::NullPacket.copyTo(buf);
          return ts::PKT_SIZE;
        });
    EXPECT_CALL(*file, Read)
        .WillOnce(testing::Return(0));  // EOF
    EXPECT_CALL(*ring, Write)
        .WillOnce([](uint8_t* buf, size_t size) {
          EXPECT_EQ(RingFileSink::kBufferSize, size);
          EXPECT_EQ(0, std::memcmp(buf, ts::NullPacket.b, ts::PKT_SIZE));
          EXPECT_EQ(0, std::memcmp(buf + ts::PKT_SIZE, ts::NullPacket.b, ts::PKT_SIZE));
          return size;
        });
    EXPECT_CALL(*ring, Write)
        .Times(kNumBuffers - 1)
        .WillRepeatedly(testing::Return(RingFileSink::kBufferSize));
    EXPECT_CALL(*ring, Sync)
        .WillOnce(testing::Return(true));
    EXPECT_CALL(observer, OnChunkFlushed)
        .WillOnce([](uint64_t pos, size_t chunk_size, uint64_t ring_size) {
          EXPECT_EQ(kChunkSize, pos);
          EXPECT_EQ(kChunkSize, chunk_size);
          EXPECT_EQ(kRingSize, ring_size);
        });
  }

  EXPECT_CALL(*ring, Trunc).Times(0);  // never called
  EXPECT_CALL(*ring, Seek).Times(0);  // never called
  EXPECT_CALL(observer, OnWrappedAround).Times(0);  // never called

  FileSource src(std::move(file));
  auto sink = std::make_unique<RingFileSink>(std::move(ring), kChunkSize, kNumChunks);
  sink->SetObserver(&observer);
  src.Connect(std::move(sink));
  EXPECT_TRUE(src.FeedPackets());
}

TEST(RingFileSinkTest, ReachBufferSize) {
  constexpr auto kNumPackets = RingFileSink::kBufferSize / ts::PKT_SIZE + 1;

  auto file = std::make_unique<MockFile>();
  auto ring = std::make_unique<MockFile>();
  MockPacketRingObserver observer;

  {
    testing::InSequence seq;
    EXPECT_CALL(*file, Read)
        .Times(kNumPackets)
        .WillRepeatedly([](uint8_t* buf, size_t) {
          ts::NullPacket.copyTo(buf);
          return ts::PKT_SIZE;
        });
    EXPECT_CALL(*ring, Write)
        .WillOnce([](uint8_t* buf, size_t size) {
          EXPECT_EQ(RingFileSink::kBufferSize, size);
          return size;
        });
    EXPECT_CALL(*file, Read)
        .WillOnce(testing::Return(0));  // EOF
    EXPECT_CALL(*ring, Write)
        .Times(kNumBuffers - 1)
        .WillRepeatedly(testing::Return(RingFileSink::kBufferSize));
    EXPECT_CALL(*ring, Sync)
        .WillOnce(testing::Return(true));
    EXPECT_CALL(observer, OnChunkFlushed)
        .WillOnce([](uint64_t pos, size_t chunk_size, uint64_t ring_size) {
          EXPECT_EQ(kChunkSize, pos);
          EXPECT_EQ(kChunkSize, chunk_size);
          EXPECT_EQ(kRingSize, ring_size);
        });
  }

  EXPECT_CALL(*ring, Trunc).Times(0);  // never called
  EXPECT_CALL(*ring, Seek).Times(0);  // never called
  EXPECT_CALL(observer, OnWrappedAround).Times(0);  // never called

  FileSource src(std::move(file));
  auto sink = std::make_unique<RingFileSink>(std::move(ring), kChunkSize, kNumChunks);
  sink->SetObserver(&observer);
  src.Connect(std::move(sink));
  EXPECT_TRUE(src.FeedPackets());
}

TEST(RingFileSinkTest, ReachChunkSize) {
  constexpr auto kNumPackets1 = RingFileSink::kBufferSize / ts::PKT_SIZE + 1;
  constexpr auto kNumPackets2 = kChunkSize / ts::PKT_SIZE + 1;

  auto file = std::make_unique<MockFile>();
  auto ring = std::make_unique<MockFile>();
  MockPacketRingObserver observer;

  {
    testing::InSequence seq;
    EXPECT_CALL(*file, Read)
        .Times(kNumPackets1)
        .WillRepeatedly([](uint8_t* buf, size_t) {
          ts::NullPacket.copyTo(buf);
          return ts::PKT_SIZE;
        });
    EXPECT_CALL(*ring, Write)
        .WillOnce([](uint8_t* buf, size_t size) {
          EXPECT_EQ(RingFileSink::kBufferSize, size);
          return size;
        });
    EXPECT_CALL(*file, Read)
        .Times(kNumPackets2 - kNumPackets1)
        .WillRepeatedly([](uint8_t* buf, size_t) {
          ts::NullPacket.copyTo(buf);
          return ts::PKT_SIZE;
        });
    EXPECT_CALL(*ring, Write)
        .WillOnce([](uint8_t* buf, size_t size) {
          EXPECT_EQ(RingFileSink::kBufferSize, size);
          return size;
        });
    EXPECT_CALL(*ring, Sync)
        .WillOnce(testing::Return(true));
    EXPECT_CALL(observer, OnChunkFlushed)
        .WillOnce([](uint64_t pos, size_t chunk_size, uint64_t ring_size) {
          EXPECT_EQ(kChunkSize, pos);
          EXPECT_EQ(kChunkSize, chunk_size);
          EXPECT_EQ(kRingSize, ring_size);
        });
    EXPECT_CALL(*file, Read)
        .WillOnce(testing::Return(0));  // EOF
    EXPECT_CALL(*ring, Write)
        .Times(kNumBuffers)
        .WillRepeatedly(testing::Return(RingFileSink::kBufferSize));
    EXPECT_CALL(*ring, Sync)
        .WillOnce(testing::Return(true));
    EXPECT_CALL(observer, OnChunkFlushed)
        .WillOnce([](uint64_t pos, size_t chunk_size, uint64_t ring_size) {
          EXPECT_EQ(kChunkSize * 2, pos);
          EXPECT_EQ(kChunkSize, chunk_size);
          EXPECT_EQ(kRingSize, ring_size);
        });
    EXPECT_CALL(*ring, Trunc)
        .WillOnce([](int64_t size) {
          EXPECT_EQ(kRingSize, static_cast<uint64_t>(size));
          return true;
        });
    EXPECT_CALL(*ring, Seek)
        .WillOnce([](int64_t offset, SeekMode mode) {
          EXPECT_EQ(0, offset);
          EXPECT_EQ(SeekMode::kSet, mode);
          return 0;
        });
    EXPECT_CALL(observer, OnWrappedAround)
        .WillOnce([](uint64_t ring_size) {
          EXPECT_EQ(kRingSize, ring_size);
        });
  }

  FileSource src(std::move(file));
  auto sink = std::make_unique<RingFileSink>(std::move(ring), kChunkSize, kNumChunks);
  sink->SetObserver(&observer);
  src.Connect(std::move(sink));
  EXPECT_TRUE(src.FeedPackets());
}

TEST(RingFileSinkTest, ReachRingSize) {
  constexpr auto kNumPackets1 = RingFileSink::kBufferSize / ts::PKT_SIZE + 1;
  constexpr auto kNumPackets2 = kChunkSize / ts::PKT_SIZE + 1;
  constexpr auto kNumPackets3 =
      (kChunkSize + RingFileSink::kBufferSize) / ts::PKT_SIZE + 1;
  constexpr auto kNumPackets4 = kRingSize / ts::PKT_SIZE + 1;

  auto file = std::make_unique<MockFile>();
  auto ring = std::make_unique<MockFile>();
  MockPacketRingObserver observer;

  {
    testing::InSequence seq;
    EXPECT_CALL(*file, Read)
        .Times(kNumPackets1)
        .WillRepeatedly([](uint8_t* buf, size_t) {
          ts::NullPacket.copyTo(buf);
          return ts::PKT_SIZE;
        });
    EXPECT_CALL(*ring, Write)
        .WillOnce([](uint8_t* buf, size_t size) {
          EXPECT_EQ(RingFileSink::kBufferSize, size);
          return size;
        });
    EXPECT_CALL(*file, Read)
        .Times(kNumPackets2 - kNumPackets1)
        .WillRepeatedly([](uint8_t* buf, size_t) {
          ts::NullPacket.copyTo(buf);
          return ts::PKT_SIZE;
        });
    EXPECT_CALL(*ring, Write)
        .WillOnce([](uint8_t* buf, size_t size) {
          EXPECT_EQ(RingFileSink::kBufferSize, size);
          return size;
        });
    EXPECT_CALL(*ring, Sync)
        .WillOnce(testing::Return(true));
    EXPECT_CALL(observer, OnChunkFlushed)
        .WillOnce([](uint64_t pos, size_t chunk_size, uint64_t ring_size) {
          EXPECT_EQ(kChunkSize, pos);
          EXPECT_EQ(kChunkSize, chunk_size);
          EXPECT_EQ(kRingSize, ring_size);
        });
    EXPECT_CALL(*file, Read)
        .Times(kNumPackets3 - kNumPackets2)
        .WillRepeatedly([](uint8_t* buf, size_t) {
          ts::NullPacket.copyTo(buf);
          return ts::PKT_SIZE;
        });
    EXPECT_CALL(*ring, Write)
        .WillOnce([](uint8_t* buf, size_t size) {
          EXPECT_EQ(RingFileSink::kBufferSize, size);
          return size;
        });
    EXPECT_CALL(*file, Read)
        .Times(kNumPackets4 - kNumPackets3)
        .WillRepeatedly([](uint8_t* buf, size_t) {
          ts::NullPacket.copyTo(buf);
          return ts::PKT_SIZE;
        });
    EXPECT_CALL(*ring, Write)
        .WillOnce([](uint8_t* buf, size_t size) {
          EXPECT_EQ(RingFileSink::kBufferSize, size);
          return size;
        });
    EXPECT_CALL(*ring, Sync)
        .WillOnce(testing::Return(true));
    EXPECT_CALL(observer, OnChunkFlushed)
        .WillOnce([](uint64_t pos, size_t chunk_size, uint64_t ring_size) {
          EXPECT_EQ(kChunkSize * 2, pos);
          EXPECT_EQ(kChunkSize, chunk_size);
          EXPECT_EQ(kRingSize, ring_size);
        });
    EXPECT_CALL(*ring, Trunc)
        .WillOnce([](int64_t size) {
          EXPECT_EQ(kRingSize, size);
          return true;
        });
    EXPECT_CALL(*ring, Seek)
        .WillOnce([](int64_t offset, SeekMode mode) {
          EXPECT_EQ(0, offset);
          EXPECT_EQ(SeekMode::kSet, mode);
          return 0;
        });
    EXPECT_CALL(observer, OnWrappedAround)
        .WillOnce([](uint64_t ring_size) {
          EXPECT_EQ(kRingSize, ring_size);
        });
    EXPECT_CALL(*file, Read)
        .WillOnce(testing::Return(0));  // EOF
    EXPECT_CALL(*ring, Write)
        .Times(kNumBuffers)
        .WillRepeatedly(testing::Return(RingFileSink::kBufferSize));
    EXPECT_CALL(*ring, Sync)
        .WillOnce(testing::Return(true));
    EXPECT_CALL(observer, OnChunkFlushed)
        .WillOnce([](uint64_t pos, size_t chunk_size, uint64_t ring_size) {
          EXPECT_EQ(kChunkSize, pos);
          EXPECT_EQ(kChunkSize, chunk_size);
          EXPECT_EQ(kRingSize, ring_size);
        });
  }

  FileSource src(std::move(file));
  auto sink = std::make_unique<RingFileSink>(std::move(ring), kChunkSize, kNumChunks);
  sink->SetObserver(&observer);
  src.Connect(std::move(sink));
  EXPECT_TRUE(src.FeedPackets());
}

TEST(RingFileSinkTest, FailWriteInFlush) {
  constexpr auto kNumPackets = RingFileSink::kBufferSize / ts::PKT_SIZE + 1;

  auto file = std::make_unique<MockFile>();
  auto ring = std::make_unique<MockFile>();
  MockPacketRingObserver observer;

  {
    testing::InSequence seq;
    EXPECT_CALL(*file, Read)
        .Times(kNumPackets)
        .WillRepeatedly([](uint8_t* buf, size_t) {
          ts::NullPacket.copyTo(buf);
          return ts::PKT_SIZE;
        });
    EXPECT_CALL(*ring, Write)
        .WillOnce(testing::Return(-1));
  }

  EXPECT_CALL(*ring, Sync).Times(0);  // never called
  EXPECT_CALL(*ring, Trunc).Times(0);  // never called
  EXPECT_CALL(*ring, Seek).Times(0);   // never called
  EXPECT_CALL(observer, OnChunkFlushed).Times(0);  // never called

  FileSource src(std::move(file));
  auto sink = std::make_unique<RingFileSink>(std::move(ring), kChunkSize, kNumChunks);
  sink->SetObserver(&observer);
  src.Connect(std::move(sink));
  EXPECT_FALSE(src.FeedPackets());
}

TEST(RingFileSinkTest, FailSyncInFlush) {
  constexpr auto kNumPackets1 = RingFileSink::kBufferSize / ts::PKT_SIZE + 1;
  constexpr auto kNumPackets2 = kChunkSize / ts::PKT_SIZE + 1;

  auto file = std::make_unique<MockFile>();
  auto ring = std::make_unique<MockFile>();
  MockPacketRingObserver observer;

  {
    testing::InSequence seq;
    EXPECT_CALL(*file, Read)
        .Times(kNumPackets1)
        .WillRepeatedly([](uint8_t* buf, size_t) {
          ts::NullPacket.copyTo(buf);
          return ts::PKT_SIZE;
        });
    EXPECT_CALL(*ring, Write)
        .WillOnce([](uint8_t* buf, size_t size) {
          EXPECT_EQ(RingFileSink::kBufferSize, size);
          return size;
        });
    EXPECT_CALL(*file, Read)
        .Times(kNumPackets2 - kNumPackets1)
        .WillRepeatedly([](uint8_t* buf, size_t) {
          ts::NullPacket.copyTo(buf);
          return ts::PKT_SIZE;
        });
    EXPECT_CALL(*ring, Write)
        .WillOnce([](uint8_t* buf, size_t size) {
          EXPECT_EQ(RingFileSink::kBufferSize, size);
          return size;
        });
    EXPECT_CALL(*ring, Sync)
        .WillOnce(testing::Return(false));
  }

  EXPECT_CALL(*ring, Trunc).Times(0);  // never called
  EXPECT_CALL(*ring, Seek).Times(0);  // never called
  EXPECT_CALL(observer, OnChunkFlushed).Times(0);  // never called
  EXPECT_CALL(observer, OnWrappedAround).Times(0);  // never called

  FileSource src(std::move(file));
  auto sink = std::make_unique<RingFileSink>(std::move(ring), kChunkSize, kNumChunks);
  sink->SetObserver(&observer);
  src.Connect(std::move(sink));
  EXPECT_FALSE(src.FeedPackets());
}

TEST(RingFileSinkTest, FailTruncInFlush) {
  constexpr auto kNumPackets1 = RingFileSink::kBufferSize / ts::PKT_SIZE + 1;
  constexpr auto kNumPackets2 = kChunkSize / ts::PKT_SIZE + 1;
  constexpr auto kNumPackets3 =
      (kChunkSize + RingFileSink::kBufferSize) / ts::PKT_SIZE + 1;
  constexpr auto kNumPackets4 = kRingSize / ts::PKT_SIZE + 1;

  auto file = std::make_unique<MockFile>();
  auto ring = std::make_unique<MockFile>();
  MockPacketRingObserver observer;

  {
    testing::InSequence seq;
    EXPECT_CALL(*file, Read)
        .Times(kNumPackets1)
        .WillRepeatedly([](uint8_t* buf, size_t) {
          ts::NullPacket.copyTo(buf);
          return ts::PKT_SIZE;
        });
    EXPECT_CALL(*ring, Write)
        .WillOnce([](uint8_t* buf, size_t size) {
          EXPECT_EQ(RingFileSink::kBufferSize, size);
          return size;
        });
    EXPECT_CALL(*file, Read)
        .Times(kNumPackets2 - kNumPackets1)
        .WillRepeatedly([](uint8_t* buf, size_t) {
          ts::NullPacket.copyTo(buf);
          return ts::PKT_SIZE;
        });
    EXPECT_CALL(*ring, Write)
        .WillOnce([](uint8_t* buf, size_t size) {
          EXPECT_EQ(RingFileSink::kBufferSize, size);
          return size;
        });
    EXPECT_CALL(*ring, Sync)
        .WillOnce(testing::Return(true));
    EXPECT_CALL(observer, OnChunkFlushed)
        .WillOnce([](uint64_t pos, size_t chunk_size, uint64_t ring_size) {
          EXPECT_EQ(kChunkSize, pos);
          EXPECT_EQ(kChunkSize, chunk_size);
          EXPECT_EQ(kRingSize, ring_size);
        });
    EXPECT_CALL(*file, Read)
        .Times(kNumPackets3 - kNumPackets2)
        .WillRepeatedly([](uint8_t* buf, size_t) {
          ts::NullPacket.copyTo(buf);
          return ts::PKT_SIZE;
        });
    EXPECT_CALL(*ring, Write)
        .WillOnce([](uint8_t* buf, size_t size) {
          EXPECT_EQ(RingFileSink::kBufferSize, size);
          return size;
        });
    EXPECT_CALL(*file, Read)
        .Times(kNumPackets4 - kNumPackets3)
        .WillRepeatedly([](uint8_t* buf, size_t) {
          ts::NullPacket.copyTo(buf);
          return ts::PKT_SIZE;
        });
    EXPECT_CALL(*ring, Write)
        .WillOnce([](uint8_t* buf, size_t size) {
          EXPECT_EQ(RingFileSink::kBufferSize, size);
          return size;
        });
    EXPECT_CALL(*ring, Sync)
        .WillOnce(testing::Return(true));
    EXPECT_CALL(observer, OnChunkFlushed)
        .WillOnce([](uint64_t pos, size_t chunk_size, uint64_t ring_size) {
          EXPECT_EQ(kChunkSize * 2, pos);
          EXPECT_EQ(kChunkSize, chunk_size);
          EXPECT_EQ(kRingSize, ring_size);
        });
    EXPECT_CALL(*ring, Trunc)
        .WillOnce(testing::Return(false));
  }

  EXPECT_CALL(*ring, Seek).Times(0);  // never called
  EXPECT_CALL(observer, OnWrappedAround).Times(0);  // never called

  FileSource src(std::move(file));
  auto sink = std::make_unique<RingFileSink>(std::move(ring), kChunkSize, kNumChunks);
  sink->SetObserver(&observer);
  src.Connect(std::move(sink));
  EXPECT_FALSE(src.FeedPackets());
}

TEST(RingFileSinkTest, FailSeekInFlush) {
  constexpr auto kNumPackets1 = RingFileSink::kBufferSize / ts::PKT_SIZE + 1;
  constexpr auto kNumPackets2 = kChunkSize / ts::PKT_SIZE + 1;
  constexpr auto kNumPackets3 =
      (kChunkSize + RingFileSink::kBufferSize) / ts::PKT_SIZE + 1;
  constexpr auto kNumPackets4 = kRingSize / ts::PKT_SIZE + 1;

  auto file = std::make_unique<MockFile>();
  auto ring = std::make_unique<MockFile>();
  MockPacketRingObserver observer;

  {
    testing::InSequence seq;
    EXPECT_CALL(*file, Read)
        .Times(kNumPackets1)
        .WillRepeatedly([](uint8_t* buf, size_t) {
          ts::NullPacket.copyTo(buf);
          return ts::PKT_SIZE;
        });
    EXPECT_CALL(*ring, Write)
        .WillOnce([](uint8_t* buf, size_t size) {
          EXPECT_EQ(RingFileSink::kBufferSize, size);
          return size;
        });
    EXPECT_CALL(*file, Read)
        .Times(kNumPackets2 - kNumPackets1)
        .WillRepeatedly([](uint8_t* buf, size_t) {
          ts::NullPacket.copyTo(buf);
          return ts::PKT_SIZE;
        });
    EXPECT_CALL(*ring, Write)
        .WillOnce([](uint8_t* buf, size_t size) {
          EXPECT_EQ(RingFileSink::kBufferSize, size);
          return size;
        });
    EXPECT_CALL(*ring, Sync)
        .WillOnce(testing::Return(true));
    EXPECT_CALL(observer, OnChunkFlushed)
        .WillOnce([](uint64_t pos, size_t chunk_size, uint64_t ring_size) {
          EXPECT_EQ(kChunkSize, pos);
          EXPECT_EQ(kChunkSize, chunk_size);
          EXPECT_EQ(kRingSize, ring_size);
        });
    EXPECT_CALL(*file, Read)
        .Times(kNumPackets3 - kNumPackets2)
        .WillRepeatedly([](uint8_t* buf, size_t) {
          ts::NullPacket.copyTo(buf);
          return ts::PKT_SIZE;
        });
    EXPECT_CALL(*ring, Write)
        .WillOnce([](uint8_t* buf, size_t size) {
          EXPECT_EQ(RingFileSink::kBufferSize, size);
          return size;
        });
    EXPECT_CALL(*file, Read)
        .Times(kNumPackets4 - kNumPackets3)
        .WillRepeatedly([](uint8_t* buf, size_t) {
          ts::NullPacket.copyTo(buf);
          return ts::PKT_SIZE;
        });
    EXPECT_CALL(*ring, Write)
        .WillOnce([](uint8_t* buf, size_t size) {
          EXPECT_EQ(RingFileSink::kBufferSize, size);
          return size;
        });
    EXPECT_CALL(*ring, Sync)
        .WillOnce(testing::Return(true));
    EXPECT_CALL(observer, OnChunkFlushed)
        .WillOnce([](uint64_t pos, size_t chunk_size, uint64_t ring_size) {
          EXPECT_EQ(kChunkSize * 2, pos);
          EXPECT_EQ(kChunkSize, chunk_size);
          EXPECT_EQ(kRingSize, ring_size);
        });
    EXPECT_CALL(*ring, Trunc)
        .WillOnce([](int64_t size) {
          EXPECT_EQ(kRingSize, static_cast<uint64_t>(size));
          return true;
        });
    EXPECT_CALL(*ring, Seek)
        .WillOnce(testing::Return(-1));
  }

  EXPECT_CALL(observer, OnWrappedAround).Times(0);  // never called

  FileSource src(std::move(file));
  auto sink = std::make_unique<RingFileSink>(std::move(ring), kChunkSize, kNumChunks);
  sink->SetObserver(&observer);
  src.Connect(std::move(sink));
  EXPECT_FALSE(src.FeedPackets());
}

TEST(RingFileSinkTest, FailWriteInEnd) {
  auto file = std::make_unique<MockFile>();
  auto ring = std::make_unique<MockFile>();
  MockPacketRingObserver observer;

  {
    testing::InSequence seq;
    EXPECT_CALL(*file, Read)
        .WillOnce([](uint8_t* buf, size_t) {
          ts::NullPacket.copyTo(buf);
          return ts::PKT_SIZE;
        });
    EXPECT_CALL(*file, Read)
        .WillOnce(testing::Return(0));  // EOF
    EXPECT_CALL(*ring, Write)
        .WillOnce(testing::Return(-1));
  }

  EXPECT_CALL(*ring, Sync).Times(0);  // never called
  EXPECT_CALL(*ring, Trunc).Times(0);  // never called
  EXPECT_CALL(*ring, Seek).Times(0);   // never called
  EXPECT_CALL(observer, OnChunkFlushed).Times(0);  // never called
  EXPECT_CALL(observer, OnWrappedAround).Times(0);  // never called

  FileSource src(std::move(file));
  auto sink = std::make_unique<RingFileSink>(std::move(ring), kChunkSize, kNumChunks);
  sink->SetObserver(&observer);
  src.Connect(std::move(sink));
  EXPECT_FALSE(src.FeedPackets());
}

TEST(RingFileSinkTest, FailSyncInEnd) {
  auto file = std::make_unique<MockFile>();
  auto ring = std::make_unique<MockFile>();
  MockPacketRingObserver observer;

  {
    testing::InSequence seq;
    EXPECT_CALL(*file, Read)
        .WillOnce([](uint8_t* buf, size_t) {
          ts::NullPacket.copyTo(buf);
          return ts::PKT_SIZE;
        });
    EXPECT_CALL(*file, Read)
        .WillOnce(testing::Return(0));  // EOF
    EXPECT_CALL(*ring, Write)
        .Times(kNumBuffers)
        .WillRepeatedly(testing::Return(RingFileSink::kBufferSize));
    EXPECT_CALL(*ring, Sync)
        .WillOnce(testing::Return(false));
  }

  EXPECT_CALL(*ring, Trunc).Times(0);  // never called
  EXPECT_CALL(*ring, Seek).Times(0);  // never called
  EXPECT_CALL(observer, OnChunkFlushed).Times(0);  // never called
  EXPECT_CALL(observer, OnWrappedAround).Times(0);  // never called

  FileSource src(std::move(file));
  auto sink = std::make_unique<RingFileSink>(std::move(ring), kChunkSize, kNumChunks);
  sink->SetObserver(&observer);
  src.Connect(std::move(sink));
  EXPECT_FALSE(src.FeedPackets());
}

TEST(RingFileSinkTest, FailTruncInEnd) {
  constexpr auto kNumPackets1 = RingFileSink::kBufferSize / ts::PKT_SIZE + 1;
  constexpr auto kNumPackets2 = kChunkSize / ts::PKT_SIZE + 1;
  constexpr auto kNumPackets3 =
      (kChunkSize + RingFileSink::kBufferSize) / ts::PKT_SIZE + 1;

  auto file = std::make_unique<MockFile>();
  auto ring = std::make_unique<MockFile>();
  MockPacketRingObserver observer;

  {
    testing::InSequence seq;
    EXPECT_CALL(*file, Read)
        .Times(kNumPackets1)
        .WillRepeatedly([](uint8_t* buf, size_t) {
          ts::NullPacket.copyTo(buf);
          return ts::PKT_SIZE;
        });
    EXPECT_CALL(*ring, Write)
        .WillOnce([](uint8_t* buf, size_t size) {
          EXPECT_EQ(RingFileSink::kBufferSize, size);
          return size;
        });
    EXPECT_CALL(*file, Read)
        .Times(kNumPackets2 - kNumPackets1)
        .WillRepeatedly([](uint8_t* buf, size_t) {
          ts::NullPacket.copyTo(buf);
          return ts::PKT_SIZE;
        });
    EXPECT_CALL(*ring, Write)
        .WillOnce([](uint8_t* buf, size_t size) {
          EXPECT_EQ(RingFileSink::kBufferSize, size);
          return size;
        });
    EXPECT_CALL(*ring, Sync)
        .WillOnce(testing::Return(true));
    EXPECT_CALL(observer, OnChunkFlushed)
        .WillOnce([](uint64_t pos, size_t chunk_size, uint64_t ring_size) {
          EXPECT_EQ(kChunkSize, pos);
          EXPECT_EQ(kChunkSize, chunk_size);
          EXPECT_EQ(kRingSize, ring_size);
        });
    EXPECT_CALL(*file, Read)
        .Times(kNumPackets3 - kNumPackets2)
        .WillRepeatedly([](uint8_t* buf, size_t) {
          ts::NullPacket.copyTo(buf);
          return ts::PKT_SIZE;
        });
    EXPECT_CALL(*ring, Write)
        .WillOnce([](uint8_t* buf, size_t size) {
          EXPECT_EQ(RingFileSink::kBufferSize, size);
          return size;
        });
    EXPECT_CALL(*file, Read)
        .WillOnce(testing::Return(0));  // EOF
    EXPECT_CALL(*ring, Write)
        .WillOnce([](uint8_t* buf, size_t size) {
          EXPECT_EQ(RingFileSink::kBufferSize, size);
          return size;
        });
    EXPECT_CALL(*ring, Sync)
        .WillOnce(testing::Return(true));
    EXPECT_CALL(observer, OnChunkFlushed)
        .WillOnce([](uint64_t pos, size_t chunk_size, uint64_t ring_size) {
          EXPECT_EQ(kChunkSize * 2, pos);
          EXPECT_EQ(kChunkSize, chunk_size);
          EXPECT_EQ(kRingSize, ring_size);
        });
    EXPECT_CALL(*ring, Trunc)
        .WillOnce(testing::Return(false));
  }

  EXPECT_CALL(*ring, Seek).Times(0);  // never called
  EXPECT_CALL(observer, OnWrappedAround).Times(0);  // never called

  FileSource src(std::move(file));
  auto sink = std::make_unique<RingFileSink>(std::move(ring), kChunkSize, kNumChunks);
  sink->SetObserver(&observer);
  src.Connect(std::move(sink));
  EXPECT_FALSE(src.FeedPackets());
}

TEST(RingFileSinkTest, FailSeekInEnd) {
  constexpr auto kNumPackets1 = RingFileSink::kBufferSize / ts::PKT_SIZE + 1;
  constexpr auto kNumPackets2 = kChunkSize / ts::PKT_SIZE + 1;
  constexpr auto kNumPackets3 =
      (kChunkSize + RingFileSink::kBufferSize) / ts::PKT_SIZE + 1;

  auto file = std::make_unique<MockFile>();
  auto ring = std::make_unique<MockFile>();
  MockPacketRingObserver observer;

  {
    testing::InSequence seq;
    EXPECT_CALL(*file, Read)
        .Times(kNumPackets1)
        .WillRepeatedly([](uint8_t* buf, size_t) {
          ts::NullPacket.copyTo(buf);
          return ts::PKT_SIZE;
        });
    EXPECT_CALL(*ring, Write)
        .WillOnce([](uint8_t* buf, size_t size) {
          EXPECT_EQ(RingFileSink::kBufferSize, size);
          return size;
        });
    EXPECT_CALL(*file, Read)
        .Times(kNumPackets2 - kNumPackets1)
        .WillRepeatedly([](uint8_t* buf, size_t) {
          ts::NullPacket.copyTo(buf);
          return ts::PKT_SIZE;
        });
    EXPECT_CALL(*ring, Write)
        .WillOnce([](uint8_t* buf, size_t size) {
          EXPECT_EQ(RingFileSink::kBufferSize, size);
          return size;
        });
    EXPECT_CALL(*ring, Sync)
        .WillOnce(testing::Return(true));
    EXPECT_CALL(observer, OnChunkFlushed)
        .WillOnce([](uint64_t pos, size_t chunk_size, uint64_t ring_size) {
          EXPECT_EQ(kChunkSize, pos);
          EXPECT_EQ(kChunkSize, chunk_size);
          EXPECT_EQ(kRingSize, ring_size);
        });
    EXPECT_CALL(*file, Read)
        .Times(kNumPackets3 - kNumPackets2)
        .WillRepeatedly([](uint8_t* buf, size_t) {
          ts::NullPacket.copyTo(buf);
          return ts::PKT_SIZE;
        });
    EXPECT_CALL(*ring, Write)
        .WillOnce([](uint8_t* buf, size_t size) {
          EXPECT_EQ(RingFileSink::kBufferSize, size);
          return size;
        });
    EXPECT_CALL(*file, Read)
        .WillOnce(testing::Return(0));  // EOF
    EXPECT_CALL(*ring, Write)
        .WillOnce([](uint8_t* buf, size_t size) {
          EXPECT_EQ(RingFileSink::kBufferSize, size);
          return size;
        });
    EXPECT_CALL(*ring, Sync)
        .WillOnce(testing::Return(true));
    EXPECT_CALL(observer, OnChunkFlushed)
        .WillOnce([](uint64_t pos, size_t chunk_size, uint64_t ring_size) {
          EXPECT_EQ(kChunkSize * 2, pos);
          EXPECT_EQ(kChunkSize, chunk_size);
          EXPECT_EQ(kRingSize, ring_size);
        });
    EXPECT_CALL(*ring, Trunc)
        .WillOnce([](int64_t size) {
          EXPECT_EQ(kRingSize, static_cast<uint64_t>(size));
          return true;
        });
    EXPECT_CALL(*ring, Seek)
        .WillOnce(testing::Return(-1));
  }

  EXPECT_CALL(observer, OnWrappedAround).Times(0);  // never called

  FileSource src(std::move(file));
  auto sink = std::make_unique<RingFileSink>(std::move(ring), kChunkSize, kNumChunks);
  sink->SetObserver(&observer);
  src.Connect(std::move(sink));
  EXPECT_FALSE(src.FeedPackets());
}
