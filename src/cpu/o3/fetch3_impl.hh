#include "cpu/o3/fetch_pipe.hh"
#include "debug/Fetch3.hh"

// IF3 receive ICache date from IF1, and do predecode
template<class Impl>
void
FetchStage3<Impl>::fetch(bool &status_change, PipelineFetch<Impl> *upper)
{
    printf("FetchStage3.fetch() is called\n");
    ThreadID tid = upper->getFetchingThread();

    assert(!upper->cpu->switchedOut());

    if (tid == InvalidThreadID) {
        return;
    }

    // squash logic

    // Receive Icache response
    // getFetchData();
    // doPredecode();

    // The current PC.
    TheISA::PCState thisPC = upper->toFetch3->pc;
    printf("fetch3: thisPC = %08lx\n", thisPC.pc());

    TheISA::PCState nextPC = thisPC;

    if (!this->stalls[tid].fetch4) {
        upper->fromFetch3->pc = thisPC;
        DPRINTF(Fetch3, "[tid:%i] Sending if3 pc:%x to if4\n", tid, thisPC);
        this->wroteToTimeBuffer = true;
    }

    // upper->pc[tid] = nextPC;
}

template<class Impl>
std::string
FetchStage3<Impl>::name() const
{
    return std::string(".Fetch3");
}