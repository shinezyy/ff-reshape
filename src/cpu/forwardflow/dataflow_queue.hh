//
// Created by zyy on 19-6-10.
//

#ifndef __FF_DATAFLOW_QUEUE_HH__
#define __FF_DATAFLOW_QUEUE_HH__

#include <cstdint>
#include <deque>
#include <random>
#include <unordered_map>
#include <vector>

#include "cpu/forwardflow/comm.hh"
#include "cpu/forwardflow/crossbar.hh"
#include "cpu/forwardflow/crossbar_narrow.hh"
#include "cpu/forwardflow/network.hh"
#include "cpu/timebuf.hh"
#include "fu_pool.hh"

struct DerivFFCPUParams;

namespace FF{

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

template <class Impl>
class DataflowQueueBank{

public:
    typedef typename Impl::DynInstPtr DynInstPtr;
//    using DynInstPtr = BaseO3DynInst<Impl>*;

    typedef typename Impl::CPUPol::MemDepUnit MemDepUnit;

    typedef typename Impl::CPUPol CPUPolicy;

    typedef typename Impl::CPUPol::ReadyInstsQueue XReadyInstsQueue;
private:
    typedef typename CPUPolicy::DataflowQueues DQ;

    DQ *dq;

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

    // input forward pointers
    std::vector<WKPointer> inputPointers;

    // output forward pointers
    // init according to nops

    std::vector<DQPointer> outputPointers;

    unsigned tail;

    unsigned readyQueueSize;

    std::deque<DQPointer> readyQueue;

public:
    void advanceTail();

    void setTail(unsigned t);

    explicit DataflowQueueBank(DerivFFCPUParams *params, unsigned bankID, DQ *dq);

    bool canServeNew();

    bool wakeup(WKPointer pointer);

    DynInstPtr wakeupInstsFromBank();

    std::vector<DQPointer> readPointersFromBank();

    DynInstPtr readInstsFromBank(DQPointer pointer) const;

    void writeInstsToBank(DQPointer pointer, DynInstPtr& inst);

    void checkReadiness(DQPointer pointer);

    DynInstPtr findInst(InstSeqNum) const;

    void checkPending();

    std::vector<std::array<DQPointer, 4>> prematureFwPointers;

    void resetState();

    DynInstPtr tryWakeTail();

    DynInstPtr tryWakeDirect();

    std::string _name;

    std::string name() const {return _name;}

    void clear(bool markSquashed);

    void erase(DQPointer p, bool markSquashed);

    void cycleStart();

    void clearPending(DynInstPtr &inst);

    DQPointer extraWakeupPointer;

    void printTail();

    void setNearlyWakeup(DQPointer ptr);

    //stats:
    Stats::Scalar wakenUpByPointer;
    Stats::Scalar wakenUpAtTail;
    Stats::Scalar directReady;
    Stats::Scalar directWakenUp;


    void regStats();

    bool hasTooManyPendingInsts();

    void squash(const DQPointer &squash_from);

    bool servedForwarder;

    bool servedNonForwarder;

    void dumpOutPointers() const;

    void dumpInputPointers() const;

    void countUpPendingPointers();

    void countUpPendingInst();

    XReadyInstsQueue *readyInstsQueue;
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

    typedef typename Impl::CPUPol::LSQ LSQ;

    typedef typename Impl::CPUPol::DataflowQueueBank XDataflowQueueBank;
//    using XDataflowQueueBank = DataflowQueueBank<Impl>;
//
    typedef typename Impl::CPUPol::ReadyInstsQueue XReadyInstsQueue;

    const unsigned WritePorts, ReadPorts;

    unsigned writes, reads;

    bool insert(DynInstPtr &inst, bool nonSpec);

    void tick();

    void cycleStart();

    explicit DataflowQueues(DerivFFCPUParams *);

private:
    // init from params
    const unsigned nBanks;
    const unsigned nOps;
    const unsigned nFUGroups;

    const unsigned depth;

    const unsigned queueSize;

    std::vector<std::deque<WKPointer>> wakeQueues;

    std::vector<std::deque<DQPacket<PointerPair>>> forwardPointerQueue;

    std::vector<XDataflowQueueBank *> dqs;

    TimeBuffer<DQStruct> *DQTS;

    typename TimeBuffer<DQStruct>::wire toNextCycle;
    typename TimeBuffer<DQStruct>::wire fromLastCycle;

//    std::vector<bool> wakenValids;
//    std::vector<DynInstPtr> wakenInsts;

    OmegaNetwork<DynInstPtr> bankFUMIN;
    OmegaNetwork<WKPointer> wakeupQueueBankMIN;
    OmegaNetwork<PointerPair> pointerQueueBankMIN;

    DQPacket<WKPointer> nullWKPkt;
    DQPacket<PointerPair> nullFWPkt;
    DQPacket<DynInstPtr> nullInstPkt;

    CrossBar<DynInstPtr> bankFUXBar;
    CrossBar<WKPointer> wakeupQueueBankXBar;
    CrossBar<PointerPair> pointerQueueBankXBar;

    CrossBarNarrow<WKPointer> wakeupQueueBankNarrowXBar;

    std::vector<DQPacket<DynInstPtr>> fu_requests;
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

    std::pair<unsigned, boost::dynamic_bitset<> > coordinateFU(
            DynInstPtr &inst, unsigned bank);

    std::vector<std::vector<bool>> fuGroupCaps;
    std::unordered_map<OpClass, unsigned> opLat;

    bool llBlocked;
    bool llBlockedNext;

    boost::dynamic_bitset<> uint2Bits(unsigned);

    unsigned head, tail;

    const unsigned int bankWidth;
    const unsigned int bankMask;
    const unsigned int indexWidth;
    const unsigned int indexMask;

public:
    DQPointer uint2Pointer(unsigned) const;

    unsigned pointer2uint(const DQPointer &) const;

    unsigned pointer2uint(const WKPointer &) const;

    unsigned getHeadPtr() const {return head;}
    unsigned getTailPtr() const {return tail;}

    unsigned getHeadTerm() const {return headTerm;}

    void retireHead(bool isSquashed, FFRegValue v);

    std::unordered_map<DQPointer, FFRegValue> committedValues;

    DynInstPtr getHead() const;

    std::list<DynInstPtr> getBankHeads();

    std::list<DynInstPtr> getBankTails();

    DynInstPtr getTail();

    void squash(DQPointer p, bool all, bool including);

    bool isFull() const;

    unsigned numInDQ() const;

    unsigned numFree() const;

    bool isEmpty() const;

    bool insertBarrier(DynInstPtr &inst);

    bool insertNonSpec(DynInstPtr &inst);

//    void recordProducer(DynInstPtr &inst);

    void insertForwardPointer(PointerPair pair);

    void digestForwardPointer();


    bool stallToUnclog() const;

    DynInstPtr findInst(InstSeqNum seq_to_quash) const;

    FFRegValue readReg(const DQPointer &src, const DQPointer &dest);

    void setReg(DQPointer pointer, FFRegValue val);

    void addReadyMemInst(DynInstPtr inst, bool isOrderDep = true);

    void rescheduleMemInst(DynInstPtr &inst, bool isStrictOrdered, bool isFalsePositive = false);

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

    void scheduleNonSpec();

private:

    unsigned extraWKPtr;

    void incExtraWKptr();

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

    bool wakeupQueueClogging() const;
    bool fwPointerQueueClogging() const;

    std::vector<FFRegValue> regFile;

    MemDepUnit memDepUnit;

    void markFwPointers(std::array<DQPointer, 4> &pointers,
            PointerPair &pair, DynInstPtr &inst);

    std::list<DynInstPtr> blockedMemInsts;

    std::list<DynInstPtr> retryMemInsts;

    void readQueueHeads();

    void dumpInstPackets(std::vector<DQPacket<DynInstPtr>*>&);

    void dumpPairPackets(std::vector<DQPacket<PointerPair>*>&);

    std::list<DynInstPtr> deferredMemInsts;

    DIEWC *diewc;

    void extraWakeup(const WKPointer &wk);

    void alignTails();

    void replayMemInsts();

    DynInstPtr getBlockedMemInst();

    void dumpQueues();

public:
    bool logicallyLT(unsigned left, unsigned right) const;

    bool validPosition(unsigned u) const;

    bool logicallyLET(unsigned left, unsigned right) const;

    unsigned dec(unsigned u) const;

    unsigned inc(unsigned u) const;

    void deferMemInst(DynInstPtr &inst);

    DynInstPtr getDeferredMemInstToExecute();

    void setTimeBuf(TimeBuffer<DQStruct>* dqtb);

    void setLSQ(LSQ *lsq);

    void setDIEWC(DIEWC *diewc);

    void setCPU(O3CPU *_cpu) {cpu = _cpu;};

    std::string name() const {return "dataflow_queue";}

    void violation(DynInstPtr store, DynInstPtr violator);

    void tryFastCleanup();

    unsigned numInFlightFw();

    void writebackLoad(DynInstPtr &inst);

    void wakeMemRelated(DynInstPtr &inst);

    void completeMemInst(DynInstPtr &inst);

    DynInstPtr findBySeq(InstSeqNum seq);

    bool queuesEmpty();

    void endCycle();

private:
    unsigned oldestUsed;

    std::pair<InstSeqNum, Addr> clearHalfWKQueue();

    std::pair<InstSeqNum, Addr> clearHalfFWQueue();

    void processWKQueueFull();

    void processFWQueueFull();

    std::list<unsigned> opPrioList;

    void rearrangePrioList();

    void countUpPointers();
public:
    void maintainOldestUsed();

    unsigned getOldestUsed() {return oldestUsed;};

    void dumpFwQSize();

    bool halfSquash;

    InstSeqNum halfSquashSeq;

    Addr halfSquashPC;

    DynInstPtr readInst(const DQPointer &p) const;

    bool hasTooManyPendingInsts();

    void advanceHead();

    DynInstPtr checkAndGetParent(const DQPointer &parent, const DQPointer &child) const;

    Stats::Vector readyWaitTime;

    Stats::Scalar oldWaitYoung;


    Stats::Vector WKFlowUsage;

    Stats::Vector WKQueueLen;

    Stats::Scalar HalfSquashes;
private:

    unsigned qAllocPtr{};

    unsigned allocateWakeQ();

    std::mt19937 gen;

    std::uniform_int_distribution<unsigned> randAllocator;

    void tryResetRef();

    std::vector<std::deque<WKPointer>> tempWakeQueues;

    void mergeExtraWKPointers();

    void dumpWkQSize();

    void dumpWkQ();

    void readPointersToWkQ();

    void clearInflightPackets();

    void clearPending2SquashedRange(unsigned start, unsigned end);

    int headTerm;

    const unsigned termMax;

    void genFUValidMask();

    const bool MINWakeup;
    const bool XBarWakeup;
    const bool NarrowXBarWakeup;

    void checkUpdateSeq(InstSeqNum &seq, Addr &addr, InstSeqNum seq_new, Addr addr_new);

    void pushToWakeQueue(unsigned q_index, WKPointer ptr);

    const bool AgedWakeQueuePush;
    const bool AgedWakeQueuePktSel;

    int pointerCmp(const WKPointer &lptr, const WKPointer &rptr);

    bool notOlderThan(const WKPointer &lptr, const WKPointer &rptr);
    bool notYoungerThan(const WKPointer &lptr, const WKPointer &rptr);

    // l < r: l is oldder, return -1
    // l == r: return 0
    // l > r: return 1
    std::vector<bool> pushFF;

    std::vector<XReadyInstsQueue*> readyInstsQueues;

    std::vector<unsigned> fuPointer[OpGroups::nOpGroups];

    void shuffleNeighbors();
public:
    void countCycles(DynInstPtr &inst, WKPointer *wk);

    int FPAddOps[3]{OpClass::FloatAdd,
        OpClass::FloatCmp,
        OpClass::FloatCvt};

    int MultDivOps[7]{OpClass::IntMult,
        OpClass::IntDiv,

        OpClass::FloatMult,
        OpClass::FloatMultAcc,

        OpClass::FloatMisc,

        OpClass::FloatDiv,
        OpClass::FloatSqrt};

    bool matchInGroup(OpClass op, OpGroups op_group);

};

}

#endif //__FF_DATAFLOW_QUEUE_HH__
