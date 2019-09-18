#!/bin/bash

suffix=$1
#measure cpu and memory
cd ~/webtracereplay; top -b -d 5 -p $(/bin/pidof traffic_server)|grep --line-buffered zhenyus >  log/eval_top_${suffix}.log &

rm log/eval_ats_${suffix}.log
while [[ 1 ]]
do
  /opt/ts/bin/traffic_ctl metric get proxy.process.cache_total_misses_bytes proxy.process.cache_total_bytes >> log/eval_ats_${suffix}.log
  sleep 5
done
