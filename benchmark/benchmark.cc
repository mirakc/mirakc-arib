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
