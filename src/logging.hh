// Copyright (c) 2019 Masayuki Nagamachi <masayuki.nagamachi@gmail.com>
//
// Licensed under either of
//
//   * Apache License, Version 2.0
//     (LICENSE-APACHE or http://www.apache.org/licenses/LICENSE-2.0)
//   * MIT License
//     (LICENSE-MIT or http://opensource.org/licenses/MIT)
//
// at your option.

#pragma once

#include <cstdlib>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace {

inline spdlog::level::level_enum GetLogLevel() {
  const auto* v = std::getenv("MIRAKC_ARIB_LOG");
  if (v == nullptr) {
    return spdlog::level::off;
  }
  return spdlog::level::from_str(std::string(v));
}

inline void InitLogger(const char* name) {
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
  ((cond) ? (void)0 : (MIRAKC_ARIB_LOG( \
      spdlog::level::critical, "Assertion failed: " #cond), std::abort()))

}  // namespace
