# mirakc-arib

> mirakc-tools for Japanese TV broadcast contents

[![linux-status](https://github.com/masnagam/mirakc-arib/workflows/Linux/badge.svg)](https://github.com/masnagam/mirakc-arib/actions?workflow=Linux)
[![macos-status](https://github.com/masnagam/mirakc-arib/workflows/macOS/badge.svg)](https://github.com/masnagam/mirakc-arib/actions?workflow=macOS)
[![arm-linux-status](https://github.com/masnagam/mirakc-arib/workflows/ARM-Linux/badge.svg)](https://github.com/masnagam/mirakc-arib/actions?workflow=ARM-Linux)

## How to build

Make sure that tools listed below has already been installed:

* autoconf
* automake
* cmake
* dos2unix
* gcc/g++
* libtool
* make
* ninja (optional)
* patch
* pkg-config

Then:

```console
$ cmake -S . -B build -G Ninja -D CMAKE_BUILD_TYPE=Release
$ ninja -C build vendor
$ ninja -C build
$ build/bin/mirakc-arib -h
```

### Cross compilation

Use a CMake toolchain file like below:

```console
$ cmake -S . -B build -G Ninja -D CMAKE_BUILD_TYPE=Release \
    -D CMAKE_TOOLCHAIN_FILE=/path/to/toolchain.cmake
$ ninja -C build vendor
$ ninja -C build
```

Content shown below is a CMake toolchain file which can be used for a cross
compilation on a Debian-based Linux distribution, targeting AArch64 like ROCK64:

```cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(MIRAKC_ARIB_HOST_TRIPLE aarch64-linux-gnu)

set(CMAKE_C_COMPILER ${MIRAKC_ARIB_HOST_TRIPLE}-gcc)
set(CMAKE_C_COMPILER_TARGET ${MIRAKC_ARIB_HOST_TRIPLE})

set(CMAKE_CXX_COMPILER ${MIRAKC_ARIB_HOST_TRIPLE}-g++)
set(CMAKE_CXX_COMPILER_TARGET ${MIRAKC_ARIB_HOST_TRIPLE})
```

Make sure that a toolchain for the cross compilation has been installed before
running `cmake` with a CMake toolchain file.

Several CMake toolchain files are included in the
[toolchain.cmake.d](./toolchain.cmake.d) folder.

## How to test

```console
$ cmake -S . -B build -G Ninja -D CMAKE_BUILD_TYPE=Debug -D MIRAKC_ARIB_TEST=ON
$ ninja -C build vendor
$ ninja -C build test
```

## Logging

Define the `MIRAKC_ARIB_LOG` environment variable like below:

```console
$ cat file.ts | MIRAKC_ARIB_LOG=info mirakc-arib scan-services
```

One of the following log levels can be specified:

* trace
* debug
* info
* warning
* error
* critical
* off

## Why not use `tsp`?

`tsp` creates a thread for each plug-in.  This approach can work effectively
when running a single `tsp` with multiple plug-ins.

Usages of mirakc-arib are different from `tsp`:

* Multiple mirakc-arib sub-commands may be executed in parallel
  * Costs of context switching may be considerable
* It seems not to be a good idea to construct a mirakc-arib sub-command of
  multiple plug-ins
  * Communication costs between plug-ins may be considerable

## Dependencies

mirakc-arib is static-linked against the following libraries:

* [masnagam/docopt.cpp] forked from [docopt/docopt.cpp] (Boost/MIT)
* [fmtlib/fmt] (MIT)
* [gabime/spdlog] w/ [patches/spdlog.patch](./patches/spdlog.patch) (MIT)
* [Tencent/rapidjson] (MIT)
* [tplgy/cppcodec] (MIT)
* [masnagam/tsduck-arib] forked from [tsduck/tsduck] (BSD 2-Clause)
* [masnagam/aribb24] forked from [nkoriyama/aribb24] (LGPL-3.0)

The following libraries are used for testing purposes:

* [google/googletest] (BSD 3-Clause)
* [google/benchmark] (Apache-2.0)

## TODO

* Add more unit tests
* Collect code coverage using gcov while testing

## ARIB STD Specifications

ARIB STD specifications can be freely downloaded from
[this page](https://www.arib.or.jp/english/std_tr/broadcasting/sb_ej.html).

* ARIB STD-B10: Additional definitions of tables and descriptors
* ARIB STD-B24: Character encoding

## Acknowledgments

mirakc-arib is implemented based on knowledge gained from the following software
implementations:

* [node-aribts]
* [epgdump]
* [ariblib]

## License

Licensed under either of

* Apache License, Version 2.0
  ([LICENSE-APACHE] or http://www.apache.org/licenses/LICENSE-2.0)
* MIT License
  ([LICENSE-MIT] or http://opensource.org/licenses/MIT)

at your option.

Unless you explicitly state otherwise, any contribution intentionally submitted
for inclusion in this project by you, as defined in the Apache-2.0 license,
shall be dual licensed as above, without any additional terms or conditions.

[masnagam/docopt.cpp]: https://github.com/masnagam/docopt.cpp
[docopt/docopt.cpp]: https://github.com/docopt/docopt.cpp
[fmtlib/fmt]: https://github.com/fmtlib/fmt
[gabime/spdlog]: https://github.com/gabime/spdlog
[Tencent/rapidjson]: https://github.com/Tencent/rapidjson
[tplgy/cppcodec]: https://github.com/tplgy/cppcodec
[masnagam/tsduck-arib]: https://github.com/masnagam/tsduck-arib
[tsduck/tsduck]: https://github.com/tsduck/tsduck
[masnagam/aribb24]: https://github.com/masnagam/aribb24
[nkoriyama/aribb24]: https://github.com/nkoriyama/aribb24
[google/googletest]: https://github.com/google/googletest
[google/benchmark]: https://github.com/google/benchmark
[node-aribts]: https://github.com/rndomhack/node-aribts
[epgdump]: https://github.com/Piro77/epgdump
[ariblib]: https://github.com/youzaka/ariblib
[LICENSE-APACHE]: ./LICENSE-APACHE
[LICENSE-MIT]: ./LICENSE-MIT
