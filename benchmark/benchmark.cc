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

#include <benchmark/benchmark.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/null_sink.h>

int main(int argc, char** argv) {
  spdlog::set_default_logger(spdlog::null_logger_st("null"));
  ::benchmark::Initialize(&argc, argv);
  if (::benchmark::ReportUnrecognizedArguments(argc, argv))
    return 1;
  ::benchmark::RunSpecifiedBenchmarks();
  return 0;
}
