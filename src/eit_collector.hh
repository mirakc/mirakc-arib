#pragma once

#include <map>
#include <memory>
#include <sstream>

#include <LibISDB/LibISDB.hpp>
#include <LibISDB/EPG/EventInfo.hpp>
#include <fmt/format.h>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <tsduck/tsduck.h>

#include "base.hh"
#include "jsonl_source.hh"
#include "logging.hh"
#include "packet_source.hh"
#include "tsduck_helper.hh"

namespace {

struct EitCollectorOption final {
  SidSet sids;
  SidSet xsids;
  ts::MilliSecond time_limit = 30 * ts::MilliSecPerSec;  // 30s
  bool streaming = false;
};

class TableProgress {
 public:
  TableProgress() {
    for (size_t i = 0; i < kNumSections; ++i) {
      section_versions_[i] = 0xFF;  // means no version is stored.
    }
  }

  ~TableProgress() = default;

  void Reset() {
    for (size_t i = 0; i < kNumSegments; ++i) {
      collected_[i] = 0;
      unused_[i] = 0;
    }
    completed_ = false;
  }

  void Unuse() {
    for (size_t i = 0; i < kNumSegments; ++i) {
      unused_[i] = 0xFF;
    }
    completed_ = true;
  }

  void Update(const EitSection& eit) {
    if (!CheckConsistency(eit)) {
      Reset();
    }

    for (auto i = eit.last_segment_index() + 1; i < kNumSegments; ++i) {
      unused_[i] = 0xFF;
    }

    for (auto i = eit.last_section_index() + 1; i < 8; ++i) {
      unused_[eit.segment_index()] |= 1 << i;
    }

    collected_[eit.segment_index()] |= 1 << eit.section_index();

    for (size_t i = eit.section_index(); i <= eit.last_section_index(); ++i) {
      if (section_versions_[i] != 0xFF && section_versions_[i] != eit.version) {
        MIRAKC_ARIB_INFO("  Version changed: sec#{:02X}: {:02d} -> {:02d}",
                         i, section_versions_[i], eit.version);
      }
      section_versions_[i] = eit.version;
    }

    completed_ = CheckCompleted();
  }

  void UpdateUnused(const ts::Time& timestamp) {
    size_t segment = ((ts::Time::Fields)(timestamp)).hour / 3;
    for (size_t i = 0; i < segment; ++i) {
      unused_[i] = 0xFF;
    }

    completed_ = CheckCompleted();
  }

  bool CheckCollected(const EitSection& eit) const {
    for (size_t i = eit.section_index(); i < eit.last_section_index(); ++i) {
      if (section_versions_[i] == 0xFF) {
        return false;
      }
      if (section_versions_[i] != eit.version) {
        return false;
      }
    }
    auto mask = 1 << eit.section_index();
    return (collected_[eit.segment_index()] & mask) != 0;
  }

  bool IsCompleted() const {
    return completed_;
  }

  void Show(size_t index) const {
    MIRAKC_ARIB_TRACE("      {}: {:3d}/256", index, CalcProgressCount());
    std::stringstream ss;
    for (size_t i = 0; i < kNumSegments; ++i) {
      ss << '[';
      for (uint8_t j = 0; j < 8; ++j) {
        auto mask = 1 << j;
        if (unused_[i] & mask) {
          ss << '.';
        } else if (collected_[i] & mask) {
          ss << '*';
        } else {
          ss << ' ';
        }
      }
      ss << ']';
      if (i % 8 == 7) {
        MIRAKC_ARIB_TRACE("         {}", ss.str());
        ss.str("");
      }
    }
  }

  size_t CountSections() const {
    size_t n = 0;
    for (size_t i = 0; i < kNumSegments; ++i) {
      for (uint8_t j = 0; j < 8; ++j) {
        auto mask = 1 << j;
        if (collected_[i] & mask) {
          n++;
        }
      }
    }
    return n;
  }

 private:
  static constexpr size_t kNumSections = 256;
  static constexpr size_t kNumSegments = kNumSections / 8;

  bool CheckConsistency(const EitSection& eit) {
    // NOTE:
    //
    // Many implementations processing tables assume that the version number of
    // a table is applied to all sections included in the table.  As a natural
    // consequence, they reset the all sections in the table when the version
    // number in a section of the table is changed.
    //
    // But the definition of the sub table is unclear.  A table consists of a
    // number of sub tables which have a same table_id.  The sub table is
    // defined as a collection of sections having a same version number.  But
    // there is no description about how many sections are included in a sub
    // table.
    //
    // Actually, a TS stream contains a table which includes sections of
    // different versions like below:
    //
    //   Section(00): version(12)
    //   Section(08): version(12)
    //   Section(10): version(11)
    //   Section(18): version(11)
    //   ...
    //
    // In the case above, IsCompleted() never returns true.  As a result,
    // this program never stops.
    //
    // For avoiding the situation above, this method doesn't check whether the
    // current table version number is equal to `eit.version`, and always
    // returns true.
    return true;
  }

  bool CheckCompleted() const {
    for (size_t i = 0; i < kNumSegments; ++i) {
      if ((collected_[i] | unused_[i]) != 0xFF) {
        return false;
      }
    }
    return true;
  }

  int CalcProgressCount() const {
    int count = 0;
    for (size_t i = 0; i < kNumSegments; ++i) {
      auto progress = (collected_[i] | unused_[i]);
      for (uint8_t j = 0; j < 8; ++j) {
        if (progress & (1 << j)) {
          ++count;
        }
      }
    }
    return count;
  }

  // Bitmap
  uint8_t collected_[kNumSegments] = { 0 };
  uint8_t unused_[kNumSegments] = { 0 };
  uint8_t section_versions_[kNumSections];
  bool completed_ = false;

  MIRAKC_ARIB_NON_COPYABLE(TableProgress);
};

class TableGroupProgress {
 public:
  TableGroupProgress() = default;
  ~TableGroupProgress() = default;

  void Update(const EitSection& eit) {
    if (!CheckConsistency(eit)) {
      for (size_t i = 0; i < kNumTables; ++i) {
        tables_[i].Reset();
      }
      for (auto i = eit.last_table_index() + 1; i < kNumTables; ++i) {
        tables_[i].Unuse();
      }
      completed_ = false;
    }

    tables_[eit.table_index()].Update(eit);
    last_table_index_ = eit.last_table_index();

    completed_ = CheckCompleted();
  }

  inline void UpdateUnused(const ts::Time& timestamp) {
    tables_[0].UpdateUnused(timestamp);
    completed_ = CheckCompleted();
  }

  bool CheckCollected(const EitSection& eit) const {
    if (last_table_index_ < 0) {
      return false;
    }
    if (last_table_index_ != eit.last_table_index()) {
      return false;
    }
    return tables_[eit.table_index()].CheckCollected(eit);
  }

  bool IsCompleted() const {
    if (last_table_index_ < 0) {
      return true;
    }
    return completed_;
  }

  void Show(const char* label) const {
    MIRAKC_ARIB_TRACE("    {}: last-table-index({}), ltid-changed({})",
                      label, last_table_index_, last_table_index_change_count_);
    for (size_t i = 0; i < kNumTables; ++i) {
      if (tables_[i].IsCompleted()) {
        continue;
      }
      tables_[i].Show(i);
    }
  }

  size_t CountSections() const {
    size_t n = 0;
    for (size_t i = 0; i < kNumTables; ++i) {
      n += tables_[i].CountSections();
    }
    return n;
  }

 private:
  static constexpr size_t kNumTables = 8;

  bool CheckConsistency(const EitSection& eit) {
    if (last_table_index_ < 0) {
      return false;
    }
    if (last_table_index_ != eit.last_table_index()) {
      MIRAKC_ARIB_INFO("  Last table index changed: {} -> {}",
                       last_table_index_, eit.last_table_index());
      last_table_index_change_count_++;
      return false;
    }
    return true;
  }

  bool CheckCompleted() const {
    for (size_t i = 0; i < kNumTables; ++i) {
      if (!tables_[i].IsCompleted()) {
        return false;
      }
    }
    return true;
  }

  TableProgress tables_[kNumTables];
  int last_table_index_ = -1;
  int last_table_index_change_count_ = 0;
  bool completed_ = false;

  MIRAKC_ARIB_NON_COPYABLE(TableGroupProgress);
};

class ServiceProgress {
 public:
  ServiceProgress() = default;
  ~ServiceProgress() = default;

  void Update(const EitSection& eit) {
    if (eit.IsBasic()) {
      basic_.Update(eit);
    } else {
      extra_.Update(eit);
    }
  }

  inline void UpdateUnused(const ts::Time& timestamp) {
    basic_.UpdateUnused(timestamp);
    extra_.UpdateUnused(timestamp);
  }

  bool CheckCollected(const EitSection& eit) const {
    if (eit.IsBasic()) {
      return basic_.CheckCollected(eit);
    }
    return extra_.CheckCollected(eit);
  }

  bool IsCompleted() const {
    return basic_.IsCompleted() && extra_.IsCompleted();
  }

  void Show(uint32_t id) const {
    MIRAKC_ARIB_TRACE("  {:08X}:", id);
    if (!basic_.IsCompleted()) {
      basic_.Show("basic");
    }
    if (!extra_.IsCompleted()) {
      extra_.Show("extra");
    }
  }

  size_t CountSections() const {
    return basic_.CountSections() + extra_.CountSections();
  }

 private:
  TableGroupProgress basic_;
  TableGroupProgress extra_;

  MIRAKC_ARIB_NON_COPYABLE(ServiceProgress);
};

class CollectProgress {
 public:
  CollectProgress() = default;
  ~CollectProgress() = default;

  void Update(const EitSection& eit) {
    services_[eit.service_triple()].Update(eit);
    completed_ = CheckCompleted();
  }

  void UpdateUnused(const ts::Time& timestamp) {
    for (auto& pair : services_) {
      pair.second.UpdateUnused(timestamp);
    }
  }

  bool CheckCollected(const EitSection& eit) const {
    if (services_.find(eit.service_triple()) == services_.end()) {
      return false;
    }
    return services_.at(eit.service_triple()).CheckCollected(eit);
  }

  bool IsCompleted() const {
    return completed_;
  }

  void Show() const {
    if (IsCompleted()) {
      return;
    }
    MIRAKC_ARIB_TRACE("Progress:");
    for (const auto& pair : services_) {
      if (pair.second.IsCompleted()) {
        continue;
      }
      pair.second.Show(pair.first);
    }
  }

  size_t CountServices() const {
    return services_.size();
  }

  size_t CountSections() const {
    size_t n = 0;
    for (const auto& pair : services_) {
      n += pair.second.CountSections();
    }
    return n;
  }

 private:
  bool CheckCompleted() const {
    for (const auto& pair : services_) {
      if (!pair.second.IsCompleted()) {
        return false;
      }
    }
    return true;
  }

  std::map<uint64_t, ServiceProgress> services_;
  bool completed_ = false;

  MIRAKC_ARIB_NON_COPYABLE(CollectProgress);
};

class EitCollector final : public PacketSink,
                           public JsonlSource,
                           public ts::SectionHandlerInterface,
                           public ts::TableHandlerInterface {
 public:
  explicit EitCollector(const EitCollectorOption& option)
      : option_(option),
        demux_(context_) {
    if (spdlog::default_logger()->should_log(spdlog::level::trace)) {
      EnableShowProgress();
    }

    demux_.setSectionHandler(this);
    demux_.setTableHandler(this);
    demux_.addPID(ts::PID_EIT);
    demux_.addPID(ts::PID_TOT);
  }

  ~EitCollector() override {}

  bool Start() override {
    start_time_ = ts::Time::CurrentUTC();
    return true;
  }

  bool End() override {
    auto elapse = ts::Time::CurrentUTC() - start_time_;
    auto min = elapse / ts::MilliSecPerMin;
    auto sec = (elapse - min * ts::MilliSecPerMin) / ts::MilliSecPerSec;
    auto ms = elapse % ts::MilliSecPerSec;
    MIRAKC_ARIB_INFO(
        "Collected {} services, {} sections, {}:{:02d}.{:03d} elapsed",
        progress_.CountServices(), progress_.CountSections(), min, sec, ms);
    return IsCompleted();
  }

  bool HandlePacket(const ts::TSPacket& packet) override {
    demux_.feedPacket(packet);
    if (IsCompleted()) {
      MIRAKC_ARIB_INFO("Completed");
      return false;
    }
    if (CheckTimeout()) {
      MIRAKC_ARIB_ERROR("Timed out");
      return false;
    }
    return true;
  }

 private:
  void handleSection(ts::SectionDemux&, const ts::Section& section) override {
    if (!section.isValid()) {
      return;
    }

    const auto tid = section.tableId();
    if (tid < ts::TID_EIT_MIN || tid > ts::TID_EIT_MAX) {
      return;
    }
    if (tid == ts::TID_EIT_PF_ACT || tid == ts::TID_EIT_PF_OTH) {
      return;
    }

    if (section.isNext()) {
      return;
    }

    if (section.payloadSize() < EitSection::EIT_PAYLOAD_FIXED_SIZE) {
      return;
    }

    EitSection eit(section);
    if (!option_.sids.IsEmpty() && !option_.sids.Contain(eit.sid)) {
      MIRAKC_ARIB_DEBUG(
          "Ignore SID#{:04X} according to the inclusion list", eit.sid);
      return;
    }
    if (!option_.xsids.IsEmpty() && option_.xsids.Contain(eit.sid)) {
      MIRAKC_ARIB_DEBUG(
          "Ignore SID#{:04X} according to the exclusion list", eit.sid);
      return;
    }
    if (CheckCollected(eit)) {
      return;
    }

    MIRAKC_ARIB_INFO(
        "EIT: onid({:04X}) tsid({:04X}) sid({:04X}) tid({:04X}/{:02X})"
        " sec({:02X}:{:02X}/{:02X}) ver({:02d})",
        eit.nid, eit.tsid, eit.sid, eit.tid, eit.last_table_id,
        eit.section_number, eit.segment_last_section_number,
        eit.last_section_number, eit.version);

    WriteEitSection(eit);
    UpdateProgress(eit);
  }

  void handleTable(ts::SectionDemux&, const ts::BinaryTable& table) override {
    // In ARIB, the timezone of TDT/TOT is JST.
    switch (table.tableId()) {
      case ts::TID_TDT:
        HandleTdt(table);
        break;
      case ts::TID_TOT:
        HandleTot(table);
        break;
      default:
        break;
    }
  }

  inline void HandleTdt(const ts::BinaryTable& table) {
    ts::TDT tdt(context_, table);

    if (!tdt.isValid()) {
      MIRAKC_ARIB_WARN("Broken TDT, skip");
      return;
    }

    MIRAKC_ARIB_INFO("TDT: {}", tdt.utc_time);
    HandleTime(tdt.utc_time);
  }

  inline void HandleTot(const ts::BinaryTable& table) {
    ts::TOT tot(context_, table);

    if (!tot.isValid()) {
      MIRAKC_ARIB_WARN("Broken TOT, skip");
      return;
    }

    MIRAKC_ARIB_INFO("TOT: {}", tot.utc_time);
    HandleTime(tot.utc_time);
  }

  inline void HandleTime(const ts::Time& time) {
    timestamp_ = time;

    progress_.UpdateUnused(timestamp_);

    if (!has_timestamp_) {
      last_updated_ = timestamp_;
      has_timestamp_ = true;
    }
  }

  inline bool CheckCollected(const EitSection& eit) const {
    return progress_.CheckCollected(eit);
  }

  void WriteEitSection(const EitSection& eit) {
    FeedDocument(MakeJsonValue(eit));
  }

  void UpdateProgress(const EitSection& eit) {
    last_updated_ = timestamp_;
    progress_.Update(eit);
    if (show_progress_) {
      progress_.Show();
    }
  }

  inline bool IsCompleted() const {
    if (option_.streaming) {
      return false;
    }
    return progress_.IsCompleted();
  }

  inline bool CheckTimeout() const {
    if (option_.streaming) {
      return false;
    }
    auto elapsed = timestamp_ - last_updated_;
    return elapsed >= option_.time_limit;
  }

  inline void EnableShowProgress() {
    show_progress_ = true;
  }

  const EitCollectorOption option_;
  ts::DuckContext context_;
  ts::SectionDemux demux_;
  bool has_timestamp_ = false;
  ts::Time timestamp_;  // JST
  ts::Time last_updated_;  // JST
  CollectProgress progress_;
  bool show_progress_ = false;
  ts::Time start_time_;  // UTC

  MIRAKC_ARIB_NON_COPYABLE(EitCollector);
};

}  // namespace
