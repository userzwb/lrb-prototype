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
        if (!(_key->b[0]%10))
            return 0;
        return 2000;
    }
};

VDiskCache * new_VdiskCacheStatic() {
    return new VDiskCacheStatic;
}
