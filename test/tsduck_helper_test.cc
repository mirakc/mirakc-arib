#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "tsduck_helper.hh"

TEST(TsduckHelperTest, MakeEventsJsonValue) {
  static const uint8_t kData[] = {
    // event_id
    0x00, 0x01,
    // start_time, duration
    0x00, 0x00, 0x00, 0x00, 0x00, 0x12, 0x34, 0x56,
    // running_status, free_CA_mode, descriptors_loop_length
    0x00, 0x00,
  };
  EitSection eit;
  eit.events_data = kData;
  eit.events_size = sizeof(kData);
  rapidjson::Document doc(rapidjson::kObjectType);
  auto& allocator = doc.GetAllocator();
  auto events = MakeEventsJsonValue(eit, allocator);
  EXPECT_EQ(1, events.Size());
  EXPECT_EQ(1, events[0]["eventId"]);
  EXPECT_EQ(-9 * 3600000, events[0]["startTime"]);
  EXPECT_EQ(12 * 3600000 + 34 * 60000 + 56000, events[0]["duration"]);
  EXPECT_TRUE(events[0]["scrambled"].IsFalse());
  EXPECT_EQ(0, events[0]["descriptors"].Size());
}

TEST(TsduckHelperTest, MakeEventsJsonValue_UndefinedStartTimeAndDuration) {
  static const uint8_t kData[] = {
    // event_id
    0x00, 0x01,
    // start_time(undefined), duration(undefined)
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    // running_status, free_CA_mode, descriptors_loop_length
    0x00, 0x00,
  };
  EitSection eit;
  eit.events_data = kData;
  eit.events_size = sizeof(kData);
  rapidjson::Document doc(rapidjson::kObjectType);
  auto& allocator = doc.GetAllocator();
  auto events = MakeEventsJsonValue(eit, allocator);
  EXPECT_EQ(1, events.Size());
  EXPECT_EQ(1, events[0]["eventId"]);
  EXPECT_TRUE(events[0]["startTime"].IsNull());
  EXPECT_TRUE(events[0]["duration"].IsNull());
  EXPECT_TRUE(events[0]["scrambled"].IsFalse());
  EXPECT_EQ(0, events[0]["descriptors"].Size());
}
