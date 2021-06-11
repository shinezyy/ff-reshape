#include "cpu/o3/base_fetch_stage.hh"
#include "cpu/o3/pipeline_fetch.hh"
#include "debug/Fetch2.hh"

// IF2 do nothing
template<class Impl>
void
FetchStage2<Impl>::fetch(bool &status_change)
{

    DecoupledIO *thisStage = this->thisStage;
    DecoupledIO *nextStage = this->nextStage;
    [[maybe_unused]] DecoupledIO *prevStage = this->prevStage;

    DPRINTF(Fetch2, "FetchStage2.fetch() is called\n");
    // ThreadID tid = this->upper->getFetchingThread();
    ThreadID tid = 0;

    assert(!this->upper->cpu->switchedOut());

    if (tid == InvalidThreadID) {
        return;
    }

    TheISA::PCState thisPC;

    thisStage->valid(thisStage->lastValid());

    // squash logic
    if (this->fetchStatus[tid] == this->Squashing) {
        thisPC = 0;
        this->pcReg[tid] = 0;
        this->fetchStatus[tid] = this->Idle;
        thisStage->reset();
        DPRINTF(Fetch2, "fetch2: Squashing\n");
        return;
    }

    this->fetchStatus[tid] =
        static_cast<typename BaseFetchStage<Impl>::ThreadStatus>
        (this->upper->toFetch2->lastStatus);

    // The current PC.
    if (prevStage->lastFire()) {
        thisPC = this->upper->toFetch2->pc;
        this->pcReg[tid] = thisPC;
        thisStage->valid(true);
    } else {
        thisPC = this->pcReg[tid];
        if (this->fetchStatus[tid] == this->Squashing) {
            thisStage->valid(false);
        } else if (thisStage->lastFire()) {
            thisStage->valid(false);
        }
    }

    // thisStage->ready = nextStage->ready || !thisStage->valid;
    // thisStage->ready((nextStage->ready() && this->upper->cacheReading) || !thisStage->valid());
    thisStage->ready(!thisStage->valid());
    thisStage->fire(thisStage->valid() && nextStage->ready());

    DPRINTF(Fetch2, "fetch2: thisPC = %08lx\n", thisPC.pc());

    TheISA::PCState nextPC = thisPC;

    this->upper->fromFetch2->lastStatus = this->fetchStatus[tid];
    DPRINTF(Fetch2, "fetch2: fetchStatus=%s\n", this->printStatus(this->fetchStatus[tid]));
    DPRINTF(Fetch2, "if2 v:%d, if3 r:%d, if2 fire:%d\n",
            thisStage->valid(), nextStage->ready(), thisStage->fire());


    if (thisStage->fire()) {
        status_change = true;
        this->upper->fromFetch2->pc = thisPC;
        DPRINTF(Fetch2, "[tid:%i] Sending if2 pc:%x to if3\n", tid, thisPC);
        this->wroteToTimeBuffer = true;
    } else {
        DPRINTF(Fetch2, "[tid:%i] *Stall* if2 pc:%x to if3\n", tid, thisPC);
    }

}

template<class Impl>
std::string
FetchStage2<Impl>::name() const
{
    return BaseFetchStage<Impl>::cpu->name() + ".Fetch2";
}