#!/bin/bash

origin_ip=$1
sed -i '197s/.*/map \/ http:\/\/172.0.0.1:7000/' /opt/ts/etc/trafficserver/remap.config
