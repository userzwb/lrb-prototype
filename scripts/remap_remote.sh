#!/bin/bash

origin_ip=$1
sed -i "198s/.*/map \/ http:\/\/${origin_ip}:7000/" /opt/ts/etc/trafficserver/remap.config
sed -i "199s/.*/map \/:6000 http:\/\/${origin_ip}:7000/" /opt/ts/etc/trafficserver/remap.config
sed -i '200s/.*/#map \/ http:\/\/localhost:7000/' /opt/ts/etc/trafficserver/remap.config
sed -i '201s/.*/#map \/:6000 http:\/\/localhost:7000/' /opt/ts/etc/trafficserver/remap.config
