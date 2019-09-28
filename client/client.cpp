#include <iostream>
#include <fstream>
#include <stdio.h>
#include <queue>
#include <unordered_map>
#include <vector>
#include <utility>
#include <curl/curl.h>
#include <thread>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <chrono>
#include <atomic>
#include <string>
#include <stdlib.h>
#include <math.h>
#include <random>
#include <csignal>

using namespace std;
volatile bool queueFull;
volatile bool running;
mutex urlMutex;
mutex histMutex;
//key, length TODO: length calculation is broken
queue<pair<string, uint64_t>> urlQueue;
char *path;
string cacheip;
ofstream outTp;
bool real_time;
atomic_bool is_stop;

std::atomic<long> bytes;
std::atomic<long> reqs;
std::atomic<double> latency;

unordered_map<double, long> e2e_latency_histData;
unordered_map<double, long> fb_latency_histData;

static size_t throw_away(void *ptr, size_t size, size_t nmemb, void *data) {
    (void) ptr;
    (void) data;
    return (size_t) (size * nmemb);
}


void histogram(double val_e2e, double val_fb) {
    //input unit: log10(ns)
    histMutex.lock();
    e2e_latency_histData[round(val_e2e * 10) / 10.0]++;
    fb_latency_histData[round(val_fb * 10) / 10.0]++;
    histMutex.unlock();
}

int measureThread() {
    string currentID;
    uint64_t current_len;

    CURL *curl_handle;
    /* init the curl session */
    curl_handle = curl_easy_init();
    /*include header pragmas*/
    struct curl_slist *headers = NULL; // init to NULL is important
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
    /* no progress meter please */
    curl_easy_setopt(curl_handle, CURLOPT_NOPROGRESS, 1L);
    /* send all data to this function  */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, throw_away);
    //    curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, write_string);
    //    curl_easy_setopt(curl_handle, CURLOPT_WRITEHEADER, &currentHeader);
    /* set buffer for content */
    //    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &currentBody);

    while (!queueFull || !::urlQueue.empty()) {
        if (is_stop)
            return 0;
        urlMutex.lock();
        if (!::urlQueue.empty()) {
            currentID = ::urlQueue.front().first;
            current_len = ::urlQueue.front().second;

            urlQueue.pop();
            urlMutex.unlock();
        } else {
            urlMutex.unlock();
            //      cerr << "sleep for " << 10 << endl;
            this_thread::sleep_for(chrono::milliseconds(10));//wait a little bit
            continue;
        }
        //cerr << "get " << cacheip + currentID << "\n";
        /* set URL to get */
        curl_easy_setopt(curl_handle, CURLOPT_URL, (cacheip + currentID).c_str());
        curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 10L);
        //fetch URL
        CURLcode res;
        // if couldn't connect, try again
        for (int failc = 0; failc < 10; failc++) {
            //profile latency and perform
            res = curl_easy_perform(curl_handle);
            if (res == CURLE_OK)
                break;
            else if (res == CURLE_PARTIAL_FILE)
                break;  //fake
            else if (res == CURLE_COULDNT_CONNECT)
                this_thread::sleep_for(chrono::milliseconds(1));//wait a little bit
            else
                break; //fail and don't try again
        }


        //cannot get size information for fake header
//      double content_length = 0.0;
//      res = curl_easy_getinfo(curl_handle, CURLINFO_CONTENT_LENGTH_DOWNLOAD,
//			      &content_length);

        if ((CURLE_OK == res || CURLE_PARTIAL_FILE == res)) {
            bytes += (long) current_len;
            reqs++;

            double t_fb;
            curl_easy_getinfo(curl_handle, CURLINFO_STARTTRANSFER_TIME, &t_fb);
            double t_e2e;
            curl_easy_getinfo(curl_handle, CURLINFO_TOTAL_TIME, &t_e2e);
            latency.store(t_fb);
            //get elapsed time
            histogram(log10(t_e2e) + 9, log10(t_fb) + 9);
        }
        currentID.clear();
    }

    /* cleanup curl stuff */
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl_handle);

    return 0;
}

int requestCreate() {
    unordered_map<long, long> osizes;
    uint64_t time, id;
    uint64_t length;
    ifstream infile(path);
    if (!infile) {
        cerr << "Error: cannot opening file " << path << endl;
        abort();
    }

    auto wall_clock = chrono::steady_clock::now();
    unsigned int trace_clock;
    //init trace_clock
    std::string line;
    getline(infile, line);
    istringstream iss(line);
    iss >> trace_clock;
    infile.clear();
    infile.seekg(0, ios::beg);

    while (infile >> time >> id >> length) {
        if (is_stop)
            return 0;
        if (urlQueue.size() > 1000000) {
//        if (mean) {
//            cerr << "more than 1 million requests queuing, stop the client" << endl;
//            return 0;
//        } else
            //allow queuing so much
            this_thread::sleep_for(chrono::milliseconds(10));
        }
        if (real_time) {
            while (true) {
                auto wall_clock_elapse = chrono::duration<float>(chrono::steady_clock::now() - wall_clock).count();
                if (time - trace_clock <= wall_clock_elapse) {
                    wall_clock = chrono::steady_clock::now();
                    trace_clock = time;
                    break;
                } else {
                    auto sleep_time = chrono::microseconds(
                            static_cast<int>(1000000 * (time - trace_clock - wall_clock_elapse)));
                    this_thread::sleep_for(sleep_time);//wait a little bit
                }
            }
        }
        urlMutex.lock();
        urlQueue.push({to_string(id), length});
        urlMutex.unlock();
    }

    return 0;
}

void output() {
    int throughput_counter = 0;
    int throughput_interval = 1000;  //1000ms
    int latency_interval = 100;
    long tmpr=0, tmpb=0;
    while (running) {
        chrono::high_resolution_clock::time_point start = chrono::high_resolution_clock::now();
        this_thread::sleep_for(chrono::milliseconds(latency_interval));
        if (!((throughput_counter++)%(throughput_interval/latency_interval))) {
            tmpr = reqs.load();
            tmpb = bytes.load();
            reqs.store(0);
            bytes.store(0);
        }
        const double tmpl = latency.load();
        chrono::high_resolution_clock::time_point end = chrono::high_resolution_clock::now();
        const long timeElapsed = chrono::duration_cast<chrono::milliseconds>(end - start).count();
        outTp << tmpr << " " << tmpb << " " << tmpl << " " << timeElapsed << endl;
        cout << tmpr << " " << tmpb << " " << tmpl << " " << timeElapsed << endl;
    }
}

void signalHandler(int signum) {
    cerr << "Interrupt signal (" << signum << ") received.\n";
    is_stop = true;
}

int main(int argc, char *argv[]) {

    // parameters
    if (argc != 7) {
        cerr << "usage: path noThreads cacheIP throughput_log latency_log if_real_time" << endl;
        return 1;
    }
    path = argv[1];
    const int numberOfThreads = atoi(argv[2]);
    cacheip = argv[3];
    string latency_filename = argv[5];
    is_stop = false;
    signal(SIGTERM, signalHandler);
    signal(SIGINT, signalHandler);


    bytes.store(0);
    reqs.store(0);

    outTp.open(argv[4]);
    real_time = static_cast<bool>(stoull(argv[6]));

    // init curl
    curl_global_init(CURL_GLOBAL_ALL);

    //perform measurements in different threads, save time stamps (global - rough, local - exact)
    cerr << "Starting threads" << endl;
    queueFull = false;
    ::running = true;
    thread threads[numberOfThreads];
    thread outputth = thread(output);
    //starting threads
    for (int i = 0; i < numberOfThreads; i++) {
        threads[i] = thread(measureThread);
    }
    ofstream outHist(latency_filename);
    if (!outHist) {
        cerr << "Error: cannot opening file " << latency_filename << endl;
        abort();
    }
    // start creating queue
    chrono::high_resolution_clock::time_point ostart = chrono::high_resolution_clock::now();
    requestCreate();
    queueFull = true;
    cerr << "Finished queue\n";
    for (int i = 0; i < numberOfThreads; i++) {
        threads[i].join();
    }
    chrono::high_resolution_clock::time_point oend = chrono::high_resolution_clock::now();
    long otimeElapsed = chrono::duration_cast<chrono::milliseconds>(oend - ostart).count();
    ::running = false;
    cerr << "Duration: " << otimeElapsed << endl;
    outputth.join();
    cerr << "Finished threads\n";
    curl_global_cleanup();

    for (auto it: e2e_latency_histData)
        outHist << "e2e " << it.first << " " << it.second << endl;
    for (auto it: fb_latency_histData)
        outHist << "fb " << it.first << " " << it.second << endl;
    outHist.close();
    cerr << "Finished logging latency\n";


    return 0;
}
