#include <iostream>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <climits>
#include <algorithm>
#include <functional>
#include "fcgiapp.h"
#include <stdlib.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <sstream>


using namespace std;

// config
int numberOfThreads = 16;
int latency = 0;
//assume size <= 4GB
struct FeatureT {
    uint64_t size;
    vector<uint16_t> extra_features;
};

// read in map: object id-> object size
unordered_map<long, FeatureT> feature_map;
// block of data used as byte content of object
char dataBig[32768];
char dataMedium[2048];
char dataSmall[64];
std::atomic<long> bytes;
std::atomic<long> reqs;
volatile bool running;

int n_extra_features = 0;

void serverThread() {
    FCGX_Request request;
    FCGX_Init();
    FCGX_InitRequest(&request, 0, 0);

    while (FCGX_Accept_r(&request) == 0) {
        string uri(FCGX_GetParam("REQUEST_URI", request.envp));
        if (uri.length() > 1) {
            uri.erase(0, 1);
            const long id = atol(uri.c_str());
            auto it = feature_map.find(id);
            if (it != feature_map.end()) {
                if (latency)
                    this_thread::sleep_for(chrono::milliseconds(latency));
                const long csize = it->second.size;

                //logging
                reqs++;
                bytes += csize;

                // send along content length and type
                FCGX_FPrintF(request.out, "Content-length: %ld\r\n"
                                          "Content-type: application/octet-stream\r\n", csize);
                if (n_extra_features) {
                    FCGX_FPrintF(request.out, "X-extra-fields: ");
                    for (int i = 0; i < n_extra_features; ++i)
                        FCGX_FPrintF(request.out, "%04x", it->second.extra_features[i]);
                }
                FCGX_FPrintF(request.out, "\r\n\r\n");
                // write bytes 1) in large/medium/small blocks and 2) the remaining bytes individually
                ldiv_t divresultBig;
                divresultBig = div(csize, 32768L);
                for (long i = 0; i < divresultBig.quot; i++)
                    FCGX_PutStr(dataBig, 32768, request.out);
                ldiv_t divresultMedium;
                divresultMedium = div(divresultBig.rem, 2048L);
                for (long i = 0; i < divresultMedium.quot; i++)
                    FCGX_PutStr(dataMedium, 2048, request.out);
                ldiv_t divresultSmall;
                divresultSmall = div(divresultMedium.rem, 64L);
                for (long i = 0; i < divresultSmall.quot; i++)
                    FCGX_PutStr(dataSmall, 64, request.out);
                for (long i = 0; i < divresultSmall.rem; i++)
                    FCGX_PutChar(i, request.out);

            } else { // didn't find requested object id in osizes map
                FCGX_FPrintF(request.out, "Content-type: text/html\r\n"
                                          "\r\n"
                                          "<html>TestCache Server <br>\r\n IDofcontent not found!</html>\n");
                cerr << "no size: " << id << endl;
            }
        } else {// didn't get an id request
            FCGX_FPrintF(request.out, "Content-type: text/html\r\n"
                                      "\r\n"
                                      "<html>TestCache Server <br>\r\n use:IPofserver/IDofcontent</html>\n");
            cerr << "no id: " << endl;
        }
        FCGX_Finish_r(&request);
    }
}


void output() {
    while (running) {
        chrono::high_resolution_clock::time_point start = chrono::high_resolution_clock::now();
        reqs.store(0);
        bytes.store(0);
        this_thread::sleep_for(chrono::milliseconds(1000));
        const long tmpr = reqs.load();
        const long tmpb = bytes.load();
        chrono::high_resolution_clock::time_point end = chrono::high_resolution_clock::now();
        const long timeElapsed = chrono::duration_cast<chrono::milliseconds>(end - start).count();
        if (tmpr != 0L || tmpb != 0L)
            printf("%li %li %li\n", tmpr, tmpb, timeElapsed);
        fflush(stdout);
    }
}


int main(int argc, char *argv[]) {

    // parameters
    if (argc != 4) {
        cerr << "arguments: origin origin_db_path num_threads latency" << endl;
        abort();
    }

    numberOfThreads = stoll(argv[2]);
    latency = stoll(argv[3]);
    cerr<<"num_threads: "<<numberOfThreads<<endl;
    cerr<<"latency: "<<latency<<endl;

    bytes.store(0);
    reqs.store(0);

    // read in map of object id-> object size
    const char *path = argv[1];
    ifstream infile(path);
    if (!infile) {
        cerr << "Error: cannot opening file " << path << endl;
        exit(-1);
    }

    {
        //get whether file is in a correct format
        std::string line;
        getline(infile, line);
        istringstream iss(line);
        uint64_t tmp;
        int counter = 0;
        while (iss >> tmp) {
            ++counter;
        }
        infile.clear();
        infile.seekg(0, ios::beg);
        n_extra_features = counter - 2;
        cerr<<"n_extra_fields: "<<n_extra_features<<endl;
    }

    long id;
    uint64_t size;
    while (infile >> id >> size) {
        FeatureT f = {.size=size};
        uint16_t extra_feature;
        for (int i = 0; i < n_extra_features; ++i) {
            infile >> extra_feature;
            f.extra_features.emplace_back(extra_feature);
        }
        feature_map[id] = f;
    }


    // blocks of data used as byte content of object
    for (int i = 0; i < 32768; i++)
        dataBig[i] = (char) i;
    for (int i = 0; i < 2048; i++)
        dataMedium[i] = (char) i;
    for (int i = 0; i < 64; i++)
        dataSmall[i] = (char) i;

    ::running = true;
    thread outputth = thread(output);

    //start server threads
    thread threads[::numberOfThreads];
    for (int i = 0; i < numberOfThreads; i++) {
        threads[i] = thread(serverThread);
    }
    cerr << "started" << endl;


    for (int i = 0; i < numberOfThreads; i++) {
        threads[i].join();
    }

    ::running = false;
    outputth.join();

    return 0;
}
