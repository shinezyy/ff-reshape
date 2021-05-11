#ifndef __CPU_O3_BASE_FETCH_STAGE_HH__
#define __CPU_O3_BASE_FETCH_STAGE_HH__

#include "cpu/o3/decoupledIO.hh"
#include "cpu/o3/fetch.hh"
#include "cpu/o3/pipeline_fetch.hh"
#include "params/DerivO3CPU.hh"

template <class Impl>
class BaseFetchStage
{
  public:
    /** Typedefs from Impl. */
    typedef typename Impl::CPUPol CPUPol;
    typedef typename Impl::DynInst DynInst;
    typedef typename Impl::DynInstPtr DynInstPtr;
    typedef typename Impl::O3CPU O3CPU;

    /** Typedefs from the CPU policy. */
    typedef typename CPUPol::FetchStruct FetchStruct;
    typedef typename CPUPol::TimeStruct TimeStruct;

    /** Overall fetch status. Used to determine if the CPU can
     * deschedule itsef due to a lack of activity.
     */
    enum FetchStatus {
        Active,
        Inactive
    };

    /** Individual thread status. */
    enum ThreadStatus {
        Running,
        Idle,
        Squashing,
        Blocked,
        Fetching,
        TrapPending,
        QuiescePending,
        ItlbWait,
        IcacheWaitResponse,
        IcacheWaitRetry,
        IcacheAccessComplete,
        NoGoodAddr
    };

  public:
    TheISA::PCState pcReg[Impl::MaxThreads];

    /** Size of instructions. */
    int instSize;

    /** Fetch status. */
    FetchStatus _status;

    /** Per-thread status. */
    ThreadStatus fetchStatus[Impl::MaxThreads];

    /** List of Active Threads */
    std::list<ThreadID> *activeThreads;

    /** Number of threads. */
    ThreadID numThreads;

    /** Number of threads that are actively fetching. */
    ThreadID numFetchingThreads;

    /** Thread ID being fetched. */
    ThreadID threadFetched;

    /** The size of the fetch buffer in bytes. The fetch buffer
     *  itself may be smaller than a cache line.
     */
    unsigned fetchBufferSize;

    /** Mask to align a fetch address to a fetch buffer boundary. */
    Addr fetchBufferMask;

  public:
    /** BaseFetchStage constructor. */
    BaseFetchStage(O3CPU *_cpu, const DerivO3CPUParams &params, PipelineFetch<Impl> *upper);

    /** Connect pipeline with next stage. */
    void connNextStage(BaseFetchStage<Impl> *next);

    /** Returns the name of fetch. */
    virtual std::string name() const;

    /** Sets the main backwards communication time buffer pointer. */
    void setTimeBuffer(TimeBuffer<TimeStruct> *time_buffer);

    /** Squashes a specific thread and resets the PC. Also tells the CPU to
     * remove any instructions that are not in the ROB. The source of this
     * squash should be the commit stage.
     */
    void squash(const TheISA::PCState &newPC, const InstSeqNum seq_num,
                DynInstPtr squashInst, ThreadID tid);

    /** Ticks the fetch stage, processing all inputs signals and fetching
     * as many instructions as possible.
     */
    void tick(bool &status_change);

    /** Save valid and ready values in this tick,
     *  can be use in next tick
     */
    void advance();

    virtual void fetch(bool &status_change)
    { printf("This function shouldn't be call\n"); }

    /** Checks all input signals and updates the status as necessary.
     *  @return: Returns if the status has changed due to input signals.
     */
    // bool checkSignalsAndUpdate(ThreadID tid);

    /** Checks if a thread is stalled. */
    // bool checkStall(ThreadID tid) const;

    /** Align a PC to the start of a fetch buffer block. */
    Addr bufferAlignPC(Addr addr, Addr mask)
    {
        return (addr & ~mask);
    }

    /** Returns the appropriate thread to fetch, given the fetch policy. */
    ThreadID getFetchingThread();

    /** Sets fetchStatus of active threads. */
    void setFetchStatus(ThreadStatus status, ThreadID tid){
      fetchStatus[tid] = status;
    }

    std::string printStatus(int status);

  protected:
    /** Pointer to the O3CPU. */
    O3CPU *cpu;

    PipelineFetch<Impl> *upper;

    DecoupledIO *thisStage;
    DecoupledIO *nextStage;
    DecoupledIO *prevStage;

    /** Variable that tracks if fetch has written to the time buffer this
     * cycle. Used to tell CPU if there is activity this cycle.
     */
    bool wroteToTimeBuffer;

    /** Updates overall fetch stage status; to be called at the end of each
     * cycle. */
    FetchStatus updateFetchStatus();
};

template <class Impl>
class FetchStage1 : public BaseFetchStage<Impl>
{
  public:
    typedef typename Impl::O3CPU O3CPU;

    FetchStage1(O3CPU *_cpu, const DerivO3CPUParams &params, PipelineFetch<Impl> *upper)
      : BaseFetchStage<Impl>(_cpu, params, upper) {}

    virtual void fetch(bool &status_change) override;

    virtual std::string name() const override;
};

template <class Impl>
class FetchStage2 : public BaseFetchStage<Impl>
{
  public:
    typedef typename Impl::O3CPU O3CPU;

    FetchStage2(O3CPU *_cpu, const DerivO3CPUParams &params, PipelineFetch<Impl> *upper)
      : BaseFetchStage<Impl>(_cpu, params, upper) {}

    virtual void fetch(bool &status_change) override;

    virtual std::string name() const override;
};

template <class Impl>
class FetchStage3 : public BaseFetchStage<Impl>
{
  public:
    typedef typename Impl::O3CPU O3CPU;

    FetchStage3(O3CPU *_cpu, const DerivO3CPUParams &params, PipelineFetch<Impl> *upper)
      : BaseFetchStage<Impl>(_cpu, params, upper), hasData(false), lastHasData(false) {}

    virtual void fetch(bool &status_change) override;

    virtual std::string name() const override;

  private:
    bool hasData;
    bool lastHasData;

    uint8_t *cacheData;
};

template <class Impl>
class FetchStage4 : public BaseFetchStage<Impl>
{
  private:
    /** BPredUnit. */
    BPredUnit *branchPred;

    StaticInstPtr staticInst;

    /** The decoder. */
    TheISA::Decoder *decoder[Impl::MaxThreads];

    int numInst;

    bool lastDecodeStall;

    Addr fetchOffset[Impl::MaxThreads];

    TheISA::MachInst *cacheInsts;

  public:
    typedef typename Impl::O3CPU O3CPU;

    FetchStage4(O3CPU *_cpu, const DerivO3CPUParams &params, PipelineFetch<Impl> *upper)
      : BaseFetchStage<Impl>(_cpu, params, upper),
        numInst(0),
        lastDecodeStall(false)
    {
      for (int i = 0; i < Impl::MaxThreads; i++) {
        decoder[i] = nullptr;
      }

      for (ThreadID tid = 0; tid < this->upper->numThreads; tid++) {
          decoder[tid] = new TheISA::Decoder(
                  dynamic_cast<TheISA::ISA *>(params.isa[tid]));
      }

      branchPred = upper->branchPred;
    }

    typedef typename Impl::DynInstPtr DynInstPtr;

    virtual std::string name() const override;

  public:
    virtual void fetch(bool &status_change) override;

    bool lookupAndUpdateNextPC(const DynInstPtr &inst, TheISA::PCState &pc);
};

#endif //__CPU_O3_BASE_FETCH_STAGE_HH__