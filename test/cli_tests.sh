#!/bin/sh

# SPDX-License-Identifier: GPL-2.0-or-later

# mirakc-arib
# Copyright (C) 2019 masnagam
#
# This program is free software; you can redistribute it and/or modify it under the terms of the
# GNU General Public License as published by the Free Software Foundation; either version 2 of the
# License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
# without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See
# the GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along with this program; if
# not, write to the Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
# 02110-1301, USA.

TMPFILE=$(mktemp)
trap "rm $TMPFILE" EXIT

MIRAKC_ARIB="$1"

export MIRAKC_ARIB_LOG=none

assert() {
  expected=$1
  cmd="$2"
  error=$(sh -c "$cmd </dev/null >/dev/null" 2>&1)
  actual=$?
  if [ $expected -eq $actual ]; then
    echo "PASS: $cmd"
  else
    echo "FAIL: $cmd: expectd($expected) actual($actual)"
    echo "$error"
    exit 1
  fi
}

for opt in '-h' '--help'
do
  assert 0 "$MIRAKC_ARIB $opt"
  for cmd in 'scan-services' 'sync-clocks' 'collect-eits' 'collect-logos' \
             'filter-service' 'filter-program' 'record-service' 'track-airtime' \
             'seek-start' 'print-pes'
  do
    assert 0 "$MIRAKC_ARIB $cmd $opt"
  done
done

assert 0 "$MIRAKC_ARIB --version"

assert 1 "$MIRAKC_ARIB scan-services"
assert 1 "$MIRAKC_ARIB scan-services --sids=1 --sids=0xFFFF --xsids=1 --xsids=0xFFFF"

assert 1 "$MIRAKC_ARIB sync-clocks"
assert 1 "$MIRAKC_ARIB sync-clocks --sids=1 --sids=0xFFFF --xsids=1 --xsids=0xFFFF"

assert 0 "$MIRAKC_ARIB collect-eits"
assert 0 "$MIRAKC_ARIB collect-eits --sids=1 --sids=0xFFFF --xsids=1 --xsids=0xFFFF --time-limit=0x7FFFFFFFFFFFFFFF --streaming"
assert 134 "$MIRAKC_ARIB collect-eits --time-limit=0xFFFFFFFFFFFFFFFF"
assert 255 "$MIRAKC_ARIB collect-eits --only-actual --only-others"

assert 0 "$MIRAKC_ARIB collect-eitpf"
assert 0 "$MIRAKC_ARIB collect-eitpf --sids=1 --sids=0xFFFF --streaming"
assert 0 "$MIRAKC_ARIB collect-eitpf --sids=1 --sids=0xFFFF --streaming --present"
assert 0 "$MIRAKC_ARIB collect-eitpf --sids=1 --sids=0xFFFF --streaming --following"
assert 255 "$MIRAKC_ARIB collect-eitpf --present --following"

assert 0 "$MIRAKC_ARIB filter-service --sid=1"
assert 0 "$MIRAKC_ARIB filter-service --sid=0xFFFF"

assert 0 "$MIRAKC_ARIB filter-program --sid=1 --eid=1 --clock-pid=1 --clock-pcr=1 --clock-time=1"
assert 0 "$MIRAKC_ARIB filter-program --sid=0xFFFF --eid=0xFFFF --clock-pid=0xFFFF --clock-pcr=0x7FFFFFFFFFFFFFFF --clock-time=-9223372036854775808 --start-margin=1 --end-margin=1 --wait-until=-9223372036854775808 --pre-streaming"
assert 134 "$MIRAKC_ARIB filter-program --sid=1 --eid=1 --clock-pid=1 --clock-pcr=0xFFFFFFFFFFFFFFFFF --clock-time=1"
assert 134 "$MIRAKC_ARIB filter-program --sid=1 --eid=1 --clock-pid=1 --clock-pcr=1 --clock-time=-9223372036854775809"
assert 0 "$MIRAKC_ARIB filter-program --sid=1 --eid=1 --clock-pid=1 --clock-pcr=1 --clock-time=1 --audio-tags=0 --audio-tags=255 --video-tags=0 --video-tags=255"
assert 134 "$MIRAKC_ARIB filter-program --sid=1 --eid=1 --clock-pid=1 --clock-pcr=1 --clock-time=1 --audio-tags='-1'"
assert 134 "$MIRAKC_ARIB filter-program --sid=1 --eid=1 --clock-pid=1 --clock-pcr=1 --clock-time=1 --audio-tags=256"
assert 134 "$MIRAKC_ARIB filter-program --sid=1 --eid=1 --clock-pid=1 --clock-pcr=1 --clock-time=1 --video-tags='-1'"
assert 134 "$MIRAKC_ARIB filter-program --sid=1 --eid=1 --clock-pid=1 --clock-pcr=1 --clock-time=1 --video-tags=256"
assert 134 "$MIRAKC_ARIB filter-program --sid=1 --eid=1 --clock-pid=1 --clock-pcr=1 --clock-time=1 --wait-until=-9223372036854775809"

assert 0 "$MIRAKC_ARIB filter-program-metadata"
assert 0 "$MIRAKC_ARIB filter-program-metadata --sid=1"
assert 0 "$MIRAKC_ARIB filter-program-metadata --sid=0xFFFF"

assert 0 "$MIRAKC_ARIB record-service --sid=1 --file=$TMPFILE --chunk-size=8192 --num-chunks=1"
assert 0 "$MIRAKC_ARIB record-service --sid=1 --file=$TMPFILE --chunk-size=8192 --num-chunks=1 --start-pos=0"
assert 0 "$MIRAKC_ARIB record-service --sid=1 --file=$TMPFILE --chunk-size=8192 --num-chunks=2 --start-pos=8192"
if [ -z "$CI" ]
then
  # This test fails in GitHub Actions.
  assert 0 "$MIRAKC_ARIB record-service --sid=0xFFFF --file=$TMPFILE --chunk-size=0x7FFE0000 --num-chunks=0x7FFFFFFF --start-pos=0x3FFEFFFF00040000"
fi
assert 134 "$MIRAKC_ARIB record-service --sid=1 --file=$TMPFILE --chunk-size=0 --num-chunks=1"
assert 134 "$MIRAKC_ARIB record-service --sid=1 --file=$TMPFILE --chunk-size=1 --num-chunks=1"
assert 134 "$MIRAKC_ARIB record-service --sid=1 --file=$TMPFILE --chunk-size=0xFFFE0000 --num-chunks=1"
assert 134 "$MIRAKC_ARIB record-service --sid=1 --file=$TMPFILE --chunk-size=8192 --num-chunks=0"
assert 134 "$MIRAKC_ARIB record-service --sid=1 --file=$TMPFILE --chunk-size=8192 --num-chunks=0x80000000"
assert 134 "$MIRAKC_ARIB record-service --sid=1 --file=$TMPFILE --chunk-size=8192 --num-chunks=1 --start-pos=1"
assert 134 "$MIRAKC_ARIB record-service --sid=1 --file=$TMPFILE --chunk-size=8192 --num-chunks=1 --start-pos=8192"

assert 0 "$MIRAKC_ARIB track-airtime --sid=1 --eid=1"
assert 0 "$MIRAKC_ARIB track-airtime --sid=0xFFFF --eid=0xFFFF"

assert 0 "$MIRAKC_ARIB seek-start --sid=1 --max-duration=1"
assert 0 "$MIRAKC_ARIB seek-start --sid=0xFFFF --max-duration=0x7FFFFFFFFFFFFFFF --max-packets=0x7FFFFFFF"
assert 134 "$MIRAKC_ARIB seek-start --sid=0xFFFF --max-duration=0xFFFFFFFFFFFFFFFF --max-packets=0x7FFFFFFF"

assert 0 "$MIRAKC_ARIB print-pes"
