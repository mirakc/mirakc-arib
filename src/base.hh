#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>

#include <tsduck/tsduck.h>
#include <docopt/docopt.h>

namespace {

#define MIRAKC_ARIB_NON_COPYABLE(clss) \
  clss(const clss&) = delete; \
  clss& operator=(const clss&) = delete

#define MIRAKC_ARIB_EXPECTS(cond) \
  ((void)((cond) ? 0 : spdlog::critical("`" #cond "` failed")))

inline std::string& trim(std::string& str) {
  static const char kWhitespaces[] = "\n";
  return str
      .erase(0, str.find_first_not_of(kWhitespaces))
      .erase(str.find_last_not_of(kWhitespaces) + 1);
}

using Args = std::map<std::string, docopt::value>;

constexpr ts::MilliSecond kJstTzOffset = 9 * ts::MilliSecPerHour;

class ExcludedSidSet {
 public:
  ExcludedSidSet() = default;
  ~ExcludedSidSet() = default;

  explicit ExcludedSidSet(const ExcludedSidSet& set) : set_(set.set_) {}

  void Add(const std::vector<std::string>& sids) {
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

 private:
  explicit ExcludedSidSet(ExcludedSidSet&& set) = delete;

  std::unordered_set<uint16_t> set_;
};

}  // namespace
