#include <cstring>

#include <gtest/gtest.h>

#include "packet_buffer.hh"
#include "test_helper.hh"

TEST(PacketBufferTest, Case1) {
  static const uint8_t kData[] = { 1, 2 };

  PacketBuffer buffer(4);
  buffer.Write(kData, sizeof(kData));

  MockStreamSink sink;
  {
    testing::InSequence seq;
    EXPECT_CALL(sink, Write).WillOnce(
        [](const uint8_t* data, size_t size) {
          static const uint8_t kExpected[] = { 1, 2 };
          EXPECT_EQ(sizeof(kExpected), size);
          EXPECT_EQ(0, std::memcmp(data, kExpected, sizeof(kExpected)));
          return true;
        });
  }
  EXPECT_TRUE(buffer.Flush(&sink));
}

TEST(PacketBufferTest, Case2) {
  static const uint8_t kData[] = { 1, 2, 3, 4, 5 };

  PacketBuffer buffer(4);
  buffer.Write(kData, sizeof(kData));

  MockStreamSink sink;
  {
    testing::InSequence seq;
    EXPECT_CALL(sink, Write).WillOnce(
        [](const uint8_t* data, size_t size) {
          static const uint8_t kExpected[] = { 2, 3, 4 };
          EXPECT_EQ(sizeof(kExpected), size);
          EXPECT_EQ(0, std::memcmp(data, kExpected, sizeof(kExpected)));
          return true;
        });
    EXPECT_CALL(sink, Write).WillOnce(
        [](const uint8_t* data, size_t size) {
          static const uint8_t kExpected[] = { 5 };
          EXPECT_EQ(sizeof(kExpected), size);
          EXPECT_EQ(0, std::memcmp(data, kExpected, sizeof(kExpected)));
          return true;
        });
  }
  EXPECT_TRUE(buffer.Flush(&sink));
}

TEST(PacketBufferTest, Case3) {
  static const uint8_t kData1[] = { 1, 2, 3, 4, 5 };
  static const uint8_t kData2[] = { 6, 7 };

  PacketBuffer buffer(4);
  buffer.Write(kData1, sizeof(kData1));
  buffer.Write(kData2, sizeof(kData2));

  MockStreamSink sink;
  {
    testing::InSequence seq;
    EXPECT_CALL(sink, Write).WillOnce(
        [](const uint8_t* data, size_t size) {
          static const uint8_t kExpected[] = { 4 };
          EXPECT_EQ(sizeof(kExpected), size);
          EXPECT_EQ(0, std::memcmp(data, kExpected, sizeof(kExpected)));
          return true;
        });
    EXPECT_CALL(sink, Write).WillOnce(
        [](const uint8_t* data, size_t size) {
          static const uint8_t kExpected[] = { 5, 6, 7 };
          EXPECT_EQ(sizeof(kExpected), size);
          EXPECT_EQ(0, std::memcmp(data, kExpected, sizeof(kExpected)));
          return true;
        });
  }
  EXPECT_TRUE(buffer.Flush(&sink));
}

TEST(PacketBufferTest, Case4) {
  static const uint8_t kData1[] = { 1, 2, 3, 4, 5 };
  static const uint8_t kData2[] = { 6, 7, 8, 9, 10 };

  PacketBuffer buffer(4);
  buffer.Write(kData1, sizeof(kData1));
  buffer.Write(kData2, sizeof(kData2));

  MockStreamSink sink;
  {
    testing::InSequence seq;
    EXPECT_CALL(sink, Write).WillOnce(
        [](const uint8_t* data, size_t size) {
          static const uint8_t kExpected[] = { 7, 8 };
          EXPECT_EQ(sizeof(kExpected), size);
          EXPECT_EQ(0, std::memcmp(data, kExpected, sizeof(kExpected)));
          return true;
        });
    EXPECT_CALL(sink, Write).WillOnce(
        [](const uint8_t* data, size_t size) {
          static const uint8_t kExpected[] = { 9, 10 };
          EXPECT_EQ(sizeof(kExpected), size);
          EXPECT_EQ(0, std::memcmp(data, kExpected, sizeof(kExpected)));
          return true;
        });
  }
  EXPECT_TRUE(buffer.Flush(&sink));
}
