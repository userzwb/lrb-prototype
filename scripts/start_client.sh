#!/usr/bin/env bash


if [[ "$#" = 7 ]]; then
  suffix=$1
  trace=$2
  phase=$3
  n_client=$4
  host=$5
  timeout=$6
  realtime=$7
elif [[ "$#" = 0 ]]; then
  suffix=wiki_1400m_4mb_fifo_0_pni_0
  trace=wiki_1400m_4mb
  phase=warmup
  n_client=180
  host=localhost
  timeout=10
  realtime=0
else
    echo "Illegal number of parameters"
    exit 1
fi

trap '[[ -z "$(jobs -p)" ]] || kill $(jobs -p)' EXIT

rm -f ~/webtracereplay/log/throughput_${phase}_${suffix}.log ~/webtracereplay/log/latency_${phase}_${suffix}.log
if [[ ${phase} == "warmup" ]]; then
  touch /tmp/influx.log ; tail -f /tmp/influx.log | while read v; do curl -s -m 1 -XPOST 'http://mmx.cs.princeton.edu:8086/write?db=mydb' -u admin:system --data-binary "$v";done &
  timeout ${timeout} ~/webtracereplay/client/client ~/webtracereplay/${trace}_${phase}.tr ${n_client} ${host}:6000/ ~/webtracereplay/log/throughput_${phase}_${suffix}.log ~/webtracereplay/log/latency_${phase}_${suffix}.log ${realtime} 2>/dev/null | stdbuf -o0 awk '{print "'${suffix}' client_throughput="$2",latency="$3",client_n_req="$1}' >> /tmp/influx.log
else
  timeout ${timeout} ~/webtracereplay/client/client ~/webtracereplay/${trace}_${phase}.tr ${n_client} ${host}:6000/ ~/webtracereplay/log/throughput_${phase}_${suffix}.log ~/webtracereplay/log/latency_${phase}_${suffix}.log ${realtime} 2>/dev/null | stdbuf -o0 awk '{print "'${suffix}' client_throughput="$2",latency="$3",client_n_req="$1}' >> /tmp/influx.log
fi
# for sync
sleep 15
