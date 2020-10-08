//
// Created by zyy on 2020/1/15.
//

#ifndef GEM5_DATAFLOW_QUEUE_TOP_HH
#define GEM5_DATAFLOW_QUEUE_TOP_HH

//#define zcoding

#include <cstdint>
#include <deque>
#include <random>
#include <unordered_map>
#include <vector>

#ifdef __CLION_CODING__
#include "cpu/forwardflow/dataflow_queue.hh"
#include "cpu/forwardflow/dataflow_queue_bank.hh"
#include "cpu/forwardflow/dyn_inst.hh"
#include "cpu/forwardflow/mem_dep_unit.hh"

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
class DQTop
{
public:
    // typedec:

    typedef typename Impl::CPUPol::DIEWC DIEWC;
    typedef typename Impl::CPUPol::DQStruct DQTopTS;
    typedef typename Impl::CPUPol::FUWrapper FUWrapper;
    typedef typename Impl::CPUPol::LSQ LSQ;

#ifdef __CLION_CODING__
    typedef DataflowQueues<Impl> DataflowQueues;
    typedef DataflowQueueBank<Impl> DataflowQueueBank;
    typedef MemDepUnit<StoreSet, Impl> MemDepUnit;

    template<class Impl>
    class FullInst: public BaseDynInst<Impl>, public BaseO3DynInst<Impl> {};
    using DynInstPtr = FullInst<Impl>*;
#else
    typedef typename Impl::CPUPol::DataflowQueues DataflowQueues;
    typedef typename Impl::CPUPol::DataflowQueueBank DataflowQueueBank;
    typedef typename Impl::DynInstPtr DynInstPtr;
    typedef typename Impl::CPUPol::MemDepUnit MemDepUnit;

#endif

public:
    DQCommon c;

    DIEWC *diewc;

    // DQ groups:
    std::vector<DataflowQueues *> dqGroups;

    MemDepUnit memDepUnit;

    std::list<DynInstPtr> deferredMemInsts;

    std::list<DynInstPtr> blockedMemInsts;

    std::list<DynInstPtr> retryMemInsts;

    std::unordered_map<BasePointer, FFRegValue> committedValues;

    explicit DQTop(DerivFFCPUParams *params);

    void cycleStart();

    void tick();

    /** Re-executes all rescheduled memory instructions. */
    void replayMemInst(DynInstPtr &inst);

    void scheduleNonSpec();

    // 1: sent reexec pointer; 2: canceling bypassing
    std::pair<bool, bool> reExecTailLoad(DynInstPtr &inst, bool is_tail);

    void centralizedExtraWakeup(const WKPointer &wk);

    bool hasTooManyPendingInsts();

    void advanceHead();

    DynInstPtr findBySeq(InstSeqNum seq);

    DynInstPtr getTail();

    DynInstPtr getTailInDQ();

    bool isFull() const;

    unsigned getHeadPtr() const {return head;}

    unsigned getTailPtr() const {return tail;}

    unsigned getHeadTerm() const {return headTerm;}

    DynInstPtr getHead() const;

    DynInstPtr readInst(const BasePointer &p) const;

    unsigned int head;

    unsigned int tail;

    unsigned int headTerm;

    bool logicallyLT(unsigned left, unsigned right) const;

    bool logicallyLET(unsigned left, unsigned right) const;

    unsigned dec(unsigned u) const;

    unsigned inc(unsigned u) const;

    void maintainOldestUsed();

    unsigned getOldestUsed() {return oldestUsed;};

    unsigned int oldestUsed;

    bool validPosition(unsigned u) const;

    DataflowQueues *committingGroup;

    void updateCommittingGroup(DataflowQueues *dq);

    void alignTails();

    // dispatch

    std::pair<bool, PointerPair> insertBarrier(DynInstPtr &inst);

    std::pair<bool, PointerPair> insertNonSpec(DynInstPtr &inst);

    std::pair<bool, PointerPair> insert(DynInstPtr &inst, bool non_spec);

    void insertForwardPointer(PointerPair pair);

    std::list<DynInstPtr> getBankHeads();

    std::list<DynInstPtr> getBankTails();

    bool stallToUnclog() const;

    void retireHead(bool isSquashed, FFRegValue v);

    bool isEmpty() const;

    unsigned numInDQ() const;

    unsigned numFree() const;

    void tryFastCleanup();

    void squash(BasePointer p, bool all, bool including);

    bool queuesEmpty();


    // wiring
    void setTimeBuf(TimeBuffer<DQTopTS>* dqtb);

    void setLSQ(LSQ *lsq);

    void setDIEWC(DIEWC *diewc);

    typedef typename Impl::O3CPU O3CPU;
    O3CPU *cpu;
    void setCPU(O3CPU *_cpu) {cpu = _cpu;};


    // Mem related
    void deferMemInst(DynInstPtr &inst);

    DynInstPtr getDeferredMemInstToExecute();

    DynInstPtr getBlockedMemInst();

    void rescheduleMemInst(DynInstPtr &inst, bool isStrictOrdered, bool isFalsePositive = false);

    void replayMemInsts();

    void blockMemInst(DynInstPtr &inst);

    void cacheUnblocked();

    bool writebackLoad(DynInstPtr &inst);

    void completeMemInst(DynInstPtr &inst);

    // Drain, Switch, Initiation
    void takeOverFrom();

    void drainSanityCheck() const;

    void resetState();

    void resetEntries();

    void regStats();


    unsigned numInFlightFw();

    void dumpFwQSize();

    void clearInflightPackets();

    // Routing related:

    // MemDep to DQ groups: star link: 1 - to - all routing; sparse

    // dispatching.instructions: only one working group; WxW xbar - to - N groups
private:
    ////////////// Dispatching
    const unsigned dispatchWidth;

    // insts
    std::list<DynInstPtr> centerInstBuffer;

    void clearInstBuffer();

    DataflowQueues *dispatchingGroup;

    void distributeInstsToGroup();

    bool switchDispGroup{};

    DynInstPtr switchOn{};

    void schedSwitchDispatchingGroup(DynInstPtr &inst);

    void switchDispatchingGroup();
public:
    bool isSwitching() { return switchDispGroup; };

private:
    // FW pointers
    // dispatching.forward_pointers planA: W - to - W*G asymmetric topology
    // dispatching.forward_pointers planB: 1 - to - 2: working + last working + backward ring routing
//    std::array<PointerPair, Impl::MaxBanks * Impl::MaxOps> centerPairBuffer;
    std::list<PointerPair> centerPairBuffer;

    void clearPairBuffer();

    void distributePairsToGroup();

    //Committing
//    DataflowQueues *committingGroup;
//
//    void switchCommittingGroup();
public:

    unsigned getNextGroup(unsigned group_id) const;

    unsigned getPrevGroup(unsigned group_id) const;

    // wakeup.pointers: Ring, to next group
    void sendToNextGroup(unsigned sending_group, const WKPointer &wk_pointer);

    // deadlock squashing
public:
    bool halfSquash;

    InstSeqNum halfSquashSeq;

    Addr halfSquashPC;

    bool notifyHalfSquash(InstSeqNum new_seq, Addr new_pc);

    //// stats:
public:
    Stats::Scalar HalfSquashes;

    void addReadyMemInst(DynInstPtr inst, bool isOrderDep = true);

    void endCycle();

private:
    std::vector<std::list<WKPointer> > pseudoCenterWKPointerBuffer;
    unsigned center2GroupRate;

    std::vector<std::list<WKPointer> > interGroupBuffer;
    unsigned group2GroupRate;

public:
    void groupsTxPointers();

    void groupsRxFromCenterBuffer();

    void groupsRxFromPrevGroup();

    unsigned groupsRxFromBuffers(std::vector<std::list<WKPointer>> &queues,
            unsigned limit);

    // update at the end of a cycle
private:
    void checkFlagsAndUpdate();

    unsigned incIndex(unsigned u);

    unsigned decIndex(unsigned u);

public:
    std::string name() const {return "DQTop";}

    Stats::Scalar RegReadCenterInstBuf;
    Stats::Scalar RegReadCenterPairBuf;
    Stats::Scalar RegReadCenterWKBuf;
    Stats::Scalar RegReadInterGroupWKBuf;

    Stats::Scalar RegWriteCenterInstBuf;
    Stats::Scalar RegWriteCenterPairBuf;
    Stats::Scalar RegWriteCenterWKBuf;
    Stats::Scalar RegWriteInterGroupWKBuf;

    unsigned numInFlightWk() const;

    void checkSanity() const;
};


}

#undef zcoding

#endif //GEM6_DATAFLOW_QUEUE_TOP_HH
