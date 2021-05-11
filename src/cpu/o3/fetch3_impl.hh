#include "cpu/o3/base_fetch_stage.hh"
#include "cpu/o3/pipeline_fetch.hh"
#include "debug/Fetch3.hh"

// IF3 receive ICache date from IF1, and do predecode
template<class Impl>
void
FetchStage3<Impl>::fetch(bool &status_change)
{
    /** Typedefs from ISA. */
    typedef TheISA::MachInst MachInst;

    DecoupledIO *thisStage = this->thisStage;
    DecoupledIO *nextStage = this->nextStage;
    [[maybe_unused]] DecoupledIO *prevStage = this->prevStage;

    DPRINTF(Fetch3, "FetchStage3.fetch() is called\n");
    // ThreadID tid = this->upper->getFetchingThread();
    ThreadID tid = 0;

    assert(!this->upper->cpu->switchedOut());

    if (tid == InvalidThreadID) {
        return;
    }

    TheISA::PCState thisPC;

    thisStage->valid(thisStage->lastValid());
    DPRINTF(Fetch3, "lastValid: %d\n", thisStage->lastValid());

    // squash logic
    if (this->fetchStatus[tid] == this->Squashing) {
        thisPC = 0;
        this->pcReg[tid] = 0;
        this->fetchStatus[tid] = this->Idle;
        thisStage->reset();
        hasData = false;
        lastHasData = false;
        DPRINTF(Fetch3, "fetch3: Squashing\n");
        return;
    }

    this->fetchStatus[tid] =
        static_cast<typename BaseFetchStage<Impl>::ThreadStatus>
        (this->upper->toFetch3->lastStatus);

    // The current PC
    if (this->fetchStatus[tid] != this->Squashing) {
        if (prevStage->lastFire()) {
            thisPC = this->upper->toFetch3->pc;
            this->pcReg[tid] = thisPC;
            thisStage->valid(true);
            DPRINTF(Fetch3, "Set if3_valid is true\n");
        } else {
            thisPC = this->pcReg[tid];
            if (thisStage->lastFire()) {
                thisStage->valid(false);
                DPRINTF(Fetch3, "Set if3_valid is false\n");
            }
        }
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
    if (!hasData && thisStage->valid() && this->upper->fetchBufferValid[tid]) {
      if (this->upper->fetchBufferPC[tid] == fetchBufferBlockPC) {
        cacheData = new uint8_t[this->upper->fetchBufferSize];
        memcpy(cacheData, this->upper->fetchBuffer[tid], this->upper->fetchBufferSize);

        TheISA::MachInst *cacheInsts = reinterpret_cast<TheISA::MachInst *>(cacheData);

        // TODO: cacheData need keep when if3 stall
        this->upper->fromFetch3->cacheData = cacheData;
        hasData = true;
        this->upper->cacheReading = false;
        this->upper->fetchBufferValid[tid] = false;

        inst = cacheInsts[blkOffset];

        DPRINTF(Fetch3, "if3 get the data: %08x\n", inst);
        this->upper->fromFetch3->lastStatus = this->Running;
        this->fetchStatus[tid] = this->Running;
        this->upper->stalls[tid].fetch3 = false;
      } else {
        DPRINTF(Fetch3, "WARN: fetchBufferValid, but PC is %08x, excepted %08x\n",
                this->upper->fetchBufferPC[tid], fetchBufferBlockPC);
        this->upper->fromFetch3->lastStatus = this->IcacheWaitResponse;
        this->fetchStatus[tid] = this->IcacheWaitResponse;
        this->upper->fromFetch3->cacheData = nullptr;
        this->upper->fetchBufferValid[tid] = false;
        // assert(false);
      }
    } else {
        this->upper->fromFetch3->lastStatus = this->IcacheWaitResponse;
        this->fetchStatus[tid] = this->IcacheWaitResponse;
        this->upper->fromFetch3->cacheData = nullptr;
    }

    // thisStage->ready = nextStage->ready || !thisStage->valid;
    thisStage->ready(!thisStage->valid() || (thisStage->valid() && nextStage->ready() && hasData));
    thisStage->fire(thisStage->valid() && nextStage->ready() && hasData);

    DPRINTF(Fetch3, "if3 v:%d, if4 r:%d, if3 fire:%d, hasData:%d\n",
            thisStage->valid(), nextStage->ready(), thisStage->fire(), hasData);

    // doPredecode();

    if (thisStage->fire()) {
        this->upper->fromFetch3->pc = thisPC;
        this->upper->fromFetch3->cacheData = cacheData;
        DPRINTF(Fetch3, "[tid:%i] Sending if3 pc:%x to if4\n", tid, thisPC);
        DPRINTF(Fetch3, "cacheData: %x\n", cacheData);
        this->wroteToTimeBuffer = true;
        hasData = false;
    } else {
        DPRINTF(Fetch3, "[tid:%i] *Stall* if3 pc:%x to if4\n", tid, thisPC);
    }

    lastHasData = hasData;
}

template<class Impl>
std::string
FetchStage3<Impl>::name() const
{
    return std::string(".Fetch3");
}
