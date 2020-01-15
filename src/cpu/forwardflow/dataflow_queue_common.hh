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

namespace FF {
// common
struct DQCommon {
    void init(DerivFFCPUParams *params);

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

    boost::dynamic_bitset<> uint2Bits(unsigned from);

    DQPointer uint2Pointer(unsigned u) const;

    unsigned pointer2uint(const DQPointer &ptr) const;

    unsigned pointer2uint(const WKPointer &ptr) const;

    unsigned bankSize;

    unsigned nBanks;
    unsigned groupSize;

    unsigned nGroups;
    unsigned dqSize;

    unsigned addrWidth;

};

extern DQCommon dqCommon;

template <class Impl>
class ReadyInstsQueue{

public:
    typedef typename Impl::DynInstPtr DynInstPtr;

    ReadyInstsQueue(DerivFFCPUParams *params);

    void squash(InstSeqNum inst_seq);

    DynInstPtr getInst(OpGroups group);

    void insertInst(OpGroups group, DynInstPtr &inst);

    void insertEmpirically(DynInstPtr &inst);

    const size_t maxReadyQueueSize;

    bool isFull();

    bool isFull(OpGroups group);

    unsigned targetGroup;

    std::vector<std::list<DynInstPtr> > preScheduledQueues;

};

}

#endif //GEM5_DATAFLOW_QUEUE_COMMON_HH
