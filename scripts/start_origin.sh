#!/usr/bin/env bash


if [[ "$#" = 7 ]]; then
  origin_ip_internal=$1
  trace=$2
  n_origin_threads=$3
  latency=$4
  phase=$5
  suffix=$6
  home=$7
elif [[ "$#" = 0 ]]; then
  origin_ip_internal=10.1.255.250
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

echo "starting origin at ${origin_ip_internal}"
sudo nginx -s stop
sudo nginx -c ~/webtracereplay/server/nginx.conf
pkill -9 -f origin/origin
spawn-fcgi -a 127.0.0.1 -p 9000 -n ${home}/origin/origin ${home}/${trace}_origin.tr ${n_origin_threads} ${latency} 2>${home}/log/origin_"${phase}"_"${suffix}".err </dev/null | tee ${home}/log/origin_"${phase}"_"${suffix}".log | stdbuf -o0 awk '{print "'${suffix}' origin_throughput="$2",origin_n_req="$1}' &>> /tmp/influx.log

