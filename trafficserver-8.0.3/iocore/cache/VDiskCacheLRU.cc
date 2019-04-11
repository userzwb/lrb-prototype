//
// Created by zhenyus on 4/3/19.
//

#include "P_VDiskCache.h"

typedef std::list<uint64_t >::iterator ListIteratorType;
typedef std::unordered_map<uint64_t , ListIteratorType> lruCacheMapType;

class VDiskCacheLRU: public VDiskCache{
public:
    // list for recency order
    std::list<uint64_t > _cacheList;
    // map to find objects in list
    lruCacheMapType _cacheMap;
    std::unordered_map<uint64_t , int64_t > _size_map;

    void admit(const CacheKey * _key, const int64_t & size) override {
        const uint64_t & key = _key->b[0];

        // object feasible to store?
        if (size > _cacheSize) {
            return;
        }
        // check eviction needed
        while (_currentSize + size > _cacheSize) {
            evict();
        }
        // admit new object
        _cacheList.push_front(key);
        _cacheMap[key] = _cacheList.begin();
        _currentSize += size;
        _size_map[key] = size;
    }

    void evict() {
        // evict least popular (i.e. last element)
        if (_cacheList.size() > 0) {
            ListIteratorType lit = _cacheList.end();
            lit--;
            uint64_t obj = *lit;
            auto & size = _size_map[obj];
            _currentSize -= size;
            _size_map.erase(obj);
            _cacheMap.erase(obj);
            _cacheList.erase(lit);
        }
    }

    void hit(lruCacheMapType::const_iterator it, uint64_t size) {
        _cacheList.splice(_cacheList.begin(), _cacheList, it->second);
    }

    uint64_t lookup(const CacheKey * _key) override {
        const uint64_t & obj = _key->b[0];
        auto it = _cacheMap.find(obj);
        if (it != _cacheMap.end()) {
            // log hit
            auto & size = _size_map[obj];
//            LOG("h", 0, obj.id, obj.size);
            hit(it, size);
            return size;
        }
        return 0;
    }
};

VDiskCache * new_VdiskCacheLRU() {
    return new VDiskCacheLRU;
}
