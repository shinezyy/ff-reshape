//
// Created by zyy on 19-6-11.
//

#ifndef __FF_DIEWC_HH__
#define __FF_DIEWC_HH__

#include <queue>
#include <tuple>

#ifdef __CLION_CODING__
#include "cpu/ff_base_dyn_inst.hh"
#include "cpu/forwardflow/arch_state.hh"
#include "cpu/forwardflow/dataflow_queue_top.hh"
#include "cpu/forwardflow/dyn_inst.hh"
#include "cpu/forwardflow/lsq.hh"

#endif

#include "base/statistics.hh"
#include "config/the_isa.hh"
#include "cpu/fanout_pred.hh"
#include "cpu/forwardflow/comm.hh"
#include "cpu/forwardflow/thread_state.hh"
#include "cpu/pred/mem_dep_pred.hh"
#include "cpu/timebuf.hh"

struct DerivFFCPUParams;

namespace FF{

template<class Impl>
class FFDIEWC //dispatch, issue, execution, writeback, commit
{

public:
    //Typedefs from Impl


    typedef typename Impl::CPUPol CPUPol;

#ifdef __CLION_CODING__
    template<class Impl>
    class FullInst: public BaseDynInst<Impl>, public BaseO3DynInst<Impl> {};
    using DynInstPtr = FullInst<Impl>*;

    using XFFCPU = FFCPU<Impl>;
    using XLSQ = LSQ<Impl>;
    using DQTop = DQTop<Impl>;
    using ArchState = ArchState<Impl>;
#else
    typedef typename Impl::DynInst DynInst;
    typedef typename Impl::DynInstPtr DynInstPtr;
    typedef typename Impl::O3CPU XFFCPU;
    typedef typename CPUPol::LSQ XLSQ;
    typedef typename CPUPol::DQTop DQTop;
    typedef typename CPUPol::ArchState ArchState;
#endif


//    typedef typename CPUPol::DIEWC2DIEWC DIEWC2DIEWC;
    typedef typename CPUPol::TimeStruct TimeStruct;
    typedef typename CPUPol::FetchStruct FetchStruct;
    typedef typename CPUPol::FFAllocationStruct AllocationStruct;
    typedef typename CPUPol::FUWrapper FUWrapper;

    typedef O3ThreadState<Impl> Thread;


private:
    XFFCPU *cpu;

    /** Decode instruction queue interface. */
    TimeBuffer<AllocationStruct> *allocationQueue;

    /** Wire to get decode's output from decode queue. */
    typename TimeBuffer<AllocationStruct>::wire fromAllocation;

//    TimeBuffer<DIEWC2DIEWC> *localQueue;


    TimeBuffer<TimeStruct> *backwardTB;

    typename TimeBuffer<TimeStruct>::wire toNextCycle;

    typename TimeBuffer<TimeStruct>::wire fromLastCycle;

    typename TimeBuffer<TimeStruct>::wire toFetch;

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

    DQTop dq;

    ArchState archState;

    FUWrapper fuWrapper;

    typedef std::deque<DynInstPtr> InstQueue;
    InstQueue insts_from_allocation;
    InstQueue skidBuffer;

    std::queue<PointerPair> pointerPackets;

    std::list<DynInstPtr> instsToCommit;

    void readInstsToCommit();

public:
    XLSQ ldstQueue;

    void tick();

    void squash(const DynInstPtr &inst);

    void squashInFlight();

    unsigned numInWindow();

    void takeOverFrom();

    void drainSanityCheck() const;

    bool isDrained() const;

    void drain();

    void drainResume();

    void startupStage();

    void regProbePoints();

    void resetEntries();

private:

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

    bool dqSquashing;

    InstSeqNum dqSquashSeq;

    bool checkStall();

    void block();

    void unblock();

    bool validInstsFromAllocation();

    void skidInsert();

private:
    unsigned dispatchWidth;

    // todo: change them from int to Stat

    bool updatedQueues;

    unsigned skidBufferMax;

    unsigned width;

    bool commitHead(const DynInstPtr &head_inst, unsigned inst_num);

    bool committedStores;

    Thread *thread;

public:
    void generateTrapEvent(ThreadID tid, Fault inst_fault);

    void generateTCEvent(ThreadID tid);

    void processTrapEvent(ThreadID tid);

private:

    bool trapInFlight;

    bool trapSquash;

    const Cycles trapLatency;

    void commitInsts();

    Fault interrupt;

    void handleInterrupt();

    DynInstPtr getTailInst();


    bool changedDQNumEntries;

    TheISA::PCState pc;

    bool canHandleInterrupts;
    InstSeqNum lastCommitedSeqNum;

    void squashAfter(const DynInstPtr &inst);

    bool drainPending;
    bool drainImminent;
    bool skipThisCycle;
    bool avoidQuiesceLiveLock;

    DynInstPtr squashAfterInst;

    void handleSquash();

    InstSeqNum youngestSeqNum;

    /** Probe points. */
    ProbePointArg<DynInstPtr> *ppMispredict;

    ProbePointArg<DynInstPtr> *ppDispatch;
    /** To probe when instruction execution begins. */
    ProbePointArg<DynInstPtr> *ppExecute;
    /** To probe when instruction execution is complete. */
    ProbePointArg<DynInstPtr> *ppToCommit;
    ProbePointArg<DynInstPtr> *ppCommit;
    ProbePointArg<DynInstPtr> *ppCommitStall;
    ProbePointArg<DynInstPtr> *ppSquash;

    /** Distribution of the number of committed instructions each cycle. */
    Stats::Distribution numCommittedDist;
    /** Number of cycles where the commit bandwidth limit is reached. */
    Stats::Scalar commitEligibleSamples;
    /** Committed instructions by instruction type (OpClass) */
    Stats::Vector statCommittedInstType;

    bool tcSquash;

    /** Handles squashing due to a trap. */
    void squashFromTrap();

    /** Handles squashing due to an TC write. */
    void squashFromTC();

    /** Handles a squash from a squashAfter() request. */
    void squashFromSquashAfter();

    /** Squashes all in flight instructions. */
    void squashAll();

    void clearAllocatedInsts();
public:
    FFDIEWC(XFFCPU*, const DerivFFCPUParams *);

    void cacheUnblocked();

     /** Tells memory dependence unit that a memory instruction needs to be
     * rescheduled. It will re-execute once replayMemInst() is called.
     */
    void rescheduleMemInst(const DynInstPtr &inst, bool isStrictOrdered, bool isFalsePositive = false);

    /** Re-executes all rescheduled memory instructions. */
    void replayMemInst(const DynInstPtr &inst);

    /** Moves memory instruction onto the list of cache blocked instructions */
    void blockMemInst(const DynInstPtr &inst);

    // this
    void instToWriteback(const DynInstPtr &inst);

    bool updateLSQNextCycle;

    void activityThisCycle();

    void wakeCPU();

    void checkMisprediction(const DynInstPtr &inst);

    std::string name() const;

    void regStats();

        /** Sets the PC of a specific thread. */
    void pcState(const TheISA::PCState &val)
    { pc = val; }

    /** Reads the PC of a specific thread. */
    TheISA::PCState pcState() { return pc; }

    /** Returns the PC of a specific thread. */
    Addr instAddr() { return pc.instAddr(); }

    /** Returns the next PC of a specific thread. */
    Addr nextInstAddr() { return pc.nextInstAddr(); }

    /** Reads the micro PC of a specific thread. */
    Addr microPC() { return pc.microPC(); }

    /** Sets pointer to list of active threads. */
    void setActiveThreads(std::list<ThreadID> *at_ptr);

    /** Sets the main time buffer pointer, used for backwards communication. */
    void setTimeBuffer(TimeBuffer<TimeStruct> *tb_ptr);

    void setFetchQueue(TimeBuffer<FetchStruct> *fq_ptr);

    void setAllocQueue(TimeBuffer<AllocationStruct> *aq_ptr);

    /** Sets the list of threads. */
    void setThreads(std::vector<Thread *> &threads);

    /** Deschedules a thread from scheduling */
    void deactivateThread(ThreadID tid);

    DynInstPtr readTailInst(ThreadID tid);

private:
    void updateComInstStats(const DynInstPtr &ffdiewc);

    void insertPointerPairs(const std::list<PointerPair>&);

    void insertPointerPair(const PointerPair&);

    void squashDueToBranch(const DynInstPtr &ffdiewc);

    std::list <ThreadID> *activeThreads;

    unsigned allocationToDIEWCDelay;

    void clearAtStart();

    void clearAtEnd();
public:
    // stats
    Stats::Scalar dispSquashedInsts;
    Stats::Scalar dqFullEvents;
    Stats::Scalar lqFullEvents;
    Stats::Scalar sqFullEvents;
    Stats::Scalar dispaLoads;
    Stats::Scalar dispStores;
    Stats::Scalar dispNonSpecInsts;
    Stats::Scalar dispatchedInsts;
    Stats::Scalar blockCycles;
    Stats::Scalar squashCycles;
    Stats::Scalar runningCycles;
    Stats::Scalar unblockCycles;
    Stats::Scalar commitNonSpecStalls;
    Stats::Scalar commitSquashedInsts;
    Stats::Scalar branchMispredicts;
    Stats::Scalar predictedTakenIncorrect;
    Stats::Scalar predictedNotTakenIncorrect;

    Stats::Scalar memOrderViolationEvents;

    Stats::Scalar instsCommitted;
    Stats::Scalar forwardersCommitted;
    Stats::Scalar opsCommitted;
    Stats::Scalar statComBranches;
    Stats::Scalar statComRefs;
    Stats::Scalar statComLoads;
    Stats::Scalar statComMembars;
    Stats::Scalar statComInteger;
    Stats::Scalar statComFloating;
    Stats::Scalar statComVector;
    Stats::Scalar statComFunctionCalls;

    Stats::Scalar iewExecutedInsts;
    Stats::Scalar iewExecutedBranches;
    Stats::Scalar iewExecutedRefs;
    Stats::Scalar iewExecLoadInsts;
    Stats::Formula iewExecStoreInsts;

    Stats::Scalar totalFanoutPredictions;
    Stats::Scalar largeFanoutInsts;
    Stats::Scalar falseNegativeLF;
    Stats::Scalar falsePositiveLF;
    Stats::Formula fanoutMispredRate;

    Stats::Scalar firstLevelFw;
    Stats::Scalar secondaryLevelFw;

    Stats::Scalar gainFromReshape;
    Stats::Scalar reshapeContrib;
    Stats::Scalar nonCriticalForward;
    Stats::Scalar negativeContrib;

    Stats::Scalar wkDelayedCycles;

    Stats::Scalar ssrDelay;
    Stats::Scalar queueingDelay;
    Stats::Scalar pendingDelay;
    Stats::Scalar FUContentionDelay;

    Stats::Scalar HeadNotExec;
    Stats::Scalar headExecDistance;
    Stats::Formula meanHeadExecDistance;

    Stats::Scalar readyExecDelayTicks;
    Stats::Scalar readyInBankDelay;
    Stats::Scalar headReadyExecDelayTicks;
    Stats::Scalar headReadyNotExec;

    Stats::Scalar TNBypass;
    Stats::Scalar TPBypass;
    Stats::Scalar FNBypass;
    Stats::Scalar FPBypass;
    Stats::Scalar FPCanceledBypass;
    Stats::Scalar FPSquashedBypass;
    Stats::Formula loadSquashRate;

    Stats::Scalar reExecutedLoads;
    Stats::Scalar reExecutedBypass;
    Stats::Scalar reExecutedNonBypass;
    Stats::Formula loadReExecRate;

    Stats::Scalar verificationSkipped;
    Stats::Formula verifSkipRate;

    Stats::Scalar headNotVerified;

    ArchState *getArchState() {return &archState;}

    DQTop *getDQ() {return &dq;}

    void executeInst(const DynInstPtr &inst);

    void squashDueToMemMissPred(const DynInstPtr &violator);

private:
    void sendBackwardInfo();

    unsigned oldestForwarded;

public:
    void setOldestFw(BasePointer _ptr);

    void resetOldestFw();

    InstSeqNum getOldestFw();

    bool DQPointerJumped;

    Addr toCheckpoint;

    bool cptHint{false};

    void setFanoutPred(FanoutPred *fanoutPred1);

    void tryResetRef();

  private:
    const unsigned commitTraceInterval;
    unsigned commitCounter;

    void checkDQHalfSquash();

    void updateExeInstStats(const DynInstPtr &inst);

    bool anySuccessfulCommit;

    FanoutPred *fanoutPred;

    const unsigned largeFanoutThreshold;

    DynInstPtr insertForwarder(const DynInstPtr &inst, const DynInstPtr &anchor);

    bool EnableReshape;

    uint64_t commitAll{};

    InstSeqNum youngestExecuted{};

    MemDepPredictor *mDepPred;

    InstSeqNum verifiedTailLoad{0};
    InstSeqNum bypassCanceled{0};

    InstSeqNum scheduledNonSpec{0};

    void tryVerifyTailLoads();

    bool tryVerifyTailLoad(const DynInstPtr &load, bool is_tail);


    void setUpLoad(const DynInstPtr &inst);

    InstSeqNum lastCompletedStoreSN{};

    InstSeqNum lastCompletedStoreSSN{};

  public:
    void setStoreCompleted(InstSeqNum sn, Addr eff_addr, InstSeqNum ssn);

    InstSeqNum getLastCompletedStoreSN() {return lastCompletedStoreSSN;}
  private:

    bool checkViolation(const DynInstPtr &inst);

    void setupPointerLink(const DynInstPtr &inst, bool jumped, const PointerPair &pair);
  public:
    void squashLoad(const DynInstPtr &inst);
};


}


#endif //__FF_DIEWC_HH__
