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
ssh "$origin_ip_internal" "sudo nginx -s stop"
ssh "$origin_ip_internal" "sudo nginx -c ~/webtracereplay/server/nginx.conf"
ssh "$origin_ip_internal" pkill -f origin
ssh "$origin_ip_internal" "nohup spawn-fcgi -a 127.0.0.1 -p 9000 -n "${home}"/origin/origin "${home}"/"${trace}"_origin.tr "${n_origin_threads}" "${latency}" 1> "${home}"/log/origin_"${phase}"_"${suffix}".log 2>/dev/null </dev/null &"

