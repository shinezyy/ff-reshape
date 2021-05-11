#ifndef __CPU_O3_PIPELINE_FETCH_HH__
#define __CPU_O3_PIPELINE_FETCH_HH__

#include "cpu/o3/fetch.hh"
#include "params/DerivO3CPU.hh"

template <class Impl> class FetchStage1;
template <class Impl> class FetchStage2;

template <class Impl> class FetchStage3;

template <class Impl> class FetchStage4;

template <class Impl>
class PipelineFetch : public DefaultFetch<Impl>
{
  public:
    /** Typedefs from Impl. */
    typedef typename Impl::CPUPol CPUPol;
    typedef typename Impl::O3CPU O3CPU;

    typedef typename CPUPol::FetchStageStruct FetchStageStruct;

    typedef typename DefaultFetch<Impl>::ThreadStatus ThreadStatus;

    /** PipelineFetch constructor. */
    PipelineFetch(O3CPU *_cpu, const DerivO3CPUParams &params);

    /** Ticks the fetch stage, processing all inputs signals and fetching
     * as many instructions as possible.
     */
    void tick();

    /** Sets fetchStatus of active threads. */
    virtual void setFetchStatus(ThreadStatus status, ThreadID tid) override;

    void fetchSquash(const TheISA::PCState &newPC, ThreadID tid);

  public:
    FetchStage1<Impl> *fetch1;
    FetchStage2<Impl> *fetch2;
    FetchStage3<Impl> *fetch3;
    FetchStage4<Impl> *fetch4;

    bool cacheReading;

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

};

#endif //__CPU_O3_PIPELINE_FETCH_HH__