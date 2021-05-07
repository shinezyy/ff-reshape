#include "cpu/o3/cpu.hh"
#include "cpu/o3/fetch_pipe.hh"
#include "debug/Fetch.hh"
#include "debug/Fetch1.hh"

// IF1 just send request to ICache
template<class Impl>
void
FetchStage1<Impl>::fetch(bool &status_change)
{
    /** Typedefs from ISA. */
    typedef TheISA::MachInst MachInst;

    DPRINTF(Fetch1, "FetchStage1.fetch() is called\n");
    // ThreadID tid = this->upper->getFetchingThread();
    ThreadID tid = 0;

    assert(!this->upper->cpu->switchedOut());

    if (tid == InvalidThreadID) {
        return;
    }

    TheISA::PCState thisPC;

    // squash logic
    if (this->fetchStatus[tid] == this->Squashing) {
        thisPC = 0;
        this->pcReg[tid] = 0;
        DPRINTF(Fetch1, "fetch1: thisPC = %08lx\n", thisPC.pc());
        return;
    }

    // The current PC.
    if (this->fetchStatus[tid] == this->Running) {
        thisPC = this->upper->pc[tid];
        this->pcReg[tid] = thisPC;
    } else {
        thisPC = this->pcReg[tid];
    }

    DPRINTF(Fetch1, "fetch1: thisPC = %08lx\n", thisPC.pc());

    Addr fetchAddr = thisPC.instAddr() & BaseCPU::PCMask;
    [[maybe_unused]] Addr fetchBufferBlockPC = this->upper->bufferAlignPC(fetchAddr, this->upper->fetchBufferMask);

    // TheISA::PCState nextPC = thisPC;
    TheISA::PCState nextPC = thisPC.instAddr() + this->fetchWidth * sizeof(MachInst);

    if (this->fetchStatus[tid] == this->IcacheAccessComplete) {
        this->upper->fromFetch1->lastStatus = this->Running;
    } else {
        this->upper->fromFetch1->lastStatus = this->fetchStatus[tid];
    }

    DPRINTF(Fetch1, "fetch1: fetchStatus=%s\n", this->printStatus(this->fetchStatus[tid]));

    if (this->fetchStatus[tid] == this->Running && !this->upper->stalls[tid].fetch2) {
        this->upper->fromFetch1->pc = thisPC;
        DPRINTF(Fetch1, "[tid:%i] Sending if1 pc:%x to if2\n", tid, thisPC);
        this->wroteToTimeBuffer = true;
    } else if (this->fetchStatus[tid] == this->IcacheAccessComplete && !this->upper->stalls[tid].fetch2) {
        this->upper->fromFetch1->pc = nextPC;
        DPRINTF(Fetch1, "[tid:%i] Sending if1 pc:%x to if2\n", tid, nextPC);
        this->wroteToTimeBuffer = true;
    } else {
        DPRINTF(Fetch1, "[tid:%i] *Stall* if1 pc:%x to if2\n", tid, thisPC);
    }

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
        if (!this->upper->stalls[tid].fetch2) {
            DPRINTF(Fetch1, "[tid:%i] Attempting to translate and read "
                    "instruction, starting at PC %s.\n", tid, thisPC);

            this->upper->fetchCacheLine(fetchAddr, tid, thisPC.instAddr());
            nextPC = thisPC.instAddr() + this->fetchWidth * sizeof(MachInst);
            DPRINTF(Fetch1, "nextPC = %x, thisPC = %x\n", nextPC, thisPC);
        }

        // I should handle all read ICache operations at here, and dump the data returend,
        // then check the if3, if if3 need a cache bolck in the dumped data, then send it to if3
    }

    this->upper->pc[tid] = nextPC;
    DPRINTF(Fetch1, "nextPC = %x, pc = %x\n", nextPC, this->upper->pc[tid]);
}

template<class Impl>
std::string
FetchStage1<Impl>::name() const
{
    return std::string(".Fetch1");
}
