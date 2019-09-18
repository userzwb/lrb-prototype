#!/bin/bash

#TODO: restore original config
traces=(wc1400m_ts ntg1_400m_16mb)
#traces=(ntg1_400m_16mb)

algs=(fifo lru wlc)
#algs=(lru wlc)

request_rates=(0 1)
#request_rates=(0)

rm /home/zhenyus/webtracereplay/scripts/measurements.job
for alg in "${algs[@]}"; do
for trace in "${traces[@]}"; do
for request_rate in "${request_rates[@]}"; do
  suffix=${trace}_${alg}_${request_rate}
  echo "bash -x /home/zhenyus/webtracereplay/scripts/measure_new.sh "$trace" "$alg" "$request_rate" &> "/tmp/${suffix}_prototype.log  >>  /home/zhenyus/webtracereplay/scripts/measurements.job
done
done
done

parallel -v --jobs 12 --eta < /home/zhenyus/webtracereplay/scripts/measurements.job
