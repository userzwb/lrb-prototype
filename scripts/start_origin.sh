#!/usr/bin/env bash


if [[ "$#" = 6 ]]; then
  trace=$1
  n_origin_threads=$2
  latency=$3
  phase=$4
  suffix=$5
  home=$6
elif [[ "$#" = 0 ]]; then
  trace=wiki_1400m_4mb
  n_origin_threads=1024
  latency=0
  phase=warmup
  suffix=wiki_1400m_4mb_fifo_0_pni_0
  home=/usr/people/zhenyus/webtracereplay
else
    echo "Illegal number of parameters"
    exit 1
fi

echo "starting origin..."
sudo nginx -s stop
sudo nginx -c ~/webtracereplay/server/nginx.conf
pkill -9 -f origin/origin
pkill -9 -f /tmp/influx.log
rm -f /tmp/influx.log
touch /tmp/influx.log ; tail -f /tmp/influx.log | while read v; do curl -s -m 1 -XPOST 'http://mmx.cs.princeton.edu:8086/write?db=mydb' -u admin:system --data-binary "$v";done &
rm -f ${home}/log/origin_"${phase}"_"${suffix}".err ${home}/log/origin_"${phase}"_"${suffix}".log
spawn-fcgi -a 127.0.0.1 -p 9000 -n ${home}/origin/origin ${home}/${trace}_origin.tr ${n_origin_threads} ${latency} 2>${home}/log/origin_"${phase}"_"${suffix}".err </dev/null | tee ${home}/log/origin_"${phase}"_"${suffix}".log | stdbuf -o0 awk '{print "'${suffix}' origin_throughput="$2",origin_n_req="$1}' &>> /tmp/influx.log

