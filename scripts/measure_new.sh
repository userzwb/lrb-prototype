#!/usr/bin/env bash
# Asssume not recompile

if [[ "$#" = 3 ]]; then
  trace=$1
  alg=$2
  real_time=$3
elif [[ "$#" = 0 ]]; then
  trace=wc1400m_ts
  #trace=ntg1_400m_16mb
  alg=lru
  #alg=fifo
  real_time=0
else
    echo "Illegal number of parameters"
    exit 1
fi

n_client=1024
n_origin_threads=1024
native_ats_snapshot="native-v1"
zhenyu_ats_snapshot="wlc-v2"

suffix=${trace}_${alg}_${real_time}

if [[ ${alg} = "wlc" ]]; then
  snapshot_id=$zhenyu_ats_snapshot
  if [[ ${trace} = 'wc1400m_ts' ]]; then
    ram_size=28998676480
    memory_window=117440512
  else
    ram_size=31006543872
    memory_window=12582912
  fi
  suffix=${suffix}_${memory_window}
elif [[ ${alg} = "lru" ]]; then
  snapshot_id=$zhenyu_ats_snapshot
  if [[ ${trace} = 'wc1400m_ts' ]]; then
    ram_size=32177463296
  else
    ram_size=32153886720
  fi
else
  snapshot_id=$native_ats_snapshot
  ram_size=34359738368
fi

if [[ ${trace} = 'wc1400m_ts' ]]; then
  ssd_config="--local-ssd=interface=NVME --local-ssd=interface=NVME --local-ssd=interface=NVME --local-ssd=interface=NVME"
  cache_size=549755813888
else
  ssd_config="--local-ssd=interface=NVME --local-ssd=interface=NVME --local-ssd=interface=NVME --local-ssd=interface=NVME --local-ssd=interface=NVME --local-ssd=interface=NVME --local-ssd=interface=NVME --local-ssd=interface=NVME"
  cache_size=1099511627776
fi


#create client
client_name=client-${trace:0:1}-${alg}-${real_time}
echo $client_name
gcloud compute --project "analog-delight-252816" disks create $client_name --size "128" --zone "us-east4-c" --source-snapshot $snapshot_id --type "pd-standard"

gcloud beta compute --project=analog-delight-252816 instances create $client_name --zone=us-east4-c --machine-type=n1-standard-16 --subnet=default --no-address --network-tier=PREMIUM --metadata=ssh-keys=zhenyus:ssh-rsa\ AAAAB3NzaC1yc2EAAAADAQABAAABAQCn207dzUfLZHccGWnk4gl\+76QU2D05ausXsqBNTMp9BvJeXEvbcSkauSsA1ih73nt2yI4yOs94SfwmXBa7ZNAp4nEy2mLDywLFjN/qhmWd4z1ucqw4mD5mJCHOFBWPimlWZmTpTkmauNFnfbGdmP2CspR2JJaNUORX/TPo0Xvj1aNLwfJn76voXDrPDaX5QqiOlhVRZJNJvWX/ybDLyllsh0eNAkVfTqNzyvKK/Ms7M3yKcicYccJBwY35rS0rxvpw9i9v5pvi\+81taXA5HX9KkjtA/keGcfhN95VO2vpVXmSWmYnsO5zv42xKzfC0USICV3fssdXj/H2bvvWrOKDD\ zhenyus --maintenance-policy=MIGRATE --service-account=78652309126-compute@developer.gserviceaccount.com --scopes=https://www.googleapis.com/auth/devstorage.read_only,https://www.googleapis.com/auth/logging.write,https://www.googleapis.com/auth/monitoring.write,https://www.googleapis.com/auth/servicecontrol,https://www.googleapis.com/auth/service.management.readonly,https://www.googleapis.com/auth/trace.append --disk=name=${client_name},device-name=${client_name},mode=rw,boot=yes,auto-delete=yes --reservation-affinity=any

client_ip_internal=$( gcloud compute instances describe $client_name --format='get(networkInterfaces[0].networkIP)' )

echo "$client_ip_internal"

#create origin
origin_name=origin-${trace:0:1}-${alg}-${real_time}
echo $origin_name
gcloud compute --project "analog-delight-252816" disks create $origin_name --size "128" --zone "us-east4-c" --source-snapshot $snapshot_id --type "pd-standard"

gcloud beta compute --project=analog-delight-252816 instances create $origin_name --zone=us-east4-c --machine-type=n1-standard-16 --subnet=default --no-address --network-tier=PREMIUM --metadata=ssh-keys=zhenyus:ssh-rsa\ AAAAB3NzaC1yc2EAAAADAQABAAABAQCn207dzUfLZHccGWnk4gl\+76QU2D05ausXsqBNTMp9BvJeXEvbcSkauSsA1ih73nt2yI4yOs94SfwmXBa7ZNAp4nEy2mLDywLFjN/qhmWd4z1ucqw4mD5mJCHOFBWPimlWZmTpTkmauNFnfbGdmP2CspR2JJaNUORX/TPo0Xvj1aNLwfJn76voXDrPDaX5QqiOlhVRZJNJvWX/ybDLyllsh0eNAkVfTqNzyvKK/Ms7M3yKcicYccJBwY35rS0rxvpw9i9v5pvi\+81taXA5HX9KkjtA/keGcfhN95VO2vpVXmSWmYnsO5zv42xKzfC0USICV3fssdXj/H2bvvWrOKDD\ zhenyus --maintenance-policy=MIGRATE --service-account=78652309126-compute@developer.gserviceaccount.com --scopes=https://www.googleapis.com/auth/devstorage.read_only,https://www.googleapis.com/auth/logging.write,https://www.googleapis.com/auth/monitoring.write,https://www.googleapis.com/auth/servicecontrol,https://www.googleapis.com/auth/service.management.readonly,https://www.googleapis.com/auth/trace.append --disk=name=${origin_name},device-name=${origin_name},mode=rw,boot=yes,auto-delete=yes --reservation-affinity=any

origin_ip_internal=$( gcloud compute instances describe $origin_name --format='get(networkInterfaces[0].networkIP)' )

echo "$origin_ip_internal"

#create proxy
proxy_name=proxy-${trace:0:1}-${alg}-${real_time}
echo $proxy_name
gcloud compute --project "analog-delight-252816" disks create $proxy_name --size "128" --zone "us-east4-c" --source-snapshot $snapshot_id --type "pd-standard"

gcloud beta compute --project=analog-delight-252816 instances create $proxy_name --zone=us-east4-c --machine-type=n1-standard-64 --subnet=default --network-tier=PREMIUM --metadata=ssh-keys=zhenyus:ssh-rsa\ AAAAB3NzaC1yc2EAAAADAQABAAABAQCn207dzUfLZHccGWnk4gl\+76QU2D05ausXsqBNTMp9BvJeXEvbcSkauSsA1ih73nt2yI4yOs94SfwmXBa7ZNAp4nEy2mLDywLFjN/qhmWd4z1ucqw4mD5mJCHOFBWPimlWZmTpTkmauNFnfbGdmP2CspR2JJaNUORX/TPo0Xvj1aNLwfJn76voXDrPDaX5QqiOlhVRZJNJvWX/ybDLyllsh0eNAkVfTqNzyvKK/Ms7M3yKcicYccJBwY35rS0rxvpw9i9v5pvi\+81taXA5HX9KkjtA/keGcfhN95VO2vpVXmSWmYnsO5zv42xKzfC0USICV3fssdXj/H2bvvWrOKDD\ zhenyus --maintenance-policy=MIGRATE --service-account=78652309126-compute@developer.gserviceaccount.com --scopes=https://www.googleapis.com/auth/devstorage.read_only,https://www.googleapis.com/auth/logging.write,https://www.googleapis.com/auth/monitoring.write,https://www.googleapis.com/auth/servicecontrol,https://www.googleapis.com/auth/service.management.readonly,https://www.googleapis.com/auth/trace.append --disk=name=${proxy_name},device-name=${proxy_name},mode=rw,boot=yes,auto-delete=yes ${ssd_config} --reservation-affinity=any

proxy_ip_internal=$( gcloud compute instances describe $proxy_name --format='get(networkInterfaces[0].networkIP)' )
proxy_ip_external=$( gcloud compute instances describe $proxy_name --format='get(networkInterfaces[0].accessConfigs[0].natIP)' )

echo "$proxy_ip_external"
echo "$proxy_ip_internal"

ssh-keygen -R "$proxy_ip_external"
ssh-keygen -R "$client_ip_internal"
ssh-keygen -R "$origin_ip_internal"
echo "wait until the servers ready"
until ssh ${proxy_ip_external} 'echo 1>/dev/null'; do
  echo "waiting proxy"
  sleep 5
done

until ssh -o ProxyJump=${proxy_ip_external} $client_ip_internal 'echo 1>/dev/null'; do
  echo "waiting client"
  sleep 5
done

until ssh -o ProxyJump=${proxy_ip_external} $origin_ip_internal 'echo 1>/dev/null'; do
  echo "waiting origin"
  sleep 5
done

echo "updating repo"
ssh "$proxy_ip_external" "cd ~/webtracereplay/origin && git pull && make"
ssh "$proxy_ip_external" "cd ~/webtracereplay/client && git pull && make"
ssh -o ProxyJump=${proxy_ip_external} $client_ip_internal "cd ~/webtracereplay/client && git pull && make"
ssh -o ProxyJump=${proxy_ip_external} $origin_ip_internal "cd ~/webtracereplay/origin && git pull && make"

#change config based on trace, alg: hosting.config, records.config, storage.config, volume.config
if [[ ${trace} = "wc1400m_ts" ]]; then
  ssh "$proxy_ip_external" "cp ~/webtracereplay/tsconfig_backup/hosting_4.config ~/webtracereplay/tsconfig_gcp/hosting.config"
  ssh "$proxy_ip_external" "cp ~/webtracereplay/tsconfig_backup/storage_4.config ~/webtracereplay/tsconfig_gcp/storage.config"
  ssh "$proxy_ip_external" "cp ~/webtracereplay/tsconfig_backup/volume_4.config ~/webtracereplay/tsconfig_gcp/volume.config"
elif [[ ${trace} = "ntg1_400m_16mb" ]]; then
  ssh "$proxy_ip_external" "cp ~/webtracereplay/tsconfig_backup/hosting_8.config ~/webtracereplay/tsconfig_gcp/hosting.config"
  ssh "$proxy_ip_external" "cp ~/webtracereplay/tsconfig_backup/storage_8.config ~/webtracereplay/tsconfig_gcp/storage.config"
  ssh "$proxy_ip_external" "cp ~/webtracereplay/tsconfig_backup/volume_8.config ~/webtracereplay/tsconfig_gcp/volume.config"
else
  echo "error: no trace found"
  exit 1
fi

#change config based on trace, alg: hosting.config, records.config, storage.config, volume.config
ssh "$proxy_ip_external" "sed -i 's/^CONFIG proxy.config.cache.ram_cache.size.*/CONFIG proxy.config.cache.ram_cache.size INT "${ram_size}"/g' /opt/ts/etc/trafficserver/records.config"
if [[ ${alg} = "wlc" ]]; then
	ssh "$proxy_ip_external" "sed -i 's/^CONFIG proxy.config.cache.vdisk_cache.algorithm.*/CONFIG proxy.config.cache.vdisk_cache.algorithm INT 0/g' /opt/ts/etc/trafficserver/records.config"
	ssh "$proxy_ip_external" "sed -i 's/^CONFIG proxy.config.cache.vdisk_cache.memory_window.*/CONFIG proxy.config.cache.vdisk_cache.memory_window INT "${memory_window}"/g' /opt/ts/etc/trafficserver/records.config"
elif [[ ${alg} = "lru" ]]; then
	ssh "$proxy_ip_external" "sed -i 's/^CONFIG proxy.config.cache.vdisk_cache.algorithm.*/CONFIG proxy.config.cache.vdisk_cache.algorithm INT 1/g' /opt/ts/etc/trafficserver/records.config"
elif [[ ${alg} = "fifo" ]]; then
  echo "not changing record because of fifo"
else
  echo "error: no algorithm found"
  exit 1
fi


echo "set client latency"
ssh -o ProxyJump=${proxy_ip_external} $client_ip_internal bash /home/zhenyus/webtracereplay/scripts/instrument_latency.sh $proxy_ip_internal

echo "starting origin"
ssh -o ProxyJump=${proxy_ip_external} "$origin_ip_internal" "sudo nginx -s stop"
ssh -o ProxyJump=${proxy_ip_external} "$origin_ip_internal" "sudo nginx -c ~/webtracereplay/server/nginx.conf"
ssh -o ProxyJump=${proxy_ip_external} "$origin_ip_internal" pkill -f origin
ssh -o ProxyJump=${proxy_ip_external} "$origin_ip_internal" "cd /home/zhenyus/webtracereplay/origin && spawn-fcgi -a 127.0.0.1 -p 9000 -n ./origin ../origin_"${trace}".tr "${n_origin_threads}" 100 > /tmp/proxy.log" &

echo "set proxy SSD permission"
ssh "$proxy_ip_external" 'for i in $(seq 8); do sudo chmod 777 /dev/nvme0n$i; done'

echo "open orgin in proxy"
ssh "$proxy_ip_external" "sudo nginx -s stop"
ssh "$proxy_ip_external" "sudo nginx -c ~/webtracereplay/server/nginx.conf"
ssh "$proxy_ip_external" pkill -f origin
ssh "$proxy_ip_external" "cd /home/zhenyus/webtracereplay/origin && spawn-fcgi -a 127.0.0.1 -p 9000 -n ./origin ../origin_"${trace}".tr "${n_origin_threads}" 0 > /tmp/proxy.log" &

echo "use local proxy"
ssh "$proxy_ip_external" /home/zhenyus/webtracereplay/scripts/remap_local.sh $origin_ip_internal

#restart
ssh "$proxy_ip_external" "/opt/ts/bin/trafficserver restart"

echo "warmuping up"
ssh "$proxy_ip_external" pkill -f client
ssh "$proxy_ip_external" 'rm /home/zhenyus/webtracereplay/log/*'
#TODO: remove this timeout later
#ssh "$proxy_ip_external" "cd /home/zhenyus/webtracereplay/client; ./client ../client_"${trace}"_warmup.tr "${n_client}" localhost:6000/ ../log/warmup_throughput_"${suffix}".log ../log/warmup_latency_"${suffix}".log 0"
ssh "$proxy_ip_external" "cd /home/zhenyus/webtracereplay/client; timeout 10 ./client ../client_"${trace}"_warmup.tr "${n_client}" localhost:6000/ ../log/warmup_throughput_"${suffix}".log ../log/warmup_latency_"${suffix}".log 0"

echo "switch to remote mode"
#use remote proxy and reload
ssh "$proxy_ip_external" /home/zhenyus/webtracereplay/scripts/remap_remote.sh $origin_ip_internal
ssh "$proxy_ip_external" "/opt/ts/bin/traffic_ctl config reload"
sleep 10

echo "start measuring segment stat"
#: record segment byte miss/req
ssh "$proxy_ip_external" 'pkill -f segment_static'
ssh "$proxy_ip_external" /home/zhenyus/webtracereplay/scripts/segment_static.sh ${suffix} &

echo "using remote client"
ssh -o ProxyJump=${proxy_ip_external} $client_ip_internal pkill -f client
ssh -o ProxyJump=${proxy_ip_external} $client_ip_internal 'rm /home/zhenyus/webtracereplay/log/*'
#TODO: make time out to be max 1 hour
ssh -o ProxyJump=${proxy_ip_external} $client_ip_internal "cd /home/zhenyus/webtracereplay/client; timeout 20 ./client ../client_"${trace}"_eval.tr "${n_client}" "${proxy_ip_internal}":6000/ ../log/eval_throughput_"${suffix}".log ../log/eval_latency_"${suffix}".log "${real_time}
sleep 15 # for sync
echo "stop measuring segment stat"
ssh "$proxy_ip_external" 'pkill -f segment_static'

echo "downloading..."
scp -3 "$proxy_ip_external":~/webtracereplay/log/* ~/gcp_log/
scp -3 "$proxy_ip_external":~/webtracereplay/log/* fat:~/webcachesim/gcp_log/
scp -3 -o ProxyJump=${proxy_ip_external} "$client_ip_internal":~/webtracereplay/log/* ~/gcp_log/
scp ~/gcp_log/* fat:~/webcachesim/gcp_log/

echo "deleting vms"
#TODO: enable deleting
#gcloud compute instances delete --quiet $origin_name
#gcloud compute instances delete --quiet $client_name
#gcloud compute instances delete --quiet $proxy_name

