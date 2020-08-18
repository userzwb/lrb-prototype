# webtracereplay2

This project provides simple tools to replay http request traces to evaluate the performance of caching systems and webservers.
We also build a simulator available [here](https://github.com/sunnyszy/lrb).

There are three components:

 - the client: which reads in the trace file and generates valid http requests
 - the proxy: the cache server. We build on top of Apache Traffic Server.
 - the origin: which emulates a database or storage server

These tools were built to evaluate the Learning relaxed Belady algorithm (LRB), a new machine-learning-based caching algorithm. The client and origin were build on top of [webtracereplay](https://github.com/dasebe/webtracereplay), see [References](#references) for more information.

## Install webtracereplay

Please follow the [instruction](INSTALL.md) to install the origin, client and cache proxy.

## Request traces

We will need request traces for the client (to request objects) and the origin (to emulate objects).
We preprocess the trace for you: [download link](http://lrb.cs.princeton.edu/wiki2018_4mb.tar.gz). To uncomress:
```shell script
tar -xzvf wiki2018_4mb.tar.gz
#move origin trace (wiki2018_4mb_origin.tr) to origin:~/webtracereplay/
#move client trace (wiki2018_4mb_warmup.tr and wiki2018_4mb_eval.tr) to client:~/webtracereplay/
```

### Trace format 

The trace format is similar to [LRB simulator](https://github.com/sunnyszy/lrb#trace-format). The differences:

* Request is chunked to 4MB at maximum. This is to simply ATS caching without considering internal storage chunking.
* Origin trace only has key, size, extra feature information without time. It it used to generate "fake" response.
* Client trace only has time, id, size information without extra features. The size information is used only for measurement.

## Run an experiment

We made a scripts to run the experiment on Google Cloud. To run it
* Create a VM in google cloud with Ubuntu 18.04 OS
* Install client/origin/proxy on this machine.
* Download the [Wikipedia trace](#request-traces) to this machine.
* Make a snapshot of this machine, and then terminate this machine.
* On your local machine, fill in the Google Cloud variables in [scripts/measure.sh](scripts/measure.sh). Note that you may need to modified the home directory and username in the scripts.
* Use [scripts/measurement.sh](scripts/measure.sh) from your local machine to run an experiment.

#### `measurement.sh` usage
```shell script
./measurement.sh trace algorithm trace_timestamp test_bed trail
```
* trace: the trace file prefix
* algorithm: current support: LRB, LRU, FIFO, Unmodified
* trace_timestamp: 0 (clients send requests in a closed-loop) or 1 (clients send request based on trace timestamp)
* test_bed: current only support: gcp
* trail: versioning purpose. Not effect in results.

Result log will be download to current local folder after experiment finishes.

#### Example: test Unmodified ATS under max workload 
```shell script
# on your local machine
./measurement.sh wiki2018_4mb Unmodified 0 gcp 0
```

#### Example: test LRB under normal workload (trace timestamp)
```shell script
# on your local machine
./measurement.sh wiki2018_4mb LRB 1 gcp 0
```

## Result format

throughput.log: client side throughput

| reqs/s | bytes/s | sampled latency | time since last print (usually 100ms)|
| --- | ---- | ----| --- |

origin.log: origin side throughput

| reqs/s | bytes/s | time since last print (usually 1000ms)|
| --- | ---- | ---- |

latency.log: user latency histogram

| type (end-to-end latency or first-byte latency)| latency (log10(ns))| count|
| --- | --- | ---|


top.log: log from top command

ps.log: log from ps command with pcpu,rss,vsz fields.


## Contributors are welcome

Want to contribute? Great! We follow the [Github contribution work flow](https://help.github.com/articles/github-flow/).
This means that submissions should fork and use a Github pull requests to get merged into this code base.

If you come across a bug in webcachesim, please file a bug report by [creating a new issue](https://github.com/sunnyszy/lrb-prototype/issues/new).


## References

We ask academic works, which built on this code, to reference the LRB/AdaptSize papers:

    Learning Relaxed Belady for Content Distribution Network Caching
    Zhenyu Song, Daniel S. Berger, Kai Li, Wyatt Lloyd
    USENIX NSDI 2020.
    
    AdaptSize: Orchestrating the Hot Object Memory Cache in a CDN
    Daniel S. Berger, Ramesh K. Sitaraman, Mor Harchol-Balter
    USENIX NSDI 2017.
