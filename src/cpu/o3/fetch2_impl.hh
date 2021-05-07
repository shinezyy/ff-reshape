#include "cpu/o3/fetch_pipe.hh"
#include "debug/Fetch2.hh"

// IF2 do nothing
template<class Impl>
void
FetchStage2<Impl>::fetch(bool &status_change)
{
    DPRINTF(Fetch2, "FetchStage2.fetch() is called\n");
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
        DPRINTF(Fetch2, "fetch2: thisPC = %08lx\n", thisPC.pc());
        return;
    }

    this->fetchStatus[tid] =
        static_cast<typename BaseFetchStage<Impl>::ThreadStatus>
        (this->upper->toFetch2->lastStatus);

    // The current PC.
    if (this->upper->toFetch2->lastStatus == this->Running) {
        thisPC = this->upper->toFetch2->pc;
        this->pcReg[tid] = thisPC;
    } else {
        thisPC = this->pcReg[tid];
    }


    DPRINTF(Fetch2, "fetch2: thisPC = %08lx\n", thisPC.pc());

    TheISA::PCState nextPC = thisPC;

    this->upper->fromFetch2->lastStatus = this->fetchStatus[tid];
    DPRINTF(Fetch2, "fetch2: fetchStatus=%s\n", this->printStatus(this->fetchStatus[tid]));

    if (this->fetchStatus[tid] == this->Running && !this->upper->stalls[tid].fetch2) {
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
    return std::string(".Fetch2");
}
