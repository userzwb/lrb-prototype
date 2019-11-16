#!/bin/bash


if [[ "$#" = 3 ]]; then
  suffix=$1
  test_bed=$2
  phase=$3
elif [[ "$#" = 0 ]]; then
  suffix=wiki_1400m_4mb_fifo_0_pni_0
  test_bed=pni
  phase=warmup
else
    echo "Illegal number of parameters"
    exit 1
fi

rm ~/webtracereplay/log/top_${phase}_${suffix}.log
if [[ ${test_bed} = 'gcp' ]]; then
  #measure cpu and memory
#  top -b -d 5 -p $(/bin/pidof traffic_server)|grep --line-buffered zhenyus 2>/dev/null | tee ~/webtracereplay/log/top_${suffix}.log &
  while true; do ps -o pcpu,rss,vsz $(pidof traffic_server) | tail -n1 | tee -a ~/webtracereplay/log/top_${phase}_${suffix}.log | stdbuf -o0 awk '{print "'${suffix}' pcpu="$1",rss="$2",vsz="$3}' >> /tmp/influx.log; sleep 1;done
elif [[ ${test_bed} = "pni" ]]; then
  #measure cpu and memory
#  top -b -d 5 -p $(/usr/sbin/pidof traffic_server)|grep --line-buffered zhenyus >  log/top_${suffix}.log &
  while true; do ps -o pcpu,rss,vsz $(pidof traffic_server) | tail -n1 | tee -a ~/webtracereplay/log/top_${phase}_${suffix}.log | stdbuf -o0 awk '{print "'${suffix}' pcpu="$1",rss="$2",vsz="$3}' >> /tmp/influx.log; sleep 1;done
else
   echo "wrong test_bed"
   exit 1
fi

#
#rm log/ats_${suffix}.log
#while [[ 1 ]]
#do
#  /opt/ts/bin/traffic_ctl metric get proxy.process.cache_total_misses_bytes proxy.process.cache_total_bytes >> log/ats_${suffix}.log
#  sleep 5
#done
