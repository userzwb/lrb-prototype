/** @file

  A brief file description

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#pragma once

#include "I_Cache.h"
#include <unordered_map>
// Generic Ram Cache interface

struct RamCache {
  // returns 1 on found/stored, 0 on not found/stored, if provided auxkey1 and auxkey2 must match
  virtual int get(CryptoHash *key, Ptr<IOBufferData> *ret_data, uint32_t auxkey1 = 0, uint32_t auxkey2 = 0) = 0;
  virtual int put(CryptoHash *key, IOBufferData *data, uint32_t len, bool copy = false, uint32_t auxkey1 = 0,
                  uint32_t auxkey2 = 0)                                                                     = 0;
  virtual int fixup(const CryptoHash *key, uint32_t old_auxkey1, uint32_t old_auxkey2, uint32_t new_auxkey1,
                    uint32_t new_auxkey2)                                                                   = 0;
  virtual int64_t size() const                                                                              = 0;

  virtual void init(int64_t max_bytes, Vol *vol) = 0;
  virtual ~RamCache(){};
};

RamCache *new_RamCacheLRU();
RamCache *new_RamCacheCLFUS();

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

class GDBTCache{
public:
    //key -> (0/1 list, idx)
    std::unordered_map <CacheKey, uint32_t> key_map;
    std::vector<GDBTMeta> meta_holder;
    void admit(const CacheKey * _key, const int64_t & size) {
        auto &key = *_key;
        auto it = key_map.find(key);
        if (it == key_map.end()) {
            //fresh insert
            key_map.insert({key, meta_holder.size()});
            meta_holder.emplace_back(key, size);
        } else {
            auto & pos = it->second;
            auto & meta = meta_holder[pos];
            assert(meta._size == size);
            assert(meta._key == key);
        }
    }

    void fetch(const CacheKey * _key) {
        auto &key = *_key;
        auto it = key_map.find(key);
        if (it != key_map.end()) {
            auto & pos = it->second;
            meta_holder[pos]._fetched = true;
        }
    }

    bool lookup(const CacheKey * _key) {
        bool ret;
        auto &key = *_key;

        //first update the metadata: insert/update, which can trigger pending data.mature
        auto it = key_map.find(key);
        if (it != key_map.end()) {
            auto & pos = it->second;
            ret = meta_holder[pos]._fetched;
        } else {
            ret = false;
        }
        return ret;
    }
};
