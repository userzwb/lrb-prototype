#!/usr/bin/env bash

alg=fifo
#alg=gbdt
#2424// for debug
#u=k
# for dev
u=m

# warmup
ssh cache_client "cd ~/webtracereplay; ./client/client client_100${u}.tr 256 n01:6000/ throughput.log latency.log 0"
sleep 5  #for sync
ssh cache_proxy "/opt/ts/bin/traffic_ctl metric get proxy.process.cache_total_misses_bytes" > byte_miss_${u}_${alg}.log
ssh cache_proxy "/opt/ts/bin/traffic_ctl metric get proxy.process.cache_total_bytes" > byte_${u}_${alg}.log
date +%s > date_${u}_${alg}.log

# for 1 to 256
for i in $(seq 0 8);do
 n_client=$(( 2**$i ))
## echo $n_client
 ssh cache_client "cd ~/webtracereplay; ./client/client client_200${u}_1${i}.tr ${n_client} n01:6000/ log/throughput_${i}${u}_${alg}.log log/latency_${i}${u}_${alg}.log 0"
 sleep 5  #for sync
 ssh cache_proxy "/opt/ts/bin/traffic_ctl metric get proxy.process.cache_total_misses_bytes" >> byte_miss_${u}_${alg}.log
 ssh cache_proxy "/opt/ts/bin/traffic_ctl metric get proxy.process.cache_total_bytes" >> byte_${u}_${alg}.log
 date +%s >> date_${u}_${alg}.log
done

#download
scp cache_client:~/webtracereplay/log/latency* .
scp ./* fat:~/webcachesim/ats_log/