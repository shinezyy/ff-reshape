//
// Created by zyy on 19-6-11.
//
#include "cpu/forwardflow/diewc.hh"

#include "arch/utility.hh"
#include "cpu/forwardflow/arch_state.hh"
#include "cpu/forwardflow/store_set.hh"
#include "debug/Commit.hh"
#include "debug/CommitObserve.hh"
#include "debug/CommitRate.hh"
#include "debug/DIEWC.hh"
#include "debug/DQGOF.hh"
#include "debug/DQV2.hh"
#include "debug/Drain.hh"
#include "debug/ExecFaulting.hh"
#include "debug/FFCommit.hh"
#include "debug/FFDisp.hh"
#include "debug/FFExec.hh"
#include "debug/FFSquash.hh"
#include "debug/FanoutLog.hh"
#include "debug/IEW.hh"
#include "debug/NoSQSMB.hh"
#include "debug/O3PipeView.hh"
#include "debug/ObExec.hh"
#include "debug/RSProbe1.hh"
#include "debug/Reshape.hh"
#include "debug/Reshape2.hh"
#include "debug/ValueCommit.hh"
#include "debug/ValueExec.hh"
#include "params/DerivFFCPU.hh"

namespace FF{

using namespace std;

//#ifdef __JETBRAINS_IDE__
//using DynInstPtr = BaseO3DynInst<Impl>*;
//#endif

template<class Impl>
void FFDIEWC<Impl>::tick() {

    clearAtStart();

    // todo: Execute ready insts
    // execute();

    dq.cycleStart();


    // todo read allocated instructions from allocation stage
    // part receive
    readInsts();

    checkSignalsAndUpdate();
    // todo insert these insts into LSQ until full
    // if no LSQ entry allocated for an LD/ST inst, it and further insts should not be insert into DQ (block)


    // todo: remember to advance
    //  - issue to execution queue
    //  - commit queue
    //  - timebuffers inside DQ
    advanceQueues();

    // todo: DQ tick
    // part DQ tick: should not read values from part bring value part route pointer in the same cycle
    dq.tick();
//
    // TODO: it writes center buffer
    ldstQueue.writebackStores();

    // todo: commit from DQ
    // part DQC
    // part DQC should not read from DQCR in the same cycle
    commit();

    // todo: read from DQ head To Simulate that DQ is a RAM instead of combinational.
    // part DQCR
    writeCommitQueue();

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

    checkDQHalfSquash();


    // commit from LSQ
    if (anySuccessfulCommit) {
        DPRINTF(FFCommit, "Mark load/store insts oldder than [%lu] as can wb\n",
                lastCommitedSeqNum);
        // TODO: it writes center buffer
        ldstQueue.commitStores(lastCommitedSeqNum, DummyTid);
        ldstQueue.commitLoads(lastCommitedSeqNum, DummyTid);

        updateLSQNextCycle = true;
    }

    // handle non spec instructions
#define tbuf fromLastCycle->diewc2diewc

    if (tbuf.nonSpecSeqNum != 0) {
        if (tbuf.nonSpecSeqNum <= scheduledNonSpec) {
            DPRINTF(DIEWC, "Cannot schedule nonSpec %lu because %lu has been scheduled!\n",
                    tbuf.nonSpecSeqNum > scheduledNonSpec);

        } else if (tbuf.strictlyOrdered) {
            DPRINTF(DIEWC, "Recv strictlyOrdered non spec\n");
            // TODO: it writes center buffer
            dq.replayMemInst(tbuf.strictlyOrderedLoad);
            tbuf.strictlyOrderedLoad->setAtCommit();

        } else {
            DPRINTF(DIEWC, "Recv other non spec than strictly orderded\n");
            if (!dq.getTail()) {
                DPRINTF(FFSquash, "Ignore scheduling attempt to squashed inst\n");
            } else {
                assert(tbuf.nonSpecSeqNum == dq.getTail()->seqNum);
                // TODO: it writes center buffer
                dq.scheduleNonSpec();
                scheduledNonSpec = tbuf.nonSpecSeqNum;
            }
        }
    }
#undef tbuf

    tryVerifyTailLoad();

    sendBackwardInfo();

    clearAtEnd();
}

template<class Impl>
void FFDIEWC<Impl>::tryVerifyTailLoad() {
    DynInstPtr tail = getTailInst();
    if (tail && tail->isLoad() && tail->seqNum != verifiedTailLoad) {
        InstSeqNum nvul = tail->seqNVul;
        DPRINTF(NoSQSMB, "Last verified load is %lu\n", verifiedTailLoad);
        DPRINTF(NoSQSMB, "NVul of load [%lu] is %lu\n", tail->seqNum, nvul);
        bool skip_verify;
        InstSeqNum low_ssn = 0, high_ssn = 0, ssn = 0;

        if (tail->physEffAddrHigh) { // split
            DPRINTF(NoSQSMB, "Load [%lu] is split\n", tail->seqNum);
            low_ssn = mDepPred->lookupAddr(tail->physEffAddrLow);
            high_ssn = mDepPred->lookupAddr(tail->physEffAddrHigh);
        } else {
            ssn = mDepPred->lookupAddr(tail->physEffAddrLow);
            DPRINTF(NoSQSMB, "Load [%lu] consists of only one access,"
                    "with last store SN: %lu\n", tail->seqNum, ssn);
        }

        if (tail->seqNum == bypassCanceled) {
            skip_verify = false;

        } else if (tail->isRVAmoLoadHalf() || tail->isLoadReserved()) {
            // AMO load and LR are not verifiable
            skip_verify = true;

        } else if (tail->memPredHistory->bypass) {
            DPRINTF(NoSQSMB, "Load [%lu] is predicted to bypass\n", tail->seqNum);
            if (tail->orderFulfilled()) {
                if (tail->physEffAddrHigh) {
                    skip_verify = (low_ssn == nvul) && (high_ssn == nvul);
                } else {
                    skip_verify = ssn == nvul;
                }
            } else {
                skip_verify = false;
                DPRINTF(NoSQSMB, "But has not received bypass value\n");
            }

        } else {
            DPRINTF(NoSQSMB, "Load [%lu] is predicted to not bypass\n", tail->seqNum);
            // ssn == 0 means its entry might be evicted
            if (tail->physEffAddrHigh) {
                skip_verify = ssn > 0 && low_ssn <= nvul && high_ssn <= nvul;
                // skip_verify = low_ssn <= nvul && high_ssn <= nvul;
            } else {
                skip_verify = ssn > 0 && ssn <= nvul;
                // skip_verify = ssn <= nvul;
            }
        }

        if (skip_verify) {
            DPRINTF(NoSQSMB, "Skip verifying load [%lu]\n", tail->seqNum);
            tail->loadVerified = true;
            verifiedTailLoad = tail->seqNum;
            tail->setCanCommit();
            if (tail->isLoad() &&
                tail->isNormalBypass() &&
                !(tail->isRVAmoLoadHalf() || tail->isLoadReserved()) ) {

                tail->setExecutedPure();
                tail->receivedDest = true;
                tail->setIntRegOperand(tail->staticInst.get(), 0, tail->bypassVal.i);
            }

        } else if (!ldstQueue.numStoresToWB(DummyTid)){
            auto [sent_reexec, canceled_bypassing] = dq.reExecTailLoad(bypassCanceled);
            if (sent_reexec) {
                verifiedTailLoad = tail->seqNum;
            }
            if (canceled_bypassing && tail->memPredHistory) {
                DPRINTF(NoSQPred, "Count down bypassing confidence for inst[%lu], "
                                  "because it does not received the bypassing pointer"
                                  " even after it becomes DQ tail\n", tail->seqNum);
                mDepPred->update(tail->instAddr(), false,
                                 0, // dont care
                                 0, // dont care
                                 tail->memPredHistory // will be deleted
                );

            }
        } else {
            DPRINTF(NoSQSMB, "Cannot verify load [%lu] yet, because of pending wb store\n",
                    tail->seqNum);
        }
    }
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
            DPRINTF(DIEWC || Debug::FFDisp, "Switch to Squashing\n");
            toAllocation->diewcUnblock = true;
            wroteToTimeBuffer = true;
        }
        dispatchStatus = Squashing;
        fetchRedirect = false;
        DPRINTF(DIEWC, "Turn to squashing\n");
        return;
    } else {
        if (commitStatus == DQSquashing) {
            commitStatus = CommitRunning;
        }
    }

    if (dqSquashing) {
        DPRINTF(DIEWC || Debug::FFDisp, "Switch to Squashing\n");
        dispatchStatus = Squashing;
        clearAllocatedInsts();
        wroteToTimeBuffer = true;
    }
    if (checkStall()){
        DPRINTF(DIEWC || Debug::FFDisp, "DIEWC blocked after checkStall\n");
        block();
        return;
    }
    if (dispatchStatus == Blocked) {
        DPRINTF(DIEWC || Debug::FFDisp, "Switch to unblocking after blocked\n");
        dispatchStatus = Unblocking;
        unblock();
        return;
    }
    if (dispatchStatus == Squashing) {
        DPRINTF(DIEWC || Debug::FFDisp, "Switch to Running\n");
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
        insts_from_allocation.push_back(inst);
    }
}

template<class Impl>
void FFDIEWC<Impl>::dispatch() {


    InstQueue &to_dispatch = dispatchStatus == Unblocking ?
            skidBuffer : insts_from_allocation;
    unsigned num_insts_to_disp = to_dispatch.size();
    DynInstPtr inst = nullptr;
    bool normally_add_to_dq = false;
    unsigned dispatched = 0;


    for (; dispatched < num_insts_to_disp &&
           dispatched < dispatchWidth && !to_dispatch.empty();) {
        inst = to_dispatch.front();
        assert(inst);

        if (inst->isSquashed()) {
            ++dispSquashedInsts;
            to_dispatch.pop_front();

            if (inst->isLoad()) {
                toAllocation->diewcInfo.dispatchedToLQ++;
                DPRINTF(DIEWC, "backward disp to LQ inc by inst[%d], "
                               "to LQ = %d",
                        inst->seqNum, toAllocation->diewcInfo.dispatchedToLQ);
            }
            if (inst->isStore()) {
                toAllocation->diewcInfo.dispatchedToSQ++;
            }
            if (!inst->isForwarder()) {
                toAllocation->diewcInfo.dispatched++;
            }

            dispatched++;
            continue;
        }

        if (dq.isFull() || dq.hasTooManyPendingInsts() || dq.isSwitching()) {
            DPRINTF(DIEWC, "block because DQ is full or is switching\n");
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
            DPRINTF(DIEWC || Debug::FFDisp, "DIEWC blocked because LSQ is full\n");
            block();
            toAllocation->diewcUnblock = false;
            break;
        }

        // postponed allocation;
        dq.advanceHead();
        inst->dqPosition = dq.c.uint2Pointer(dq.getHeadPtr());

        DPRINTF(DQGOF, "Current head: %u," ptrfmt "\n",
                dq.getHeadPtr(), extptr(inst->dqPosition));

        inst->dqPosition.term = dq.getHeadTerm();

        bool jumped = false;
        PointerPair pair;
        pair.dest.valid = false;
        pair.isBypass = false;

        DPRINTF(DIEWC||Debug::RSProbe1, "Dispatching inst[%llu] %s PC: %s\n",
                inst->seqNum, inst->staticInst->disassemble(inst->instAddr()),
                inst->pcState());
        if (inst->isLoad()) {
            ldstQueue.insertLoad(inst);
            ++dispaLoads;

            DPRINTF(DIEWC, "backward disp to LQ inc by inst[%d], "
                           "to LQ = %d\n",
                    inst->seqNum, toAllocation->diewcInfo.dispatchedToLQ);
            toAllocation->diewcInfo.dispatchedToLQ++;

            if (inst->isLoadReserved()) {
                DPRINTF(DIEWC, "Load reserved: %s\n",
                        inst->staticInst->disassemble(inst->instAddr()));
                inst->setCanCommit();

                std::tie(jumped, pair) = dq.insertNonSpec(inst);
                setupPointerLink(inst, jumped, pair);

                normally_add_to_dq = false;

            } else {
                normally_add_to_dq = true;
                setUpLoad(inst);
            }

        } else if (inst->isStore()) {
            ldstQueue.insertStore(inst);
            ++dispStores;

            if (inst->isStoreConditional()) {
                DPRINTF(DIEWC, "Store cond: %s\n",
                        inst->staticInst->disassemble(inst->instAddr()));

                inst->setCanCommit();

                std::tie(jumped, pair) = dq.insertNonSpec(inst);
                setupPointerLink(inst, jumped, pair);

                normally_add_to_dq = false;

            } else {
                normally_add_to_dq = true;
            }
            toAllocation->diewcInfo.dispatchedToSQ++;

        } else if (inst->isMemBarrier() || inst->isWriteBarrier()) {
            DPRINTF(DIEWC, "Mem barrier: %s\n",
                    inst->staticInst->disassemble(inst->instAddr()));
            inst->setCanCommit();
            std::tie(jumped, pair) = dq.insertBarrier(inst);
            setupPointerLink(inst, jumped, pair);

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

        if (normally_add_to_dq && inst->isNonSpeculative()) {
            inst->setCanCommit();

            std::tie(jumped, pair) = dq.insertNonSpec(inst);
            setupPointerLink(inst, jumped, pair);


            ++dispNonSpecInsts;
            normally_add_to_dq = false;
        }

        if (normally_add_to_dq) {
            std::tie(jumped, pair) = dq.insert(inst, false);
            setupPointerLink(inst, jumped, pair);

            youngestSeqNum = inst->seqNum;
        }

        if (jumped) {
            if (!dq.validPosition(oldestForwarded)) {
                oldestForwarded = dq.getTailPtr();
            }
            dq.maintainOldestUsed();
        }

        if (!to_dispatch.front()->isForwarder()) {
            toAllocation->diewcInfo.dispatched++;
        }
        to_dispatch.pop_front();

        if (EnableReshape && !inst->forwarded &&
                !(inst->isStoreConditional() || inst->isSerializeAfter())) {
            bool is_lf_source, is_lf_drain;
            list<DynInstPtr> to_forward;
            std::tie(is_lf_source, is_lf_drain) = archState.forwardAfter(inst, to_forward);
            if (is_lf_source || is_lf_drain) {
                DynInstPtr last = inst;
                std::stack<DynInstPtr> stack;
                for (auto &it: to_forward) {
                    stack.push(insertForwarder(it, last));
                    last = stack.top(); // last forwarder
                }
                while (!stack.empty()) {
                    to_dispatch.push_front(stack.top());
                    stack.pop();
                }

                inst->forwarded = true;

                if (is_lf_source) {
                    firstLevelFw++;
                }
                if (is_lf_drain) {
                    secondaryLevelFw++;
                }
            } else {
                DPRINTF(Reshape, "lf_source: %i, lf_drain: %i\n", is_lf_drain, is_lf_drain);
            }
        } else {
            DPRINTF(Reshape, "Will not insert after inst[%lu]\n", inst->seqNum);
        }

        ++dispatchedInsts;
#if TRACING_ON
        inst->dispatchTick = curTick() - inst->fetchTick;
#endif
        ppDispatch->notify(inst);

        if (!inst->isForwarder()) {
            dispatched++;
        }
    }

    if (dispatched) {
        toAllocation->diewcInfo.usedDQ = true;
        activityThisCycle();
    }
    // archState.dumpMaps();

    if (!to_dispatch.empty()) {
        DPRINTF(DIEWC || Debug::FFDisp, "DIEWC blocked because instructions are not used up\n");
        block();
        toAllocation->diewcUnblock = false;
    }

    if (dispatchStatus == Idle && dispatched) {
        dispatchStatus = Running;
        updatedQueues = true;
    }
}

template<class Impl>
void FFDIEWC<Impl>::setupPointerLink(FFDIEWC::DynInstPtr &inst, bool jumped, const PointerPair &pair)
{
    auto pairs = archState.recordAndUpdateMap(inst);

    if (pair.dest.valid) {
        DPRINTF(NoSQSMB, "Found barrier dep:" ptrfmt " to " ptrfmt "\n",
                extptr(pair.dest), extptr(pair.payload));

        if (pairs.size()) {
            const auto &last_pair = pairs.back();

            if (last_pair.isBypass) {
                DPRINTF(NoSQSMB, "SMB pair:" ptrfmt " to " ptrfmt "is overridden\n",
                        extptr(last_pair.dest), extptr(last_pair.payload));
                pairs.pop_back();
            }
        }
        inst->seqNVul = getLastCompletedStoreSN(); // change NVul

        insertPointerPairs(pairs);

        insertPointerPair(pair);
    } else {
        DPRINTF(NoSQSMB, "Barrier pair is invalid\n");
        insertPointerPairs(pairs);
    }
}

template<class Impl>
void FFDIEWC<Impl>::forward() {
    DPRINTF(FFDisp, "Inserting pointers to center buffer\n");
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
    auto heads = dq.getBankTails();
    for (unsigned i = 0; i < width; i++) {
        auto &inst = heads.front();
        if (!inst) {
            break;
        }
        toNextCycle->diewc2diewc.commitQueue[i] = inst;
        DPRINTF(Commit, "Write inst[%d] to next cycle\n", inst->seqNum);
        heads.pop_front();
    }
}

template<class Impl>
void FFDIEWC<Impl>::commit() {
    // read insts
    for (unsigned i = 0; i < width; i++) {
        DynInstPtr inst = fromLastCycle->diewc2diewc.commitQueue[i];
        if (inst) {
            // push only non null pointers
            insts_to_commit.push(inst);
            DPRINTF(Commit, "read inst[%d] from last cycle\n", inst->seqNum);
        }
    }

    handleSquash();

    if (!(commitStatus == DQSquashing || commitStatus == TrapPending)) {
        commitInsts();
    } else {
        DPRINTF(FFSquash, "DQ is squashing\n");
    }

    // todo: ppStall here?
}

template<class Impl>
bool FFDIEWC<Impl>::checkStall() {
    // todo: fix mysterious stall
    if (dqSquashing) {
        DPRINTF(DIEWC, "block because dq squashing\n");
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

    DPRINTF(DIEWC || Debug::FFDisp, "skidInsert in func block\n");
    skidInsert();
    DPRINTF(DIEWC || Debug::FFDisp, "Switch to Blocked in block()\n");
    dispatchStatus = Blocked;
}

template<class Impl>
void FFDIEWC<Impl>::unblock() {
    if (skidBuffer.empty()) {
        DPRINTF(DIEWC, "Switch to Running in unblock()\n");
        toAllocation->diewcUnblock = true;
        wroteToTimeBuffer = true;
        dispatchStatus = Running;
    } else {
        DPRINTF(DIEWC, "There are %i insts remained in skidBuffer, "
                "Status remains to be unblocking\n", skidBuffer.size());
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
            unblock();
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
        insts_from_allocation.pop_front();
        skidBuffer.push_back(inst);
    }
    assert(skidBuffer.size() <= skidBufferMax);
}

template<class Impl>
void FFDIEWC<Impl>::execute() {
    // fuWrapper.tick();
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
        // In FF, we keeps to read from the DQ, so speculative instructions
        // can also reach here before executed
        if (head_inst->isNonSpeculative() || head_inst->isLoadReserved() ||
            head_inst->isStoreConditional() ||
            head_inst->isMemBarrier() || head_inst->isWriteBarrier() ||
            (head_inst->isLoad() && head_inst->strictlyOrdered())) {

            DPRINTF(Commit || Debug::FFCommit, "Encountered a barrier or non-speculative "
                    "instruction [sn:%lli] at the head of the ROB, PC %s.\n",
                    head_inst->seqNum, head_inst->pcState());

            if (inst_num > 0 || ldstQueue.hasStoresToWB()) {
                DPRINTF(Commit || Debug::FFCommit, "Waiting for all stores to writeback. inst_num: %i, "
                        "has store to wb: %i\n", inst_num, ldstQueue.hasStoresToWB());
                return false;
            }

            toNextCycle->diewc2diewc.nonSpecSeqNum = head_inst->seqNum;

            // Change the instruction so it won't try to commit again until
            // it is executed.
            head_inst->clearCanCommit();

            if (head_inst->isLoad() && head_inst->strictlyOrdered()) {
                DPRINTF(Commit || Debug::FFCommit, "[sn:%lli]: Strictly ordered load, PC %s.\n",
                        head_inst->seqNum, head_inst->pcState());
                toNextCycle->diewc2diewc.strictlyOrdered = true;
                toNextCycle->diewc2diewc.strictlyOrderedLoad = head_inst;
            } else {
                ++commitNonSpecStalls;
            }
        } else {
            DPRINTF(Commit || Debug::FFCommit, "Normal inst[%d] has not been executed yet\n",
                    head_inst->seqNum);

            HeadNotExec++;
            if (youngestExecuted == 0) {
                youngestExecuted = head_inst->seqNum;
            }
            if (head_inst->readyTick) {
                headReadyNotExec++;
                head_inst->headNotExec = true;
            }
            DPRINTF(CommitObserve, "youngestExecuted: %lu, head_inst: %lu\n",
                    youngestExecuted, head_inst->seqNum);
            if (youngestExecuted > head_inst->seqNum) {
                headExecDistance += ((youngestExecuted - head_inst->seqNum) / 100);
            }
        }
        return false;
    }

    if (head_inst->completeTick == curTick() - head_inst->fetchTick) {
        DPRINTF(FFCommit, "Inst[%llu] must not be committed and executed in "
                "the same cycle\n", head_inst->seqNum);
        return false;
    } else {
        DPRINTF(FFCommit, "comp tick: %u, curTick: %llu, fetch tick: %llu\n",
                head_inst->completeTick, curTick(), head_inst->fetchTick);
    }

    if (head_inst->isStoreConditional() && !head_inst->isCompleted()) {
        DPRINTF(FFCommit, "Inst[%llu] is store cond, and not completed yet,"
                " cannot commit\n", head_inst->seqNum);
        return false;
    }

    if (head_inst->isLoad() && !head_inst->loadVerified) {
        DPRINTF(FFCommit, "Inst[%llu] is load but not verified yet,"
                          " cannot commit\n", head_inst->seqNum);
        return false;
    }

    if (!dq.logicallyLT(dq.c.pointer2uint(head_inst->dqPosition), oldestForwarded) &&
            !(head_inst->isStoreConditional() || head_inst->isSerializeAfter())) {
        DPRINTF(FFCommit, "Inst[%llu] @(%i %i) is forwarded recently,"
                          " and cannot be committed right now\n",
                          head_inst->seqNum, head_inst->dqPosition.bank,
                          head_inst->dqPosition.index);
        return false;
    }

    if (head_inst->numDestRegs() && !head_inst->receivedDest) {
        DPRINTF(FFCommit, "Instruction[%lu] has not obtained its value from "
                "interconnect network\n", head_inst->seqNum);
        return false;
    }

    if (toNextCycle->diewc2diewc.squash &&
            toNextCycle->diewc2diewc.squashedSeqNum <= head_inst->seqNum) {
        DPRINTF(FFCommit, "Inst[%llu]'s checkpoint will be used for squashing next cycle"
                " to squash after[%llu], and cannot be committed right now\n",
                head_inst->seqNum,
                toNextCycle->diewc2diewc.squashedSeqNum);
        return false;
    }

    if (head_inst->isThreadSync()) {
        // Not handled for now.
        panic("Thread sync instructions are not handled yet.\n");
    }

    // Check if the instruction caused a fault.  If so, trap.
    Fault inst_fault = head_inst->getFault();

    // Stores mark themselves as completed.
    if (head_inst->isStore() && inst_fault == NoFault) {
        head_inst->setCompleted();

        mDepPred->commitStore(head_inst->physEffAddrLow,
                              head_inst->seqNum, head_inst->dqPosition);

        if (head_inst->physEffAddrHigh) {
            mDepPred->commitStore(head_inst->physEffAddrHigh,
                                  head_inst->seqNum, head_inst->dqPosition);
        }
    }


    if (inst_fault != NoFault) {
        DPRINTF(Commit || Debug::FFCommit, "Inst [sn:%lli] PC %s has a fault\n",
                head_inst->seqNum, head_inst->pcState());

        if (ldstQueue.hasStoresToWB() || inst_num > 0) {
            DPRINTF(Commit || Debug::FFCommit, "Stores outstanding, fault must wait.\n");
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

    if (head_inst->isLoad()) {
        mDepPred->commitLoad(head_inst->physEffAddrLow,
                             head_inst->seqNum, head_inst->dqPosition);

        if (head_inst->physEffAddrHigh) {
            // pass yet
        }
//        mDepPred->update(head_inst->instAddr(),
//                     head_inst->shouldForward,
//                     head_inst->shouldForwFrom, 0,
//                     head_inst->memPredHistory);
    }

    if (FullSystem) {
        panic("FF does not consider FullSystem yet\n");
    }
    DPRINTF(Commit || Debug::FFCommit, "Committing instruction with [sn:%lli] PC %s @DQ(%d %d)\n",
            head_inst->seqNum, head_inst->pcState(),
            head_inst->dqPosition.bank, head_inst->dqPosition.index);

    if (commitCounter >= commitTraceInterval && !head_inst->isForwarder()) {
        DPRINTFR(ValueCommit, "%lu VCommitting %lu instruction with sn:%lu PC:",
                curTick(), commitAll, head_inst->seqNum);
        if (Debug::ValueCommit) {
            std::cout << head_inst->pcState();
        }
        if (head_inst->numDestRegs() > 0) {
            DPRINTFR(ValueCommit, ", with wb value: %lu",
                    head_inst->getResult().asIntegerNoAssert());
        } else {
            DPRINTFR(ValueCommit, ", with wb value: none");
        }
        if (head_inst->isMemRef()) {
            DPRINTFR(ValueCommit, ", with v_addr: 0x%lx\n", head_inst->effAddr);
        } else {
            DPRINTFR(ValueCommit, ", with v_addr: none\n");
        }

        if (head_inst->numDestRegs() > 0) {
            DPRINTFR(FanoutLog, "Inst[%lu] with PC: %s has %u children and pred large: %i\n",
                    head_inst->seqNum, head_inst->pcState(),
                    head_inst->numChildren, head_inst->predLargeFanout);
        }

        commitCounter = 0;
    } else {
        commitCounter++;
    }
    if (!head_inst->isForwarder()) {
        commitAll++;
    }

    if (head_inst->traceData) {
        head_inst->traceData->setFetchSeq(head_inst->seqNum);
        head_inst->traceData->setCPSeq(thread->numOp);
        head_inst->traceData->dump();
        delete head_inst->traceData;
        head_inst->traceData = NULL;
    }
    if (head_inst->isReturn()) {
        DPRINTF(FFCommit, "Return Instruction Committed [sn:%lli] PC %s\n",
                        head_inst->seqNum, head_inst->pcState());
    }

    // Update the commit rename map
    bool valid;
    FFRegValue value;
    tie(valid, value) = archState.commitInst(head_inst);
    dq.retireHead(valid, value);

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
    DPRINTF(Commit, "Scheduled latency: %lli\n", latency);

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

    DPRINTF(Commit || Debug::FFCommit, "Trying to commit instructions in the DQ.\n");

    unsigned num_committed = 0, commit_limit = width;

    DynInstPtr head_inst;

    // Commit as many instructions as possible until the commit bandwidth
    // limit is reached, or it becomes impossible to commit any more.
    while (num_committed < commit_limit && !skipThisCycle) {
        // Check for any interrupt that we've already squashed for
        // and start processing it.
        if (interrupt != NoFault)
            handleInterrupt();

        head_inst = getTailInst(); // head_inst: olddest inst/ tail in DQ

        if (!head_inst) {
            DPRINTF(Commit || Debug::FFCommit, "Break commit because oldest inst is null\n");
            num_committed++;
            if (!dq.isEmpty()) {
                dq.tryFastCleanup();
                if (!dq.validPosition(oldestForwarded)) {
                    // 反正clean up之后有很多空位，不及时commit也无所谓
                    oldestForwarded = dq.getTailPtr();
                }
                dq.maintainOldestUsed();
            }
            break;
        }
        if (!head_inst->readyToCommit()) {
            DPRINTF(Commit || Debug::FFCommit, "Break commit because oldest inst is not ready to commit\n");
            HeadNotExec++;
            if (youngestExecuted == 0) {
                youngestExecuted = head_inst->seqNum;
            }
            if (head_inst->readyTick) {
                headReadyNotExec++;
                head_inst->headNotExec = true;
            }

            DPRINTF(CommitObserve, "youngestExecuted: %lu, head_inst: %lu\n",
                    youngestExecuted, head_inst->seqNum);
            if (youngestExecuted > head_inst->seqNum) {
                headExecDistance += ((youngestExecuted - head_inst->seqNum) / 100);
            }
            break;
        }

        DPRINTF(Commit || Debug::FFCommit, "Trying to commit head instruction, [sn:%i]\n",
                head_inst->seqNum);

        // If the head instruction is squashed, it is ready to retire
        // (be removed from the ROB) at any time.
        if (head_inst->isSquashed()) {
            DPRINTF(Commit, "Retiring squashed instruction from DQ.\n");
            activityThisCycle();

            head_inst->clearInDQ();
            cpu->removeFrontInst(head_inst);

            ++commitSquashedInsts;
            // Notify potential listeners that this instruction is squashed
            ppSquash->notify(head_inst);
            ++num_committed;
            insts_to_commit.pop();
            continue;

        } else {
            pc = head_inst->pcState();

            // Increment the total number of non-speculative instructions
            // executed.
            // Hack for now: it really shouldn't happen until after the
            // commit is deemed to be successful, but this count is needed
            // for syscalls.
            DPRINTF(Commit, "commit reach 1\n");
            thread->funcExeInst++;

            // Try to commit the head instruction.
            bool commit_success = commitHead(head_inst, num_committed);
            DPRINTF(Commit, "commit reach 2\n");

            if (commit_success) {
                activityThisCycle();

                toAllocation->diewcInfo.updateDQTail = true;
                toAllocation->diewcInfo.updateDQHead = true;
                if (head_inst->isForwarder()) {
                    commit_limit++;
                }
                statCommittedInstType[head_inst->opClass()]++;
                ppCommit->notify(head_inst);

                changedDQNumEntries = true;

                // Set the doneSeqNum to the youngest committed instruction.
                if (!toNextCycle->diewc2diewc.squash) {
                    toNextCycle->diewc2diewc.doneSeqNum = head_inst->seqNum;
                }
                // toNextCycle->diewc2diewc.donePointer = head_inst->dqPosition;

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
                anySuccessfulCommit = true;
                DPRINTF(FFCommit, "Commit successfully, set lastest to %lu\n", lastCommitedSeqNum);

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

                insts_to_commit.pop();
                toAllocation->diewcInfo.usedDQ = true;
            } else {
                DPRINTF(Commit, "Unable to commit head instruction PC:%s "
                        "[sn:%llu].\n",
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
typename FFDIEWC<Impl>::DynInstPtr FFDIEWC<Impl>::getTailInst() {
    DynInstPtr n = nullptr;
    return insts_to_commit.empty() ? n : insts_to_commit.front();
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
    DPRINTF(FFSquash, "handleSquash entry\n");
    if (trapSquash) {
        assert(!tcSquash);
        squashFromTrap();

    } else if (tcSquash) {
        assert(commitStatus != TrapPending);
        squashFromTC();

    } else if (commitStatus == SquashAfterPending) {
        squashFromSquashAfter();
    }

    if (fromLastCycle->diewc2diewc.squash) {
        DPRINTF(FFSquash, "squashedSeqNum: %llu youngestSeqNum: %llu\n",
                fromLastCycle->diewc2diewc.squashedSeqNum,
                youngestSeqNum);
    }
    if (fromLastCycle->diewc2diewc.squash &&
        commitStatus != TrapPending &&
        (fromLastCycle->diewc2diewc.squashedSeqNum <= youngestSeqNum ||
         fromLastCycle->diewc2diewc.squashAll)) {

        if (fromLastCycle->diewc2diewc.mispredictInst) {
            DPRINTF(FFCommit,
                    "Squashing due to branch mispred PC:%#x [sn:%i]\n",
                    fromLastCycle->diewc2diewc.mispredictInst->instAddr(),
                    fromLastCycle->diewc2diewc.squashedSeqNum);
        } else {
            DPRINTF(FFCommit,
                    "Squashing due to order violation [sn:%i]\n",
                    fromLastCycle->diewc2diewc.squashedSeqNum);
        }

        toAllocation->diewcInfo.usedDQ = true;
        changedDQNumEntries = true;
        commitStatus = DQSquashing;

        if (!fromLastCycle->diewc2diewc.squashAll) {
            InstSeqNum squashed_inst = fromLastCycle->diewc2diewc.squashedSeqNum;

            if (fromLastCycle->diewc2diewc.includeSquashInst) {
                squashed_inst--;
            }

            youngestSeqNum = squashed_inst;
            auto p = fromLastCycle->diewc2diewc.squashedPointer;
            DPRINTF(FFSquash, "Olddest inst ptr to squash: (%i %i)\n", p.bank, p.index);
            dq.squash(fromLastCycle->diewc2diewc.squashedPointer, false, false);
            dqSquashing = true;
            dqSquashSeq = squashed_inst;
            archState.recoverCPT(squashed_inst);

            // toNextCycle->diewc2diewc.squash = false;
            // toNextCycle->diewc2diewc.pc = fromLastCycle->diewc2diewc.pc;
            // does not override if there is another squashing this cycle

            // todo: following LOCs will cause loop?
            // toNextCycle->diewc2diewc.doneSeqNum = squashed_inst;
            // toNextCycle->diewc2diewc.squash = true;
            // toNextCycle->diewc2diewc.dqSquashing = true;
            // toNextCycle->diewc2diewc.mispredictInst =
            //         fromLastCycle->diewc2diewc.mispredictInst;
            // toNextCycle->diewc2diewc.branchTaken =
            //         fromLastCycle->diewc2diewc.branchTaken;
            // toNextCycle->diewc2diewc.squashInst = dq.findInst(squashed_inst);

            if (toNextCycle->diewc2diewc.mispredictInst) {
                if (toNextCycle->diewc2diewc.mispredictInst->isUncondCtrl()) {
                    toNextCycle->diewc2diewc.branchTaken = true;
                }
            }

        } else {
            DPRINTF(FFSquash, "Squashing all!\n");
            DQPointer dont_care;
            bool dont_care_either = false;
            dq.squash(dont_care, true, dont_care_either);
            archState.squashAll();
            // archState.dumpMaps();
        }

    }

    if (dqSquashing && dq.queuesEmpty()) {
        DPRINTF(FFSquash, "cleared dqSquashing\n");
        dqSquashing = false;
        dqSquashSeq = 0;
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
    DynInstPtr tail_inst = dq.getTail();
    InstSeqNum squashed_inst = dq.isEmpty() ? lastCommitedSeqNum :
        (tail_inst ? tail_inst->seqNum - 1: lastCommitedSeqNum);

    youngestSeqNum = lastCommitedSeqNum;

    toAllocation->diewcInfo.usedDQ = true;

    changedDQNumEntries = true;

    commitStatus = DQSquashing; // to prevent pc from advancing

    toNextCycle->diewc2diewc.doneSeqNum = squashed_inst;
    toNextCycle->diewc2diewc.squashedSeqNum = squashed_inst;
    // if (!dq.isEmpty()) {
    //     toNextCycle->diewc2diewc.donePointer = dq.getTail()->dqPosition;
    // }
    toNextCycle->diewc2diewc.squash = true;
//    toNextCycle->diewc2diewc.dqSquashing = true;
    toNextCycle->diewc2diewc.mispredictInst = nullptr;
    toNextCycle->diewc2diewc.squashInst = nullptr;
    toNextCycle->diewc2diewc.pc = pc;
    toNextCycle->diewc2diewc.squashAll = true;
    DPRINTF(IEW, "SquashAll toNextCycle PC: %s.\n", pc);
}

template<class Impl>
FFDIEWC<Impl>::FFDIEWC(XFFCPU *cpu, DerivFFCPUParams *params)
        :
        cpu(cpu),
        freeEntries{params->numDQBanks * params->DQDepth * params->numDQGroups,
                    params->LQEntries, params->SQEntries},
        serializeOnNextInst(false),
        dq(params),
        archState(params),
        fuWrapper(),
        ldstQueue(cpu, this, params),
        fetchRedirect(false),
        dqSquashing(false),
        dqSquashSeq(0),
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
        allocationToDIEWCDelay(params->allocationToDIEWCDelay),
        commitTraceInterval(params->commitTraceInterval),
        commitCounter(0),
        largeFanoutThreshold(params->LargeFanoutThreshold),
        EnableReshape(params->EnableReshape),
        mDepPred(params->mDepPred)
{
    skidBufferMax = (allocationToDIEWCDelay + 1 + 4)*width;

    dq.setLSQ(&ldstQueue);
    dq.setDIEWC(this);
    dq.setCPU(cpu);

    archState.setDIEWC(this);
    archState.setDQ(&dq);

    ldstQueue.setDQCommon(&dq.c);
    ldstQueue.setMemDepPred(mDepPred);
}

template<class Impl>
void
FFDIEWC<Impl>::squashInFlight()
{
    DPRINTF(IEW, "Squashing in-flight instructions.\n");

    // Tell the IQ to start squashing.
    // todo: we don't need this in forwardflow?

    // Tell the LDSTQ to start squashing.
    // todo: check this LOCs
    if (fromLastCycle->diewc2diewc.squashAll) {
        ldstQueue.squash(fromLastCycle->diewc2diewc.doneSeqNum, DummyTid);
    } else {
        ldstQueue.squash(fromLastCycle->diewc2diewc.squashedSeqNum, DummyTid);
    }
    // dq.squash(fromLastCycle->diewc2diewc.donePointer, false);
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

        if (!skidBuffer.front()->isForwarder()) {
            toAllocation->diewcInfo.dispatched++;
        }

        skidBuffer.pop_front();
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
        if (!insts.front()->isForwarder()) {
            toAllocation->diewcInfo.dispatched++;
        }

        insts.pop_front();
    }
}

template<class Impl>
void FFDIEWC<Impl>::updateComInstStats(DynInstPtr &inst) {

    if (!inst->isMicroop() || inst->isLastMicroop()) {
        if (!inst->isForwarder()) {
            instsCommitted++;
        } else {
            forwardersCommitted++;
        }
    }
    if (!inst->isForwarder()) {
        opsCommitted++;
    }

    // To match the old model, don't count nops and instruction
    // prefetches towards the total commit count.
    if (!inst->isNop() && !inst->isInstPrefetch()) {
        cpu->instDone(DummyTid, inst);
    }

    //
    //  Control Instructions
    //
    if (inst->isControl())
        statComBranches++;

        //
    //  Memory references
    //
    if (inst->isMemRef()) {
        statComRefs++;

        if (inst->isLoad()) {
            statComLoads++;
        }
    }

    if (inst->isMemBarrier()) {
        statComMembars++;
    }

    // Integer Instruction
    if (inst->isInteger())
        statComInteger++;

    // Floating Point Instruction
    if (inst->isFloating())
        statComFloating++;
    // Vector Instruction
    if (inst->isVector())
        statComVector++;

    // Function Calls
    if (inst->isCall())
        statComFunctionCalls++;

    gainFromReshape += inst->gainFromReshape;
    reshapeContrib += inst->reshapeContrib;
    nonCriticalForward += inst->nonCriticalFw;
    negativeContrib += inst->negativeContrib;

    wkDelayedCycles += inst->wkDelayedCycle;
    queueingDelay += inst->queueingDelay;
    ssrDelay += inst->ssrDelay;
    pendingDelay += inst->pendingDelay;
    FUContentionDelay += inst->FUContentionDelay;

}

template<class Impl>
void FFDIEWC<Impl>::insertPointerPairs(const std::list<PointerPair>& pairs) {
    for (const auto &pair: pairs) {
        pointerPackets.push(pair);
    }
    DPRINTF(FFDisp, "Size of pair buffer after merge %lu pairs: %lu\n",
            pairs.size(), pointerPackets.size());
}

template<class Impl>
void FFDIEWC<Impl>::insertPointerPair(const PointerPair& pair) {
    DPRINTF(FFDisp, "Inserting single pair:" ptrfmt "->" ptrfmt "\n",
            extptr(pair.dest), extptr(pair.payload));
    setOldestFw(pair.dest);
    pointerPackets.push(pair);
    DPRINTF(FFDisp, "Size of pair buffer after merge 1 pair: %lu\n",
            pointerPackets.size());
}


template<class Impl>
void FFDIEWC<Impl>::rescheduleMemInst(DynInstPtr &inst, bool isStrictOrdered,
        bool isFalsePositive)
{
    dq.rescheduleMemInst(inst, isStrictOrdered, isFalsePositive);
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
    assert(inst->isLoad() || inst->isStoreConditional());
    inst->setCanCommit();
    // assert(inst->sfuWrapper);
    // inst->sfuWrapper->markWb();
    archState.postExecInst(inst);

    DPRINTF(NoSQSMB, "Inst[%lu] wb count: %i dest value: %lu\n",
            inst->seqNum, inst->wbCount, inst->getDestValue().i);

    bool violation = false;
    if (!inst->loadVerified &&
        (inst->isNormalBypass() || (inst->loadVerifying && inst->wbCount == 2))) {
        violation = checkViolation(inst);
        if (violation) {
            DPRINTF(NoSQSMB, "violation detected!\n");
        }
    }


    if (violation) {
        fetchRedirect = true;
        ++memOrderViolationEvents;
        squashDueToMemMissPred(inst);

        SSBFCell *cell = mDepPred->tssbf.find(inst->physEffAddrLow);
        if (inst->isNormalBypass()) {
            // false positive
            DPRINTF(NoSQPred, "Count down bypassing confidence for inst[%lu]\n", inst->seqNum);
            mDepPred->update(inst->instAddr(), false,
                             0, // dont care
                             0, // dont care
                             inst->memPredHistory
            );

        } else {
            // false negative
            if (cell) {
                DPRINTF(NoSQPred, "Marking mem dep:" ptrfmt "->" ptrfmt "\n",
                        extptr(cell->predecessorPosition), extptr(inst->dqPosition));
                mDepPred->update(inst->instAddr(), true,

                                 inst->seqNum - cell->lastStore, //sn dist
                                 dq.c.computeDist(inst->dqPosition, cell->predecessorPosition),
                        // pointer dist to last predecessor

                                 inst->memPredHistory);
            } else {
                DPRINTF(NoSQPred, "Producing TSSBF entry has been evicted,"
                                  " we have to give up recording\n");
            }
        }
    } else {
        dq.writebackLoad(inst);
    }
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

    toAllocation->diewcInfo.dqHead = dq.getHeadPtr() + 1;
    toAllocation->diewcInfo.dqTail = dq.getTailPtr();

    if (cpu->checker) {
        cpu->checker->setDcachePort(&cpu->getDataPort());

        cpu->activateStage(XFFCPU::IEWCIdx);
    }
    commitCounter = 0;
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
typename Impl::DynInstPtr FFDIEWC<Impl>::readTailInst(ThreadID tid)
{
    return dq.getTail();
}

template<class Impl>
void FFDIEWC<Impl>::checkMisprediction(DynInstPtr &inst)
{
    if (!fetchRedirect ||
        !toNextCycle->diewc2diewc.squash ||
        toNextCycle->diewc2diewc.squashedSeqNum > inst->seqNum) {

        if (inst->mispredicted()) {
            panic("unexpected mispredicted mem ref\n");
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
        toNextCycle->diewc2diewc.doneSeqNum = inst->seqNum;
        toNextCycle->diewc2diewc.squashedSeqNum = inst->seqNum;
        toNextCycle->diewc2diewc.squashedPointer = inst->dqPosition;

        TheISA::PCState pc = inst->pcState();
        TheISA::advancePC(pc, inst->staticInst);
        toNextCycle->diewc2diewc.pc = pc;
        DPRINTF(IEW, "toNextCycle PC: %s.\n", pc);
        DPRINTF(IEW, "Will replay after inst[%llu].\n", inst->seqNum);

        toNextCycle->diewc2diewc.mispredictInst = inst;
        toNextCycle->diewc2diewc.squashInst = inst;
        toNextCycle->diewc2diewc.includeSquashInst = false;
        toNextCycle->diewc2diewc.branchTaken = inst->pcState().branching();

        wroteToTimeBuffer = true;
        branchMispredicts++;
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
        .name(name() + ".dispSquashedInsts")
        .desc("dispSquashedInsts");
    dqFullEvents
        .name(name() + ".dqFullEvents")
        .desc("dqFullEvents");
    lqFullEvents
        .name(name() + ".lqFullEvents")
        .desc("lqFullEvents");
    sqFullEvents
        .name(name() + ".sqFullEvents")
        .desc("sqFullEvents");
    dispaLoads
        .name(name() + ".dispaLoads")
        .desc("dispaLoads");
    dispStores
        .name(name() + ".dispStores")
        .desc("dispStores");
    dispNonSpecInsts
        .name(name() + ".dispNonSpecInsts")
        .desc("dispNonSpecInsts");
    dispatchedInsts
        .name(name() + ".dispatchedInsts")
        .desc("dispatchedInsts");
    blockCycles
        .name(name() + ".blockCycles")
        .desc("blockCycles");
    squashCycles
        .name(name() + ".squashCycles")
        .desc("squashCycles");
    runningCycles
        .name(name() + ".runningCycles")
        .desc("runningCycles");
    unblockCycles
        .name(name() + ".unblockCycles")
        .desc("unblockCycles");
    commitNonSpecStalls
        .name(name() + ".commitNonSpecStalls")
        .desc("commitNonSpecStalls");
    commitSquashedInsts
        .name(name() + ".commitSquashedInsts")
        .desc("commitSquashedInsts");
    branchMispredicts
        .name(name() + ".branchMispredicts")
        .desc("branchMispredicts");
    predictedTakenIncorrect
        .name(name() + ".predictedTakenIncorrect")
        .desc("predictedTakenIncorrect");
    predictedNotTakenIncorrect
        .name(name() + ".predictedNotTakenIncorrect")
        .desc("predictedNotTakenIncorrect");

    commitEligibleSamples
            .name(name() + ".ommitEligibleSamples")
            .desc("ommitEligibleSamples");

    statCommittedInstType
            .init(Num_OpClasses)
            .name(name() + ".statCommittedInstType")
            .desc("statCommittedInstType");

    numCommittedDist
            .init(0, width, 1)
            .name(name() + ".numCommittedDist")
            .desc("numCommittedDist")
            .flags(Stats::pdf);

    instsCommitted
        .name(name() + ".instsCommitted")
        .desc("instsCommitted")
        ;
    forwardersCommitted
        .name(name() + ".forwardersCommitted")
        .desc("forwardersCommitted")
        ;
    opsCommitted
        .name(name() + ".opsCommitted")
        .desc("opsCommitted")
        ;
    statComBranches
        .name(name() + ".statComBranches")
        .desc("statComBranches")
        ;
    statComRefs
        .name(name() + ".statComRefs")
        .desc("statComRefs")
        ;
    statComLoads
        .name(name() + ".statComLoads")
        .desc("statComLoads")
        ;
    statComMembars
        .name(name() + ".statComMembars")
        .desc("statComMembars")
        ;
    statComInteger
        .name(name() + ".statComInteger")
        .desc("statComInteger")
        ;
    statComFloating
        .name(name() + ".statComFloating")
        .desc("statComFloating")
        ;
    statComVector
        .name(name() + ".statComVector")
        .desc("statComVector")
        ;
    statComFunctionCalls
        .name(name() + ".statComFunctionCalls")
        .desc("statComFunctionCalls")
        ;

    dq.regStats();
    ldstQueue.regStats();
    archState.regStats();

    iewExecutedInsts
            .name(name() + ".iewExecutedInsts")
            .desc("Number of executed instructions");

    iewExecutedRefs
            .name(name() + ".exec_refs")
            .desc("number of memory reference insts executed");

    iewExecLoadInsts
            .name(name() + ".iewExecLoadInsts")
            .desc("Number of load instructions executed");

    iewExecutedBranches
            .name(name() + ".exec_branches")
            .desc("Number of branches executed");

    iewExecStoreInsts
            .name(name() + ".exec_stores")
            .desc("Number of stores executed");
    iewExecStoreInsts = iewExecutedRefs - iewExecLoadInsts;

    totalFanoutPredictions
        .name(name() + ".totalFanoutPredictions")
        .desc("totalFanoutPredictions");
    falseNegativeLF
        .name(name() + ".falseNegativeLF")
        .desc("falseNegativeLF");
    falsePositiveLF
        .name(name() + ".falsePositiveLF")
        .desc("falsePositiveLF");
    fanoutMispredRate
        .name(name() + ".fanoutMispredRate")
        .desc("fanoutMispredRate");
    largeFanoutInsts
        .name(name() + ".largeFanoutInsts")
        .desc("largeFanoutInsts");
    fanoutMispredRate = (falseNegativeLF + falsePositiveLF) / totalFanoutPredictions;

    falseNegativeBypass
            .name(name() + ".falseNegativeBypass")
            .desc("falseNegativeBypass");
    falsePositiveBypass
            .name(name() + ".falsePositiveBypass")
            .desc("falsePositiveBypass");


    firstLevelFw
        .name(name() + ".firstLevelFw")
        .desc("firstLevelFw");
    secondaryLevelFw
        .name(name() + ".secondaryLevelFw")
        .desc("secondaryLevelFw");

    gainFromReshape
        .name(name() + ".gainFromReshape")
        .desc("gainFromReshape");

    reshapeContrib
        .name(name() + ".reshapeContrib")
        .desc("reshapeContrib");

    nonCriticalForward
        .name(name() + ".nonCriticalForward")
        .desc("nonCriticalForward");

    negativeContrib
        .name(name() + ".negativeContrib")
        .desc("negativeContrib");

    wkDelayedCycles
        .name(name() + ".wkDelayedCycles")
        .desc("wkDelayedCycles");

    ssrDelay
        .name(name() + ".ssrDelay")
        .desc("ssrDelay");

    queueingDelay
        .name(name() + ".queueingDelay")
        .desc("queueingDelay");

    pendingDelay
        .name(name() + ".pendingDelay")
        .desc("pendingDelay");

    FUContentionDelay
        .name(name() + ".FUContentionDelay")
        .desc("FUContentionDelay");

    HeadNotExec
        .name(name() + ".HeadNotExec")
        .desc("HeadNotExec")
        ;
    headExecDistance
        .name(name() + ".headExecDistance")
        .desc("headExecDistance")
        ;
    meanHeadExecDistance
        .name(name() + ".meanHeadExecDistance")
        .desc("meanHeadExecDistance")
        ;
    meanHeadExecDistance = headExecDistance / HeadNotExec;

    readyExecDelayTicks
        .name(name() + ".readyExecDelayTicks")
        .desc("readyExecDelayTicks")
        ;
    readyInBankDelay
        .name(name() + ".readyInBankDelay")
        .desc("readyInBankDelay")
        ;
    headReadyExecDelayTicks
        .name(name() + ".headReadyExecDelayTicks")
        .desc("headReadyExecDelayTicks")
        ;
    headReadyNotExec
        .name(name() + ".headReadyNotExec")
        .desc("headReadyNotExec")
        ;

}

template<class Impl>
void FFDIEWC<Impl>::setAllocQueue(TimeBuffer<AllocationStruct> *aq_ptr)
{
    allocationQueue = aq_ptr;

    fromAllocation = allocationQueue->getWire(-1);
}

template<class Impl>
void FFDIEWC<Impl>::executeInst(DynInstPtr &inst)
{
    assert(inst);
    DPRINTF(DIEWC||Debug::RSProbe1 || Debug::FFExec || Debug::ObExec,
            "Executing inst[%lu] %s\n", inst->seqNum,
            inst->staticInst->disassemble(inst->instAddr()));
    DPRINTF(ValueExec, "Executing inst[%lu] ", inst->seqNum);
    if (Debug::ValueExec) {
        std::cout << inst->staticInst->disassemble(inst->instAddr()) << endl;
    }

    if (inst->seqNum > youngestExecuted) {
        youngestExecuted = inst->seqNum;
    }

    activityThisCycle();
    if (inst->isSquashed()) {
        inst->setExecuted();
        inst->completeTick = curTick() - inst->fetchTick;
        DPRINTF(DIEWC, "set completeTick to %u\n", inst->completeTick);
        inst->setCanCommit();
        return;
    }

    Fault fault = NoFault;

    if (inst->isMemRef()) {
        if (inst->isLoad()) {
            fault = ldstQueue.executeLoad(inst);

            if (inst->isTranslationDelayed() &&
                fault == NoFault) {
                // A hw page table walk is currently going on; the
                // instruction must be deferred.
                DPRINTF(IEW || Debug::FFExec, "Execute: Delayed translation, deferring "
                             "load.\n");
                dq.deferMemInst(inst); // todo ???
                return;;
            }

            if (inst->isDataPrefetch() || inst->isInstPrefetch()) {
                inst->fault = NoFault;
            }
        } else if (inst->isStore()) {
            fault = ldstQueue.executeStore(inst);

            if (inst->isTranslationDelayed() &&
                fault == NoFault) {
                // A hw page table walk is currently going on; the
                // instruction must be deferred.
                DPRINTF(IEW, "Execute: Delayed translation, deferring "
                             "store.\n");
                dq.deferMemInst(inst);
                return;
            }

            // If the store had a fault then it may not have a mem req
            if (fault != NoFault || !inst->readPredicate() ||
                !inst->isStoreConditional()) {
                // If the instruction faulted, then we need to send it along
                // to commit without the instruction completing.
                // Send this instruction to commit, also make sure iew stage
                // realizes there is activity.
                if (!inst->readPredicate()) {
                    panic("readPredicate not handled in RV\n");
                }
                inst->setExecuted();
                inst->completeTick = curTick() - inst->fetchTick;
                DPRINTF(DIEWC, "set completeTick to %u\n", inst->completeTick);
                inst->setCanCommit();

                if (!inst->isStoreConditional()) {
                    dq.completeMemInst(inst);
                }
            }

            // Store conditionals will mark themselves as
            // executed, and their writeback event will add the
            // instruction to the queue to commit.
        } else {
            panic("Unexpected memory type!\n");
        }
    } else {
        if (inst->getFault() == NoFault) {
            inst->execute();
            if (!inst->readPredicate())
                panic("readPredicate not handled in RV\n");
        }
        inst->setExecuted();
        inst->completeTick = curTick() - inst->fetchTick;

        if (inst->readyTick) {
            inst->readyExecDelayTicks = curTick() - inst->readyTick;
            readyExecDelayTicks += inst->readyExecDelayTicks;
            if (inst->headNotExec) {
                headReadyExecDelayTicks += inst->readyExecDelayTicks;
            }
        }
        readyInBankDelay += inst->readyInBankDelay;

        DPRINTF(DIEWC, "set completeTick to %u\n", inst->completeTick);
        inst->setCanCommit();
        archState.postExecInst(inst);
    }

    if (inst->isMemBarrier() || inst->isWriteBarrier()) {
        dq.completeMemInst(inst);
    }

    updateExeInstStats(inst);

    if ((!fetchRedirect ||  // fetch not redirected

        !toNextCycle->diewc2diewc.squash ||  // no squash needed yet

        toNextCycle->diewc2diewc.squashedSeqNum > inst->seqNum ||
        // this squash is more primary that one found in this cycle

        (toNextCycle->diewc2diewc.squashedSeqNum == inst->seqNum &&
         (toNextCycle->diewc2diewc.memViolation || toNextCycle->diewc2diewc.halfSquash)
         && inst->mispredicted()))
        // branch misprediction is more primary than the mem violation

        && (!fromLastCycle->diewc2diewc.squash ||
            fromLastCycle->diewc2diewc.squashedSeqNum > inst->seqNum ||
        // this squash is more primary that one found in last cycle

        (fromLastCycle->diewc2diewc.squashedSeqNum == inst->seqNum &&
         (fromLastCycle->diewc2diewc.memViolation || fromLastCycle->diewc2diewc.halfSquash)
         && inst->mispredicted()))
        // branch misprediction is more primary than the mem violation found in last cycle
        ) {

        // Prevent testing for misprediction on load instructions,
        // that have not been executed.
        bool loadNotExecuted = !inst->isExecuted() && inst->isLoad();

        if (inst->isControl() && !inst->mispredicted()) {
            TheISA::PCState temp_pc = inst->pcState();
            TheISA::advancePC(temp_pc, inst->staticInst);
            TheISA::PCState predPC = inst->predPC;
            DPRINTF(IEW, "Execute: Branch prediction is correct for inst[%llu]\n",
                    inst->seqNum);
            DPRINTF(DIEWC, "Pred PC: %s, NPC: %s\n", predPC, temp_pc);

        }

        if (inst->isControl() && inst->mispredicted() && !loadNotExecuted) {
            fetchRedirect = true;

            DPRINTF(IEW, "Execute: Branch mispredict detected.\n");
            DPRINTF(IEW, "Predicted target was PC: %s.\n",
                    inst->readPredTarg());
            DPRINTF(IEW, "Execute: Redirecting fetch to PC: %s.\n",
                    inst->pcState());
            // If incorrect, then signal the ROB that it must be squashed.
            squashDueToBranch(inst);

            ppMispredict->notify(inst);

            if (inst->readPredTaken()) {
                predictedTakenIncorrect++;
            } else {
                predictedNotTakenIncorrect++;
            }
        }
    } else {
        DPRINTF(FFSquash, "Will not check for squash because condition not satisified\n");
    }
}

template<class Impl>
void
FFDIEWC<Impl>::squashDueToMemMissPred(DynInstPtr &violator)
{
    DPRINTF(DIEWC, "Memory violation, squashing violator and younger "
                 "insts, PC: %s [sn:%i].\n", violator->pcState(), violator->seqNum);

    if ((!toNextCycle->diewc2diewc.squash ||
        violator->seqNum <= toNextCycle->diewc2diewc.squashedSeqNum)
            //more primary than that found in this cyle
        && (!fromLastCycle->diewc2diewc.squash ||
        violator->seqNum <= fromLastCycle->diewc2diewc.squashedSeqNum)
        //more primary than that found in last cyle
        ) {

        InstSeqNum youngest_cpted_inst_seq = archState.getYoungestCPTBefore(violator->seqNum);

        if (!youngest_cpted_inst_seq) {
            squashAll();
             // Where to find a cpt hint?
             cptHint = true;
             toCheckpoint = violator->instAddr() - 4;
             DPRINTF(FFSquash, "Hint to checkpoint on pc: 0x%llx next time"
                     " in case mem violation\n", toCheckpoint);

        } else {
            toNextCycle->diewc2diewc.squash = true;

            toNextCycle->diewc2diewc.doneSeqNum = youngest_cpted_inst_seq;
            toNextCycle->diewc2diewc.squashedSeqNum = youngest_cpted_inst_seq;
            DynInstPtr innocent_victim = dq.findBySeq(youngest_cpted_inst_seq);
            toNextCycle->diewc2diewc.squashedPointer = innocent_victim->dqPosition;

            TheISA::PCState npc;
            if (innocent_victim->isControl() && !innocent_victim->isExecuted() ) {
                npc = innocent_victim->predPC;
            } else {
                npc = innocent_victim->pcState();
                TheISA::advancePC(npc, innocent_victim->staticInst);
            }
            toNextCycle->diewc2diewc.pc = npc;
            DPRINTF(IEW, "Will replay after inst[%llu].\n", innocent_victim->seqNum);
            DPRINTF(IEW, "toNextCycle PC: %s.\n", npc);
            toNextCycle->diewc2diewc.mispredictInst = nullptr;

            // Must include the memory violator in the squash.
            // todo: note that this is not true in forward flow
            toNextCycle->diewc2diewc.includeSquashInst = false;

            wroteToTimeBuffer = true;
        }
        toNextCycle->diewc2diewc.memViolation = true;
    }
}

template<class Impl>
void FFDIEWC<Impl>::clearAtStart()
{
    skipThisCycle = false;
    while (!insts_to_commit.empty()) {
        insts_to_commit.pop();
    }
    DQPointerJumped = false;
    anySuccessfulCommit = false;
}

template<class Impl>
void FFDIEWC<Impl>::sendBackwardInfo()
{
    toAllocation->diewcInfo.dqTail = dq.getTailPtr();
    toAllocation->diewcInfo.dqHead = dq.inc(dq.getHeadPtr()); // p+1 to allocation
    DPRINTF(DIEWC, "To allocation head: %i, tail: %i\n", toAllocation->diewcInfo.dqHead,
            toAllocation->diewcInfo.dqTail);
    if (DQPointerJumped) {
        toAllocation->diewcInfo.updateDQTail = true;
        if (!dq.validPosition(oldestForwarded)) {
            oldestForwarded = dq.getTailPtr();
        }
        dq.maintainOldestUsed();

        if (fromLastCycle->diewc2diewc.squash) {
            toAllocation->diewcInfo.updateDQHead = true;
        }
    }
    toAllocation->diewcInfo.freeDQEntries = dq.numFree();
    toAllocation->diewcInfo.emptyDQ = dq.isEmpty();
}


template<class Impl>
void FFDIEWC<Impl>::setOldestFw(BasePointer _ptr)
{
    auto ptr = dq.c.pointer2uint(_ptr);
    assert(dq.validPosition(ptr));
    if (!dq.validPosition(oldestForwarded)) {
        oldestForwarded = dq.getTailPtr();
    } else if (dq.logicallyLT(ptr, oldestForwarded)) {
        oldestForwarded = ptr;
        DPRINTF(FFCommit, "Setting oldest forwarded to %d:" ptrfmt "\n",
                oldestForwarded, extptr(_ptr));
    }
}

template<class Impl>
void FFDIEWC<Impl>::resetOldestFw()
{
    // todo: use it @ no fw pointers in flight
    oldestForwarded = dq.getHeadPtr();  // set it to the youngest inst
    DPRINTF(FFCommit, "Resetting oldest forwarded to %d\n", oldestForwarded);
}

template<class Impl>
InstSeqNum FFDIEWC<Impl>::getOldestFw()
{
    return oldestForwarded;
}

template<class Impl>
void FFDIEWC<Impl>::clearAtEnd()
{
    dq.endCycle();
}

template<class Impl>
void FFDIEWC<Impl>::checkDQHalfSquash()
{
    if (!dq.halfSquash) {
        return;
    }
    InstSeqNum victim_seq = dq.halfSquashSeq;

    DPRINTF(DIEWC, "DQ halfSquash, squash after %llu\n", victim_seq);

    if ((!toNextCycle->diewc2diewc.squash ||
        victim_seq <= toNextCycle->diewc2diewc.squashedSeqNum)
            //more primary than that found in this cyle
        && (!fromLastCycle->diewc2diewc.squash ||
        victim_seq <= fromLastCycle->diewc2diewc.squashedSeqNum)
        //more primary than that found in last cyle

        && (!dqSquashing || victim_seq <= dqSquashSeq)
        // more primary than processing squashing
        ) {

        InstSeqNum youngest_cpted_inst_seq = archState.getYoungestCPTBefore(victim_seq);

        if (!youngest_cpted_inst_seq) {
            squashAll();
            cptHint = true;
            toCheckpoint = dq.halfSquashPC;
            DPRINTF(FFSquash, "Squash all because no cpt found\n");
            DPRINTF(FFSquash, "Hint to checkpoint on pc: 0x%llx next time"
                    " in case half squash\n", toCheckpoint);

        } else {
            toNextCycle->diewc2diewc.squash = true;

            toNextCycle->diewc2diewc.doneSeqNum = youngest_cpted_inst_seq;
            toNextCycle->diewc2diewc.squashedSeqNum = youngest_cpted_inst_seq;
            DynInstPtr innocent_victim = dq.findBySeq(youngest_cpted_inst_seq);
            toNextCycle->diewc2diewc.squashedPointer = innocent_victim->dqPosition;

            TheISA::PCState npc;
            if (innocent_victim->isControl() && !innocent_victim->isExecuted() ) {
                npc = innocent_victim->predPC;
            } else {
                npc = innocent_victim->pcState();
                TheISA::advancePC(npc, innocent_victim->staticInst);
            }
            toNextCycle->diewc2diewc.pc = npc;
            DPRINTF(IEW, "Will replay after inst[%llu].\n", innocent_victim->seqNum);
            DPRINTF(IEW, "toNextCycle PC: %s.\n", npc);
            toNextCycle->diewc2diewc.mispredictInst = nullptr;

            // Must include the memory violator in the squash.
            // todo: note that this is not true in forward flow
            toNextCycle->diewc2diewc.includeSquashInst = false;

            wroteToTimeBuffer = true;
        }

        toNextCycle->diewc2diewc.halfSquash = true;
    }

}

template<class Impl>
void FFDIEWC<Impl>:: updateExeInstStats(DynInstPtr &inst)
{
    iewExecutedInsts++;

    if (inst->isControl()) {
        iewExecutedBranches++;
    }

    if (inst->isMemRef()) {
        iewExecutedRefs++;

        if (inst->isLoad()) {
            iewExecLoadInsts++;
        }
    }
}

template<class Impl>
void FFDIEWC<Impl>::setFanoutPred(FanoutPred *fanoutPred1)
{
    fanoutPred = fanoutPred1;
}

template<class Impl>
typename Impl::DynInstPtr
FFDIEWC<Impl>::insertForwarder(
        DynInstPtr &parent_inst, DynInstPtr &anchor)
{
    const RegId &lf_reg = parent_inst->staticInst->destRegIdx(0);

    StaticInstPtr static_forwarder = new ForwarderInst(lf_reg);

    TheISA::PCState forward_pc(anchor->pcState().npc());
    forward_pc.instNPC(anchor->pcState().npc());

    DynInstPtr forwarder = new DynInst(static_forwarder,
            nullptr, forward_pc, forward_pc,
            anchor->seqNum + (++anchor->numFollowingFw), cpu);

    forwarder->setTid(DummyTid);
    forwarder->setASID(DummyTid);
    forwarder->setThreadState(cpu->thread[DummyTid]);

    forwarder->setInstListIt(cpu->addInstAfter(forwarder,
                anchor->getInstListIt()));
    if (parent_inst->predLargeFanout) {
        forwarder->numForwardRest = (parent_inst->predFanout - 3) / 3;
        forwarder->ancestorPointer = parent_inst->dqPosition;
        forwarder->ancestorPointer.valid = true;
        forwarder->fwLevel = 0;
    } else {
        assert(parent_inst->isForwarder());
        DPRINTF(Reshape||Debug::RSProbe1, "Inserting secondary forwarder\n");
        forwarder->numForwardRest = parent_inst->numForwardRest - 1;
        forwarder->ancestorPointer = parent_inst->ancestorPointer;
        forwarder->fwLevel = parent_inst->fwLevel + 1;
    }

    DPRINTF(Reshape||Debug::RSProbe1, "Inserting forwarder inst[%llu] after inst[%llu]"
            "to forwarding value of inst[%llu], set ancestor to (%i) (%i %i)\n",
            forwarder->seqNum, anchor->seqNum, parent_inst->seqNum,
            forwarder->ancestorPointer.valid,
            forwarder->ancestorPointer.bank,
            forwarder->ancestorPointer.index);

    return forwarder;
}

template<class Impl>
void
FFDIEWC<Impl>::tryResetRef()
{
    if (dq.numInFlightFw() == 0) {
        resetOldestFw();
    } else if (Debug::DQV2 || Debug::FFCommit) {
        dq.dumpFwQSize();
    }
}

template<class Impl>
void
FFDIEWC<Impl>::setUpLoad(DynInstPtr &inst)
{
    MemPredHistory *hist = inst->memPredHistory;
    if (hist->bypass) {
        inst->seqNVul = inst->seqNum - hist->distPair.snDistance;
        // touch tssbf!
    } else {
        inst->seqNVul = getLastCompletedStoreSN();
    }
}

template<class Impl>
void
FFDIEWC<Impl>::setStoreCompleted(InstSeqNum sn)
{
    if (sn > lastCompletedStoreSN) {
        // todo: check why it is 0?
        lastCompletedStoreSN = sn;
    }
}

template<class Impl>
bool FFDIEWC<Impl>::checkViolation(FFDIEWC::DynInstPtr &inst)
{
    assert(inst->isLoad());
    if (inst->isNormalBypass()) {
        // compare bypassed value against dest
        if (inst->bypassVal.i != inst->getDestValue().i) {
            falsePositiveBypass++;
            return true;
        } else {
            inst->loadVerified = true;
        }
    } else {
        // compare old dest against new dest
        inst->loadVerifying = false;
        if (inst->speculativeLoadValue.i != inst->getDestValue().i) {
            DPRINTF(NoSQSMB, "Saved speculative value: %lu, newly loaded value: %lu\n",
                    inst->speculativeLoadValue.i, inst->getDestValue().i);
            falseNegativeBypass++;
            return true;
        } else {
            inst->loadVerified = true;
        }
    }
    DPRINTF(NoSQSMB, "No violation detected, mark inst as verified\n");
    return false;
}

}

#include "cpu/forwardflow/isa_specific.hh"

template class FF::FFDIEWC<FFCPUImpl>;
