//
// Created by zhenyus on 4/3/19.
//

#include "P_VDiskCache.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <chrono>
#include <random>
#include <assert.h>
#include <LightGBM/c_api.h>

namespace GDBT {
    uint8_t max_n_past_timestamps = 32;
    uint8_t max_n_past_distances = 31;
    uint8_t base_edwt_window = 10;
    uint8_t n_edwt_feature = 10;
    std::vector<uint32_t > edwt_windows;
    std::vector<double > hash_edwt;
    uint32_t max_hash_edwt_idx;
    uint64_t forget_window = 10000000;
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
    bool _fetched;

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
        _fetched = false;
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

class VDiskCacheGDBT: public VDiskCache{
public:
    //key -> (0/1 list, idx)
    std::unordered_map<uint64_t, std::pair<bool, uint32_t>> key_map;
    std::vector<GDBTMeta> meta_holder[2];

    std::vector<uint64_t> forget_table;
    GDBTTrainingData training_data;

    // sample_size
    uint sample_rate = 32;
    uint64_t current_rank_pos = 0;
    uint64_t training_sample_interval = 1;

    double training_loss = 0;
    uint64_t n_force_eviction = 0;
    uint64_t t_counter = 0;

    BoosterHandle booster = nullptr;

    std::unordered_map<std::string, std::string> GDBT_train_params = {
            {"boosting",                   "gbdt"},
            {"objective",                  "regression"},
            {"num_iterations",             "1"},
            {"num_leaves",                  "32"},
            {"num_threads",                "1"},
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

    virtual void init(int64_t max_bytes) {
        VDiskCache::init(max_bytes);
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
        GDBT_inference_params = GDBT_train_params;
    }

    void train() {
        auto timeBegin = std::chrono::system_clock::now();
        if (booster)
            LGBM_BoosterFree(booster);
        // create training dataset
        DatasetHandle trainData;
        LGBM_DatasetCreateFromCSR(
                static_cast<void *>(training_data.indptr.data()),
                C_API_DTYPE_INT32,
                training_data.indices.data(),
                static_cast<void *>(training_data.data.data()),
                C_API_DTYPE_FLOAT64,
                training_data.indptr.size(),
                training_data.data.size(),
                GDBT::n_feature,  //remove future t
                GDBT_train_params,
                nullptr,
                &trainData);

        LGBM_DatasetSetField(trainData,
                             "label",
                             static_cast<void *>(training_data.labels.data()),
                             training_data.labels.size(),
                             C_API_DTYPE_FLOAT32);

        // init booster
        LGBM_BoosterCreate(trainData, GDBT_train_params, &booster);
        // train
        for (int i = 0; i < stoi(GDBT_train_params["num_iterations"]); i++) {
            int isFinished;
            LGBM_BoosterUpdateOneIter(booster, &isFinished);
            if (isFinished) {
                break;
            }
        }

        int64_t len;
        std::vector<double > result(training_data.indptr.size()-1);
        LGBM_BoosterPredictForCSR(booster,
                                  static_cast<void *>(training_data.indptr.data()),
                                  C_API_DTYPE_INT32,
                                  training_data.indices.data(),
                                  static_cast<void *>(training_data.data.data()),
                                  C_API_DTYPE_FLOAT64,
                                  training_data.indptr.size(),
                                  training_data.data.size(),
                                  GDBT::n_feature,  //remove future t
                                  C_API_PREDICT_NORMAL,
                                  0,
                                  GDBT_train_params,
                                  &len,
                                  result.data());
        double se = 0;
        for (int i = 0; i < result.size(); ++i) {
            auto diff = result[i] - training_data.labels[i];
            se += diff * diff;
        }
        training_loss = training_loss * 0.99 + se/GDBT::batch_size*0.01;

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
#ifdef LOG_SAMPLE_RATE
        bool log_flag = ((double) rand() / (RAND_MAX)) < LOG_SAMPLE_RATE;
#endif
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
            auto &pos = meta_it->second.second;
            bool &meta_id = meta_it->second.first;
            auto &meta = meta_holder[meta_id][pos];

            //timeout mature
            if (!meta._sample_times.empty()) {
                //mature
                uint64_t future_distance = GDBT::forget_window * 2;
                for (auto & sample_time: meta._sample_times) {
                    //don't use label within the first forget window because the data is not static
                    training_data.emplace_back(meta, sample_time, future_distance);
                    //training
                    if (training_data.labels.size() == GDBT::batch_size) {
                        train();
                        training_data.clear();
                    }
                }
                meta._sample_times.clear();
            }

            if (booster && !meta_id && t > GDBT::forget_window*1.5)
                ++n_force_eviction;
            assert(meta._key == key);
            if (!meta_id)
                _currentSize -= meta._size;
            //evict
            uint32_t tail_pos = meta_holder[meta_id].size() - 1;
            if (pos != tail_pos) {
                //swap tail
                meta_holder[meta_id][pos] = meta_holder[meta_id][tail_pos];
                key_map.find(meta_holder[meta_id][tail_pos]._key)->second.second = pos;
            }
            meta_holder[meta_id].pop_back();
            key_map.erase(key);
            forget_table[t%GDBT::s_forget_table] = 0;
        }
    }

    std::pair<uint64_t, uint32_t> rank(const uint64_t & t) {
        //if not trained yet, use random
        if (booster == nullptr) {
            uint32_t rand_idx = current_rank_pos % meta_holder[0].size();
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
            uint32_t pos = (i+current_rank_pos)%meta_holder[0].size();
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
        worst_pos = (worst_pos+current_rank_pos)%meta_holder[0].size();
        auto & meta = meta_holder[0][worst_pos];
        auto & worst_key = meta._key;
        current_rank_pos += n_sample;
        return {worst_key, worst_pos};
    }

    void evict(const uint64_t & t) {
        auto epair = rank(t);
        uint64_t & key = epair.first;
        uint32_t & old_pos = epair.second;

        //bring list 0 to list 1
        uint32_t new_pos = meta_holder[1].size();

        meta_holder[1].emplace_back(meta_holder[0][old_pos]);
        uint32_t activate_tail_idx = meta_holder[0].size()-1;
        if (old_pos !=  activate_tail_idx) {
            //move tail
            meta_holder[0][old_pos] = meta_holder[0][activate_tail_idx];
            key_map.find(meta_holder[0][activate_tail_idx]._key)->second.second = old_pos;
        }
        meta_holder[0].pop_back();

        auto it = key_map.find(key);
        it->second.first = 1;
        it->second.second = new_pos;
        _currentSize -= meta_holder[1][new_pos]._size;
    }

    void admit(const CacheKey * _key, const int64_t & size) override {
        uint64_t t = t_counter;
        const uint64_t & key = _key->b[0];
        // object feasible to store?
        if (size > _cacheSize) {
            return;
        }

        auto it = key_map.find(key);
        if (it == key_map.end()) {
            //fresh insert
            key_map.insert({key, {0, (uint32_t) meta_holder[0].size()}});
            meta_holder[0].emplace_back(key, size, t);
            _currentSize += size;
            forget_table[(t + GDBT::forget_window)%GDBT::s_forget_table] = key+1;
            if (_currentSize <= _cacheSize)
                return;
        } else if (size + _currentSize <= _cacheSize){
            //bring list 1 to list 0
            //first move meta data, then modify hash table
            uint32_t tail0_pos = meta_holder[0].size();
            meta_holder[0].emplace_back(meta_holder[1][it->second.second]);
            uint32_t tail1_pos = meta_holder[1].size()-1;
            if (it->second.second !=  tail1_pos) {
                //swap tail
                meta_holder[1][it->second.second] = meta_holder[1][tail1_pos];
                key_map.find(meta_holder[1][tail1_pos]._key)->second.second = it->second.second;
            }
            meta_holder[1].pop_back();
            it->second = {0, tail0_pos};
            _currentSize += size;
            return;
        } else {
            //insert-evict
            auto epair = rank(t);
            auto & key0 = epair.first;
            auto & pos0 = epair.second;
            auto & pos1 = it->second.second;
            _currentSize = _currentSize - meta_holder[0][pos0]._size + size;
            std::swap(meta_holder[0][pos0], meta_holder[1][pos1]);
            swap(it->second, key_map.find(key0)->second);
        }
        // check more eviction needed?
        while (_currentSize > _cacheSize) {
            evict(t);
        }
    }

    void fetch(const CacheKey * _key) override {
        const uint64_t &key = _key->b[0];
        auto it = key_map.find(key);
        if (it != key_map.end()) {
            auto & list_idx = it->second.first;
            auto & pos = it->second.second;
            meta_holder[list_idx][pos]._fetched = true;
        }
    }

    uint64_t lookup(const CacheKey * _key) override {
        bool ret;
        const uint64_t &key = _key->b[0];
        uint64_t t = t_counter;
        if (!(t%1000000)) {
            std::cerr << "cache size: "<<_currentSize<<"/"<<_cacheSize<<std::endl;
            std::cerr << "n_metadata: "<<key_map.size()<<std::endl;
            std::cerr << "n_training: "<<training_data.labels.size()<<std::endl;
            std::cerr << "training loss: " << training_loss << std::endl;
            std::cerr << "n_force_eviction: " << n_force_eviction <<std::endl;
        }


        //first update the metadata: insert/update, which can trigger pending data.mature
        auto it = key_map.find(key);
        if (it != key_map.end()) {
            //update past timestamps
            bool &list_idx = it->second.first;
            uint32_t &pos_idx = it->second.second;
            GDBTMeta &meta = meta_holder[list_idx][pos_idx];
            assert(meta._key == key);
            uint64_t last_timestamp = meta._past_timestamp;
            uint64_t forget_timestamp = last_timestamp + GDBT::forget_window;
            //if the key in key_map, it must also in forget table
            //todo: key never 0 because we want to use forget table 0 means None
            auto &forget_key = forget_table[forget_timestamp % GDBT::s_forget_table];
            assert(forget_key);
            //re-request
            if (!meta._sample_times.empty()) {
                //mature
                uint64_t future_distance = t - last_timestamp;
                for (auto & sample_time: meta._sample_times) {
                    //don't use label within the first forget window because the data is not static
                    training_data.emplace_back(meta, sample_time, future_distance);
                    //training
                    if (training_data.labels.size() == GDBT::batch_size) {
                        train();
                        training_data.clear();
                    }
                }
                meta._sample_times.clear();
            }
            //remove this entry
            forget_table[forget_timestamp%GDBT::s_forget_table] = 0;
            forget_table[(t+GDBT::forget_window)%GDBT::s_forget_table] = key+1;

            //make this update after update training, otherwise the last timestamp will change
            meta.update(t);
            //update forget_table
            ret = !list_idx;
        } else {
            ret = false;
        }

        forget(t);
        //sampling
        if (!(t % training_sample_interval))
            sample(t);
        ++t_counter;
        return ret;
    }


};

VDiskCache * new_VdiskCacheGDBT() {
    return new VDiskCacheGDBT;
}