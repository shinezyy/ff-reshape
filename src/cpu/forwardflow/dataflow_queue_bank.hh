//
// Created by zyy on 2020/1/15.
//

#ifndef GEM5_DATAFLOW_QUEUE_BANK_HH
#define GEM5_DATAFLOW_QUEUE_BANK_HH

#include <cstdint>
#include <deque>
#include <random>
#include <unordered_map>
#include <vector>

#ifdef __CLION_CODING__
#include "cpu/forwardflow/dataflow_queue.hh"
#include "cpu/forwardflow/dataflow_queue_top.hh"
#include "cpu/forwardflow/dyn_inst.hh"

#endif

#include "cpu/forwardflow/comm.hh"
#include "cpu/forwardflow/crossbar.hh"
#include "cpu/forwardflow/crossbar_dedi_dest.hh"
#include "cpu/forwardflow/crossbar_narrow.hh"
#include "cpu/forwardflow/network.hh"
#include "cpu/timebuf.hh"
#include "fu_pool.hh"
#include "params/DerivFFCPU.hh"

namespace FF {
template<class Impl>
class DataflowQueueBank {

    typedef typename Impl::CPUPol CPUPolicy;

public:
#ifdef __CLION_CODING__
    typedef DataflowQueues<Impl> DQ;
    typedef DQTop<Impl> DQTop;

    template<class Impl>
    class FullInst: public BaseDynInst<Impl>, public BaseO3DynInst<Impl> {};
    using DynInstPtr = FullInst<Impl>*;

#else
    typedef typename Impl::CPUPol::DataflowQueues DQ;
    typedef typename CPUPolicy::DQTop DQTop;
    typedef typename Impl::DynInstPtr DynInstPtr;
#endif

    typedef typename Impl::CPUPol::ReadyInstsQueue XReadyInstsQueue;

private:

    DQ *dq;

    const unsigned nOps{4};

    const unsigned depth;

    const DQPointer nullDQPointer;

    std::vector<DynInstPtr> instArray;

    // instructions that waiting for only one operands
    boost::dynamic_bitset<> nearlyWakeup;

    // instructions that waiting for only one operands
    // and the wakeup pointer has already arrived
public:
    std::vector<WKPointer> pendingWakeupPointers;

    std::vector<WKPointer> localWKPointers;

private:
    bool anyPending;

    // input forward pointers
    std::vector<WKPointer> inputPointers;

    // output forward pointers
    // init according to nops

    std::vector<WKPointer> outputPointers;

    unsigned tail;

    unsigned readyQueueSize;

    std::deque<BasePointer> readyQueue;

public:
    void advanceTail();

    void setTail(unsigned t);

    explicit DataflowQueueBank(DerivFFCPUParams *params,
            unsigned bank_id, DQ *dq, DQTop *_top);

    bool canServeNew();

    bool wakeup(WKPointer pointer);

    DynInstPtr wakeupInstsFromBank();

    std::vector<WKPointer> readPointersFromBank();

    DynInstPtr readInstsFromBank(const BasePointer &pointer) const;

    void writeInstsToBank(const BasePointer &pointer, const DynInstPtr &inst);

    void checkReadiness(BasePointer pointer);

    DynInstPtr findInst(InstSeqNum) const;

    void checkPending();

    std::vector<std::array<DQPointer, 4>> prematureFwPointers;

    void resetState();

    DynInstPtr tryWakeTail();

    DynInstPtr tryWakeDirect();

    unsigned bankID;

    std::string _name;

    std::string name() const { return _name; }

    void clear(bool markSquashed);

    void erase(BasePointer p, bool markSquashed);

    void cycleStart();

    void clearPending(const DynInstPtr &inst);

    DQPointer extraWakeupPointer;

    void printTail();

//    void setNearlyWakeup(DQPointer ptr);

    //stats:
    Stats::Scalar wakenUpByPointer;
    Stats::Scalar wakenUpAtTail;
    Stats::Scalar directReady;
    Stats::Scalar directWakenUp;


    void regStats();

    bool hasTooManyPendingInsts();

    void squashReady(const BasePointer &squash_from);

    bool servedForwarder;

    bool servedNonForwarder;

    void dumpOutPointers() const;

    void dumpInputPointers() const;

    void countUpPendingPointers();

    void countUpPendingInst();

    XReadyInstsQueue *readyInstsQueue;

    void mergeLocalWKPointers();

public:
    void setTop(DQTop *_top) {top = _top;}

private:
    DQTop *top;

public:
    Stats::Scalar SRAMWriteInst;
    Stats::Scalar SRAMReadInst;
    Stats::Scalar SRAMReadPointer;
    Stats::Scalar SRAMWriteValue;
    Stats::Scalar SRAMReadValue;
    Stats::Scalar RegWriteValid;
    Stats::Scalar RegReadValid;
    Stats::Scalar RegWriteNbusy;
    Stats::Scalar RegReadNbusy;
    Stats::Scalar RegWriteRxBuf;
    Stats::Scalar RegReadRxBuf;
    Stats::Scalar QueueReadReadyInstBuf;
    Stats::Scalar QueueWriteReadyInstBuf;
};

}

#endif //GEM5_DATAFLOW_QUEUE_BANK_HH