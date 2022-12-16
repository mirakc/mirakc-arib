#include <cstdlib>
#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "packet_source.hh"

#include "test_helper.hh"

TEST(PacketSourceTest, EmptyFile) {
  auto file = std::make_unique<MockFile>();
  auto sink = std::make_unique<MockSink>();

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*file, Read).WillOnce(testing::Return(0));  // EOF
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  EXPECT_CALL(*sink, HandlePacket).Times(0);  // Never called

  FileSource src(std::move(file));
  src.Connect(std::move(sink));
  src.FeedPackets();
}

TEST(PacketSourceTest, OneByteFile) {
  auto file = std::make_unique<MockFile>();
  auto sink = std::make_unique<MockSink>();

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*file, Read).WillOnce(testing::Return(1));
    EXPECT_CALL(*file, Read).WillOnce(testing::Return(0));  // EOF
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  EXPECT_CALL(*sink, HandlePacket).Times(0);  // Never called

  FileSource src(std::move(file));
  src.Connect(std::move(sink));
  src.FeedPackets();
}

TEST(PacketSourceTest, OnePacketFile) {
  auto file = std::make_unique<MockFile>();
  auto sink = std::make_unique<MockSink>();

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*file, Read).WillOnce(
        [](uint8_t* buf, size_t) {
          ts::NullPacket.copyTo(buf);
          return ts::PKT_SIZE;
        });
    EXPECT_CALL(*sink, HandlePacket).WillOnce(testing::Return(true));
    EXPECT_CALL(*file, Read).WillOnce(testing::Return(0));  // EOF
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  FileSource src(std::move(file));
  src.Connect(std::move(sink));
  src.FeedPackets();
}

TEST(PacketSourceTest, Resync) {
  auto file = std::make_unique<MockFile>();
  auto sink = std::make_unique<MockSink>();

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*file, Read).WillOnce(
        [](uint8_t* buf, size_t) {
          buf[0] = 0;
          buf[1] = ts::SYNC_BYTE;
          buf[2] = 0;
          return 3;
        });
    EXPECT_CALL(*file, Read).WillOnce(
        [](uint8_t* buf, size_t) {
          static constexpr size_t kNBytes = 5 * ts::PKT_SIZE;
          for (auto* p = buf; p < buf + kNBytes; p += ts::PKT_SIZE) {
            ts::NullPacket.copyTo(p);
          }
          return kNBytes;
        });
    EXPECT_CALL(*sink, HandlePacket)
        .Times(5).WillRepeatedly(testing::Return(true));
    EXPECT_CALL(*file, Read).WillOnce(testing::Return(0));  // EOF
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  FileSource src(std::move(file));
  src.Connect(std::move(sink));
  src.FeedPackets();
}

TEST(PacketSourceTest, ResyncFailure) {
  auto file = std::make_unique<MockFile>();
  auto sink = std::make_unique<MockSink>();

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*file, Read).WillOnce(
        [](uint8_t* buf, size_t) {
          static constexpr size_t kNBytes = 10 * ts::PKT_SIZE;
          memset(buf, 0, kNBytes);
          return kNBytes;
        });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  EXPECT_CALL(*sink, HandlePacket).Times(0);  // Never called

  FileSource src(std::move(file));
  src.Connect(std::move(sink));
  src.FeedPackets();
}

TEST(PacketSourceTest, WrapAroundWhileResync) {
  auto file = std::make_unique<MockFile>();
  auto sink = std::make_unique<MockSink>();

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*file, Read).WillOnce(
        [](uint8_t* buf, size_t size) {
          auto nread = ((size / ts::PKT_SIZE) - 2) * ts::PKT_SIZE;
          for (auto* p = buf; p < buf + nread; p += ts::PKT_SIZE) {
            ts::NullPacket.copyTo(p);
          }
          return nread;
        });
    EXPECT_CALL(*sink, HandlePacket)
        .Times((FileSource::kReadChunkSize / ts::PKT_SIZE) - 2)
        .WillRepeatedly(testing::Return(true));
    EXPECT_CALL(*file, Read).WillOnce(
        [](uint8_t* buf, size_t) {
          buf[0] = 0;
          buf[1] = ts::SYNC_BYTE;
          buf[2] = 0;
          return 3;
        });
    EXPECT_CALL(*file, Read).WillOnce(
        [](uint8_t* buf, size_t) {
          ts::NullPacket.copyTo(buf);
          return ts::PKT_SIZE;
        });
    EXPECT_CALL(*file, Read).WillOnce(
        [](uint8_t* buf, size_t) {
          static constexpr size_t kNBytes = 4 * ts::PKT_SIZE;
          for (auto* p = buf; p < buf + kNBytes; p += ts::PKT_SIZE) {
            ts::NullPacket.copyTo(p);
          }
          return kNBytes;
        });
    EXPECT_CALL(*sink, HandlePacket)
        .Times(5).WillRepeatedly(testing::Return(true));
    EXPECT_CALL(*file, Read).WillOnce(testing::Return(0));  // EOF
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  FileSource src(std::move(file));
  src.Connect(std::move(sink));
  src.FeedPackets();
}

TEST(PacketSourceTest, ResyncFailedWithEOF) {
  auto file = std::make_unique<MockFile>();
  auto sink = std::make_unique<MockSink>();

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*file, Read).WillOnce(
        [](uint8_t* buf, size_t) {
          buf[0] = 0;
          return 1;
        });
    EXPECT_CALL(*file, Read).WillOnce(testing::Return(0));  // EOF
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  EXPECT_CALL(*sink, HandlePacket).Times(0);  // Never called

  FileSource src(std::move(file));
  src.Connect(std::move(sink));
  src.FeedPackets();
}

TEST(PacketSourceTest, ResyncFailedWithNoSyncByte) {
  auto file = std::make_unique<MockFile>();
  auto sink = std::make_unique<MockSink>();

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*file, Read).WillOnce(
        [](uint8_t* buf, size_t) {
          buf[0] = 0;
          return 1;
        });
    EXPECT_CALL(*file, Read).WillOnce(
        [](uint8_t* buf, size_t) {
          memset(buf, 0, 5 * ts::PKT_SIZE);
          return 5 * ts::PKT_SIZE;
        });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  EXPECT_CALL(*sink, HandlePacket).Times(0);  // Never called

  FileSource src(std::move(file));
  src.Connect(std::move(sink));
  src.FeedPackets();
}

TEST(PacketSourceTest, Successfully) {
  auto file = std::make_unique<MockFile>();
  auto sink = std::make_unique<MockSink>();

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*file, Read).WillOnce(testing::Return(0));  // EOF
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  EXPECT_CALL(*sink, HandlePacket).Times(0);  // Never called

  FileSource src(std::move(file));
  src.Connect(std::move(sink));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
}

TEST(PacketSourceTest, Unsuccessfully) {
  auto file = std::make_unique<MockFile>();
  auto sink = std::make_unique<MockSink>();

  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*file, Read).WillOnce(testing::Return(0));  // EOF
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_FAILURE));
  }

  EXPECT_CALL(*sink, HandlePacket).Times(0);  // Never called

  FileSource src(std::move(file));
  src.Connect(std::move(sink));
  EXPECT_EQ(EXIT_FAILURE, src.FeedPackets());
}
