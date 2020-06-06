cmake_minimum_required(VERSION 3.5.0)

project(mirakc-arib)

if(NOT UNIX)
  message(FATAL_ERROR "Can be built only on Linux and macOS at this moment")
endif()

option(MIRAKC_ARIB_TEST "Build tests." OFF)
option(MIRAKC_ARIB_VENDOR_TEST "Build tests in vendor libs." OFF)

set(MIRAKC_ARIB_VERSION
  "0.4.1"
  CACHE STRING "mirakc-arib version." FORCE)
set(MIRAKC_ARIB_DOCOPT_VERSION
  "715aca4846fddc61649f8e363c029f7fd3e21c2c"
  CACHE STRING "docopt.cpp version to use." FORCE)
set(MIRAKC_ARIB_FMT_VERSION
  "6.2.1"
  CACHE STRING "fmt version to use." FORCE)
set(MIRAKC_ARIB_SPDLOG_VERSION
  "v1.6.1"
  CACHE STRING "spdlog version to use." FORCE)
set(MIRAKC_ARIB_RAPIDJSON_VERSION
  "v1.1.0"
  CACHE STRING "RapidJSON version to use." FORCE)
set(MIRAKC_ARIB_CPPCODEC_VERSION
  "bd6ddf95129e769b50ef63e0f558fa21364f3f65"
  CACHE STRING "cppcodec version to use." FORCE)
set(MIRAKC_ARIB_ARIBB24_VERSION
  "654026e064ec127beff43bf3ecdcebd54119be15"
  CACHE STRING "aribb24 version to use." FORCE)
set(MIRAKC_ARIB_TSDUCK_ARIB_VERSION
  "c400025b7d31e26c0c15471e81adf2ad50632281"
  CACHE STRING "tsduck-arib version to use." FORCE)
set(MIRAKC_ARIB_GOOGLETEST_VERSION
  "release-1.10.0"
  CACHE STRING "googletest version to use." FORCE)
set(MIRAKC_ARIB_GOOGLE_BENCHMARK_VERSION
  "v1.5.0"
  CACHE STRING "Google Benchmark version to use." FORCE)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS OFF)

if($ENV{MIRAKC_ARIB_CI})
  # Disable parallel compilation.
  #
  # Parallel compilation in a CI service causes a GCC internal errors like
  # below:
  #
  #   In file included from tsEnumeration.h:36,
  #          from tsMPEG.h:39,
  #          from tsAbstractDefinedByStandards.h:36,
  #          from tsAbstractSignalization.h:36,
  #          from tsAbstractDescriptor.h:36,
  #          from tsSystemClockDescriptor.h:36,
  #          from tsSystemClockDescriptor.cpp:30:
  #   tsUString.h: In destructor 'ts::UString::~UString()':
  #   tsUString.h:141:21: internal compiler error: Segmentation fault
  #        class TSDUCKDLL UString: public std::u16string
  #                        ^~~~~~~
  #   Please submit a full bug report,
  #   with preprocessed source if appropriate.
  #   See <file:///usr/share/doc/gcc-8/README.Bugs> for instructions.
  #
  # Or:
  #
  #   cc1plus: internal compiler error: Segmentation fault
  #   Please submit a full bug report,
  #   with preprocessed source if appropriate.
  #   See <file:///usr/share/doc/gcc-8/README.Bugs> for instructions.
  set(NPROC 1)
else()
  include(ProcessorCount)
  ProcessorCount(NPROC)
endif()

# Display general information
message(STATUS "Create build system with the following variables:")
message(STATUS "  CMAKE_BUILD_TYPE                : ${CMAKE_BUILD_TYPE}")
message(STATUS "  CMAKE_HOST_SYSTEM_NAME          : ${CMAKE_HOST_SYSTEM_NAME}")
message(STATUS "  CMAKE_HOST_SYSTEM_PROCESSOR     : ${CMAKE_HOST_SYSTEM_PROCESSOR}")
message(STATUS "  CMAKE_SYSTEM_NAME               : ${CMAKE_SYSTEM_NAME}")
message(STATUS "  CMAKE_SYSTEM_PROCESSOR          : ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "  CMAKE_RUNTIME_OUTPUT_DIRECTORY  : ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "  CMAKE_ARCHIVE_OUTPUT_DIRECTORY  : ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}")
message(STATUS "  CMAKE_LIBRARY_OUTPUT_DIRECTORY  : ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
message(STATUS "  CMAKE_CXX_STANDARD              : ${CMAKE_CXX_STANDARD}")
message(STATUS "  CMAKE_CXX_STANDARD_REQUIRED     : ${CMAKE_CXX_STANDARD_REQUIRED}")
message(STATUS "  CMAKE_CXX_EXTENSIONS            : ${CMAKE_CXX_EXTENSIONS}")
message(STATUS "  MIRAKC_ARIB_TEST                : ${MIRAKC_ARIB_TEST}")
message(STATUS "  NPROC                           : ${NPROC}")
message(STATUS "  MIRAKC_ARIB_VERSION             : ${MIRAKC_ARIB_VERSION}")
message(STATUS "  MIRAKC_ARIB_DOCOPT_VERSION      : ${MIRAKC_ARIB_DOCOPT_VERSION}")
message(STATUS "  MIRAKC_ARIB_FMT_VERSION         : ${MIRAKC_ARIB_FMT_VERSION}")
message(STATUS "  MIRAKC_ARIB_SPDLOG_VERSION      : ${MIRAKC_ARIB_SPDLOG_VERSION}")
message(STATUS "  MIRAKC_ARIB_RAPIDJSON_VERSION   : ${MIRAKC_ARIB_RAPIDJSON_VERSION}")
message(STATUS "  MIRAKC_ARIB_CPPCODEC_VERSION    : ${MIRAKC_ARIB_CPPCODEC_VERSION}")
message(STATUS "  MIRAKC_ARIB_ARIBB24_VERSION     : ${MIRAKC_ARIB_ARIBB24_VERSION}")
message(STATUS "  MIRAKC_ARIB_TSDUCK_ARIB_VERSION : ${MIRAKC_ARIB_TSDUCK_ARIB_VERSION}")

# a thread library is required

find_package(Threads REQUIRED)

# third party libraries
#
# TODO: run test before installation

include(ExternalProject)

set(MIRAKC_ARIB_VENDOR_DIR ${CMAKE_BINARY_DIR}/vendor)
set(MIRAKC_ARIB_VENDOR_READY TRUE)

add_custom_target(vendor
  # Update the build system
  ${CMAKE_COMMAND} -E echo "Updating the build system..."
  COMMAND
    ${CMAKE_COMMAND} -S ${CMAKE_CURRENT_SOURCE_DIR} -B ${CMAKE_BINARY_DIR}
)

# docopt

ExternalProject_Add(vendor-docopt
  PREFIX
    ${MIRAKC_ARIB_VENDOR_DIR}
  GIT_REPOSITORY
    https://github.com/masnagam/docopt.cpp.git
  GIT_TAG
    ${MIRAKC_ARIB_DOCOPT_VERSION}
  CMAKE_ARGS
    -D CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    -D CMAKE_INSTALL_PREFIX=${MIRAKC_ARIB_VENDOR_DIR}
    -D CMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
  EXCLUDE_FROM_ALL
    YES
)

add_dependencies(vendor vendor-docopt)

find_package(docopt CONFIG PATHS ${MIRAKC_ARIB_VENDOR_DIR} NO_DEFAULT_PATH)

if(docopt_FOUND)
  # The following imported targets have been created at this point:
  #
  # * docopt    (shared library)
  # * docopt_s  (static library)
  #
  # These can be used with `target_link_libraries()` like below:
  #
  #   target_link_libraries(target PRIVATE docopt)
  #
  # See ${MIRAKC_ARIB_VENDOR_DIR}/lib/cmake/docopt/docopt-targets.cmake for
  # details.
else()
  message(WARNING "docopt: not found")
  set(MIRAKC_ARIB_VENDOR_READY FALSE)
endif()

# fmt

ExternalProject_Add(vendor-fmt
  PREFIX
    ${MIRAKC_ARIB_VENDOR_DIR}
  GIT_REPOSITORY
    https://github.com/fmtlib/fmt.git
  GIT_TAG
    ${MIRAKC_ARIB_FMT_VERSION}
  CMAKE_ARGS
    -D CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    -D CMAKE_INSTALL_PREFIX=${MIRAKC_ARIB_VENDOR_DIR}
    -D CMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
    -D FMT_DOC=OFF
    -D FMT_TEST=${MIRAKC_ARIB_VENDOR_TEST}
  EXCLUDE_FROM_ALL
    YES
)

add_dependencies(vendor vendor-fmt)

find_package(fmt CONFIG PATHS ${MIRAKC_ARIB_VENDOR_DIR} NO_DEFAULT_PATH)

if(fmt_FOUND)
  # The following imported targets have been created at this point:
  #
  # * fmt::fmt              (static library)
  # * fmt::fmt-header-only  (interface library)
  #
  # These can be used with `target_link_libraries()` like below:
  #
  #   target_link_libraries(target PRIVATE fmt::fmt)
  #
  # See ${MIRAKC_ARIB_VENDOR_DIR}/lib/cmake/fmt/fmt-targets.cmake for details.
else()
  message(WARNING "fmt: not found")
  set(MIRAKC_ARIB_VENDOR_READY FALSE)
endif()

# spdlog

ExternalProject_Add(vendor-spdlog
  DEPENDS
    vendor-fmt
  PREFIX
    ${MIRAKC_ARIB_VENDOR_DIR}
  GIT_REPOSITORY
    https://github.com/gabime/spdlog.git
  GIT_TAG
    ${MIRAKC_ARIB_SPDLOG_VERSION}
  PATCH_COMMAND
    git checkout -f && patch -p0 < ${CMAKE_SOURCE_DIR}/patches/spdlog.patch
  CMAKE_ARGS
    -D CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    -D CMAKE_INSTALL_PREFIX=${MIRAKC_ARIB_VENDOR_DIR}
    -D CMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
    -D SPDLOG_BUILD_EXAMPLE=OFF
    -D SPDLOG_BUILD_BENCH=OFF
    -D SPDLOG_BUILD_TESTS=${MIRAKC_ARIB_VENDOR_TEST}
    -D SPDLOG_FMT_EXTERNAL=ON
  EXCLUDE_FROM_ALL
    YES
)

add_dependencies(vendor vendor-spdlog)

find_package(spdlog CONFIG PATHS ${MIRAKC_ARIB_VENDOR_DIR} NO_DEFAULT_PATH)

if(spdlog_FOUND)
  # The following imported targets have been created at this point:
  #
  # * spdlog::spdlog  (header only)
  #
  # These can be used with `target_link_libraries()` like below:
  #
  #   target_link_libraries(target PRIVATE spdlog::spdlog)
  #
  # See ${MIRAKC_ARIB_VENDOR_DIR}/lib/cmake/spdlog/spdlogTargets.cmake for
  # details.
else()
  message(WARNING "spdlog: not found")
  set(MIRAKC_ARIB_VENDOR_READY FALSE)
endif()

# RapidJSON

ExternalProject_Add(vendor-rapidjson
  PREFIX
    ${MIRAKC_ARIB_VENDOR_DIR}
  GIT_REPOSITORY
    https://github.com/Tencent/rapidjson.git
  GIT_TAG
    ${MIRAKC_ARIB_RAPIDJSON_VERSION}
  CMAKE_ARGS
    -D CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    -D CMAKE_INSTALL_PREFIX=${MIRAKC_ARIB_VENDOR_DIR}
    -D CMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
    -D RAPIDJSON_BUILD_DOC=OFF
    -D RAPIDJSON_BUILD_EXAMPLES=OFF
    -D RAPIDJSON_BUILD_TESTS=${MIRAKC_ARIB_VENDOR_TEST}
    -D RAPIDJSON_HAS_STDSTRING=ON
  EXCLUDE_FROM_ALL
    YES
)

add_dependencies(vendor vendor-rapidjson)

find_package(RapidJSON CONFIG PATHS ${MIRAKC_ARIB_VENDOR_DIR} NO_DEFAULT_PATH)

if(RapidJSON_FOUND)
  # Add an interface target for aribb24 like other third party libraries.
  add_library(rapidjson::header-only INTERFACE IMPORTED)

  # "RAPIDJSON_HAS_STDSTRING=1" is defined for convenience.
  set_target_properties(rapidjson::header-only PROPERTIES
    INTERFACE_COMPILE_DEFINITIONS
      "RAPIDJSON_HAS_STDSTRING=1"
    INTERFACE_INCLUDE_DIRECTORIES
      "${RAPIDJSON_INCLUDE_DIRS}"
  )
else()
  message(WARNING "rapidjson: not found")
  set(MIRAKC_ARIB_VENDOR_READY FALSE)
endif()

# cppcodec

ExternalProject_Add(vendor-cppcodec
  PREFIX
    ${MIRAKC_ARIB_VENDOR_DIR}
  GIT_REPOSITORY
    https://github.com/tplgy/cppcodec.git
  GIT_TAG
    ${MIRAKC_ARIB_CPPCODEC_VERSION}
  CMAKE_ARGS
    -D CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    -D CMAKE_INSTALL_PREFIX=${MIRAKC_ARIB_VENDOR_DIR}
    -D CMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
  EXCLUDE_FROM_ALL
    YES
)

add_dependencies(vendor vendor-cppcodec)

find_file(cppcodec cppcodec/base64_rfc4648.hpp
  PATHS ${MIRAKC_ARIB_VENDOR_DIR}/include NO_DEFAULT_PATH)

if(cppcodec)
  # Add an interface target for cppcodec like other third party libraries.
  add_library(cppcodec::header-only INTERFACE IMPORTED)

  set_target_properties(cppcodec::header-only PROPERTIES
    INTERFACE_INCUDE_DIRECTORIES
      ${MIRAKC_ARIB_VENDOR_DIR}/include
  )
else()
  message(WARNING "cppcodec: not found")
  set(MIRAKC_ARIB_VENDOR_READY FALSE)
endif()

# aribb24

ExternalProject_Add(vendor-aribb24
  PREFIX
    ${MIRAKC_ARIB_VENDOR_DIR}
  GIT_REPOSITORY
    https://github.com/masnagam/aribb24.git
  GIT_TAG
    ${MIRAKC_ARIB_ARIBB24_VERSION}
  BUILD_IN_SOURCE
    YES
  CONFIGURE_COMMAND
    ./bootstrap
  COMMAND
    ./configure
      --prefix=${MIRAKC_ARIB_VENDOR_DIR}
      --disable-shared --with-pic --without-libpng
      $<$<BOOL:CMAKE_CROSSCOMPILING>:--host=${CMAKE_C_COMPILER_TARGET}>
  BUILD_COMMAND
    make -j ${NPROC}
  EXCLUDE_FROM_ALL
    YES
)

add_dependencies(vendor vendor-aribb24)

find_library(aribb24_LIB aribb24 PATHS ${MIRAKC_ARIB_VENDOR_DIR}/lib
  NO_DEFAULT_PATH)

if(aribb24_LIB)
  # Add an interface target for aribb24 like other third party libraries.
  add_library(aribb24::static-lib STATIC IMPORTED)

  set_target_properties(aribb24::static-lib PROPERTIES
    IMPORTED_LINK_INTERFACE_LANGUAGES
      CXX
    IMPORTED_LOCATION
      ${aribb24_LIB}
    INTERFACE_INCUDE_DIRECTORIES
      ${MIRAKC_ARIB_VENDOR_DIR}/include
  )
else()
  message(WARNING "aribb24: not found")
  set(MIRAKC_ARIB_VENDOR_READY FALSE)
endif()

# tsduck-arib

set(MIRAKC_ARIB_TSDUCK_DEFS ARIB=1 NOCURL=1 NODTAPI=1 NOPCSC=1 NOSRT=1)

if(NOT MIRAKC_ARIB_VENDOR_TEST)
  list(APPEND MIRAKC_ARIB_TSDUCK_DEFS NOTEST=1)
endif()

if(CMAKE_CROSSCOMPILING)
  # TSDuck supports the cross compilation.  But it's not suitable for
  # Debian/Ubuntu.  Define variables for the cross compilation directly instead
  # of the CROSS, CROSS_PREFIX and CROSS_TARGET variables.
  list(APPEND MIRAKC_ARIB_TSDUCK_DEFS
    MAIN_ARCH=${CMAKE_SYSTEM_PROCESSOR}
    TARGET_FLAGS=
    CC=${CMAKE_C_COMPILER}
    GCC=${CMAKE_C_COMPILER}
    CXX=${CMAKE_CXX_COMPILER}
    LD=${CMAKE_C_COMPILER_TARGET}-ld
  )
endif()

if(${CMAKE_BUILD_TYPE} STREQUAL Debug)
  list(APPEND MIRAKC_ARIB_TSDUCK_DEFS DEBUG=1)
endif()

ExternalProject_Add(vendor-tsduck-arib
  DEPENDS
    vendor-aribb24
  PREFIX
    ${MIRAKC_ARIB_VENDOR_DIR}
  GIT_REPOSITORY
    https://github.com/masnagam/tsduck-arib.git
  GIT_TAG
    ${MIRAKC_ARIB_TSDUCK_ARIB_VERSION}
  BUILD_IN_SOURCE
    YES
  CONFIGURE_COMMAND
    ""
  BUILD_COMMAND
    PKG_CONFIG_PATH=${MIRAKC_ARIB_VENDOR_DIR}/lib/pkgconfig
      make -j ${NPROC} ${MIRAKC_ARIB_TSDUCK_DEFS}
  INSTALL_COMMAND
    PKG_CONFIG_PATH=${MIRAKC_ARIB_VENDOR_DIR}/lib/pkgconfig
      make install-devel SYSPREFIX=${MIRAKC_ARIB_VENDOR_DIR}
                         ${MIRAKC_ARIB_TSDUCK_DEFS}
  EXCLUDE_FROM_ALL
    YES
)

add_dependencies(vendor vendor-tsduck-arib)

find_library(tsduck-arib_LIB tsduck
  PATHS ${MIRAKC_ARIB_VENDOR_DIR}/lib ${MIRAKC_ARIB_VENDOR_DIR}/lib64
  NO_DEFAULT_PATH)

if(tsduck-arib_LIB)
  # Add an interface target for tsduck-arib like other third party libraries.
  add_library(tsduck-arib::static-lib STATIC IMPORTED)

  set_target_properties(tsduck-arib::static-lib PROPERTIES
    IMPORTED_LINK_INTERFACE_LANGUAGES
      CXX
    IMPORTED_LOCATION
      ${tsduck-arib_LIB}
    INTERFACE_COMPILE_DEFINITIONS
      "TS_ARIB=1; TS_NO_CURL=1; TS_NO_DTAPI=1; TS_NO_PCSC=1"
    INTERFACE_INCLUDE_DIRECTORIES
      ${MIRAKC_ARIB_VENDOR_DIR}/include
    INTERFACE_LINK_LIBRARIES
      "${CMAKE_THREAD_LIBS_INIT}"
  )
else()
  message(WARNING "tsduck-arib: not found")
  set(MIRAKC_ARIB_VENDOR_READY FALSE)
endif()

if(MIRAKC_ARIB_TEST)
  # Google Test

  ExternalProject_Add(vendor-googletest
    PREFIX
      ${MIRAKC_ARIB_VENDOR_DIR}
    GIT_REPOSITORY
      https://github.com/google/googletest.git
    GIT_TAG
      ${MIRAKC_ARIB_GOOGLETEST_VERSION}
    CMAKE_ARGS
      -D CMAKE_BUILD_TYPE=Release
      -D CMAKE_INSTALL_PREFIX=${MIRAKC_ARIB_VENDOR_DIR}
    EXCLUDE_FROM_ALL
      YES
  )

  add_dependencies(vendor vendor-googletest)

  find_package(GTest CONFIG PATHS ${MIRAKC_ARIB_VENDOR_DIR} NO_DEFAULT_PATH)

  if(GTest_FOUND)
    # The following imported targets have been created at this point:
    #
    # * GTest::gtest       (static library)
    # * GTest::gtest_main  (static library)
    # * GTest::gmock       (static library)
    # * GTest::gmock_main  (static library)
    #
    # These can be used with `target_link_libraries()` like below:
    #
    #   target_link_libraries(target PRIVATE GTest::gtest_main GTest::gtest)
    #
    # See ${MIRAKC_ARIB_VENDOR_DIR}/lib/cmake/GTest/GTestTargets.cmake for
    # details.
  else()
    message(WARNING "googletest: not found")
    set(MIRAKC_ARIB_VENDOR_READY FALSE)
  endif()

  # Google Benchmark

  ExternalProject_Add(vendor-google-benchmark
    PREFIX
      ${MIRAKC_ARIB_VENDOR_DIR}
    GIT_REPOSITORY
      https://github.com/google/benchmark.git
    GIT_TAG
      ${MIRAKC_ARIB_GOOGLE_BENCHMARK_VERSION}
    CMAKE_ARGS
      -D CMAKE_BUILD_TYPE=Release
      -D CMAKE_INSTALL_PREFIX=${MIRAKC_ARIB_VENDOR_DIR}
      -D BENCHMARK_ENABLE_TESTING=OFF
    EXCLUDE_FROM_ALL
      YES
  )

  add_dependencies(vendor vendor-google-benchmark)

  find_package(benchmark CONFIG PATHS ${MIRAKC_ARIB_VENDOR_DIR} NO_DEFAULT_PATH)

  if(benchmark_FOUND)
    # The following imported targets have been created at this point:
    #
    # * benchmark::benchmark       (static library)
    # * benchmark::benchmark_main  (static library)
    #
    # These can be used with `target_link_libraries()` like below:
    #
    #   target_link_libraries(target
    #     PRIVATE
    #       benchmark::benchmark_main
    #       benchmark::benchmark
    #   )
    #
    # See ${MIRAKC_ARIB_VENDOR_DIR}/lib/cmake/benchmark/benchmarkTargets.cmake
    # for details.
  else()
    message(WARNING "google-benchmark: not found")
    set(MIRAKC_ARIB_VENDOR_READY FALSE)
  endif()
endif()

# Check whether all vendor libraries have been installed or not.

if(NOT MIRAKC_ARIB_VENDOR_READY)
  message(WARNING "Build the `vendor` target first")
  return()
endif()

# mirakc-arib

add_executable(mirakc-arib
  src/airtime_tracker.hh
  src/base.hh
  src/eit_collector.hh
  src/file.hh
  src/jsonl_sink.hh
  src/jsonl_source.hh
  src/logging.hh
  src/logo_collector.hh
  src/main.cc
  src/packet_sink.hh
  src/packet_source.hh
  src/pcr_synchronizer.hh
  src/program_filter.hh
  src/service_filter.hh
  src/service_scanner.hh
)

target_link_libraries(mirakc-arib
  PRIVATE
    docopt_s
    fmt::fmt
    spdlog::spdlog
    rapidjson::header-only
    cppcodec::header-only
    tsduck-arib::static-lib
    aribb24::static-lib
)

target_compile_definitions(mirakc-arib
  PRIVATE
    SPDLOG_DISABLE_DEFAULT_LOGGER
    MIRAKC_ARIB_VERSION="${MIRAKC_ARIB_VERSION}"
    MIRAKC_ARIB_DOCOPT_VERSION="${MIRAKC_ARIB_DOCOPT_VERSION}"
    MIRAKC_ARIB_FMT_VERSION="${MIRAKC_ARIB_FMT_VERSION}"
    MIRAKC_ARIB_SPDLOG_VERSION="${MIRAKC_ARIB_SPDLOG_VERSION}"
    MIRAKC_ARIB_RAPIDJSON_VERSION="${MIRAKC_ARIB_RAPIDJSON_VERSION}"
    MIRAKC_ARIB_CPPCODEC_VERSION="${MIRAKC_ARIB_CPPCODEC_VERSION}"
    MIRAKC_ARIB_ARIBB24_VERSION="${MIRAKC_ARIB_ARIBB24_VERSION}"
    MIRAKC_ARIB_TSDUCK_ARIB_VERSION="${MIRAKC_ARIB_TSDUCK_ARIB_VERSION}"
)

if(MIRAKC_ARIB_TEST)
  # test

  add_executable(mirakc-arib-test
    test/airtime_tracker_test.cc
    test/eit_collector_test.cc
    test/logo_collector_test.cc
    test/packet_source_test.cc
    test/pcr_synchronizer_test.cc
    test/program_filter_test.cc
    test/service_filter_test.cc
    test/service_scanner_test.cc
    test/test.cc
    test/test_helper.hh
  )

  target_include_directories(mirakc-arib-test
    PRIVATE
      src
  )

  target_compile_definitions(mirakc-arib-test
    PRIVATE
      SPDLOG_DISABLE_DEFAULT_LOGGER
  )

  target_link_libraries(mirakc-arib-test
    PRIVATE
      GTest::gmock
      GTest::gtest
      fmt::fmt
      spdlog::spdlog
      rapidjson::header-only
      cppcodec::header-only
      tsduck-arib::static-lib
      aribb24::static-lib
  )

  add_custom_target(test
    $<TARGET_FILE:mirakc-arib-test> --gtest_shuffle)

  # benchmark

  add_executable(mirakc-arib-benchmark
    benchmark/benchmark.cc
    benchmark/packet_source_benchmark.cc
  )

  target_include_directories(mirakc-arib-benchmark
    PRIVATE
      src
  )

  target_link_libraries(mirakc-arib-benchmark
    PRIVATE
      benchmark::benchmark
      fmt::fmt
      spdlog::spdlog
      rapidjson::header-only
      cppcodec::header-only
      tsduck-arib::static-lib
      aribb24::static-lib
  )

  add_custom_target(benchmark $<TARGET_FILE:mirakc-arib-benchmark>)
endif()
