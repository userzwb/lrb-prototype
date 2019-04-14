#!/usr/bin/env bash
# Asssume not recompile, origin already running. Only change size

# algs: fifo lru static random gbdt
algs=(random gbdt)
# for debug
#u=k
# for dev
u=m

sizes=(128G)
n_client=256

for alg in "${algs[@]}"; do
for s in "${sizes[@]}"; do
	#modify size
	if [[ ${alg} = "gbdt" ]]; then
		alg_idx=0
	elif [[ ${alg} = "lru" ]]; then
	    alg_idx=1
	elif [[ ${alg} = "static" ]]; then
		alg_idx=2
	elif [[ ${alg} = "random" ]]; then
		alg_idx=3
	elif [[ ${alg} = "fifo" ]]; then
		alg_idx=-1
	else
	    echo "error: algorithm ${alg} not found"
	    return -1
	fi
	ssh cache_proxy "sed -i 's/^CONFIG proxy.config.cache.vdisk_cache.algorithm.*/CONFIG proxy.config.cache.vdisk_cache.algorithm INT ${alg_idx}/g' /opt/ts/etc/trafficserver/records.config"
	ssh cache_proxy "sed -i 's/\/dev\/fioa.*/\/dev\/fioa ${s}/g' /opt/ts/etc/trafficserver/storage.config"
	#restart
	ssh cache_proxy "/opt/ts/bin/trafficserver restart"

	# warmup
	ssh cache_client "cd ~/webtracereplay; ./client/client client_200${u}_00.tr ${n_client} n01:6000/ log/warmup_throughput_${u}_${alg}_${s}.log log/warmup_latency_${u}_${alg}_${s}.log 0"
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
done

#download
scp cache_client:~/webtracereplay/log/* .
scp ./* fat:~/webcachesim/ats_log/
