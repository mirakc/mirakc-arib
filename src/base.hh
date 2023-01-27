#pragma once

#include <string>
#include <unordered_set>

#include <docopt/docopt.h>
#include <tsduck/tsduck.h>

#include "tsduck_helper.hh"

namespace {

constexpr size_t kBlockSize = 4096;

#define MIRAKC_ARIB_NON_COPYABLE(clss) \
  clss(const clss&) = delete; \
  clss& operator=(const clss&) = delete

#define MIRAKC_ARIB_EXPECTS(cond) ((void)((cond) ? 0 : spdlog::critical("`" #cond "` failed")))

inline std::string& trim(std::string& str) {
  static const char kWhitespaces[] = "\n";
  return str.erase(0, str.find_first_not_of(kWhitespaces))
      .erase(str.find_last_not_of(kWhitespaces) + 1);
}

using Args = std::map<std::string, docopt::value>;

class SidSet {
 public:
  SidSet() = default;
  ~SidSet() = default;

  explicit SidSet(const SidSet& set) : set_(set.set_) {}

  inline bool IsEmpty() const {
    return set_.empty();
  }

  inline void Add(const std::vector<std::string>& sids) {
    for (const auto& str : sids) {
      size_t pos;
      uint16_t sid = static_cast<uint16_t>(std::stoi(str, &pos));
      if (pos != str.length()) {
        // non digits, ignore
        continue;
      }
      Add(sid);
    }
  }

  inline void Add(uint16_t sid) {
    set_.insert(sid);
  }

  inline bool Contain(uint16_t sid) const {
    return set_.find(sid) != set_.end();
  }

  inline size_t size() const {
    return set_.size();
  }

 private:
  explicit SidSet(SidSet&& set) = delete;

  std::unordered_set<uint16_t> set_;
};

class ClockBaseline final {
 public:
  ClockBaseline() = default;

  explicit ClockBaseline(const ClockBaseline& cbl) = default;

  ~ClockBaseline() = default;

  ts::PID pid() const {
    return pid_;
  }

  int64_t pcr() const {
    return pcr_;
  }

  const ts::Time& time() const {
    return time_;
  }

  ts::Time PcrToTime(int64_t pcr) const {
    MIRAKC_ARIB_ASSERT(IsReady());
    auto delta_ms = (pcr - pcr_) / kPcrTicksPerMs;
    return time_ + delta_ms;
  }

  int64_t TimeToPcr(const ts::Time& time) const {
    MIRAKC_ARIB_ASSERT(IsReady());
    auto ms = time - time_;  // may be a negative value
    auto pcr = pcr_ + ms * kPcrTicksPerMs;
    while (pcr < 0) {
      pcr += kPcrUpperBound;
    }
    MIRAKC_ARIB_ASSERT(pcr >= 0);
    return pcr % kPcrUpperBound;
  }

  bool HasPid() const {
    return pid_ != ts::PID_NULL;
  }

  bool IsReady() const {
    return pcr_ready_ && time_ready_;
  }

  void SetPid(ts::PID pid) {
    pid_ = pid;
    pcr_ready_ = false;
    time_ready_ = false;
  }

  void SetPcr(int64_t pcr) {
    MIRAKC_ARIB_ASSERT(IsValidPcr(pcr));
    pcr_ = pcr;
    pcr_ready_ = true;
    MIRAKC_ARIB_TRACE("Updated baseline clock PCR: {:011X}", pcr);
  }

  void SetTime(const ts::Time& time) {
    time_ = time;
    time_ready_ = true;
    MIRAKC_ARIB_TRACE("Updated baseline clock time: {}", time);
  }

  void Invalidate() {
    pcr_ready_ = false;
    time_ready_ = false;
  }

  ClockBaseline& operator=(const ClockBaseline& rhs) = default;

 private:
  ts::Time time_;  // JST
  int64_t pcr_ = 0;
  ts::PID pid_ = ts::PID_NULL;
  bool pcr_ready_ = false;
  bool time_ready_ = false;
};

class Clock final {
 public:
  Clock() = default;

  explicit Clock(const ClockBaseline& cbl) : baseline_(cbl) {}

  ~Clock() = default;

  ts::PID pid() const {
    return baseline_.pid();
  }

  bool HasPid() const {
    return baseline_.HasPid();
  }

  bool IsReady() const {
    return ready_ && baseline_.IsReady();
  }

  ts::Time Now() const {
    if (IsReady()) {
      auto last_pcr = last_pcr_;
      if (pcr_wrap_around_) {
        last_pcr += kPcrUpperBound;
        MIRAKC_ARIB_ASSERT(last_pcr > 0);
      }
      return baseline_.PcrToTime(last_pcr);
    }
    // Compute the current TS time using the current local time while switching
    // the PCR PID.
    auto delta = ts::Time::CurrentLocalTime() - baseline_local_time_;
    return baseline_.time() + delta;
  }

  void SetPid(ts::PID pid) {
    baseline_.SetPid(pid);
    ready_ = false;
  }

  void UpdateTime(const ts::Time& time) {
    baseline_.SetTime(time);
    baseline_local_time_ = ts::Time::CurrentLocalTime();
    if (ready_) {
      SyncPcr();
    }
  }

  void UpdatePcr(int64_t pcr) {
    MIRAKC_ARIB_ASSERT(IsValidPcr(pcr));
    if (IsReady()) {
      auto delta = ComputeDelta(pcr, last_pcr_);
      MIRAKC_ARIB_ASSERT(delta >= 0);
      if (delta >= kPcrTicksPerSec) {  // delta >= 1s
        MIRAKC_ARIB_WARN("PCR#{:04X}: too large delta {} -> {}, invalidate the clock for resync",
            baseline_.pid(), FormatPcr(last_pcr_), FormatPcr(pcr));
        Invalidate();
        return;
      }
    }
    if (pcr < last_pcr_) {
      MIRAKC_ARIB_DEBUG("PCR#{:04X}: wrap-around {} -> {}", baseline_.pid(), FormatPcr(last_pcr_),
          FormatPcr(pcr));
      pcr_wrap_around_ = true;
    }
    last_pcr_ = pcr;
    ready_ = true;
    if (!baseline_.IsReady()) {
      SyncPcr();
    }
  }

  int64_t TimeToPcr(const ts::Time& time) const {
    return baseline_.TimeToPcr(time);
  }

  ts::Time PcrToTime(int64_t pcr) const {
    MIRAKC_ARIB_ASSERT(IsValidPcr(pcr));
    return baseline_.PcrToTime(pcr);
  }

 private:
  static int64_t ComputeDelta(int64_t pcr, int64_t base_pcr) {
    if (pcr < base_pcr) {
      return kPcrUpperBound - base_pcr + pcr;
    }
    return pcr - base_pcr;
  }

  void Invalidate() {
    baseline_.Invalidate();
    last_pcr_ = 0;
    ready_ = false;
    pcr_wrap_around_ = false;
  }

  void SyncPcr() {
    baseline_.SetPcr(last_pcr_);
    pcr_wrap_around_ = false;
  }

  ClockBaseline baseline_;
  ts::Time baseline_local_time_;
  int64_t last_pcr_ = 0;
  bool ready_ = false;
  bool pcr_wrap_around_ = false;
};

}  // namespace
