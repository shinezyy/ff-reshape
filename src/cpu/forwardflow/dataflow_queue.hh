//
// Created by zyy on 19-6-10.
//

#ifndef __FF_DATAFLOW_QUEUE_HH__
#define __FF_DATAFLOW_QUEUE_HH__

#include <cstdint>
#include <queue>
#include <vector>

#include "cpu/forwardflow/comm.hh"
//#include "cpu/forwardflow/dyn_inst.hh"
#include "cpu/forwardflow/network.hh"
#include "cpu/timebuf.hh"
#include "fu_pool.hh"

struct DerivFFCPUParams;

namespace FF{


template <class Impl>
class DataflowQueueBank{

    typedef typename Impl::DynInstPtr DynInstPtr;
//    using DynInstPtr = BaseO3DynInst<Impl>*;

    typedef typename Impl::CPUPol::MemDepUnit MemDepUnit;

    const unsigned nOps{4};


    const unsigned depth;

    const DQPointer nullDQPointer;

    std::vector<DynInstPtr> instArray;

    // instructions that waiting for only one operands
    boost::dynamic_bitset<> nearlyWakeup;

    // instructions that waiting for only one operands
    // and the wakeup pointer has already arrived
    std::vector<WKPointer> pendingWakeupPointers;
    bool anyPending;

    std::vector<unsigned> readyIndices;

    // input forward pointers
    std::vector<WKPointer> inputPointers;

    // inst rejected by FU
    DynInstPtr pendingInst;
    bool pendingInstValid;

    // output forward pointers
    // init according to nops

    std::vector<DQPointer> outputPointers;

public:
    explicit DataflowQueueBank(DerivFFCPUParams *params);

    bool canServeNew();

    bool wakeup(WKPointer pointer);

    std::tuple<bool, DynInstPtr> wakeupInstsFromBank();

    const std::vector<DQPointer> readPointersFromBank();

    DynInstPtr &readInstsFromBank(DQPointer pointer);

    void writeInstsToBank(DQPointer pointer, DynInstPtr& inst);

    void checkReadiness(DQPointer pointer);

    DynInstPtr findInst(InstSeqNum) const;

    bool instGranted;

    void tick();

    std::vector<std::array<DQPointer, 4>> prematureFwPointers;

    void resetState();

};


template <class Impl>
class DataflowQueues
{
private:
    const DQPointer nullDQPointer;
    const WKPointer nullWKPointer;

public:
    typedef typename Impl::O3CPU O3CPU;

    typedef typename Impl::DynInstPtr DynInstPtr;
//    using DynInstPtr = BaseO3DynInst<Impl>*;

    typedef typename Impl::CPUPol::DIEWC DIEWC;
    typedef typename Impl::CPUPol::MemDepUnit MemDepUnit;
    typedef typename Impl::CPUPol::DQStruct DQStruct;
    typedef typename Impl::CPUPol::FUWrapper FUWrapper;

    typedef typename Impl::CPUPol::DataflowQueueBank XDataflowQueueBank;
//    using XDataflowQueueBank = DataflowQueueBank<Impl>;

    const unsigned WritePorts, ReadPorts;

    unsigned writes, reads;

    bool insert(DynInstPtr &inst);

    void tick();

    void clear();

    explicit DataflowQueues(DerivFFCPUParams *);

private:
    // init from params
    const unsigned nBanks;
    const unsigned nOps;
    const unsigned nFUGroups;

    const unsigned depth;

    const unsigned queueSize;

    std::vector<std::queue<WKPointer>> wakeQueues;

    std::vector<std::queue<DQPacket<PointerPair>>> forwardPointerQueue;

    std::vector<XDataflowQueueBank> dqs;

    TimeBuffer<DQStruct> *DQTS;

    typename TimeBuffer<DQStruct>::wire toNextCycle;
    typename TimeBuffer<DQStruct>::wire fromLastCycle;

//    std::vector<bool> wakenValids;
//    std::vector<DynInstPtr> wakenInsts;

    OmegaNetwork<DynInstPtr> bankFUNet;

    OmegaNetwork<WKPointer> wakeupQueueBankNet;

    OmegaNetwork<PointerPair> pointerQueueBankNet;

    std::vector<DQPacket<DynInstPtr>> fu_requests;
    std::vector<bool> fu_req_granted;
    std::vector<DQPacket<DynInstPtr>*> fu_req_ptrs;
    std::vector<DQPacket<DynInstPtr>*> fu_granted_ptrs;

    std::vector<DQPacket<WKPointer>> wakeup_requests;
    std::vector<bool> wake_req_granted;
    std::vector<DQPacket<WKPointer>*> wakeup_req_ptrs;
    std::vector<DQPacket<WKPointer>*> wakeup_granted_ptrs;

    std::vector<DQPacket<PointerPair>> insert_requests;
    std::vector<bool> insert_req_granted;
    std::vector<DQPacket<PointerPair>*> insert_req_ptrs;
    std::vector<DQPacket<PointerPair>*> insert_granted_ptrs;

    std::vector<FUWrapper> fuWrappers;

    FFFUPool *fuPool;

    boost::dynamic_bitset<> coordinateFU(DynInstPtr &inst, unsigned bank);

    std::vector<std::vector<bool>> fuGroupCaps;
    std::vector<unsigned> fuPointer;
    bool llBlocked;
    bool llBlockedNext;

    boost::dynamic_bitset<> uint2Bits(unsigned);

    unsigned head, tail;

    DQPointer uint2Pointer(unsigned);

    unsigned pointer2uint(DQPointer);

public:
    unsigned getHeadPtr() const {return head;}
    unsigned getTailPtr() const {return tail;}

    void retireHead();

    unsigned int indexMask;
    unsigned int indexWidth;
    unsigned int bankMask;

    DynInstPtr getHead();

    DynInstPtr getTail();

    void squash(InstSeqNum );

    bool isFull();

    unsigned numInDQ();

    unsigned numFree();

    bool isEmpty();

    void insertBarrier(DynInstPtr &inst);

    void insertNonSpec(DynInstPtr &inst);

//    void recordProducer(DynInstPtr &inst);

    void insertForwardPointer(PointerPair pair);

    bool stallToUnclog();

    DynInstPtr findInst(InstSeqNum seq_to_quash) const;

    FFRegValue readReg(DQPointer pointer);

    void setReg(DQPointer pointer, FFRegValue val);

    void addReadyMemInst(DynInstPtr);

    void rescheduleMemInst(DynInstPtr &inst);

    /** Re-executes all rescheduled memory instructions. */
    void replayMemInst(DynInstPtr &inst);

    /** Moves memory instruction onto the list of cache blocked instructions */
    void blockMemInst(DynInstPtr &inst);

    void cacheUnblocked();

    void drainSanityCheck() const;

    void takeOverFrom();

    void resetState();

    void resetEntries();

    void regStats();
private:

    unsigned readyMemInstPtr;

    unsigned forwardPtrIndex;

    /** Pointer to CPU. */
    O3CPU *cpu;

    const unsigned maxQueueDepth;


    unsigned numPendingWakeups;
    unsigned numPendingWakeupMax;
    const unsigned PendingWakeupThreshold;
    const unsigned PendingWakeupMaxThreshold;

    unsigned numPendingFwPointers;
    unsigned numPendingFwPointerMax;
    const unsigned PendingFwPointerThreshold;

    bool wakeupQueueClogging();
    bool fwPointerQueueClogging();

    std::vector<FFRegValue> regFile;

    MemDepUnit memDepUnit;

    void markFwPointers(std::array<DQPointer, 4> &pointers, PointerPair &pair);

    std::list<DynInstPtr> blockedMemInsts;

    std::list<DynInstPtr> retryMemInsts;

    void readQueueHeads();

    void dumpInstPackets(std::vector<DQPacket<DynInstPtr>*>&);
public:
    void setTimeBuf(TimeBuffer<DQStruct>* dqtb);
};

}

#endif //__FF_DATAFLOW_QUEUE_HH__
