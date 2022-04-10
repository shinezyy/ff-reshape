//
// Created by zcq on 2022/2/11.
//

#ifndef GEM5_LVNATASKS_HH
#define GEM5_LVNATASKS_HH

#include "base/types.hh"

namespace LvNATasks {
    // 0~7 for low priv
    // 8~15 for high priv task
    // 16~31 are bypassIdx of 0~15
    enum JobId {
        MaxLowPrivId = 7,
        MaxCtxId = 7,
        QosIdStart = 8,
        NumId = 16,
        NumBuckets = 32,
        UnusedId = 1024,
    };
    const int NumJobs = 5;
    static inline uint32_t job2QosId(uint32_t job_id){
      return job_id + QosIdStart;
    }
}
#endif //GEM5_LVNATASKS_HH
