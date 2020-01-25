#!/bin/bash

proxy_ip=$1
test_bed=$2
if [[ ${test_bed} == "pni" ]]; then
  interface=$(/usr/sbin/ip link | awk -F: '$0 !~ "lo|vir|wl|^[^0-9]"{print $2;getline}' | head -n 1)
else
  interface=$(/sbin/ip link | awk -F: '$0 !~ "lo|vir|wl|^[^0-9]"{print $2;getline}' | head -n 1)
fi
interface="${interface:1:${#interface}-1}"
latency="10ms"

sudo tc qdisc del dev "$interface" root
sudo tc qdisc add dev "$interface" root handle 1: prio
sudo tc filter add dev "$interface" parent 1:0 protocol ip prio 1 u32 match ip dst "$proxy_ip" flowid 2:1
sudo tc qdisc add dev "$interface" parent 1:1 handle 2: netem delay $latency