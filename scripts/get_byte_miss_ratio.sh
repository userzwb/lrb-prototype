#!/bin/bash

last_byte_req=0
last_byte_miss=0

while [[ 1 ]]
do
  byte_req=$(/opt/ts/bin/traffic_ctl metric match proxy.process.cache_total_bytes|cut -d' ' -f2)
  byte_miss=$(/opt/ts/bin/traffic_ctl metric match proxy.process.cache_total_misses_bytes|cut -d' ' -f2)

  echo "client traffic: " $(( $byte_req - $last_byte_req ))
  segment_bmr=$(echo "scale=3; (${byte_miss}-${last_byte_miss})/(${byte_req}-${last_byte_req})" | bc -l)
  echo "segment byte miss ratio:" $segment_bmr
  bmr=$(echo "scale=3; ${byte_miss}/${byte_req}" | bc -l)
  echo "byte miss ratio:" $bmr
  echo

  last_byte_req=$byte_req
  last_byte_miss=$byte_miss
  sleep 5
done
