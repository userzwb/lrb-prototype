# 本质上来说，in_ip是局域网ip，外网是无法直接连接到的（类似华为服务器，服务器通过会拥有私有ip和公有ip）
# 外网无法直接接入google cloude，所以得需要一台服务器有外部ip，再通过外部ip连接到内部ip，最终实现对每个服务器的控制
# 所以本质上，很多操作都是为了实现对服务器的控制和脚本的运行
# 自己的服务器因为虚拟机都是能够直接通信的，所以我们并不需要external_ip
#!/usr/bin/env bash
# Asssume traffic server is built and installed

#TODO: Fill in your Google Cloud detail
#google-cloud-project=
# google-cloud-zone=
# google-cloud-snapshot-id=
# google-cloud-service-account=

# 判断参数数量
if [[ "$#" = 5 ]]; then
  trace=$1
  alg=$2
  real_time=$3
  test_bed=$4
  trail=$5
elif [[ "$#" = 0 ]]; then
# 默认参数
  trace=wiki2018_4mb
  alg=LRB
  real_time=0
  test_bed=gcp
  trail=0
else
    echo "Illegal number of parameters"
    exit 1
fi



n_origin_threads=2048
# 将参数整合，用于client、origin和server启动
suffix=${trace}_${alg}_${real_time}_${test_bed}_${trail}

# 设置缓存大小？32GB，差距可能是在元数据的空间占用上
if [[ ${alg} = "LRB" ]]; then
  if [[ ${trace} = 'wiki2018_4mb' ]]; then
    ram_size=29429944320
    memory_window=748938014
#    memory_window=58720256
  else
    ram_size=31006543872
    memory_window=100663296
  fi
elif [[ ${alg} = "LRU" ]]; then
  if [[ ${trace} = 'wiki2018_4mb' ]]; then
    ram_size=33044983808
  else
    ram_size=32153886720
  fi
else
  ram_size=34359738368
fi

if [[ ${trace} = 'wiki2018_4mb' ]]; then
  ssd_config="--local-ssd=interface=NVME --local-ssd=interface=NVME --local-ssd=interface=NVME --local-ssd=interface=NVME --local-ssd=interface=NVME --local-ssd=interface=NVME --local-ssd=interface=NVME --local-ssd=interface=NVME"
  cache_size=1099511627776
  n_warmup_client=180
  n_client=1024
else
# 将本地 SSD 的接口类型设置为 NVME（NVME 是一种高性能的存储协议，通常用于固态硬盘。）
  ssd_config="--local-ssd=interface=NVME --local-ssd=interface=NVME --local-ssd=interface=NVME --local-ssd=interface=NVME --local-ssd=interface=NVME --local-ssd=interface=NVME --local-ssd=interface=NVME --local-ssd=interface=NVME"
  cache_size=1099511627776
  n_warmup_client=1024
  n_client=1024
fi


if [[ ${test_bed} = 'gcp' ]]; then
  echo "error: no google cloud account"
# google云上执行的指令，创建三台虚拟机，并获取其ip
  #create client
  # client_name=client-${trace:0:1}-${alg}-${real_time}-${trail}

  # echo $client_name
  # gcloud compute --project "${google-cloud-project}" disks create $client_name --size "128" --zone "${google-cloud-zone}" --source-snapshot ${google-cloud-snapshot-id} --type "pd-standard"

  # gcloud beta compute --project=${google-cloud-project} instances create $client_name --zone=${google-cloud-zone} --machine-type=n1-standard-16 --subnet=default --no-address --network-tier=PREMIUM --maintenance-policy=MIGRATE --service-account=${google-cloud-service-account} --scopes=https://www.googleapis.com/auth/devstorage.read_only,https://www.googleapis.com/auth/logging.write,https://www.googleapis.com/auth/monitoring.write,https://www.googleapis.com/auth/servicecontrol,https://www.googleapis.com/auth/service.management.readonly,https://www.googleapis.com/auth/trace.append --disk=name=${client_name},device-name=${client_name},mode=rw,boot=yes,auto-delete=yes --reservation-affinity=any

  # client_ip_internal=$( gcloud compute instances describe $client_name --format='get(networkInterfaces[0].networkIP)' )

  # echo "$client_ip_internal"

  # #create origin
  # origin_name=origin-${trace:0:1}-${alg}-${real_time}-${trail}
  # echo $origin_name
  # gcloud compute --project "${google-cloud-project}" disks create $origin_name --size "128" --zone "${google-cloud-zone}" --source-snapshot ${google-cloud-snapshot-id} --type "pd-standard"

  # gcloud beta compute --project=${google-cloud-project} instances create $origin_name --zone=${google-cloud-zone} --machine-type=n1-standard-16 --subnet=default --no-address --network-tier=PREMIUM --maintenance-policy=MIGRATE --service-account=${google-cloud-service-account} --scopes=https://www.googleapis.com/auth/devstorage.read_only,https://www.googleapis.com/auth/logging.write,https://www.googleapis.com/auth/monitoring.write,https://www.googleapis.com/auth/servicecontrol,https://www.googleapis.com/auth/service.management.readonly,https://www.googleapis.com/auth/trace.append --disk=name=${origin_name},device-name=${origin_name},mode=rw,boot=yes,auto-delete=yes --reservation-affinity=any

  # origin_ip_internal=$( gcloud compute instances describe $origin_name --format='get(networkInterfaces[0].networkIP)' )

  # echo "$origin_ip_internal"

  # #create proxy
  # proxy_name=proxy-${trace:0:1}-${alg}-${real_time}-${trail}
  # echo $proxy_name
  # gcloud compute --project "${google-cloud-project}" disks create $proxy_name --size "128" --zone "${google-cloud-zone}" --source-snapshot ${google-cloud-snapshot-id} --type "pd-standard"

  # gcloud beta compute --project=${google-cloud-project} instances create $proxy_name --zone=${google-cloud-zone} --machine-type=n1-standard-64 --subnet=default --network-tier=PREMIUM --maintenance-policy=MIGRATE --service-account=${google-cloud-service-account} --scopes=https://www.googleapis.com/auth/devstorage.read_only,https://www.googleapis.com/auth/logging.write,https://www.googleapis.com/auth/monitoring.write,https://www.googleapis.com/auth/servicecontrol,https://www.googleapis.com/auth/service.management.readonly,https://www.googleapis.com/auth/trace.append --disk=name=${proxy_name},device-name=${proxy_name},mode=rw,boot=yes,auto-delete=yes ${ssd_config} --reservation-affinity=any

  # proxy_ip_internal=$( gcloud compute instances describe $proxy_name --format='get(networkInterfaces[0].networkIP)' )
  # proxy_ip_internal=$( gcloud compute instances describe $proxy_name --format='get(networkInterfaces[0].accessConfigs[0].natIP)' )

  # echo "$proxy_ip_internal"
  # echo "$proxy_ip_internal"

  # ssh-keygen -R "$proxy_ip_internal"
  # ssh-keygen -R "$client_ip_internal"
  # ssh-keygen -R "$origin_ip_internal"
  # echo "wait until the servers ready"
  # until ssh ${proxy_ip_internal} 'echo 1>/dev/null'; do
  #   echo "waiting proxy"
  #   sleep 5
  # done

  # until ssh -o ProxyJump=${proxy_ip_internal} $client_ip_internal 'echo 1>/dev/null'; do
  #   echo "waiting client"
  #   sleep 5
  # done

  # until ssh -o ProxyJump=${proxy_ip_internal} $origin_ip_internal 'echo 1>/dev/null'; do
  #   echo "waiting origin"
  #   sleep 5
  # done

  # echo "updating repo"
  # ssh "$proxy_ip_internal" "cd ~/webtracereplay/origin && git pull && make"
  # ssh "$proxy_ip_internal" "cd ~/webtracereplay/client && git pull && make"
  # ssh -o ProxyJump=${proxy_ip_internal} $client_ip_internal "cd ~/webtracereplay/client && git pull && make"
  # ssh -o ProxyJump=${proxy_ip_internal} $origin_ip_internal "cd ~/webtracereplay/origin && git pull && make"

  # #change config based on trace, alg: hosting.config, records.config, storage.config, volume.config
  # #use single SSD
  # ssh "$proxy_ip_internal" "cp ~/webtracereplay/tsconfig_backup/storage.config ~/webtracereplay/tsconfig/storage.config"

  # echo "set proxy SSD permission"
  # ssh "$proxy_ip_internal" 'sudo apt-get update && sudo apt-get install mdadm --no-install-recommends'

  # ssh "$proxy_ip_internal" 'sudo mdadm --create /dev/md0 --level=0 --raid-devices=8 /dev/nvme0n1 /dev/nvme0n2 /dev/nvme0n3 /dev/nvme0n4 /dev/nvme0n5 /dev/nvme0n6 /dev/nvme0n7 /dev/nvme0n8'

  # ssh "$proxy_ip_internal" 'sudo chmod 777 /dev/md0'
  # home=/home/zhenyus/webtracereplay

elif [[ ${test_bed} = "pni" ]]; then
  # proxy_ip_internal=cache_proxy
  proxy_ip_internal=root@192.168.122.238
  client_ip_internal=root@192.168.122.108
  origin_ip_internal=root@192.168.122.154

  echo "updating repo"
  ssh "$proxy_ip_internal" "cd ~/webtracereplay/origin && git pull && make"
  ssh "$proxy_ip_internal" "cd ~/webtracereplay/client && git pull && make"
  ssh $client_ip_internal "cd ~/webtracereplay/client && git pull && make"
  ssh $origin_ip_internal "cd ~/webtracereplay/origin && git pull && make"

  #use single SSD
  ssh "$proxy_ip_internal" "cp ~/webtracereplay/tsconfig_backup/storage_pni.config /opt/ts/etc/trafficserver/storage.config"
  home=~/webtracereplay

  echo "trimming SSD"
  ssh "$proxy_ip_internal" "/sbin/blkdiscard /dev/fioa"

else
   echo "wrong test_bed"
   exit 1
fi


#change config based on trace, alg: hosting.config, records.config, storage.config, volume.config
# 设置缓存算法和缓存大小
ssh "$proxy_ip_internal" "sed -i 's/^CONFIG proxy.config.cache.ram_cache.size.*/CONFIG proxy.config.cache.ram_cache.size INT "${ram_size}"/g' /opt/ts/etc/trafficserver/records.config"
if [[ ${alg} = "LRB" ]]; then
	ssh "$proxy_ip_internal" "sed -i 's/^.*CONFIG proxy.config.cache.vdisk_cache.algorithm.*/CONFIG proxy.config.cache.vdisk_cache.algorithm STRING LRB/g' /opt/ts/etc/trafficserver/records.config"
	ssh "$proxy_ip_internal" "sed -i 's/^CONFIG proxy.config.cache.vdisk_cache.memory_window.*/CONFIG proxy.config.cache.vdisk_cache.memory_window INT "${memory_window}"/g' /opt/ts/etc/trafficserver/records.config"
elif [[ ${alg} = "LRU" ]]; then
	ssh "$proxy_ip_internal" "sed -i 's/^.*CONFIG proxy.config.cache.vdisk_cache.algorithm.*/CONFIG proxy.config.cache.vdisk_cache.algorithm STRING LRU/g' /opt/ts/etc/trafficserver/records.config"
elif [[ ${alg} = "FIFO" ]]; then
	ssh "$proxy_ip_internal" "sed -i 's/^.*CONFIG proxy.config.cache.vdisk_cache.algorithm.*/CONFIG proxy.config.cache.vdisk_cache.algorithm STRING FIFO/g' /opt/ts/etc/trafficserver/records.config"
elif [[ ${alg} = "Unmodified" ]]; then
	ssh "$proxy_ip_internal" "sed -i 's/^.*CONFIG proxy.config.cache.vdisk_cache.algorithm.*/#CONFIG proxy.config.cache.vdisk_cache.algorithm STRING/g' /opt/ts/etc/trafficserver/records.config"
elif [[ ${alg} = "Static" ]]; then
	ssh "$proxy_ip_internal" "sed -i 's/^.*CONFIG proxy.config.cache.vdisk_cache.algorithm.*/CONFIG proxy.config.cache.vdisk_cache.algorithm STRING Static/g' /opt/ts/etc/trafficserver/records.config"
else
  echo "error: no algorithm found"
  exit 1
fi

kill_background() {
ssh "$origin_ip_internal" "pkill -f origin"
ssh ${proxy_ip_internal} "pkill -f traffic_server"
ssh "$proxy_ip_internal" 'pkill -f segment_ps'
ssh "$proxy_ip_internal" 'pkill -f segment_top'
ssh ${proxy_ip_internal} "pkill -f start_proxy"
ssh "$proxy_ip_internal" pkill -f client
ssh $client_ip_internal pkill -f client
}

trap 'kill_background' EXIT

echo "set client latency"
# 设置延迟
ssh $client_ip_internal bash ${home}/scripts/instrument_latency.sh $proxy_ip_internal ${test_bed}

# 启动origin，并设置输出
echo "starting origin"
ssh "$origin_ip_internal" "pkill -f origin"
ssh "$origin_ip_internal" "nohup ~/webtracereplay/scripts/start_origin.sh "${trace}" "${n_origin_threads}" 100 eval "${suffix}" "${home}" &>/tmp/start_origin_"${suffix}".log &"
# wait enought time for origin to init
sleep 300

# 启用远程代理
echo "use remote proxy"
ssh "$proxy_ip_internal" ${home}/scripts/remap_remote.sh $origin_ip_internal

#restart
ssh ${proxy_ip_internal} "pkill -f traffic_server"
ssh ${proxy_ip_internal} "nohup ~/webtracereplay/scripts/start_proxy.sh "${suffix}" "${test_bed}" &>/tmp/start_proxy.log &"
# wait enought time for traffic_server to init
sleep 300

echo "start measuring segment statistics"
ssh "$proxy_ip_internal" 'pkill -f segment_ps'
ssh "$proxy_ip_internal" 'pkill -f segment_top'
ssh "$proxy_ip_internal" "nohup "${home}"/scripts/segment_ps.sh "${suffix}" "${test_bed}" &>/tmp/segment_ps.log &"
ssh "$proxy_ip_internal" "nohup "${home}"/scripts/segment_top.sh "${suffix}" "${test_bed}" &>/tmp/segment_top.log &"

echo "warmuping up"
ssh $client_ip_internal pkill -f client
#estimate finish = 90*1024/1.2/3600 = 21.333333333333332
ssh $client_ip_internal "~/webtracereplay/scripts/start_client.sh "${suffix}" "${trace}" warmup "${n_client}" "${proxy_ip_internal}" 129600 0 &>/tmp/start_client_"${suffix}".log"
sleep 15 # for sync

echo "using remote client"
ssh $client_ip_internal pkill -f client
#estimate finish = 90/2800*400*1024/1.2/3600 = 3.0476190476190474
ssh $client_ip_internal "~/webtracereplay/scripts/start_client.sh "${suffix}" "${trace}" eval "${n_client}" "${proxy_ip_internal}" 16200 "${real_time}" &>/tmp/start_client_"${suffix}".log"
sleep 15 # for sync
echo "stop measuring segment stat"
ssh "$proxy_ip_internal" 'pkill -f segment_ps'
ssh "$proxy_ip_internal" 'pkill -f segment_top'
ssh "$proxy_ip_internal" 'tail -n 10000 /opt/ts/var/log/trafficserver/small.log' > /home/zhenyus/gcp_log/small_eval_${suffix}.log

echo "downloading..."
scp -3 "$origin_ip_internal":~/webtracereplay/log/* ./
scp -3 "$client_ip_internal":~/webtracereplay/log/* ./
rsync "$proxy_ip_internal":~/webtracereplay/log/* ./
scp -3 "$proxy_ip_internal":/opt/ts/var/log/trafficserver/diag.log ./

echo "deleting vms"

if [[ ${test_bed} = 'gcp' ]]; then
  echo ${suffix} finish
  # enable deleting
#  gcloud compute instances delete --quiet $origin_name
#  gcloud compute instances delete --quiet $client_name
#  gcloud compute instances delete --quiet $proxy_name
elif [[ ${test_bed} = "pni" ]]; then
  echo ${suffix} finish
else
   echo "wrong test_bed"
   exit 1
fi

