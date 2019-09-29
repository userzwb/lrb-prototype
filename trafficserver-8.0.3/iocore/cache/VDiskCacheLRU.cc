//
// Created by zhenyus on 4/3/19.
//

#include "P_VDiskCache.h"
#include <mutex>
#include <chrono>
#include <thread>
#include "sparsepp/spp.h"
using spp::sparse_hash_map;

typedef std::list<uint64_t >::iterator ListIteratorType;
//typedef std::unordered_map<uint64_t , ListIteratorType> lruCacheMapType;

//class BloomFilter {
//public:
//    uint8_t current_filter = 0;
//    std::unordered_set<uint64_t> *_filters;
//    //assume partioned into 4 SSD
//    static const size_t max_n_element = 10000000;
//
//    BloomFilter() {
//        _filters = new std::unordered_set<uint64_t>[2];
//        for (int i = 0; i < 2; ++i)
//            _filters[i].reserve(max_n_element);
//    }
//
//    inline bool exist(const uint64_t &key) {
//        return (_filters[0].count(key)) || (_filters[1].count(key));
//    }
//
//    inline bool exist_or_insert(const uint64_t &key) {
//        if (exist(key))
//            return true;
//        else
//            insert(key);
//        return false;
//    }
//
//    void insert(const uint64_t &key) {
//        if (_filters[current_filter].size() > max_n_element) {
//            //if accumulate more than 40 million, switch
//            if (!_filters[1 - current_filter].empty())
//                _filters[1 - current_filter].clear();
//            current_filter = 1 - current_filter;
//        }
//        _filters[current_filter].insert(key);
//    }
//};

class VDiskCacheLRU: public VDiskCache{
public:
    // list for recency order
    std::list<uint64_t > _cacheList;
    // map to find objects in list
//    lruCacheMapType _cacheMap;
    sparse_hash_map<uint64_t , int64_t > _size_map;
    std::mutex _mutex;
    std::atomic_uint64_t t_counter = {0};
//    BloomFilter filter;
    std::thread print_status_thread;

    void init(int64_t memory_window, int64_t max_bytes) override {
        VDiskCache::init(memory_window, max_bytes);
        print_status_thread = std::thread(&VDiskCacheLRU::async_print_status, this);

    }
    void print_stats() {
        std::cerr << "\ncache size: " << _currentSize << "/" << _cacheSize << " (" << ((double) _currentSize) / _cacheSize
                  << ")" << std::endl
                  << "n_metadata: " << _size_map.size() << std::endl;
    }


    void async_print_status() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            print_stats();
        }
    }

    void admit(const CacheKey * _key, const int64_t & size) override {
        _mutex.lock();
        const uint64_t & key = _key->b[0];

//        bool seen = filter.exist_or_insert(key);
//        if (!seen)
//            goto LDone;

        //already admitted
        if (_size_map.find(key) != _size_map.end())
            goto LDone;

        // object feasible to store?
        if (size > _cacheSize) {
            goto LDone;
        }
        // check eviction needed
        while (_currentSize + size > _cacheSize) {
            evict();
        }
        // admit new object
        _cacheList.push_front(key);
//        _cacheMap[key] = _cacheList.begin();
        _currentSize += size;
        _size_map[key] = size;

        LDone:
            _mutex.unlock();
    }

    void evict() {
        // evict least popular (i.e. last element)
        if (!_cacheList.empty()) {
            ListIteratorType lit = _cacheList.end();
            lit--;
            uint64_t obj = *lit;
            auto & size = _size_map[obj];
            _currentSize -= size;
            _size_map.erase(obj);
//            _cacheMap.erase(obj);
            _cacheList.erase(lit);
        }
    }

//    void hit(lruCacheMapType::const_iterator it, uint64_t size) {
//        _cacheList.splice(_cacheList.begin(), _cacheList, it->second);
//    }

    uint64_t lookup(const CacheKey * _key) override {
        _mutex.lock();
//        uint64_t t = t_counter++;
////        if (!(t%1000000)) {
//        }
        const uint64_t & obj = _key->b[0];
        auto it = _size_map.find(obj);
        uint64_t ret = 0;
        if (it != _size_map.end()) {
            // log hit
            auto & size = it->second;
//            LOG("h", 0, obj.id, obj.size);
//            hit(it, size);
            ret = size;
        }
        _mutex.unlock();
        return ret;
    }
};

VDiskCache * new_VdiskCacheLRU() {
    return new VDiskCacheLRU;
}
