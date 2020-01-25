#!/bin/bash


if [[ "$#" = 2 ]]; then
  suffix=$1
  test_bed=$2
elif [[ "$#" = 0 ]]; then
  suffix=wiki_1400m_4mb_fifo_0_pni_0
  test_bed=pni
else
    echo "Illegal number of parameters"
    exit 1
fi

rm ~/webtracereplay/log/ps_${suffix}.log
if [[ ${test_bed} = 'gcp' ]]; then
  #measure cpu and memory
  top -b -d 5 -p $(/bin/pidof traffic_server)|grep --line-buffered zhenyus 2>/dev/null | tee ~/webtracereplay/log/top_${suffix}.log &
elif [[ ${test_bed} = "pni" ]]; then
  #measure cpu and memory
  top -b -d 5 -p $(/usr/sbin/pidof traffic_server)|grep --line-buffered zhenyus >  log/top_${suffix}.log &
else
   echo "wrong test_bed"
   exit 1
fi

