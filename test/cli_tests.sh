#!/bin/sh

MIRAKC_ARIB="$1"

export MIRAKC_ARIB_LOG=none

# show help

echo 'testing help...'
for opt in '-h' '--help'
do
  $MIRAKC_ARIB $opt >/dev/null
  for cmd in 'scan-services' 'sync-clocks' 'collect-eits' 'collect-logos' \
             'filter-service' 'filter-program' 'track-airtime' 'seek-start' \
             'print-timetable'
  do
    $MIRAKC_ARIB $cmd $opt >/dev/null
  done
done
echo 'PASS'

# show version

echo 'testing version...'
$MIRAKC_ARIB --version >/dev/null
echo 'PASS'

# scan-services

echo 'testing scan-services...'
$MIRAKC_ARIB scan-services </dev/null >/dev/null
$MIRAKC_ARIB scan-services \
  --sids=1 --sids=0xFFFF --xsids=1 --xsids=0xFFFF </dev/null >/dev/null
echo 'PASS'

# sync-clocks

echo 'testing sync-clocks...'
$MIRAKC_ARIB sync-clocks </dev/null >/dev/null
$MIRAKC_ARIB sync-clocks \
  --sids=1 --sids=0xFFFF --xsids=1 --xsids=0xFFFF </dev/null >/dev/null
echo 'PASS'

# collect-eits

echo 'testing collect-eits...'
$MIRAKC_ARIB collect-eits </dev/null >/dev/null
$MIRAKC_ARIB collect-eits \
  --sids=1 --sids=0xFFFF --xsids=1 --xsids=0xFFFF \
  --time-limit=0x7FFFFFFFFFFFFFFF --streaming </dev/null >/dev/null
echo 'PASS'

# filter-service

echo 'testing filter-service...'
$MIRAKC_ARIB filter-service --sid=1 </dev/null >/dev/null
$MIRAKC_ARIB filter-service --sid=0xFFFF </dev/null >/dev/null
echo 'PASS'

# filter-program

echo 'testing filter-program...'
$MIRAKC_ARIB filter-program --sid=1 --eid=1 --clock-pcr=1 --clock-time=1 \
  </dev/null >/dev/null
$MIRAKC_ARIB filter-program --sid=0xFFFF --eid=0xFFFF \
  --clock-pcr=0x7FFFFFFFFFFFFFFF --clock-time=-9223372036854775808 \
  --start-margin=1 --end-margin=1 --pre-streaming </dev/null >/dev/null
echo 'PASS'

# track-airtime

echo 'testing track-airtime...'
$MIRAKC_ARIB track-airtime --sid=1 --eid=1 </dev/null >/dev/null
$MIRAKC_ARIB track-airtime --sid=0xFFFF --eid=0xFFFF </dev/null >/dev/null
echo 'PASS'

# seek-start

echo 'testing seek-start...'
$MIRAKC_ARIB seek-start --sid=1 </dev/null >/dev/null
$MIRAKC_ARIB seek-start --sid=0xFFFF --max-duration=0x7FFFFFFFFFFFFFFF \
  --max-packets=0x7FFFFFFF </dev/null >/dev/null
echo 'PASS'

# print-timetable

echo 'testing print-timetable...'
$MIRAKC_ARIB print-timetable </dev/null >/dev/null
echo 'PASS'
