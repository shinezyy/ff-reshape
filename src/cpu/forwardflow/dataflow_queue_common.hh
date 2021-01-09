//
// Created by zyy on 2020/1/15.
//

#ifndef GEM5_DATAFLOW_QUEUE_COMMON_HH
#define GEM5_DATAFLOW_QUEUE_COMMON_HH

#include <cstdint>
#include <deque>
#include <random>
#include <unordered_map>
#include <vector>

#include "cpu/forwardflow/comm.hh"
#include "cpu/forwardflow/network.hh"
#include "cpu/timebuf.hh"
#include "fu_pool.hh"

struct DerivFFCPUParams;

namespace FF {
// common
struct DQCommon {
public:
    explicit DQCommon(DerivFFCPUParams *params);

    boost::dynamic_bitset<> uint2Bits(unsigned from);

    DQPointer uint2Pointer(unsigned u) const;

    unsigned pointer2uint(const BasePointer &ptr) const;

    // assuming that o is older than y
    unsigned computeDist(const BasePointer &y, const BasePointer &o);

    const int FPAddOps[3]{OpClass::FloatAdd,
                          OpClass::FloatCmp,
                          OpClass::FloatCvt};

    const int MultDivOps[7]{OpClass::IntMult,
                            OpClass::IntDiv,

                            OpClass::FloatMult,
                            OpClass::FloatMultAcc,

                            OpClass::FloatMisc,

                            OpClass::FloatDiv,
                            OpClass::FloatSqrt};

    const DQPointer nullDQPointer;
    const WKPointer nullWKPointer;

    const unsigned bankSize;

    const unsigned nBanks;
    const unsigned groupSize;

    const unsigned nGroups;
    const unsigned dqSize;

    const unsigned addrWidth;

    const unsigned termMax;

    void notImplemented();

    const unsigned nOps;

    std::string name() const { return "DQCommon"; }
};

}

#endif //GEM5_DATAFLOW_QUEUE_COMMON_HH
