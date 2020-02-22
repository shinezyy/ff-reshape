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
#include "cpu/forwardflow/crossbar_dedi_dest.hh"
#include "cpu/forwardflow/crossbar_narrow.hh"
#include "cpu/forwardflow/dataflow_queue_common.hh"
#include "cpu/forwardflow/network.hh"
#include "cpu/timebuf.hh"
#include "fu_pool.hh"

//#define zcoding

#ifdef zcoding
//#include "cpu/forwardflow/dataflow_queue_top.hh"
#include "cpu/forwardflow/dyn_inst.hh"

#endif

struct DerivFFCPUParams;

namespace FF{

template <class Impl>
class DataflowQueues
{
//private:
//    const DQPointer nullDQPointer;
//    const WKPointer nullWKPointer;

public:
    typedef typename Impl::O3CPU O3CPU;

#ifdef zcoding
    using DynInstPtr = BaseO3DynInst<Impl>*;
#else
    typedef typename Impl::DynInstPtr DynInstPtr;
#endif

    typedef typename Impl::CPUPol::DQTop DQTop;
    typedef typename Impl::CPUPol::DIEWC DIEWC;
    typedef typename Impl::CPUPol::DQTopTS DQTopTs;
    typedef typename Impl::CPUPol::DQGroupTS DQGroupTs;
    typedef typename Impl::CPUPol::FUWrapper FUWrapper;
    typedef typename Impl::CPUPol::LSQ LSQ;

    typedef typename Impl::CPUPol::DataflowQueueBank XDataflowQueueBank;
//    using XDataflowQueueBank = DataflowQueueBank<Impl>;
//
    typedef typename Impl::CPUPol::ReadyInstsQueue XReadyInstsQueue;

    unsigned writes, reads;

    DataflowQueues(DerivFFCPUParams *, unsigned groupID, DQCommon *_c);

    XDataflowQueueBank * operator [](unsigned bank);

    std::vector<XDataflowQueueBank *> dqs;

private:
    // init from params
    const unsigned nBanks;
    const unsigned nOps;
    const unsigned nFUGroups;

    const unsigned depth;

    const unsigned queueSize;

    std::vector<std::deque<WKPointer>> wakeQueues;

    std::vector<std::deque<DQPacket<PointerPair>>> forwardPointerQueue;

    DQCommon *c;

    TimeBuffer<DQTopTs> *DQTS;

    DQGroupTs* toNextCycle;
    DQGroupTs* fromLastCycle;

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
    CrossBarDedi<WKPointer> wakeupQueueBankDediXBar;

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

    unsigned head, tail;

    const unsigned int bankWidth;
    const unsigned int bankMask;
    const unsigned int indexWidth;
    const unsigned int indexMask;

public:

    std::unordered_map<DQPointer, FFRegValue> committedValues;

    std::list<DynInstPtr> getBankHeads();

    std::list<DynInstPtr> getBankTails();

    void squashFU(InstSeqNum seq);

    void squashReady(InstSeqNum seq);

    void squashAll();
//    void recordProducer(DynInstPtr &inst);

    void insertForwardPointer(PointerPair pair);

    void digestForwardPointer();


    bool stallToUnclog() const;

    DynInstPtr findInst(InstSeqNum seq_to_quash) const;

    FFRegValue readReg(const DQPointer &src, const DQPointer &dest);

    void setReg(DQPointer pointer, FFRegValue val);

    void resetState();

    void resetEntries();

    void regStats();

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

//    std::vector<FFRegValue> regFile;

    void markFwPointers(std::array<DQPointer, 4> &pointers,
            PointerPair &pair, DynInstPtr &inst);


    void readQueueHeads();

    void dumpInstPackets(std::vector<DQPacket<DynInstPtr>*>&);

    void dumpPairPackets(std::vector<DQPacket<PointerPair>*>&);

    DIEWC *diewc;

    void extraWakeup(const WKPointer &wk);

    void dumpQueues();

public:
    void alignTails();

    void setTimeBuf(TimeBuffer<DQTopTs>* dqtb);

    void setLSQ(LSQ *lsq);

    void setDIEWC(DIEWC *diewc);

    void setCPU(O3CPU *_cpu) {cpu = _cpu;};

    void tryFastCleanup();

    unsigned numInFlightFw();

    void writebackLoad(DynInstPtr &inst);

    void wakeMemRelated(DynInstPtr &inst);

    void completeMemInst(DynInstPtr &inst);

    DynInstPtr findBySeq(InstSeqNum seq);

    bool queuesEmpty();

    void endCycle();

private:


    std::list<unsigned> opPrioList;

    void rearrangePrioList();

    void countUpPointers();
public:
    void dumpFwQSize();

    DynInstPtr readInst(const DQPointer &p) const;

    Stats::Vector readyWaitTime;

    Stats::Scalar oldWaitYoung;

    Stats::Vector WKFlowUsage;

    Stats::Vector WKQueueLen;

    Stats::Scalar SrcOpPackets;
    Stats::Scalar DestOpPackets;
    Stats::Scalar KeySrcPacket;
    Stats::Scalar MemPackets;
    Stats::Scalar OrderPackets;
    Stats::Scalar MiscPackets;
    Stats::Formula TotalPackets;

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

public:
    void clearInflightPackets();

    void clearPending2SquashedRange(unsigned start, unsigned end);

private:
    int headTerm;

    const unsigned termMax;

    void genFUValidMask();

public:
    const bool MINWakeup;
    const bool XBarWakeup;
    const bool NarrowXBarWakeup;
    const bool DediXBarWakeup;

    const bool NarrowLocalForward;

private:
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


    bool matchInGroup(OpClass op, OpGroups op_group);

    void mergeLocalWKPointers();

    void cycleStart();

    void tick();

    bool hasTooManyPendingInsts();

    //// inter group transferring
public:
    void setGroupID(unsigned id);

    unsigned getGroupID() {return groupID;}

    void setTop(DQTop *_top) {top = _top;}

private:
    DQTop *top;

public:
    unsigned groupID;

private:
    // physically located between DQ groups
    std::deque<WKPointer> outQueue;

    void sendToNextGroup(const WKPointer &wk_pointer);

    unsigned interGroupSent;

    const unsigned interGroupBW; // TODO: enforece it

public:
    void transmitPointers();

    void clearSent(); // TODO: clear cyclely

    void receivePointers(const WKPointer &wk_pointer);

    // half squash related:
public:
    bool halfSquash;

    InstSeqNum halfSquashSeq;

    Addr halfSquashPC;

    std::pair<InstSeqNum, Addr> clearHalfWKQueue();

    std::pair<InstSeqNum, Addr> clearHalfFWQueue();

    void processWKQueueFull();

    void processFWQueueFull();

private:
    std::string _name;

public:
    std::string name() const { return _name; }

};

}

#undef zcoding

#endif //__FF_DATAFLOW_QUEUE_HH__
