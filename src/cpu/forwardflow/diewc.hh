//
// Created by zyy on 19-6-11.
//

#ifndef __FF_DIEWC_HH__
#define __FF_DIEWC_HH__

#include <queue>

#include "cpu/forwardflow/cpu.hh"
#include "cpu/forwardflow/dataflow_queue.hh"
#include "cpu/forwardflow/dyn_inst.hh"
#include "cpu/forwardflow/lsq.hh"
#include "cpu/forwardflow/thread_state.hh"
#include "cpu/timebuf.hh"

namespace FF{

template<class Impl>
class FFDIEWC //dispatch, issue, execution, writeback, commit
{

public:
    //Typedefs from Impl
    typedef typename Impl::CPUPol CPUPol;

//    typedef typename Impl::DynInstPtr DynInstPtr;
    using DynInstPtr = BaseO3DynInst<Impl>*;
//    typedef typename Impl::O3CPU XFFCPU;
    using XFFCPU = FFCPU<Impl>;
//    typedef typename CPUPol::LSQ XLSQ;
    using XLSQ = LSQ<Impl>;
//    typedef typename CPUPol::DataflowQueues DataflowQueues;
    using XDataflowQueues = DataflowQueues<Impl>;

    typedef typename CPUPol::FFAllocationStruct AllocationStruct;
    typedef typename CPUPol::DIEWC2DIEWC DIEWC2DIEWC;
    typedef typename CPUPol::TimeStruct TimeStruct;
    typedef typename CPUPol::ArchState ArchState;
    typedef typename CPUPol::FUWrapper FUWrapper;

    typedef O3ThreadState<Impl> Thread;

private:
    /** Decode instruction queue interface. */
    TimeBuffer<AllocationStruct> *allocationQueue;

    /** Wire to get decode's output from decode queue. */
    typename TimeBuffer<AllocationStruct>::wire fromAllocation;

    TimeBuffer<DIEWC2DIEWC> *localQueue;

    typename TimeBuffer<DIEWC2DIEWC>::wire toNextCycle;

    typename TimeBuffer<DIEWC2DIEWC>::wire fromLastCycle;

    TimeBuffer<TimeStruct> *backwardTB;

    typename TimeBuffer<TimeStruct>::wire toFetch;
    typename TimeBuffer<TimeStruct>::wire fromCommit;
    typename TimeBuffer<TimeStruct>::wire toAllocation;

    /** Variable that tracks if decode has written to the time buffer this
     * cycle. Used to tell CPU if there is activity this cycle.
     */
    bool wroteToTimeBuffer;

    /** Structures whose free entries impact the amount of instructions that
     * can be renamed.
     */
    struct FreeEntries {
        unsigned dqEntries;
        unsigned lqEntries;
        unsigned sqEntries;
    };

    FreeEntries freeEntries;

    /** Records if pipeline needs to serialize on the next instruction for any
     * thread.
     */
    bool serializeOnNextInst;

    XDataflowQueues dq;

    ArchState archState;

    XLSQ ldstQueue;

    FUWrapper fuWrapper;

    typedef std::queue<DynInstPtr> InstQueue;
    InstQueue insts_from_allocation;
    InstQueue skidBuffer;

    std::queue<PointerPair> pointerPackets;

    InstQueue insts_to_commit;

public:
    void tick();

private:
    void squash(DynInstPtr &inst);

    void checkSignalsAndUpdate();

    void readInsts();

    void tryDispatch();

    void dispatch();

    void execute();

    void forward();

    void writeCommitQueue();

    void commit();

    void advanceQueues();

public:
    /** Overall IEW stage status. Used to determine if the CPU can
     * deschedule itself due to a lack of activity.
     */
    enum OverallStatus {
        Active,
        Inactive
    };

    /** Status for Issue, Execute, and Writeback stages. */
    enum DispatchStatus {
        Running,
        Blocked,
        Idle,
        Squashing,
        Unblocking
    };


    /** Individual thread status. */
    enum CommitStatus {
        CommitRunning,
        CommitIdle,
        TrapPending,
        FetchTrapPending,
        SquashAfterPending, //< Committing instructions before a squash.
        DQSquashing,
    };


private:
    /** Overall stage status. */
    OverallStatus _status;
    /** Dispatch status. */
    DispatchStatus dispatchStatus;

    CommitStatus commitStatus;

    bool fetchRedirect;

    bool dqSqushing;

    void clearAllocatedInts();

    bool checkStall();

    void block();

    void unblock();

    bool validInstsFromAllocation();

    void skidInsert();

private:
    unsigned dispatchWidth;

    // todo: change them from int to Stat
    int dispSquashedInsts;
    int dqFullEvents;
    int lqFullEvents;
    int sqFullEvents;
    int dispaLoads;
    int dispStores;
    int dispNonSpecInsts;
    int dispatchedInsts;
    int blockCycles;
    int squashCycles;
    int runningCycles;
    int unblockCycles;
    bool updatedQueues;

    unsigned skidBufferMax;

    unsigned width;

    bool commitHead(DynInstPtr &head_inst, unsigned inst_num);

    int commitNonSpecStalls;
    bool committedStores;
    bool hasStoresToWB;

    Thread *thread;

    XFFCPU *cpu;

    void generateTrapEvent(ThreadID tid, Fault inst_fault);

    void processTrapEvent(ThreadID tid);

    bool trapInFlight;

    bool trapSquash;

    const Cycles trapLatency;

    void commitInsts();

    Fault interrupt;

    void handleInterrupt();

    DynInstPtr &getHeadInst();

    int commitSquashedInsts;
    bool changedDQNumEntries;
    ExecContext::PCState pc;
    bool canHandleInterrupts;
    InstSeqNum lastCommitedSeqNum;

    void squashAfter(DynInstPtr &inst);

    bool drainPending;
    bool drainImminent;
    bool skipThisCycle;
    bool avoidQuiesceLiveLock;

    DynInstPtr squashAfterInst;

    void handleSquash();

    InstSeqNum youngestSeqNum;

    int branchMispredicts;
};


}


#endif //__FF_DIEWC_HH__
