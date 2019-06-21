//
// Created by zyy on 19-6-11.
//

#include "cpu/forwardflow/diewc.hh"
#include "diewc.hh"

namespace FF{

using namespace std;

template<class Impl>
void FFDIEWC<Impl>::tick() {

    // todo: commit from DQ
    // part DQC
    // part DQC should not read from DQCR in the same cycle
    commit();


    // todo: DQ tick
    // part DQ tick: should not read values from part bring value part route pointer in the same cycle

    // todo: read from DQ head To Simulate that DQ is a RAM instead of combinational.
    // part DQCR
    writeCommitQueue();


    // todo: Execute ready insts
    execute();



    // todo read allocated instructions from allocation stage
    // part receive
    readInsts();
    checkSignalsAndUpdate();
    // todo insert these insts into LSQ until full
    // if no LSQ entry allocated for an LD/ST inst, it and further insts should not be insert into DQ (block)

    // todo insert these insts into corresponding banks,
    //  what fields should be filled:
    //  - source operand value: stacked mask
    //  RTL should be: Vsrc7.1 = d6 == src7.1 ? Vd6 : (d5 == src7.1 ? Vd5 : ...)
    // part bring value
    tryDispatch();

    // todo: forward pointers should be calculated
    //  - source operand forward pointer (if any sibling)
    //  - dest operand forward pointer
    //  these pointers should be calculated in one cycle and write to the pointer queues
    //  calculate newest definer and newest consumer, then route forward pointers to them
    // part route pointer

    forward();

    // todo: remember to advance
    //  - issue to execution queue
    //  - commit queue
    //  - timebuffers inside DQ
    advanceQueues();

}

template<class Impl>
void FFDIEWC<Impl>::squash(DynInstPtr &inst) {
    // todo calculate invalidation mask, write to DQ valid bits

    // todo find corresponding checkpoint of the consumer table, reverse to it

    // todo notify previous stages to flush instructions
}

template<class Impl>
void FFDIEWC<Impl>::checkSignalsAndUpdate() {
    if (fromCommit->ffCommitInfo.squashInFlight) {
        squash();
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
        clearAllocatedInts();
        wroteToTimeBuffer = true;
    }
    if (checkStall()){
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
    for (int i = 0; i < num; ++i) {
        DynInstPtr inst = fromAllocation->insts[i];
        insts_from_allocation.push(inst);
    }
}

template<class Impl>
void FFDIEWC<Impl>::dispatch() {
    InstQueue &to_dispatch = dispatchStatus == Unblocking ?
            skidBuffer : insts_from_allocation;
    int num_insts_to_disp = static_cast<int>(to_dispatch.size());
    DynInstPtr inst = nullptr;
    bool normally_add_to_dq = false;
    int dispatched = 0;
    for (; dispatched < num_insts_to_disp &&
           dispatched < dispatchWidth;
           ++dispatched) {
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

        if (dq.isFull()) {
            block();
            toAllocation->diewcUnblock = false;
            ++dqFullEvents;
            break;
        }

        if ((inst->isLoad() && ldstQueue.lqFull()) ||
            (inst->isStore() && ldstQueue.sqFull())) {
            if (inst->isLoad()) {
                ++lqFullEvents;
            } else {
                ++sqFullEvents;
            }
            block();
            toAllocation->diewcUnblock = false;
            break;
        }

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
            pointerPackets.push(archState.recordAndUpdateMap(inst));

            dq.insertBarrier(inst);
            normally_add_to_dq = false;

        } else if (inst->isNop()) {
            inst->setIssued();
            inst->setExecuted();
            inst->setCanCommit();
            dq.recordProducer(inst);
            normally_add_to_dq = true;
        } else {
            assert(!inst->isExecuted());
            normally_add_to_dq = true;
        }

        if (normally_add_to_dq && inst->isNonSpeculative()) {
            inst->setCanCommit();

            pointerPackets.push(archState.recordAndUpdateMap(inst));
            dq.insertNonSpec(inst);

            ++dispNonSpecInsts;
            normally_add_to_dq = false;
        }

        if (normally_add_to_dq) {
            pointerPackets.push(archState.recordAndUpdateMap(inst));
            dq.insert(inst);
        }

        to_dispatch.pop();
        toAllocation->diewcInfo.dispatched++;
        ++dispatchedInsts;
#if TRACING_ON
        inst->dispatchTick = curTick() - inst->fetchTick;
#endif
        ppDisptch->notify(inst);
    }

    if (!to_dispatch.empty()) {
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
    for (const auto &pkt: pointerPackets) {
        dq.insertForwardPointer(pkt);
        // DQ is responsible for the rest stuffs
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
        toNextCycle->commitQueue[i] = inst;
    }
}

template<class Impl>
void FFDIEWC<Impl>::commit() {
    // read insts
    for (unsigned i = 0; i < width; i++) {
        insts_to_commit.push(fromLastCycle->commitQueue[i]);
    }

    handleSquash();

    if (commitStatus != DQSquashing) {
        commitInsts();
    }
}

template<class Impl>
void FFDIEWC<Impl>::clearAllocatedInts() {
    InstQueue().swap(insts_from_allocation);

    if (dispatchStatus == Unblocking) {
        InstQueue().swap(skidBuffer);
    }
}

template<class Impl>
bool FFDIEWC<Impl>::checkStall() {
    if (dqSqushing) {
        return true;
    }
    if (dq.stallToUnclog()) {
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

    ThreadID tid = head_inst->threadNumber;

    // If the instruction is not executed yet, then it will need extra
    // handling.  Signal backwards that it should be executed.
    if (!head_inst->isExecuted()) {
        // Keep this number correct.  We have not yet actually executed
        // and committed this instruction.
        thread[tid]->funcExeInst--;

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

        toNextCycle->commitInfo.nonSpecSeqNum = head_inst->seqNum;

        // Change the instruction so it won't try to commit again until
        // it is executed.
        head_inst->clearCanCommit();

        if (head_inst->isLoad() && head_inst->strictlyOrdered()) {
            DPRINTF(Commit, "[sn:%lli]: Strictly ordered load, PC %s.\n",
                    head_inst->seqNum, head_inst->pcState());
            toNextCycle->commitInfo.strictlyOrdered = true;
            toNextCycle->commitInfo.strictlyOrderedLoad = head_inst;
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
        cpu->trap(inst_fault, tid,
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
                head_inst->traceData->setCPSeq(thread[tid]->numOp);
                head_inst->traceData->dump();
            }
            delete head_inst->traceData;
            head_inst->traceData = NULL;
        }

        // Generate trap squash event.
        generateTrapEvent(tid, inst_fault);
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
        head_inst->traceData->setCPSeq(thread[tid]->numOp);
        head_inst->traceData->dump();
        delete head_inst->traceData;
        head_inst->traceData = NULL;
    }
    if (head_inst->isReturn()) {
        DPRINTF(Commit,"Return Instruction Committed [sn:%lli] PC %s \n",
                        head_inst->seqNum, head_inst->pcState());
    }

    // Update the commit rename map
    archState.commit(head_inst);
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

        DPRINTF(Commit, "Trying to commit head instruction, [sn:%i]\n",
                head_inst->seqNum);

        // If the head instruction is squashed, it is ready to retire
        // (be removed from the ROB) at any time.
        if (head_inst->isSquashed()) {

            DPRINTF(Commit, "Retiring squashed instruction from "
                    "ROB.\n");

            dq->retireHead();

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
                toNextCycle->commitInfo.doneSeqNum = head_inst->seqNum;

                canHandleInterrupts =  (!head_inst->isDelayedCommit()) &&
                                       ((THE_ISA != ALPHA_ISA) ||
                                         (!(pc[0].instAddr() & 0x3)));

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
                        DPRINTF(Drain, "Draining: %i:%s\n", tid, pc[tid]);
                        squashAfter(head_inst);
                        cpu->commitDrained();
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
                        "[tid:%i] [sn:%i].\n",
                        head_inst->pcState(), tid ,head_inst->seqNum);

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
    DPRINTF(Commit, "Executing squash after for [tid:%i] inst [sn:%lli]\n",
            tid, head_inst->seqNum);

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

    if (fromLastCycle->squash &&
        commitStatus != TrapPending &&
        fromLastCycle->squashSeqNum <= youngestSeqNum) {
        if (fromLastCycle->mispredictInst) {
            DPRINTF(Commit,
                    "[tid:%i]: Squashing due to branch mispred PC:%#x [sn:%i]\n",
                    tid,
                    fromIEW->mispredictInst[tid]->instAddr(),
                    fromIEW->squashedSeqNum[tid]);
        } else {
            DPRINTF(Commit,
                    "[tid:%i]: Squashing due to order violation [sn:%i]\n",
                    tid, fromIEW->squashedSeqNum[tid]);
        }
        commitStatus = DQSquashing;

        InstSeqNum squashed_inst = fromLastCycle->squashedSeqNum;

        if (fromLastCycle->includeSquashInst) {
            squashed_inst--;
        }

        youngestSeqNum = squashed_inst;

        dq.squash(squashed_inst);
        changedDQNumEntries = true;

        toNextCycle->commitInfo.doneSeqNum = squashed_inst;
        toNextCycle->commitInfo.squash = true;
        toNextCycle->commitInfo.dqSquashing = true;
        toNextCycle->commitInfo.mispredictInst = fromLastCycle->mispredictInst;
        toNextCycle->commitInfo.branchTaken = fromLastCycle->branchTaken;
        toNextCycle->commitInfo.squashInst = dq.findInst(squashed_inst);

        if (toNextCycle->commitInfo.mispredict) {
            if (toNextCycle->commitInfo.mispredictInst->isUncondCtrl()) {
                toNextCycle->commitInfo.branchTaken = true;
            }
            ++branchMispredicts;
        }

        toNextCycle->commitInfo.pc = fromLastCycle->pc;
    }
}

};
