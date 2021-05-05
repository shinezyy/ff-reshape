#include "cpu/o3/fetch_pipe.hh"
#include "debug/Fetch2.hh"

// IF2 do nothing
template<class Impl>
void
FetchStage2<Impl>::fetch(bool &status_change, PipelineFetch<Impl> *upper)
{
    printf("FetchStage2.fetch() is called\n");
    ThreadID tid = upper->getFetchingThread();

    assert(!upper->cpu->switchedOut());

    if (tid == InvalidThreadID) {
        return;
    }

    // squash logic

    // The current PC.
    TheISA::PCState thisPC = upper->toFetch2->pc;
    printf("fetch2: thisPC = %08lx\n", thisPC.pc());

    TheISA::PCState nextPC = thisPC;

    if (!this->stalls[tid].fetch3) {
        upper->fromFetch2->pc = thisPC;
        DPRINTF(Fetch2, "[tid:%i] Sending if2 pc:%x to if3\n", tid, thisPC);
        this->wroteToTimeBuffer = true;
    }

    // upper->pc[tid] = nextPC;
}

template<class Impl>
std::string
FetchStage2<Impl>::name() const
{
    return std::string(".Fetch2");
}