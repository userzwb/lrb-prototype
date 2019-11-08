#!/bin/bash

suffix=$1
test_bed=$2


if [[ ${test_bed} = 'gcp' ]]; then
  #measure cpu and memory
  cd ~/webtracereplay; top -b -d 5 -p $(/bin/pidof traffic_server)|grep --line-buffered zhenyus >  log/top_${suffix}.log &
elif [[ ${test_bed} = "pni" ]]; then
  #measure cpu and memory
  cd ~/webtracereplay; top -b -d 5 -p $(/usr/sbin/pidof traffic_server)|grep --line-buffered zhenyus >  log/top_${suffix}.log &
else
   echo "wrong test_bed"
   exit 1
fi

rm log/ats_${suffix}.log
while [[ 1 ]]
do
  /opt/ts/bin/traffic_ctl metric get proxy.process.cache_total_misses_bytes proxy.process.cache_total_bytes >> log/ats_${suffix}.log
  sleep 5
done
