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
        fetch1 = new FetchStage1<Impl>(_cpu, params);
        fetch2 = new FetchStage2<Impl>(_cpu, params);
        fetch3 = new FetchStage3<Impl>(_cpu, params);
        fetch4 = new FetchStage4<Impl>(_cpu, params);

        fromFetch1 = toFetch2Buffer.getWire(0);
        fromFetch2 = toFetch3Buffer.getWire(0);
        fromFetch3 = toFetch4Buffer.getWire(0);
        fromFetch4 = toFetch1Buffer.getWire(0);

        toFetch1 = toFetch1Buffer.getWire(-1);
        toFetch2 = toFetch2Buffer.getWire(-1);
        toFetch3 = toFetch3Buffer.getWire(-1);
        toFetch4 = toFetch4Buffer.getWire(-1);
    }
/*template<class Impl>
PipelineFetch<Impl>::PipelineFetch(O3CPU *_cpu, const DerivO3CPUParams &params)
    : fetchPolicy(params.smtFetchPolicy),
      cpu(_cpu),
      fetchWidth(params.fetchWidth),
      decodeWidth(params.decodeWidth),
      retryPkt(NULL),
      retryTid(InvalidThreadID),
      cacheBlkSize(cpu->cacheLineSize()),
      fetchBufferSize(params.fetchBufferSize),
      fetchBufferMask(fetchBufferSize - 1),
      fetchQueueSize(params.fetchQueueSize),
      numThreads(params.numThreads),
      numFetchingThreads(params.smtNumFetchingThreads),
      icachePort(this, _cpu),
      finishTranslationEvent(this), fetchStats(_cpu, this),
      fetch1(_cpu, params), fetch2(_cpu, params),
      fetch3(_cpu, params), fetch4(_cpu, params)
{
    if (numThreads > Impl::MaxThreads)
        fatal("numThreads (%d) is larger than compiled limit (%d),\n"
              "\tincrease MaxThreads in src/cpu/o3/impl.hh\n",
              numThreads, static_cast<int>(Impl::MaxThreads));
    if (fetchWidth > Impl::MaxWidth)
        fatal("fetchWidth (%d) is larger than compiled limit (%d),\n"
             "\tincrease MaxWidth in src/cpu/o3/impl.hh\n",
             fetchWidth, static_cast<int>(Impl::MaxWidth));
    if (fetchBufferSize > cacheBlkSize)
        fatal("fetch buffer size (%u bytes) is greater than the cache "
              "block size (%u bytes)\n", fetchBufferSize, cacheBlkSize);
    if (cacheBlkSize % fetchBufferSize)
        fatal("cache block (%u bytes) is not a multiple of the "
              "fetch buffer (%u bytes)\n", cacheBlkSize, fetchBufferSize);

    // Get the size of an instruction.
    instSize = sizeof(TheISA::MachInst);

    for (int i = 0; i < Impl::MaxThreads; i++) {
        fetchStatus[i] = Idle;
        pc[i] = 0;
        memReq[i] = nullptr;
        stalls[i] = {false, false};
        fetchBuffer[i] = NULL;
        fetchBufferPC[i] = 0;
        fetchBufferValid[i] = false;
    }

    branchPred = params.branchPred;

    for (ThreadID tid = 0; tid < numThreads; tid++) {
        decoder[tid] = new TheISA::Decoder(
                dynamic_cast<TheISA::ISA *>(params.isa[tid]));
        // Create space to buffer the cache line data,
        // which may not hold the entire cache line.
        fetchBuffer[tid] = new uint8_t[fetchBufferSize];
    }
}*/

template <class Impl>
void
PipelineFetch<Impl>::tick()
{
    fetch1->tick(this);
    fetch2->tick(this);
    fetch3->tick(this);
    fetch4->tick(this);

    toFetch1Buffer.advance();
    toFetch2Buffer.advance();
    toFetch3Buffer.advance();
    toFetch4Buffer.advance();
}

template<class Impl>
BaseFetchStage<Impl>::BaseFetchStage(O3CPU *_cpu, const DerivO3CPUParams &params)
  : cpu(_cpu),
    fetchWidth(params.fetchWidth),
    decodeWidth(params.decodeWidth)
{
  // Get the size of an instruction.
  instSize = sizeof(TheISA::MachInst);

  for (int i = 0; i < Impl::MaxThreads; i++) {
      fetchStatus[i] = Idle;
      pc[i] = 0;
      stalls[i] = {false, false, false, false, false, false};
  }
}

template <class Impl>
bool
BaseFetchStage<Impl>::checkSignalsAndUpdate(ThreadID tid)
{
    // Update the per thread stall statuses.
    if (fromNextStage->block) {
        stalls[tid].nextStage = true;
    }

    if (fromNextStage->unblock) {
        assert(stalls[tid].nextStage);
        assert(!fromNextStage->block[tid]);
        stalls[tid].nextStage = false;
    }

    // Check squash signals from commit.
    if (fromNextStage->squash) {
        // Squash this stage
        return true;
    }

    if (checkStall(tid) &&
        fetchStatus[tid] != IcacheWaitResponse &&
        fetchStatus[tid] != IcacheWaitRetry &&
        fetchStatus[tid] != ItlbWait &&
        fetchStatus[tid] != QuiescePending) {
        DPRINTF(Fetch, "[tid:%i] Setting to blocked\n",tid);

        fetchStatus[tid] = Blocked;

        return true;
    }

    if (fetchStatus[tid] == Blocked ||
        fetchStatus[tid] == Squashing) {
        // Switch status to running if fetch isn't being told to block or
        // squash this cycle.
        DPRINTF(Fetch, "[tid:%i] Done squashing, switching to running.\n",
                tid);

        fetchStatus[tid] = Running;

        return true;
    }

    // If we've reached this point, we have not gotten any signals that
    // cause fetch to change its status.  Fetch remains the same as before.
    return false;
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
BaseFetchStage<Impl>::tick(PipelineFetch<Impl> *upper)
{
    list<ThreadID>::iterator threads = upper->activeThreads->begin();
    list<ThreadID>::iterator end = upper->activeThreads->end();
    bool status_change = false;

    wroteToTimeBuffer = false;

    while (threads != end) {
        [[maybe_unused]] ThreadID tid = *threads++;

        // Check the signals for each thread to determine the proper status
        // for each thread.
        // bool updated_status = checkSignalsAndUpdate(tid);
        bool updated_status = false;
        status_change = status_change || updated_status;
    }

    DPRINTF(Fetch, "Running %s stage.\n", this->name());

    // for (threadFetched = 0; threadFetched < numFetchingThreads;
    for (threadFetched = 0; threadFetched < 1;
         threadFetched++) {
        // Fetch each of the actively fetching threads.
        fetch(status_change, upper);
    }

    if (status_change) {
        // Change the fetch stage status if there was a status change.
        // _status = updateFetchStatus();
        _status = Active;
    }

    // If there was activity this cycle, inform the CPU of it.
    if (wroteToTimeBuffer) {
        DPRINTF(Activity, "Activity this cycle.\n");
        cpu->activityThisCycle();
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

template<class Impl>
bool
BaseFetchStage<Impl>::checkStall(ThreadID tid) const
{
    bool ret_val = false;

    if (stalls[tid].drain) {
        assert(cpu->isDraining());
        DPRINTF(Fetch,"[tid:%i] Drain stall detected.\n",tid);
        ret_val = true;
    }

    return ret_val;
}

/*template <class Impl>
bool
PipelineFetch<Impl>::fetchCacheLine(Addr vaddr, ThreadID tid, Addr pc)
{
    Fault fault = NoFault;

    assert(!cpu->switchedOut());

    // @todo: not sure if these should block translation.
    //AlphaDep
    if (cacheBlocked) {
        DPRINTF(Fetch, "[tid:%i] Can't fetch cache line, cache blocked\n",
                tid);
        return false;
    } else if (checkInterrupt(pc) && !delayedCommit[tid]) {
        // Hold off fetch from getting new instructions when:
        // Cache is blocked, or
        // while an interrupt is pending and we're not in PAL mode, or
        // fetch is switched out.
        DPRINTF(Fetch, "[tid:%i] Can't fetch cache line, interrupt pending\n",
                tid);
        return false;
    }

    // Align the fetch address to the start of a fetch buffer segment.
    Addr fetchBufferBlockPC = bufferAlignPC(vaddr, fetchBufferMask);

    DPRINTF(Fetch, "[tid:%i] Fetching cache line %#x for addr %#x\n",
            tid, fetchBufferBlockPC, vaddr);

    // Setup the memReq to do a read of the first instruction's address.
    // Set the appropriate read size and flags as well.
    // Build request here.
    RequestPtr mem_req = std::make_shared<Request>(
        fetchBufferBlockPC, fetchBufferSize,
        Request::INST_FETCH, cpu->instRequestorId(), pc,
        cpu->thread[tid]->contextId());

    mem_req->taskId(cpu->taskId());

    memReq[tid] = mem_req;

    // Initiate translation of the icache block
    fetchStatus[tid] = ItlbWait;
    FetchTranslation *trans = new FetchTranslation(this);
    cpu->mmu->translateTiming(mem_req, cpu->thread[tid]->getTC(),
                              trans, BaseTLB::Execute);
    return true;
}

template <class Impl>
void
PipelineFetch<Impl>::finishTranslation(const Fault &fault,
                                      const RequestPtr &mem_req)
{
    ThreadID tid = cpu->contextToThread(mem_req->contextId());
    Addr fetchBufferBlockPC = mem_req->getVaddr();

    assert(!cpu->switchedOut());

    // Wake up CPU if it was idle
    cpu->wakeCPU();

    if (fetchStatus[tid] != ItlbWait || mem_req != memReq[tid] ||
        mem_req->getVaddr() != memReq[tid]->getVaddr()) {
        DPRINTF(Fetch, "[tid:%i] Ignoring itlb completed after squash\n",
                tid);
        ++fetchStats.tlbSquashes;
        return;
    }


    // If translation was successful, attempt to read the icache block.
    if (fault == NoFault) {
        // Check that we're not going off into random memory
        // If we have, just wait around for commit to squash something and put
        // us on the right track
        if (!cpu->system->isMemAddr(mem_req->getPaddr())) {
            warn("Address %#x is outside of physical memory, stopping fetch\n",
                    mem_req->getPaddr());
            fetchStatus[tid] = NoGoodAddr;
            memReq[tid] = NULL;
            return;
        }

        // Build packet here.
        PacketPtr data_pkt = new Packet(mem_req, MemCmd::ReadReq);
        data_pkt->dataDynamic(new uint8_t[fetchBufferSize]);

        fetchBufferPC[tid] = fetchBufferBlockPC;
        fetchBufferValid[tid] = false;
        DPRINTF(Fetch, "Fetch: Doing instruction read.\n");

        fetchStats.cacheLines++;

        // Access the cache.
        if (!icachePort.sendTimingReq(data_pkt)) {
            assert(retryPkt == NULL);
            assert(retryTid == InvalidThreadID);
            DPRINTF(Fetch, "[tid:%i] Out of MSHRs!\n", tid);

            fetchStatus[tid] = IcacheWaitRetry;
            retryPkt = data_pkt;
            retryTid = tid;
            cacheBlocked = true;
        } else {
            DPRINTF(Fetch, "[tid:%i] Doing Icache access.\n", tid);
            DPRINTF(Activity, "[tid:%i] Activity: Waiting on I-cache "
                    "response.\n", tid);
            lastIcacheStall[tid] = curTick();
            fetchStatus[tid] = IcacheWaitResponse;
            // Notify Fetch Request probe when a packet containing a fetch
            // request is successfully sent
 ppFetchRequestSent->notify(mem_req);
        }
    } else {
        // Don't send an instruction to decode if we can't handle it.
        if (!(numInst < fetchWidth) || !(fetchQueue[tid].size() < fetchQueueSize)) {
            assert(!finishTranslationEvent.scheduled());
            finishTranslationEvent.setFault(fault);
            finishTranslationEvent.setReq(mem_req);
            cpu->schedule(finishTranslationEvent,
                          cpu->clockEdge(Cycles(1)));
            return;
        }
        DPRINTF(Fetch, "[tid:%i] Got back req with addr %#x but expected %#x\n",
                tid, mem_req->getVaddr(), memReq[tid]->getVaddr());
        // Translation faulted, icache request won't be sent.
        memReq[tid] = NULL;

        // Send the fault to commit.  This thread will not do anything
        // until commit handles the fault.  The only other way it can
        // wake up is if a squash comes along and changes the PC.
        TheISA::PCState fetchPC = pc[tid];

        DPRINTF(Fetch, "[tid:%i] Translation faulted, building noop.\n", tid);
        // We will use a nop in ordier to carry the fault.
        DynInstPtr instruction = buildInst(tid, StaticInst::nopStaticInstPtr,
                                           NULL, fetchPC, fetchPC, false);
        instruction->setNotAnInst();

        instruction->setPredTarg(fetchPC);
        instruction->fault = fault;
        wroteToTimeBuffer = true;

        DPRINTF(Activity, "Activity this cycle.\n");
        cpu->activityThisCycle();

        fetchStatus[tid] = TrapPending;

        DPRINTF(Fetch, "[tid:%i] Blocked, need to handle the trap.\n", tid);
        DPRINTF(Fetch, "[tid:%i] fault (%s) detected @ PC %s.\n",
                tid, fault->name(), pc[tid]);
    }
    _status = updateFetchStatus();
}*/

#endif//__CPU_O3_FETCH_PIPE_IMPL_HH__
