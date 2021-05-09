#ifndef __CPU_O3_FETCH_PIPE_IMPL_HH__
#define __CPU_O3_FETCH_PIPE_IMPL_HH__

#include <algorithm>
#include <cstring>
#include <list>
#include <map>
#include <queue>

#include "arch/generic/tlb.hh"
#include "arch/utility.hh"
#include "base/random.hh"
#include "base/types.hh"
#include "config/the_isa.hh"
#include "cpu/base.hh"
#include "cpu/exetrace.hh"
#include "cpu/o3/cpu.hh"
#include "cpu/o3/fetch.hh"
#include "cpu/o3/fetch_pipe.hh"
#include "cpu/o3/isa_specific.hh"
#include "debug/Activity.hh"
#include "debug/Drain.hh"
#include "debug/Fetch.hh"
#include "debug/LoopBuffer.hh"
#include "debug/O3PipeView.hh"
#include "debug/ValueCommit.hh"
#include "mem/packet.hh"
#include "params/DerivO3CPU.hh"
#include "sim/byteswap.hh"
#include "sim/core.hh"
#include "sim/eventq.hh"
#include "sim/full_system.hh"
#include "sim/system.hh"

template<class Impl>
PipelineFetch<Impl>::PipelineFetch(O3CPU *_cpu, const DerivO3CPUParams &params)
    : DefaultFetch<Impl>(_cpu, params),
        toFetch1Buffer(10, 10),
        toFetch2Buffer(10, 10),
        toFetch3Buffer(10, 10),
        toFetch4Buffer(10, 10)
    {
        fetch1 = new FetchStage1<Impl>(_cpu, params, this);
        fetch2 = new FetchStage2<Impl>(_cpu, params, this);
        fetch3 = new FetchStage3<Impl>(_cpu, params, this);
        fetch4 = new FetchStage4<Impl>(_cpu, params, this);

        fetch1->connNextStage(fetch2);
        fetch2->connNextStage(fetch3);
        fetch3->connNextStage(fetch4);

        fromFetch1 = toFetch2Buffer.getWire(0);
        fromFetch2 = toFetch3Buffer.getWire(0);
        fromFetch3 = toFetch4Buffer.getWire(0);
        fromFetch4 = toFetch1Buffer.getWire(0);

        toFetch1 = toFetch1Buffer.getWire(-1);
        toFetch2 = toFetch2Buffer.getWire(-1);
        toFetch3 = toFetch3Buffer.getWire(-1);
        toFetch4 = toFetch4Buffer.getWire(-1);

        for (int i = 0; i < 1; i++) {
            stalls[i] = {false, false, false, false, false, false};
        }

    }

template <class Impl>
void
PipelineFetch<Impl>::tick()
{
    list<ThreadID>::iterator threads = this->activeThreads->begin();
    list<ThreadID>::iterator end = this->activeThreads->end();
    bool status_change = false;

    this->wroteToTimeBuffer = false;

    DPRINTF(Fetch, "********************************************************\n");

    while (threads != end) {
        [[maybe_unused]] ThreadID tid = *threads++;

        // Check the signals for each thread to determine the proper status
        // for each thread.
        bool updated_status = this->checkSignalsAndUpdate(tid);
        // bool updated_status = false;
        status_change = status_change || updated_status;
    }

    /*if (stalls[0].fetch1)
        DPRINTF(Fetch, "=|= stalls: fetch1\n");

    if (stalls[0].fetch2)
        DPRINTF(Fetch, "=|= stalls: fetch2\n");

    if (stalls[0].fetch3)
        DPRINTF(Fetch, "=|= stalls: fetch3\n");

    if (stalls[0].fetch4)
        DPRINTF(Fetch, "=|= stalls: fetch4\n");

    if (stalls[0].decode)
        DPRINTF(Fetch, "=|= stalls: decode\n");

    if (stalls[0].drain)
        DPRINTF(Fetch, "=|= stalls: drain\n");
    */

    fetch1->tick(status_change);
    DPRINTF(Fetch, "\n");
    fetch2->tick(status_change);
    DPRINTF(Fetch, "\n");
    fetch3->tick(status_change);
    DPRINTF(Fetch, "\n");
    fetch4->tick(status_change);
    DPRINTF(Fetch, "\n");

    if (status_change) {
        // Change the fetch stage status if there was a status change.
        this->_status = this->updateFetchStatus();
        // this->_status = this->Active;
    }

    // Send instructions enqueued into the fetch queue to decode.
    // Limit rate by fetchWidth.  Stall if decode is stalled.
    unsigned insts_to_decode = 0;
    unsigned available_insts = 0;

    for (auto tid : *(this->activeThreads)) {
        if (!stalls[tid].decode) {
            available_insts += this->fetchQueue[tid].size();
        }
    }

    // Pick a random thread to start trying to grab instructions from
    auto tid_itr = this->activeThreads->begin();
    std::advance(tid_itr, random_mt.random<uint8_t>(0, this->activeThreads->size() - 1));

    while (available_insts != 0 && insts_to_decode < this->decodeWidth) {
        ThreadID tid = *tid_itr;
        if (!stalls[tid].decode && !this->fetchQueue[tid].empty()) {
            const auto& inst = this->fetchQueue[tid].front();
            this->toDecode->insts[this->toDecode->size++] = inst;
            DPRINTF(Fetch, "[tid:%i] [sn:%llu] Sending instruction %08x to decode "
                    "from fetch queue. Fetch queue size: %i.\n",
                    tid, inst->seqNum, inst->staticInst->machInst, this->fetchQueue[tid].size());

            this->wroteToTimeBuffer = true;
            this->fetchQueue[tid].pop_front();
            insts_to_decode++;
            available_insts--;
        }

        tid_itr++;
        // Wrap around if at end of active threads list
        if (tid_itr == this->activeThreads->end())
            tid_itr = this->activeThreads->begin();
    }

    // If there was activity this cycle, inform the CPU of it.
    if (this->wroteToTimeBuffer) {
        DPRINTF(Activity, "Activity this cycle.\n");
        this->cpu->activityThisCycle();
    }

    toFetch1Buffer.advance();
    toFetch2Buffer.advance();
    toFetch3Buffer.advance();
    toFetch4Buffer.advance();

    fetch1->decoupledBufferAdvance();
    fetch2->decoupledBufferAdvance();
    fetch3->decoupledBufferAdvance();
    fetch4->decoupledBufferAdvance();
}

template<class Impl>
void
PipelineFetch<Impl>::setFetchStatus(ThreadStatus status, ThreadID tid)
{
    DPRINTF(Fetch, "setFetchStatus is called: %d\n", status);

    this->fetchStatus[tid] = status;

    switch(status) {
        case this->Idle :
        case this->Squashing :
        case this->Blocked :
        case this->TrapPending :
        case this->QuiescePending :
            fetch1->setFetchStatus(static_cast<typename BaseFetchStage<Impl>::ThreadStatus>(status), tid);
            fetch2->setFetchStatus(static_cast<typename BaseFetchStage<Impl>::ThreadStatus>(status), tid);
            fetch3->setFetchStatus(static_cast<typename BaseFetchStage<Impl>::ThreadStatus>(status), tid);
            fetch4->setFetchStatus(static_cast<typename BaseFetchStage<Impl>::ThreadStatus>(status), tid);
            break;

        case this->Running :
        case this->ItlbWait :
        case this->IcacheWaitRetry :
        case this->IcacheWaitResponse :
        case this->IcacheAccessComplete :
        case this->NoGoodAddr :
            fetch1->setFetchStatus(static_cast<typename BaseFetchStage<Impl>::ThreadStatus>(status), tid);
            break;

        default: break;
    }
}

template <class Impl>
void
PipelineFetch<Impl>::fetchSquash(const TheISA::PCState &newPC, ThreadID tid)
{
    DPRINTF(Fetch, "[tid:%i] Squashing, setting PC to: %s.\n",
            tid, newPC);

    this->pc[tid] = newPC;
    this->fetchOffset[tid] = 0;

    // Clear the icache miss if it's outstanding.
    if (this->fetchStatus[tid] == this->IcacheWaitResponse) {
        DPRINTF(Fetch, "[tid:%i] Squashing outstanding Icache miss.\n",
                tid);
        this->memReq[tid] = NULL;
    } else if (this->fetchStatus[tid] == this->ItlbWait) {
        DPRINTF(Fetch, "[tid:%i] Squashing outstanding ITLB miss.\n",
                tid);
        this->memReq[tid] = NULL;
    }

    // Get rid of the retrying packet if it was from this thread.
    if (this->retryTid == tid) {
        assert(this->cacheBlocked);
        if (this->retryPkt) {
            delete this->retryPkt;
        }
        this->retryPkt = NULL;
        this->retryTid = InvalidThreadID;
    }

    this->setFetchStatus(this->Squashing, tid);
}

/*template <class Impl>
bool
PipelineFetch<Impl>::checkSignalsAndUpdate(ThreadID tid)
{
    bool needUpdate = DefaultFetch<Impl>::checkSignalsAndUpdate(tid);

    if (this->checkStall(tid) &&
        fetch4->fetchStatus[tid] == fetch4->Running) {
        DPRINTF(Fetch, "[tid:%i] Setting to blocked\n",tid);

        setFetchStatus(this->Blocked, tid);

        return true;
    }

    return needUpdate;
}*/

template<class Impl>
std::string
BaseFetchStage<Impl>::printStatus(int status)
{
  switch (status) {
    case  0: return std::string("Running");
    case  1: return std::string("Idle");
    case  2: return std::string("Squashing");
    case  3: return std::string("Blocked");
    case  4: return std::string("Fetching");
    case  5: return std::string("TrapPending");
    case  6: return std::string("QuiescePending");
    case  7: return std::string("ItlbWait");
    case  8: return std::string("IcacheWaitResponse");
    case  9: return std::string("IcacheWaitRetry");
    case 10: return std::string("IcacheAccessComplete");
    case 11: return std::string("NoGoodAddr");
    default: return std::string("Wrong Status");
  }
}

template<class Impl>
BaseFetchStage<Impl>::BaseFetchStage(O3CPU *_cpu, const DerivO3CPUParams &params, PipelineFetch<Impl> *upper)
  : cpu(_cpu),
    upper(upper),
    decoupledBuffer(1, 1),
    lastValid(false), lastReady(false)
{
  // Get the size of an instruction.
  instSize = sizeof(TheISA::MachInst);

  for (int i = 0; i < Impl::MaxThreads; i++) {
      fetchStatus[i] = Idle;
      pcReg[i] = 0;
  }

  thisStage = decoupledBuffer.getWire(0);
  // nextStage = nullptr;
  // prevStage = nullptr;
}

template<class Impl>
void
BaseFetchStage<Impl>::connNextStage(BaseFetchStage<Impl> *next)
{
    // This need replace by set and get functions
    this->nextStage = next->decoupledBuffer.getWire(-1);
    next->prevStage = this->decoupledBuffer.getWire(-1);
}

template<class Impl>
typename BaseFetchStage<Impl>::FetchStatus
BaseFetchStage<Impl>::updateFetchStatus()
{
    //Check Running
    list<ThreadID>::iterator threads = activeThreads->begin();
    list<ThreadID>::iterator end = activeThreads->end();

    while (threads != end) {
        ThreadID tid = *threads++;

        if (fetchStatus[tid] == Running ||
            fetchStatus[tid] == Squashing) {

            if (_status == Inactive) {
                DPRINTF(Activity, "[tid:%i] Activating stage.\n",tid);

                if (fetchStatus[tid] == IcacheAccessComplete) {
                    DPRINTF(Activity, "[tid:%i] Activating fetch due to cache"
                            "completion\n",tid);
                }

                cpu->activateStage(O3CPU::FetchIdx);
            }

            return Active;
        }
    }

    // Stage is switching from active to inactive, notify CPU of it.
    if (_status == Active) {
        DPRINTF(Activity, "Deactivating stage.\n");

        cpu->deactivateStage(O3CPU::FetchIdx);
    }

    return Inactive;
}

template <class Impl>
void
BaseFetchStage<Impl>::tick(bool &status_change)
{
    // DPRINTF(Fetch, "Running %s stage. fetchStatus: %s\n", this->name(), printStatus(this->fetchStatus[0]));

    // for (threadFetched = 0; threadFetched < numFetchingThreads;
    for (threadFetched = 0; threadFetched < 1;
         threadFetched++) {
        // Fetch each of the actively fetching threads.
        fetch(status_change);
    }
}

template <class Impl>
std::string
BaseFetchStage<Impl>::name() const
{
    return cpu->name() + ".BaseFetchStage";
}

template<class Impl>
ThreadID
BaseFetchStage<Impl>::getFetchingThread()
{
    if (numThreads > 1) {
        return InvalidThreadID;
    } else {
        list<ThreadID>::iterator thread = activeThreads->begin();
        if (thread == activeThreads->end()) {
            return InvalidThreadID;
        }

        ThreadID tid = *thread;

        if (fetchStatus[tid] == Running ||
            fetchStatus[tid] == IcacheAccessComplete ||
            fetchStatus[tid] == Idle) {
            return tid;
        } else {
            return InvalidThreadID;
        }
    }
}

#endif//__CPU_O3_FETCH_PIPE_IMPL_HH__
