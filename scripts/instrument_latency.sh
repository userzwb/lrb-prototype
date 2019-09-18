#!/bin/bash

interface=$(ip link | awk -F: '$0 !~ "lo|vir|wl|^[^0-9]"{print $2;getline}')
interface="${interface:1:${#interface}-1}"
latency="10ms"
proxy_ip=$1

sudo tc qdisc del dev "$interface" root
sudo tc qdisc add dev "$interface" root handle 1: prio
sudo tc filter add dev "$interface" parent 1:0 protocol ip prio 1 u32 match ip dst "$proxy_ip" flowid 2:1
sudo tc qdisc add dev "$interface" parent 1:1 handle 2: netem delay $latency