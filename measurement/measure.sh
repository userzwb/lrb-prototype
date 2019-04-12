#!/usr/bin/env bash
# Asssume not recompile, origin already running. Only change size

#alg=fifo
#alg=lru
alg=static
#alg=gbdt
# for debug
#u=k
# for dev
u=m

sizes=(128G)
n_client=256

for s in "${sizes[@]}"; do
	#modify size
	ssh cache_proxy "sed -i 's/\/dev\/fioa.*/\/dev\/fioa ${s}/g' /opt/ts/etc/trafficserver/storage.config"
	#restart
	ssh cache_proxy "/opt/ts/bin/trafficserver restart"

	# warmup
	ssh cache_client "cd ~/webtracereplay; ./client/client client_100${u}.tr ${n_client} n01:6000/ throughput.log latency.log 0"
	sleep 15  #for sync
	ssh cache_proxy "/opt/ts/bin/traffic_ctl metric get proxy.process.cache_total_misses_bytes" > byte_miss_${u}_${alg}_${s}.log
	ssh cache_proxy "/opt/ts/bin/traffic_ctl metric get proxy.process.cache_total_bytes" > byte_${u}_${alg}_${s}.log
	date +%s > date_${u}_${alg}_${s}.log

	## echo $n_client
    ssh cache_client "cd ~/webtracereplay; ./client/client client_200${u}_01.tr ${n_client} n01:6000/ log/throughput_${u}_${alg}_${s}.log log/latency_${u}_${alg}_${s}.log 0"
    sleep 15  #for sync
    ssh cache_proxy "/opt/ts/bin/traffic_ctl metric get proxy.process.cache_total_misses_bytes" >> byte_miss_${u}_${alg}_${s}.log
    ssh cache_proxy "/opt/ts/bin/traffic_ctl metric get proxy.process.cache_total_bytes" >> byte_${u}_${alg}_${s}.log
    date +%s >> date_${u}_${alg}_${s}.log
done

#download
scp cache_client:~/webtracereplay/log/* .
scp ./* fat:~/webcachesim/ats_log/
