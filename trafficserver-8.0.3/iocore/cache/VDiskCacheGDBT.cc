//
// Created by zhenyus on 4/3/19.
//

#include "P_VDiskCache.h"
#include <unordered_map>

class GDBTMeta {
public:
    CacheKey _key;
    int64_t _size;
//    uint8_t _past_distance_idx;
//    uint64_t _past_timestamp;
//    vector<uint64_t> _past_distances;
//    vector<uint64_t> _extra_features;
//    vector<double > _edwt;
//    vector<uint64_t> _sample_times;
    bool _fetched;

    GDBTMeta(
            const CacheKey & key,
            const int64_t & size
//            const uint64_t & past_timestamp,
//            const vector<uint64_t> & extra_features
            ) {
        _key = key;
        _size = size;
//        _past_timestamp = past_timestamp;
//        _past_distances = vector<uint64_t >(GDBT::max_n_past_distances);
//        _past_distance_idx = (uint8_t) 0;
//        _extra_features = extra_features;
//        _edwt = vector<double >(GDBT::n_edwt_feature, 1);
        _fetched = false;
    }

//
//    inline void update(const uint64_t &past_timestamp) {
//        //distance
//        uint64_t _distance = past_timestamp - _past_timestamp;
//        _past_distances[_past_distance_idx%GDBT::max_n_past_distances] = _distance;
//        _past_distance_idx = _past_distance_idx + (uint8_t) 1;
//        if (_past_distance_idx >= GDBT::max_n_past_distances * 2)
//            _past_distance_idx -= GDBT::max_n_past_distances;
//        //timestamp
//        _past_timestamp = past_timestamp;
//        for (uint8_t i = 0; i < GDBT::n_edwt_feature; ++i) {
//            uint32_t _distance_idx = min(uint32_t (_distance/GDBT::edwt_windows[i]), GDBT::max_hash_edwt_idx);
//            _edwt[i] = _edwt[i] * GDBT::hash_edwt[_distance_idx] + 1;
//        }
//    }
};

class VDiskCacheGDBT: public VDiskCache{
public:

    void admit(const CacheKey * _key, const int64_t & size) override {
    }

    void evict() override {
    }

    void fetch(const CacheKey * _key) override {
    }

    uint64_t lookup(const CacheKey * _key) override {
        return 0;
    }


};

VDiskCache * new_VdiskCacheGDBT() {
    return new VDiskCacheGDBT;
}