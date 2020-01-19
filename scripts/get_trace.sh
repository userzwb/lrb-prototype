#!/bin/bash

~/trace_operation/build/chunk wiki2018.tr wiki2018_4mb.tr && cat wiki2018_4mb.tr | awk -F' ' '{size[$2]=$3;f1[$2]=$4}END{for (k in size){print k,size[k],f1[k]}}'|sort -n -k1,1 > wiki2018_4mb_origin.tr && head -n 2400000000 wiki2018_4mb.tr > wiki2018_4mb_warmup.tr && tail -n +2400000001 wiki2018_4mb.tr > wiki2018_4mb_eval.tr && head -n 2400000000 wiki2018_4mb.tr > wiki2018_4mb_warmup.tr && tail -n +2400000001 wiki2018_4mb.tr > wiki2018_4mb_eval.tr

~/trace_operation/build/chunk ntg1_200m_base.tr ntg1_200m_base_4mb.tr && cat ntg1_200m_base_4mb.tr |awk -F' ' '{size[$2]=$3;f1[$2]=$4;f2[$2]=$5}END{for (k in size){print k,size[k],f1[k],f2[k]}}'|sort -n -k1,1  > ntg1_200m_base_4mb_origin.tr && head -n 170000000 ntg1_200m_base_4mb.tr > ntg1_200m_base_4mb_warmup.tr && tail -n +170000001 ntg1_200m_base_4mb.tr > ntg1_200m_base_4mb_eval.tr


