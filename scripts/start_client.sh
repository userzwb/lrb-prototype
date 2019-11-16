#!/usr/bin/env bash


if [[ "$#" = 6 ]]; then
  suffix=$1
  trace=$2
  phase=$3
  n_client=$4
  host=$5
  timeout=$6
elif [[ "$#" = 0 ]]; then
  suffix=wiki_1400m_4mb_fifo_0_pni_0
  trace=wiki_1400m_4mb
  phase=warmup
  n_client=180
  host=localhost
  timeout=10
else
    echo "Illegal number of parameters"
    exit 1
fi


rm -f ~/webtracereplay/log/*
if [[ ${phase} == "warmup" ]]; then
  timeout ${timeout} ~/webtracereplay/client/client ~/webtracereplay/${trace}_${phase}.tr ${n_client} ${host}:6000/ ~/webtracereplay/log/throughput_${phase}_${suffix}.log ~/webtracereplay/log/latency_${phase}_${suffix}.log 0 2>/dev/null | stdbuf -o0 awk '{print "'${suffix}' client_throughput="$2",latency="$3",client_n_req="$1}' >> /tmp/influx.log
else
  echo ' ' > /tmp/influx.log ; tail -f /tmp/influx.log | while read v; do curl -m 1 -XPOST 'http://mmx.cs.princeton.edu:8086/write?db=mydb' -u admin:system --data-binary "$v";done &
  timeout ${timeout} ~/webtracereplay/client/client ~/webtracereplay/${trace}_${phase}.tr ${n_client} ${host}:6000/ ~/webtracereplay/log/throughput_${phase}_${suffix}.log ~/webtracereplay/log/latency_${phase}_${suffix}.log 0 2>/dev/null | stdbuf -o0 awk '{print "'${suffix}' client_throughput="$2",latency="$3",client_n_req="$1}' >> /tmp/influx.log
fi
# for sync
sleep 15
