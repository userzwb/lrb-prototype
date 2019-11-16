#!/usr/bin/env bash


if [[ "$#" = 6 ]]; then
  origin_ip_internal=$1
  trace=$2
  n_origin_threads=$3
  latency=$4
  phase=$5
  suffix=$6
else
    echo "Illegal number of parameters"
    exit 1
fi

echo "starting origin at ${origin_ip_internal}"
ssh "$origin_ip_internal" "sudo nginx -s stop"
ssh "$origin_ip_internal" "sudo nginx -c ~/webtracereplay/server/nginx.conf"
ssh "$origin_ip_internal" pkill -f origin
ssh "$origin_ip_internal" "cd ~/webtracereplay/origin && spawn-fcgi -a 127.0.0.1 -p 9000 -n ./origin ../"${trace}"_origin.tr "${n_origin_threads}" "${latency}" > ../log/origin_"${phase}"_"_${suffix}".log" &

