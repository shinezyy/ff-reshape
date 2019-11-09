//
// Created by zyy on 19-6-10.
//

#include <random>

#include "cpu/forwardflow/dataflow_queue.hh"
#include "debug/DQ.hh"
#include "debug/DQB.hh"  // DQ bank
#include "debug/DQOmega.hh"
#include "debug/DQRead.hh"  // DQ read
#include "debug/DQWake.hh"
#include "debug/DQWakeV1.hh"
#include "debug/DQWrite.hh"  // DQ write
#include "debug/FFCommit.hh"
#include "debug/FFExec.hh"
#include "debug/FFSquash.hh"
#include "debug/FUW.hh"
#include "debug/QClog.hh"
#include "debug/RSProbe1.hh"
#include "debug/RSProbe2.hh"
#include "debug/Reshape.hh"
#include "params/DerivFFCPU.hh"
#include "params/FFFUPool.hh"

namespace FF {

using namespace std;

using boost::dynamic_bitset;

template<class Impl>
ReadyInstsQueue<Impl>::ReadyInstsQueue(DerivFFCPUParams *params)
:   maxReadyQueueSize(params->MaxReadyQueueSize),
    preScheduledQueues(nOpGroups)
{
}

template<class Impl>
void
ReadyInstsQueue<Impl>::squash(InstSeqNum seq)
{
    for (auto &q: preScheduledQueues) {
        const auto end = q.end();
        auto it = q.begin();
        while (it != end) {
            if ((*it)->seqNum > seq) {
                it = q.erase(it);
            } else {
                it++;
            }
        }
    }
}

template<class Impl>
typename Impl::DynInstPtr
ReadyInstsQueue<Impl>::getInst(OpGroups group)
{
    assert(group < preScheduledQueues.size());
    if (!preScheduledQueues[group].empty()) {
        return preScheduledQueues[group].front();
    } else {
        return nullptr;
    }
}

template<class Impl>
void
ReadyInstsQueue<Impl>::insertInst(OpGroups group, DynInstPtr &inst)
{
    preScheduledQueues[group].push_back(inst);
}

template<class Impl>
void
ReadyInstsQueue<Impl>::insertEmpirically(DynInstPtr &inst)
{
    if (preScheduledQueues[OpGroups::MultDiv].size() <
            preScheduledQueues[OpGroups::FPAdd].size()) {
        preScheduledQueues[OpGroups::MultDiv].push_back(inst);
        DPRINTF(DQWake, "Inst[%lu] inserted into MD queue empirically\n", inst->seqNum);

    } else {
        preScheduledQueues[OpGroups::FPAdd].push_back(inst);
        DPRINTF(DQWake, "Inst[%lu] inserted into FPAdd queue empirically\n", inst->seqNum);
    }
}

template<class Impl>
bool
ReadyInstsQueue<Impl>::isFull()
{
    return isFull(OpGroups::FPAdd) || isFull(OpGroups::MultDiv);
}

template<class Impl>
bool
ReadyInstsQueue<Impl>::isFull(OpGroups group)
{
    assert(group < preScheduledQueues.size());
    return preScheduledQueues[group].size() >= maxReadyQueueSize;
}

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
                        DynInstPtr parent = dq->checkAndGetParent(
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

                        DynInstPtr parent = dq->checkAndGetParent(
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
    for (unsigned op = 0; op < nOps; op++) {
        if (served_forwarder) {
            break;
        }
        const auto &ptr = inputPointers[op];
        auto &optr = outputPointers[op];

        if (!ptr.valid) {
            optr.valid = false;
        } else {
            DPRINTF(DQ, "op = %i\n", op);
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
                        DPRINTF(FFSquash, "Put forwarding wakeup Pointer(%i) (%i %i) (%i)"
                                " to outputPointers with val: (%i) (%llu)\n",
                                optr.valid, optr.bank, optr.index, optr.op, optr.hasVal, optr.val.i);
                    }
                } else {
                    DPRINTF(DQ, "Skip invalid pointer on op[%i]\n", op);
                    optr.valid = false;
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

    DPRINTF(DQWake, "Mark waking up: (%i %i) (%i) in inputPointers\n",
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
void DataflowQueueBank<Impl>::squash(const DQPointer &squash_ptr)
{
    for (auto &ptr: readyQueue) {
        if (!dq->validPosition(dq->pointer2uint(ptr))) {
            ptr.valid = false;
            DPRINTF(FFSquash, "Squashed direct waking pointer to (%i %i)\n",
                    ptr.bank, ptr.index);

        } else if (dq->logicallyLT(dq->pointer2uint(squash_ptr), dq->pointer2uint(ptr))) {
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
void DataflowQueues<Impl>::tick()
{
    // fu preparation
    for (auto &wrapper: fuWrappers) {
        wrapper.startCycle();
    }
    readPointersToWkQ();

    readQueueHeads();

    digestForwardPointer();

    diewc->tryResetRef();

    DPRINTF(DQ, "Setup fu requests\n");
    // For each bank
    //  get ready instructions produced by last tick from time struct
    for (unsigned i = 0; i < nBanks*OpGroups::nOpGroups; i++) {

        fu_requests[i].valid = fromLastCycle->instValids[i];
        if (fromLastCycle->instValids[i]) {

            DPRINTF(DQ, "inst[%d] valid, &inst: %lu\n",
                    i, fromLastCycle->insts[i]->seqNum);

            fu_requests[i].payload = fromLastCycle->insts[i];
            std::tie(fu_requests[i].dest, fu_requests[i].destBits) =
                coordinateFU(fromLastCycle->insts[i], i);
        }
    }
    shuffleNeighbors();
    // For each bank

    //  For each valid ready instruction compete for a FU via the omega network
    //  FU should ensure that this is no write port hazard next cycle
    //      If FU grant an inst, pass its direct child pointer to this FU's corresponding Pointer Queue
    //      to wake up the child one cycle before its value arrived

    DPRINTF(DQ, "FU selecting ready insts from banks\n");
    DPRINTF(DQWake, "FU req packets:\n");
    dumpInstPackets(fu_req_ptrs);
    fu_granted_ptrs = bankFUXBar.select(fu_req_ptrs, &nullInstPkt, nFUGroups);
    for (unsigned b = 0; b < nBanks; b++) {

        assert(fu_granted_ptrs[b]);
        if (!fu_granted_ptrs[b]->valid || !fu_granted_ptrs[b]->payload) {
            continue;
        }
        DynInstPtr &inst = fu_granted_ptrs[b]->payload;

        DPRINTF(DQWake, "Inst[%d] selected by fu%u\n", inst->seqNum, b);
        unsigned source_bank = fu_granted_ptrs[b]->source/2;

        if (inst->isSquashed()) {
            DPRINTF(FFSquash, "Skip squashed inst[%llu]\n", inst->seqNum);
            dqs[source_bank]->clearPending(inst);

        } else {
            InstSeqNum waitee;
            bool can_accept = fuWrappers[b].canServe(inst, waitee);
            if (waitee && waitee > inst->seqNum) {
                oldWaitYoung++;
            }
            if (can_accept) {
                DPRINTF(DQWake, "Inst[%d] accepted\n", inst->seqNum);
                fuWrappers[b].consume(inst);
                dqs[source_bank]->clearPending(inst);

            } else if (opLat[inst->opClass()] > 1){
                readyWaitTime[b] += 1;
                llBlockedNext = true;
            }
        }
    }

    DPRINTF(DQ, "Tell dq banks who was granted\n");
    // mark it for banks
    for (unsigned b = 0; b < nBanks; b++) {
        dqs[b]->countUpPendingInst();
    }

    // For each bank, check
    //  whether there's an inst to be waken up
    //  whether there are multiple instruction to wake up, if so
    //      buffer it and reject further requests in following cycles
    DPRINTF(DQ, "Bank check pending\n");
    for (auto bank: dqs) {
        bank->checkPending();
    }

    replayMemInsts();

    DPRINTF(DQ, "Selecting wakeup pointers from requests\n");
    // For each bank: check status: whether I can consume from queue this cycle?
    // record the state
    genFUValidMask();

    if (MINWakeup) {
        wakeup_granted_ptrs = wakeupQueueBankMIN.select(wakeup_req_ptrs);

    } else if (XBarWakeup) {
        wakeup_granted_ptrs = wakeupQueueBankXBar.select(wakeup_req_ptrs, &nullWKPkt);

    } else if (NarrowXBarWakeup) {
        wakeup_granted_ptrs = wakeupQueueBankNarrowXBar.select(wakeup_req_ptrs, &nullWKPkt);

    } else if (DediXBarWakeup) {
        wakeup_granted_ptrs = wakeupQueueBankDediXBar.select(wakeup_req_ptrs, &nullWKPkt);

    } else {
        panic("Unknown topology\n");
    }

    for (auto &ptr : wakeup_granted_ptrs) {
        if (ptr->valid) {
            DPRINTF(DQWake||Debug::RSProbe2,
                    "WakePtr[%d] (pointer(%d) (%d %d) (%d)) granted\n",
                    ptr->source, ptr->payload.valid,
                    ptr->payload.bank, ptr->payload.index, ptr->payload.op);
        }
    }

    assert(opPrioList.size() == 4);
    // check whether each bank can really accept
    unsigned wk_pkt_passed = 0;
    for (unsigned b = 0; b < nBanks; b++) {
        for (unsigned op: opPrioList) {
            if (dqs[b]->canServeNew()) {
                const auto &pkt = wakeup_granted_ptrs[b * nOps + op];
                if (!pkt->valid) {
                    continue;
                }
                DPRINTF(DQWake, "granted[%i.%i]: dest:(%i) (%i %i) (%i)\n",
                        b, op, pkt->valid,
                        pkt->payload.bank, pkt->payload.index, pkt->payload.op);
                DPRINTF(DQWake, "granted[%i.%i]: dest:(%llu) (%i) (%i %i) (%i)\n",
                        b, op, pkt->destBits.to_ulong(),
                        pkt->valid,
                        pkt->payload.bank, pkt->payload.index, pkt->payload.op);

                WKPointer &ptr = pkt->payload;
                if (ptr.wkType == WKPointer::WKOp) {
                    if (ptr.op == 0) {
                        SrcOpPackets++;
                    } else {
                        DestOpPackets++;
                    }

                } else if (ptr.wkType == WKPointer::WKMem) {
                    MemPackets++;

                } else if (ptr.wkType == WKPointer::WKOrder) {
                    OrderPackets++;

                } else {
                    assert(ptr.wkType == WKPointer::WKMisc);
                    MiscPackets++;
                }

                if (dqs[b]->wakeup(ptr)) {

                    wake_req_granted[pkt->source] = true;
                    // pop accepted pointers.
                    assert(!wakeQueues[pkt->source].empty());
                    if (wakeQueues[pkt->source].size() == numPendingWakeupMax) {
                        numPendingWakeupMax--;
                    }
                    DPRINTF(DQWake || RSProbe2, "Pop WakePtr (%i %i) (%i)from wakequeue[%u]\n",
                            ptr.bank, ptr.index, ptr.op, pkt->source);
                    wakeQueues[pkt->source].pop_front();
                    numPendingWakeups--;
                    wk_pkt_passed++;
                    DPRINTF(DQWake, "After pop, numPendingWakeups = %u\n", numPendingWakeups);

                } else {
                    DPRINTF(Reshape, "Skipped because conflict to/by forwarder\n");
                }
            }
        }
    }
    if (wk_pkt_passed != 0) {
        assert(wk_pkt_passed <= nOps * nBanks);
        WKFlowUsage[wk_pkt_passed]++;
    }

    if (Debug::QClog) {
        dumpQueues();
    }

    rearrangePrioList();

    countUpPointers();

    tryResetRef();

    // todo: write forward pointers from bank to time buffer!
    for (unsigned b = 0; b < nBanks; b++) {
        std::vector<DQPointer> forward_ptrs = dqs[b]->readPointersFromBank();
        for (unsigned op = 0; op < nOps; op++) {
            const auto &ptr = forward_ptrs[op];
            // dqs[b]->dumpOutPointers();
            DPRINTF(DQ, "Putting wk ptr (%i) (%i %i) (%i) to time buffer\n",
                    ptr.valid, ptr.bank, ptr.index, ptr.op);
            toNextCycle->pointers[b * nOps + op] = forward_ptrs[op];
        }
    }

    DPRINTF(DQ, "Wrappers setting instructions to be waken up\n");
    for (auto &wrapper: fuWrappers) {
        wrapper.setWakeup();
    }

    for (auto &wrapper: fuWrappers) {
        wrapper.executeInsts();
    }

    for (unsigned b = 0; b < nBanks; b++) {
        const DQPointer &src = fuWrappers[b].toWakeup[SrcPtr];
        const DQPointer &dest = fuWrappers[b].toWakeup[DestPtr];

        int q1 = -1, q2 = -1;
        if (dest.valid) {
            q1 = allocateWakeQ();
            DPRINTF(DQWake || Debug::RSProbe2,
                    "Got wakeup pointer to (%d %d) (%d), pushed to wakeQueue[%i]\n",
                    dest.bank, dest.index, dest.op, q1);
            pushToWakeQueue(q1, WKPointer(dest));
            numPendingWakeups++;
            DPRINTF(DQWake, "After push, numPendingWakeups = %u\n", numPendingWakeups);

        }

        if (src.valid) {
            q2 = allocateWakeQ();
            DPRINTF(DQWake || Debug::RSProbe2,
                    "Got inverse wakeup pointer to (%d %d) (%d),"
                    " pushed to wakeQueue[%i]\n",
                    src.bank, src.index, src.op, q2);
            pushToWakeQueue(q2, WKPointer(src));
            numPendingWakeups++;
            DPRINTF(DQWake, "After push, numPendingWakeups = %u\n", numPendingWakeups);

        }

        if ((q1 >= 0 && wakeQueues[q1].size() > maxQueueDepth) ||
                (q2 >= 0 && wakeQueues[q2].size() > maxQueueDepth)) {
            processWKQueueFull();
        }
    }

    // todo: write insts from bank to time buffer!
    for (unsigned i = 0; i < nBanks; i++) {
        DynInstPtr inst;
        // todo ! this function must be called after readPointersFromBank(),
        inst = dqs[i]->wakeupInstsFromBank();

        auto &ready_insts_queue = readyInstsQueues[i];
        if (!inst) {
            DPRINTF(DQWake, "No inst from bank %i this cycle\n", i);

        } else {
            DPRINTF(DQWake, "Pushing valid inst[%lu]\n", inst->seqNum);
            inst->inReadyQueue = true;
            // 应该在wakeupInstsFromBank函数中插入下面的队列，而不是这里
            if (matchInGroup(inst->opClass(), OpGroups::MultDiv)) {
                ready_insts_queue->insertInst(OpGroups::MultDiv, inst);
                DPRINTF(DQWake, "Inst[%lu] inserted into MD queue\n", inst->seqNum);

            } else if (matchInGroup(inst->opClass(), OpGroups::FPAdd)) {
                ready_insts_queue->insertInst(OpGroups::FPAdd, inst);
                DPRINTF(DQWake, "Inst[%lu] inserted into FPadd queue\n", inst->seqNum);

            } else {
                ready_insts_queue->insertEmpirically(inst);
            }
        }

        DynInstPtr md_group = ready_insts_queue->getInst(OpGroups::MultDiv);
        DynInstPtr fp_add_group = ready_insts_queue->getInst(OpGroups::FPAdd);

        toNextCycle->instValids[2*i] = !!md_group && !md_group->fuGranted;
        toNextCycle->insts[2*i] = md_group;

        toNextCycle->instValids[2*i + 1] = !!fp_add_group && !fp_add_group->fuGranted;
        toNextCycle->insts[2*i + 1] = fp_add_group;
        if (!toNextCycle->instValids[2*i + 1]) {
            DPRINTF(DQWake, "Inst from FP add valid: %i\n", fp_add_group);
            if (fp_add_group) {
                DPRINTF(DQWake, "Inst[%lu] fu granted: %i\n", fp_add_group->seqNum,
                        fp_add_group->fuGranted);
            }
        }

        if (toNextCycle->instValids[2*i]) {
            DPRINTF(DQWake, "toNext cycle inst[%lu]\n", toNextCycle->insts[2*i]->seqNum);
        }

        if (toNextCycle->instValids[2*i + 1]) {
            DPRINTF(DQWake, "toNext cycle inst[%lu]\n", toNextCycle->insts[2*i + 1]->seqNum);
        }
    }
    for (unsigned i = 0; i < nBanks; i++) {
        dqs[i]->countUpPendingPointers();
    }

}

template<class Impl>
void DataflowQueues<Impl>::cycleStart()
{
//    fill(wakenValids.begin(), wakenValids.end(), false);
    fill(wake_req_granted.begin(), wake_req_granted.end(), false);
    for (auto bank: dqs) {
        bank->cycleStart();
    }
    llBlocked = llBlockedNext;
    llBlockedNext = false;
    if (llBlocked) {
        DPRINTF(FUW, "ll blocked last cycle\n");
    }
    halfSquash = false;
    halfSquashSeq = 0;
}

template<class Impl>
DataflowQueues<Impl>::DataflowQueues(DerivFFCPUParams *params)
        :
        nullDQPointer{false, 0, 0, 0, 0},
        nullWKPointer(WKPointer()),
        WritePorts(1),
        ReadPorts(1),
        writes(0),
        reads(0),
        nBanks(params->numDQBanks),
        nOps(params->numOperands),
        nFUGroups(params->numDQBanks),
        depth(params->DQDepth),
        queueSize(nBanks * depth),
        wakeQueues(nBanks * nOps),
        forwardPointerQueue(nBanks * nOps),
//        wakenValids(nBanks, false),
//        wakenInsts(nBanks, nullptr),
        bankFUMIN(nBanks, true),
        wakeupQueueBankMIN(nBanks * nOps, true),
        pointerQueueBankMIN(nBanks * nOps, true),

        bankFUXBar(nBanks * OpGroups::nOpGroups),
        wakeupQueueBankXBar(nBanks * nOps),
        pointerQueueBankXBar(nBanks * nOps),

        wakeupQueueBankNarrowXBar(nBanks * nOps, nOps),

        wakeupQueueBankDediXBar(nBanks * nOps, nOps),

        fu_requests(nBanks * OpGroups::nOpGroups),
        fu_req_ptrs(nBanks * OpGroups::nOpGroups),
        fu_granted_ptrs(nBanks),

        wakeup_requests(nBanks * nOps),
        wake_req_granted(nBanks * nOps),
        wakeup_req_ptrs(nBanks * nOps),
        wakeup_granted_ptrs(nBanks * nOps),

        insert_requests(nBanks * nOps),
        insert_req_granted(nBanks * nOps),
        insert_req_ptrs(nBanks * nOps),
        insert_granted_ptrs(nBanks * nOps),

        fuWrappers(nBanks), // todo: fix it
        fuPool(params->fuPool),
        fuGroupCaps(Num_OpClasses, vector<bool>(nBanks)),


        bankWidth((unsigned) ceilLog2(params->numDQBanks)),
        bankMask((unsigned) (1 << bankWidth) - 1),
        indexWidth((unsigned) ceilLog2(params->DQDepth)),
        indexMask((unsigned) ((1 << indexWidth) - 1) << bankWidth),

        extraWKPtr(0),

        maxQueueDepth(params->pendingQueueDepth),
        numPendingWakeups(0),
        numPendingWakeupMax(0),

        PendingWakeupThreshold(params->PendingWakeupThreshold),
        PendingWakeupMaxThreshold(params->PendingWakeupMaxThreshold),

        numPendingFwPointers(0),
        numPendingFwPointerMax(0),
        PendingFwPointerThreshold(params->PendingFwPointerThreshold),
        opPrioList{1, 2, 3, 0},
        gen(0xdeadbeef),
        randAllocator(0, nBanks * nOps),
        tempWakeQueues(nBanks * nOps),
        headTerm(0),
        termMax(params->TermMax),
        MINWakeup(params->MINWakeup),
        XBarWakeup(params->XBarWakeup),
        NarrowXBarWakeup(params->NarrowXBarWakeup),
        DediXBarWakeup(params->DediXBarWakeup),
        AgedWakeQueuePush(params->AgedWakeQueuePush),
        AgedWakeQueuePktSel(params->AgedWakeQueuePktSel),
        pushFF(nBanks * nOps, false)
{
    assert(MINWakeup + XBarWakeup + NarrowXBarWakeup + DediXBarWakeup == 1);
    // init inputs to omega networks
    for (unsigned b = 0; b < nBanks; b++) {
        for (unsigned op = 0; op < nBanks; op++) {
            unsigned index = b * nOps + op;
            DQPacket<WKPointer> &dq_pkt = wakeup_requests[index];
            dq_pkt.valid = false;
            dq_pkt.source = index;
            wakeup_req_ptrs[index] = &wakeup_requests[index];

            DQPacket<PointerPair> &pair_pkt = insert_requests[index];
            pair_pkt.valid = false;
            pair_pkt.source = index;
            insert_req_ptrs[index] = &insert_requests[index];
        }
        fuWrappers[b].init(fuPool->fw, b);
        // initiate fu bit map here
        fuWrappers[b].fillMyBitMap(fuGroupCaps, b);
        fuWrappers[b].fillLatTable(opLat);
        fuWrappers[b].setDQ(this);

        dqs.emplace_back(new XDataflowQueueBank(params, b, this));
        readyInstsQueues.emplace_back(new XReadyInstsQueue(params));
        dqs[b]->readyInstsQueue = readyInstsQueues[b];
    }

    for (unsigned i = 0; i < nBanks * OpGroups::nOpGroups; i++) {
        DQPacket<DynInstPtr> &fu_pkt = fu_requests[i];
        fu_pkt.valid = false;
        fu_pkt.source = i;

        fu_req_ptrs[i] = &fu_requests[i];
    }
    fuPointer[OpGroups::MultDiv] = std::vector<unsigned>{0, 0, 2, 2};
    fuPointer[OpGroups::FPAdd] = std::vector<unsigned>{1, 1, 3, 3};

    memDepUnit.init(params, DummyTid);
    memDepUnit.setIQ(this);
    resetState();

    nullWKPkt.valid = false;
    nullWKPkt.destBits = boost::dynamic_bitset<>(ceilLog2(nOps * nBanks) + 1);

    nullFWPkt.valid = false;
    nullFWPkt.destBits = boost::dynamic_bitset<>(ceilLog2(nOps * nBanks) + 1);

    nullInstPkt.valid = false;
    nullInstPkt.destBits = boost::dynamic_bitset<>(ceilLog2(nBanks) + 1);
}

template<class Impl>
boost::dynamic_bitset<>
DataflowQueues<Impl>::uint2Bits(unsigned from)
{
    auto res = boost::dynamic_bitset<>(depth);
    for (unsigned i = 0; i < depth; i++, from >>= 1) {
        res[i] = from & 1;
    }
    return res;
}

template<class Impl>
DQPointer
DataflowQueues<Impl>::uint2Pointer(unsigned u) const
{
    unsigned bank = u & bankMask;
    unsigned index = (u & indexMask) >> bankWidth;
    unsigned group = 0; //todo group is not supported yet
    return DQPointer{true, group, bank, index, 0};
}


template<class Impl>
unsigned DataflowQueues<Impl>::pointer2uint(const DQPointer &ptr) const
{
    return (ptr.index << bankWidth) | ptr.bank;
}
template<class Impl>
unsigned DataflowQueues<Impl>::pointer2uint(const WKPointer &ptr) const
{
    return (ptr.index << bankWidth) | ptr.bank;
}

template<class Impl>
std::pair<unsigned, boost::dynamic_bitset<> >
DataflowQueues<Impl>::coordinateFU(
        DataflowQueues::DynInstPtr &inst, unsigned from)
{
    // find another FU group with desired capability.
    DPRINTF(FUW, "Coordinating req %u.%u  with llb:%i\n", from/2, from%2, llBlocked);
    // if (llBlocked) {
    //     auto fu_bitmap = fuGroupCaps[inst->opClass()];
    //     do {
    //         fuPointer[from] = (fuPointer[from] + 1) % nBanks;
    //     } while (!fu_bitmap[fuPointer[from]]);
    //     DPRINTF(FUW, "switch to req fu %u\n", fuPointer[from]);

    // } else if (opLat[inst->opClass()] == 1) {
    //     fuPointer[from] = from;
    //     DPRINTF(FUW, "reset to origin\n");
    // }

    int type = from%2,
        bank = from/2;
    int md = OpGroups::MultDiv,
        fa = OpGroups::FPAdd;
    const int group_ipr = 3;
    if (inst->opClass() == OpClass::IprAccess) {
        DPRINTF(FUW, "Routing inst[%lu] to %i\n", inst->seqNum, group_ipr);
        return std::make_pair(group_ipr, uint2Bits(group_ipr));
    } else if (type == md){ //OpGroups::MultDiv
        DPRINTF(FUW, "Routing inst[%lu] to %i\n", inst->seqNum, fuPointer[md][bank]);
        return std::make_pair(fuPointer[md][bank],
                uint2Bits(fuPointer[md][bank]));
    }
    //else (type == fa){ //OpGroups::FPadd
    DPRINTF(FUW, "Routing inst[%lu] to %i\n", inst->seqNum, fuPointer[fa][bank]);
    return std::make_pair(fuPointer[fa][bank],
            uint2Bits(fuPointer[fa][bank]));
}

template<class Impl>
void DataflowQueues<Impl>::insertForwardPointer(PointerPair pair)
{
    assert(forwardPtrIndex < nBanks * nOps);

    DQPacket<PointerPair> pkt;
    if (pair.dest.valid) {
        pkt.valid = pair.dest.valid;
        pkt.source = forwardPtrIndex;
        pkt.payload = pair;
        unsigned d = pair.dest.bank * nOps + pair.dest.op;
        pkt.dest = d;
        pkt.destBits = uint2Bits(d);

        DPRINTF(DQWake, "Insert Fw Pointer (%i %i) (%i)-> (%i %i) (%i)\n",
                pair.dest.bank, pair.dest.index, pair.dest.op,
                pair.payload.bank, pair.payload.index, pair.payload.op);

        forwardPointerQueue[forwardPtrIndex].push_back(pkt);

        if (forwardPointerQueue[forwardPtrIndex].size() > maxQueueDepth) {
            processFWQueueFull();
        }

        numPendingFwPointers++;
        DPRINTF(DQWake, "numPendingFwPointers: %i\n", numPendingFwPointers);
        if (forwardPointerQueue[forwardPtrIndex].size() > numPendingFwPointerMax) {
            numPendingFwPointerMax = static_cast<unsigned int>(
                    forwardPointerQueue[forwardPtrIndex].size());
        }
        forwardPtrIndex = (forwardPtrIndex + 1) % (nBanks * nOps);
    }
}

template<class Impl>
void DataflowQueues<Impl>::retireHead(bool result_valid, FFRegValue v)
{
    assert(!isEmpty());
    DQPointer head_ptr = uint2Pointer(tail);
    alignTails();
    DPRINTF(FFCommit, "Position of inst to commit:(%d %d)\n",
            head_ptr.bank, head_ptr.index);
    DynInstPtr head_inst = dqs[head_ptr.bank]->readInstsFromBank(head_ptr);

    assert (head_inst);

    cpu->removeFrontInst(head_inst);
    if (result_valid) {
        committedValues[head_inst->dqPosition] = v;
    } else {
        // this instruciton produce not value,
        // and previous owner's children show never read from here!.
        committedValues.erase(head_inst->dqPosition);
    }
    head_inst->clearInDQ();
    DPRINTF(FFCommit, "head inst sn: %llu\n", head_inst->seqNum);
    DPRINTF(FFCommit, "head inst pc: %s\n", head_inst->pcState());
    dqs[head_ptr.bank]->advanceTail();
    if (head != tail) {
        tail = inc(tail);
        DPRINTF(DQ, "tail becomes %u in retiring\n", tail);
    }

    DPRINTF(FFCommit, "Advance youngest ptr to %d, olddest ptr to %d\n", head, tail);
}

template<class Impl>
bool DataflowQueues<Impl>::isFull() const
{
    bool res = head == queueSize - 1 ? tail == 0 : head == tail - 1;
    if (res) {
        DPRINTF(DQ, "DQ is full head = %d, tail = %d\n", head, tail);
    }
    return res;
}

template<class Impl>
bool DataflowQueues<Impl>::isEmpty() const
{
    DPRINTF(DQ, "head: %u, tail: %u\n", head, tail);
    return head == tail && !getHead();
}


template<class Impl>
typename DataflowQueues<Impl>::DynInstPtr
DataflowQueues<Impl>::getHead() const
{
    auto head_ptr = uint2Pointer(head);
    const XDataflowQueueBank *bank = dqs[head_ptr.bank];
    const auto &inst = bank->readInstsFromBank(head_ptr);
    return inst;
}

template<class Impl>
typename DataflowQueues<Impl>::DynInstPtr
DataflowQueues<Impl>::getTail()
{
    auto head_ptr = uint2Pointer(tail);
    XDataflowQueueBank *bank = dqs[head_ptr.bank];
    const auto &inst = bank->readInstsFromBank(head_ptr);
    return inst;
}

template<class Impl>
bool DataflowQueues<Impl>::stallToUnclog() const
{
    auto x1 = isFull();
    auto x2 = wakeupQueueClogging();
    auto x3 = fwPointerQueueClogging();
    return x1 || x2 || x3;
}

template<class Impl>
typename DataflowQueues<Impl>::DynInstPtr
DataflowQueues<Impl>::findInst(InstSeqNum num) const
{
    unsigned count = 0;
    DynInstPtr inst = nullptr;
    for (const auto &bank: dqs) {
        if (DynInstPtr tmp = bank->findInst(num)) {
            count += 1;
            inst = tmp;
        }
    }
    assert(count == 1);
    return inst;
}

template<class Impl>
bool DataflowQueues<Impl>::wakeupQueueClogging() const
{
    bool res = numPendingWakeups >= PendingWakeupThreshold ||
           numPendingWakeupMax >= PendingWakeupMaxThreshold;
    if (res) {
        DPRINTF(DQ, "pending wakeup = %d, threshold = %d"
                    "pending wakeup (single queue) = %d, threshold = %d\n",
                    numPendingWakeups, PendingWakeupThreshold,
                    numPendingWakeupMax, PendingWakeupMaxThreshold);
    }
    return res;
}

template<class Impl>
bool DataflowQueues<Impl>::fwPointerQueueClogging() const
{
    bool res = numPendingFwPointers >= PendingFwPointerThreshold;
    if (res) {
        DPRINTF(DQ, "pending fw ptr = %d, threshold = %d\n",
                numPendingFwPointers, PendingFwPointerThreshold);
//        for (const auto &q: forwardPointerQueue) {
//            DPRINTF(DQ, "fw ptr queue size: %i\n", q.size());
//        }
    }
    return res;
}

template<class Impl>
FFRegValue DataflowQueues<Impl>::readReg(const DQPointer &src, const DQPointer &dest)
{
    auto b = src.bank;
    FFRegValue result;

    bool readFromCommitted;

    if (!validPosition(pointer2uint(src))) {
        // src is committed
        readFromCommitted = true;

    } else if (logicallyLT(pointer2uint(src), pointer2uint(dest))) {
        // src is not committed yet
        readFromCommitted = false;

    } else {
        // src is committed and newer inst has occupied its position
        readFromCommitted = true;
    }

    if (!readFromCommitted) {
        auto inst = dqs[b]->readInstsFromBank(src);
        assert(inst);
        DPRINTF(FFExec, "Reading value: %llu from inst[%llu]\n",
                result.i, inst->seqNum);
        assert(inst->isExecuted() || inst->isForwarder());
        // not committed yet
        result = inst->getDestValue();
    } else {
        if (!committedValues.count(src)) {
            DPRINTF(FFExec, "Reading uncommitted pointer (%i %i) (%i)!\n",
                    src.bank, src.index, src.op);
            assert(committedValues.count(src));
        }
        DPRINTF(FFExec, "Reading value: %llu from committed entry: (%i %i) (%i)\n",
                result.i, src.bank, src.index, src.op);
        result = committedValues[src];
    }
    return result;
}

template<class Impl>
void DataflowQueues<Impl>::setReg(DQPointer ptr, FFRegValue val)
{
    regFile[pointer2uint(ptr)] = val;
}

template<class Impl>
void DataflowQueues<Impl>::addReadyMemInst(DynInstPtr inst, bool isOrderDep)
{
    if (inst->isSquashed()) {
        DPRINTF(DQWake, "Cancel replaying mem inst[%llu] because it was squashed\n", inst->seqNum);
        return;
    }
    DPRINTF(DQWake, "Replaying mem inst[%llu]\n", inst->seqNum);
    WKPointer wk(inst->dqPosition);
    if (isOrderDep) {
        wk.wkType = WKPointer::WKOrder;
    } else {
        wk.wkType = WKPointer::WKMem;
    }
    extraWakeup(wk);
}

template<class Impl>
void DataflowQueues<Impl>::squash(DQPointer p, bool all, bool including)
{
    if (all) {
        DPRINTF(FFSquash, "DQ: squash ALL instructions\n");
        for (auto &bank: dqs) {
            bank->clear(true);
        }
        head = 0;
        tail = 0;
        memDepUnit.squash(0, DummyTid);
        diewc->DQPointerJumped = true;
        cpu->removeInstsUntil(0, DummyTid);

        for (auto &wrapper: fuWrappers) {
            wrapper.squash(0);
        }
        for (auto &q: wakeQueues) {
            q.clear();
        }
        for (auto &q: forwardPointerQueue) {
            q.clear();
        }
        numPendingWakeups = 0;
        numPendingFwPointers = 0;
        numPendingWakeupMax = 0;
        numPendingFwPointerMax = 0;

        return;
    }

    unsigned u = pointer2uint(p);
    unsigned head_next;
    if (including) {
        if (u != tail) {
            head_next = dec(u);  // move head backward
        } else {
            head_next = tail;
        }
    } else { // mispredicted branch should not be squashed
        head_next = u;
        u = inc(u);  // squash first mis-fetched instruction
    }

    auto head_next_p = uint2Pointer(head_next);

    auto head_inst_next = dqs[head_next_p.bank]->readInstsFromBank(head_next_p);
    if (!(head_inst_next || head_next == tail)) {
        DPRINTF(FFSquash, "head: %i, tail: %i, head_next: %i\n", head, tail, head_next);
        assert(head_inst_next || head_next == tail);
    }
    DPRINTF(FFSquash, "DQ: squash all instruction after inst[%llu]\n",
            head_inst_next->seqNum);

    memDepUnit.squash(head_inst_next->seqNum, DummyTid);
    for (auto &wrapper: fuWrappers) {
        wrapper.squash(head_inst_next->seqNum);
    }
    cpu->removeInstsUntil(head_inst_next->seqNum, DummyTid);

    if (head != head_next) {
        while (validPosition(u) && logicallyLET(u, head)) {
            auto ptr = uint2Pointer(u);
            auto &bank = dqs.at(ptr.bank);
            bank->erase(ptr, true);
            if (u == head) {
                break;
            }
            u = inc(u);
        }
        DPRINTF(FFSquash, "DQ logic head becomes %d, physical is %d, tail is %d\n",
                head_next, head, tail);

        for (auto &bank: dqs) {
            bank->squash(p);
        }

        for (auto &q: readyInstsQueues) {
            q->squash(head_inst_next->seqNum);
        }
    } else {
        DPRINTF(FFSquash, "Very rare case: the youngset inst mispredicted, do nothing\n");
    }
}

template<class Impl>
bool DataflowQueues<Impl>::insert(DynInstPtr &inst, bool nonSpec)
{
    // todo: send to allocated DQ position
    assert(inst);

    bool jumped = false;

    DQPointer allocated = inst->dqPosition;
    DPRINTF(DQ, "allocated @(%d %d)\n", allocated.bank, allocated.index);

    auto dead_inst = dqs[allocated.bank]->readInstsFromBank(allocated);
    if (dead_inst) {
        DPRINTF(FFCommit, "Dead inst[%llu] found unexpectedly\n", dead_inst->seqNum);
        assert(!dead_inst);
    }
    dqs[allocated.bank]->writeInstsToBank(allocated, inst);
    if (isEmpty()) {
        tail = pointer2uint(allocated); //keep them together
        DPRINTF(DQ, "tail becomes %u to keep with head\n", tail);
        jumped = true;
        alignTails();
    }

    inst->setInDQ();
    // we don't need to add to dependents or producers here,
    //  which is maintained in DIEWC by archState



    if (inst->isMemRef() && !nonSpec) {
        memDepUnit.insert(inst);
    }
    dqs[allocated.bank]->checkReadiness(allocated);

    return jumped;
}

template<class Impl>
bool DataflowQueues<Impl>::insertBarrier(DynInstPtr &inst)
{
    memDepUnit.insertBarrier(inst);
    return insertNonSpec(inst);
}

template<class Impl>
bool DataflowQueues<Impl>::insertNonSpec(DynInstPtr &inst)
{
    bool non_spec = false;
    if (inst->isStoreConditional()) {
        memDepUnit.insertNonSpec(inst);
        non_spec = true;
    }
    assert(inst);
    inst->miscDepReady = false;
    inst->hasMiscDep = true;
    return insert(inst, non_spec);
}

template<class Impl>
void
DataflowQueues<Impl>::markFwPointers(
        std::array<DQPointer, 4> &pointers, PointerPair &pair, DynInstPtr &inst)
{
    unsigned op = pair.dest.op;
    if (inst) {
        DPRINTF(Reshape, "Source inst[%lu] is forwarder: %i, forwardOp: %i\n",
                inst->seqNum, inst->isForwarder(), inst->forwardOp);
    }
    if (pointers[op].valid) {
        DPRINTF(FFSquash, "Overriding previous (squashed) sibling:(%d %d) (%d)\n",
                pointers[op].bank, pointers[op].index, pointers[op].op);

        if (inst && (inst->isExecuted() || (!inst->isForwarder() && inst->opReady[op]) ||
                    (inst->isForwarder() && inst->forwardOpReady()))) {
            DPRINTF(FFSquash, "And extra wakeup new sibling\n");
            auto wk_ptr = WKPointer(pair.payload);
            wk_ptr.isFwExtra = true;
            wk_ptr.hasVal = true;
            if (op == 0) {
                wk_ptr.val = inst->getDestValue();
            } else {
                wk_ptr.val = inst->getOpValue(op);
            }
            extraWakeup(wk_ptr);
            if (op == 0) {
                inst->destReforward = false;
            }

        } else if (inst && inst->fuGranted && op == 0 && !inst->isForwarder()){
            DPRINTF(FFSquash, "And mark it to wakeup new child\n");
            inst->destReforward = true;
        }
    } else if (inst && ( (!inst->isForwarder() && inst->opReady[op]) ||
                (inst->isForwarder() && inst->forwardOpReady()))) {
        DPRINTF(DQWake, "which has already been waken up! op[%i] ready: %i\n",
                op, inst->opReady[op]);
        if (inst->isForwarder()) {
            DPRINTF(DQWake, "fw op(%i) ready: %i\n",
                    inst->forwardOp, inst->forwardOpReady());
        }
        auto wk_ptr = WKPointer(pair.payload);
        wk_ptr.isFwExtra = true;
        wk_ptr.hasVal = true;
        if (inst->isForwarder()) {
            wk_ptr.val = inst->getOpValue(inst->forwardOp);
        } else {
            if (op == 0) {
                wk_ptr.val = inst->getDestValue();
            } else {
                wk_ptr.val = inst->getOpValue(op);
            }
        }
        extraWakeup(wk_ptr);

    } else if (inst && (op == 0 && inst->fuGranted && !inst->opReady[op])) {
        DPRINTF(FFSquash, "And mark it to new coming child\n");
        inst->destReforward = true;
    }
    pointers[op] = pair.payload;
    DPRINTF(DQ, "And let it forward its value to (%d) (%d %d) (%d)\n",
            pair.payload.valid,
            pair.payload.bank, pair.payload.index, pair.payload.op);
}

template<class Impl>
void DataflowQueues<Impl>::rescheduleMemInst(DynInstPtr &inst, bool isStrictOrdered,
        bool isFalsePositive)
{
    DPRINTF(DQ, "Marking inst[%llu] as need rescheduling\n", inst->seqNum);
    inst->translationStarted(false);
    inst->translationCompleted(false);
    if (!isFalsePositive) {
        inst->clearCanIssue();
    }
    inst->fuGranted = false;
    inst->inReadyQueue = false;

    if (isStrictOrdered) {
        inst->hasMiscDep = true;  // this is rare, do not send a packet here
    } else {
        inst->hasOrderDep = true;  // this is rare, do not send a packet here
        inst->orderDepReady = false;
    }

    memDepUnit.reschedule(inst);
}

template<class Impl>
void DataflowQueues<Impl>::replayMemInst(DataflowQueues::DynInstPtr &inst)
{
    memDepUnit.replay();
}

template<class Impl>
void DataflowQueues<Impl>::cacheUnblocked()
{
    retryMemInsts.splice(retryMemInsts.end(), blockedMemInsts);
    DPRINTF(DQ, "blocked mem insts size: %llu, retry mem inst size: %llu\n",
            blockedMemInsts.size(), retryMemInsts.size());
    cpu->wakeCPU();
}

template<class Impl>
void DataflowQueues<Impl>::blockMemInst(DataflowQueues::DynInstPtr &inst)
{
    inst->translationStarted(false);
    inst->translationCompleted(false);
    inst->clearCanIssue();
    inst->clearIssued();
    inst->fuGranted = false;
    inst->inReadyQueue = false;
    inst->hasMemDep = true;
    inst->memDepReady = false;
    blockedMemInsts.push_back(inst);
    DPRINTF(DQWake, "Insert block mem inst[%llu] into blocked mem inst list, "
                    "size after insert: %llu\n",
            inst->seqNum, blockedMemInsts.size());
}

template<class Impl>
unsigned DataflowQueues<Impl>::numInDQ() const
{
    return head < tail ? head + queueSize - tail + 1 : head - tail + 1;
}

template<class Impl>
unsigned DataflowQueues<Impl>::numFree() const
{
    return queueSize - numInDQ();
}

template<class Impl>
void DataflowQueues<Impl>::drainSanityCheck() const
{
    assert(this->isEmpty());
    memDepUnit.drainSanityCheck();
}

template<class Impl>
void DataflowQueues<Impl>::takeOverFrom()
{
    resetState();
}

template<class Impl>
void DataflowQueues<Impl>::resetState()
{
    head = 0;
    tail = 0;
    forwardPtrIndex = 0;

    blockedMemInsts.clear();
    retryMemInsts.clear();

    numPendingWakeups = 0;
    numPendingWakeupMax = 0;
    numPendingFwPointers = 0;
    numPendingFwPointerMax = 0;

    for (auto &bank: dqs) {
        bank->resetState();
    }
}

template<class Impl>
void DataflowQueues<Impl>::resetEntries()
{
    // todo: it seems that DQ has nothing to do yet?
}

template<class Impl>
void DataflowQueues<Impl>::regStats()
{
    memDepUnit.regStats();
    for (auto &dq: dqs) {
        dq->regStats();
    }
    readyWaitTime
        .init(nBanks)
        .name(name() + ".readyWaitTime")
        .desc("readyWaitTime")
        .flags(Stats::total);

    oldWaitYoung
        .name(name() + ".oldWaitYoung")
        .desc("oldWaitYoung");

    WKFlowUsage
        .init(nOps * nBanks + 1)
        .name(name() + ".WKFlowUsage")
        .desc("WKFlowUsage")
        .flags(Stats::total);

    WKQueueLen
        .init(nOps * nBanks * maxQueueDepth + 1)
        .prereq(WKQueueLen)
        .name(name() + ".WKQueueLen")
        .desc("WKQueueLen")
        .flags(Stats::cdf);

    HalfSquashes
        .name(name() + ".HalfSquashes")
        .desc("HalfSquashes");

    SrcOpPackets
        .name(name() + ".SrcOpPackets")
        .desc("SrcOpPackets");
    DestOpPackets
        .name(name() + ".DestOpPackets")
        .desc("DestOpPackets");
    MemPackets
        .name(name() + ".MemPackets")
        .desc("MemPackets");
    OrderPackets
        .name(name() + ".OrderPackets")
        .desc("OrderPackets");
    MiscPackets
        .name(name() + ".MiscPackets")
        .desc("MiscPackets");
    TotalPackets
        .name(name() + ".TotalPackets")
        .desc("TotalPackets");
    TotalPackets = SrcOpPackets + DestOpPackets + MemPackets + \
                   OrderPackets + MiscPackets;

}

template<class Impl>
void DataflowQueues<Impl>::readQueueHeads()
{
    for (unsigned b = 0; b < nBanks; b++) {
        for (unsigned op = 0; op < nOps; op++) {
            unsigned i = b * nOps + op;

            // read fw pointers
            auto &fw_pkt = insert_requests[i];
            if (forwardPointerQueue[i].empty()) {
                fw_pkt.valid = false;
            } else {
                fw_pkt = forwardPointerQueue[i].front();
            }

            // read wakeup pointers
            auto &wk_pkt = wakeup_requests[i];
            const auto &q = wakeQueues[i];
            if (q.empty()) {
                wk_pkt.valid = false;
            } else {
                const WKPointer &ptr = q.front();
                DPRINTF(DQWake||Debug::RSProbe1,
                        "WKQ[%i] Found valid wakePtr:(%i) (%i %i) (%i)\n",
                        i, ptr.valid, ptr.bank, ptr.index, ptr.op);
                wk_pkt.valid = ptr.valid;
                wk_pkt.payload = ptr;
                unsigned d = ptr.bank * nOps + ptr.op;
                wk_pkt.dest = d;
                wk_pkt.destBits = uint2Bits(d);
//                wk_pkt.source = i;
            }
        }
    }
}

template<class Impl>
void DataflowQueues<Impl>::setTimeBuf(TimeBuffer<DQStruct> *dqtb)
{
    DQTS = dqtb;
    fromLastCycle = dqtb->getWire(-1);
    toNextCycle = dqtb->getWire(0);
}

template<class Impl>
void DataflowQueues<Impl>::dumpInstPackets(vector<DQPacket<DynInstPtr> *> &v)
{
    for (auto& pkt: v) {
        assert(pkt);
        DPRINTF(DQWake, "&pkt: %p, ", pkt);
        DPRINTFR(DQWake, "v: %d, dest: %lu, src: %d,",
                pkt->valid, pkt->destBits.to_ulong(), pkt->source);
        DPRINTFR(DQWake, " inst: %p\n", pkt->payload);
    }
}

template<class Impl>
void DataflowQueues<Impl>::dumpPairPackets(vector<DQPacket<PointerPair> *> &v)
{
    for (auto& pkt: v) {
        assert(pkt);
        DPRINTF(DQOmega, "&pkt: %p, ", pkt);
        DPRINTFR(DQOmega, "v: %d, dest: %lu, src: %d,",
                 pkt->valid, pkt->destBits.to_ulong(), pkt->source);
        DPRINTFR(DQOmega, " pair dest:(%d) (%d %d) (%d) \n",
                pkt->payload.dest.valid, pkt->payload.dest.bank,
                pkt->payload.dest.index, pkt->payload.dest.op);
    }
}

template<class Impl>
void DataflowQueues<Impl>::setLSQ(LSQ *lsq)
{
    for (auto &wrapper: fuWrappers) {
        wrapper.setLSQ(lsq);
    }
}

template<class Impl>
void DataflowQueues<Impl>::deferMemInst(DynInstPtr &inst)
{
    deferredMemInsts.push_back(inst);
}

template<class Impl>
typename Impl::DynInstPtr
DataflowQueues<Impl>::getDeferredMemInstToExecute()
{
    auto it = deferredMemInsts.begin();
    while (it != deferredMemInsts.end()) {
        if ((*it)->translationCompleted() || (*it)->isSquashed()) {
            DynInstPtr inst = *it;
            deferredMemInsts.erase(it);
            return inst;
        }
    }
    return nullptr;
}

template<class Impl>
void DataflowQueues<Impl>::setDIEWC(DIEWC *_diewc)
{
    diewc = _diewc;
    for (auto &wrapper: fuWrappers) {
        wrapper.setExec(diewc);
    }
}

template<class Impl>
void DataflowQueues<Impl>::violation(DynInstPtr store, DynInstPtr violator)
{
    memDepUnit.violation(store, violator);
}

template<class Impl>
void DataflowQueues<Impl>::scheduleNonSpec()
{
    if (!getTail()) {
        DPRINTF(FFSquash, "Ignore scheduling attempt to squashing inst\n");
        return;
    }
    WKPointer wk = WKPointer(getTail()->dqPosition);
    auto p = uint2Pointer(tail);
    DPRINTF(DQ, "Scheduling non spec inst @ (%i %i)\n", p.bank, p.index);
    wk.wkType = WKPointer::WKMisc;
    extraWakeup(wk);
}

template<class Impl>
void DataflowQueues<Impl>::incExtraWKptr()
{
    extraWKPtr = (extraWKPtr + 1) % nBanks;
}

template<class Impl>
list<typename Impl::DynInstPtr>
DataflowQueues<Impl>::getBankHeads()
{
    list<DynInstPtr> heads;
    auto ptr_i = head;
    for (unsigned count = 0; count < nBanks; count++) {
        auto ptr = uint2Pointer(ptr_i);
        DynInstPtr inst = dqs[ptr.bank]->readInstsFromBank(ptr);
        if (inst) {
            DPRINTF(DQRead, "read inst[%d] from DQ\n", inst->seqNum);
        } else {
            DPRINTF(DQRead, "inst@[%d] is null\n", ptr_i);
        }
        heads.push_back(inst);
        ptr_i = dec(ptr_i);
    }
    return heads;
}

template<class Impl>
list<typename Impl::DynInstPtr>
DataflowQueues<Impl>::getBankTails()
{
    list<DynInstPtr> tails;
    unsigned ptr_i = tail;
    for (unsigned count = 0; count < nBanks; count++) {
        auto ptr = uint2Pointer(ptr_i);
        DynInstPtr inst = dqs[ptr.bank]->readInstsFromBank(ptr);
        if (inst) {
            DPRINTF(DQRead, "read inst[%llu] from DQ\n", inst->seqNum);
        } else {
            DPRINTF(DQRead, "inst@[%d] is null\n", ptr_i);
        }
        tails.push_back(inst);
        ptr_i = inc(ptr_i);
    }
    return tails;
}

template<class Impl>
bool
DataflowQueues<Impl>::logicallyLET(unsigned x, unsigned y) const
{
    return logicallyLT(x, y) || x == y;
}

template<class Impl>
bool
DataflowQueues<Impl>::logicallyLT(unsigned x, unsigned y) const
{
    DPRINTF(FFSquash, "head: %i tail: %i x: %i y: %i\n", head, tail, x, y);
    if (head >= tail) {
        assert(head >= x);
        assert(head >= y);
        assert(inc(x) >= tail || (inc(x) == 0));
        assert(inc(y) >= tail || (inc(y) == 0));

        return x < y;

    } else {
        assert(!(x > head && x < tail));
        assert(!(y > head && y < tail));

        if ((x <= head && y <= head) ||
                (x >= tail && y >= tail)) {
            return x < y;
        } else {
            // 大的小，小的大
            return x > y;
        }
    }
}

template<class Impl>
bool
DataflowQueues<Impl>::validPosition(unsigned u) const
{
    DPRINTF(DQ, "head: %i tail: %i u: %u\n", head, tail, u);
    if (head >= tail) {
        return (u <= head && u >= tail);
    } else {
        return (u <= head || u >= tail);
    }
}

template<class Impl>
unsigned
DataflowQueues<Impl>::inc(unsigned u) const
{
    return (u+1) % queueSize;
}

template<class Impl>
unsigned
DataflowQueues<Impl>::dec(unsigned u) const
{
    return u == 0 ? queueSize - 1 : u - 1;
}

template<class Impl>
void DataflowQueues<Impl>::tryFastCleanup()
{
    // todo: clean bubbles left by squashed instructions
    auto inst = getTail();
    if (inst) {
        DPRINTF(FFSquash, "Strangely reaching fast cleanup when DQ tail is not null!\n");
    }
    unsigned old_tail = tail;
    while (!inst && !isEmpty()) {
        auto tail_ptr = uint2Pointer(tail);
        auto bank = dqs[tail_ptr.bank];
        bank->setTail(uint2Pointer(tail).index);
        bank->advanceTail();

        tail = inc(tail);
        DPRINTF(DQ, "tail becomes %u in fast clean up\n", tail);
        inst = getTail();
        diewc->DQPointerJumped = true;
    }
    if (isEmpty()) {
        tail = inc(tail);
        head = inc(head);
    }
    DPRINTF(FFCommit, "Fastly advance youngest ptr to %d, olddest ptr to %d\n", head, tail);
    clearPending2SquashedRange(old_tail, tail);
}

template<class Impl>
unsigned DataflowQueues<Impl>::numInFlightFw()
{
    return numPendingFwPointers;
}

template<class Impl>
void DataflowQueues<Impl>::digestForwardPointer()
{
    DPRINTF(DQ, "Pair packets before selection\n");
    dumpPairPackets(insert_req_ptrs);
    insert_granted_ptrs = pointerQueueBankXBar.select(insert_req_ptrs, &nullFWPkt);

    DPRINTF(DQ, "Selected pairs:\n");
    dumpPairPackets(insert_granted_ptrs);

    DPRINTF(DQ, "Pair packets after selection\n");
    dumpPairPackets(insert_req_ptrs);

    for (auto &ptr : insert_granted_ptrs) {
        if (ptr->valid) {
            DPRINTF(DQ, "pair[%d] (pointer(%d) (%d %d) (%d)) granted\n",
                    ptr->source, ptr->payload.dest.valid,
                    ptr->payload.dest.bank, ptr->payload.dest.index, ptr->payload.dest.op);
        }
    }
    for (unsigned b = 0; b < nBanks; b++) {
        for (unsigned op = 0; op <= 3; op++) {
            const auto &pkt = insert_granted_ptrs[b * nOps + op];
            if (!pkt->valid || !pkt->payload.dest.valid ||
                !pkt->payload.payload.valid) {
                continue;
            }
            PointerPair &pair = pkt->payload;
            insert_req_granted[pkt->source] = true;

            DynInstPtr inst = dqs[pair.dest.bank]->readInstsFromBank(pair.dest);

            if (inst) {
                DPRINTF(DQ, "Insert fw pointer (%d %d) (%d) after inst reached\n",
                        pair.dest.bank, pair.dest.index, pair.dest.op);
                markFwPointers(inst->pointers, pair, inst);

                if (op == 0 && inst->destReforward && inst->isExecuted()) {
                    // already executed but not forwarded
                    WKPointer wk(pair.payload);
                    wk.hasVal = true;
                    wk.val = inst->getDestValue();
                    extraWakeup(wk);
                    inst->destReforward = false;
                }

            } else {
                DPRINTF(DQ, "Insert fw pointer (%d %d) (%d) before inst reached\n",
                        pair.dest.bank, pair.dest.index, pair.dest.op);
                auto &pointers =
                        dqs[pair.dest.bank]->prematureFwPointers[pair.dest.index];
                markFwPointers(pointers, pair, inst);
            }

            assert(!forwardPointerQueue[pkt->source].empty());
            if (forwardPointerQueue[pkt->source].size() == numPendingFwPointerMax) {
                numPendingFwPointerMax--;
            }
            forwardPointerQueue[pkt->source].pop_front();
            numPendingFwPointers--;

            // handle the special case that inst executed before pointer reached
        }
    }
}

template<class Impl>
void DataflowQueues<Impl>::writebackLoad(DynInstPtr &inst)
{
//    DPRINTF(DQWake, "Original ptr: (%i) (%i %i) (%i)\n",
//            inst->pointers[0].valid, inst->pointers[0].bank,
//            inst->pointers[0].index, inst->pointers[0].op);
    if (inst->pointers[0].valid) {
        WKPointer wk(inst->pointers[0]);
        DPRINTF(DQWake, "Sending pointer to (%i) (%i %i) (%i) that depends on"
                        " load[%llu] (%i %i) loaded value: %llu\n",
                        wk.valid, wk.bank, wk.index, wk.op,
                        inst->seqNum,
                        inst->dqPosition.bank, inst->dqPosition.index,
                        inst->getDestValue().i
               );
        wk.hasVal = true;
        wk.val = inst->getDestValue();
        extraWakeup(wk);

        completeMemInst(inst);
    } else {
        auto &p = inst->dqPosition;
        DPRINTF(DQWake, "Mark itself[%llu] (%i) (%i %i) (%i) ready\n",
                inst->seqNum, p.valid, p.bank, p.index, p.op);
        inst->opReady[0] = true;
        completeMemInst(inst);
    }
}

template<class Impl>
void DataflowQueues<Impl>::extraWakeup(const WKPointer &wk)
{
    auto q = allocateWakeQ();
    DPRINTF(DQWake||Debug::RSProbe2,
            "Push Wake (extra) Ptr (%i %i) (%i) to temp wakequeue[%u]\n",
            wk.bank, wk.index, wk.op, q);
    tempWakeQueues[q].push_back(wk);
    DPRINTF(DQWake, "After push, numPendingWakeups = %u\n", numPendingWakeups);
}

template<class Impl>
void DataflowQueues<Impl>::alignTails()
{
    for (unsigned count = 0; count < nBanks; count++) {
        auto u = (count + tail) % queueSize;
        auto ptr = uint2Pointer(u);
        dqs[ptr.bank]->setTail(ptr.index);
    }
}

template<class Impl>
void DataflowQueues<Impl>::wakeMemRelated(DynInstPtr &inst)
{
    if (inst->isMemRef()) {
        memDepUnit.wakeDependents(inst);
    }
}

template<class Impl>
void DataflowQueues<Impl>::completeMemInst(DynInstPtr &inst)
{
    inst->receivedDest = true;
    if (inst->isMemRef()) {
        // complateMemInst
        inst->memOpDone(true);
        memDepUnit.completed(inst);

    } else if (inst->isMemBarrier() || inst->isWriteBarrier()) {
        memDepUnit.completeBarrier(inst);
    }
}

template<class Impl>
void DataflowQueues<Impl>::replayMemInsts()
{
    DynInstPtr inst;
    if ((inst = getDeferredMemInstToExecute())) {
        DPRINTF(DQWake, "Replaying from deferred\n");
        addReadyMemInst(inst, false);
    } else if ((inst = getBlockedMemInst())) {
        DPRINTF(DQWake, "Replaying from blocked\n");
        addReadyMemInst(inst, false);
    }
}

template<class Impl>
typename Impl::DynInstPtr DataflowQueues<Impl>::getBlockedMemInst()
{
    if (retryMemInsts.empty()) {
        return nullptr;
    } else {
        auto inst = retryMemInsts.front();
        retryMemInsts.pop_front();
        DPRINTF(DQ, "retry mem insts size: %llu after pop\n",
                retryMemInsts.size());
        return inst;
    }
}

template<class Impl>
typename Impl::DynInstPtr DataflowQueues<Impl>::findBySeq(InstSeqNum seq)
{
    for (unsigned u = tail; logicallyLET(u, head); u = inc(u)) {
        auto p = uint2Pointer(u);
        auto inst = dqs[p.bank]->readInstsFromBank(p);
        if (inst && inst->seqNum == seq) {
            return inst;
        }
    }
    panic("It must be it DQ!\n");
}

template<class Impl>
bool DataflowQueues<Impl>::queuesEmpty()
{
    bool fwPointerEmpty = true;
    bool wkPointerEmpty = true;
    for (unsigned b = 0; b < nBanks; b++) {
        for (unsigned op = 0; op < nOps; op++) {
            fwPointerEmpty &= forwardPointerQueue[b*nOps+op].empty();
            wkPointerEmpty &= wakeQueues[b*nOps+op].empty();
        }
    }
    return fwPointerEmpty && wkPointerEmpty;
}

template<class Impl>
void DataflowQueues<Impl>::dumpQueues()
{
    printf("Dump wakeQueues:\n");
    for (unsigned b = 0; b < nBanks; b++) {
        for (unsigned op = 0; op < nOps; op++) {
            printf("Q[%i]: ", b*nOps+op);
            auto &q = wakeQueues[b*nOps + op];
            for (const auto &p: q) {
                printf("(%i) (%i %i) (%i), ",
                        p.valid, p.bank, p.index, p.op);
            }
            printf("\n");
        }
    }
    printf("Dump fw pointer queues:\n");
    for (unsigned b = 0; b < nBanks; b++) {
        for (unsigned op = 0; op < nOps; op++) {
            printf("Q[%i]: ", b*nOps+op);
            auto &q = forwardPointerQueue[b*nOps + op];
            for (const auto &p: q) {
                printf("(%i) (%i %i) (%i), ",
                        p.payload.dest.valid, p.payload.dest.bank,
                        p.payload.dest.index, p.payload.dest.op);
            }
            printf("\n");
        }
    }
}

template<class Impl>
void DataflowQueues<Impl>::endCycle()
{
    for (auto &wrapper: fuWrappers) {
        wrapper.endCycle();
    }

    mergeExtraWKPointers();

    DPRINTF(DQ, "Size of blockedMemInsts: %llu, size of retryMemInsts: %llu\n",
            blockedMemInsts.size(), retryMemInsts.size());
}

template<class Impl>
void DataflowQueues<Impl>::dumpFwQSize()
{
    DPRINTF(DQ || Debug::RSProbe1, "fw queue in flight = %i, cannot reset oldestRef\n", numPendingFwPointers);
    for (unsigned b = 0; b < nBanks; b++) {
        for (unsigned op = 0; op < nOps; op++) {
            DPRINTFR(DQ || Debug::RSProbe1, "fw queue %i.%i size: %llu\n",
                    b, op, forwardPointerQueue[b*nOps + op].size());
        }
    }
}

template<class Impl>
void DataflowQueues<Impl>::maintainOldestUsed()
{
}

template<class Impl>
std::pair<InstSeqNum, Addr> DataflowQueues<Impl>::clearHalfWKQueue()
{
    unsigned oldest_to_squash = getHeadPtr();
    for (auto &q: wakeQueues) {
        if (q.empty()) {
            continue;
        }
        for (size_t i = 0, e = q.size(); i < e/2; i++) {
            const auto &ele = q[e - 1 - i];
            if (ele.valid) {
                unsigned p = pointer2uint(DQPointer(ele));
                if (validPosition(p)) {
                    auto p_ptr = uint2Pointer(p);
                    auto p_inst = dqs[p_ptr.bank]->readInstsFromBank(p_ptr);

                    if (logicallyLT(p, oldest_to_squash) && p_inst) {
                        oldest_to_squash = p;
                    }
                }
            }
            q.pop_back();
            numPendingWakeups--;
        }
    }
    if (oldest_to_squash == getHeadPtr()) {
        warn("oldest_to_squash == getHeadPtr, this case is infrequent\n");
    }
    auto ptr = uint2Pointer(oldest_to_squash);
    auto inst = dqs[ptr.bank]->readInstsFromBank(ptr);

    auto sec_ptr = uint2Pointer(dec(oldest_to_squash));
    auto second_oldest_inst = dqs[sec_ptr.bank]->readInstsFromBank(sec_ptr);
    Addr hint_pc = 0;
    if (second_oldest_inst) {
        hint_pc = second_oldest_inst->instAddr();
    }

    if (inst) {
        return std::make_pair(inst->seqNum, hint_pc);
    } else {
        return std::make_pair(std::numeric_limits<InstSeqNum>::max(), hint_pc);
    }
}

template<class Impl>
std::pair<InstSeqNum, Addr> DataflowQueues<Impl>::clearHalfFWQueue()
{
    unsigned oldest_to_squash = getHeadPtr();
    for (auto &q: forwardPointerQueue) {
        if (q.empty()) {
            continue;
        }
        for (size_t i = 0, e = q.size(); i < e/2; i++) {
            const auto &ele = q[e - 1 - i];
            if (ele.valid) {
                unsigned p = pointer2uint(ele.payload.payload);
                if (validPosition(p)) {
                    auto p_ptr = uint2Pointer(p);
                    auto p_inst = dqs[p_ptr.bank]->readInstsFromBank(p_ptr);
                    if (logicallyLT(p, oldest_to_squash) && p_inst) {
                        oldest_to_squash = p;
                    }
                }
            }
            q.pop_back();
            numPendingFwPointers--;
        }
    }
    if (oldest_to_squash == getHeadPtr()) {
        warn("oldest_to_squash == getHeadPtr, this case is infrequent\n");
    }
    auto ptr = uint2Pointer(oldest_to_squash);
    auto inst = dqs[ptr.bank]->readInstsFromBank(ptr);

    auto sec_ptr = uint2Pointer(dec(oldest_to_squash));
    auto second_oldest_inst = dqs[ptr.bank]->readInstsFromBank(sec_ptr);
    Addr hint_pc = 0;
    if (second_oldest_inst) {
        hint_pc = second_oldest_inst->instAddr();
    }

    if (inst) {
        return std::make_pair(inst->seqNum, hint_pc);
    } else {
        return std::make_pair(std::numeric_limits<InstSeqNum>::max(), hint_pc);
    }
}

template<class Impl>
void DataflowQueues<Impl>::processWKQueueFull()
{
    bool old_seq_valid = false;
    if (halfSquash) {
        old_seq_valid = true;
    } else {
        HalfSquashes++;
        halfSquash = true;
    }

    DPRINTF(DQ, "Dump before clear:\n");
    if (Debug::DQ || Debug::RSProbe1) {
        dumpQueues();
    }

    if (old_seq_valid) {
        InstSeqNum another_seq;
        Addr another_pc;
        std::tie(another_seq, another_pc) = clearHalfWKQueue();
        checkUpdateSeq(halfSquashSeq, halfSquashPC, another_seq, another_pc);
    } else {
        std::tie(halfSquashSeq, halfSquashPC) = clearHalfWKQueue();
    }


    bool clear_another_queue = false;
    for (auto q: forwardPointerQueue) {
        if (q.size() > maxQueueDepth/2) {
            clear_another_queue = true;
        }
    }

    if (clear_another_queue) {
        InstSeqNum another_seq;
        Addr another_pc;
        std::tie(another_seq, another_pc) = clearHalfFWQueue();
        checkUpdateSeq(halfSquashSeq, halfSquashPC, another_seq, another_pc);
    }

    DPRINTF(DQ, "Dump after clear:\n");
    if (Debug::DQ || Debug::RSProbe1) {
        dumpQueues();
    }
}

template<class Impl>
void DataflowQueues<Impl>::processFWQueueFull()
{
    bool old_seq_valid = false;
    if (halfSquash) {
        old_seq_valid = true;
    } else {
        HalfSquashes++;
        halfSquash = true;
    }

    DPRINTF(DQ || Debug::RSProbe1, "Dump before clear:\n");
    if (Debug::DQ || Debug::RSProbe1) {
        dumpQueues();
    }

    if (old_seq_valid) {
        InstSeqNum another_seq;
        Addr another_pc;
        std::tie(another_seq, another_pc) = clearHalfFWQueue();
        checkUpdateSeq(halfSquashSeq, halfSquashPC, another_seq, another_pc);
    } else {
        std::tie(halfSquashSeq, halfSquashPC) = clearHalfFWQueue();
    }

    bool clear_another_queue = false;
    for (auto q: wakeQueues) {
        if (q.size() > maxQueueDepth/2) {
            clear_another_queue = true;
        }
    }

    if (clear_another_queue) {
        InstSeqNum another_seq;
        Addr another_pc;
        std::tie(another_seq, another_pc) = clearHalfWKQueue();
        checkUpdateSeq(halfSquashSeq, halfSquashPC, another_seq, another_pc);
    }

    DPRINTF(DQ || Debug::RSProbe1, "Dump after clear:\n");
    if (Debug::DQ || Debug::RSProbe1) {
        dumpQueues();
    }
}

template<class Impl>
typename DataflowQueues<Impl>::DynInstPtr
DataflowQueues<Impl>::readInst(const DQPointer &p) const
{
    const XDataflowQueueBank *bank = dqs[p.bank];
    const auto &inst = bank->readInstsFromBank(p);
    return inst;
}

template<class Impl>
bool
DataflowQueues<Impl>::hasTooManyPendingInsts()
{
    for (const auto &dq: dqs) {
        if (dq->hasTooManyPendingInsts()) {
            return true;
        }
    }
    return false;
}

template<class Impl>
void
DataflowQueues<Impl>::advanceHead()
{
    if (isEmpty()) {
        clearInflightPackets();
        head = inc(head);
        tail = inc(tail);
        return;
    } else {
        assert(!isFull());
        head = inc(head);
    }

    if (head == 0) {
        headTerm = (headTerm + 1) % termMax;
    }

    auto allocated = uint2Pointer(head);
    auto dead_inst = dqs[allocated.bank]->readInstsFromBank(allocated);
    if (dead_inst) {
        DPRINTF(FFCommit, "Dead inst[%llu] found unexpectedly\n", dead_inst->seqNum);
        assert(!dead_inst);
    }
}

template<class Impl>
typename Impl::DynInstPtr
DataflowQueues<Impl>::checkAndGetParent(
        const DQPointer &parent, const DQPointer &child) const
{
    assert(child.valid);
    assert(parent.valid);
    assert(readInst(child));

    unsigned pu = pointer2uint(parent);

    if (!validPosition(pu)) return nullptr;

    if (!logicallyLT(pu, pointer2uint(child))) return nullptr;

    return readInst(parent);
}

template<class Impl>
void
DataflowQueues<Impl>::countUpPointers()
{
    unsigned total_queue_len = 0;
    for (deque<WKPointer> &q :wakeQueues) {
        total_queue_len += q.size();
        for (WKPointer &p: q) {
            if (p.valid) {
                p.queueTime++;
            }
        }
    }
    assert(total_queue_len < nOps * nBanks * maxQueueDepth);
    if (total_queue_len != 0) {
        WKQueueLen[total_queue_len]++;
    }
}

template<class Impl>
void
DataflowQueues<Impl>::rearrangePrioList()
{
    opPrioList.push_back(opPrioList.front());
    opPrioList.pop_front();
}

template<class Impl>
unsigned
DataflowQueues<Impl>::allocateWakeQ()
{
    unsigned start = qAllocPtr;
    while (!(wakeQueues[qAllocPtr].empty() && tempWakeQueues[qAllocPtr].empty())) {
        qAllocPtr = (qAllocPtr + 1) % (nOps * nBanks);
        if (qAllocPtr == start) {
            qAllocPtr = randAllocator(gen) % (nOps * nBanks);
            break;
        }
    }
    unsigned tmp = qAllocPtr;
    qAllocPtr = (qAllocPtr + 1) % (nOps * nBanks);
    return tmp;
}

template<class Impl>
void
DataflowQueues<Impl>::tryResetRef()
{
}

template<class Impl>
void
DataflowQueues<Impl>::mergeExtraWKPointers()
{
    DPRINTF(DQWake, "Before cycle-end push, numPendingWakeups = %u\n", numPendingWakeups);
    for (unsigned i = 0; i < nOps*nBanks; i++) {
        while (!tempWakeQueues[i].empty()) {
            const auto &dest = tempWakeQueues[i].front();
            DPRINTF(DQWake || Debug::RSProbe2,
                    "Got temped wakeup pointer to (%d %d) (%d), pushed to wakeQueue[%i]\n",
                    dest.bank, dest.index, dest.op, i);
            pushToWakeQueue(i, tempWakeQueues[i].front());
            tempWakeQueues[i].pop_front();
            numPendingWakeups++;
        }
    }
    DPRINTF(DQWake || RSProbe1,
            "After cycle-end push, numPendingWakeups = %u\n", numPendingWakeups);
    dumpWkQSize();
    if (Debug::RSProbe2) {
        dumpWkQ();
    }
}

template<class Impl>
void
DataflowQueues<Impl>::dumpWkQSize()
{
    for (unsigned b = 0; b < nBanks; b++) {
        for (unsigned op = 0; op < nOps; op++) {
            DPRINTFR(DQ || RSProbe1, "wake queue %i.%i size: %llu\n",
                    b, op, wakeQueues[b*nOps + op].size());
        }
    }
}

template<class Impl>
void
DataflowQueues<Impl>::readPointersToWkQ()
{
    DPRINTF(DQ, "Reading wk pointers from banks to wake queus\n");
    // push forward pointers to queues
    for (unsigned b = 0; b < nBanks; b++) {
        for (unsigned op = 0; op < nOps; op++) {
            auto &ptr = fromLastCycle->pointers[b * nOps + op];
            if (ptr.valid) {
                DPRINTF(DQWake||Debug::RSProbe2,
                        "Push WakePtr (%i %i) (%i) to wakequeue[%u]\n",
                        ptr.bank, ptr.index, ptr.op, b);
                pushToWakeQueue(b * nOps + op, WKPointer(ptr));
                numPendingWakeups++;
                DPRINTF(DQWake, "After push, numPendingWakeups = %u\n", numPendingWakeups);
                if (wakeQueues[b * nOps + op].size() > maxQueueDepth) {
                    processWKQueueFull();
                }
            } else {
                DPRINTF(DQWakeV1, "Ignore Invalid WakePtr (%i) (%i %i) (%i)\n",
                        ptr.valid, ptr.bank, ptr.index, ptr.op);
            }
        }
    }
}

template<class Impl>
void
DataflowQueues<Impl>::clearInflightPackets()
{
    DPRINTF(FFSquash, "Clear all in-flight fw and wk pointers\n");
    for (auto &q: wakeQueues) {
        q.clear();
    }
    for (auto &q: forwardPointerQueue) {
        q.clear();
    }
    numPendingWakeups = 0;
    numPendingWakeupMax = 0;
    numPendingFwPointers = 0;
    numPendingFwPointerMax = 0;
}

template<class Impl>
void
DataflowQueues<Impl>::clearPending2SquashedRange(unsigned start, unsigned end)
{
    for (auto &q: wakeQueues) {
        for (auto wk_ptr: q) {
            if (!validPosition(pointer2uint(DQPointer(wk_ptr)))) {
                wk_ptr.valid = false;
            }
        }
    }
}

template<class Impl>
void
DataflowQueues<Impl>::dumpWkQ()
{
    printf("Dump wakeQueues:\n");
    for (unsigned b = 0; b < nBanks; b++) {
        for (unsigned op = 0; op < nOps; op++) {
            printf("Q[%i]: ", b*nOps+op);
            auto &q = wakeQueues[b*nOps + op];
            for (const auto &p: q) {
                printf("(%i) (%i %i) (%i), ",
                        p.valid, p.bank, p.index, p.op);
            }
            printf("\n");
        }
    }
}

template<class Impl>
void
DataflowQueues<Impl>::genFUValidMask()
{
    for (auto &req: wakeup_requests) {
        if (req.valid && !dqs[req.payload.bank]->canServeNew()) {
            req.valid = false;
        }
    }
}

template<class Impl>
void
DataflowQueues<Impl>::countCycles(typename Impl::DynInstPtr &inst, WKPointer *wk)
{
    inst->ssrDelay = wk->ssrDelay;
    inst->queueingDelay = wk->queueTime;
    inst->pendingDelay = wk->pendingTime;
}

template<class Impl>
void
DataflowQueues<Impl>::checkUpdateSeq(InstSeqNum &seq, Addr &addr,
        InstSeqNum seq_new, Addr addr_new)
{
    if (seq_new <= seq) {
        seq = seq_new;
        addr = addr_new;
    }
}

template<class Impl>
void
DataflowQueues<Impl>::pushToWakeQueue(unsigned q_index, WKPointer ptr)
{
    auto &q = wakeQueues[q_index];
    if (!AgedWakeQueuePush) {
        q.push_back(ptr);

    } else {
        if (q.empty()) {
            q.push_back(ptr);
            return;
        }

        const WKPointer &front = q.front();
        const WKPointer &back = q.back();
        if (q.size() > 1) {

            if (notYoungerThan(ptr, front)) {
                q.push_front(ptr);

            } else if (notOlderThan(ptr, back)) {
                q.push_back(ptr);

            } else {
                if (pushFF[q_index]) {
                    WKPointer tmp = q.front();
                    q.front() = ptr;
                    q.push_front(tmp);
                } else {
                    WKPointer tmp = q.back();
                    q.back() = ptr;
                    q.push_back(tmp);
                }
                pushFF[q_index] = !pushFF[q_index];
            }

        } else {
            if (notYoungerThan(ptr, front) != 1) {
                q.push_front(ptr);
            } else {
                q.push_back(ptr);
            }
        }
    }
}

template<class Impl>
int
DataflowQueues<Impl>::pointerCmp(const WKPointer &lptr, const WKPointer &rptr)
{
    // in the same term
    if (rptr.term == lptr.term) {
        if (pointer2uint(lptr) < pointer2uint(rptr)) {
            return -1;
        } else if (pointer2uint(lptr) > pointer2uint(rptr)) {
            return 1;
        } else {
            return 0;
        }
    }

    // not in the same term
    int term_diff = lptr.term - rptr.term;
    if (term_diff == 1 || term_diff == -1) {
        return term_diff;
    }

    // wrapped around
    if (term_diff > 0) {
        return -1;
    } else {
        return 1;
    }
}

template<class Impl>
bool
DataflowQueues<Impl>::notOlderThan(const WKPointer &lptr, const WKPointer &rptr)
{
    return pointerCmp(lptr, rptr) != -1;
}

template<class Impl>
bool
DataflowQueues<Impl>::notYoungerThan(const WKPointer &lptr, const WKPointer &rptr)
{
    return pointerCmp(lptr, rptr) != 1;
}

template<class Impl>
bool
DataflowQueues<Impl>::matchInGroup(OpClass op, OpGroups op_group)
{
    if (op_group == OpGroups::MultDiv) {
        for (int i: MultDivOps) {
            if (i == op) {
                return true;
            }
        }
        return false;
    }
    if (op_group == OpGroups::FPAdd) {
        for (int i: FPAddOps) {
            if (i == op) {
                return true;
            }
        }
        return false;
    }
    return false;
}
template<class Impl>
void
DataflowQueues<Impl>::shuffleNeighbors()
{
    std::shuffle(std::begin(fuPointer[OpGroups::MultDiv]),
            std::end(fuPointer[OpGroups::MultDiv]), gen);
    std::shuffle(std::begin(fuPointer[OpGroups::FPAdd]),
            std::end(fuPointer[OpGroups::FPAdd]), gen);
    DPRINTF(FUW, "shuffled pointers:\n");
    for (const auto x:fuPointer[OpGroups::MultDiv]) {
        DPRINTFR(FUW, "%i ", x);
    }
    DPRINTFR(FUW, "\n");
    for (const auto x:fuPointer[OpGroups::FPAdd]) {
        DPRINTFR(FUW, "%i ", x);
    }
    DPRINTFR(FUW, "\n");
}

}

#include "cpu/forwardflow/isa_specific.hh"

template class FF::DataflowQueues<FFCPUImpl>;
