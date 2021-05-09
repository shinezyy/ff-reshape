#include "cpu/o3/fetch_pipe.hh"
#include "debug/Fetch3.hh"

// IF3 receive ICache date from IF1, and do predecode
template<class Impl>
void
FetchStage3<Impl>::fetch(bool &status_change)
{
    /** Typedefs from ISA. */
    typedef TheISA::MachInst MachInst;

    DPRINTF(Fetch3, "FetchStage3.fetch() is called\n");
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
        this->fetchStatus[tid] = this->Idle;
        DPRINTF(Fetch3, "fetch3: Squashing\n");
        return;
    }

    this->fetchStatus[tid] =
        static_cast<typename BaseFetchStage<Impl>::ThreadStatus>
        (this->upper->toFetch3->lastStatus);

    // The current PC
    if (this->upper->toFetch3->lastStatus == this->Running) {
        thisPC = this->upper->toFetch3->pc;
        this->pcReg[tid] = thisPC;
    } else {
        thisPC = this->pcReg[tid];
    }

    DPRINTF(Fetch3, "fetch3: fetchStatus=%s\n", this->printStatus(this->fetchStatus[tid]));

    DPRINTF(Fetch3, "fetch3: thisPC = %08lx\n", thisPC.pc());

    TheISA::PCState nextPC = thisPC;

    if (this->fetchStatus[tid] == this->IcacheAccessComplete) {
        DPRINTF(Fetch, "[tid:%i] Icache miss is complete.\n", tid);

        this->fetchStatus[tid] = this->Running;
        this->upper->stalls[tid].fetch3 = false;
        status_change = true;
    } else if (this->fetchStatus[tid] == this->ItlbWait ||
            this->fetchStatus[tid] == this->IcacheWaitRetry ||
            this->fetchStatus[tid] == this->IcacheWaitResponse)
    {
        this->upper->stalls[tid].fetch3 = true;
    }

    Addr fetchAddr = thisPC.instAddr() & BaseCPU::PCMask;
    Addr fetchBufferBlockPC = this->upper->bufferAlignPC(fetchAddr, this->upper->fetchBufferMask);

    unsigned blkOffset = (fetchAddr - this->upper->fetchBufferPC[tid]) / this->upper->instSize;

    // squash logic

    // Receive Icache response
    MachInst inst;
    if (this->upper->fetchBufferValid[tid]) {
      if (this->upper->fetchBufferPC[tid] == fetchBufferBlockPC) {
        TheISA::MachInst *cacheInsts =
            reinterpret_cast<TheISA::MachInst *>(this->upper->fetchBuffer[tid]);

        this->upper->fromFetch3->cacheData = new uint8_t[this->upper->fetchBufferSize];
        memcpy(this->upper->fromFetch3->cacheData, this->upper->fetchBuffer[tid], this->upper->fetchBufferSize);

        inst = cacheInsts[blkOffset];

        DPRINTF(Fetch3, "if3 get the data: %08x\n", inst);
        this->upper->fromFetch3->lastStatus = this->Running;
        this->fetchStatus[tid] = this->Running;
        this->upper->stalls[tid].fetch3 = false;
      } else {
        DPRINTF(Fetch3, "fetchBufferValid, but PC is %08x, excepted %08x",
                this->upper->fetchBufferPC[tid], fetchBufferBlockPC);
        this->upper->fromFetch3->lastStatus = this->IcacheWaitResponse;
        this->fetchStatus[tid] = this->IcacheWaitResponse;
        this->upper->fromFetch3->cacheData = nullptr;
        assert(false);
      }
    } else {
        this->upper->fromFetch3->lastStatus = this->IcacheWaitResponse;
        this->fetchStatus[tid] = this->IcacheWaitResponse;
        this->upper->fromFetch3->cacheData = nullptr;
    }


    // doPredecode();

    if (this->fetchStatus[tid] == this->Running && !this->upper->stalls[tid].fetch4) {
        this->upper->fromFetch3->pc = thisPC;
        DPRINTF(Fetch3, "[tid:%i] Sending if3 pc:%x to if4\n", tid, thisPC);
        this->wroteToTimeBuffer = true;
    } else {
        DPRINTF(Fetch3, "[tid:%i] *Stall* if3 pc:%x to if4\n", tid, thisPC);
    }

}

template<class Impl>
std::string
FetchStage3<Impl>::name() const
{
    return std::string(".Fetch3");
}
