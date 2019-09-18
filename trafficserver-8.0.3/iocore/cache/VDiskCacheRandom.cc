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
#include <mutex>
#include <thread>
#include <queue>
#include <shared_mutex>

namespace Random {
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


class RandomMeta {
public:
    uint64_t _key;
    int64_t _size;
    uint8_t _past_distance_idx;
    uint64_t _past_timestamp;
    std::vector<uint64_t> _past_distances;
//    vector<uint64_t> _extra_features;
    std::vector<double > _edwt;

    RandomMeta(
            const uint64_t & key,
            const int64_t & size,
            const uint64_t & past_timestamp
//            const vector<uint64_t> & extra_features
            ) {
        _key = key;
        _size = size;
        _past_timestamp = past_timestamp;
        _past_distances = std::vector<uint64_t >(Random::max_n_past_distances);
        _past_distance_idx = (uint8_t) 0;
//        _extra_features = extra_features;
        _edwt = std::vector<double >(Random::n_edwt_feature, 1);
    }


    inline void update(const uint64_t &past_timestamp) {
        //distance
        uint64_t _distance = past_timestamp - _past_timestamp;
        _past_distances[_past_distance_idx%Random::max_n_past_distances] = _distance;
        _past_distance_idx = _past_distance_idx + (uint8_t) 1;
        if (_past_distance_idx >= Random::max_n_past_distances * 2)
            _past_distance_idx -= Random::max_n_past_distances;
        //timestamp
        _past_timestamp = past_timestamp;
        for (uint8_t i = 0; i < Random::n_edwt_feature; ++i) {
            uint32_t _distance_idx = std::min(uint32_t (_distance/Random::edwt_windows[i]), Random::max_hash_edwt_idx);
            _edwt[i] = _edwt[i] * Random::hash_edwt[_distance_idx] + 1;
        }
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

class VDiskCacheRandom: public VDiskCache{
public:
    //key -> (0/1 list, idx)
    std::unordered_map<uint64_t, KeyMapEntryT> key_map;
    //size map is exposed to get/put function
    std::unordered_map<uint64_t, uint32_t> size_map;
    std::shared_mutex size_map_mutex;

    std::vector<RandomMeta> meta_holder[2];

    std::vector<uint64_t> forget_table;

    // sample_size
    uint sample_rate = 64;
    //mutex guarantee the concurrency control, so counter doesn't need to be atomic
    uint64_t t_counter = 0;

    //op queue
    std::queue<OpT> op_queue;
    std::mutex op_queue_mutex;

    std::thread lookup_get_thread;
    std::thread print_status_thread;

    std::default_random_engine _generator = std::default_random_engine();
    std::uniform_int_distribution<std::size_t> _distribution = std::uniform_int_distribution<std::size_t>();

    void print_stats() {
        op_queue_mutex.lock();
        std::cerr << "op queue length: "<<op_queue.size()<<std::endl;
        op_queue_mutex.unlock();
        std::cerr << "cache size: "<<_currentSize<<"/"<<_cacheSize<<std::endl;
        std::cerr << "n_metadata: "<<key_map.size()<<std::endl;
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

    virtual void init(int64_t memory_window, int64_t max_bytes) {
        VDiskCache::init(memory_window, max_bytes);
//        VDiskCache::init(1000000000);
        Random::s_forget_table = Random::forget_window+1;
        forget_table.resize(Random::s_forget_table);
        Random::max_n_past_distances = Random::max_n_past_timestamps-1;
        //init
        Random::edwt_windows = std::vector<uint32_t >(Random::n_edwt_feature);
        for (uint8_t i = 0; i < Random::n_edwt_feature; ++i) {
            Random::edwt_windows[i] = pow(2, Random::base_edwt_window+i);
        }
        Random::max_hash_edwt_idx = (uint64_t) (Random::forget_window/pow(2, Random::base_edwt_window))-1;
        Random::hash_edwt = std::vector<double >(Random::max_hash_edwt_idx+1);
        for (int i = 0; i < Random::hash_edwt.size(); ++i)
            Random::hash_edwt[i] = pow(0.5, i);

        //interval, distances, size, extra_features, n_past_intervals, edwt
        Random::n_feature = Random::max_n_past_timestamps + 2 + Random::n_edwt_feature;
//        training_thread = std::thread(&VDiskCacheRandom::async_training, this);
        print_status_thread = std::thread(&VDiskCacheRandom::async_print_status, this);
        lookup_get_thread = std::thread(&VDiskCacheRandom::async_lookup_get, this);
    }


    void forget(uint64_t &t) {
        //remove item from forget table, which is not going to be affect from update
        auto & _forget_key = forget_table[t%Random::s_forget_table];
        if (_forget_key) {
            auto key = _forget_key - 1;
            auto meta_it = key_map.find(key);
            auto pos = meta_it->second.list_pos;
            bool meta_id = meta_it->second.list_idx;
            auto &meta = meta_holder[meta_id][pos];

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
            forget_table[t%Random::s_forget_table] = 0;
        }
    }

std::pair<uint64_t, uint32_t> rank(const uint64_t & t) {
    //if not trained yet, use random
    uint32_t rand_idx = _distribution(_generator) % meta_holder[0].size();
    return {meta_holder[0][rand_idx]._key, rand_idx};
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
            assert(!(forget_table[(t_counter + Random::forget_window)%Random::s_forget_table]));
            forget_table[(t_counter + Random::forget_window) % Random::s_forget_table] = key + 1;
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
//            if (!(t_counter % training_sample_interval))
//                sample(t_counter);
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
            RandomMeta &meta = meta_holder[key_idx.list_idx][key_idx.list_pos];
            assert(meta._key == key);
            uint64_t last_timestamp = meta._past_timestamp;
            uint64_t forget_timestamp = last_timestamp + Random::forget_window;
            //if the key in key_map, it must also in forget table
            auto &forget_key = forget_table[forget_timestamp % Random::s_forget_table];
            //key never 0 because we want to use forget table 0 means None
            assert(forget_key);
//            auto time_begin1 = std::chrono::system_clock::now();
            //re-request
//            auto time_begin2 = std::chrono::system_clock::now();
            //remove this entry
            forget_table[forget_timestamp%Random::s_forget_table] = 0;
            forget_table[(t_counter+Random::forget_window)%Random::s_forget_table] = key+1;

            //make this update after update training, otherwise the last timestamp will change
            meta.update(t_counter);
            //update forget_table

            forget(t_counter);
//            auto time_begin3 = std::chrono::system_clock::now();
            //sampling
//            if (!(t_counter % training_sample_interval))
//                sample(t_counter);
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

VDiskCache * new_VdiskCacheRandom() {
    return new VDiskCacheRandom;
}