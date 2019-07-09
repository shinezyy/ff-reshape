//
// Created by zyy on 19-6-17.
//

#ifndef __FF_ALLOCATION_HH__
#define __FF_ALLOCATION_HH__

#include <deque>
#include <unordered_map>

#include "base/statistics.hh"
#include "cpu/timebuf.hh"
#include "dq_pointer.hh"

//#include "dyn_inst.hh"

struct DerivFFCPUParams;

namespace FF {

template <class Impl>
class Allocation
{

    typedef typename Impl::CPUPol CPUPol;
    typedef typename Impl::O3CPU O3CPU;

    typedef typename Impl::DynInstPtr DynInstPtr;
//    using DynInstPtr = BaseO3DynInst<Impl>;

    typedef typename Impl::CPUPol::FFDecodeStruct DecodeStruct;
    typedef typename Impl::CPUPol::FFAllocationStruct AllocationStruct;

public:
    void tick();

private:

    /** Calculates the number of free DQ entries. */
    inline int calcFreeDQEntries();

    /** Calculates the number of free LQ entries. */
    inline int calcFreeLQEntries();

    /** Calculates the number of free SQ entries. */
    inline int calcFreeSQEntries();
    /** Pointer to CPU. */
    O3CPU *cpu;

    typedef typename CPUPol::TimeStruct TimeStruct;

    /** Pointer to main time buffer used for backwards communication. */
    TimeBuffer<TimeStruct> *timeBuffer;

    /** Wire to get DIEWC's output from backwards time buffer. */
    typename TimeBuffer<TimeStruct>::wire fromDIEWC;

    /** Wire to write infromation heading to previous stages. */
    typename TimeBuffer<TimeStruct>::wire toDecode;

    /** Allocation instruction queue. */
    TimeBuffer<AllocationStruct> *allocationQueue;

    /** Wire to write any information heading to DIEWC. */
    typename TimeBuffer<AllocationStruct>::wire toDIEWC;

    unsigned toDIEWCIndex;

    unsigned allocationWidth;

    /** Decode instruction queue interface. */
    TimeBuffer<DecodeStruct> *decodeQueue;

    /** Wire to get decode's output from decode queue. */
    typename TimeBuffer<DecodeStruct>::wire fromDecode;

    bool wroteToTimeBuffer;

    bool blockThisCycle;

    int toIEWIndex;

    /** Whether or not rename needs to resume a serialize instruction
     * after squashing. */
    bool resumeSerialize;

    /** Whether or not rename needs to resume clearing out the skidbuffer
     * after squashing. */
    bool resumeUnblocking;

public:
    // A deque is used to queue the instructions. Barrier insts must
    // be added to the front of the queue, which is the only reason for
    // using a deque instead of a queue. (Most other stages use a
    // queue)
    typedef std::deque<DynInstPtr> InstQueue;

    /** Queue of all instructions coming from decode this cycle. */
    InstQueue insts;

    /** Skid buffer between rename and decode. */
    InstQueue skidBuffer;


private:
    void readInsts();

    /** Checks the signals and updates the status. */
    bool checkSignalsAndUpdate();

    bool checkStall();

    bool block();

    bool unblock();

    // do real allocation
    void allocate(bool &);

    void allocateInsts();

    bool canAllocate();

    DQPointer allocateDQEntry();

    void updateStatus();

    /** Count of instructions in progress that have been sent off to the DQ
     * and ROB, but are not yet included in their occupancy counts.
    */
    int instsInProgress;
    int loadsInProgress;
    int storesInProgress;

    void updateInProgress();

    void readFreeEntries();

    void readStallSignals();

public:

    enum OverallAllocationStatus {
        Active,
        Inactive
    };

    enum AllocationStatus {
        Running,
        Idle,
        Squashing,
        Blocked,
        Unblocking,
        SerializeStall
    };

    enum FullSource {
        DQ,
        LQ,
        SQ,
        NONE
    };

    bool diewcStall;

    bool emptyDQ;

  private:
    /** Rename status. */
    OverallAllocationStatus overallStatus;
    AllocationStatus allocationStatus;

    DynInstPtr serializeInst;

    bool serializeOnNextInst;

    void skidInsert();

    unsigned int validInsts();

    unsigned skidBufferMax;

    Stats::Scalar allocationBlockCycles;
    Stats::Scalar allocationSquashCycles;
    Stats::Scalar allocationSerializeStallCycles;
    Stats::Scalar allocatedSquashInsts;
    Stats::Scalar allocationDQFullEvents;
    Stats::Scalar allocatedSerilizing;
    Stats::Scalar allocatedTempSerilizing;
    Stats::Scalar allocatedSerializing;
    Stats::Scalar allocationAllocatedInsts;
    Stats::Scalar allocationSkidInsts;

public:
    void regStats();

    explicit Allocation(O3CPU*, DerivFFCPUParams*);

    std::string name() const;

    void setDecodeQueue(TimeBuffer<DecodeStruct> *dcq);

    void setAllocQueue(TimeBuffer<AllocationStruct> *alq);

private:
    unsigned flatHead, flatTail;

    DQPointer PositionfromUint(unsigned);

    const unsigned int indexWidth;
    const unsigned int indexMask;
    const unsigned int bankMask;
    const unsigned int dqSize;

    bool backendStall;

    struct FreeEntries {
        unsigned dqEntries;
        unsigned lqEntries;
        unsigned sqEntries;
    } freeEntries;

    void incrFullStat(FullSource source);

    void serializeAfter(InstQueue &deque);

};


}


#endif //__FF_ALLOCATION_HH__
