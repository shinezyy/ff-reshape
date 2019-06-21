//
// Created by zyy on 19-6-10.
//

#ifndef __FF_DATAFLOW_QUEUE_HH__
#define __FF_DATAFLOW_QUEUE_HH__

#include <cstdint>
#include <queue>
#include <vector>

#include "cpu/forwardflow/comm.hh"
#include "cpu/forwardflow/dyn_inst.hh"
#include "cpu/forwardflow/network.hh"

struct DerivFFCPUParams;

namespace FF{


template <class Impl>
class DataflowQueueBank{

//    using DynInstPtr = RefCountingPtr<BaseO3DynInst<Impl>>;
    typedef typename Impl::DynInstPtr DynInstPtr;

    const unsigned nOps{4};


    const unsigned depth;

    const DQPointer nullDQPointer;

    std::vector<DynInstPtr> instArray;


    // instructions that waiting for only one operands
    boost::dynamic_bitset<> nearlyWakeup;

    // instructions that waiting for only one operands
    // and the wakeup pointer has already arrived
    std::vector<DQPointer> pendingWakeupPointers;
    bool anyPending;


    // input forward pointers
    std::vector<DQPointer> inputPointers;

    // inst rejected by FU
    DynInstPtr pendingInst;
    bool pendingInstValid;

    // output forward pointers
    // init according to nops
    std::vector<DQPointer> outputPointers;

public:
    DataflowQueueBank(DerivFFCPUParams *params);

    bool canServeNew();

    bool wakeup(DQPointer pointer);

    std::tuple<bool, DynInstPtr &> wakeupInstsFromBank();

    const std::vector<DQPointer> readPointersFromBank();

    DynInstPtr &readInstsFromBank(DQPointer pointer);

    bool instGranted;

    void tick();
};


template <class Impl>
class DataflowQueues
{
private:
    const DQPointer nullDQPointer;

public:
    typedef typename Impl::O3CPU O3CPU;

//    typedef typename Impl::DynInstPtr DynInstPtr;
    using DynInstPtr = BaseO3DynInst<Impl>*;

    typedef typename Impl::CPUPol::IEW IEW;
    typedef typename Impl::CPUPol::MemDepUnit MemDepUnit;
    typedef typename Impl::CPUPol::DQStruct DQStruct;
    typedef typename Impl::CPUPol::FUWrapper FUWrapper;

//    typedef typename Impl::CPUPol::DataflowQueueBank DataflowQueueBank;
    using XDataflowQueueBank = DataflowQueueBank<Impl>;

    const unsigned WritePorts, ReadPorts;

    unsigned writes, reads;

    bool insert(DynInstPtr &ptr);

    void tick();

    void clear();

    DataflowQueues(DerivFFCPUParams *);

private:
    // init from params
    const unsigned nBanks;
    const unsigned nOps;
    const unsigned nFUGroups;

    std::vector<std::queue<DQPointer>> wake_queues;

    std::vector<std::queue<Packet<PointerPair>>> forwardPointerQueue;

    std::vector<XDataflowQueueBank> dqs;

    TimeBuffer<DQStruct> *DQTS;

    typename TimeBuffer<DQStruct>::wire toNextCycle;
    typename TimeBuffer<DQStruct>::wire fromLastCycle;

    std::vector<bost> wakenValids;
    std::vector<DynInstPtr> wakenInsts;

    OmegaNetwork<DynInstPtr> bankFUNet;

    OmegaNetwork<DQPointer> wakeupQueueBankNet;

    OmegaNetwork<PointerPair> pointerQueueBankNet;

    std::vector<Packet<DynInstPtr>> fu_requests;
    std::vector<bool> fu_req_granted;
    std::vector<Packet<DynInstPtr>*> fu_req_ptrs;
    std::vector<Packet<DynInstPtr>*> fu_granted_ptrs;

    std::vector<Packet<DQPointer>> wakeup_requests;
    std::vector<bool> wake_req_granted;
    std::vector<Packet<DQPointer>*> wakeup_req_ptrs;
    std::vector<Packet<DQPointer>*> wakeup_granted_ptrs;

    std::vector<Packet<PointerPair>> insert_requests;
    std::vector<bool> insert_req_granted;
    std::vector<Packet<PointerPair>*> insert_req_ptrs;
    std::vector<Packet<PointerPair>*> insert_granted_ptrs;

    std::vector<FUWrapper> fuWrappers;

    const unsigned maxQueueDepth;

    boost::dynamic_bitset<> coordinateFU(DynInstPtr &inst, unsigned bank);

    std::vector<std::vecotr<bool>> fuGroupCaps;
    std::vector<unsigned> fuPointer;
    std::vector llBlocked;
    std::vector llBlockedNext;

    boost::dynamic_bitset<> uint2Bits(unsigned);

    void insertForwardPointer(PointerPair pair);

    DynInstPtr getHead();

    unsigned head, tail;

    DQPointer uint2Pointer(unsigned);

public:
    void retireHead();

    unsigned int indexMask;
    unsigned int indexWidth;
    unsigned int bankMask;

private:
    unsigned forwardPtrIndex;
};

}

#endif //__FF_DATAFLOW_QUEUE_HH__
