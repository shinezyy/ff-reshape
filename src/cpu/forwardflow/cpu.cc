/*
 * Copyright (c) 2011-2012, 2014, 2016, 2017 ARM Limited
 * Copyright (c) 2013 Advanced Micro Devices, Inc.
 * All rights reserved
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Copyright (c) 2004-2006 The Regents of The University of Michigan
 * Copyright (c) 2011 Regents of the University of California
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Authors: Kevin Lim
 *          Korey Sewell
 *          Rick Strong
 */
#include "cpu/forwardflow/cpu.hh"
#include "arch/generic/traits.hh"
#include "config/the_isa.hh"
#include "cpu/activity.hh"
#include "cpu/checker/cpu.hh"
#include "cpu/checker/thread_context.hh"
#include "cpu/forwardflow/isa_specific.hh"
#include "cpu/forwardflow/thread_context.hh"
#include "cpu/simple_thread.hh"
#include "cpu/thread_context.hh"
#include "debug/Activity.hh"
#include "debug/Drain.hh"
#include "debug/FFCPU.hh"
#include "debug/FFCommit.hh"
#include "debug/FFExec.hh"
#include "debug/FFInit.hh"
#include "debug/FFSquash.hh"
#include "debug/Quiesce.hh"
#include "debug/ValueCommit.hh"
#include "enums/MemoryMode.hh"
#include "sim/core.hh"
#include "sim/full_system.hh"
#include "sim/process.hh"
#include "sim/stat_control.hh"
#include "sim/system.hh"


struct BaseCPUParams;

namespace FF{

using namespace TheISA;
using namespace std;

BaseO3CPU::BaseO3CPU(const BaseCPUParams *params)
    : BaseCPU(*params)
{
}

void
BaseO3CPU::regStats()
{
    BaseCPU::regStats();
}

template<class Impl>
bool
FFCPU<Impl>::IcachePort::recvTimingResp(PacketPtr pkt)
{
    DPRINTF(FFCPU, "Fetch unit received timing\n");
    // We shouldn't ever get a cacheable block in Modified state
    assert(pkt->req->isUncacheable() ||
           !(pkt->cacheResponding() && !pkt->hasSharers()));
    fetch->processCacheCompletion(pkt);

    return true;
}

template<class Impl>
void
FFCPU<Impl>::IcachePort::recvReqRetry()
{
    fetch->recvReqRetry();
}

template <class Impl>
bool
FFCPU<Impl>::DcachePort::recvTimingResp(PacketPtr pkt)
{
    return lsq->recvTimingResp(pkt);
}

template <class Impl>
void
FFCPU<Impl>::DcachePort::recvTimingSnoopReq(PacketPtr pkt)
{
    for (ThreadID tid = 0; tid < cpu->numThreads; tid++) {
        if (cpu->getCpuAddrMonitor(tid)->doMonitor(pkt)) {
            cpu->wakeup(tid);
        }
    }
    lsq->recvTimingSnoopReq(pkt);
}

template <class Impl>
void
FFCPU<Impl>::DcachePort::recvReqRetry()
{
    lsq->recvReqRetry();
}

template <class Impl>
FFCPU<Impl>::FFCPU(const DerivFFCPUParams *params)
    : BaseO3CPU(params),
      mmu(params->mmu),
      tickEvent([this]{ tick(); }, "FFCPU tick",
                false, Event::CPU_Tick_Pri),
#ifndef NDEBUG
      instcount(0),
#endif
      removeInstsThisCycle(false),
      fetch(this, params),
      decode(this, params),
      allocation(this, params),
      diewc(this, params),

      isa(numThreads, NULL),

      timeBuffer(params->backComSize, params->forwardComSize),
      fetchQueue(params->backComSize, params->forwardComSize),
      decodeQueue(params->backComSize, params->forwardComSize),
      allocationQueue(params->backComSize, params->forwardComSize),
      DQQueue(params->backComSize, params->forwardComSize),
//      renameQueue(params->backComSize, params->forwardComSize),
//      iewQueue(params->backComSize, params->forwardComSize),
      activityRec(name(), NumStages,
                  params->backComSize + params->forwardComSize,
                  params->activity),

      globalSeqNum(100),
      system(params->system),
      lastRunningCycle(curCycle()),
      fanoutPred(params)
{
    if (!params->switched_out) {
        _status = Running;
    } else {
        _status = SwitchedOut;
    }

    if (params->checker) {
        BaseCPU *temp_checker = params->checker;
        checker = dynamic_cast<Checker<Impl> *>(temp_checker);
        checker->setIcachePort(&this->fetch.getInstPort());
        checker->setSystem(params->system);
    } else {
        checker = NULL;
    }

    if (!FullSystem) {
        thread.resize(numThreads);
        tids.resize(numThreads);
    }

    // The stages also need their CPU pointer setup.  However this
    // must be done at the upper level CPU because they have pointers
    // to the upper level CPU, and not this FFCPU.

    // Set up Pointers to the activeThreads list for each stage
    fetch.setActiveThreads(&activeThreads);
    decode.setActiveThreads(&activeThreads);
    diewc.setActiveThreads(&activeThreads);

    // Give each of the stages the time buffer they will use.
    fetch.setTimeBuffer(&timeBuffer);
    decode.setTimeBuffer(&timeBuffer);
    allocation.setTimeBuffer(&timeBuffer);
    diewc.setTimeBuffer(&timeBuffer);

    // Also setup each of the stages' queues.
    fetch.setFetchQueue(&fetchQueue);
    decode.setFetchQueue(&fetchQueue);
    diewc.setFetchQueue(&fetchQueue);

    decode.setDecodeQueue(&decodeQueue);
    allocation.setDecodeQueue(&decodeQueue);

    allocation.setAllocQueue(&allocationQueue);
    diewc.setAllocQueue(&allocationQueue);
//    diewc.setIEWQueue(&iewQueue); // todo: set self-to-self queue

    fetch.setFanoutPred(&fanoutPred);
    diewc.setFanoutPred(&fanoutPred);

    setPointers();

    dq->setTimeBuf(&DQQueue);

    ThreadID active_threads;
    if (FullSystem) {
        active_threads = 1;
    } else {
        active_threads = params->workload.size();

        if (active_threads > Impl::MaxThreads) {
            panic("Workload Size too large. Increase the 'MaxThreads' "
                  "constant in your FFCPU impl. file (e.g. o3/alpha/impl.hh) "
                  "or edit your workload size.");
        }
    }

    lastActivatedCycle = 0;
#if 0
    // Give renameMap & rename stage access to the freeList;
    for (ThreadID tid = 0; tid < numThreads; tid++)
        globalSeqNum[tid] = 1;
#endif

    DPRINTF(FFCPU, "Creating FFCPU object.\n");

    // Setup any thread state.
    this->thread.resize(this->numThreads);

    for (ThreadID tid = 0; tid < this->numThreads; ++tid) {
        if (FullSystem) {
            // SMT is not supported in FS mode yet.
            assert(this->numThreads == 1);
            this->thread[tid] = new Thread(this, 0, NULL);
        } else {
            if (tid < params->workload.size()) {
                DPRINTF(FFCPU, "Workload[%i] process is %#x",
                        tid, this->thread[tid]);
                this->thread[tid] = new typename FFCPU<Impl>::Thread(
                        (typename Impl::O3CPU *)(this),
                        tid, params->workload[tid]);

                //usedTids[tid] = true;
                //threadMap[tid] = tid;
            } else {
                //Allocate Empty thread so M5 can use later
                //when scheduling threads to CPU
                Process* dummy_proc = NULL;

                this->thread[tid] = new typename FFCPU<Impl>::Thread(
                        (typename Impl::O3CPU *)(this),
                        tid, dummy_proc);
                //usedTids[tid] = false;
            }
        }

        ThreadContext *tc;

        // Setup the TC that will serve as the interface to the threads/CPU.
        O3ThreadContext<Impl> *o3_tc = new O3ThreadContext<Impl>;

        tc = o3_tc;

        // If we're using a checker, then the TC should be the
        // CheckerThreadContext.
        if (params->checker) {
            tc = new CheckerThreadContext<O3ThreadContext<Impl> >(
                o3_tc, this->checker);
        }

        o3_tc->cpu = (typename Impl::O3CPU *)(this);
        assert(o3_tc->cpu);
        o3_tc->thread = this->thread[tid];

        // Give the thread the TC.
        this->thread[tid]->tc = tc;

        // Add the TC to the CPU's list of TC's.
        this->threadContexts.push_back(tc);
    }

    // FFCPU always requires an interrupt controller.
    if (!params->switched_out && interrupts.empty()) {
        fatal("FFCPU %s has no interrupt controller.\n"
              "Ensure createInterruptController() is called.\n", name());
    }

    for (ThreadID tid = 0; tid < this->numThreads; tid++) {
        isa[tid] = dynamic_cast<TheISA::ISA *>(params->isa[tid]);
        this->thread[tid]->setFuncExeInst(0);
    }

    init_difftest();

    diff.nemu_reg = nemu_reg;
    diff.wpc = diff_wpc;
    diff.wdata = diff_wdata;
    diff.wdst = diff_wdst;
}

template <class Impl>
FFCPU<Impl>::~FFCPU()
{
}

template <class Impl>
void
FFCPU<Impl>::regProbePoints()
{
    BaseCPU::regProbePoints();

    ppInstAccessComplete = new ProbePointArg<PacketPtr>(getProbeManager(), "InstAccessComplete");
    ppDataAccessComplete = new ProbePointArg<std::pair<DynInstPtr, PacketPtr> >(
            getProbeManager(), "DataAccessComplete");

    fetch.regProbePoints();
    diewc.regProbePoints();
}

template <class Impl>
void
FFCPU<Impl>::regStats()
{
    BaseO3CPU::regStats();

    // Register any of the FFCPU's stats here.
    timesIdled
        .name(name() + ".timesIdled")
        .desc("Number of times that the entire CPU went into an idle state and"
              " unscheduled itself")
        .prereq(timesIdled);

    idleCycles
        .name(name() + ".idleCycles")
        .desc("Total number of cycles that the CPU has spent unscheduled due "
              "to idling")
        .prereq(idleCycles);

    quiesceCycles
        .name(name() + ".quiesceCycles")
        .desc("Total number of cycles that CPU has spent quiesced or waiting "
              "for an interrupt")
        .prereq(quiesceCycles);

    // Number of Instructions simulated
    // --------------------------------
    // Should probably be in Base CPU but need templated
    // MaxThreads so put in here instead
    committedInsts
        .init(numThreads)
        .name(name() + ".committedInsts")
        .desc("Number of Instructions Simulated")
        .flags(Stats::total);

    committedOps
        .init(numThreads)
        .name(name() + ".committedOps")
        .desc("Number of Ops (including micro ops) Simulated")
        .flags(Stats::total);

    cpi
        .name(name() + ".cpi")
        .desc("CPI: Cycles Per Instruction")
        .precision(6);
    cpi = baseStats.numCycles / committedInsts;

    totalCpi
        .name(name() + ".cpi_total")
        .desc("CPI: Total CPI of All Threads")
        .precision(6);
    totalCpi = baseStats.numCycles / sum(committedInsts);

    ipc
        .name(name() + ".ipc")
        .desc("IPC: Instructions Per Cycle")
        .precision(6);
    ipc =  committedInsts / baseStats.numCycles;

    totalIpc
        .name(name() + ".ipc_total")
        .desc("IPC: Total IPC of All Threads")
        .precision(6);
    totalIpc =  sum(committedInsts) / baseStats.numCycles;

    this->fetch.regStats();
    this->decode.regStats();
    this->allocation.regStats();
    this->diewc.regStats();

    intRegfileReads
        .name(name() + ".int_regfile_reads")
        .desc("number of integer regfile reads")
        .prereq(intRegfileReads);

    intRegfileWrites
        .name(name() + ".int_regfile_writes")
        .desc("number of integer regfile writes")
        .prereq(intRegfileWrites);

    fpRegfileReads
        .name(name() + ".fp_regfile_reads")
        .desc("number of floating regfile reads")
        .prereq(fpRegfileReads);

    fpRegfileWrites
        .name(name() + ".fp_regfile_writes")
        .desc("number of floating regfile writes")
        .prereq(fpRegfileWrites);

    vecRegfileReads
        .name(name() + ".vec_regfile_reads")
        .desc("number of vector regfile reads")
        .prereq(vecRegfileReads);

    vecRegfileWrites
        .name(name() + ".vec_regfile_writes")
        .desc("number of vector regfile writes")
        .prereq(vecRegfileWrites);

    ccRegfileReads
        .name(name() + ".cc_regfile_reads")
        .desc("number of cc regfile reads")
        .prereq(ccRegfileReads);

    ccRegfileWrites
        .name(name() + ".cc_regfile_writes")
        .desc("number of cc regfile writes")
        .prereq(ccRegfileWrites);

    miscRegfileReads
        .name(name() + ".misc_regfile_reads")
        .desc("number of misc regfile reads")
        .prereq(miscRegfileReads);

    miscRegfileWrites
        .name(name() + ".misc_regfile_writes")
        .desc("number of misc regfile writes")
        .prereq(miscRegfileWrites);

    squashedFUTime
        .name(name() + ".squashedFUTime")
        .desc("squashedFUTime");

    lastCommitTick
        .name(name() + ".lastCommitTick")
        .desc("lastCommitTick");
}

template <class Impl>
void
FFCPU<Impl>::tick()
{
    DPRINTF(FFCPU, "\n\nO3CPU: Ticking main, O3CPU.\n");
    assert(!switchedOut());
    assert(drainState() != DrainState::Drained);

    ++baseStats.numCycles;
    updateCycleCounters(BaseCPU::CPU_STATE_ON);

//    activity = false;

    //Tick each of the stages
    fetch.tick();

    decode.tick();

    allocation.tick();

    diewc.tick();

    // Now advance the time buffers
    timeBuffer.advance();

    fetchQueue.advance();
    decodeQueue.advance();
    allocationQueue.advance();
    DQQueue.advance();

    activityRec.advance();

    if (removeInstsThisCycle) {
        cleanUpRemovedInsts();
    }

    if (!tickEvent.scheduled()) {
        if (_status == SwitchedOut) {
            DPRINTF(FFCPU, "Switched out!\n");
            // increment stat
            lastRunningCycle = curCycle();
        } else if (!activityRec.active() || _status == Idle) {
            // DPRINTF(FFCPU, "Idle!\n");
            schedule(tickEvent, clockEdge(Cycles(1)));
            DPRINTF(FFCPU, "Although idle, Scheduling next tick!\n");
            // lastRunningCycle = curCycle();
            // timesIdled++;
        } else {
            schedule(tickEvent, clockEdge(Cycles(1)));
            DPRINTF(FFCPU, "Scheduling next tick!\n");
        }
    }

    if (!FullSystem)
        updateThreadPriority();

    tryDrain();
}

template <class Impl>
void
FFCPU<Impl>::init()
{
    BaseCPU::init();

    for (ThreadID tid = 0; tid < numThreads; ++tid) {
        // Set noSquashFromTC so that the CPU doesn't squash when initially
        // setting up registers.
        thread[tid]->noSquashFromTC = true;
        // Initialise the ThreadContext's memory proxies
        thread[tid]->initMemProxies(thread[tid]->getTC());
        DPRINTF(FFInit, "Init cpu\n");
    }

    // Clear noSquashFromTC.
    for (int tid = 0; tid < numThreads; ++tid)
        thread[tid]->noSquashFromTC = false;

    diewc.setThreads(thread);
}

template <class Impl>
void
FFCPU<Impl>::startup()
{
    BaseCPU::startup();

    fetch.startupStage();
    decode.startupStage();
    allocation.startupStage();
    diewc.startupStage();
}

template <class Impl>
void
FFCPU<Impl>::activateThread(ThreadID tid)
{
    list<ThreadID>::iterator isActive =
        std::find(activeThreads.begin(), activeThreads.end(), tid);

    DPRINTF(FFCPU, "[tid:%i]: Calling activate thread.\n", tid);
    assert(!switchedOut());

    if (isActive == activeThreads.end()) {
        DPRINTF(FFCPU, "[tid:%i]: Adding to active threads list\n",
                tid);

        activeThreads.push_back(tid);
    }
}

template <class Impl>
void
FFCPU<Impl>::deactivateThread(ThreadID tid)
{
    //Remove From Active List, if Active
    list<ThreadID>::iterator thread_it =
        std::find(activeThreads.begin(), activeThreads.end(), tid);

    DPRINTF(FFCPU, "[tid:%i]: Calling deactivate thread.\n", tid);
    assert(!switchedOut());

    if (thread_it != activeThreads.end()) {
        DPRINTF(FFCPU,"[tid:%i]: Removing from active threads list\n",
                tid);
        activeThreads.erase(thread_it);
    }

    fetch.deactivateThread(tid);
    diewc.deactivateThread(tid);
}

template <class Impl>
Counter
FFCPU<Impl>::totalInsts() const
{
    Counter total(0);

    ThreadID size = thread.size();
    for (ThreadID i = 0; i < size; i++)
        total += thread[i]->numInst;

    return total;
}

template <class Impl>
Counter
FFCPU<Impl>::totalOps() const
{
    Counter total(0);

    ThreadID size = thread.size();
    for (ThreadID i = 0; i < size; i++)
        total += thread[i]->numOp;

    return total;
}

template <class Impl>
void
FFCPU<Impl>::activateContext(ThreadID tid)
{
    assert(!switchedOut());

    // Needs to set each stage to running as well.
    activateThread(tid);

    // We don't want to wake the CPU if it is drained. In that case,
    // we just want to flag the thread as active and schedule the tick
    // event from drainResume() instead.
    if (drainState() == DrainState::Drained)
        return;

    // If we are time 0 or if the last activation time is in the past,
    // schedule the next tick and wake up the fetch unit
    if (lastActivatedCycle == 0 || lastActivatedCycle < curTick()) {
        scheduleTickEvent(Cycles(0));

        // Be sure to signal that there's some activity so the CPU doesn't
        // deschedule itself.
        activityRec.activity();
        fetch.wakeFromQuiesce();

        Cycles cycles(curCycle() - lastRunningCycle);
        // @todo: This is an oddity that is only here to match the stats
        if (cycles != 0)
            --cycles;
        quiesceCycles += cycles;

        lastActivatedCycle = curTick();

        _status = Running;

        BaseCPU::activateContext(tid);
    }
}

template <class Impl>
void
FFCPU<Impl>::suspendContext(ThreadID tid)
{
    DPRINTF(FFCPU,"[tid: %i]: Suspending Thread Context.\n", tid);
    assert(!switchedOut());

    deactivateThread(tid);

    // If this was the last thread then unschedule the tick event.
    if (activeThreads.size() == 0) {
        unscheduleTickEvent();
        lastRunningCycle = curCycle();
        _status = Idle;
    }

    DPRINTF(Quiesce, "Suspending Context\n");

    BaseCPU::suspendContext(tid);
}

template <class Impl>
void
FFCPU<Impl>::haltContext(ThreadID tid)
{
    //For now, this is the same as deallocate
    DPRINTF(FFCPU,"[tid:%i]: Halt Context called. Deallocating", tid);
    assert(!switchedOut());

    deactivateThread(tid);
    removeThread(tid);

    // If this was the last thread then unschedule the tick event.
    if (activeThreads.size() == 0) {
        if (tickEvent.scheduled())
        {
            unscheduleTickEvent();
        }
        lastRunningCycle = curCycle();
        _status = Idle;
    }
    updateCycleCounters(BaseCPU::CPU_STATE_SLEEP);
}

template <class Impl>
void
FFCPU<Impl>::insertThread(ThreadID tid)
{
    DPRINTF(FFCPU,"[tid:%i] Initializing thread into CPU");
    // Will change now that the PC and thread state is internal to the CPU
    // and not in the ThreadContext.
    ThreadContext *src_tc;
    if (FullSystem)
        src_tc = system->threads[tid];
    else
        src_tc = tcBase(tid);

    //Copy Thread Data Into RegFile
    //this->copyFromTC(tid);

    //Set PC/NPC/NNPC
    pcState(src_tc->pcState(), tid);

    src_tc->setStatus(ThreadContext::Active);

    activateContext(tid);

    //Reset ROB/IQ/LSQ Entries
    diewc.resetEntries();
}

template <class Impl>
void
FFCPU<Impl>::removeThread(ThreadID tid)
{
    DPRINTF(FFCPU,"[tid:%i] Removing thread context from CPU.\n", tid);

    // Copy Thread Data From RegFile
    // If thread is suspended, it might be re-allocated
    // this->copyToTC(tid);


    // @todo: 2-27-2008: Fix how we free up rename mappings
    // here to alleviate the case for double-freeing registers
    // in SMT workloads.

    // Squash Throughout Pipeline
    DynInstPtr inst = diewc.readTailInst(tid);
    InstSeqNum squash_seq_num = inst->seqNum;
    fetch.squash(0, squash_seq_num, inst, tid);
    decode.squash(tid);
    diewc.squashInFlight();  // todo: check sanity
    diewc.ldstQueue.squash(squash_seq_num, tid);


    assert(diewc.numInWindow() == 0);
    assert(diewc.ldstQueue.getCount(tid) == 0);

    // Reset ROB/IQ/LSQ Entries

    // Commented out for now.  This should be possible to do by
    // telling all the pipeline stages to drain first, and then
    // checking until the drain completes.  Once the pipeline is
    // drained, call resetEntries(). - 10-09-06 ktlim
/*
    if (activeThreads.size() >= 1) {
        commit.rob->resetEntries();
        diewc.resetEntries();
    }
*/
}

template <class Impl>
Fault
FFCPU<Impl>::hwrei(ThreadID tid)
{
    return NoFault;
}

template <class Impl>
bool
FFCPU<Impl>::simPalCheck(int palFunc, ThreadID tid)
{
    return true;
}

template <class Impl>
Fault
FFCPU<Impl>::getInterrupts()
{
    // Check if there are any outstanding interrupts
    return this->interrupts[0]->getInterrupt();
}

template <class Impl>
void
FFCPU<Impl>::processInterrupts(const Fault &interrupt)
{
    // Check for interrupts here.  For now can copy the code that
    // exists within isa_fullsys_traits.hh.  Also assume that thread 0
    // is the one that handles the interrupts.
    // @todo: Possibly consolidate the interrupt checking code.
    // @todo: Allow other threads to handle interrupts.

    assert(interrupt != NoFault);
    this->interrupts[0]->updateIntrInfo();

    DPRINTF(FFCPU, "Interrupt %s being handled\n", interrupt->name());
    this->trap(interrupt, 0, nullptr);
}

template <class Impl>
void
FFCPU<Impl>::trap(const Fault &fault, ThreadID tid,
                      const StaticInstPtr &inst)
{
    // Pass the thread's TC into the invoke method.
    fault->invoke(this->threadContexts[tid], inst);
}

template <class Impl>
void
FFCPU<Impl>::serializeThread(CheckpointOut &cp, ThreadID tid) const
{
    thread[tid]->serialize(cp);
}

template <class Impl>
void
FFCPU<Impl>::unserializeThread(CheckpointIn &cp, ThreadID tid)
{
    thread[tid]->unserialize(cp);
}

template <class Impl>
DrainState
FFCPU<Impl>::drain()
{
    // Deschedule any power gating event (if any)
    deschedulePowerGatingEvent();

    // If the CPU isn't doing anything, then return immediately.
    if (switchedOut())
        return DrainState::Drained;

    DPRINTF(Drain, "Draining...\n");

    // We only need to signal a drain to the commit stage as this
    // initiates squashing controls the draining. Once the commit
    // stage commits an instruction where it is safe to stop, it'll
    // squash the rest of the instructions in the pipeline and force
    // the fetch stage to stall. The pipeline will be drained once all
    // in-flight instructions have retired.
    diewc.drain();

    // Wake the CPU and record activity so everything can drain out if
    // the CPU was not able to immediately drain.
    if (!isDrained())  {
        // If a thread is suspended, wake it up so it can be drained
        for (auto t : threadContexts) {
            if (t->status() == ThreadContext::Suspended){
                DPRINTF(Drain, "Currently suspended so activate %i \n",
                        t->threadId());
                t->activate();
                // As the thread is now active, change the power state as well
                activateContext(t->threadId());
            }
        }

        wakeCPU();
        activityRec.activity();

        DPRINTF(Drain, "CPU not drained\n");

        return DrainState::Draining;
    } else {
        DPRINTF(Drain, "CPU is already drained\n");
        if (tickEvent.scheduled())
            deschedule(tickEvent);

        // Flush out any old data from the time buffers.  In
        // particular, there might be some data in flight from the
        // fetch stage that isn't visible in any of the CPU buffers we
        // test in isDrained().
        for (int i = 0; i < timeBuffer.getSize(); ++i) {
            timeBuffer.advance();
            fetchQueue.advance();
            decodeQueue.advance();
            allocationQueue.advance();
            DQQueue.advance();
        }

        drainSanityCheck();
        return DrainState::Drained;
    }
}

template <class Impl>
bool
FFCPU<Impl>::tryDrain()
{
    if (drainState() != DrainState::Draining || !isDrained())
        return false;

    if (tickEvent.scheduled())
        deschedule(tickEvent);

    DPRINTF(Drain, "CPU done draining, processing drain event\n");
    signalDrainDone();

    return true;
}

template <class Impl>
void
FFCPU<Impl>::drainSanityCheck() const
{
    assert(isDrained());
    fetch.drainSanityCheck();
    decode.drainSanityCheck();
    diewc.drainSanityCheck();
}

template <class Impl>
bool
FFCPU<Impl>::isDrained() const
{
    bool drained(true);

    if (!instList.empty() || !removeList.empty()) {
        DPRINTF(Drain, "Main CPU structures not drained.\n");
        drained = false;
    }

    if (!fetch.isDrained()) {
        DPRINTF(Drain, "Fetch not drained.\n");
        drained = false;
    }

    if (!decode.isDrained()) {
        DPRINTF(Drain, "Decode not drained.\n");
        drained = false;
    }

    if (!diewc.isDrained()) {
        DPRINTF(Drain, "DIEWC not drained.\n");
        drained = false;
    }

    return drained;
}

template <class Impl>
void
FFCPU<Impl>::commitDrained(ThreadID tid)
{
    fetch.drainStall(tid);
}

template <class Impl>
void
FFCPU<Impl>::drainResume()
{
    if (switchedOut())
        return;

    DPRINTF(Drain, "Resuming...\n");
    verifyMemoryMode();

    fetch.drainResume();
    diewc.drainResume();

    _status = Idle;
    for (ThreadID i = 0; i < thread.size(); i++) {
        if (thread[i]->status() == ThreadContext::Active) {
            DPRINTF(Drain, "Activating thread: %i\n", i);
            activateThread(i);
            _status = Running;
        }
    }

    assert(!tickEvent.scheduled());
    if (_status == Running)
        schedule(tickEvent, nextCycle());

    // Reschedule any power gating event (if any)
    schedulePowerGatingEvent();
}

template <class Impl>
void
FFCPU<Impl>::switchOut()
{
    DPRINTF(FFCPU, "Switching out\n");
    BaseCPU::switchOut();

    activityRec.reset();

    _status = SwitchedOut;

    if (checker)
        checker->switchOut();
}

template <class Impl>
void
FFCPU<Impl>::takeOverFrom(BaseCPU *oldCPU)
{
    BaseCPU::takeOverFrom(oldCPU);

    fetch.takeOverFrom();
    decode.takeOverFrom();
    diewc.takeOverFrom();

    assert(!tickEvent.scheduled());

    FFCPU<Impl> *oldO3CPU = dynamic_cast<FFCPU<Impl>*>(oldCPU);
    if (oldO3CPU)
        globalSeqNum = oldO3CPU->globalSeqNum;

    lastRunningCycle = curCycle();
    _status = Idle;
}

template <class Impl>
void
FFCPU<Impl>::verifyMemoryMode() const
{
    if (!system->isTimingMode()) {
        fatal("The O3 CPU requires the memory system to be in "
              "'timing' mode.\n");
    }
}

template <class Impl>
RegVal
FFCPU<Impl>::readMiscRegNoEffect(int misc_reg, ThreadID tid) const
{
    DPRINTF(FFExec, "reading misc reg with no effect\n");
    return this->isa[tid]->readMiscRegNoEffect(misc_reg);
}

template <class Impl>
RegVal
FFCPU<Impl>::readMiscReg(int misc_reg, ThreadID tid)
{
    DPRINTF(FFExec, "reading misc reg for tid %i\n", tid);
    assert(this->isa[tid]);
    miscRegfileReads++;
    return this->isa[tid]->readMiscReg(misc_reg);
}

template <class Impl>
void
FFCPU<Impl>::setMiscRegNoEffect(int misc_reg,
        const RegVal &val, ThreadID tid)
{
    this->isa[tid]->setMiscRegNoEffect(misc_reg, val);
}

template <class Impl>
void
FFCPU<Impl>::setMiscReg(int misc_reg,
        const RegVal &val, ThreadID tid)
{
    miscRegfileWrites++;
    this->isa[tid]->setMiscReg(misc_reg, val);
}


template <class Impl>
uint64_t
FFCPU<Impl>::readArchIntReg(int reg_idx, ThreadID tid)
{
    intRegfileReads++;

    return archState->readIntReg(reg_idx);
}

template <class Impl>
double
FFCPU<Impl>::readArchFloatReg(int reg_idx, ThreadID tid)
{
    fpRegfileReads++;

    return archState->readFloatReg(reg_idx);
}

template <class Impl>
uint64_t
FFCPU<Impl>::readArchFloatRegInt(int reg_idx, ThreadID tid)
{
    fpRegfileReads++;

    return archState->readFloatRegBits(reg_idx);
}

template <class Impl>
auto
FFCPU<Impl>::readArchVecReg(int reg_idx, ThreadID tid) const
        -> const VecRegContainer&
{
    panic("No Vec support in RV-ForwardFlow");
}

template <class Impl>
auto
FFCPU<Impl>::getWritableArchVecReg(int reg_idx, ThreadID tid)
        -> VecRegContainer&
{
    panic("No Vec support in RV-ForwardFlow");
}

template <class Impl>
auto
FFCPU<Impl>::readArchVecElem(const RegIndex& reg_idx, const ElemIndex& ldx,
                                 ThreadID tid) const -> const VecElem&
{
    panic("No Vec support in RV-ForwardFlow");
}

template <class Impl>
RegVal
FFCPU<Impl>::readArchCCReg(int reg_idx, ThreadID tid)
{
    panic("No CC support in RV-ForwardFlow");
}

template <class Impl>
void
FFCPU<Impl>::setArchIntReg(int reg_idx, uint64_t val, ThreadID tid)
{
    intRegfileWrites++;
    DPRINTF(FFInit, "write %llu to int reg(%i)\n", val, reg_idx);

    archState->setIntReg(reg_idx, val);
}

template <class Impl>
void
FFCPU<Impl>::setArchFloatReg(int reg_idx, float val, ThreadID tid)
{
    fpRegfileWrites++;
    DPRINTF(FFInit, "write %f to float reg(%i)\n", val, reg_idx);

    archState->setFloatReg(reg_idx, val);
}

template <class Impl>
void
FFCPU<Impl>::setArchFloatRegInt(int reg_idx, uint64_t val, ThreadID tid)
{
    fpRegfileWrites++;
    DPRINTF(FFInit, "write %llu to float reg(%i)\n", val, reg_idx);

    archState->setFloatRegBits(reg_idx, val);
}

template <class Impl>
void
FFCPU<Impl>::setArchVecReg(int reg_idx, const VecRegContainer& val,
                               ThreadID tid)
{

    panic("No Vec support in RV-ForwardFlow");
}

template <class Impl>
void
FFCPU<Impl>::setArchVecElem(const RegIndex& reg_idx, const ElemIndex& ldx,
                                const VecElem& val, ThreadID tid)
{
    panic("No Vec support in RV-ForwardFlow");
}

template <class Impl>
void
FFCPU<Impl>::setArchCCReg(int reg_idx, RegVal val, ThreadID tid)
{
    panic("No CC support in RV-ForwardFlow");
}

template <class Impl>
TheISA::PCState
FFCPU<Impl>::pcState(ThreadID tid)
{
    return diewc.pcState();
}

template <class Impl>
void
FFCPU<Impl>::pcState(const TheISA::PCState &val, ThreadID tid)
{
    diewc.pcState(val);
}

template <class Impl>
Addr
FFCPU<Impl>::instAddr(ThreadID tid)
{
    return diewc.instAddr();
}

template <class Impl>
Addr
FFCPU<Impl>::nextInstAddr(ThreadID tid)
{
    return diewc.nextInstAddr();
}

template <class Impl>
MicroPC
FFCPU<Impl>::microPC(ThreadID tid)
{
    return diewc.microPC();
}

template <class Impl>
void
FFCPU<Impl>::squashFromTC(ThreadID tid)
{
    this->thread[tid]->noSquashFromTC = true;
    this->diewc.generateTCEvent(tid);
}

template <class Impl>
typename FFCPU<Impl>::ListIt
FFCPU<Impl>::addInst(const DynInstPtr &inst)
{
    instList.push_back(inst);

    return --(instList.end());
}

template <class Impl>
typename FFCPU<Impl>::ListIt
FFCPU<Impl>::addInstAfter(const DynInstPtr &inst, ListIt anchor)
{
    auto younger = std::next(anchor);

    inst->seqNum = (*anchor)->seqNum + 1;
    if (younger != instList.end()) {
        assert((*younger)->seqNum > inst->seqNum);
    }

    instList.insert(younger, inst);

    return ++anchor;
}

template <class Impl>
void
FFCPU<Impl>::instDone(ThreadID tid, const DynInstPtr &inst)
{
    bool should_diff = false;
    // Keep an instruction count.
    if ((!inst->isMicroop() || inst->isLastMicroop()) &&
            !inst->isForwarder()) {
        should_diff = true;
        thread[tid]->numInst++;
        thread[tid]->threadStats.numInsts++;
        committedInsts[tid]++;

        // Check for instruction-count-based events.
        thread[tid]->comInstEventQueue.serviceEvents(thread[tid]->numInst);

        if (this->nextDumpInstCount
                && totalInsts() == this->nextDumpInstCount) {
            fprintf(stderr, "Will trigger stat dump and reset\n");
            Stats::schedStatEvent(true, true, curTick(), 0);

            if (this->repeatDumpInstCount) {
                this->nextDumpInstCount += this->repeatDumpInstCount;
            };
        }

        if (!hasCommit && inst->instAddr() == 0x80000000u) {
            hasCommit = true;
            readGem5Regs();
            gem5_reg[DIFFTEST_THIS_PC] = inst->instAddr();
            ref_difftest_memcpy_from_dut(0x80000000, pmemStart, pmemSize);
            ref_difftest_setregs(gem5_reg);
        }

        if (scFailed) {
            DPRINTF(ValueCommit,
                    "Skip inst after scFailed. Insts since this failure: %u, total failure count = %u\n",
                    scFailed, totalSCFailures);
            should_diff = false;
            scFailed++;

        } else if (scFenceInFlight) {
            assert(inst->isWriteBarrier() && inst->isReadBarrier());
            DPRINTF(ValueCommit, "Skip diff fence generated from LR/SC\n");
            should_diff = false;

        } else {
            should_diff = true;
        }
    }

    scFenceInFlight = false;

    auto old_sc_failed = false;
    if (!inst->isLastMicroop() &&
            inst->isStoreConditional() && !inst->isLastMicroop() && inst->isDelayedCommit()) {
        if (scFailed) {
            old_sc_failed = true;
            scFailed = 0;
        }
        scFenceInFlight = true;
        DPRINTF(ValueCommit, "Diff SC even if it is not the last Microop\n");
        should_diff = true;
    }
    if (should_diff) {
        auto [diff_at, npc_match] = diffWithNEMU(inst);
        if (diff_at != NoneDiff) {
            if (npc_match && diff_at == PCDiff) {
                warn("Found PC mismatch, Let NEMU run one more instruction\n");
                std::tie(diff_at, npc_match) = diffWithNEMU(inst);
                if (diff_at != NoneDiff) {
                    panic("Difftest failed again!\n");
                } else {
                    warn("Difftest matched again, NEMU seems to commit the failed mem instruction\n");
                }

            } else if (scFenceInFlight) {
                scFailed = 1;
                totalSCFailures++;
                warn("SC failed, Let GEM5 run alone until next SC, SC failure #%u\n",
                        totalSCFailures);

            } else {
                panic("Difftest failed!\n");
            }
        } else { // no diff
            if (old_sc_failed) {
                warn("SC failure #%u resolved\n", totalSCFailures);
            }
        }
    }

    if (!inst->isForwarder()) {
        thread[tid]->numOp++;
        thread[tid]->threadStats.numOps++;
        committedOps[tid]++;
    }

    probeInstCommit(inst->staticInst, inst->instAddr());

    lastCommitTick = curTick();
}

template <class Impl>
void
FFCPU<Impl>::removeFrontInst(const DynInstPtr &inst)
{
    DPRINTF(FFCommit, "Adding committed instruction PC %s "
            "[sn:%lli] to remove list @addr: %p\n",
            inst->pcState(), inst->seqNum, inst.get());

    removeInstsThisCycle = true;

    // Remove the front instruction.
    removeList.push(inst->getInstListIt());
}

template <class Impl>
void
FFCPU<Impl>::removeInstsNotInROB(ThreadID tid)
{
    DPRINTF(FFCommit, "Thread %i: Deleting instructions from instruction"
            " list.\n", tid);

    ListIt end_it;

    bool rob_empty = false;

    // todo: fix in FF
    if (instList.empty()) {
        return;
    } else if (dq->isEmpty()) {
        DPRINTF(FFCommit, "ROB is empty, squashing all insts.\n");
        end_it = instList.begin();
        rob_empty = true;
    } else {
        end_it = (dq->getHead())->getInstListIt();
        DPRINTF(FFCommit, "ROB is not empty, squashing insts not in ROB.\n");
    }

    removeInstsThisCycle = true;

    ListIt inst_it = instList.end();

    inst_it--;

    // Walk through the instruction list, removing any instructions
    // that were inserted after the given instruction iterator, end_it.
    while (inst_it != end_it) {
        assert(!instList.empty());

        squashInstIt(inst_it, tid);

        inst_it--;
    }

    // If the ROB was empty, then we actually need to remove the first
    // instruction as well.
    if (rob_empty) {
        squashInstIt(inst_it, tid);
    }
}

template <class Impl>
void
FFCPU<Impl>::removeInstsUntil(const InstSeqNum &seq_num, ThreadID tid)
{
    if (seq_num > 0) {
        assert(!instList.empty());
    }

    removeInstsThisCycle = true;

    ListIt inst_iter = instList.end();

    inst_iter--;

    if (inst_iter == instList.end()) {
        DPRINTF(FFSquash, "Ignore squash because no inst found\n");
        return;
    }

    DPRINTF(FFCommit, "Deleting instructions from instruction "
            "list that are from [tid:%i] and above [sn:%lli] (end=%lli).\n",
            tid, seq_num, (*inst_iter)->seqNum);

    while ((*inst_iter)->seqNum > seq_num) {

        bool break_loop = (inst_iter == instList.begin());

        squashInstIt(inst_iter, tid);

        inst_iter--;

        if (break_loop)
            break;
    }
}

template <class Impl>
inline void
FFCPU<Impl>::squashInstIt(const ListIt &instIt, ThreadID tid)
{
    if ((*instIt)->threadNumber == tid) {
        DPRINTF(FFSquash, "Squashing instruction, "
                "[tid:%i] [sn:%lli] PC %s\n",
                (*instIt)->threadNumber,
                (*instIt)->seqNum,
                (*instIt)->pcState());

        if ((*instIt)->isLoad()) {
            dq->squashLoad(*instIt);
        }

        // Mark it as squashed.
        (*instIt)->setSquashed();

        // @todo: Formulate a consistent method for deleting
        // instructions from the instruction list
        // Remove the instruction from the list.
        removeList.push(instIt);

        if ((*instIt)->isExecuted()) {
            squashedFUTime += (*instIt)->opLat;
        }
    }
}

template <class Impl>
void
FFCPU<Impl>::cleanUpRemovedInsts()
{
    while (!removeList.empty()) {
        DPRINTF(FFCommit, "Removing instruction @addr: %p\n",
                (*removeList.front()).get());
        DPRINTF(FFCommit, "Removing instruction, "
                "[tid:%i] [sn:%lli] PC %s\n",
                (*removeList.front())->threadNumber,
                (*removeList.front())->seqNum,
                (*removeList.front())->pcState());

        instList.erase(removeList.front());

        removeList.pop();
    }

    if (Debug::FFCommit) {
        if (curTick() % 50000 == 0 || curTick() % 50500 == 0) {
            dumpInsts();
        }
    }

    removeInstsThisCycle = false;
}
/*
template <class Impl>
void
FFCPU<Impl>::removeAllInsts()
{
    instList.clear();
}
*/
template <class Impl>
void
FFCPU<Impl>::dumpInsts()
{
    int num = 0;

    ListIt inst_list_it = instList.begin();

    cprintf("Dumping Instruction List\n");

    while (inst_list_it != instList.end()) {
        cprintf("Instruction:%i\nPC:%#x\n[tid:%i]\n[sn:%lli]\nIssued:%i\n"
                "Squashed:%i\n %s\n\n",
                num, (*inst_list_it)->instAddr(), (*inst_list_it)->threadNumber,
                (*inst_list_it)->seqNum, (*inst_list_it)->isIssued(),
                (*inst_list_it)->isSquashed(),
                (*inst_list_it)->staticInst->disassemble((*inst_list_it)->instAddr()));
        inst_list_it++;
        ++num;
    }
}

template <class Impl>
void
FFCPU<Impl>::wakeCPU()
{
    if (activityRec.active() || tickEvent.scheduled()) {
        DPRINTF(Activity, "CPU already running.\n");
        return;
    }

    DPRINTF(Activity, "Waking up CPU\n");

    Cycles cycles(curCycle() - lastRunningCycle);
    // @todo: This is an oddity that is only here to match the stats
    if (cycles > 1) {
        --cycles;
        idleCycles += cycles;
        baseStats.numCycles += cycles;
    }

    schedule(tickEvent, clockEdge());
}

template <class Impl>
void
FFCPU<Impl>::wakeup(ThreadID tid)
{
    if (this->thread[tid]->status() != ThreadContext::Suspended)
        return;

    this->wakeCPU();

    DPRINTF(Quiesce, "Suspended Processor woken\n");
    this->threadContexts[tid]->activate();
}

template <class Impl>
ThreadID
FFCPU<Impl>::getFreeTid()
{
    for (ThreadID tid = 0; tid < numThreads; tid++) {
        if (!tids[tid]) {
            tids[tid] = true;
            return tid;
        }
    }

    return InvalidThreadID;
}

template <class Impl>
void
FFCPU<Impl>::updateThreadPriority()
{
    if (activeThreads.size() > 1) {
        //DEFAULT TO ROUND ROBIN SCHEME
        //e.g. Move highest priority to end of thread list
        list<ThreadID>::iterator list_begin = activeThreads.begin();

        unsigned high_thread = *list_begin;

        activeThreads.erase(list_begin);

        activeThreads.push_back(high_thread);
    }
}

template<class Impl>
uint64_t FFCPU<Impl>::readIntReg(DQPointer ptr) {
    panic("Should not be used!\n");
}

template<class Impl>
double FFCPU<Impl>::readFloatReg(DQPointer ptr) {
    panic("Should not be used!\n");
}

template<class Impl>
uint64_t FFCPU<Impl>::readFloatRegBits(DQPointer ptr) {
    panic("Should not be used!\n");
}

// template<class Impl>
// void FFCPU<Impl>::setIntReg(DQPointer ptr, uint64_t val_) {
//     FFRegValue val;
//     val.i = val_;
//     dq->setReg(ptr, val);
// }
//
// template<class Impl>
// void FFCPU<Impl>::setFloatReg(DQPointer ptr, double val_) {
//     FFRegValue val;
//     val.f = val_;
//     dq->setReg(ptr, val);
// }
//
// template<class Impl>
// void FFCPU<Impl>::setFloatRegBits(DQPointer ptr, uint64_t val_) {
//     FFRegValue val;
//     val.i = val_;
//     dq->setReg(ptr, val);
// }

template<class Impl>
void FFCPU<Impl>::setPointers()
{
    archState = diewc.getArchState();
    dq = diewc.getDQ();
}

template <class Impl>
void
FFCPU<Impl>::readGem5Regs()
{
    for (int i = 0; i < 32; i++) {
        gem5_reg[i] = 0;
        gem5_reg[i + 32] = 0;
    }
}

template <class Impl>
std::pair<int, bool>
FFCPU<Impl>::diffWithNEMU(const DynInstPtr &inst)
{
    int diff_at = DiffAt::NoneDiff;
    bool npc_match = false;

    difftest_step(&diff);

    auto gem5_pc = inst->instAddr();
    auto nemu_pc = nemu_reg[DIFFTEST_THIS_PC];
    DPRINTF(ValueCommit, "NEMU PC: %#x, GEM5 PC: %#x\n", nemu_pc, gem5_pc);

    auto nemu_store_addr = nemu_reg[DIFFTEST_STORE_ADDR];
    if (nemu_store_addr) {
        DPRINTF(ValueCommit, "NEMU store addr: %#lx\n", nemu_store_addr);
    }

    uint8_t gem5_inst[5];
    uint8_t nemu_inst[9];

    int nemu_inst_len = nemu_reg[DIFFTEST_RVC] ? 2 : 4;
    int gem5_inst_len = inst->pcState().compressed() ? 2 : 4;

    assert(inst->staticInst->asBytes(gem5_inst, 8));
    *reinterpret_cast<uint32_t *>(nemu_inst) =
        htole<uint32_t>(nemu_reg[DIFFTEST_INST_PAYLOAD]);

    if (nemu_pc != gem5_pc) {
        warn("NEMU store addr: %#lx\n", nemu_store_addr);
        warn("Inst [sn:%lli]\n", inst->seqNum);
        warn("Diff at %s, NEMU: %#lx, GEM5: %#lx\n",
                "PC", nemu_pc, gem5_pc
            );
        if (!diff_at) {
            diff_at = PCDiff;
            if (diff.npc == gem5_pc) {
                npc_match = true;
            }
        }
    }

    if (diff.npc != inst->nextInstAddr()) {
        warn("Inst [sn:%lli]\n", inst->seqNum);
        warn("Diff at %s, NEMU: %#lx, GEM5: %#lx\n",
                "NPC", diff.npc, inst->nextInstAddr()
            );
        if (!diff_at)
            diff_at = NPCDiff;
    } else {
        DPRINTF(ValueCommit, "Inst [sn:%lli] %s, NEMU: %#lx, GEM5: %#lx\n",
                inst->seqNum, "NPC", diff.npc, inst->nextInstAddr()
               );
    }

    if (nemu_inst_len != gem5_inst_len ||
            memcmp(gem5_inst, nemu_inst, nemu_inst_len) != 0) {
        warn("Inst [sn:%lli]\n", inst->seqNum);
        warn("Diff at %s, NEMU: %02x %02x %02x %02x (%i),"
                "GEM5: %02x %02x %02x %02x (%i)\n",
                "inst payload",
                nemu_inst[0], nemu_inst[1], nemu_inst[2], nemu_inst[3], nemu_inst_len,
                gem5_inst[0], gem5_inst[1], gem5_inst[2], gem5_inst[3], gem5_inst_len
            );
        if (!diff_at)
            diff_at = InstDiff;
    }

    DPRINTF(ValueCommit, "Inst [sn:%llu] @ %#lx is %s\n",
            inst->seqNum, inst->instAddr(),
            inst->staticInst->disassemble(inst->instAddr()));

    // auto gem5_mstatus = readMiscRegNoEffect(MISCREG_STATUS, 0);
    // auto nemu_mstatus = nemu_reg[DIFFTEST_MSTATUS];
    // DPRINTF(ValueCommit, "%s: NEMU = %#lx, GEM5 = %#lx\n",
    //         "mstatus", nemu_mstatus, gem5_mstatus);

    if (inst->numDestRegs() > 0) {
        const auto &dest = inst->staticInst->destRegIdx(0);
        auto dest_tag = dest.index() + dest.isFloatReg() * 32;

        auto gem5_val = inst->getResult().asIntegerNoAssert();
        auto nemu_val = nemu_reg[dest_tag];

        DPRINTF(ValueCommit, "%s NEMU value: %#lx, GEM5 value: %#lx\n",
                ::reg_name[dest_tag], nemu_val, gem5_val);

        if ((dest.isFloatReg() || dest.isIntReg()) && !dest.isZeroReg()) {

            if (gem5_val != nemu_val) {
                if (dest.isFloatReg() && (gem5_val^nemu_val) == ((0xffffffffULL) << 32)) {
                    DPRINTF(ValueCommit, "Difference might be caused by box, ignore it\n");

                // } else if (0x40600000 <= inst->physEffAddr && inst->physEffAddr <= 0x4060000c) {
                //     DPRINTF(ValueCommit, "Difference might be caused by read %s at %#lx, ignore it\n",
                //             "serial", inst->physEffAddr);

                } else {
                    warn("Inst [sn:%lli]\n", inst->seqNum);
                    warn("Diff at %s Ref value: %#lx, GEM5 value: %#lx\n",
                            ::reg_name[dest_tag], nemu_val, gem5_val
                        );
                    if (!diff_at)
                        diff_at = ValueDiff;
                }
            }
        }
    }
    return std::make_pair(diff_at, npc_match);
}

ThreadID DummyTid = 0;

}
// Forward declaration of FFCPU.
template class FF::FFCPU<FFCPUImpl>;

#include "cpu/forwardflow/deriv.hh"

#include "params/DerivFFCPU.hh"

DerivFFCPU *
DerivFFCPUParams::create() const
{
    return new DerivFFCPU(this);
}