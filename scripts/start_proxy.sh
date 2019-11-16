#!/usr/bin/env bash


if [[ "$#" = 1 ]]; then
  suffix=$1
elif [[ "$#" = 0 ]]; then
  suffix=wiki_1400m_4mb_fifo_0_pni_0
else
    echo "Illegal number of parameters"
    exit 1
fi

rm -rf /opt/ts/var/log/trafficserver/*
pkill -9 -f trafficserver
/opt/ts/bin/traffic_server -Cclear
curl -XPOST 'http://mmx.cs.princeton.edu:8086/query?db=mydb' -u admin:system --data-urlencode 'q=DROP MEASUREMENT '${suffix}
echo ' ' > /tmp/influx.log ; tail -f /tmp/influx.log | while read v; do curl -m 1 -XPOST 'http://mmx.cs.princeton.edu:8086/write?db=mydb' -u admin:system --data-binary "$v";done &
env LD_PRELOAD="/usr/lib64/libtcmalloc.so.4" /opt/ts/bin/traffic_server

