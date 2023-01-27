#include <cstdlib>
#include <memory>

#include <fmt/format.h>
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
  MOCK_METHOD(void, OnEndOfChunk, (uint64_t), (override));
};

}  // namespace

TEST(RingFileSinkTest, MaxValues) {
  EXPECT_EQ(0x7FFFE000, RingFileSink::kMaxChunkSize);
  EXPECT_EQ(0x7FFFFFFF, RingFileSink::kMaxNumChunks);
  EXPECT_EQ(0x3FFFEFFF80002000, RingFileSink::kMaxRingSize);
  EXPECT_EQ("3FFFEFFF80002000", fmt::format("{:X}", RingFileSink::kMaxRingSize));

  auto ring = std::make_unique<MockFile>();
  RingFileSink sink(std::move(ring), RingFileSink::kMaxChunkSize, RingFileSink::kMaxNumChunks);
  EXPECT_EQ(RingFileSink::kMaxRingSize, sink.ring_size());
}

TEST(RingFileSinkTest, EmptyFile) {
  auto file = std::make_unique<MockFile>();
  auto ring = std::make_unique<MockFile>();
  MockPacketRingObserver observer;

  {
    testing::InSequence seq;
    EXPECT_CALL(*file, Read).WillOnce(testing::Return(0));  // EOF
  }

  EXPECT_CALL(*ring, Write).Times(0);            // never called
  EXPECT_CALL(*ring, Sync).Times(0);             // never called
  EXPECT_CALL(*ring, Trunc).Times(0);            // never called
  EXPECT_CALL(*ring, Seek).Times(0);             // never called
  EXPECT_CALL(observer, OnEndOfChunk).Times(0);  // never called

  FileSource src(std::move(file));
  auto sink = std::make_unique<RingFileSink>(std::move(ring), kChunkSize, kNumChunks);
  sink->SetObserver(&observer);
  src.Connect(std::move(sink));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
}

TEST(RingFileSinkTest, OnePacket) {
  auto file = std::make_unique<MockFile>();
  auto ring = std::make_unique<MockFile>();
  MockPacketRingObserver observer;

  {
    testing::InSequence seq;
    EXPECT_CALL(*file, Read).WillOnce([](uint8_t* buf, size_t) {
      ts::NullPacket.copyTo(buf);
      return ts::PKT_SIZE;
    });
    EXPECT_CALL(*file, Read).WillOnce(testing::Return(0));  // EOF
  }

  EXPECT_CALL(*ring, Write).Times(0);            // never called
  EXPECT_CALL(*ring, Sync).Times(0);             // never called
  EXPECT_CALL(*ring, Trunc).Times(0);            // never called
  EXPECT_CALL(*ring, Seek).Times(0);             // never called
  EXPECT_CALL(observer, OnEndOfChunk).Times(0);  // never called

  FileSource src(std::move(file));
  auto sink = std::make_unique<RingFileSink>(std::move(ring), kChunkSize, kNumChunks);
  sink->SetObserver(&observer);
  src.Connect(std::move(sink));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
}

TEST(RingFileSinkTest, ReachBufferSize) {
  constexpr auto kNumPackets = RingFileSink::kBufferSize / ts::PKT_SIZE + 1;

  auto file = std::make_unique<MockFile>();
  auto ring = std::make_unique<MockFile>();
  MockPacketRingObserver observer;

  {
    testing::InSequence seq;
    EXPECT_CALL(*file, Read).Times(kNumPackets).WillRepeatedly([](uint8_t* buf, size_t) {
      ts::NullPacket.copyTo(buf);
      return ts::PKT_SIZE;
    });
    EXPECT_CALL(*ring, Write).WillOnce([](uint8_t* buf, size_t size) {
      EXPECT_EQ(RingFileSink::kBufferSize, size);
      return size;
    });
    EXPECT_CALL(*file, Read).WillOnce(testing::Return(0));  // EOF
  }

  EXPECT_CALL(*ring, Sync).Times(0);             // never called
  EXPECT_CALL(*ring, Trunc).Times(0);            // never called
  EXPECT_CALL(*ring, Seek).Times(0);             // never called
  EXPECT_CALL(observer, OnEndOfChunk).Times(0);  // never called

  FileSource src(std::move(file));
  auto sink = std::make_unique<RingFileSink>(std::move(ring), kChunkSize, kNumChunks);
  sink->SetObserver(&observer);
  src.Connect(std::move(sink));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
}

TEST(RingFileSinkTest, ReachChunkSize) {
  constexpr auto kNumPackets1 = RingFileSink::kBufferSize / ts::PKT_SIZE + 1;
  constexpr auto kNumPackets2 = kChunkSize / ts::PKT_SIZE + 1;

  auto file = std::make_unique<MockFile>();
  auto ring = std::make_unique<MockFile>();
  MockPacketRingObserver observer;

  {
    testing::InSequence seq;
    EXPECT_CALL(*file, Read).Times(kNumPackets1).WillRepeatedly([](uint8_t* buf, size_t) {
      ts::NullPacket.copyTo(buf);
      return ts::PKT_SIZE;
    });
    EXPECT_CALL(*ring, Write).WillOnce([](uint8_t* buf, size_t size) {
      EXPECT_EQ(RingFileSink::kBufferSize, size);
      return size;
    });
    EXPECT_CALL(*file, Read)
        .Times(kNumPackets2 - kNumPackets1)
        .WillRepeatedly([](uint8_t* buf, size_t) {
          ts::NullPacket.copyTo(buf);
          return ts::PKT_SIZE;
        });
    EXPECT_CALL(*ring, Write).WillOnce([](uint8_t* buf, size_t size) {
      EXPECT_EQ(RingFileSink::kBufferSize, size);
      return size;
    });
    EXPECT_CALL(*ring, Sync).WillOnce(testing::Return(true));
    EXPECT_CALL(observer, OnEndOfChunk).WillOnce([](uint64_t pos) { EXPECT_EQ(kChunkSize, pos); });
    EXPECT_CALL(*file, Read).WillOnce(testing::Return(0));  // EOF
  }

  FileSource src(std::move(file));
  auto sink = std::make_unique<RingFileSink>(std::move(ring), kChunkSize, kNumChunks);
  sink->SetObserver(&observer);
  src.Connect(std::move(sink));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
}

TEST(RingFileSinkTest, ReachRingSize) {
  constexpr auto kNumPackets1 = RingFileSink::kBufferSize / ts::PKT_SIZE + 1;
  constexpr auto kNumPackets2 = kChunkSize / ts::PKT_SIZE + 1;
  constexpr auto kNumPackets3 = (kChunkSize + RingFileSink::kBufferSize) / ts::PKT_SIZE + 1;
  constexpr auto kNumPackets4 = kRingSize / ts::PKT_SIZE + 1;

  auto file = std::make_unique<MockFile>();
  auto ring = std::make_unique<MockFile>();
  MockPacketRingObserver observer;

  {
    testing::InSequence seq;
    EXPECT_CALL(*file, Read).Times(kNumPackets1).WillRepeatedly([](uint8_t* buf, size_t) {
      ts::NullPacket.copyTo(buf);
      return ts::PKT_SIZE;
    });
    EXPECT_CALL(*ring, Write).WillOnce([](uint8_t* buf, size_t size) {
      EXPECT_EQ(RingFileSink::kBufferSize, size);
      return size;
    });
    EXPECT_CALL(*file, Read)
        .Times(kNumPackets2 - kNumPackets1)
        .WillRepeatedly([](uint8_t* buf, size_t) {
          ts::NullPacket.copyTo(buf);
          return ts::PKT_SIZE;
        });
    EXPECT_CALL(*ring, Write).WillOnce([](uint8_t* buf, size_t size) {
      EXPECT_EQ(RingFileSink::kBufferSize, size);
      return size;
    });
    EXPECT_CALL(*ring, Sync).WillOnce(testing::Return(true));
    EXPECT_CALL(observer, OnEndOfChunk).WillOnce([](uint64_t pos) { EXPECT_EQ(kChunkSize, pos); });
    EXPECT_CALL(*file, Read)
        .Times(kNumPackets3 - kNumPackets2)
        .WillRepeatedly([](uint8_t* buf, size_t) {
          ts::NullPacket.copyTo(buf);
          return ts::PKT_SIZE;
        });
    EXPECT_CALL(*ring, Write).WillOnce([](uint8_t* buf, size_t size) {
      EXPECT_EQ(RingFileSink::kBufferSize, size);
      return size;
    });
    EXPECT_CALL(*file, Read)
        .Times(kNumPackets4 - kNumPackets3)
        .WillRepeatedly([](uint8_t* buf, size_t) {
          ts::NullPacket.copyTo(buf);
          return ts::PKT_SIZE;
        });
    EXPECT_CALL(*ring, Write).WillOnce([](uint8_t* buf, size_t size) {
      EXPECT_EQ(RingFileSink::kBufferSize, size);
      return size;
    });
    EXPECT_CALL(*ring, Sync).WillOnce(testing::Return(true));
    EXPECT_CALL(observer, OnEndOfChunk).WillOnce([](uint64_t pos) { EXPECT_EQ(kRingSize, pos); });
    EXPECT_CALL(*ring, Trunc).WillOnce([](int64_t size) {
      EXPECT_EQ(kRingSize, size);
      return true;
    });
    EXPECT_CALL(*ring, Seek).WillOnce([](int64_t offset, SeekMode mode) {
      EXPECT_EQ(0, offset);
      EXPECT_EQ(SeekMode::kSet, mode);
      return 0;
    });
    EXPECT_CALL(*file, Read).WillOnce(testing::Return(0));  // EOF
  }

  FileSource src(std::move(file));
  auto sink = std::make_unique<RingFileSink>(std::move(ring), kChunkSize, kNumChunks);
  sink->SetObserver(&observer);
  src.Connect(std::move(sink));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
}

TEST(RingFileSinkTest, FailWriteInFlush) {
  constexpr auto kNumPackets = RingFileSink::kBufferSize / ts::PKT_SIZE + 1;

  auto file = std::make_unique<MockFile>();
  auto ring = std::make_unique<MockFile>();
  MockPacketRingObserver observer;

  {
    testing::InSequence seq;
    EXPECT_CALL(*file, Read).Times(kNumPackets).WillRepeatedly([](uint8_t* buf, size_t) {
      ts::NullPacket.copyTo(buf);
      return ts::PKT_SIZE;
    });
    EXPECT_CALL(*ring, Write).WillOnce(testing::Return(-1));
  }

  EXPECT_CALL(*ring, Sync).Times(0);             // never called
  EXPECT_CALL(*ring, Trunc).Times(0);            // never called
  EXPECT_CALL(*ring, Seek).Times(0);             // never called
  EXPECT_CALL(observer, OnEndOfChunk).Times(0);  // never called

  FileSource src(std::move(file));
  auto sink = std::make_unique<RingFileSink>(std::move(ring), kChunkSize, kNumChunks);
  sink->SetObserver(&observer);
  src.Connect(std::move(sink));
  EXPECT_EQ(EXIT_FAILURE, src.FeedPackets());
}

TEST(RingFileSinkTest, FailSyncInFlush) {
  constexpr auto kNumPackets1 = RingFileSink::kBufferSize / ts::PKT_SIZE + 1;
  constexpr auto kNumPackets2 = kChunkSize / ts::PKT_SIZE + 1;

  auto file = std::make_unique<MockFile>();
  auto ring = std::make_unique<MockFile>();
  MockPacketRingObserver observer;

  {
    testing::InSequence seq;
    EXPECT_CALL(*file, Read).Times(kNumPackets1).WillRepeatedly([](uint8_t* buf, size_t) {
      ts::NullPacket.copyTo(buf);
      return ts::PKT_SIZE;
    });
    EXPECT_CALL(*ring, Write).WillOnce([](uint8_t* buf, size_t size) {
      EXPECT_EQ(RingFileSink::kBufferSize, size);
      return size;
    });
    EXPECT_CALL(*file, Read)
        .Times(kNumPackets2 - kNumPackets1)
        .WillRepeatedly([](uint8_t* buf, size_t) {
          ts::NullPacket.copyTo(buf);
          return ts::PKT_SIZE;
        });
    EXPECT_CALL(*ring, Write).WillOnce([](uint8_t* buf, size_t size) {
      EXPECT_EQ(RingFileSink::kBufferSize, size);
      return size;
    });
    EXPECT_CALL(*ring, Sync).WillOnce(testing::Return(false));
  }

  EXPECT_CALL(*ring, Trunc).Times(0);            // never called
  EXPECT_CALL(*ring, Seek).Times(0);             // never called
  EXPECT_CALL(observer, OnEndOfChunk).Times(0);  // never called

  FileSource src(std::move(file));
  auto sink = std::make_unique<RingFileSink>(std::move(ring), kChunkSize, kNumChunks);
  sink->SetObserver(&observer);
  src.Connect(std::move(sink));
  EXPECT_EQ(EXIT_FAILURE, src.FeedPackets());
}

TEST(RingFileSinkTest, FailTruncInFlush) {
  constexpr auto kNumPackets1 = RingFileSink::kBufferSize / ts::PKT_SIZE + 1;
  constexpr auto kNumPackets2 = kChunkSize / ts::PKT_SIZE + 1;
  constexpr auto kNumPackets3 = (kChunkSize + RingFileSink::kBufferSize) / ts::PKT_SIZE + 1;
  constexpr auto kNumPackets4 = kRingSize / ts::PKT_SIZE + 1;

  auto file = std::make_unique<MockFile>();
  auto ring = std::make_unique<MockFile>();
  MockPacketRingObserver observer;

  {
    testing::InSequence seq;
    EXPECT_CALL(*file, Read).Times(kNumPackets1).WillRepeatedly([](uint8_t* buf, size_t) {
      ts::NullPacket.copyTo(buf);
      return ts::PKT_SIZE;
    });
    EXPECT_CALL(*ring, Write).WillOnce([](uint8_t* buf, size_t size) {
      EXPECT_EQ(RingFileSink::kBufferSize, size);
      return size;
    });
    EXPECT_CALL(*file, Read)
        .Times(kNumPackets2 - kNumPackets1)
        .WillRepeatedly([](uint8_t* buf, size_t) {
          ts::NullPacket.copyTo(buf);
          return ts::PKT_SIZE;
        });
    EXPECT_CALL(*ring, Write).WillOnce([](uint8_t* buf, size_t size) {
      EXPECT_EQ(RingFileSink::kBufferSize, size);
      return size;
    });
    EXPECT_CALL(*ring, Sync).WillOnce(testing::Return(true));
    EXPECT_CALL(observer, OnEndOfChunk).WillOnce([](uint64_t pos) { EXPECT_EQ(kChunkSize, pos); });
    EXPECT_CALL(*file, Read)
        .Times(kNumPackets3 - kNumPackets2)
        .WillRepeatedly([](uint8_t* buf, size_t) {
          ts::NullPacket.copyTo(buf);
          return ts::PKT_SIZE;
        });
    EXPECT_CALL(*ring, Write).WillOnce([](uint8_t* buf, size_t size) {
      EXPECT_EQ(RingFileSink::kBufferSize, size);
      return size;
    });
    EXPECT_CALL(*file, Read)
        .Times(kNumPackets4 - kNumPackets3)
        .WillRepeatedly([](uint8_t* buf, size_t) {
          ts::NullPacket.copyTo(buf);
          return ts::PKT_SIZE;
        });
    EXPECT_CALL(*ring, Write).WillOnce([](uint8_t* buf, size_t size) {
      EXPECT_EQ(RingFileSink::kBufferSize, size);
      return size;
    });
    EXPECT_CALL(*ring, Sync).WillOnce(testing::Return(true));
    EXPECT_CALL(observer, OnEndOfChunk).WillOnce([](uint64_t pos) {
      EXPECT_EQ(kChunkSize * 2, pos);
    });
    EXPECT_CALL(*ring, Trunc).WillOnce(testing::Return(false));
  }

  EXPECT_CALL(*ring, Seek).Times(0);  // never called

  FileSource src(std::move(file));
  auto sink = std::make_unique<RingFileSink>(std::move(ring), kChunkSize, kNumChunks);
  sink->SetObserver(&observer);
  src.Connect(std::move(sink));
  EXPECT_EQ(EXIT_FAILURE, src.FeedPackets());
}

TEST(RingFileSinkTest, FailSeekInFlush) {
  constexpr auto kNumPackets1 = RingFileSink::kBufferSize / ts::PKT_SIZE + 1;
  constexpr auto kNumPackets2 = kChunkSize / ts::PKT_SIZE + 1;
  constexpr auto kNumPackets3 = (kChunkSize + RingFileSink::kBufferSize) / ts::PKT_SIZE + 1;
  constexpr auto kNumPackets4 = kRingSize / ts::PKT_SIZE + 1;

  auto file = std::make_unique<MockFile>();
  auto ring = std::make_unique<MockFile>();
  MockPacketRingObserver observer;

  {
    testing::InSequence seq;
    EXPECT_CALL(*file, Read).Times(kNumPackets1).WillRepeatedly([](uint8_t* buf, size_t) {
      ts::NullPacket.copyTo(buf);
      return ts::PKT_SIZE;
    });
    EXPECT_CALL(*ring, Write).WillOnce([](uint8_t* buf, size_t size) {
      EXPECT_EQ(RingFileSink::kBufferSize, size);
      return size;
    });
    EXPECT_CALL(*file, Read)
        .Times(kNumPackets2 - kNumPackets1)
        .WillRepeatedly([](uint8_t* buf, size_t) {
          ts::NullPacket.copyTo(buf);
          return ts::PKT_SIZE;
        });
    EXPECT_CALL(*ring, Write).WillOnce([](uint8_t* buf, size_t size) {
      EXPECT_EQ(RingFileSink::kBufferSize, size);
      return size;
    });
    EXPECT_CALL(*ring, Sync).WillOnce(testing::Return(true));
    EXPECT_CALL(observer, OnEndOfChunk).WillOnce([](uint64_t pos) { EXPECT_EQ(kChunkSize, pos); });
    EXPECT_CALL(*file, Read)
        .Times(kNumPackets3 - kNumPackets2)
        .WillRepeatedly([](uint8_t* buf, size_t) {
          ts::NullPacket.copyTo(buf);
          return ts::PKT_SIZE;
        });
    EXPECT_CALL(*ring, Write).WillOnce([](uint8_t* buf, size_t size) {
      EXPECT_EQ(RingFileSink::kBufferSize, size);
      return size;
    });
    EXPECT_CALL(*file, Read)
        .Times(kNumPackets4 - kNumPackets3)
        .WillRepeatedly([](uint8_t* buf, size_t) {
          ts::NullPacket.copyTo(buf);
          return ts::PKT_SIZE;
        });
    EXPECT_CALL(*ring, Write).WillOnce([](uint8_t* buf, size_t size) {
      EXPECT_EQ(RingFileSink::kBufferSize, size);
      return size;
    });
    EXPECT_CALL(*ring, Sync).WillOnce(testing::Return(true));
    EXPECT_CALL(observer, OnEndOfChunk).WillOnce([](uint64_t pos) {
      EXPECT_EQ(kChunkSize * 2, pos);
    });
    EXPECT_CALL(*ring, Trunc).WillOnce([](int64_t size) {
      EXPECT_EQ(kRingSize, static_cast<uint64_t>(size));
      return true;
    });
    EXPECT_CALL(*ring, Seek).WillOnce(testing::Return(-1));
  }

  FileSource src(std::move(file));
  auto sink = std::make_unique<RingFileSink>(std::move(ring), kChunkSize, kNumChunks);
  sink->SetObserver(&observer);
  src.Connect(std::move(sink));
  EXPECT_EQ(EXIT_FAILURE, src.FeedPackets());
}

TEST(RingFileSinkTest, SetPosition) {
  auto file = std::make_unique<MockFile>();
  auto ring = std::make_unique<MockFile>();
  MockPacketRingObserver observer;

  {
    testing::InSequence seq;
    EXPECT_CALL(*ring, Seek).WillOnce([](int64_t offset, SeekMode mode) {
      EXPECT_EQ(kChunkSize, offset);
      EXPECT_EQ(SeekMode::kSet, mode);
      return 0;
    });
    EXPECT_CALL(*file, Read).WillOnce(testing::Return(0));  // EOF
  }

  EXPECT_CALL(*ring, Write).Times(0);            // never called
  EXPECT_CALL(*ring, Sync).Times(0);             // never called
  EXPECT_CALL(*ring, Trunc).Times(0);            // never called
  EXPECT_CALL(observer, OnEndOfChunk).Times(0);  // never called

  FileSource src(std::move(file));
  auto sink = std::make_unique<RingFileSink>(std::move(ring), kChunkSize, kNumChunks);
  sink->SetObserver(&observer);
  sink->SetPosition(kChunkSize);
  src.Connect(std::move(sink));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
}
