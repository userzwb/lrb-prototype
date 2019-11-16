#!/bin/bash

#TODO: restore original config
#traces=(wiki_1400m_4mb ntg1_400m_4mb)
traces=(wiki_1400m_4mb)

algs=(ats fifo lru)
#algs=(fifo lru wlc ats)

#request_rates=(0 1)
request_rates=(0)

test_beds=(pni)
#test_beds=(pni gcp)


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

##TODO: run more WLC
#alg='wlc'
#trails=(1 2 3)
#for trail in "${trails[@]}"; do
#for trace in "${traces[@]}"; do
#for request_rate in "${request_rates[@]}"; do
#  suffix=${trace}_${alg}_${request_rate}_${trail}
#  echo "bash -x /home/zhenyus/webtracereplay/scripts/measure.sh "$trace" "$alg" "$request_rate" "$trail" &> "/tmp/${suffix}_prototype.log  >>  /home/zhenyus/webtracereplay/scripts/measurements.job
#done
#done
#done

# PNI: one by one
parallel -v --jobs 1 --eta < /home/zhenyus/webtracereplay/scripts/measurements.job
