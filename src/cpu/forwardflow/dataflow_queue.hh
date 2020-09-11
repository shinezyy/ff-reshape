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

#ifdef __CLION_CODING__
#include "cpu/ff_base_dyn_inst.hh"
#include "cpu/forwardflow/dataflow_queue_bank.hh"
#include "cpu/forwardflow/dataflow_queue_top.hh"
#include "cpu/forwardflow/diewc.hh"
#include "cpu/forwardflow/dyn_inst.hh"
#include "cpu/forwardflow/lsq.hh"

#endif

#include "cpu/forwardflow/comm.hh"
#include "cpu/forwardflow/crossbar.hh"
#include "cpu/forwardflow/crossbar_dedi_dest.hh"
#include "cpu/forwardflow/crossbar_narrow.hh"
#include "cpu/forwardflow/dataflow_queue_common.hh"
#include "cpu/forwardflow/network.hh"
#include "cpu/timebuf.hh"
#include "fu_pool.hh"


struct DerivFFCPUParams;

namespace FF{

template <class Impl>
class DataflowQueues
{
//private:
//    const DQPointer nullDQPointer;
//    const WKPointer nullWKPointer;

public:
    typedef typename Impl::CPUPol CPUPol;

#ifdef __CLION_CODING__
    template<class Impl>
    class FullInst: public BaseDynInst<Impl>, public BaseO3DynInst<Impl> {
    };

    using DynInstPtr = FullInst<Impl>*;

    using O3CPU = FFCPU<Impl>;
    using LSQ = LSQ<Impl>;
    using DQTop = DQTop<Impl>;
    using DIEWC = FFDIEWC<Impl>;
    using XDataflowQueueBank = DataflowQueueBank<Impl>;
#else
    typedef typename Impl::DynInst DynInst;
    typedef typename Impl::DynInstPtr DynInstPtr;
    typedef typename Impl::O3CPU O3CPU;
    typedef typename CPUPol::LSQ LSQ;
    typedef typename CPUPol::DQTop DQTop;
    typedef typename Impl::CPUPol::DIEWC DIEWC;
    typedef typename Impl::CPUPol::DataflowQueueBank XDataflowQueueBank;
#endif

    typedef typename Impl::CPUPol::DQTopTS DQTopTs;
    typedef typename Impl::CPUPol::DQGroupTS DQGroupTs;
    typedef typename Impl::CPUPol::FUWrapper FUWrapper;

    typedef typename Impl::CPUPol::ReadyInstsQueue XReadyInstsQueue;

    unsigned writes, reads;

    DataflowQueues(DerivFFCPUParams *, unsigned groupID, DQCommon *_c, DQTop *_top);

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

    typename TimeBuffer<DQTopTs>::wire topToNext;
    typename TimeBuffer<DQTopTs>::wire topFromLast;

    DQGroupTs* toNextCycle;
    DQGroupTs* fromLastCycle;

public:
    void tieWire(); // executed every cycle for convenience

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

    std::unordered_map<BasePointer, FFRegValue> committedValues;

    std::list<DynInstPtr> getBankHeads();

    std::list<DynInstPtr> getBankTails();

    void squashFU(InstSeqNum seq);

    void squashReady(InstSeqNum seq);

    void squashAll();
//    void recordProducer(DynInstPtr &inst);

    void insertForwardPointer(PointerPair pair);

    void digestPairs();


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

  public:
    bool wakeupQueueClogging() const;
    bool fwPointerQueueClogging() const;

    unsigned numInFlightWk() const;
    unsigned numInFlightFw() const;

  private:

//    std::vector<FFRegValue> regFile;

    void markFwPointers(std::array<DQPointer, 4> &pointers,
            PointerPair &pair, DynInstPtr &inst);


    void readPairQueueHeads();

    void readWakeQueueHeads();

    void dumpInstPackets(std::vector<DQPacket<DynInstPtr>*>&);

    void dumpPairPackets(std::vector<DQPacket<PointerPair>*>&);

    DIEWC *diewc;

public:
    void extraWakeup(const WKPointer &wk);

private:
    void dumpQueues();

    void setWakeupPointersFromFUs();

    void readReadyInstsFromLastCycle();

    void selectReadyInsts();

    void writeFwPointersToNextCycle();

    void wakeupInsts();

    void selectPointersFromWakeQueues();

public:
    void alignTails();

    void setTimeBuf(TimeBuffer<DQTopTs>* dqtb);

    void setLSQ(LSQ *lsq);

    void setDIEWC(DIEWC *diewc);

    void setCPU(O3CPU *_cpu) {cpu = _cpu;};

    void tryFastCleanup();

    DynInstPtr findBySeq(InstSeqNum seq);

    bool queuesEmpty();

private:


    std::list<unsigned> opPrioList;

    void rearrangePrioList();

    void countUpPointers();
public:
    void dumpFwQSize();

    DynInstPtr readInst(const BasePointer &p) const;

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

    void dumpWkQSize();

    void dumpWkQ();

    void readPointersFromLastCycleToWakeQueues();

public:
    void clearInflightPackets();

    void clearPending2SquashedRange(unsigned start, unsigned end);

    void mergeExtraWKPointers();

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

    void put2OutBuffer(const WKPointer &wk_pointer);

    unsigned interGroupSent;

    const unsigned interGroupBW; // TODO: enforece it

public:
    void transmitPointers();

    void clearSent(); // TODO: clear cyclely

    void receivePointers(const WKPointer &wk_pointer);

    // half squash related:
public:
    std::pair<InstSeqNum, Addr> clearHalfWKQueue();

    std::pair<InstSeqNum, Addr> clearHalfFWQueue();

    void processWKQueueFull();

    void processFWQueueFull();

private:
    std::string _name;

public:
    std::string name() const { return _name; }

private:
    void pickInterGroupPointers();

    unsigned fuFAIndex, fuMDIndex;
public:
    Stats::Scalar QueueWriteTxBuf;
    Stats::Scalar QueueReadTxBuf;
    Stats::Scalar QueueReadPairBuf;
    Stats::Scalar QueueWritePairBuf;
    Stats::Scalar CombWKNet;
    Stats::Scalar CombFWNet;
    Stats::Scalar CombSelNet;

    Stats::Scalar SRAMWritePointer;
};

}

#undef zcoding

#endif //__FF_DATAFLOW_QUEUE_HH__
