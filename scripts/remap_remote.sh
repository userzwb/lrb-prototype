#!/bin/bash

origin_ip=$1
sed -i "197s/.*/map \/ http:\/\/${origin_ip}:7000/" /opt/ts/etc/trafficserver/remap.config
