//
// Created by zyy on 2020/1/15.
//

#include "dataflow_queue_bank.hh"
#include "debug/DQ.hh"
#include "debug/DQWake.hh"
#include "debug/DQWrite.hh"
#include "debug/FFExec.hh"
#include "debug/FFSquash.hh"
#include "debug/RSProbe1.hh"
#include "debug/Reshape.hh"

namespace FF {

using boost::dynamic_bitset;

template<class Impl>
void
DataflowQueueBank<Impl>::clear(bool markSquashed)
{
    for (auto &inst: instArray) {
        if (markSquashed && inst) {
            inst->setSquashed();
        }
        inst = nullptr;
    }
    nearlyWakeup.reset();
    readyQueue.clear();
}

template<class Impl>
void
DataflowQueueBank<Impl>::erase(DQPointer p, bool markSquashed)
{
    if (markSquashed && instArray[p.index]) {
        instArray[p.index]->setSquashed();
    }
    instArray[p.index] = nullptr;
    nearlyWakeup.reset(p.index);
}

template<class Impl>
void
DataflowQueueBank<Impl>::advanceTail()
{
    DPRINTF(FFSquash, "Tail before adv = %u\n", tail);
    instArray.at(tail) = nullptr;
    nearlyWakeup.reset(tail);
    tail = (tail + 1) % depth;
    DPRINTF(FFSquash, "Tail after adv = %u\n", tail);
}

template<class Impl>
typename Impl::DynInstPtr
DataflowQueueBank<Impl>::tryWakeTail()
{
    DynInstPtr tail_inst = instArray.at(tail);
    if (!tail_inst) {
        DPRINTF(DQWake, "Tail[%d] is null, skip\n", tail);
        return nullptr;
    }

    if (tail_inst->inReadyQueue || tail_inst->fuGranted ||
        (tail_inst->isForwarder() && tail_inst->isExecuted())) {
        DPRINTF(DQWake, "Tail[%d] has been granted, skip\n", tail);
        return nullptr;
    }
    for (unsigned op = 1; op < nOps; op++) {
        if (tail_inst->hasOp[op] && !tail_inst->opReady[op]) {
            DPRINTF(DQWake, "inst[%d] has op [%d] not ready and cannot execute\n",
                    tail_inst->seqNum, op);
            return nullptr;
        }
    }
    if (!tail_inst->memOpFulfilled() || !tail_inst->miscOpFulfilled() ||
        !tail_inst->orderFulfilled()) {
        DPRINTF(DQWake, "inst[%d] has mem/order/misc op not ready and cannot execute\n",
                tail_inst->seqNum);
        return nullptr;
    }
    DPRINTF(DQWake, "inst[%d] is ready but not waken up!,"
                    " and is waken up by head checker\n", tail_inst->seqNum);
    wakenUpAtTail++;
    return tail_inst;
}


template<class Impl>
typename Impl::DynInstPtr
DataflowQueueBank<Impl>::tryWakeDirect()
{
    while (!readyQueue.empty() && !readyQueue.front().valid) {
        readyQueue.pop_front();
    }
    if (readyQueue.empty()) {
        return nullptr;
    }
    const DQPointer &ptr = readyQueue.front();
    DynInstPtr inst = instArray.at(ptr.index);
    if (!inst) {
        DPRINTF(DQWake, "Inst at [%d] is null (squashed), skip\n", tail);
        readyQueue.pop_front();
        return nullptr;
    }
    if (inst->inReadyQueue || inst->fuGranted || (inst->isForwarder() && inst->isExecuted())) {
        DPRINTF(DQWake, "Inst at [%d] has been granted, skip\n", tail);
        readyQueue.pop_front();
        return nullptr;
    }
    DPRINTF(DQWake, "Ptr(%i) (%i %i) (%i) is in readyQueue\n",
            ptr.valid, ptr.bank, ptr.index, ptr.op);

    DPRINTF(DQWake, "Checking inst[%lu] which is supposed to be directly wakenup\n",
            inst->seqNum);

    for (unsigned op = 1; op < nOps; op++) {
        if (inst->hasOp[op] && !inst->opReady[op]) {
            panic("inst[%lu] has op [%d] not ready and cannot execute\n",
                  inst->seqNum, op);
        }
    }
    if (!inst->memOpFulfilled() || !inst->miscOpFulfilled() ||
        !inst->orderFulfilled()) {
        DPRINTF(DQWake, "inst[%d] has mem/order/misc op not ready and cannot execute\n",
                inst->seqNum);
        readyQueue.pop_front();
        return nullptr;
    }
    DPRINTF(DQWake||Debug::RSProbe1, "inst[%d] is ready but not waken up!,"
                                     " and is waken up by direct wake-up checker\n", inst->seqNum);
    readyQueue.pop_front();
    directWakenUp++;
    return inst;
}

template<class Impl>
typename Impl::DynInstPtr
DataflowQueueBank<Impl>::wakeupInstsFromBank()
{
    DynInstPtr first = nullptr;
    auto first_index = -1;
    auto first_op = 0;
    unsigned wakeup_count = 0;

    auto &pointers = anyPending ? pendingWakeupPointers : inputPointers;

    if (anyPending) {
        DPRINTF(DQWake, "Select pendingWakeupPointers\n");
    } else {
        DPRINTF(DQWake, "Select inputPointers\n");
    }

    std::array<bool, 4> need_pending_ptr;
    std::fill(need_pending_ptr.begin(), need_pending_ptr.end(), false);

    for (unsigned op = 0; op < nOps; op++) {
        auto &ptr = pointers[op];

        if (!ptr.valid) {
            DPRINTF(DQ, "skip pointer: (%i) (%i %i) (%i)\n",
                    ptr.valid, ptr.bank, ptr.index, ptr.op);
            continue;
        }
        DPRINTF(DQ, "pointer: (%i %i) (%i)\n", ptr.bank, ptr.index, ptr.op);

        if (!instArray.at(ptr.index) || instArray.at(ptr.index)->isSquashed()) {
            DPRINTF(DQWake, "Wakeup ignores null inst @%d\n", ptr.index);
            if (anyPending) {
                ptr.valid = false;
            }
            continue;
        }

        auto &inst = instArray[ptr.index];

        DPRINTF(DQWake||Debug::RSProbe1, "Pointer (%i %i) (%i) working on inst[%llu]\n",
                ptr.bank, ptr.index, ptr.op, inst->seqNum);

        if (inst->dqPosition.term != ptr.term) {
            DPRINTF(DQWake, "Term not match on WK: ptr: %d, inst: %d\n",
                    ptr.term, inst->dqPosition.term);
            if (anyPending) {
                ptr.valid = false;
            }
            continue;
        }

        if (op == 0 && !(ptr.wkType == WKPointer::WKMem ||
                         ptr.wkType == WKPointer::WKMisc || ptr.wkType == WKPointer::WKOrder)) {
            DPRINTF(DQWake, "Mark received dest after ont wakeup pointer to Dest\n");
            inst->receivedDest = true;
            if (anyPending) {
                ptr.valid = false;
            }
            continue;
        }

        bool handle_wakeup = false;

        if (ptr.wkType == WKPointer::WKMisc) {
            handle_wakeup = true;
            inst->miscDepReady = true;
            DPRINTF(DQWake, "Non spec inst [%llu] received wakeup pkt\n", inst->seqNum);

        } else if (ptr.wkType == WKPointer::WKMem) {
            inst->memDepReady = true;
            inst->queueingDelay += ptr.queueTime;
            wakeup_count++;
            DPRINTF(DQWake, "Mem inst [%llu] received wakeup pkt\n",
                    inst->seqNum);
            if (!inst->inReadyQueue && !inst->fuGranted) {
                first = inst;
            } else {
                DPRINTF(DQWake, "But ignore it because inst [%llu] has already been granted\n",
                        inst->seqNum);
            }

        } else if (ptr.wkType == WKPointer::WKOrder) {
            inst->orderDepReady = true;
            DPRINTF(DQWake, "Mem inst [%llu] (has order dep) received wakeup pkt\n",
                    inst->seqNum);
            if (!inst->inReadyQueue && !inst->fuGranted) {
                handle_wakeup = true;
            } else {
                DPRINTF(DQWake, "But ignore it because inst [%llu] has already been granted\n",
                        inst->seqNum);
            }

        } else { //  if (ptr.wkType == WKPointer::WKOp)
            handle_wakeup = true;
            inst->opReady[op] = true;
            if (op != 0) {
                assert(inst->indirectRegIndices.at(op).size());
                assert(ptr.hasVal);

                int src_reg_idx = inst->indirectRegIndices.at(op).front();
                FFRegValue v = ptr.val;

                for (const auto i: inst->indirectRegIndices.at(op)) {
                    DPRINTF(FFExec, "Setting src reg[%i] of inst[%llu] to %llu\n",
                            i, inst->seqNum, v.i);
                    inst->setSrcValue(i, v);
                }

                if (!inst->isForwarder() &&
                    ptr.reshapeOp >= 0 && ptr.reshapeOp <= 2 &&
                    (ptr.wkType == WKPointer::WKOp && !ptr.isFwExtra &&
                     !anyPending)) {
                    if (nearlyWakeup[ptr.index]) {

                        inst->gainFromReshape = ptr.fwLevel*2 + (ptr.reshapeOp + 1) - 2;
                        DynInstPtr parent = top->checkAndGetParent(
                                inst->getSrcPointer(src_reg_idx), inst->dqPosition);

                        if (parent) {
                            parent->reshapeContrib += inst->gainFromReshape;
                            DPRINTF(Reshape||Debug::RSProbe1, "inst[%llu] gain %i from reshape,"
                                                              " ancestor cummulative contrib: %i\n",
                                    inst->seqNum, inst->gainFromReshape, parent->reshapeContrib);
                        }
                    } else {
                        inst->nonCriticalFw += 1;
                        DPRINTF(Reshape||Debug::RSProbe1,
                                "inst[%llu] received non-critial fw %i from reshape\n",
                                inst->seqNum, inst->nonCriticalFw);

                        DynInstPtr parent = top->checkAndGetParent(
                                inst->getSrcPointer(src_reg_idx), inst->dqPosition);
                        if (parent) {
                            parent->negativeContrib += 1;
                        }
                    }
                }
            } else {
            }
            DPRINTF(DQWake||Debug::RSProbe1,
                    "Mark op[%d] of inst [%llu] ready\n", op, inst->seqNum);
        }

        if (handle_wakeup) {
            if (nearlyWakeup[ptr.index]) {
                assert(inst->numBusyOps() == 0);
                DPRINTF(DQWake, "inst [%llu] is ready to waken up\n", inst->seqNum);
                if (!inst->isForwarder()) {
                    wakeup_count++;
                    if (!first) {
                        if (ptr.wkType != WKPointer::WKOp || ptr.isFwExtra) {
                            inst->wkDelayedCycle = std::max((int) 0, ((int) ptr.queueTime) - 1);
                        } else {
                            inst->wkDelayedCycle = ptr.queueTime;
                        }
                        dq->countCycles(inst, &ptr);
                        DPRINTF(DQWake||Debug::RSProbe1,
                                "inst [%llu] is the gifted one in this bank\n",
                                inst->seqNum);
                        first = inst;
                        first_index = ptr.index;
                        if (anyPending) {
                            DPRINTF(DQWake, "Cleared pending pointer (%i %i) (%i)\n",
                                    ptr.bank, ptr.index, ptr.op);
                            ptr.valid = false;
                        }
                    } else {
                        DPRINTF(DQWake||Debug::RSProbe1,
                                "inst [%llu] has no luck in this bank\n", inst->seqNum);
                        need_pending_ptr[op] = true;
                    }
                } else {
                    inst->queueingDelay = ptr.queueTime;
                    inst->pendingDelay = ptr.pendingTime;

                    DPRINTF(DQWake, "inst [%llu] is forwarder and skipped\n",
                            inst->seqNum);
                    inst->setIssued();
                    inst->execute();
                    inst->setExecuted();
                    inst->setCanCommit();
                }
            } else {
                unsigned busy_count = inst->numBusyOps();
                if (busy_count == 1) {
                    DPRINTF(DQWake, "inst [%llu] becomes nearly waken up\n", inst->seqNum);
                    nearlyWakeup.set(ptr.index);
                }
                if (anyPending) {
                    DPRINTF(DQWake, "Cleared pending pointer (%i %i) (%i)\n",
                            ptr.bank, ptr.index, ptr.op);
                    ptr.valid = false;
                }
            }
        }
    }
    if (wakeup_count > 1) {
        for (unsigned op = 1; op < nOps; op++) {
            auto &ptr = pointers[op];
            if (ptr.index != first_index && need_pending_ptr[op]) {
                DPRINTF(DQWake, "Pointer (%i %i) (%i) becomes pending\n",
                        ptr.bank, ptr.index, ptr.op);
                pendingWakeupPointers[op] = ptr;
            }
        }
    }

    // if any pending then inputPointers are all invalid, vise versa (they are mutex)
    // todo ! this function must be called after readPointersFromBank(),
    //  because of the following lines
    pendingWakeupPointers[first_op].valid = false;
    inputPointers[first_op].valid = false;

    if (first) {
        wakenUpByPointer++;
    }

    if (wakeup_count == 0) {
        first = tryWakeTail();
        if (first) {
            for (auto it = readyQueue.begin(); it != readyQueue.end(); it++) {
                if (*it == first->dqPosition) {
                    DPRINTF(DQWake,
                            "Direct waken-up inst[%lu] is waken up by tail check\n",
                            first->seqNum);
                    readyQueue.erase(it);
                    break;
                }
            }
        }
    }

    if (wakeup_count == 0 && !first) {
        first = tryWakeDirect();
    } else {
        DPRINTF(DQWake, "No directReady!\n");
    }
    if (first) {
        DPRINTF(DQWake, "Wakeup valid inst: %llu\n", first->seqNum);
        DPRINTF(DQWake, "Setting pending inst[%llu]\n", first->seqNum);
    }

    return first;
}

template<class Impl>
std::vector<DQPointer>
DataflowQueueBank<Impl>::readPointersFromBank()
{
    DPRINTF(DQWake, "Reading pointers from banks\n");
    DPRINTF(DQ, "Tail: %i\n", tail);
    bool served_forwarder = false;

    bool grab_from_local_fw = dq->NarrowXBarWakeup && dq->NarrowLocalForward && anyPending;

    for (unsigned op = 0; op < nOps; op++) {
        if (served_forwarder) {
            break;
        }
        auto &ptr = grab_from_local_fw ? pendingWakeupPointers[op] : inputPointers[op];
        auto &optr = outputPointers[op];

        if (!ptr.valid) {
            optr.valid = false;

        } else if (grab_from_local_fw && !ptr.isLocal) {
            optr.valid = false;

        } else {
            DPRINTF(DQ, "op = %i\n", op);
            DPRINTF(DQWake, "Reading forward pointer according to (%i %i) (%i)\n",
                    ptr.bank, ptr.index, ptr.op);
            const auto &inst = instArray.at(ptr.index);
            if (!inst) {
                DPRINTF(FFSquash, "Ignore forwarded pointer to (%i %i) (%i) (squashed)\n",
                        ptr.bank, ptr.index, ptr.op);
                optr.valid = false;

            } else if (inst->dqPosition.term != ptr.term) {
                DPRINTF(DQWake, "Term not match on FW: ptr: %d, inst: %d\n",
                        ptr.term, inst->dqPosition.term);
                optr.valid = false;

            } else {
                if (op == 0) {
                    outputPointers[0].valid = false; // dest register should never be forwarded
                }

                if (inst->isForwarder() && op == inst->forwardOp) {
                    served_forwarder = true;
                    for (unsigned out_op = 0; out_op < nOps; out_op++) {
                        auto &out_ptr = outputPointers[out_op];
                        out_ptr = inst->pointers[out_op];
                        out_ptr.reshapeOp = out_op;
                        out_ptr.fwLevel = inst->fwLevel;
                        out_ptr.ssrDelay = ptr.ssrDelay + 1;

                        if (out_ptr.valid) {
                            DPRINTF(FFSquash||Debug::RSProbe1,
                                    "Put forwarding wakeup Pointer (%i %i) (%i)"
                                    " to outputPointers, reshapeOp: %i\n",
                                    out_ptr.bank, out_ptr.index, out_ptr.op, out_op);
                        }
                    }

                } else if (inst->pointers[op].valid) {
                    DPRINTF(DQ, "op now = %i\n", op);
                    if (op != 0 || inst->destReforward) {
                        optr = inst->pointers[op];

                        optr.hasVal = true;
                        if (op == 0 && inst->destReforward) {
                            DPRINTF(DQ, "Although its wakeup ptr to dest,"
                                        " it is still forwarded because of destReforward flag\n");
                            optr.val = inst->getDestValue();
                        } else {
                            optr.val = ptr.val;
                        }

                        optr.ssrDelay = ptr.ssrDelay + 1;
                        if (op == 0 && optr.valid && inst->destReforward) {
                            inst->destReforward = false;
                        }
                        if (grab_from_local_fw && ptr.isLocal) {
                            ptr.isLocal = false;
                            DPRINTF(DQWake, "Pointer (%i) (%i %i) (%i) isLocal <- false\n",
                                    optr.valid, optr.bank, optr.index, optr.op);
                        }
                        DPRINTF(FFSquash, "Put forwarding wakeup Pointer(%i) (%i %i) (%i)"
                                          " to outputPointers with val: (%i) (%llu)\n",
                                optr.valid, optr.bank, optr.index, optr.op, optr.hasVal, optr.val.i);
                    }
                } else {
                    DPRINTF(DQ, "Skip invalid pointer on op[%i]\n", op);
                    optr.valid = false;
                    if (grab_from_local_fw && ptr.isLocal) {
                        ptr.isLocal = false;
                        DPRINTF(DQWake, "Pointer (%i) (%i %i) (%i) isLocal <- false\n",
                                optr.valid, optr.bank, optr.index, optr.op);
                    }
                }
            }
        }
    }
    // dumpOutPointers();
    return outputPointers;
}

template<class Impl>
bool DataflowQueueBank<Impl>::canServeNew()
{
    auto flag = true;
    for (const auto &it: pendingWakeupPointers) {
        if (it.valid) {
            DPRINTF(DQWake, "Pending pointer (%u %u) (%u)\n",
                    it.bank, it.index, it.op);
            flag &= false;
        }
    }
    anyPending = !flag;

    flag &= !readyInstsQueue->isFull(); // it returns true if any queue is full

    flag &= !servedForwarder;
    if (!flag) {
        DPRINTF(DQWake, "DQ bank cannot serve new insts!\n");
    }
    return flag;
}

template<class Impl>
bool DataflowQueueBank<Impl>::wakeup(WKPointer pointer)
{
    auto &inst = instArray[pointer.index];
    if (!inst) {
        DPRINTF(DQWake, "Ignored squashed inst@(%i %i)\n", pointer.bank, pointer.index);
        return true;
    }

    if (inst->isForwarder() && (servedNonForwarder || servedForwarder) ) {
        return false;

    } else if (servedForwarder) { //not forwarder
        return false;
    }

    DPRINTF(DQWake, "Mark to wake up: (%i %i) (%i) in inputPointers\n",
            pointer.bank, pointer.index, pointer.op);

    auto op = pointer.op;
    assert(!inputPointers[op].valid);
    inputPointers[op] = pointer;

    if (inst && inst->isForwarder()) {
        servedForwarder = true;
    } else if (inst){
        servedNonForwarder = true;
    }

    return true;
}

template<class Impl>
void DataflowQueueBank<Impl>::checkPending()
{
}

template<class Impl>
DataflowQueueBank<Impl>::DataflowQueueBank(
        DerivFFCPUParams *params, unsigned bankID, DQ *dq)
        : dq(dq),
          nOps(params->numOperands),
          depth(params->DQDepth),
          nullDQPointer(DQPointer{false, 0, 0, 0, 0}),
          instArray(depth, nullptr),
          nearlyWakeup(dynamic_bitset<>(depth)),
          pendingWakeupPointers(nOps),
          localWKPointers(nOps),
          anyPending(false),
          inputPointers(nOps),
          outputPointers(nOps, nullDQPointer),
          tail(0),
          readyQueueSize(params->readyQueueSize),
          prematureFwPointers(depth)
{
    for (auto &line: prematureFwPointers) {
        for (auto &ptr: line) {
            ptr.valid = false;
        }
    }
    for (auto &ptr: localWKPointers) {
        ptr.valid = false;
    }
    std::ostringstream s;
    s << "DQBank" << bankID;
    _name = s.str();
}

template<class Impl>
typename DataflowQueueBank<Impl>::DynInstPtr
DataflowQueueBank<Impl>::readInstsFromBank(DQPointer pointer) const
{
//    DPRINTF(DQB, "pointer: %i, %i, %i\n",
//            pointer.bank, pointer.index, pointer.op);
    return instArray.at(pointer.index);
}

template<class Impl>
typename DataflowQueueBank<Impl>::DynInstPtr
DataflowQueueBank<Impl>::findInst(InstSeqNum num) const
{
    for (const auto &inst: instArray) {
        if (inst && inst->seqNum == num) {
            return inst;
        }
    }
    return nullptr;
}

template<class Impl>
void
DataflowQueueBank<Impl>::writeInstsToBank(
        DQPointer pointer, DataflowQueueBank::DynInstPtr &inst)
{
    DPRINTF(DQWrite, "insert inst[%llu] to (%d %d)\n",
            inst->seqNum, pointer.bank, pointer.index);
    auto index = pointer.index;
    assert(!instArray[index]);
    instArray[index] = inst;

    DPRINTF(DQWrite, "Tail before writing: %u\n", tail);
    if (!instArray[tail]) {
        tail = index;
        DPRINTF(DQWrite, "Tail becomes %u\n", tail);
    }
}

template<class Impl>
void DataflowQueueBank<Impl>::checkReadiness(DQPointer pointer)
{
    auto index = pointer.index;
    assert(instArray[index]);
    DynInstPtr &inst = instArray[index];

    unsigned busy_count = inst->numBusyOps();

    if (busy_count == 0) {
        DPRINTF(DQWake||Debug::RSProbe1,
                "inst [%llu] becomes ready on dispatch\n", inst->seqNum);
        directReady++;
        nearlyWakeup.set(index);
        readyQueue.push_back(pointer);
        DPRINTF(DQWake,
                "Push (%i) (%i %i) (%i) into ready queue\n",
                pointer.valid, pointer.bank, pointer.index, pointer.op);
    }
    if (busy_count == 1) {
        DPRINTF(DQWake, "inst [%llu] becomes nearly waken up\n", inst->seqNum);
        nearlyWakeup.set(index);
    }
}

template<class Impl>
void DataflowQueueBank<Impl>::resetState()
{
    for (auto &it: instArray) {
        it = nullptr;
    }
    nearlyWakeup.reset();
    cycleStart();
}

template<class Impl>
void DataflowQueueBank<Impl>::setTail(unsigned t)
{
    tail = t;
    DPRINTF(DQWrite, "Tail set to %u\n", tail);
}

template<class Impl>
void DataflowQueueBank<Impl>::cycleStart()
{
    extraWakeupPointer = nullDQPointer;
    for (auto &p: inputPointers) {
        p.valid = false;
    }
    for (auto &p: outputPointers) {
        p.valid = false;
    }
    servedForwarder = false;
    servedNonForwarder = false;
}

template<class Impl>
void DataflowQueueBank<Impl>::clearPending(DynInstPtr &inst)
{
    for (auto &q: readyInstsQueue->preScheduledQueues) {
        if (!q.empty() && inst->seqNum == q.front()->seqNum) {
            q.pop_front();
            return;
        }
    }
    if (!inst->isSquashed()) {
        panic("Clearing non-exsting inst[%lu]", inst->seqNum);
    }
}

template<class Impl>
void DataflowQueueBank<Impl>::printTail()
{
    DPRINTF(DQ, "D Tail = %u\n", tail);
}

template<class Impl>
void DataflowQueueBank<Impl>::regStats()
{
    wakenUpAtTail
            .name(name() + ".wakenUpAtTail")
            .desc("wakenUpAtTail");
    wakenUpByPointer
            .name(name() + ".wakenUpByPointer")
            .desc("wakenUpByPointer");
    directWakenUp
            .name(name() + ".directWakenUp")
            .desc("directWakenUp");
    directReady
            .name(name() + ".directReady")
            .desc("directReady");

}

template<class Impl>
bool DataflowQueueBank<Impl>::hasTooManyPendingInsts()
{
    return readyQueue.size() >= readyQueueSize;
}

template<class Impl>
void DataflowQueueBank<Impl>::squashReady(const DQPointer &squash_ptr)
{
    for (auto &ptr: readyQueue) {
        if (!top->validPosition(top->c.pointer2uint(ptr))) {
            ptr.valid = false;
            DPRINTF(FFSquash, "Squashed direct waking pointer to (%i %i)\n",
                    ptr.bank, ptr.index);

        } else if (top->logicallyLT(top->c.pointer2uint(squash_ptr), top->c.pointer2uint(ptr))) {
            ptr.valid = false;
            DPRINTF(FFSquash, "Squashed direct waking pointer to (%i %i)\n",
                    ptr.bank, ptr.index);
        }
    }

}

template<class Impl>
void DataflowQueueBank<Impl>::dumpOutPointers() const
{
    unsigned op = 0;
    for (const auto &ptr: outputPointers) {
        DPRINTF(DQ, "outputPointers[%i]: (%i) (%i %i) (%i)\n",
                op++, ptr.valid, ptr.bank, ptr.index, ptr.op);
    }
}

template<class Impl>
void DataflowQueueBank<Impl>::dumpInputPointers() const
{
    unsigned op = 0;
    for (const auto &ptr: inputPointers) {
        DPRINTF(DQ, "inputPointers[%i]: (%i) (%i %i) (%i)\n",
                op++, ptr.valid, ptr.bank, ptr.index, ptr.op);
    }
}

template<class Impl>
void DataflowQueueBank<Impl>::countUpPendingPointers()
{
    for (auto &ptr: pendingWakeupPointers) {
        if (ptr.valid) {
            ptr.pendingTime++;
        }
    }
}

template<class Impl>
void DataflowQueueBank<Impl>::countUpPendingInst()
{
    for (auto &q: readyInstsQueue->preScheduledQueues) {
        for (auto &inst: q) {
            inst->FUContentionDelay++;
        }
    }
}

template<class Impl>
void DataflowQueueBank<Impl>::mergeLocalWKPointers()
{
    for (unsigned op = 0; op < nOps; op++) {
        auto &p_ptr = pendingWakeupPointers[op];
        auto &l_ptr = localWKPointers[op];
        if (l_ptr.valid && !p_ptr.valid) {
            p_ptr = l_ptr;
            l_ptr.valid = false;
        }
    }
}

}

#include "cpu/forwardflow//isa_specific.hh"

template class FF::DataflowQueueBank<FFCPUImpl>;
