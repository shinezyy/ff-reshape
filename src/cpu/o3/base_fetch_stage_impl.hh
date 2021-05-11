#ifndef __CPU_O3_BASE_FETCH_STAGE_IMPL_HH__
#define __CPU_O3_BASE_FETCH_STAGE_IMPL_HH__

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
#include "cpu/o3/base_fetch_stage.hh"
#include "cpu/o3/cpu.hh"
#include "cpu/o3/fetch.hh"
#include "cpu/o3/isa_specific.hh"
#include "cpu/o3/pipeline_fetch.hh"
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
    upper(upper)
{
  // Get the size of an instruction.
  instSize = sizeof(TheISA::MachInst);

  for (int i = 0; i < Impl::MaxThreads; i++) {
      fetchStatus[i] = Idle;
      pcReg[i] = 0;
  }

  thisStage = new DecoupledIO();
  nextStage = nullptr;
  prevStage = nullptr;
}

template<class Impl>
void
BaseFetchStage<Impl>::connNextStage(BaseFetchStage<Impl> *next)
{
    // This need replace by set and get functions
    this->nextStage = next->thisStage;
    next->prevStage = this->thisStage;
}

template<class Impl>
typename BaseFetchStage<Impl>::FetchStatus
BaseFetchStage<Impl>::updateFetchStatus()
{
    //Check Running
    std::list<ThreadID>::iterator threads = activeThreads->begin();
    std::list<ThreadID>::iterator end = activeThreads->end();

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
    for (threadFetched = 0; threadFetched < this->upper->numFetchingThreads;
         threadFetched++) {
        // Fetch each of the actively fetching threads.
        fetch(status_change);
    }
}

template <class Impl>
void
BaseFetchStage<Impl>::advance()
{
    thisStage->lastValid(thisStage->valid());
    thisStage->lastReady(thisStage->ready());
    thisStage->lastFire(thisStage->fire());
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
        std::list<ThreadID>::iterator thread = activeThreads->begin();
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

#endif //__CPU_O3_BASE_FETCH_STAGE_IMPL_HH__
