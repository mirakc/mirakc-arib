#pragma once

#include <cstdlib>

#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace {

inline spdlog::level::level_enum GetLogLevel() {
  const auto* v = std::getenv("MIRAKC_ARIB_LOG");
  if (v == nullptr) {
    return spdlog::level::off;
  }
  return spdlog::level::from_str(std::string(v));
}

inline void InitLogger(const std::string& name) {
  auto logger = spdlog::stderr_color_st(name);
  spdlog::set_default_logger(logger);
  spdlog::set_level(GetLogLevel());
}

#if MIRAKC_ARIB_LOG_SHOW_SOURCE_LOC
#define MIRAKC_ARIB_SOURCE_LOC \
  (spdlog::source_loc { __FILE__, __LINE__, SPDLOG_FUNCTION })
#else
#define MIRAKC_ARIB_SOURCE_LOC (spdlog::source_loc {})
#endif

#define MIRAKC_ARIB_LOG(...) spdlog::log(MIRAKC_ARIB_SOURCE_LOC, __VA_ARGS__)
#define MIRAKC_ARIB_DEBUG(...) MIRAKC_ARIB_LOG(spdlog::level::debug, __VA_ARGS__)
#define MIRAKC_ARIB_INFO(...) MIRAKC_ARIB_LOG(spdlog::level::info, __VA_ARGS__)
#define MIRAKC_ARIB_WARN(...) MIRAKC_ARIB_LOG(spdlog::level::warn, __VA_ARGS__)
#define MIRAKC_ARIB_ERROR(...) MIRAKC_ARIB_LOG(spdlog::level::err, __VA_ARGS__)

#define MIRAKC_ARIB_ASSERT(cond) \
  ((cond) ? (void)0 : \
   (MIRAKC_ARIB_LOG(spdlog::level::critical, \
                    "Assertion failed: " #cond), std::abort()))

#define MIRAKC_ARIB_ASSERT_MSG(cond, ...) \
  ((cond) ? (void)0 : \
   (MIRAKC_ARIB_LOG(spdlog::level::critical, \
                    "Assertion failed: " #cond ": " __VA_ARGS__), std::abort()))

#define MIRAKC_ARIB_NEVER_REACH(...) \
  (MIRAKC_ARIB_LOG(spdlog::level::critical, __VA_ARGS__), std::abort())

}  // namespace
