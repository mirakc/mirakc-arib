# mirakc-arib

> mirakc-tools for Japanese TV broadcast contents

[![CI](https://github.com/mirakc/mirakc-arib/actions/workflows/ci.yml/badge.svg)](https://github.com/mirakc/mirakc-arib/actions/workflows/ci.yml)
[![coverage](https://codecov.io/gh/mirakc/mirakc-arib/branch/main/graph/badge.svg?token=iYQqKQLd7A)](https://codecov.io/gh/mirakc/mirakc-arib)

## How to build

Get the repository including the Git submodule:

```shell
git clone --recursive https://github.com/mirakc/mirakc-arib.git
```

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

```shell
cmake -S . -B build -G Ninja -D CMAKE_BUILD_TYPE=Release
ninja -C build vendor
ninja -C build
build/bin/mirakc-arib -h
```

### Cross compilation

Use a CMake toolchain file like below:

```shell
cmake -S . -B build -G Ninja -D CMAKE_BUILD_TYPE=Release \
  -D CMAKE_TOOLCHAIN_FILE=/path/to/toolchain.cmake
ninja -C build vendor
ninja -C build
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

```shell
cmake -S . -B build -G Ninja -D CMAKE_BUILD_TYPE=Debug -D MIRAKC_ARIB_TEST=ON
ninja -C build vendor
ninja -C build check
ninja -C build test
ninja -C build cli-tests
```

The `check` target requires `clang-format`.

## Logging

Define the `MIRAKC_ARIB_LOG` environment variable like below:

```shell
cat file.ts | MIRAKC_ARIB_LOG=info mirakc-arib scan-services
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

* [mirakc/docopt.cpp] forked from [docopt/docopt.cpp] (BSL-1.0 OR MIT)
* [fmtlib/fmt] (MIT)
* [gabime/spdlog] w/ [patches/spdlog.patch](./patches/spdlog.patch) (MIT)
* [Tencent/rapidjson] (MIT)
* [tplgy/cppcodec] (MIT)
* [mirakc/tsduck-arib] forked from [tsduck/tsduck] (BSD-2-Clause)
* [mirakc/aribb24] forked from [nkoriyama/aribb24] (LGPL-3.0)
* [DBCTRADO/LibISDB] w/ [patches/LibISDB.patch](./patches/LibISDB.patch) (GPL-2.0-or-later)

The following libraries are used for testing purposes:

* [google/googletest] (BSD-3-Clause)
* [google/benchmark] (Apache-2.0)

## TODO

* Add more unit tests

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

Licensed under:

* the GNU General Public License
  ([LICENSE] or https://www.gnu.org/licenses/old-licenses/gpl-2.0.txt)

as published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

[mirakc/docopt.cpp]: https://github.com/mirakc/docopt.cpp
[docopt/docopt.cpp]: https://github.com/docopt/docopt.cpp
[fmtlib/fmt]: https://github.com/fmtlib/fmt
[gabime/spdlog]: https://github.com/gabime/spdlog
[Tencent/rapidjson]: https://github.com/Tencent/rapidjson
[tplgy/cppcodec]: https://github.com/tplgy/cppcodec
[mirakc/tsduck-arib]: https://github.com/mirakc/tsduck-arib
[tsduck/tsduck]: https://github.com/tsduck/tsduck
[mirakc/aribb24]: https://github.com/mirakc/aribb24
[nkoriyama/aribb24]: https://github.com/nkoriyama/aribb24
[DBCTRADO/LibISDB]: https://github.com/DBCTRADO/LibISDB
[google/googletest]: https://github.com/google/googletest
[google/benchmark]: https://github.com/google/benchmark
[node-aribts]: https://github.com/rndomhack/node-aribts
[epgdump]: https://github.com/Piro77/epgdump
[ariblib]: https://github.com/youzaka/ariblib
[LICENSE]: ./LICENSE
