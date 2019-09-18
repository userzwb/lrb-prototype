//
// Created by zhenyus on 4/3/19.
//

#ifndef WEBTRACEREPLAY_P_VDISKCACHE_H
#define WEBTRACEREPLAY_P_VDISKCACHE_H

#include "I_Cache.h"
#include <unordered_map>
#include <list>

class VDiskCache{
public:
    // basic cache properties
    int64_t _cacheSize; // size of cache in bytes
    int64_t _currentSize; // total size of objects in cache in bytes

    //trace is use for set WLC memory window
    virtual void init(int64_t memory_window, int64_t max_bytes) {
        _cacheSize = max_bytes;
        _currentSize = 0;
    }

    virtual void admit(const CacheKey * _key, const int64_t & size) = 0;

    virtual uint64_t lookup(const CacheKey * _key) = 0;
};

VDiskCache *new_VdiskCacheLRU();
VDiskCache *new_VdiskCacheWLC();
VDiskCache *new_VdiskCacheStatic();
VDiskCache *new_VdiskCacheRandom();

#endif //WEBTRACEREPLAY_P_VDISKCACHE_H
