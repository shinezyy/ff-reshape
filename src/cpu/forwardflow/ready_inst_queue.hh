//
// Created by zyy on 2020/1/18.
//

#ifndef GEM5_READY_INST_QUEUE_HH
#define GEM5_READY_INST_QUEUE_HH

#include <cstdint>
#include <deque>
#include <random>
#include <unordered_map>
#include <vector>

#include <params/DerivFFCPU.hh>

#include "cpu/forwardflow/comm.hh"
#include "cpu/forwardflow/network.hh"
#include "cpu/timebuf.hh"
#include "fu_pool.hh"

namespace FF
{

template <class Impl>
class ReadyInstsQueue{

public:
    typedef typename Impl::DynInstPtr DynInstPtr;

    ReadyInstsQueue(DerivFFCPUParams *params);

    void squash(InstSeqNum inst_seq);

    DynInstPtr getInst(FF::OpGroups group);

    void insertInst(FF::OpGroups group, DynInstPtr &inst);

    void insertEmpirically(DynInstPtr &inst);

    const std::size_t maxReadyQueueSize;

    bool isFull();

    bool isFull(FF::OpGroups group);

    unsigned targetGroup;

    std::vector<std::__cxx11::list<DynInstPtr> > preScheduledQueues;

};

}

#endif //GEM5_READY_INST_QUEUE_HH
