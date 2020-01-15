//
// Created by zyy on 2020/1/15.
//

#ifndef GEM5_DATAFLOW_QUEUE_TOP_HH
#define GEM5_DATAFLOW_QUEUE_TOP_HH


#include <cstdint>
#include <deque>
#include <random>
#include <unordered_map>
#include <vector>

#include "cpu/forwardflow/comm.hh"
#include "cpu/forwardflow/crossbar.hh"
#include "cpu/forwardflow/crossbar_dedi_dest.hh"
#include "cpu/forwardflow/crossbar_narrow.hh"
#include "cpu/forwardflow/network.hh"
#include "cpu/timebuf.hh"
#include "fu_pool.hh"

namespace FF{

template <class Impl>
class DQTop
{
    DQTop();

    typedef typename Impl::DynInstPtr DynInstPtr;

    void cycleStart();

    void tick();

    /** Re-executes all rescheduled memory instructions. */
    void replayMemInst(DynInstPtr &inst);

    DynInstPtr getTail();

    void scheduleNonSpec();

    bool isFull() const;

    bool hasTooManyPendingInsts();

    void advanceHead();

    DynInstPtr findBySeq(InstSeqNum seq);


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


    typedef typename Impl::CPUPol::DIEWC DIEWC;
    typedef typename Impl::CPUPol::MemDepUnit MemDepUnit;
    typedef typename Impl::CPUPol::DQStruct DQStruct;
    typedef typename Impl::CPUPol::FUWrapper FUWrapper;
    typedef typename Impl::CPUPol::LSQ LSQ;
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

    void rescheduleMemInst(DynInstPtr &inst, bool isStrictOrdered, bool isFalsePositive = false);

    void replayMemInsts();

    void blockMemInst(DynInstPtr &inst);

    void cacheUnblocked();

    void writebackLoad(DynInstPtr &inst);

    void wakeMemRelated(DynInstPtr &inst);

    void completeMemInst(DynInstPtr &inst);

    void violation(DynInstPtr store, DynInstPtr violator);



    void takeOverFrom();

    void drainSanityCheck() const;

    void resetEntries();

    void regStats();


    // deadlock squashing
    bool halfSquash;

    InstSeqNum halfSquashSeq;

    Addr halfSquashPC;

    unsigned numInFlightFw();

    void dumpFwQSize();

};

}

#endif //GEM5_DATAFLOW_QUEUE_TOP_HH
