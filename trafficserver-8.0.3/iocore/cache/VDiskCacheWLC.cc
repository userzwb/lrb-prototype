//
// Created by zhenyus on 4/3/19.
//

#include "P_VDiskCache.h"
#include <atomic>
#include <unordered_map>
#include <vector>
#include <string>
#include <chrono>
#include <random>
#include <assert.h>
#include <LightGBM/c_api.h>
#include <mutex>
#include <thread>
#include <queue>
#include <shared_mutex>
#include <list>
#include "sparsepp/spp.h"

using spp::sparse_hash_map;
typedef uint64_t WLCKey;

namespace WLC {
    uint8_t max_n_past_timestamps = 32;
    uint8_t max_n_past_distances = 31;
    uint8_t base_edc_window = 10;
    const uint8_t n_edc_feature = 10;
    std::vector<uint32_t > edc_windows;
    std::vector<double > hash_edc;
    uint32_t max_hash_edc_idx;
    uint32_t memory_window = 67108864;
//    uint32_t n_extra_fields = 0;
    uint32_t batch_size = 131072;
//    const uint max_n_extra_feature = 4;
    uint32_t n_feature;
//    unordered_map<uint64_t, uint32_t> future_timestamps;
}


struct WLCMetaExtra {
    //164 byte at most
    //not 1 hit wonder
    float _edc[10];
    std::vector<uint32_t> _past_distances;
    //the next index to put the distance
    uint8_t _past_distance_idx = 1;

    WLCMetaExtra(const uint32_t &distance) {
        _past_distances = std::vector<uint32_t>(1, distance);
        for (uint8_t i = 0; i < WLC::n_edc_feature; ++i) {
            uint32_t _distance_idx = std::min(uint32_t(distance / WLC::edc_windows[i]), WLC::max_hash_edc_idx);
            _edc[i] = WLC::hash_edc[_distance_idx] + 1;
        }
    }

    void update(const uint32_t &distance) {
        uint8_t distance_idx = _past_distance_idx % WLC::max_n_past_distances;
        if (_past_distances.size() < WLC::max_n_past_distances)
            _past_distances.emplace_back(distance);
        else
            _past_distances[distance_idx] = distance;
        assert(_past_distances.size() <= WLC::max_n_past_distances);
        _past_distance_idx = _past_distance_idx + (uint8_t) 1;
        if (_past_distance_idx >= WLC::max_n_past_distances * 2)
            _past_distance_idx -= WLC::max_n_past_distances;
        for (uint8_t i = 0; i < WLC::n_edc_feature; ++i) {
            uint32_t _distance_idx = std::min(uint32_t(distance / WLC::edc_windows[i]), WLC::max_hash_edc_idx);
            _edc[i] = _edc[i] * WLC::hash_edc[_distance_idx] + 1;
        }
    }
};

class WLCMeta {
public:
    //25 byte
    uint64_t _key;
    uint32_t _past_timestamp;
//    uint16_t _extra_features[WLC::max_n_extra_feature];
    uint32_t _size;
    WLCMetaExtra *_extra = nullptr;
    std::vector<uint32_t> _sample_times;
    //vector<uint64_t > _extra_features;

    WLCMeta(
            const uint64_t & key,
            const uint32_t & size,
            const uint32_t & past_timestamp
//            const vector<uint64_t> & extra_features
            ){
        _key = key;
        _size = size;
        _past_timestamp = past_timestamp;
        //_extra_features = extra_features;
    }

    virtual ~WLCMeta() = default;

    void emplace_sample(uint32_t & sample_t) {
        _sample_times.emplace_back(sample_t);
    }

    void free() {
        delete _extra;
    }

    void update(const uint32_t &past_timestamp) {
        //distance
        uint32_t _distance = past_timestamp - _past_timestamp;
        assert(_distance);
        if (!_extra) {
            _extra = new WLCMetaExtra(_distance);
        } else
            _extra->update(_distance);
        //timestamp
        _past_timestamp = past_timestamp;
    }

    uint64_t overhead() {
        uint64_t ret = sizeof(WLCMeta);
        if (_extra)
            ret += sizeof(WLCMetaExtra) - sizeof(_sample_times) + _extra->_past_distances.capacity() * sizeof(uint32_t);
        return ret;
    }
};


class InCacheMeta : public WLCMeta {
public:
    //pointer to lru0
    std::list<WLCKey>::const_iterator p_last_request;
    //any change to functions?

    InCacheMeta(const uint64_t &key,
                const uint64_t &size,
                const uint64_t &past_timestamp,
                const std::list<WLCKey>::const_iterator &it) :
            WLCMeta(key, size, past_timestamp) {
        p_last_request = it;
    };

    InCacheMeta(const WLCMeta &meta, const std::list<WLCKey>::const_iterator &it) : WLCMeta(meta) {
        p_last_request = it;
    };

};

class InCacheLRUQueue {
public:
    std::list<WLCKey> dq;

    //size?
    //the hashtable (location information is maintained outside, and assume it is always correct)
    std::list<WLCKey>::const_iterator request(WLCKey key) {
        dq.emplace_front(key);
        return dq.cbegin();
    }

    std::list<WLCKey>::const_iterator re_request(std::list<WLCKey>::const_iterator it) {
        if (it != dq.cbegin()) {
            dq.emplace_front(*it);
            dq.erase(it);
        }
        return dq.cbegin();
    }
};

class WLCTrainingData {
public:
    //overhead: 536 * batch size bytes
    std::vector<float> labels;
    std::vector<int32_t> indptr;
    std::vector<int32_t> indices;
    std::vector<double> data;

    WLCTrainingData() {
        labels.reserve(WLC::batch_size);
        indptr.reserve(WLC::batch_size + 1);
        indptr.emplace_back(0);
        indices.reserve(WLC::batch_size * WLC::n_feature);
        data.reserve(WLC::batch_size * WLC::n_feature);
    }

    void emplace_back(WLCMeta &meta, uint32_t &sample_timestamp, uint32_t &future_interval) {
        int32_t counter = indptr.back();

        indices.emplace_back(0);
        data.emplace_back(sample_timestamp - meta._past_timestamp);
        ++counter;

        uint32_t this_past_distance = 0;
        int j = 0;
        uint8_t n_within = 0;
        if (meta._extra) {
            for (; j < meta._extra->_past_distance_idx && j < WLC::max_n_past_distances; ++j) {
                uint8_t past_distance_idx = (meta._extra->_past_distance_idx - 1 - j) % WLC::max_n_past_distances;
                const uint32_t &past_distance = meta._extra->_past_distances[past_distance_idx];
                this_past_distance += past_distance;
                indices.emplace_back(j + 1);
                data.emplace_back(past_distance);
                if (this_past_distance < WLC::memory_window) {
                    ++n_within;
                }
            }
        }

        counter += j;

        indices.emplace_back(WLC::max_n_past_timestamps);
        data.push_back(meta._size);
        ++counter;

//        for (int k = 0; k < WLC::n_extra_fields; ++k) {
//            indices.push_back(WLC::max_n_past_timestamps + k + 1);
//            data.push_back(meta._extra_features[k]);
//        }
//        counter += WLC::n_extra_fields;

        indices.push_back(WLC::max_n_past_timestamps + 1);
        data.push_back(n_within);
        ++counter;

        if (meta._extra) {
            for (int k = 0; k < WLC::n_edc_feature; ++k) {
                indices.push_back(WLC::max_n_past_timestamps + 2 + k);
                uint32_t _distance_idx = std::min(
                        uint32_t(sample_timestamp - meta._past_timestamp) / WLC::edc_windows[k],
                        WLC::max_hash_edc_idx);
                data.push_back(meta._extra->_edc[k] * WLC::hash_edc[_distance_idx]);
            }
        } else {
            for (int k = 0; k < WLC::n_edc_feature; ++k) {
                indices.push_back(WLC::max_n_past_timestamps + 2 + k);
                uint32_t _distance_idx = std::min(
                        uint32_t(sample_timestamp - meta._past_timestamp) / WLC::edc_windows[k],
                        WLC::max_hash_edc_idx);
                data.push_back(WLC::hash_edc[_distance_idx]);
            }
        }

        counter += WLC::n_edc_feature;

        labels.push_back(log1p(future_interval));
        indptr.push_back(counter);
    }

    void clear() {
        labels.clear();
        indptr.resize(1);
        indices.clear();
        data.clear();
    }
};


struct KeyMapEntryT {
    unsigned int list_idx: 1;
    unsigned int list_pos: 31;
};

struct OpT {
    uint64_t key;
    //-1 means get command
    int64_t size;
};

class VDiskCacheWLC: public VDiskCache {
public:
    //key -> (0/1 list, idx)
    sparse_hash_map<uint64_t, KeyMapEntryT> key_map;
    //size map is exposed to get/put function
    sparse_hash_map<uint64_t, uint32_t> size_map;
    std::shared_mutex size_map_mutex;

//    std::vector<WLCMeta> meta_holder[2];
    std::vector<InCacheMeta> in_cache_metas;
    std::vector<WLCMeta> out_cache_metas;

    InCacheLRUQueue in_cache_lru_queue;
    //TODO: negative queue should have a better abstraction, at least hide the round-up
    sparse_hash_map<uint32_t, uint64_t> negative_candidate_queue;
    WLCTrainingData * training_data;
    WLCTrainingData * background_training_data;
    std::mutex training_data_mutex;

    // sample_size
    uint sample_rate = 64;
    uint64_t training_sample_interval = 64;
    unsigned int segment_window = 10000000;

    double training_loss = 0;

    //mutex guarantee the concurrency control, so counter doesn't need to be atomic
    uint32_t t_counter = 0;
    //op queue
    std::queue<OpT> op_queue;
    std::mutex op_queue_mutex;

    std::thread lookup_get_thread;
    std::thread training_thread;
    std::thread print_status_thread;

    BoosterHandle booster = nullptr;
    std::mutex booster_mutex;
    bool if_trained = false;

//    double inference_time = 0;
//    double training_time = 0;

    std::unordered_map<std::string, std::string> WLC_train_params = {
            {"boosting",                   "gbdt"},
            {"objective",                  "regression"},
            {"num_iterations",             "32"},
            {"num_leaves",                  "32"},
            {"num_threads",                "4"},
            {"feature_fraction",           "0.8"},
            {"bagging_freq",               "5"},
            {"bagging_fraction",           "0.8"},
            {"learning_rate",              "0.1"},
            {"verbosity",                  "0"},
    };

    std::unordered_map<std::string, std::string> WLC_inference_params;

//    enum ObjectiveT: uint8_t {byte_hit_rate=0, object_hit_rate=1};
//    ObjectiveT objective = byte_hit_rate;

    std::default_random_engine _generator = std::default_random_engine();
    std::uniform_int_distribution<std::size_t> _distribution = std::uniform_int_distribution<std::size_t>();

    void print_stats() {
        size_map_mutex.lock_shared();
        uint64_t len_size_map = size_map.size();
        size_map_mutex.unlock_shared();
        std::cerr << "\nop queue length: "<<op_queue.size()<<std::endl;
        std::cerr << "async size_map len: "<<len_size_map<<std::endl;
        std::cerr << "cache size: " << _currentSize << "/" << _cacheSize << " (" << ((double) _currentSize) / _cacheSize
                << ")" << std::endl
                << "in/out metadata " << in_cache_metas.size() << " / " << out_cache_metas.size() << std::endl;
//        std::cerr << "cache size: "<<_currentSize<<"/"<<_cacheSize<<std::endl;
//        std::cerr << "n_metadata: "<<key_map.size()<<std::endl;
        std::cerr << "n_training: "<<training_data->labels.size()<<std::endl;

//        std::cerr << "training loss: " << training_loss << std::endl;
//        std::cerr << "n_force_eviction: " << n_force_eviction <<std::endl;
//        std::cerr << "training time: " << training_time <<std::endl;
//        std::cerr << "inference time: " << inference_time <<std::endl;
    }

    void async_training() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (training_data->labels.size() >= WLC::batch_size) {
                //assume back ground training data is already clear
                training_data_mutex.lock();
                std::swap(training_data, background_training_data);
                training_data_mutex.unlock();
                train();
                background_training_data->clear();
            }
//            printf("async training\n");
        }
    }

    void async_print_status() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            print_stats();
        }
    }

    void async_lookup_get() {
        while (true) {
            op_queue_mutex.lock();
            if (!op_queue.empty()) {
                OpT op = op_queue.front();
                op_queue.pop();
                op_queue_mutex.unlock();
                if (op.size < 0) {
                    _lookup(op.key);
                } else {
                    _admit(op.key, op.size);
                }
            } else {
                op_queue_mutex.unlock();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }

    void init(int64_t memory_window, int64_t max_bytes) override {
        WLC::memory_window = memory_window;
        VDiskCache::init(memory_window, max_bytes);
//        VDiskCache::init(30000000000);
        negative_candidate_queue.reserve(WLC::memory_window);
        WLC::max_n_past_distances = WLC::max_n_past_timestamps - 1;
        //init
        WLC::edc_windows = std::vector<uint32_t>(WLC::n_edc_feature);
        for (uint8_t i = 0; i < WLC::n_edc_feature; ++i) {
            WLC::edc_windows[i] = pow(2, WLC::base_edc_window + i);
        }
        WLC::max_hash_edc_idx = (uint64_t) (WLC::memory_window / pow(2, WLC::base_edc_window)) - 1;
        WLC::hash_edc = std::vector<double>(WLC::max_hash_edc_idx + 1);
        for (int i = 0; i < WLC::hash_edc.size(); ++i)
            WLC::hash_edc[i] = pow(0.5, i);

        //interval, distances, size, extra_features, n_past_intervals, edwt
        WLC::n_feature = WLC::max_n_past_timestamps + 2 + WLC::n_edc_feature;
        training_data = new WLCTrainingData();
        background_training_data = new WLCTrainingData();
        WLC_inference_params = WLC_train_params;
        //TODO: don't believe inference need so large number of threads
//        WLC_inference_params["num_threads"] = "4";
        training_thread = std::thread(&VDiskCacheWLC::async_training, this);
        print_status_thread = std::thread(&VDiskCacheWLC::async_print_status, this);
        lookup_get_thread = std::thread(&VDiskCacheWLC::async_lookup_get, this);
    }

    void train() {
//        auto timeBegin = std::chrono::system_clock::now();
        // create training dataset
        DatasetHandle trainData;

        BoosterHandle background_booster = nullptr;

        LGBM_DatasetCreateFromCSR(
                static_cast<void *>(background_training_data->indptr.data()),
                C_API_DTYPE_INT32,
                background_training_data->indices.data(),
                static_cast<void *>(background_training_data->data.data()),
                C_API_DTYPE_FLOAT64,
                background_training_data->indptr.size(),
                background_training_data->data.size(),
                WLC::n_feature,  //remove future t
                WLC_train_params,
                nullptr,
                &trainData);

        LGBM_DatasetSetField(trainData,
                             "label",
                             static_cast<void *>(background_training_data->labels.data()),
                             background_training_data->labels.size(),
                             C_API_DTYPE_FLOAT32);

        // init booster
        LGBM_BoosterCreate(trainData, WLC_train_params, &background_booster);

        // train
        for (int i = 0; i < stoi(WLC_train_params["num_iterations"]); i++) {
            int isFinished;
            LGBM_BoosterUpdateOneIter(background_booster, &isFinished);
            if (isFinished) {
                break;
            }
        }

//        auto time1 = std::chrono::system_clock::now();

        //don't testing training in order to reduce model available latency
        booster_mutex.lock();
        std::swap(booster, background_booster);
        booster_mutex.unlock();
        if_trained = true;

//        int64_t len;
//        std::vector<double > result(background_training_data->indptr.size()-1);
//        LGBM_BoosterPredictForCSR(background_booster,
//                                  static_cast<void *>(background_training_data->indptr.data()),
//                                  C_API_DTYPE_INT32,
//                                  background_training_data->indices.data(),
//                                  static_cast<void *>(background_training_data->data.data()),
//                                  C_API_DTYPE_FLOAT64,
//                                  background_training_data->indptr.size(),
//                                  background_training_data->data.size(),
//                                  WLC::n_feature,  //remove future t
//                                  C_API_PREDICT_NORMAL,
//                                  0,
//                                  WLC_train_params,
//                                  &len,
//                                  result.data());
//        auto time2 = std::chrono::system_clock::now();

//        double se = 0;
//        for (int i = 0; i < result.size(); ++i) {
//            auto diff = result[i] - background_training_data->labels[i];
//            se += diff * diff;
//        }
//        training_loss = training_loss * 0.99 + se/WLC::batch_size*0.01;

        if (background_booster)
            LGBM_BoosterFree(background_booster);

        LGBM_DatasetFree(trainData);
//        training_time = 0.8*training_time + 0.2*std::chrono::duration_cast<std::chrono::microseconds>(time1 - timeBegin).count();
    }

void sample() {
    // warmup not finish
    if (in_cache_metas.empty() || out_cache_metas.empty())
        return;

    auto n_l0 = static_cast<uint32_t>(in_cache_metas.size());
    auto n_l1 = static_cast<uint32_t>(out_cache_metas.size());
    auto rand_idx = _distribution(_generator);
    // at least sample 1 from the list, at most size of the list
    auto n_sample_l0 = std::min(std::max(uint32_t (training_sample_interval*n_l0/(n_l0+n_l1)), (uint32_t) 1), n_l0);
    auto n_sample_l1 = std::min(std::max(uint32_t (training_sample_interval - n_sample_l0), (uint32_t) 1), n_l1);

    //sample list 0
    for (uint32_t i = 0; i < n_sample_l0; i++) {
        uint32_t pos = (uint32_t) (i + rand_idx) % n_l0;
        auto &meta = in_cache_metas[pos];
        meta.emplace_sample(t_counter);
    }

    //sample list 1
    for (uint32_t i = 0; i < n_sample_l1; i++) {
        uint32_t pos = (uint32_t) (i + rand_idx) % n_l1;
        auto &meta = out_cache_metas[pos];
        meta.emplace_sample(t_counter);
    }
}

void forget() {
    /*
     * forget happens exactly after the beginning of each time, without doing any other operations. For example, an
     * object is request at time 0 with memory window = 5, and will be forgotten exactly at the start of time 5.
     * */
    //remove item from forget table, which is not going to be affect from update
    auto it = negative_candidate_queue.find(t_counter % WLC::memory_window);
    if (it != negative_candidate_queue.end()) {
        auto forget_key = it->second;
        auto pos = key_map.find(forget_key)->second.list_pos;
        // Forget only happens at list 1
        assert(key_map.find(forget_key)->second.list_idx);
//        auto pos = meta_it->second.list_pos;
//        bool meta_id = meta_it->second.list_idx;
        auto &meta = out_cache_metas[pos];

        //timeout mature
        if (!meta._sample_times.empty()) {
            //mature
            uint32_t future_distance = WLC::memory_window * 2;
            training_data_mutex.lock();
            for (auto & sample_time: meta._sample_times) {
                //don't use label within the first forget window because the data is not static
                training_data->emplace_back(meta, sample_time, future_distance);
            }
            training_data_mutex.unlock();
            meta._sample_times.clear();
            meta._sample_times.shrink_to_fit();
        }

        assert(meta._key == forget_key);
        //free the actual content
        meta.free();
        //evict
        uint32_t tail_pos = out_cache_metas.size() - 1;
        if (pos != tail_pos) {
            //swap tail
            out_cache_metas[pos] = out_cache_metas[tail_pos];
            key_map.find(out_cache_metas[tail_pos]._key)->second.list_pos = pos;
        }
        out_cache_metas.pop_back();
        key_map.erase(forget_key);
        size_map_mutex.lock();
        size_map.erase(forget_key);
        size_map_mutex.unlock();
        negative_candidate_queue.erase(t_counter% WLC::memory_window);
    }
}

std::pair<uint64_t, uint32_t> rank() {
    //if not trained yet, or in_cache_lru past memory window, use LRU
    uint64_t &candidate_key = in_cache_lru_queue.dq.back();
    auto it = key_map.find(candidate_key);
    auto pos = it->second.list_pos;
    auto &meta = in_cache_metas[pos];
    if ((!if_trained) || (WLC::memory_window <= t_counter - meta._past_timestamp))
        return {meta._key, pos};

    uint32_t rand_idx = _distribution(_generator) % in_cache_metas.size();

    int32_t indptr[sample_rate + 1];
    indptr[0] = 0;
    int32_t indices[sample_rate * WLC::n_feature];
    double data[sample_rate * WLC::n_feature];
    int32_t past_timestamps[sample_rate];

    unsigned int idx_feature = 0;
    unsigned int idx_row = 0;
     for (int i = 0; i < sample_rate; i++) {
        uint32_t pos = (i + rand_idx) % in_cache_metas.size();
        auto &meta = in_cache_metas[pos];

//        ids[i] = meta._key;
        //fill in past_interval
        indices[idx_feature] = 0;
        data[idx_feature++] = t_counter - meta._past_timestamp;
        past_timestamps[idx_row] = meta._past_timestamp;

        uint8_t j = 0;
        uint32_t this_past_distance = 0;
        uint8_t n_within = 0;
        if (meta._extra) {
            for (j = 0; j < meta._extra->_past_distance_idx && j < WLC::max_n_past_distances; ++j) {
                uint8_t past_distance_idx = (meta._extra->_past_distance_idx - 1 - j) % WLC::max_n_past_distances;
                uint32_t &past_distance = meta._extra->_past_distances[past_distance_idx];
                this_past_distance += past_distance;
                indices[idx_feature] = j + 1;
                data[idx_feature++] = past_distance;
                if (this_past_distance < WLC::memory_window) {
                    ++n_within;
                }
//                } else
//                    break;
            }
        }

        indices[idx_feature] = WLC::max_n_past_timestamps;
        data[idx_feature++] = meta._size;
//        sizes[idx_row] = meta._size;

//        for (uint k = 0; k < WLC::n_extra_fields; ++k) {
//            indices[idx_feature] = WLC::max_n_past_timestamps + k + 1;
//            data[idx_feature++] = meta._extra_features[k];
//        }

        indices[idx_feature] = WLC::max_n_past_timestamps + 1;
        data[idx_feature++] = n_within;

        for (uint8_t k = 0; k < WLC::n_edc_feature; ++k) {
            indices[idx_feature] = WLC::max_n_past_timestamps + 2 + k;
            uint32_t _distance_idx = std::min(uint32_t(t_counter - meta._past_timestamp) / WLC::edc_windows[k],
                                         WLC::max_hash_edc_idx);
            if (meta._extra)
                data[idx_feature++] = meta._extra->_edc[k] * WLC::hash_edc[_distance_idx];
            else
                data[idx_feature++] = WLC::hash_edc[_distance_idx];
        }
        //remove future t
        indptr[++idx_row] = idx_feature;
    }
    int64_t len;
    std::vector<double> result(sample_rate);
//    auto time_begin = std::chrono::system_clock::now();
    booster_mutex.lock();
    LGBM_BoosterPredictForCSR(booster,
                              static_cast<void *>(indptr),
                              C_API_DTYPE_INT32,
                              indices,
                              static_cast<void *>(data),
                              C_API_DTYPE_FLOAT64,
                              idx_row + 1,
                              idx_feature,
                              WLC::n_feature,  //remove future t
                              C_API_PREDICT_NORMAL,
                              0,
                              WLC_inference_params,
                              &len,
                              result.data());
    booster_mutex.unlock();

//    auto time_end = std::chrono::system_clock::now();
//    inference_time = 0.99 * inference_time + 0.01 * std::chrono::duration_cast<std::chrono::microseconds>(time_end - time_begin).count();
//    for (int i = 0; i < sample_rate; ++i)
//        result[i] -= (t - past_timestamps[i]);
//    if (objective == object_hit_rate)
//        for (uint32_t i = 0; i < n_sample; ++i)
//            result[i] *= sizes[i];

    double worst_score;
    uint32_t worst_pos;
    uint32_t min_past_timestamp;

    for (int i = 0; i < sample_rate; ++i)
        if (!i || result[i] > worst_score || (result[i] == worst_score && (past_timestamps[i] < min_past_timestamp))) {
            worst_score = result[i];
            worst_pos = i;
            min_past_timestamp = past_timestamps[i];
        }

    worst_pos = (worst_pos + rand_idx) % in_cache_metas.size();
    auto &worst_key = in_cache_metas[worst_pos]._key;

    return {worst_key, worst_pos};
}

void evict() {
    auto epair = rank();
    uint64_t & key = epair.first;
    uint32_t & old_pos = epair.second;

    auto &meta = in_cache_metas[old_pos];
    if (WLC::memory_window <= t_counter - meta._past_timestamp) {
        //must be the tail of lru
        if (!meta._sample_times.empty()) {
            //mature
            uint32_t future_distance = t_counter - meta._past_timestamp + WLC::memory_window;
            training_data_mutex.lock();
            for (auto &sample_time: meta._sample_times) {
                //don't use label within the first forget window because the data is not static
                training_data->emplace_back(meta, sample_time, future_distance);
            }
            training_data_mutex.unlock();
            meta._sample_times.clear();
            meta._sample_times.shrink_to_fit();
        }


        in_cache_lru_queue.dq.erase(meta.p_last_request);
        meta.p_last_request = in_cache_lru_queue.dq.end();
        //above is suppose to be below, but to make sure the action is correct
//        in_cache_lru_queue.dq.pop_back();
        meta.free();
        _currentSize -= meta._size;
        key_map.erase(key);
        //remove from metas
        size_map_mutex.lock();
        size_map.erase(key);
        size_map_mutex.unlock();

        uint32_t activate_tail_idx = in_cache_metas.size() - 1;
        if (old_pos != activate_tail_idx) {
            //move tail
            in_cache_metas[old_pos] = in_cache_metas[activate_tail_idx];
            key_map.find(in_cache_metas[activate_tail_idx]._key)->second.list_pos = old_pos;
        }
        in_cache_metas.pop_back();

    } else {
        //bring list 0 to list 1
        in_cache_lru_queue.dq.erase(meta.p_last_request);
        meta.p_last_request = in_cache_lru_queue.dq.end();
        _currentSize -= meta._size;
        negative_candidate_queue.insert({(meta._past_timestamp + WLC::memory_window) % WLC::memory_window, meta._key});

        uint32_t new_pos = out_cache_metas.size();
        out_cache_metas.emplace_back(in_cache_metas[old_pos]);
        uint32_t activate_tail_idx = in_cache_metas.size() - 1;
        if (old_pos != activate_tail_idx) {
            //move tail
            in_cache_metas[old_pos] = in_cache_metas[activate_tail_idx];
            key_map.find(in_cache_metas[activate_tail_idx]._key)->second.list_pos = old_pos;
        }
        in_cache_metas.pop_back();
        key_map.find(key)->second = {1, new_pos};
        //in list 1, can still query
        size_map_mutex.lock();
        size_map.find(key)->second = 0;
        size_map_mutex.unlock();
    }
}


void admit(const CacheKey * _key, const int64_t & size) override {
    const uint64_t & key = _key->b[0];
    if (size > _cacheSize)
        return;
//        auto time_begin = std::chrono::system_clock::now();
    size_map_mutex.lock_shared();
    auto it = size_map.find(key);
    if (it == size_map.end() || !(it->second)) {
        size_map_mutex.unlock_shared();
        op_queue_mutex.lock();
        op_queue.push(OpT{.key=key, .size=size});
        op_queue_mutex.unlock();
    } else {
        //already inserted
        size_map_mutex.unlock_shared();
    }
//        auto time_end = std::chrono::system_clock::now();
//        auto time_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(time_end - time_begin).count();
//        printf("admit latency: %d\n", time_elapsed);
}

uint64_t lookup(const CacheKey * _key) override {
    uint64_t ret = 0;
    const uint64_t &key = _key->b[0];

    size_map_mutex.lock_shared();
    auto it = size_map.find(key);
    if (it != size_map.end()) {
        ret = it->second;
        size_map_mutex.unlock_shared();
        op_queue_mutex.lock();
        op_queue.push(OpT{.key=key, .size=-1});
        op_queue_mutex.unlock();
    } else {
        size_map_mutex.unlock_shared();
    }
    return ret;
}

void _admit(const uint64_t & key, const uint32_t size) {
    auto it = key_map.find(key);
    if (it == key_map.end()) {
        //fresh insert
        key_map.insert({key, KeyMapEntryT{.list_idx=0, .list_pos = (uint32_t) in_cache_metas.size()}});
        size_map_mutex.lock();
        size_map.insert({key, size});
        size_map_mutex.unlock();

        auto lru_it = in_cache_lru_queue.request(key);
        in_cache_metas.emplace_back(key, size, t_counter, lru_it);
        _currentSize += size;
        if (_currentSize <= _cacheSize)
            goto Lreturn;
    } else if (!it->second.list_idx) {
        //already in the cache
        goto Lnoop;
    } else {
        //bring list 1 to list 0
        //first move meta data, then modify hash table
        uint32_t tail0_pos = in_cache_metas.size();
        auto &meta = out_cache_metas[it->second.list_pos];
        auto forget_timestamp = meta._past_timestamp + WLC::memory_window;
        negative_candidate_queue.erase(forget_timestamp % WLC::memory_window);
        auto it_lru = in_cache_lru_queue.request(key);
        in_cache_metas.emplace_back(out_cache_metas[it->second.list_pos], it_lru);
        uint32_t tail1_pos = out_cache_metas.size() - 1;
        if (it->second.list_pos != tail1_pos) {
            //swap tail
            out_cache_metas[it->second.list_pos] = out_cache_metas[tail1_pos];
            key_map.find(out_cache_metas[tail1_pos]._key)->second.list_pos = it->second.list_pos;
        }
        out_cache_metas.pop_back();
        it->second = {0, tail0_pos};
        size_map_mutex.lock();
        size_map.find(key)->second = size;
        size_map_mutex.unlock();
        _currentSize += size;
    }
    // check more eviction needed?
    while (_currentSize > _cacheSize) {
        evict();
    }
    Lreturn:
        //sampling
        if (!(t_counter % training_sample_interval))
            sample();
        ++t_counter;
        forget();
    //no logical op is performed
    Lnoop:
        return;
}

void _lookup(const uint64_t & key) {
    //first update the metadata: insert/update, which can trigger pending data.mature
    auto it = key_map.find(key);
    if (it != key_map.end()) {
        auto list_idx = it->second.list_idx;
        auto list_pos = it->second.list_pos;
        WLCMeta &meta = list_idx ? out_cache_metas[list_pos] : in_cache_metas[list_pos];
        //update past timestamps
        assert(meta._key == key);
        uint32_t last_timestamp = meta._past_timestamp;
        uint32_t forget_timestamp = last_timestamp + WLC::memory_window;
        //if the key in out_metadata, it must also in forget table
        assert((!list_idx) ||
               (negative_candidate_queue.find(forget_timestamp % WLC::memory_window) !=
                negative_candidate_queue.end()));
        //re-request
        if (!meta._sample_times.empty()) {
            //mature
            training_data_mutex.lock();
            for (auto & sample_time: meta._sample_times) {
                //don't use label within the first forget window because the data is not static
                uint32_t future_distance = t_counter - sample_time;
                training_data->emplace_back(meta, sample_time, future_distance);
            }
            training_data_mutex.unlock();
            meta._sample_times.clear();
            meta._sample_times.shrink_to_fit();
        }
        //make this update after update training, otherwise the last timestamp will change
        meta.update(t_counter);
        if (list_idx) {
            negative_candidate_queue.erase(forget_timestamp % WLC::memory_window);
            negative_candidate_queue.insert({(t_counter + WLC::memory_window) % WLC::memory_window, key});
            assert(negative_candidate_queue.find((t_counter + WLC::memory_window) % WLC::memory_window) !=
                   negative_candidate_queue.end());
        } else {
            auto *p = dynamic_cast<InCacheMeta *>(&meta);
            p->p_last_request = in_cache_lru_queue.re_request(p->p_last_request);
        }
        //sampling
        if (!(t_counter % training_sample_interval))
            sample();
        ++t_counter;
        forget();
    } else {
        //logical time won't progress as no state change in our system
    }
}

};

VDiskCache * new_VdiskCacheWLC() {
    return new VDiskCacheWLC;
}