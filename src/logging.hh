#pragma once

#include <cstdlib>

#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace {

inline void InitLogger(const std::string& name) {
  auto logger = spdlog::stderr_color_st(name);
  if (std::getenv("MIRAKC_ARIB_LOG_NO_TIMESTAMP") != nullptr) {
    logger->set_pattern("%^%L%$ %n %v");
  } else {
    logger->set_pattern("%Y-%m-%dT%H:%M:%S.%f %^%L%$ %n %v");
  }
  spdlog::set_default_logger(logger);
}

}  // namespace

#if defined(MIRAKC_ARIB_ENABLE_LOGGING_SOURCE_LOC)
#define MIRAKC_ARIB_SOURCE_LOC (spdlog::source_loc { __FILE__, __LINE__, SPDLOG_FUNCTION })
#else
#define MIRAKC_ARIB_SOURCE_LOC (spdlog::source_loc {})
#endif

#define MIRAKC_ARIB_LOG(...) spdlog::log(MIRAKC_ARIB_SOURCE_LOC, __VA_ARGS__)
#define MIRAKC_ARIB_TRACE(...) MIRAKC_ARIB_LOG(spdlog::level::trace, __VA_ARGS__)
#define MIRAKC_ARIB_DEBUG(...) MIRAKC_ARIB_LOG(spdlog::level::debug, __VA_ARGS__)
#define MIRAKC_ARIB_INFO(...) MIRAKC_ARIB_LOG(spdlog::level::info, __VA_ARGS__)
#define MIRAKC_ARIB_WARN(...) MIRAKC_ARIB_LOG(spdlog::level::warn, __VA_ARGS__)
#define MIRAKC_ARIB_ERROR(...) MIRAKC_ARIB_LOG(spdlog::level::err, __VA_ARGS__)

// In the future, the following macros might be disabled in the release build if
// that improves the performance significantly.

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

#if defined(MIRAKC_ARIB_DISABLE_LOGGING)
#undef MIRAKC_ARIB_LOG
#undef MIRAKC_ARIB_TRACE
#undef MIRAKC_ARIB_DEBUG
#undef MIRAKC_ARIB_INFO
#undef MIRAKC_ARIB_WARN
#undef MIRAKC_ARIB_ERROR
#undef MIRAKC_ARIB_ASSERT
#undef MIRAKC_ARIB_ASSERT_MSG
#undef MIRAKC_ARIB_NEVER_REACH
#define MIRAKC_ARIB_LOG(...) ((void)0)
#define MIRAKC_ARIB_TRACE(...) ((void)0)
#define MIRAKC_ARIB_DEBUG(...) ((void)0)
#define MIRAKC_ARIB_INFO(...) ((void)0)
#define MIRAKC_ARIB_WARN(...) ((void)0)
#define MIRAKC_ARIB_ERROR(...) ((void)0)
#define MIRAKC_ARIB_ASSERT(cond) ((void)0)
#define MIRAKC_ARIB_ASSERT_MSG(cond, ...) ((void)0)
#define MIRAKC_ARIB_NEVER_REACH(...) ((void)0)
#endif  // defined(MIRAKC_ARIB_DISABLE_LOGGING)
