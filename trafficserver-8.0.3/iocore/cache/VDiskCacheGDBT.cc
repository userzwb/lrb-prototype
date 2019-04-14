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

namespace GDBT {
    uint8_t max_n_past_timestamps = 32;
    uint8_t max_n_past_distances = 31;
    uint8_t base_edwt_window = 10;
    uint8_t n_edwt_feature = 10;
    std::vector<uint32_t > edwt_windows;
    std::vector<double > hash_edwt;
    uint32_t max_hash_edwt_idx;
    uint64_t forget_window = 80000000;
    uint64_t s_forget_table = forget_window + 1;
//    uint64_t n_extra_fields = 0;
    uint64_t batch_size = 100000;
    uint64_t n_feature;
}


class GDBTMeta {
public:
    uint64_t _key;
    int64_t _size;
    uint8_t _past_distance_idx;
    uint64_t _past_timestamp;
    std::vector<uint64_t> _past_distances;
//    vector<uint64_t> _extra_features;
    std::vector<double > _edwt;
    std::vector<uint64_t> _sample_times;

    GDBTMeta(
            const uint64_t & key,
            const int64_t & size,
            const uint64_t & past_timestamp
//            const vector<uint64_t> & extra_features
            ) {
        _key = key;
        _size = size;
        _past_timestamp = past_timestamp;
        _past_distances = std::vector<uint64_t >(GDBT::max_n_past_distances);
        _past_distance_idx = (uint8_t) 0;
//        _extra_features = extra_features;
        _edwt = std::vector<double >(GDBT::n_edwt_feature, 1);
    }


    inline void update(const uint64_t &past_timestamp) {
        //distance
        uint64_t _distance = past_timestamp - _past_timestamp;
        _past_distances[_past_distance_idx%GDBT::max_n_past_distances] = _distance;
        _past_distance_idx = _past_distance_idx + (uint8_t) 1;
        if (_past_distance_idx >= GDBT::max_n_past_distances * 2)
            _past_distance_idx -= GDBT::max_n_past_distances;
        //timestamp
        _past_timestamp = past_timestamp;
        for (uint8_t i = 0; i < GDBT::n_edwt_feature; ++i) {
            uint32_t _distance_idx = std::min(uint32_t (_distance/GDBT::edwt_windows[i]), GDBT::max_hash_edwt_idx);
            _edwt[i] = _edwt[i] * GDBT::hash_edwt[_distance_idx] + 1;
        }
    }
};

class GDBTTrainingData {
public:
    //overhead: 536 * batch size bytes
    std::vector<float> labels;
    std::vector<int32_t> indptr;
    std::vector<int32_t> indices;
    std::vector<double> data;
    GDBTTrainingData() {
        labels.reserve(GDBT::batch_size);
        indptr.reserve(GDBT::batch_size+1);
        indptr.emplace_back(0);
        indices.reserve(GDBT::batch_size*GDBT::n_feature);
        data.reserve(GDBT::batch_size*GDBT::n_feature);
    }

    void emplace_back(const GDBTMeta &meta, uint64_t & sample_timestamp, uint64_t & future_interval) {
        int32_t counter = indptr.back();

        indices.emplace_back(0);
        data.emplace_back(sample_timestamp-meta._past_timestamp);
        ++counter;

        uint64_t this_past_distance = 0;
        int j = 0;
        for (; j < meta._past_distance_idx && j < GDBT::max_n_past_distances; ++j) {
            uint8_t past_distance_idx = (meta._past_distance_idx - 1 - j) % GDBT::max_n_past_distances;
            const uint64_t & past_distance = meta._past_distances[past_distance_idx];
            this_past_distance += past_distance;
            if (this_past_distance < GDBT::forget_window) {
                indices.emplace_back(j+1);
                data.emplace_back(past_distance);
            } else
                break;
        }
        counter += j;

        indices.emplace_back(GDBT::max_n_past_timestamps);
        data.push_back(meta._size);
        ++counter;

//        for (int k = 0; k < GDBT::n_extra_fields; ++k) {
//            indices.push_back(GDBT::max_n_past_timestamps + k + 1);
//            data.push_back(meta._extra_features[k]);
//        }
//        counter += GDBT::n_extra_fields;

        indices.push_back(GDBT::max_n_past_timestamps+1);
        data.push_back(j);
        ++counter;

        for (int k = 0; k < GDBT::n_edwt_feature; ++k) {
            indices.push_back(GDBT::max_n_past_timestamps + 2 + k);
            uint32_t _distance_idx = std::min(uint32_t (sample_timestamp-meta._past_timestamp) / GDBT::edwt_windows[k],
                                         GDBT::max_hash_edwt_idx);
            data.push_back(meta._edwt[k]*GDBT::hash_edwt[_distance_idx]);
        }
        counter += GDBT::n_edwt_feature;

        labels.push_back(future_interval);
        indptr.push_back(counter);
    }

    void clear() {
        labels.clear();
        indptr.clear();
        indptr.emplace_back(0);
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

class VDiskCacheGDBT: public VDiskCache{
public:
    //key -> (0/1 list, idx)
    std::unordered_map<uint64_t, KeyMapEntryT> key_map;
    //size map is exposed to get/put function
    std::unordered_map<uint64_t, uint32_t> size_map;
    std::shared_mutex size_map_mutex;

    std::vector<GDBTMeta> meta_holder[2];

    std::vector<uint64_t> forget_table;
    GDBTTrainingData * training_data;
    GDBTTrainingData * background_training_data;
    std::mutex training_data_mutex;

    // sample_size
    uint sample_rate = 64;
    uint64_t training_sample_interval = 64;

    double training_loss = 0;
    uint64_t n_force_eviction = 0;

    //mutex guarantee the concurrency control, so counter doesn't need to be atomic
    uint64_t t_counter = 0;

    //op queue
    std::queue<OpT> op_queue;
    std::mutex op_queue_mutex;

    std::thread lookup_get_thread;
    std::thread training_thread;
    std::thread print_status_thread;

    BoosterHandle booster = nullptr;
    std::mutex booster_mutex;
    bool if_trained = false;

    std::unordered_map<std::string, std::string> GDBT_train_params = {
            {"boosting",                   "gbdt"},
            {"objective",                  "regression"},
            {"num_iterations",             "32"},
            {"num_leaves",                  "32"},
            {"num_threads",                "4"},
            {"shrinkage_rate",           "0.1"},
            {"feature_fraction",           "0.8"},
            {"bagging_freq",               "5"},
            {"bagging_fraction",           "0.8"},
            {"learning_rate",              "0.1"},
    };

    std::unordered_map<std::string, std::string> GDBT_inference_params;

    enum ObjectiveT: uint8_t {byte_hit_rate=0, object_hit_rate=1};
    ObjectiveT objective = byte_hit_rate;

    std::default_random_engine _generator = std::default_random_engine();
    std::uniform_int_distribution<std::size_t> _distribution = std::uniform_int_distribution<std::size_t>();

    void print_stats() {
        op_queue_mutex.lock();
        std::cerr << "op queue length: "<<op_queue.size()<<std::endl;
        op_queue_mutex.unlock();
        std::cerr << "cache size: "<<_currentSize<<"/"<<_cacheSize<<std::endl;
        std::cerr << "n_metadata: "<<key_map.size()<<std::endl;
        std::cerr << "n_training: "<<training_data->labels.size()<<std::endl;
        std::cerr << "training loss: " << training_loss << std::endl;
        std::cerr << "n_force_eviction: " << n_force_eviction <<std::endl;
    }

    void async_training() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (training_data->labels.size() >= GDBT::batch_size) {
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

    virtual void init(int64_t max_bytes) {
        VDiskCache::init(max_bytes);
//        VDiskCache::init(1000000000);
        GDBT::s_forget_table = GDBT::forget_window+1;
        forget_table.resize(GDBT::s_forget_table);
        GDBT::max_n_past_distances = GDBT::max_n_past_timestamps-1;
        //init
        GDBT::edwt_windows = std::vector<uint32_t >(GDBT::n_edwt_feature);
        for (uint8_t i = 0; i < GDBT::n_edwt_feature; ++i) {
            GDBT::edwt_windows[i] = pow(2, GDBT::base_edwt_window+i);
        }
        GDBT::max_hash_edwt_idx = (uint64_t) (GDBT::forget_window/pow(2, GDBT::base_edwt_window))-1;
        GDBT::hash_edwt = std::vector<double >(GDBT::max_hash_edwt_idx+1);
        for (int i = 0; i < GDBT::hash_edwt.size(); ++i)
            GDBT::hash_edwt[i] = pow(0.5, i);

        //interval, distances, size, extra_features, n_past_intervals, edwt
        GDBT::n_feature = GDBT::max_n_past_timestamps + 2 + GDBT::n_edwt_feature;
        training_data = new GDBTTrainingData();
        background_training_data = new GDBTTrainingData();
        GDBT_inference_params = GDBT_train_params;
        training_thread = std::thread(&VDiskCacheGDBT::async_training, this);
        print_status_thread = std::thread(&VDiskCacheGDBT::async_print_status, this);
        lookup_get_thread = std::thread(&VDiskCacheGDBT::async_lookup_get, this);
    }

    void train() {
        auto timeBegin = std::chrono::system_clock::now();
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
                GDBT::n_feature,  //remove future t
                GDBT_train_params,
                nullptr,
                &trainData);

        LGBM_DatasetSetField(trainData,
                             "label",
                             static_cast<void *>(background_training_data->labels.data()),
                             background_training_data->labels.size(),
                             C_API_DTYPE_FLOAT32);

        // init booster
        LGBM_BoosterCreate(trainData, GDBT_train_params, &background_booster);

        // train
        for (int i = 0; i < stoi(GDBT_train_params["num_iterations"]); i++) {
            int isFinished;
            LGBM_BoosterUpdateOneIter(background_booster, &isFinished);
            if (isFinished) {
                break;
            }
        }


        int64_t len;
        std::vector<double > result(background_training_data->indptr.size()-1);
        LGBM_BoosterPredictForCSR(background_booster,
                                  static_cast<void *>(background_training_data->indptr.data()),
                                  C_API_DTYPE_INT32,
                                  background_training_data->indices.data(),
                                  static_cast<void *>(background_training_data->data.data()),
                                  C_API_DTYPE_FLOAT64,
                                  background_training_data->indptr.size(),
                                  background_training_data->data.size(),
                                  GDBT::n_feature,  //remove future t
                                  C_API_PREDICT_NORMAL,
                                  0,
                                  GDBT_train_params,
                                  &len,
                                  result.data());
        double se = 0;
        for (int i = 0; i < result.size(); ++i) {
            auto diff = result[i] - background_training_data->labels[i];
            se += diff * diff;
        }
        training_loss = training_loss * 0.99 + se/GDBT::batch_size*0.01;


        //TODO: ideally this should be done before testing training
        booster_mutex.lock();
        std::swap(booster, background_booster);
        booster_mutex.unlock();
        if_trained = true;

        if (background_booster)
            LGBM_BoosterFree(booster);

        LGBM_DatasetFree(trainData);
        std::cerr << "Training time: "
             << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - timeBegin).count()
             << " ms"
             << std::endl;
    }

    void sample(uint64_t &t) {
        // warmup not finish
        if (meta_holder[0].empty() || meta_holder[1].empty())
            return;

        auto n_l0 = static_cast<uint32_t>(meta_holder[0].size());
        auto n_l1 = static_cast<uint32_t>(meta_holder[1].size());
        auto rand_idx = _distribution(_generator);
        // at least sample 1 from the list, at most size of the list
        auto n_sample_l0 = std::min(std::max(uint32_t (training_sample_interval*n_l0/(n_l0+n_l1)), (uint32_t) 1), n_l0);
        auto n_sample_l1 = std::min(std::max(uint32_t (training_sample_interval - n_sample_l0), (uint32_t) 1), n_l1);

        //sample list 0
        for (uint32_t i = 0; i < n_sample_l0; i++) {
            uint32_t pos = (uint32_t) (i + rand_idx) % n_l0;
            auto &meta = meta_holder[0][pos];
            meta._sample_times.emplace_back(t);
        }

        //sample list 1
        for (uint32_t i = 0; i < n_sample_l1; i++) {
            uint32_t pos = (uint32_t) (i + rand_idx) % n_l1;
            auto &meta = meta_holder[1][pos];
            meta._sample_times.emplace_back(t);
        }
    }

    void forget(uint64_t &t) {
        //remove item from forget table, which is not going to be affect from update
        auto & _forget_key = forget_table[t%GDBT::s_forget_table];
        if (_forget_key) {
            auto key = _forget_key - 1;
            auto meta_it = key_map.find(key);
            auto pos = meta_it->second.list_pos;
            bool meta_id = meta_it->second.list_idx;
            auto &meta = meta_holder[meta_id][pos];

            //timeout mature
            if (!meta._sample_times.empty()) {
                //mature
                uint64_t future_distance = GDBT::forget_window * 2;
                training_data_mutex.lock();
                for (auto & sample_time: meta._sample_times) {
                    //don't use label within the first forget window because the data is not static
                    training_data->emplace_back(meta, sample_time, future_distance);
                }
                training_data_mutex.unlock();
                meta._sample_times.clear();
            }

            ++n_force_eviction;
            assert(meta._key == key);
            if (!meta_id)
                _currentSize -= meta._size;
            //evict
            uint32_t tail_pos = meta_holder[meta_id].size() - 1;
            if (pos != tail_pos) {
                //swap tail
                meta_holder[meta_id][pos] = meta_holder[meta_id][tail_pos];
                key_map.find(meta_holder[meta_id][tail_pos]._key)->second.list_pos= pos;
            }
            meta_holder[meta_id].pop_back();
            key_map.erase(key);
            forget_table[t%GDBT::s_forget_table] = 0;
        }
    }

std::pair<uint64_t, uint32_t> rank(const uint64_t & t) {
    //if not trained yet, use random
    uint32_t rand_idx = _distribution(_generator) % meta_holder[0].size();
    if (!if_trained) {
        return {meta_holder[0][rand_idx]._key, rand_idx};
    }

    uint n_sample = std::min(sample_rate, (uint32_t) meta_holder[0].size());

    std::vector<int32_t> indptr = {0};
    std::vector<int32_t> indices;
    std::vector<double> data;
    std::vector<double> sizes;
    std::vector<uint64_t > past_timestamps;

    uint64_t counter = 0;
    for (int i = 0; i < n_sample; i++) {
        uint32_t pos = (i+rand_idx)%meta_holder[0].size();
        auto & meta = meta_holder[0][pos];
        //fill in past_interval
        indices.push_back(0);
        data.push_back(t - meta._past_timestamp);
        ++counter;
        past_timestamps.emplace_back(meta._past_timestamp);

        uint8_t j = 0;
        uint64_t this_past_distance = 0;
        for (j = 0; j < meta._past_distance_idx && j < GDBT::max_n_past_distances; ++j) {
            uint8_t past_distance_idx = (meta._past_distance_idx - 1 - j) % GDBT::max_n_past_distances;
            uint64_t & past_distance = meta._past_distances[past_distance_idx];
            this_past_distance += past_distance;
            if (this_past_distance < GDBT::forget_window) {
                indices.push_back(j+1);
                data.push_back(past_distance);
                ++counter;
            } else
                break;
        }

        indices.push_back(GDBT::max_n_past_timestamps);
        data.push_back(meta._size);
        sizes.push_back(meta._size);
        ++counter;


        indices.push_back(GDBT::max_n_past_timestamps+1);
        data.push_back(j);
        ++counter;

        for (uint8_t k = 0; k < GDBT::n_edwt_feature; ++k) {
            indices.push_back(GDBT::max_n_past_timestamps + 2 + k);
            uint32_t _distance_idx = std::min(uint32_t (t-meta._past_timestamp) / GDBT::edwt_windows[k],
                                         GDBT::max_hash_edwt_idx);
            data.push_back(meta._edwt[k]*GDBT::hash_edwt[_distance_idx]);
        }
        counter += GDBT::n_edwt_feature;

        //remove future t
        indptr.push_back(counter);

    }
    int64_t len;
    std::vector<double> result(n_sample);
    booster_mutex.lock();
    LGBM_BoosterPredictForCSR(booster,
                              static_cast<void *>(indptr.data()),
                              C_API_DTYPE_INT32,
                              indices.data(),
                              static_cast<void *>(data.data()),
                              C_API_DTYPE_FLOAT64,
                              indptr.size(),
                              data.size(),
                              GDBT::n_feature,  //remove future t
                              C_API_PREDICT_NORMAL,
                              0,
                              GDBT_inference_params,
                              &len,
                              result.data());
    booster_mutex.unlock();
    for (int i = 0; i < n_sample; ++i)
        result[i] -= (t - past_timestamps[i]);
    if (objective == object_hit_rate)
        for (uint32_t i = 0; i < n_sample; ++i)
            result[i] *= sizes[i];

    double worst_score;
    uint32_t worst_pos;
    uint64_t min_past_timestamp;

    for (int i = 0; i < n_sample; ++i)
        if (!i || result[i] > worst_score || (result[i] == worst_score && (past_timestamps[i] < min_past_timestamp))) {
            worst_score = result[i];
            worst_pos = i;
            min_past_timestamp = past_timestamps[i];
        }
    worst_pos = (worst_pos+rand_idx)%meta_holder[0].size();
    auto & meta = meta_holder[0][worst_pos];
    auto & worst_key = meta._key;

    return {worst_key, worst_pos};
}

    void evict(const uint64_t & t) {
        auto epair = rank(t);
        uint64_t & key = epair.first;
        uint32_t & old_pos = epair.second;

        size_map_mutex.lock();
        size_map.erase(key);
        size_map_mutex.unlock();

        //bring list 0 to list 1
        uint32_t new_pos = meta_holder[1].size();

        meta_holder[1].emplace_back(meta_holder[0][old_pos]);
        uint32_t activate_tail_idx = meta_holder[0].size()-1;
        if (old_pos !=  activate_tail_idx) {
            //move tail
            meta_holder[0][old_pos] = meta_holder[0][activate_tail_idx];
            key_map.find(meta_holder[0][activate_tail_idx]._key)->second.list_pos = old_pos;
        }
        meta_holder[0].pop_back();

        auto it = key_map.find(key);
        it->second.list_idx = 1;
        it->second.list_pos = new_pos;
        _currentSize -= meta_holder[1][new_pos]._size;
    }


    void admit(const CacheKey * _key, const int64_t & size) override {
        const uint64_t & key = _key->b[0];
        if (size > _cacheSize)
            return;
//        auto time_begin = std::chrono::system_clock::now();
        size_map_mutex.lock_shared();
        auto it = size_map.find(key);
        if (it == size_map.end()) {
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

    void _admit(const uint64_t & key, const int64_t & size) {
        uint64_t forget_key = key+1;
//        auto time_begin = std::chrono::system_clock::now();
//        long time_elapsed;
//        time_begin = std::chrono::system_clock::now();

        auto it = key_map.find(key);
        if (it == key_map.end()) {
            //fresh insert
            key_map.insert({key, KeyMapEntryT{.list_idx=0, .list_pos = (uint32_t) meta_holder[0].size()}});
            size_map_mutex.lock();
            size_map.insert({key, size});
            size_map_mutex.unlock();

            meta_holder[0].emplace_back(key, size, t_counter);
            _currentSize += size;
            //the thing here shouldn't be 0
            assert(!(forget_table[(t_counter + GDBT::forget_window)%GDBT::s_forget_table]));
            forget_table[(t_counter + GDBT::forget_window) % GDBT::s_forget_table] = key + 1;
            if (_currentSize <= _cacheSize)
                goto Lreturn;
        } else if (!it->second.list_idx) {
            //already in the cache
            goto Lnoop;
        } else if (size + _currentSize <= _cacheSize) {
            //bring list 1 to list 0
            //first modify list 0 hash table, move meta data, then modify hash table
            uint32_t tail0_pos = meta_holder[0].size();
            meta_holder[0].emplace_back(meta_holder[1][it->second.list_pos]);
            uint32_t tail1_pos = meta_holder[1].size() - 1;
            if (it->second.list_pos != tail1_pos) {
                //swap tail
                meta_holder[1][it->second.list_pos] = meta_holder[1][tail1_pos];
                key_map.find(meta_holder[1][tail1_pos]._key)->second.list_pos= it->second.list_pos;
            }
            meta_holder[1].pop_back();
            it->second = {0, tail0_pos};
            size_map_mutex.lock();
            size_map.insert({key, size});
            size_map_mutex.unlock();
            _currentSize += size;
            goto Lreturn;
        } else {
            //insert-evict
            auto epair = rank(t_counter);
            auto &key0 = epair.first;
            auto &pos0 = epair.second;
            size_map_mutex.lock();
            size_map.erase(key0);
            size_map.insert({key, size});
            size_map_mutex.unlock();
            _currentSize = _currentSize - meta_holder[0][pos0]._size + size;
            std::swap(meta_holder[0][pos0], meta_holder[1][it->second.list_pos]);
            std::swap(it->second, key_map.find(key0)->second);
        }
        // check more eviction needed?
        while (_currentSize > _cacheSize) {
            evict(t_counter);
        }
        Lreturn:
            forget(t_counter);
            //sampling
            if (!(t_counter % training_sample_interval))
                sample(t_counter);
            ++t_counter;
        //no logical op is performed
        Lnoop:
//            time_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - time_begin).count();
//            printf("admit latency: %d\n", time_elapsed);
            return;
    }

    void _lookup(const uint64_t & key) {
//        auto time_begin = std::chrono::system_clock::now();
        //first update the metadata: insert/update, which can trigger pending data.mature
        auto it = key_map.find(key);
        if (it != key_map.end()) {
            KeyMapEntryT key_idx = it->second;
            //update past timestamps
            GDBTMeta &meta = meta_holder[key_idx.list_idx][key_idx.list_pos];
            assert(meta._key == key);
            uint64_t last_timestamp = meta._past_timestamp;
            uint64_t forget_timestamp = last_timestamp + GDBT::forget_window;
            //if the key in key_map, it must also in forget table
            auto &forget_key = forget_table[forget_timestamp % GDBT::s_forget_table];
            //key never 0 because we want to use forget table 0 means None
            assert(forget_key);
//            auto time_begin1 = std::chrono::system_clock::now();
            //re-request
            if (!meta._sample_times.empty()) {
                //mature
                uint64_t future_distance = t_counter - last_timestamp;
                training_data_mutex.lock();
                for (auto & sample_time: meta._sample_times) {
                    //don't use label within the first forget window because the data is not static
                    training_data->emplace_back(meta, sample_time, future_distance);
                }
                training_data_mutex.unlock();
                meta._sample_times.clear();
            }
//            auto time_begin2 = std::chrono::system_clock::now();
            //remove this entry
            forget_table[forget_timestamp%GDBT::s_forget_table] = 0;
            forget_table[(t_counter+GDBT::forget_window)%GDBT::s_forget_table] = key+1;

            //make this update after update training, otherwise the last timestamp will change
            meta.update(t_counter);
            //update forget_table

            forget(t_counter);
//            auto time_begin3 = std::chrono::system_clock::now();
            //sampling
            if (!(t_counter % training_sample_interval))
                sample(t_counter);
            ++t_counter;
//            auto time_end = std::chrono::system_clock::now();
//            auto time_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(time_end - time_begin3).count();
//            auto time_elapsed1 = std::chrono::duration_cast<std::chrono::microseconds>(time_begin3 - time_begin2).count();
//            auto time_elapsed2 = std::chrono::duration_cast<std::chrono::microseconds>(time_begin2 - time_begin1).count();
//            auto time_elapsed3 = std::chrono::duration_cast<std::chrono::microseconds>(time_begin1 - time_begin).count();
//            printf("lookup latency: %d %d %d %d\n", time_elapsed3, time_elapsed2, time_elapsed1, time_elapsed);
        } else {
            //logical time won't progress as no state change in our system
        }
    }

};

VDiskCache * new_VdiskCacheGDBT() {
    return new VDiskCacheGDBT;
}