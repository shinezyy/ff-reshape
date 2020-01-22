//
// Created by zyy on 2020/1/15.
//

#ifndef GEM5_DATAFLOW_QUEUE_TOP_HH
#define GEM5_DATAFLOW_QUEUE_TOP_HH

#define zcoding

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

#ifdef zcoding
#include "cpu/forwardflow/dataflow_queue.hh"
#include "cpu/forwardflow/dataflow_queue_bank.hh"

#endif

struct DerivFFCPUParams;

namespace FF{

template <class Impl>
class DQTop
{
public:
    // typedec:
    typedef typename Impl::DynInstPtr DynInstPtr;

    typedef typename Impl::CPUPol::MemDepUnit MemDepUnit;

    typedef typename Impl::CPUPol::DIEWC DIEWC;
    typedef typename Impl::CPUPol::DQStruct DQStruct;
    typedef typename Impl::CPUPol::FUWrapper FUWrapper;
    typedef typename Impl::CPUPol::LSQ LSQ;

#ifdef zcoding
    typedef DataflowQueues<Impl> DataflowQueues;
    typedef DataflowQueueBank<Impl> DataflowQueueBank;
#else
    typedef typename Impl::CPUPol::DataflowQueues DataflowQueues;
    typedef typename Impl::CPUPol::DataflowQueueBank DataflowQueueBank;
#endif

public:
    DQCommon c; // todo: construct

    DIEWC diewc; // todo: construct

    // DQ groups:
    std::vector<DataflowQueues> dqGroups; // todo: construct

    MemDepUnit memDepUnit; // todo: construct

    std::list<DynInstPtr> deferredMemInsts; // todo: construct

    std::list<DynInstPtr> blockedMemInsts; // todo: construct

    std::list<DynInstPtr> retryMemInsts; // todo: construct

    std::unordered_map<DQPointer, FFRegValue> committedValues; // todo: construct

    explicit DQTop(DerivFFCPUParams *params);

    void cycleStart();

    void tick();

    /** Re-executes all rescheduled memory instructions. */
    void replayMemInst(DynInstPtr &inst);

    void scheduleNonSpec();

    void centralizedExtraWakeup(const WKPointer &wk);

    bool hasTooManyPendingInsts();

    void advanceHead();

    DynInstPtr findBySeq(InstSeqNum seq);

    DynInstPtr getTail();

    bool isFull() const;

    unsigned getHeadPtr() const {return head;}

    unsigned getTailPtr() const {return tail;}

    unsigned getHeadTerm() const {return headTerm;}

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

    // dispatch
    bool insertBarrier(DynInstPtr &inst);

    bool insertNonSpec(DynInstPtr &inst);

    bool insert(DynInstPtr &inst, bool nonSpec);


    void insertForwardPointer(PointerPair pair);

    std::list<DynInstPtr> getBankHeads();

    std::list<DynInstPtr> getBankTails();

    bool stallToUnclog() const;

    void retireHead(bool isSquashed, FFRegValue v);

    bool isEmpty() const;

    unsigned numInDQ() const;

    unsigned numFree() const;

    void tryFastCleanup();

    void squash(DQPointer p, bool all, bool including);

    bool queuesEmpty();


    // wiring
    void setTimeBuf(TimeBuffer<DQStruct>* dqtb);

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

    void writebackLoad(DynInstPtr &inst);

    void wakeMemRelated(DynInstPtr &inst);

    void completeMemInst(DynInstPtr &inst);

    void violation(DynInstPtr store, DynInstPtr violator);

    // Drain, Switch, Initiation
    void takeOverFrom();

    void drainSanityCheck() const;

    void resetState();

    void resetEntries();

    void regStats();

    // deadlock squashing
    bool halfSquash;

    InstSeqNum halfSquashSeq;

    Addr halfSquashPC;

    unsigned numInFlightFw();

    void dumpFwQSize();

    void clearInflightPackets();

    void notImplemented();

    // Routing related:

};

}

#endif //GEM5_DATAFLOW_QUEUE_TOP_HH
