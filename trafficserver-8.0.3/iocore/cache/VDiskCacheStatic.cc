//
// Created by zhenyus on 4/3/19.
//

#include "P_VDiskCache.h"
#include <atomic>

class VDiskCacheStatic: public VDiskCache{
public:

    void admit(const CacheKey * _key, const int64_t & size) override {
//        static std::atomic<int> concurrent_counter = {0};
//        static std::atomic<uint64_t > counter = {0};
//        printf("#admit: %d\n", ++counter);
//        int n_concurrent = ++concurrent_counter;
//        usleep(1000000);
//        printf("admit concurrency counter: %d\n", n_concurrent);
//        --concurrent_counter;
    }

    uint64_t lookup(const CacheKey * _key) override {
//        static std::atomic<int> concurrent_counter = {0};
//        static std::atomic<uint64_t > counter = {0};
//        printf("#lookup: %d\n", ++counter);
//        int n_concurrent = ++concurrent_counter;
//        usleep(1000000);
//        printf("lookup concurrency counter: %d\n", n_concurrent);
//        --concurrent_counter;
        //give it 33% miss rate, according to GBDT hit rate from wiki
        if (!(_key->b[0]%3))
            return 0;
        //average size is 33k
        return _key->b[0] & 66000ull;
//        return 33000;
//        return 4000;
        //random policy
        static std::atomic<int> counter = 0;
        if (!(counter++%3))
            return 0;
        return 33000;
    }
};

VDiskCache * new_VdiskCacheStatic() {
    return new VDiskCacheStatic;
}
