#include "cpu/o3/cpu.hh"
#include "cpu/o3/fetch_pipe.hh"
#include "debug/Fetch.hh"
#include "debug/Fetch1.hh"

// IF1 just send request to ICache
template<class Impl>
void
FetchStage1<Impl>::fetch(bool &status_change, PipelineFetch<Impl> *upper)
{
    /** Typedefs from ISA. */
    typedef TheISA::MachInst MachInst;

    printf("********************************************************\n");
    printf("FetchStage1.fetch() is called\n");
    ThreadID tid = upper->getFetchingThread();

    assert(!upper->cpu->switchedOut());

    if (tid == InvalidThreadID) {
        return;
    }

    // squash logic

    // The current PC.
    TheISA::PCState thisPC = upper->pc[tid];
    printf("fetch1: thisPC = %08lx\n", thisPC.pc());

    DPRINTF(Fetch1, "Attempting to fetch from [tid:%i] PC: %s\n",
            tid, thisPC);

    Addr fetchAddr = thisPC.instAddr() & BaseCPU::PCMask;
    [[maybe_unused]] Addr fetchBufferBlockPC = upper->bufferAlignPC(fetchAddr, upper->fetchBufferMask);

    TheISA::PCState nextPC = thisPC;

    // If returning from the delay of a cache miss, then update the status
    // to running, otherwise do the cache access.  Possibly move this up
    // to tick() function.
    if (this->fetchStatus[tid] == this->IcacheAccessComplete) {
        DPRINTF(Fetch, "[tid:%i] Icache miss is complete.\n", tid);

        this->fetchStatus[tid] = this->Running;
        status_change = true;

    } else if (this->fetchStatus[tid] == this->Running) {
        // Align the fetch PC so its at the start of a fetch buffer segment.
        // [[maybe_unused]] Addr fetchBufferBlockPC = upper->bufferAlignPC(fetchAddr, upper->fetchBufferMask);

        // Send a request to ICache
        if (!this->stalls[tid].fetch2) {
            DPRINTF(Fetch1, "[tid:%i] Attempting to translate and read "
                    "instruction, starting at PC %s.\n", tid, thisPC);

            // upper->fetchCacheLine(fetchAddr, tid, thisPC.instAddr());
        }

        // I should handle all read ICache operations at here, and dump the data returend,
        // then check the if3, if if3 need a cache bolck in the dumped data, then send it to if3
    }

    if (!this->stalls[tid].fetch2) {
        upper->fromFetch1->pc = thisPC;
        DPRINTF(Fetch1, "[tid:%i] Sending if1 pc:%x to if2\n", tid, thisPC);
        this->wroteToTimeBuffer = true;
    }

    nextPC = fetchAddr + this->fetchWidth * sizeof(MachInst);
    // printf("fetch1: nextPC = %08lx\n", nextPC.pc());

    upper->pc[tid] = nextPC;
}

template<class Impl>
std::string
FetchStage1<Impl>::name() const
{
    return std::string(".Fetch1");
}
