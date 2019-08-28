//
// Created by zyy on 19-6-10.
//

#include "cpu/forwardflow/dataflow_queue.hh"
#include "debug/DQ.hh"
#include "debug/DQB.hh"  // DQ bank
#include "debug/DQOmega.hh"
#include "debug/DQRead.hh"  // DQ read
#include "debug/DQWake.hh"  // DQ wakeup
#include "debug/DQWrite.hh"  // DQ write
#include "debug/FFCommit.hh"
#include "debug/FFExec.hh"
#include "debug/FFSquash.hh"
#include "debug/FUW.hh"
#include "params/DerivFFCPU.hh"
#include "params/FFFUPool.hh"

namespace FF {

using namespace std;

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

    if (tail_inst->fuGranted) {
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
    if (inst->fuGranted) {
        DPRINTF(DQWake, "Inst at [%d] has been granted, skip\n", tail);
        readyQueue.pop_front();
        return nullptr;
    }

    for (unsigned op = 1; op < nOps; op++) {
        if (inst->hasOp[op] && !inst->opReady[op]) {
            panic("inst[%d] has op [%d] not ready and cannot execute\n",
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
    DPRINTF(DQWake, "inst[%d] is ready but not waken up!,"
            " and is waken up by direct wake-up checker\n", inst->seqNum);
    readyQueue.pop_front();
    directWakenUp++;
    return inst;
}

template<class Impl>
typename Impl::DynInstPtr
DataflowQueueBank<Impl>::wakeupInstsFromBank()
{
    if (pendingInstValid) {
        DPRINTF(DQWake, "Return pending inst[%llu]\n", pendingInst->seqNum);
        return pendingInst;
    }

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
        DPRINTF(DQ, "pointer: (%i %i) (%i)\n", ptr.index, ptr.bank, ptr.op);
        if (!ptr.valid) continue;
        DPRINTF(DQ, "pointer: (%i %i) (%i)\n", ptr.index, ptr.bank, ptr.op);
        if (!instArray.at(ptr.index) || instArray.at(ptr.index)->isSquashed()) {
            DPRINTF(DQWake, "Wakeup ignores null inst @%d\n", ptr.index);
            if (anyPending) {
                ptr.valid = false;
            }
            continue;
        }

        DPRINTF(DQ, "pointer: (%i %i) (%i)\n", ptr.index, ptr.bank, ptr.op);
        auto &inst = instArray[ptr.index];
        DPRINTF(DQWake, "Pointer (%i %i) (%i) working on inst[%llu]\n",
                ptr.bank, ptr.index, ptr.op, inst->seqNum);

        if (op == 0 && !(ptr.wkType == WKPointer::WKMem ||
                    ptr.wkType == WKPointer::WKMisc || ptr.wkType == WKPointer::WKOrder)) {
            DPRINTF(DQWake, "Wakeup ignores op wakeup pointer to Dest\n");
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
            DPRINTF(DQWake, "Mem inst [%llu] received wakeup pkt\n",
                    inst->seqNum);
            if (!inst->fuGranted) {
                first = inst;
            } else {
                DPRINTF(DQWake, "But ignore it because inst [%llu] has already been granted\n",
                        inst->seqNum);
            }

        } else if (ptr.wkType == WKPointer::WKOrder) {
            inst->orderDepReady = true;
            DPRINTF(DQWake, "Mem inst [%llu] (has order dep) received wakeup pkt\n",
                    inst->seqNum);
            if (!inst->fuGranted) {
                handle_wakeup = true;
            } else {
                DPRINTF(DQWake, "But ignore it because inst [%llu] has already been granted\n",
                        inst->seqNum);
            }

        } else { //  if (ptr.wkType == WKPointer::WKOp)
            handle_wakeup = true;
            inst->opReady[op] = true;
            if (op != 0) {
                FFRegValue v = dq->readReg(inst->getSrcPointer(op - 1), DQPointer(ptr));
                DPRINTF(FFExec, "Setting src reg[%i] of inst[%llu] to %llu\n",
                        op-1, inst->seqNum, v.i);
                inst->setSrcValue(op - 1, v);
            }

            for (unsigned xop = op + 1; xop <= 3; xop++) {
                if (inst->identicalTo[xop] && inst->identicalTo[xop] == op) {
                    inst->opReady[xop] = true;
                    FFRegValue v = dq->readReg(inst->getSrcPointer(xop - 1), DQPointer(ptr));
                    DPRINTF(FFExec, "Setting src reg[%i] of inst[%llu] to %llu\n",
                            xop-1, inst->seqNum, v.i);
                    inst->setSrcValue(xop - 1, v);
                }
            }
            DPRINTF(DQWake, "Mark op[%d] of inst [%llu] ready\n", op, inst->seqNum);
        }

        if (handle_wakeup) {
            if (nearlyWakeup[ptr.index]) {
                assert(inst->numBusyOps() == 0);
                DPRINTF(DQWake, "inst [%llu] is ready to waken up\n", inst->seqNum);
                wakeup_count++;
                if (!first) {
                    DPRINTF(DQWake, "inst [%llu] is the gifted one in this bank\n", inst->seqNum);
                    first = inst;
                    first_index = ptr.index;
                    if (anyPending) {
                        DPRINTF(DQWake, "Cleared pending pointer (%i %i) (%i)\n",
                                ptr.bank, ptr.index, ptr.op);
                        ptr.valid = false;
                    }
                } else {
                    DPRINTF(DQWake, "inst [%llu] has no luck in this bank\n", inst->seqNum);
                    need_pending_ptr[op] = true;
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
        DPRINTF(DQWake, "Setting pending inst[%llu]\n", first->seqNum);
        pendingInst = first; // if it's rejected, then marks valid in tick()
    }

    if (wakeup_count == 0) {
        first = tryWakeTail();
    }

    if (wakeup_count == 0 && !first) {
        first = tryWakeDirect();
    }
    if (first) {
        DPRINTF(DQWake, "Wakeup valid inst: %llu\n", first->seqNum);
    }

    return first;
}

template<class Impl>
const std::vector<DQPointer>
DataflowQueueBank<Impl>::readPointersFromBank()
{
    DPRINTF(DQWake, "Reading pointers from banks\n");
    DPRINTF(DQ, "Tail: %i", tail);
    for (unsigned op = 0; op < nOps; op++) {
        const auto &ptr = inputPointers[op];
        auto &optr = outputPointers[op];

        if (!ptr.valid) {
            optr.valid = false;
        } else {
            if (!instArray[ptr.index]) {
                DPRINTF(FFSquash, "Ignore forwarded pointer to (%i %i) (%i) (squashed)\n",
                        ptr.bank, ptr.index, ptr.op);
                optr.valid = false;
            } else {
                if (op == 0 ) {
                    outputPointers[0].valid = false; // dest register should never be forwarded
                }

                if (instArray[ptr.index]->pointers[op].valid) {
                    if (op != 0 || instArray[ptr.index]->destReforward) {

                        if (op == 0 && instArray[ptr.index]->destReforward) {
                            DPRINTF(DQ, "Although its wakeup ptr to dest,"
                                    " it is still forwarded because of destReforward flag\n");
                        }
                        optr = instArray[ptr.index]->pointers[op];
                        if (op == 0 && optr.valid && instArray[ptr.index]->destReforward) {
                            instArray[ptr.index]->destReforward = false;
                        }
                        DPRINTF(FFSquash, "Put forwarding wakeup Pointer (%i %i) (%i)"
                                          " to outputPointers\n",
                                optr.bank, optr.index, optr.op);
                    }
                } else {
                    optr.valid = false;
                }
            }
        }
    }
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
    flag &= !pendingInstValid;
    if (pendingInstValid) {
        assert(pendingInst);
        DPRINTF(DQWake, "Pending inst[%llu]\n", pendingInst->seqNum);
    }
    anyPending = !flag;
    if (!flag) {
        DPRINTF(DQWake, "DQ bank cannot serve new insts!\n");
    }
    return flag;
}

template<class Impl>
bool DataflowQueueBank<Impl>::wakeup(WKPointer pointer)
{
    DPRINTF(DQWake, "Mark waking up: (%i %i) (%i) in inputPointers\n",
            pointer.bank, pointer.index, pointer.op);
    auto op = pointer.op;
    assert(!inputPointers[op].valid);
    inputPointers[op] = pointer;
    return true;
}

template<class Impl>
void DataflowQueueBank<Impl>::checkPending()
{
    if (pendingInst && !instGranted) {
        DPRINTF(DQWake, "Setting pending inst valid because of inst[%llu]\n",
                pendingInst->seqNum);
        pendingInstValid = true;
    }
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
          pendingInst(nullptr),
          pendingInstValid(false),
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
        DPRINTF(DQWake, "inst [%llu] becomes ready on dispatch\n", inst->seqNum);
        directReady++;
        nearlyWakeup.set(index);
        readyQueue.push_back(pointer);
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
}

template<class Impl>
void DataflowQueueBank<Impl>::clearPending()
{
    pendingInstValid = false;
    pendingInst = nullptr;
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
void DataflowQueues<Impl>::tick()
{
    // fu preparation
    for (auto &wrapper: fuWrappers) {
        wrapper.startCycle();
    }

    readQueueHeads();

    digestForwardPointer();

    DPRINTF(DQ, "Setup fu requests\n");
    // For each bank
    //  get ready instructions produced by last tick from time struct
    for (unsigned i = 0; i < nBanks; i++) {
        fu_requests[i].valid = fromLastCycle->instValids[i];
        if (fromLastCycle->instValids[i]) {

            DPRINTF(DQ, "inst[%d] valid, &inst: %p\n",
                    i, fromLastCycle->insts[i]);

            fu_requests[i].payload = fromLastCycle->insts[i];
            fu_requests[i].destBits = coordinateFU(fromLastCycle->insts[i], i);
        }
    }
    // For each bank

    //  For each valid ready instruction compete for a FU via the omega network
    //  FU should ensure that this is no write port hazard next cycle
    //      If FU grant an inst, pass its direct child pointer to this FU's corresponding Pointer Queue
    //      to wake up the child one cycle before its value arrived

    DPRINTF(DQ, "FU selecting ready insts from banks\n");
    DPRINTF(DQWake, "FU req packets:\n");
    dumpInstPackets(fu_req_ptrs);
    fu_granted_ptrs = bankFUNet.select(fu_req_ptrs);
    for (unsigned b = 0; b < nBanks; b++) {
        assert(fu_granted_ptrs[b]);
        if (!fu_granted_ptrs[b]->valid || !fu_granted_ptrs[b]->payload) {
            continue;
        }
        DynInstPtr &inst = fu_granted_ptrs[b]->payload;

        DPRINTF(DQWake, "Inst[%d] selected by fu%u\n", inst->seqNum, b);

        if (inst->isSquashed()) {
            DPRINTF(FFSquash, "Skip squashed inst[%llu]\n", inst->seqNum);
            fu_req_granted[fu_granted_ptrs[b]->source] = true;
            dqs[fu_granted_ptrs[b]->source]->clearPending();

        } else {
            bool can_accept = fuWrappers[b].canServe(inst);
            if (can_accept) {
                fu_req_granted[fu_granted_ptrs[b]->source] = true;
                fuWrappers[b].consume(inst);
                dqs[fu_granted_ptrs[b]->source]->clearPending();
            } else if (opLat[inst->opClass()] > 1){
                llBlockedNext = true;
            }
        }
    }

    DPRINTF(DQ, "Tell dq banks who was granted\n");
    // mark it for banks
    for (unsigned b = 0; b < nBanks; b++) {
        dqs[b]->instGranted = fu_req_granted[b];
    }

    replayMemInsts();

    DPRINTF(DQ, "Reading wk pointers from banks to wake queus\n");
    // push forward pointers to queues
    for (unsigned b = 0; b < nBanks; b++) {
        for (unsigned op = 0; op <= 3; op++) {
            auto &ptr = fromLastCycle->pointers[b * nOps + op];
            if (ptr.valid) {
                DPRINTF(DQWake, "Push WakePtr (%i %i) (%i) to wakequeue[%u]\n",
                        ptr.bank, ptr.index, ptr.op, b);
                wakeQueues[b * nOps + op].push_back(WKPointer(ptr));
                numPendingWakeups++;
                DPRINTF(DQWake, "After push, numPendingWakeups = %u\n", numPendingWakeups);
                if (wakeQueues[b * nOps + op].size() > maxQueueDepth) {
                    processWKQueueFull();
                }
            }
        }

    }

    DPRINTF(DQ, "Selecting wakeup pointers from requests\n");
    // For each bank: check status: whether I can consume from queue this cycle?
    // record the state
    wakeup_granted_ptrs = wakeupQueueBankNet.select(wakeup_req_ptrs);

    for (auto &ptr : wakeup_granted_ptrs) {
        if (ptr->valid) {
            DPRINTF(DQWake, "WakePtr[%d] (pointer(%d) (%d %d)(%d)) granted\n",
                    ptr->source, ptr->payload.valid,
                    ptr->payload.bank, ptr->payload.index, ptr->payload.op);
        }
    }
    // check whether each bank can really accept
    for (unsigned b = 0; b < nBanks; b++) {
        if (dqs[b]->canServeNew()) {
            for (unsigned op = 0; op <= 3; op++) {
                const auto &pkt = wakeup_granted_ptrs[b * nOps + op];

                DPRINTF(DQWake, "granted[%i.%i]: dest:(%llu) (%i) (%i %i) (%i)\n",
                        b, op, pkt->destBits.to_ulong(),
                        pkt->valid,
                        pkt->payload.bank, pkt->payload.index, pkt->payload.op);

                if (!pkt->valid) {
                    continue;
                }
                WKPointer &ptr = pkt->payload;
                wake_req_granted[pkt->source] = true;
                assert(dqs[b]->wakeup(ptr));

                // pop accepted pointers.
                assert(!wakeQueues[pkt->source].empty());
                if (wakeQueues[pkt->source].size() == numPendingWakeupMax) {
                    numPendingWakeupMax--;
                }
                DPRINTF(DQWake, "Pop WakePtr (%i %i) (%i)from wakequeue[%u]\n",
                        ptr.bank, ptr.index, ptr.op, pkt->source);
                wakeQueues[pkt->source].pop_front();
                numPendingWakeups--;
                DPRINTF(DQWake, "After pop, numPendingWakeups = %u\n", numPendingWakeups);
            }
        }
    }

    // todo: write forward pointers from bank to time buffer!
    for (unsigned b = 0; b < nBanks; b++) {
        const std::vector<DQPointer> forward_ptrs = dqs[b]->readPointersFromBank();
        for (unsigned op = 0; op <= 3; op++) {
            toNextCycle->pointers[b * nOps + op] = forward_ptrs[op];
        }
    }

    DPRINTF(DQ, "Wrappers setting instructions to be waken up\n");
    for (auto &wrapper: fuWrappers) {
        wrapper.setWakeup();
    }

    for (unsigned b = 0; b < nBanks; b++) {
        const DQPointer &src = fuWrappers[b].toWakeup[SrcPtr];
        const DQPointer &dest = fuWrappers[b].toWakeup[DestPtr];

        if (dest.valid) {
            DPRINTF(DQWake, "Got wakeup pointer to (%d %d)(%d), pushed to wakeQueue[%i]\n",
                    dest.bank, dest.index, dest.op, b * nOps);
            wakeQueues[b * nOps].push_back(WKPointer(dest));
            numPendingWakeups++;
            DPRINTF(DQWake, "After push, numPendingWakeups = %u\n", numPendingWakeups);

        }

        if (src.valid) {
            DPRINTF(DQWake, "Got inverse wakeup pointer to (%d %d)(%d), pushed to wakeQueue[%i]\n",
                    src.bank, src.index, src.op, b * nOps + 2);
            wakeQueues[b * nOps + 2].push_back(WKPointer(src));
            numPendingWakeups++;
            DPRINTF(DQWake, "After push, numPendingWakeups = %u\n", numPendingWakeups);

            unsigned used = pointer2uint(src);
            if (validPosition(used) && logicallyLET(used, oldestUsed)) {
                oldestUsed = used;
            }
        }

        if (wakeQueues[b * nOps].size() > maxQueueDepth ||
                wakeQueues[b * nOps + 2].size() > maxQueueDepth) {
            processWKQueueFull();
        }
    }

    for (auto &wrapper: fuWrappers) {
        wrapper.executeInsts();
    }

    // todo: write insts from bank to time buffer!
    for (unsigned i = 0; i < nBanks; i++) {
        DynInstPtr inst;

        inst = dqs[i]->wakeupInstsFromBank();
        // todo ! this function must be called after readPointersFromBank(),

        // fixup for late-arrival fw pointers
//        if (dqs[i]->extraWakeupPointer.valid) {
//            extraWakeup(WKPointer(dqs[i]->extraWakeupPointer));
//        }

        // read fu requests from banks
        //  and set fields for packets
        //  fu_req_ptrs = xxx
        //  update valid, dest bits and payload only (source need not be changed)
        toNextCycle->instValids[i] = !!inst && !inst->fuGranted;
        toNextCycle->insts[i] = inst;
        if (toNextCycle->instValids[i]) {
            DPRINTF(Omega, "notNext cycle inst[%d] valid, &inst: %p\n",
                    i, toNextCycle->insts[i]);
        }
    }

    // For each bank, check
    //  whether there's an inst to be waken up
    //  whether there are multiple instruction to wake up, if so
    //      buffer it and reject further requests in following cycles
    DPRINTF(DQ, "Bank check pending\n");
    for (auto bank: dqs) {
        bank->checkPending();
    }
}

template<class Impl>
void DataflowQueues<Impl>::cycleStart()
{
//    fill(wakenValids.begin(), wakenValids.end(), false);
    fill(fu_req_granted.begin(), fu_req_granted.end(), false);
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
        bankFUNet(nBanks, true),
        wakeupQueueBankNet(nBanks * nOps, true),
        pointerQueueBankNet(nBanks * nOps, true),

        fu_requests(nBanks),
        fu_req_granted(nBanks),
        fu_req_ptrs(nBanks),
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
        fuPointer(nBanks),




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
        PendingFwPointerThreshold(params->PendingFwPointerThreshold)

{
    // init inputs to omega networks
    for (unsigned b = 0; b < nBanks; b++) {
        DQPacket<DynInstPtr> &fu_pkt = fu_requests[b];
        fu_pkt.valid = false;
        fu_pkt.source = b;

        fu_req_ptrs[b] = &fu_requests[b];

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
        fuPointer[b] = b;
    }
    memDepUnit.init(params, DummyTid);
    memDepUnit.setIQ(this);
    resetState();
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
unsigned DataflowQueues<Impl>::pointer2uint(DQPointer ptr) const
{
    return (ptr.index << bankWidth) | ptr.bank;
}

template<class Impl>
boost::dynamic_bitset<>
DataflowQueues<Impl>::coordinateFU(
        DataflowQueues::DynInstPtr &inst, unsigned bank)
{
    // find another FU group with desired capability.
    DPRINTF(FUW, "Coordinating bank %u with llb:%i\n", bank, llBlocked);
    if (llBlocked) {
        auto fu_bitmap = fuGroupCaps[inst->opClass()];
        do {
            fuPointer[bank] = (fuPointer[bank] + 1) % nBanks;
        } while (!fu_bitmap[fuPointer[bank]]);
        DPRINTF(FUW, "switch to req fu %u\n", fuPointer[bank]);

    } else if (opLat[inst->opClass()] == 1) {
        fuPointer[bank] = bank;
        DPRINTF(FUW, "reset to origin\n");
    }
    return uint2Bits(fuPointer[bank]);
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
        pkt.destBits = uint2Bits(pair.dest.bank * nOps + pair.dest.op);

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
        assert(inst->isExecuted());
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
}

template<class Impl>
bool DataflowQueues<Impl>::insert(DynInstPtr &inst, bool nonSpec)
{
    // todo: send to allocated DQ position
    assert(inst);
    assert(!isFull());

    bool jumped = false;
    DQPointer allocated = inst->dqPosition;
    DPRINTF(DQ, "allocated @(%d %d)\n", allocated.bank, allocated.index);

    auto dead_inst = dqs[allocated.bank]->readInstsFromBank(allocated);
    if (dead_inst) {
        DPRINTF(FFCommit, "Dead inst[%llu] found unexpectedly\n", dead_inst->seqNum);
        assert(!dqs[allocated.bank]->readInstsFromBank(allocated));
    }
    dqs[allocated.bank]->writeInstsToBank(allocated, inst);
    if (isEmpty()) {
        tail = pointer2uint(allocated); //keep them together
        DPRINTF(DQ, "tail becomes %u to keep with head\n", tail);
        jumped = true;
        alignTails();
    }
    head = pointer2uint(allocated);

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
    if (pointers[op].valid) {
        DPRINTF(FFSquash, "Overriding previous (squashed) sibling:(%d %d) (%d)\n",
                pointers[op].bank, pointers[op].index, pointers[op].op);

        if (inst && (inst->isExecuted() || inst->opReady[op])) {
            DPRINTF(FFSquash, "And extra wakeup new sibling\n");
            extraWakeup(WKPointer(pair.payload));
            if (op == 0) {
                inst->destReforward = false;
            }

        } else if (inst && inst->fuGranted && op == 0){
            DPRINTF(FFSquash, "And mark it to wakeup new child\n");
            inst->destReforward = true;
        }
    } else if (inst && inst->opReady[op]) {
        DPRINTF(DQWake, "which has already been waken up!\n");
        extraWakeup(WKPointer(pair.payload));
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
                DPRINTF(DQWake, "Found valid wakePtr:(%i) (%i %i) (%i)\n",
                        ptr.valid, ptr.bank, ptr.index, ptr.op);
                wk_pkt.valid = ptr.valid;
                wk_pkt.payload = ptr;
                wk_pkt.destBits = uint2Bits(ptr.bank * nOps + ptr.op);
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
        DPRINTFR(DQOmega, " pair dest:(%d) (%d %d)(%d) \n",
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
    auto p = uint2Pointer(tail);
    WKPointer wk(p);
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
    return u == 0 ? queueSize : u - 1;
}

template<class Impl>
void DataflowQueues<Impl>::tryFastCleanup()
{
    // todo: clean bubbles left by squashed instructions
    auto inst = getTail();
    if (inst) {
        DPRINTF(FFSquash, "Strangely reaching fast cleanup when DQ head is not null!\n");
    }
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
    DPRINTF(FFCommit, "Fastly advance youngest ptr to %d, olddest ptr to %d\n", head, tail);
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
    insert_granted_ptrs = pointerQueueBankNet.select(insert_req_ptrs);

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
                    extraWakeup(WKPointer(pair.payload));
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
    DPRINTF(DQWake, "Push Wake (extra) Ptr (%i %i) (%i) to wakequeue[%u]\n",
            wk.bank, wk.index, wk.op, extraWKPtr * nOps + 3);
    wakeQueues[extraWKPtr * nOps + 3].push_back(wk);
    numPendingWakeups++;
    DPRINTF(DQWake, "After push, numPendingWakeups = %u\n", numPendingWakeups);
    incExtraWKptr();
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
//    bool wkPointerEmpty = true;
    for (unsigned b = 0; b < nBanks; b++) {
        for (unsigned op = 0; op < nOps; op++) {
            fwPointerEmpty &= forwardPointerQueue[b*nOps+op].empty();
//            wkPointerEmpty &= forwardPointerQueue[b*nOps+op].empty();
        }
    }
    return fwPointerEmpty;
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

    if (numPendingWakeups == 0) {
        oldestUsed = getHeadPtr();
        DPRINTF(DQ, "Set oldestUsed to youngest inst on wake queue empty\n");
    } else {
        DPRINTF(DQ, "wake queue in flight = %i, cannot reset oldestUsed\n", numPendingWakeups);
        for (unsigned b = 0; b < nBanks; b++) {
            for (unsigned op = 0; op < nOps; op++) {
                DPRINTFR(DQ, "wake queue %i.%i size: %llu\n",
                        b, op, wakeQueues[b*nOps + op].size());
            }
        }
    }

    DPRINTF(DQ, "Size of blockedMemInsts: %llu, size of retryMemInsts: %llu\n",
            blockedMemInsts.size(), retryMemInsts.size());
}

template<class Impl>
void DataflowQueues<Impl>::dumpFwQSize()
{
    DPRINTF(DQ, "fw queue in flight = %i, cannot reset oldestRef\n", numPendingFwPointers);
    for (unsigned b = 0; b < nBanks; b++) {
        for (unsigned op = 0; op < nOps; op++) {
            DPRINTFR(DQ, "fw queue %i.%i size: %llu\n",
                    b, op, forwardPointerQueue[b*nOps + op].size());
        }
    }
}

template<class Impl>
void DataflowQueues<Impl>::maintainOldestUsed()
{
    if (!validPosition(oldestUsed)) {
        oldestUsed = getTailPtr();
        DPRINTF(DQ, "Set oldestUsed to tail: %u\n", oldestUsed);
    }
}

template<class Impl>
InstSeqNum DataflowQueues<Impl>::clearHalfWKQueue()
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
                if (!validPosition(p)) {
                    panic("WTF, a pointer in wk queue is not valid!\n");
                }
                if (logicallyLT(p, oldest_to_squash)) {
                    oldest_to_squash = p;
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
    if (inst) {
        return inst->seqNum;
    } else {
        return std::numeric_limits<InstSeqNum>::max();
    }
}

template<class Impl>
InstSeqNum DataflowQueues<Impl>::clearHalfFWQueue()
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
                if (!validPosition(p)) {
                    panic("WTF, a receiver pointer in fw queue is not valid!\n");
                }
                if (logicallyLT(p, oldest_to_squash)) {
                    oldest_to_squash = p;
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
    if (inst) {
        return inst->seqNum;
    } else {
        return std::numeric_limits<InstSeqNum>::max();
    }
}

template<class Impl>
void DataflowQueues<Impl>::processWKQueueFull()
{
    halfSquash = true;

    DPRINTF(DQ, "Dump before clear:\n");
    if (Debug::DQ) {
        dumpQueues();
    }

    halfSquashSeq = clearHalfWKQueue();

    bool clear_fw_queue = false;
    for (auto q: forwardPointerQueue) {
        if (q.size() > maxQueueDepth/2) {
            clear_fw_queue = true;
        }
    }

    if (clear_fw_queue) {
        InstSeqNum another_seq = clearHalfFWQueue();
        if (another_seq < halfSquashSeq) {
            halfSquashSeq = another_seq;
        }
    }

    DPRINTF(DQ, "Dump after clear:\n");
    if (Debug::DQ) {
        dumpQueues();
    }
}

template<class Impl>
void DataflowQueues<Impl>::processFWQueueFull()
{
    halfSquash = true;

    DPRINTF(DQ, "Dump before clear:\n");
    if (Debug::DQ) {
        dumpQueues();
    }

    halfSquashSeq = clearHalfFWQueue();

    bool clear_fw_queue = false;
    for (auto q: wakeQueues) {
        if (q.size() > maxQueueDepth/2) {
            clear_fw_queue = true;
        }
    }

    if (clear_fw_queue) {
        InstSeqNum another_seq = clearHalfWKQueue();
        if (another_seq < halfSquashSeq) {
            halfSquashSeq = another_seq;
        }
    }

    DPRINTF(DQ, "Dump after clear:\n");
    if (Debug::DQ) {
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

}

#include "cpu/forwardflow/isa_specific.hh"

template class FF::DataflowQueues<FFCPUImpl>;
