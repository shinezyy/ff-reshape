#ifndef __CPU_O3_FETCH_PIPE_HH__
#define __CPU_O3_FETCH_PIPE_HH__

#include "cpu/o3/fetch.hh"
#include "params/DerivO3CPU.hh"

template <class Impl>
class FetchStage1;

template <class Impl>
class FetchStage2;

template <class Impl>
class FetchStage3;

template <class Impl>
class FetchStage4;

template <class Impl>
class PipelineFetch : public DefaultFetch<Impl>
{
  public:
    /** Typedefs from Impl. */
    typedef typename Impl::CPUPol CPUPol;
    typedef typename Impl::DynInst DynInst;
    typedef typename Impl::DynInstPtr DynInstPtr;
    typedef typename Impl::O3CPU O3CPU;

    typedef typename CPUPol::FetchStageStruct FetchStageStruct;

    /** PipelineFetch constructor. */
    PipelineFetch(O3CPU *_cpu, const DerivO3CPUParams &params);

    /** Ticks the fetch stage, processing all inputs signals and fetching
     * as many instructions as possible.
     */
    void tick();

    // std::list<ThreadID> *getActiveThreads() {
    //   return this->activeThreads;
    // }

    /**
     * Fetches the cache line that contains the fetch PC.  Returns any
     * fault that happened.  Puts the data into the class variable
     * fetchBuffer, which may not hold the entire fetched cache line.
     * @param vaddr The memory address that is being fetched from.
     * @param ret_fault The fault reference that will be set to the result of
     * the icache access.
     * @param tid Thread id.
     * @param pc The actual PC of the current instruction.
     * @return Any fault that occured.
     */
    // bool fetchCacheLine(Addr vaddr, ThreadID tid, Addr pc);
    // void finishTranslation(const Fault &fault, const RequestPtr &mem_req);

  public:
    FetchStage1<Impl> *fetch1;
    FetchStage2<Impl> *fetch2;
    FetchStage3<Impl> *fetch3;
    FetchStage4<Impl> *fetch4;

    TimeBuffer<FetchStageStruct> toFetch1Buffer;
    TimeBuffer<FetchStageStruct> toFetch2Buffer;
    TimeBuffer<FetchStageStruct> toFetch3Buffer;
    TimeBuffer<FetchStageStruct> toFetch4Buffer;

    typename TimeBuffer<FetchStageStruct>::wire toFetch1;
    typename TimeBuffer<FetchStageStruct>::wire toFetch2;
    typename TimeBuffer<FetchStageStruct>::wire toFetch3;
    typename TimeBuffer<FetchStageStruct>::wire toFetch4;

    typename TimeBuffer<FetchStageStruct>::wire fromFetch1;
    typename TimeBuffer<FetchStageStruct>::wire fromFetch2;
    typename TimeBuffer<FetchStageStruct>::wire fromFetch3;
    typename TimeBuffer<FetchStageStruct>::wire fromFetch4;
};


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

  protected:
    TheISA::PCState pc[Impl::MaxThreads];

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
    BaseFetchStage(O3CPU *_cpu, const DerivO3CPUParams &params);

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
    void tick(PipelineFetch<Impl> *upper);

    virtual void fetch(bool &status_change, PipelineFetch<Impl> *upper)
    { printf("This function shouldn't be call\n"); }

    /** Checks all input signals and updates the status as necessary.
     *  @return: Returns if the status has changed due to input signals.
     */
    bool checkSignalsAndUpdate(ThreadID tid);

    /** Checks if a thread is stalled. */
    bool checkStall(ThreadID tid) const;

    /** Align a PC to the start of a fetch buffer block. */
    Addr bufferAlignPC(Addr addr, Addr mask)
    {
        return (addr & ~mask);
    }

    /** Returns the appropriate thread to fetch, given the fetch policy. */
    ThreadID getFetchingThread();

  protected:
    /** Pointer to the O3CPU. */
    O3CPU *cpu;

    typename TimeBuffer<TimeStruct>::wire fromNextStage;

    /** Variable that tracks if fetch has written to the time buffer this
     * cycle. Used to tell CPU if there is activity this cycle.
     */
    bool wroteToTimeBuffer;

    /** Source of possible stalls. */
    struct Stalls {
        bool fetch1;
        bool fetch2;
        bool fetch3;
        bool fetch4;
        bool decode;
        bool drain;
    };

    /** Tracks which stages are telling fetch to stall. */
    Stalls stalls[Impl::MaxThreads];

    /** The width of fetch in instructions. */
    unsigned fetchWidth;

    /** The width of decode in instructions. */
    unsigned decodeWidth;

    /** Updates overall fetch stage status; to be called at the end of each
     * cycle. */
    FetchStatus updateFetchStatus();
};

template <class Impl>
class FetchStage1 : public BaseFetchStage<Impl>
{
  public:
    typedef typename Impl::O3CPU O3CPU;

    FetchStage1(O3CPU *_cpu, const DerivO3CPUParams &params) : BaseFetchStage<Impl>(_cpu, params) {}

    virtual void fetch(bool &status_change, PipelineFetch<Impl> *upper) override;

    virtual std::string name() const override;
};

template <class Impl>
class FetchStage2 : public BaseFetchStage<Impl>
{
  public:
    typedef typename Impl::O3CPU O3CPU;

    FetchStage2(O3CPU *_cpu, const DerivO3CPUParams &params) : BaseFetchStage<Impl>(_cpu, params) {}

    virtual void fetch(bool &status_change, PipelineFetch<Impl> *upper) override;

    virtual std::string name() const override;
};

template <class Impl>
class FetchStage3 : public BaseFetchStage<Impl>
{
  public:
    typedef typename Impl::O3CPU O3CPU;

    FetchStage3(O3CPU *_cpu, const DerivO3CPUParams &params) : BaseFetchStage<Impl>(_cpu, params) {}

    virtual void fetch(bool &status_change, PipelineFetch<Impl> *upper) override;

    virtual std::string name() const override;
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

  public:
    typedef typename Impl::O3CPU O3CPU;

    FetchStage4(O3CPU *_cpu, const DerivO3CPUParams &params) : BaseFetchStage<Impl>(_cpu, params) {
      for (int i = 0; i < Impl::MaxThreads; i++) {
        decoder[i] = nullptr;
      }

      // for (ThreadID tid = 0; tid < numThreads; tid++) {
      for (ThreadID tid = 0; tid < 1; tid++) {
          decoder[tid] = new TheISA::Decoder(
                  dynamic_cast<TheISA::ISA *>(params.isa[tid]));
      }

      branchPred = params.branchPred;
    }

    typedef typename Impl::DynInstPtr DynInstPtr;

    virtual std::string name() const override;

  public:
    virtual void fetch(bool &status_change, PipelineFetch<Impl> *upper) override;

    bool lookupAndUpdateNextPC(const DynInstPtr &inst, TheISA::PCState &pc);

};

#endif //__CPU_O3_FETCH_PIPE_HH__