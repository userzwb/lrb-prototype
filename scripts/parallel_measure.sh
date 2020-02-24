#!/bin/bash

traces=(wiki2018_4mb)

algs=(fifo ats LRU LRB)

request_rates=(0 1)

test_beds=(gcp)


rm /home/zhenyus/webtracereplay/scripts/measurements.job
for alg in "${algs[@]}"; do
for trace in "${traces[@]}"; do
for request_rate in "${request_rates[@]}"; do
for test_bed in "${test_beds[@]}"; do
  trail=0
  suffix=${trace}_${alg}_${request_rate}_${test_bed}_${trail}
  echo "bash -x /home/zhenyus/webtracereplay/scripts/measure.sh "$trace" "$alg" "$request_rate" "$test_bed" "$trail" &> "/tmp/${suffix}_prototype.log  >>  /home/zhenyus/webtracereplay/scripts/measurements.job
done
done
done
done

# PNI: one by one
parallel -v --jobs 12 --eta < /home/zhenyus/webtracereplay/scripts/measurements.job
