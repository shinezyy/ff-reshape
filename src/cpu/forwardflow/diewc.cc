//
// Created by zyy on 19-6-11.
//
#include "cpu/forwardflow/diewc.hh"

#include "arch/utility.hh"
#include "debug/Commit.hh"
#include "debug/CommitRate.hh"
#include "debug/DIEWC.hh"
#include "debug/Drain.hh"
#include "debug/ExecFaulting.hh"
#include "debug/IEW.hh"
#include "debug/O3PipeView.hh"
#include "params/DerivFFCPU.hh"
#include "store_set.hh"

namespace FF{

using namespace std;

template<class Impl>
void FFDIEWC<Impl>::tick() {

    // todo: commit from DQ
    // part DQC
    // part DQC should not read from DQCR in the same cycle
    commit();


    // todo: read from DQ head To Simulate that DQ is a RAM instead of combinational.
    // part DQCR
    writeCommitQueue();


    // todo: Execute ready insts
    execute();

    dq.clear();


    // todo read allocated instructions from allocation stage
    // part receive
//    DPRINTF(DIEWC, "tick reach 0\n");
    readInsts();
//    DPRINTF(DIEWC, "tick reach 1\n");
    checkSignalsAndUpdate();
//    DPRINTF(DIEWC, "tick reach 2\n");
    // todo insert these insts into LSQ until full
    // if no LSQ entry allocated for an LD/ST inst, it and further insts should not be insert into DQ (block)

    // todo insert these insts into corresponding banks,
    //  what fields should be filled:
    //  - source operand value: stacked mask
    //  RTL should be: Vsrc7.1 = d6 == src7.1 ? Vd6 : (d5 == src7.1 ? Vd5 : ...)
    // part bring value
    tryDispatch();

//    DPRINTF(DIEWC, "tick reach 3\n");
    // todo: forward pointers should be calculated
    //  - source operand forward pointer (if any sibling)
    //  - dest operand forward pointer
    //  these pointers should be calculated in one cycle and write to the pointer queues
    //  calculate newest definer and newest consumer, then route forward pointers to them
    // part route pointer

    forward();
//    DPRINTF(DIEWC, "tick reach 4\n");

    // todo: remember to advance
    //  - issue to execution queue
    //  - commit queue
    //  - timebuffers inside DQ
    advanceQueues();
//    DPRINTF(DIEWC, "tick reach 5\n");

    // todo: DQ tick
    // part DQ tick: should not read values from part bring value part route pointer in the same cycle
    dq.tick();
//    DPRINTF(DIEWC, "tick reach 6\n");

}

template<class Impl>
void FFDIEWC<Impl>::squash(DynInstPtr &inst) {
    // todo calculate invalidation mask, write to DQ valid bits

    // todo find corresponding checkpoint of the consumer table, reverse to it

    // todo notify previous stages to flush instructions
}

template<class Impl>
void FFDIEWC<Impl>::checkSignalsAndUpdate() {
    if (fromLastCycle->diewc2diewc.squash) {
        squashInFlight();
        if (dispatchStatus == Blocked ||
            dispatchStatus == Unblocking) {
            toAllocation->diewcUnblock = true;
            wroteToTimeBuffer = true;
        }
        dispatchStatus = Squashing;
        fetchRedirect = false;
        return;
    }
    if (dqSqushing) {
        dispatchStatus = Squashing;
        clearAllocatedInsts();
        wroteToTimeBuffer = true;
    }
    if (checkStall()){
        DPRINTF(DIEWC, "block after checkStall\n");
        block();
        dispatchStatus = Blocked;
        return;
    }
    if (dispatchStatus == Blocked) {
        dispatchStatus = Unblocking;
        unblock();
        return;
    }
    if (dispatchStatus == Squashing) {
        dispatchStatus = Running;
        return;
    }
}

template<class Impl>
void FFDIEWC<Impl>::readInsts() {
    int num = fromAllocation->size;
    DPRINTF(DIEWC, "num from Allocation is %d\n", num);
    for (int i = 0; i < num; ++i) {
        DynInstPtr inst = fromAllocation->insts[i];
        insts_from_allocation.push(inst);
    }
}

template<class Impl>
void FFDIEWC<Impl>::dispatch() {

//    DPRINTF(DIEWC, "dispatch reach 0\n");
    InstQueue &to_dispatch = dispatchStatus == Unblocking ?
            skidBuffer : insts_from_allocation;
    int num_insts_to_disp = static_cast<int>(to_dispatch.size());
    DynInstPtr inst = nullptr;
    bool normally_add_to_dq = false;
    int dispatched = 0;

//    DPRINTF(DIEWC, "dispatch reach 1\n");
    for (; dispatched < num_insts_to_disp &&
           dispatched < dispatchWidth;
           ++dispatched) {
//        DPRINTF(DIEWC, "dispatch reach 2\n");
        inst = to_dispatch.front();
        assert(inst);

        if (inst->isSquashed()) {
            ++dispSquashedInsts;
            to_dispatch.pop();

            if (inst->isLoad()) {
                toAllocation->diewcInfo.dispatchedToLQ++;
            }
            if (inst->isStore()) {
                toAllocation->diewcInfo.dispatchedToSQ++;
            }
            toAllocation->diewcInfo.dispatched++;

            continue;
        }

//        DPRINTF(DIEWC, "dispatch reach 3\n");
        if (dq.isFull()) {
            DPRINTF(DIEWC, "block because DQ is full\n");
            block();
            toAllocation->diewcUnblock = false;
            ++dqFullEvents;
            break;
        }

//        DPRINTF(DIEWC, "dispatch reach 4\n");
        if ((inst->isLoad() && ldstQueue.lqFull()) ||
            (inst->isStore() && ldstQueue.sqFull())) {
            if (inst->isLoad()) {
                ++lqFullEvents;
            } else {
                ++sqFullEvents;
            }
            DPRINTF(DIEWC, "block because LSQ is full\n");
            block();
            toAllocation->diewcUnblock = false;
            break;
        }

//        DPRINTF(DIEWC, "dispatch reach 5\n");
        if (inst->isLoad()) {
            ldstQueue.insertLoad(inst);
            ++dispaLoads;
            normally_add_to_dq = true;
            toAllocation->diewcInfo.dispatchedToLQ++;

        } else if (inst->isStore()) {
            ldstQueue.insertStore(inst);
            ++dispStores;
            if (inst->isStoreConditional()) {
                panic("There should not be store conditional in Risc-V\n");
            }
            normally_add_to_dq = true;
            toAllocation->diewcInfo.dispatchedToLQ++;

        } else if (inst->isMemBarrier() || inst->isWriteBarrier()) {
            inst->setCanCommit();

            // get who is the oldest consumer
            insertPointerPairs(archState.recordAndUpdateMap(inst));

            dq.insertBarrier(inst);
            normally_add_to_dq = false;

        } else if (inst->isNop()) {
            inst->setIssued();
            inst->setExecuted();
            inst->setCanCommit();
            // dq.recordProducer(inst); // do not need this in FF
            archState.recordAndUpdateMap(inst);
            normally_add_to_dq = true;
        } else {
            assert(!inst->isExecuted());
            normally_add_to_dq = true;
        }

//        DPRINTF(DIEWC, "dispatch reach 6\n");
        if (normally_add_to_dq && inst->isNonSpeculative()) {
            inst->setCanCommit();

            insertPointerPairs(archState.recordAndUpdateMap(inst));
            dq.insertNonSpec(inst);

            ++dispNonSpecInsts;
            normally_add_to_dq = false;
        }

//        DPRINTF(DIEWC, "dispatch reach 7\n");
        if (normally_add_to_dq) {
//            DPRINTF(DIEWC, "dispatch reach 7.1\n");
            insertPointerPairs(archState.recordAndUpdateMap(inst));
//            DPRINTF(DIEWC, "dispatch reach 7.2\n");
            dq.insert(inst);
//            DPRINTF(DIEWC, "dispatch reach 7.3\n");
        }

//        DPRINTF(DIEWC, "dispatch reach 8\n");
        to_dispatch.pop();
        toAllocation->diewcInfo.dispatched++;
        ++dispatchedInsts;
#if TRACING_ON
        inst->dispatchTick = curTick() - inst->fetchTick;
#endif
        ppDispatch->notify(inst);
    }

//    DPRINTF(DIEWC, "dispatch reach 8\n");
    if (!to_dispatch.empty()) {
        DPRINTF(DIEWC, "block because instruction not used up\n");
        block();
        toAllocation->diewcUnblock = false;
    }

    if (dispatchStatus == Idle && dispatched) {
        dispatchStatus = Running;
        updatedQueues = true;
    }
}

template<class Impl>
void FFDIEWC<Impl>::forward() {
    while (!pointerPackets.empty()) {
        // DQ is responsible for the rest stuffs
        dq.insertForwardPointer(pointerPackets.front());
        pointerPackets.pop();
    }
}

template<class Impl>
void FFDIEWC<Impl>::advanceQueues() {
    // todo: fill
}

template<class Impl>
void FFDIEWC<Impl>::writeCommitQueue() {
    for (unsigned i = 0; i < width; i++) {
        DynInstPtr inst = dq.getHead();
        if (!inst) {
            break;
        }
        toNextCycle->diewc2diewc.commitQueue[i] = inst;
    }
}

template<class Impl>
void FFDIEWC<Impl>::commit() {
    // read insts
    for (unsigned i = 0; i < width; i++) {
        insts_to_commit.push(fromLastCycle->diewc2diewc.commitQueue[i]);
    }

    handleSquash();

    if (commitStatus != DQSquashing) {
        commitInsts();
    }

    // todo: ppStall here?
}

template<class Impl>
bool FFDIEWC<Impl>::checkStall() {
    // todo: tix mysterious stall
    if (dqSqushing) {
        return true;
    }
    if (dq.stallToUnclog()) {
        DPRINTF(DIEWC, "block because dq unclogging\n");
        return true;
    }
    return false;
}

template<class Impl>
void FFDIEWC<Impl>::block() {
    if (dispatchStatus != Blocked &&
            dispatchStatus != Unblocking) {
        toAllocation->diewcBlock = true;
        wroteToTimeBuffer = true;
    }

    DPRINTF(DIEWC, "skidInsert in func block\n");
    skidInsert();
    dispatchStatus = Blocked;
}

template<class Impl>
void FFDIEWC<Impl>::unblock() {
    if (skidBuffer.empty()) {
        toAllocation->diewcUnblock = true;
        wroteToTimeBuffer = true;
        dispatchStatus = Running;
    }
}

template<class Impl>
void FFDIEWC<Impl>::tryDispatch() {
    switch (dispatchStatus) {
        case Blocked:
            ++blockCycles;
            break;

        case Squashing:
            ++squashCycles;
            break;

        case Running:
        case Idle:
            ++runningCycles;
            dispatch();
            break;

        case Unblocking: {
            assert(!skidBuffer.empty());
            dispatch();
            ++unblockCycles;
            if (validInstsFromAllocation())  {
                DPRINTF(DIEWC, "skidInsert after dispatch in Unblocking\n");
                skidInsert();
            }
            break;
        }
    }
}

template<class Impl>
bool FFDIEWC<Impl>::validInstsFromAllocation() {
    for (int i = 0; i < fromAllocation->size; i++) {
        if (!fromAllocation->insts[i]->isSquashed()) {
            return true;
        }
    }
    return false;
}

template<class Impl>
void FFDIEWC<Impl>::skidInsert() {
    DynInstPtr inst = nullptr;
    while (!insts_from_allocation.empty()) {
        inst = insts_from_allocation.front();
        DPRINTF(DIEWC, "skidInsert inst[%i]\n", inst->seqNum);
        insts_from_allocation.pop();
        skidBuffer.push(inst);
    }
    assert(skidBuffer.size() <= skidBufferMax);
}

template<class Impl>
void FFDIEWC<Impl>::execute() {
    fuWrapper.tick();
}

template <class Impl>
bool
FFDIEWC<Impl>::
        commitHead(DynInstPtr &head_inst, unsigned inst_num)
{
    assert(head_inst);

    // If the instruction is not executed yet, then it will need extra
    // handling.  Signal backwards that it should be executed.
    if (!head_inst->isExecuted()) {
        // Keep this number correct.  We have not yet actually executed
        // and committed this instruction.
        thread->funcExeInst--;

        // Make sure we are only trying to commit un-executed instructions we
        // think are possible.
        assert(head_inst->isNonSpeculative() || head_inst->isStoreConditional()
               || head_inst->isMemBarrier() || head_inst->isWriteBarrier() ||
               (head_inst->isLoad() && head_inst->strictlyOrdered()));

        DPRINTF(Commit, "Encountered a barrier or non-speculative "
                "instruction [sn:%lli] at the head of the ROB, PC %s.\n",
                head_inst->seqNum, head_inst->pcState());

        if (inst_num > 0 || hasStoresToWB) {
            DPRINTF(Commit, "Waiting for all stores to writeback.\n");
            return false;
        }

        toNextCycle->diewc2diewc.nonSpecSeqNum = head_inst->seqNum;

        // Change the instruction so it won't try to commit again until
        // it is executed.
        head_inst->clearCanCommit();

        if (head_inst->isLoad() && head_inst->strictlyOrdered()) {
            DPRINTF(Commit, "[sn:%lli]: Strictly ordered load, PC %s.\n",
                    head_inst->seqNum, head_inst->pcState());
            toNextCycle->diewc2diewc.strictlyOrdered = true;
            toNextCycle->diewc2diewc.strictlyOrderedLoad = head_inst;
        } else {
            ++commitNonSpecStalls;
        }

        return false;
    }

    if (head_inst->isThreadSync()) {
        // Not handled for now.
        panic("Thread sync instructions are not handled yet.\n");
    }

    // Check if the instruction caused a fault.  If so, trap.
    Fault inst_fault = head_inst->getFault();

    // Stores mark themselves as completed.
    if (!head_inst->isStore() && inst_fault == NoFault) {
        head_inst->setCompleted();
    }

    if (inst_fault != NoFault) {
        DPRINTF(Commit, "Inst [sn:%lli] PC %s has a fault\n",
                head_inst->seqNum, head_inst->pcState());

        if (hasStoresToWB || inst_num > 0) {
            DPRINTF(Commit, "Stores outstanding, fault must wait.\n");
            return false;
        }

        head_inst->setCompleted();

        // If instruction has faulted, let the checker execute it and
        // check if it sees the same fault and control flow.
        if (cpu->checker) {
            // Need to check the instruction before its fault is processed
            cpu->checker->verify(head_inst);
        }

        assert(!thread->noSquashFromTC);

        // Mark that we're in state update mode so that the trap's
        // execution doesn't generate extra squashes.
        thread->noSquashFromTC = true;

        // Execute the trap.  Although it's slightly unrealistic in
        // terms of timing (as it doesn't wait for the full timing of
        // the trap event to complete before updating state), it's
        // needed to update the state as soon as possible.  This
        // prevents external agents from changing any specific state
        // that the trap need.
        cpu->trap(inst_fault, 0,
                  head_inst->notAnInst() ?
                      StaticInst::nullStaticInstPtr :
                      head_inst->staticInst);

        // Exit state update mode to avoid accidental updating.
        thread->noSquashFromTC = false;

        commitStatus = TrapPending;

        DPRINTF(Commit, "Committing instruction with fault [sn:%lli]\n",
            head_inst->seqNum);
        if (head_inst->traceData) {
            if (DTRACE(ExecFaulting)) {
                head_inst->traceData->setFetchSeq(head_inst->seqNum);
                head_inst->traceData->setCPSeq(thread->numOp);
                head_inst->traceData->dump();
            }
            delete head_inst->traceData;
            head_inst->traceData = NULL;
        }

        // Generate trap squash event.
        generateTrapEvent(DummyTid, inst_fault);
        return false;
    }

    updateComInstStats(head_inst);

    if (FullSystem) {
        panic("FF does not consider FullSystem yet\n");
    }
    DPRINTF(Commit, "Committing instruction with [sn:%lli] PC %s\n",
            head_inst->seqNum, head_inst->pcState());
    if (head_inst->traceData) {
        head_inst->traceData->setFetchSeq(head_inst->seqNum);
        head_inst->traceData->setCPSeq(thread->numOp);
        head_inst->traceData->dump();
        delete head_inst->traceData;
        head_inst->traceData = NULL;
    }
    if (head_inst->isReturn()) {
        DPRINTF(Commit,"Return Instruction Committed [sn:%lli] PC %s \n",
                        head_inst->seqNum, head_inst->pcState());
    }

    // Update the commit rename map
    archState.commitInst(head_inst);
    dq.retireHead();

#if TRACING_ON
    if (DTRACE(O3PipeView)) {
        head_inst->commitTick = curTick() - head_inst->fetchTick;
    }
#endif

    // If this was a store, record it for this cycle.
    if (head_inst->isStore())
        committedStores = true;

    // Return true to indicate that we have committed an instruction.
    return true;
}

template <class Impl>
void
FFDIEWC<Impl>::generateTrapEvent(ThreadID tid, Fault inst_fault)
{
    DPRINTF(Commit, "Generating trap event for [tid:%i]\n", tid);

    EventFunctionWrapper *trap = new EventFunctionWrapper(
        [this, tid]{ processTrapEvent(tid); },
        "Trap", true, Event::CPU_Tick_Pri);

    Cycles latency = dynamic_pointer_cast<SyscallRetryFault>(inst_fault) ?
                     cpu->syscallRetryLatency : trapLatency;

    cpu->schedule(trap, cpu->clockEdge(latency));
    trapInFlight = true;
    thread->trapPending = true;
}

template <class Impl>
void
FFDIEWC<Impl>::processTrapEvent(ThreadID tid)
{
    // This will get reset by commit if it was switched out at the
    // time of this event processing.
    trapSquash = true;
}



template <class Impl>
void
FFDIEWC<Impl>::commitInsts()
{
    // Can't commit and squash things at the same time...

    DPRINTF(Commit, "Trying to commit instructions in the ROB.\n");

    unsigned num_committed = 0;

    DynInstPtr head_inst;

    // Commit as many instructions as possible until the commit bandwidth
    // limit is reached, or it becomes impossible to commit any more.
    while (num_committed < width) {
        // Check for any interrupt that we've already squashed for
        // and start processing it.
        if (interrupt != NoFault)
            handleInterrupt();

        head_inst = getHeadInst();

        if (!head_inst) {
            num_committed++;
            continue;
        }

        DPRINTF(Commit, "Trying to commit head instruction, [sn:%i]\n",
                head_inst->seqNum);

        // If the head instruction is squashed, it is ready to retire
        // (be removed from the ROB) at any time.
        if (head_inst->isSquashed()) {

            DPRINTF(Commit, "Retiring squashed instruction from "
                    "ROB.\n");

            dq.retireHead();

            ++commitSquashedInsts;
            // Notify potential listeners that this instruction is squashed
            ppSquash->notify(head_inst);

            // Record that the number of ROB entries has changed.
            changedDQNumEntries = true;
        } else {
            pc = head_inst->pcState();

            // Increment the total number of non-speculative instructions
            // executed.
            // Hack for now: it really shouldn't happen until after the
            // commit is deemed to be successful, but this count is needed
            // for syscalls.
            thread->funcExeInst++;

            // Try to commit the head instruction.
            bool commit_success = commitHead(head_inst, num_committed);

            if (commit_success) {
                ++num_committed;
                statCommittedInstType[head_inst->opClass()]++;
                ppCommit->notify(head_inst);

                changedDQNumEntries = true;

                // Set the doneSeqNum to the youngest committed instruction.
                toNextCycle->diewc2diewc.doneSeqNum = head_inst->seqNum;

                canHandleInterrupts =  (!head_inst->isDelayedCommit()) &&
                                       ((THE_ISA != ALPHA_ISA) ||
                                         (!(pc.instAddr() & 0x3)));

                // at this point store conditionals should either have
                // been completed or predicated false
                assert(!head_inst->isStoreConditional() ||
                       head_inst->isCompleted() ||
                       !head_inst->readPredicate());

                // Updates misc. registers.
                head_inst->updateMiscRegs();

                // Check instruction execution if it successfully commits and
                // is not carrying a fault.
                if (cpu->checker) {
                    cpu->checker->verify(head_inst);
                }

                cpu->traceFunctions(pc.instAddr());

                TheISA::advancePC(pc, head_inst->staticInst);

                // Keep track of the last sequence number commited
                lastCommitedSeqNum = head_inst->seqNum;

                // If this is an instruction that doesn't play nicely with
                // others squash everything and restart fetch
                if (head_inst->isSquashAfter())
                    squashAfter(head_inst);

                if (drainPending) {
                    if (pc.microPC() == 0 && interrupt == NoFault &&
                        !thread->trapPending) {
                        // Last architectually committed instruction.
                        // Squash the pipeline, stall fetch, and use
                        // drainImminent to disable interrupts
                        DPRINTF(Drain, "Draining: %s\n", pc);
                        squashAfter(head_inst);
                        cpu->commitDrained(DummyTid);
                        drainImminent = true;
                    }
                }

                bool onInstBoundary = !head_inst->isMicroop() ||
                                      head_inst->isLastMicroop() ||
                                      !head_inst->isDelayedCommit();

                if (onInstBoundary) {
                    int count = 0;
                    Addr oldpc;
                    // Make sure we're not currently updating state while
                    // handling PC events.
                    assert(!thread->noSquashFromTC &&
                           !thread->trapPending);
                    do {
                        oldpc = pc.instAddr();
                        cpu->system->pcEventQueue.service(thread->getTC());
                        count++;
                    } while (oldpc != pc.instAddr());
                    if (count > 1) {
                        DPRINTF(Commit,
                                "PC skip function event, stopping commit\n");
                        skipThisCycle = true;
                        continue;
                    }
                }

                // Check if an instruction just enabled interrupts and we've
                // previously had an interrupt pending that was not handled
                // because interrupts were subsequently disabled before the
                // pipeline reached a place to handle the interrupt. In that
                // case squash now to make sure the interrupt is handled.
                //
                // If we don't do this, we might end up in a live lock situation
                if (!interrupt && avoidQuiesceLiveLock &&
                    onInstBoundary && cpu->checkInterrupts(cpu->tcBase(0)))
                    squashAfter(head_inst);
            } else {
                DPRINTF(Commit, "Unable to commit head instruction PC:%s "
                        "[sn:%i].\n",
                        head_inst->pcState(), head_inst->seqNum);

                skipThisCycle = true;
                continue;
            }
        }
    }

    DPRINTF(CommitRate, "%i\n", num_committed);
    numCommittedDist.sample(num_committed);

    if (num_committed == width) {
        commitEligibleSamples++;
    }
}

template<class Impl>
void FFDIEWC<Impl>::handleInterrupt() {
    panic("FF currently does not consider interrupt");
}

template<class Impl>
typename FFDIEWC<Impl>::DynInstPtr &FFDIEWC<Impl>::getHeadInst() {
    return insts_to_commit.front();
}

template<class Impl>
void FFDIEWC<Impl>::squashAfter(typename FFDIEWC<Impl>::DynInstPtr &head_inst) {
    DPRINTF(Commit, "Executing squash after for inst [sn:%lli]\n",
            head_inst->seqNum);

    assert(!squashAfterInst || squashAfterInst == head_inst);
    commitStatus = SquashAfterPending;
    squashAfterInst = head_inst;
}

template<class Impl>
void FFDIEWC<Impl>::handleSquash() {
    if (trapSquash) {
        assert(!tcSquash);
        squashFromTrap();

    } else if (tcSquash) {
        assert(commitStatus != TrapPending);
        squashFromTC();

    } else if (commitStatus == SquashAfterPending) {
        squashFromSquashAfter();
    }

    if (fromLastCycle->diewc2diewc.squash &&
        commitStatus != TrapPending &&
        fromLastCycle->diewc2diewc.squashedSeqNum <= youngestSeqNum) {
        if (fromLastCycle->diewc2diewc.mispredictInst) {
            DPRINTF(Commit,
                    "Squashing due to branch mispred PC:%#x [sn:%i]\n",
                    fromLastCycle->diewc2diewc.mispredictInst->instAddr(),
                    fromLastCycle->diewc2diewc.squashedSeqNum);
        } else {
            DPRINTF(Commit,
                    "Squashing due to order violation [sn:%i]\n",
                    fromLastCycle->diewc2diewc.squashedSeqNum);
        }
        commitStatus = DQSquashing;

        InstSeqNum squashed_inst = fromLastCycle->diewc2diewc.squashedSeqNum;

        if (fromLastCycle->diewc2diewc.includeSquashInst) {
            squashed_inst--;
        }

        youngestSeqNum = squashed_inst;

        dq.squash(squashed_inst);
        changedDQNumEntries = true;

        toNextCycle->diewc2diewc.doneSeqNum = squashed_inst;
        toNextCycle->diewc2diewc.squash = true;
        toNextCycle->diewc2diewc.dqSquashing = true;
        toNextCycle->diewc2diewc.mispredictInst =
                fromLastCycle->diewc2diewc.mispredictInst;
        toNextCycle->diewc2diewc.branchTaken =
                fromLastCycle->diewc2diewc.branchTaken;
        toNextCycle->diewc2diewc.squashInst = dq.findInst(squashed_inst);

        if (toNextCycle->diewc2diewc.mispredictInst) {
            if (toNextCycle->diewc2diewc.mispredictInst->isUncondCtrl()) {
                toNextCycle->diewc2diewc.branchTaken = true;
            }
            ++branchMispredicts;
        }

        toNextCycle->diewc2diewc.pc =
                fromLastCycle->diewc2diewc.pc;
    }
}

template<class Impl>
void FFDIEWC<Impl>::squashFromTrap() {
    squashAll();

    DPRINTF(Commit, "Squashing from trap, restarting at PC %s\n", pc);
    thread->trapPending = false;
    thread->noSquashFromTC = false;
    trapInFlight = false;

    trapSquash = false;

    commitStatus = DQSquashing;
    cpu->activityThisCycle();
}

template<class Impl>
void FFDIEWC<Impl>::squashFromTC() {
    squashAll();

    DPRINTF(Commit, "Squashing from TC, restarting at PC %s\n", pc);

    thread->noSquashFromTC = false;
    assert(!thread->trapPending);

    commitStatus = DQSquashing;
    cpu->activityThisCycle();

    tcSquash = false;
}

template<class Impl>
void FFDIEWC<Impl>::squashFromSquashAfter() {
    DPRINTF(Commit, "Squashing after squash after request, "
                    "restarting at PC %s\n", pc);

    squashAll();
    // Make sure to inform the fetch stage of which instruction caused
    // the squash. It'll try to re-fetch an instruction executing in
    // microcode unless this is set.
    toNextCycle->diewc2diewc.squashInst = squashAfterInst;
    squashAfterInst = nullptr;

    commitStatus = DQSquashing;
    cpu->activityThisCycle();
}

template<class Impl>
void FFDIEWC<Impl>::squashAll() {
    InstSeqNum squashed_inst = dq.isEmpty() ?
            lastCommitedSeqNum : dq.getHead()->seqNum - 1;

    youngestSeqNum = lastCommitedSeqNum;
    dq.squash(squashed_inst);

    changedDQNumEntries = true;

    toNextCycle->diewc2diewc.doneSeqNum = squashed_inst;
    toNextCycle->diewc2diewc.squash = true;
    toNextCycle->diewc2diewc.dqSquashing = true;
    toNextCycle->diewc2diewc.mispredictInst = nullptr;
    toNextCycle->diewc2diewc.squashInst = nullptr;
    toNextCycle->diewc2diewc.pc = pc;
}

template<class Impl>
FFDIEWC<Impl>::FFDIEWC(XFFCPU *cpu, DerivFFCPUParams *params)
        :
        cpu(cpu),
        freeEntries{params->numDQBanks * params->DQDepth,
                    params->LQEntries, params->SQEntries},
        serializeOnNextInst(false),
        dq(params),
        archState(params),
        fuWrapper(),
        ldstQueue(cpu, this, params), // todo: WTF fix it !!!!!!!
        fetchRedirect(false),
        dqSqushing(false),
        dispatchWidth(params->dispatchWidth),
        width(params->allocationWidth),
        trapInFlight(false),
        trapSquash(false),
        trapLatency(params->trapLatency),
        drainPending(false),
        drainImminent(false),
        skipThisCycle(false),
        avoidQuiesceLiveLock(),  //todo: fix
        squashAfterInst(nullptr),
        tcSquash(false),
        allocationToDIEWCDelay(params->allocationToDIEWCDelay)
{
    skidBufferMax = (allocationToDIEWCDelay + 1)*width;
}

template<class Impl>
void
FFDIEWC<Impl>::squashInFlight()
{
    DPRINTF(IEW, "Squashing all instructions.\n");

    // Tell the IQ to start squashing.
    // todo: we don't need this in forwardflow?

    // Tell the LDSTQ to start squashing.
    ldstQueue.squash(fromLastCycle->diewc2diewc.doneSeqNum, DummyTid);
    updatedQueues = true;

    // Clear the skid buffer in case it has any data in it.
    DPRINTF(IEW, "Removing skidbuffer instructions until [sn:%i].\n",
            fromLastCycle->diewc2diewc.doneSeqNum);

    while (!skidBuffer.empty()) {
        if (skidBuffer.front()->isLoad()) {
            toAllocation->diewcInfo.dispatchedToLQ++;
        }
        if (skidBuffer.front()->isStore()) {
            toAllocation->diewcInfo.dispatchedToSQ++;
        }

        toAllocation->diewcInfo.dispatched++;

        skidBuffer.pop();
    }

    clearAllocatedInsts();
}

template<class Impl>
void FFDIEWC<Impl>::clearAllocatedInsts() {
    DPRINTF(IEW, "Removing incoming allocated instructions\n");

    InstQueue &insts = insts_from_allocation;

    while (!insts.empty()) {

        if (insts.front()->isLoad()) {
            toAllocation->diewcInfo.dispatchedToLQ++;
        }
        if (insts.front()->isStore()) {
            toAllocation->diewcInfo.dispatchedToSQ++;
        }

        toAllocation->diewcInfo.dispatched++;

        insts.pop();
    }
}

template<class Impl>
void FFDIEWC<Impl>::updateComInstStats(DynInstPtr &ffdiewc) {

}

template<class Impl>
void FFDIEWC<Impl>::insertPointerPairs(std::list<PointerPair> pairs) {
    for (const auto &pair: pairs) {
        pointerPackets.push(pair);
    }
}

template<class Impl>
void FFDIEWC<Impl>::rescheduleMemInst(DynInstPtr &inst)
{
    dq.rescheduleMemInst(inst);
}

template<class Impl>
void FFDIEWC<Impl>::replayMemInst(DynInstPtr &inst)
{
    dq.replayMemInst(inst);
}

template<class Impl>
void FFDIEWC<Impl>::cacheUnblocked()
{
    dq.cacheUnblocked();
}

template<class Impl>
void FFDIEWC<Impl>::blockMemInst(DynInstPtr &inst)
{
    dq.blockMemInst(inst);
}

template<class Impl>
void FFDIEWC<Impl>::wakeCPU()
{
    cpu->wakeCPU();
}

template<class Impl>
void FFDIEWC<Impl>::instToWriteback(DynInstPtr &inst)
{
    assert(inst->isLoad());
    inst->sfuWrapper->markWb();
}

template<class Impl>
std::string FFDIEWC<Impl>::name() const
{
    return cpu->name() + ".diewc";
}

template<class Impl>
void FFDIEWC<Impl>::activityThisCycle()
{
    cpu->activityThisCycle();
}

template<class Impl>
unsigned FFDIEWC<Impl>::numInWindow()
{
    return dq.numInDQ();
}

template<class Impl>
void FFDIEWC<Impl>::takeOverFrom()
{
    _status = Active;
    dispatchStatus = Running;
    commitStatus = CommitRunning;

    dq.takeOverFrom();
    ldstQueue.takeOverFrom();

    startupStage();
    cpu->activityThisCycle();

    updateLSQNextCycle = false;
    fetchRedirect = false;

    //todo: advance time buffers?

}

template<class Impl>
void FFDIEWC<Impl>::startupStage()
{
    toAllocation->diewcInfo.usedDQ = true;
    toAllocation->diewcInfo.freeDQEntries = dq.numFree();

    toAllocation->diewcInfo.usedLSQ = true;
    toAllocation->diewcInfo.freeLQEntries = ldstQueue.numFreeLoadEntries(DummyTid);
    toAllocation->diewcInfo.freeSQEntries = ldstQueue.numFreeStoreEntries(DummyTid);

    toAllocation->diewcInfo.dqHead = dq.getHeadPtr();
    toAllocation->diewcInfo.dqTail = dq.getTailPtr();

    if (cpu->checker) {
        cpu->checker->setDcachePort(&cpu->getDataPort());

        cpu->activateStage(XFFCPU::IEWCIdx);
    }
}

template<class Impl>
void FFDIEWC<Impl>::drainSanityCheck() const
{
    assert(isDrained());
    dq.drainSanityCheck();
    ldstQueue.drainSanityCheck();
}

template<class Impl>
void FFDIEWC<Impl>::drain()
{
    drainPending = true;
}

template<class Impl>
void FFDIEWC<Impl>::drainResume()
{
    drainPending = false;
    drainImminent = false;
}

template<class Impl>
bool FFDIEWC<Impl>::isDrained() const
{
    return dq.getHeadPtr() == dq.getTailPtr() && interrupt == NoFault;
}

template<class Impl>
void FFDIEWC<Impl>::resetEntries()
{
    dq.resetEntries();
    ldstQueue.resetEntries();
}

template<class Impl>
void FFDIEWC<Impl>::regProbePoints()
{
    ppDispatch = new ProbePointArg<DynInstPtr>(
            cpu->getProbeManager(), "Dispatch");

    ppMispredict = new ProbePointArg<DynInstPtr>(
            cpu->getProbeManager(), "Mispredict");
    /**
     * Probe point with dynamic instruction as the argument used to probe when
     * an instruction starts to execute.
     */
    ppExecute = new ProbePointArg<DynInstPtr>(
            cpu->getProbeManager(), "Execute");
    /**
     * Probe point with dynamic instruction as the argument used to probe when
     * an instruction execution completes and it is marked ready to commit.
     */
    ppToCommit = new ProbePointArg<DynInstPtr>(
            cpu->getProbeManager(), "ToCommit");

    ppCommit = new ProbePointArg<DynInstPtr>(
            cpu->getProbeManager(), "Commit");
    ppCommitStall = new ProbePointArg<DynInstPtr>(
            cpu->getProbeManager(), "CommitStall");
    ppSquash = new ProbePointArg<DynInstPtr>(
            cpu->getProbeManager(), "Squash");
}

template<class Impl>
typename Impl::DynInstPtr FFDIEWC<Impl>::readHeadInst(ThreadID tid)
{
    return dq.getHead();
}

template<class Impl>
void FFDIEWC<Impl>::checkMisprediction(DynInstPtr &inst)
{
    if (!fetchRedirect ||
        !toNextCycle->diewc2diewc.squash ||
        toNextCycle->diewc2diewc.squashedSeqNum > inst->seqNum) {

        if (inst->mispredicted()) {
            fetchRedirect = true;

            DPRINTF(IEW, "Execute: Branch mispredict detected.\n");
            DPRINTF(IEW, "Predicted target was PC:%#x, NPC:%#x.\n",
                    inst->predInstAddr(), inst->predNextInstAddr());
            DPRINTF(IEW, "Execute: Redirecting fetch to PC: %#x,"
                    " NPC: %#x.\n", inst->nextInstAddr(),
                    inst->nextInstAddr());
            // If incorrect, then signal the ROB that it must be squashed.
            squashDueToBranch(inst);

            if (inst->readPredTaken()) {
                predictedTakenIncorrect++;
            } else {
                predictedNotTakenIncorrect++;
            }
        }
    }
}

template<class Impl>
void FFDIEWC<Impl>::squashDueToBranch(DynInstPtr &inst)
{
    if (!toNextCycle->diewc2diewc.squash ||
            inst->seqNum <= toNextCycle->diewc2diewc.squashedSeqNum) {
        toNextCycle->diewc2diewc.squash = true;
        toNextCycle->diewc2diewc.squashedSeqNum = inst->seqNum;
        toNextCycle->diewc2diewc.pc = inst->pcState();
        toNextCycle->diewc2diewc.mispredictInst = nullptr;
        toNextCycle->diewc2diewc.includeSquashInst = true;
        wroteToTimeBuffer = true;
    }
}

template<class Impl>
void FFDIEWC<Impl>::generateTCEvent(ThreadID tid)
{
    assert(!trapInFlight);
    tcSquash = true;
}

template<class Impl>
void FFDIEWC<Impl>::setActiveThreads(std::list<ThreadID> *at_ptr)
{
    ldstQueue.setActiveThreads(at_ptr);
}

template<class Impl>
void FFDIEWC<Impl>::setThreads(vector<FFDIEWC::Thread *> &threads)
{
    thread = threads[DummyTid];
}

template<class Impl>
void FFDIEWC<Impl>::deactivateThread(ThreadID tid)
{
//    nothing todo
}

template<class Impl>
void FFDIEWC<Impl>::setTimeBuffer(TimeBuffer<TimeStruct> *tb_ptr)
{
    backwardTB = tb_ptr;

    toNextCycle = backwardTB->getWire(0);

    toFetch = backwardTB->getWire(0);

    fromLastCycle = backwardTB->getWire(-1);

    toAllocation = backwardTB->getWire(0);
}

template<class Impl>
void FFDIEWC<Impl>::setFetchQueue(TimeBuffer<FetchStruct> *fq_ptr)
{
//    nothing todo
}

template<class Impl>
void FFDIEWC<Impl>::regStats()
{
    dispSquashedInsts
        .name(name() + "dispSquashedInsts")
        .desc("dispSquashedInsts");
    dqFullEvents
        .name(name() + "dqFullEvents")
        .desc("dqFullEvents");
    lqFullEvents
        .name(name() + "lqFullEvents")
        .desc("lqFullEvents");
    sqFullEvents
        .name(name() + "sqFullEvents")
        .desc("sqFullEvents");
    dispaLoads
        .name(name() + "dispaLoads")
        .desc("dispaLoads");
    dispStores
        .name(name() + "dispStores")
        .desc("dispStores");
    dispNonSpecInsts
        .name(name() + "dispNonSpecInsts")
        .desc("dispNonSpecInsts");
    dispatchedInsts
        .name(name() + "dispatchedInsts")
        .desc("dispatchedInsts");
    blockCycles
        .name(name() + "blockCycles")
        .desc("blockCycles");
    squashCycles
        .name(name() + "squashCycles")
        .desc("squashCycles");
    runningCycles
        .name(name() + "runningCycles")
        .desc("runningCycles");
    unblockCycles
        .name(name() + "unblockCycles")
        .desc("unblockCycles");
    commitNonSpecStalls
        .name(name() + "commitNonSpecStalls")
        .desc("commitNonSpecStalls");
    commitSquashedInsts
        .name(name() + "commitSquashedInsts")
        .desc("commitSquashedInsts");
    branchMispredicts
        .name(name() + "branchMispredicts")
        .desc("branchMispredicts");
    predictedTakenIncorrect
        .name(name() + "predictedTakenIncorrect")
        .desc("predictedTakenIncorrect");
    predictedNotTakenIncorrect
        .name(name() + "predictedNotTakenIncorrect")
        .desc("predictedNotTakenIncorrect");

    commitEligibleSamples
            .name(name() + "ommitEligibleSamples")
            .desc("ommitEligibleSamples");

    statCommittedInstType
            .init(Num_OpClasses)
            .name(name() + "statCommittedInstType")
            .desc("statCommittedInstType");

    numCommittedDist
            .init(0, width, 1)
            .name(name() + "numCommittedDist")
            .desc("numCommittedDist")
            .flags(Stats::pdf);

    dq.regStats();
    ldstQueue.regStats();
}

template<class Impl>
void FFDIEWC<Impl>::setAllocQueue(TimeBuffer<AllocationStruct> *aq_ptr)
{
    allocationQueue = aq_ptr;

    fromAllocation = allocationQueue->getWire(-1);
}

}

#include "cpu/forwardflow/isa_specific.hh"

template class FF::FFDIEWC<FFCPUImpl>;

