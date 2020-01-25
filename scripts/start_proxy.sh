#!/usr/bin/env bash

if [[ "$#" = 2 ]]; then
  suffix=$1
  test_bed=$2
elif [[ "$#" = 0 ]]; then
  suffix=wiki_1400m_4mb_fifo_0_pni_0
  test_bed=gcp
else
    echo "Illegal number of parameters"
    exit 1
fi

trap '[[ -z "$(jobs -p)" ]] || kill $(jobs -p)' EXIT

rm -f /opt/ts/var/log/trafficserver/*
while [[ ! -z $(ps aux|grep traffic_server|grep -v grep) ]]; do
  echo 'killing traffic_server'
  pkill -f traffic_server
  sleep 1
done
/opt/ts/bin/traffic_server -Cclear
curl -s -XPOST 'http://mmx.cs.princeton.edu:8086/query?db=mydb' -u admin:system --data-urlencode 'q=DROP MEASUREMENT '${suffix}
touch /tmp/influx.log ; tail -f /tmp/influx.log | while read v; do curl -s -m 1 -XPOST 'http://mmx.cs.princeton.edu:8086/write?db=mydb' -u admin:system --data-binary "$v";done &
if [[ ${test_bed} == "pni" ]]; then
  env LD_PRELOAD="/usr/lib64/libtcmalloc.so.4" /opt/ts/bin/traffic_server
else
  env LD_PRELOAD="/usr/lib/x86_64-linux-gnu/libtcmalloc.so.4" /opt/ts/bin/traffic_server
fi

