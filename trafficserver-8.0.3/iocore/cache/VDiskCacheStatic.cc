//
// Created by zhenyus on 4/3/19.
//

#include "P_VDiskCache.h"

class VDiskCacheStatic: public VDiskCache{
public:

    void admit(const CacheKey * _key, const int64_t & size) override {
    }

    void fetch(const CacheKey * _key) override {
    }

    uint64_t lookup(const CacheKey * _key) override {
        //give it 25% miss rate, according to GBDT hit rate from wiki
        if (!(_key->b[0]%4))
            return 0;
        //average size is 129k
        return 129000;
    }
};

VDiskCache * new_VdiskCacheStatic() {
    return new VDiskCacheStatic;
}
