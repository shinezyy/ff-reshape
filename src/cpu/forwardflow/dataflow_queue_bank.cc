//
// Created by zyy on 2020/1/15.
//

#include "dataflow_queue_bank.hh"
#include "debug/DQ.hh"
#include "debug/DQGDL.hh"
#include "debug/DQGOF.hh"
#include "debug/DQV2.hh"
#include "debug/DQWake.hh"
#include "debug/DQWrite.hh"
#include "debug/FFDisp.hh"
#include "debug/FFExec.hh"
#include "debug/FFSquash.hh"
#include "debug/NoSQSMB.hh"
#include "debug/ObExec.hh"
#include "debug/ObFU.hh"
#include "debug/RSProbe1.hh"
#include "debug/Reshape.hh"

namespace FF {

using boost::dynamic_bitset;

template<class Impl>
DataflowQueueBank<Impl>::DataflowQueueBank(
        DerivFFCPUParams *params, unsigned bank_id,
        DQ *dq, DQTop *_top)
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
          outputPointers(nOps, WKPointer(nullDQPointer)),
          tail(0),
          readyQueueSize(params->readyQueueSize),
          prematureFwPointers(depth)
{
    setTop(_top);
    for (auto &line: prematureFwPointers) {
        for (auto &ptr: line) {
            ptr.valid = false;
        }
    }
    for (auto &ptr: localWKPointers) {
        ptr.valid = false;
    }
    bankID = bank_id;
    std::ostringstream s;
    s << "DQBank" << bankID;
    _name = dq->name() + '.' + s.str();
//    printf("Top@Init: %p\n", top);
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

    for (auto &pointers: prematureFwPointers) {
        for (auto &ptr: pointers) {
            ptr.valid =false;
        }
    }
}

template<class Impl>
void
DataflowQueueBank<Impl>::erase(BasePointer p, bool markSquashed)
{
    if (markSquashed && instArray[p.index]) {
        DPRINTF(FFSquash, "Squash and erase inst[%lu] @" ptrfmt "\n",
                instArray[p.index]->seqNum, extptr(p));
        instArray[p.index]->setSquashed();
    }

    for (unsigned op = 0; op < nOps; op++) {
        prematureFwPointers[p.index][op].valid = false;
    }

    instArray[p.index] = nullptr;
    nearlyWakeup.reset(p.index);
    RegWriteValid++;
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
    RegWriteValid++;
}

template<class Impl>
typename Impl::DynInstPtr
DataflowQueueBank<Impl>::tryWakeTail()
{
    DynInstPtr tail_inst = instArray.at(tail);
    RegReadValid++;
    if (!tail_inst) {
        DPRINTF(DQWake, "Tail[%d] is null, skip\n", tail);
        return nullptr;
    }
    if (tail_inst->isNormalBypass()) {
        DPRINTF(DQWake, "Tail[%d] is normal bypassing, skip\n", tail);
        return nullptr;
    }

    if (tail_inst->inReadyQueue || tail_inst->fuGranted ||
        (tail_inst->isForwarder() && tail_inst->isExecuted())) {
        DPRINTF(DQWake, "Tail[%d] has been granted, skip\n", tail);
        return nullptr;
    }
    RegReadNbusy++;
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
    const BasePointer &ptr = readyQueue.front();
    DynInstPtr inst = instArray.at(ptr.index);
    if (!inst) {
        DPRINTF(DQWake, "Inst at [%d] is null (squashed), skip\n", tail);
        readyQueue.pop_front();
        return nullptr;
    }

    if (inst->isNormalBypass()) {
        DPRINTF(DQWake, "Tail[%d] is normal bypassing, skip\n", tail);
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
    QueueReadReadyInstBuf++;
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

    std::array<bool, 4> need_pending_ptr{};

    RegReadRxBuf += nOps;
    RegReadValid++;
    RegReadNbusy++;

    for (unsigned op = 0; op < nOps; op++) {
        auto &ptr = pointers[op];

        if (!ptr.valid) {
            DPRINTF(DQV2, "skip pointer:" ptrfmt "\n", extptr(ptr));
            continue;
        }
        DPRINTF(DQV2, "pointer:" ptrfmt "\n", extptr(ptr));

        if (!instArray.at(ptr.index) || instArray.at(ptr.index)->isSquashed()) {
            DPRINTF(DQWake, "Wakeup ignores null inst @%d\n", ptr.index);
            if (anyPending) {
                ptr.valid = false;
            }
            continue;
        }

        auto &inst = instArray[ptr.index];

        DPRINTF(DQWake||Debug::RSProbe1, "Pointer" ptrfmt "working on inst[%llu]",
                extptr(ptr), inst->seqNum);

        if (!ptr.hasVal) {
            DPRINTFR(DQWake, "\n");
        } else {
            DPRINTFR(DQWake, "with value: %ld (%lu)\n", ptr.val.i, ptr.val.i);
        }

        if (inst->dqPosition.term != ptr.term) {
            DPRINTF(DQWake, "Term not match on WK: ptr: %d, inst: %d\n",
                    ptr.term, inst->dqPosition.term);
            if (anyPending) {
                ptr.valid = false;
            }
            continue;
        }

        if (op == 0 && !(ptr.wkType == WKPointer::WKMem ||
                         ptr.wkType == WKPointer::WKMisc ||
                         ptr.wkType == WKPointer::WKOrder ||
                         ptr.wkType == WKPointer::WKLdReExec)) {
            DPRINTF(DQWake, "Mark received dest after wakeup pointer arrives at Dest\n");
            inst->receivedDest = true;
            if (anyPending) {
                ptr.valid = false;
            }
            continue;
        }


        DPRINTF(NoSQSMB, "ptr.wkType == WKPointer::WKBypass: %i, inst->isNormalBypass(): %i\n",
                ptr.wkType == WKPointer::WKBypass, inst->isNormalBypass());

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
            if (inst->isLoad() && inst->isNormalBypass()) {
                DPRINTF(DQWake, "Bypassing for inst [%llu] is canceled\n", inst->seqNum);
                inst->bypassOp = 0;
            }

        } else if (ptr.wkType == WKPointer::WKLdReExec) {
            assert(inst->isLoad());
            bool mem_acc_not_issued = inst->numBusyOps() || !inst->memOpFulfilled() || !inst->fuGranted;
            bool not_normal_bypass = !inst->bypassOp || (inst->bypassOp && inst->dependOnBarrier);
            if (not_normal_bypass && mem_acc_not_issued) {
                DPRINTF(DQWake, "Now load inst [%llu] is tail and we dont need to "
                                "verify it\n", inst->seqNum);
                inst->loadVerified = true;

            } else if (!inst->loadVerified) {
                DPRINTF(DQWake, "Will verify load inst [%llu]\n", inst->seqNum);
                handle_wakeup = true;
                inst->loadVerifying = true;
                inst->fuGranted = false;
            } else {
                DPRINTF(DQWake, "Inst [%llu] has already been verified\n", inst->seqNum);
            }

        } else if (ptr.wkType == WKPointer::WKBypass && inst->isNormalBypass()) {
            assert(op == memBypassOp);
            inst->opReady[op] = true;
            inst->bypassVal = ptr.val;
            inst->orderDepReady = true;
            handle_wakeup = true;

        } else { //  if (ptr.wkType == WKPointer::WKOp)
            // NOTE: With NoSQ, WKOp might carry memory response!
            handle_wakeup = true;
            inst->opReady[op] = true;
            if (op != 0) {
                assert(inst->indirectRegIndices.at(op).size() ||
                        op == inst->bypassOp);
                assert(ptr.hasVal || (inst->bypassOp && !inst->isNormalBypass()));

                if (inst->indirectRegIndices.at(op).size()) {
                    FFRegValue v = ptr.val;

                    for (const auto i: inst->indirectRegIndices.at(op)) {
                        DPRINTF(FFExec, "Setting src reg[%i] of inst[%llu] to %llu\n",
                                i, inst->seqNum, v.i);
                        inst->setSrcValue(i, v);
                    }
                    SRAMWriteValue++;
                } else {
                    // todo:
                    assert(inst->bypassOp);
                    assert(!inst->isNormalBypass());
                    inst->orderDepReady = true;
                }
            }
            DPRINTF(DQWake||Debug::RSProbe1,
                    "Mark op[%d] of inst [%llu] ready\n", op, inst->seqNum);
        }

        if (handle_wakeup) {
            if (nearlyWakeup[ptr.index]) {
                // assertion
                assert(inst->numBusyOps() == 0);

                // action
                wakeup_count++;

                // log
                DPRINTF(DQWake || Debug::ObExec, "inst [%llu]: %s is ready to waken up\n",
                        inst->seqNum, inst->staticInst->disassemble(inst->instAddr()));
                if (ptr.wkType == WKPointer::WKLdReExec) {
                    DPRINTF(DQWake || Debug::ObExec || Debug::NoSQSMB,
                            "Waking up inst [%llu](normal bypassing) to verify it\n",
                            inst->seqNum);
                }

                if (inst->readyTick == 0) {
                    inst->readyTick = curTick();
                }
                if (!first) {
                    first = inst;
                    first_index = ptr.index;

                    // counting
                    if (ptr.wkType != WKPointer::WKOp || ptr.isFwExtra) {
                        inst->wkDelayedCycle = std::max((int) 0, ((int) ptr.queueTime) - 1);
                    } else {
                        inst->wkDelayedCycle = ptr.queueTime;
                    }
                    dq->countCycles(inst, &ptr);

                    // log
                    DPRINTF(DQWake || Debug::RSProbe1,
                            "inst [%llu] is the gifted one in this bank\n",
                            inst->seqNum);

                    if (anyPending) {
                        DPRINTF(DQWake, "Cleared pending pointer (%i %i) (%i)\n",
                                ptr.bank, ptr.index, ptr.op);
                        ptr.valid = false;
                    }
                } else {
                    need_pending_ptr[op] = true;

                    // counting
                    inst->readyInBankDelay += 1;

                    // log
                    DPRINTF(DQWake || Debug::RSProbe1,
                            "inst [%llu] has no luck in this bank\n", inst->seqNum);
                }

            } else {
                unsigned busy_count = inst->numBusyOps();
                RegReadNbusy++;
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
                RegWriteRxBuf++;
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
        SRAMReadInst++;
        SRAMReadValue += nOps;
        DPRINTF(DQWake, "Wakeup valid inst: %llu\n", first->seqNum);
        DPRINTF(DQWake, "Setting pending inst[%llu]\n", first->seqNum);
    }

    return first;
}

template<class Impl>
std::vector<WKPointer>
DataflowQueueBank<Impl>::readPointersFromBank()
{
    DPRINTF(DQWake, "Reading pointers from banks\n");
    DPRINTF(DQ, "Tail: %i\n", tail);

    bool grab_from_local_fw = dq->NarrowXBarWakeup && dq->NarrowLocalForward && anyPending;

    SRAMReadPointer += nOps;
    for (unsigned op = 0; op < nOps; op++) {
        auto &ptr = grab_from_local_fw ? pendingWakeupPointers[op] : inputPointers[op];
        auto &optr = outputPointers[op];
        RegReadRxBuf++;

        if (!ptr.valid) {
            optr.valid = false;

        } else if (grab_from_local_fw && !ptr.isLocal) {
            optr.valid = false;

        } else {
            DPRINTF(DQ || Debug::DQWake, "op = %i\n", op);
            DPRINTF(DQWake, "Reading forward pointer at" ptrfmt "\n",
                    extptr(ptr));
            const auto &inst = instArray.at(ptr.index);
            if (!inst) {
                DPRINTF(FFSquash, "Ignore forwarded pointer to" ptrfmt "(squashed)\n",
                        extptr(ptr));
                optr.valid = false;

            } else if (inst->dqPosition.term != ptr.term) {
                DPRINTF(DQWake, "Term not match on FW: ptr: %d, inst: %d\n",
                        ptr.term, inst->dqPosition.term);
                optr.valid = false;

            } else if (ptr.wkType == WKPointer::WKLdReExec) {
                DPRINTF(DQWake || Debug::NoSQSMB, "Load Re-exec does not generate fw pointer\n");
                optr.valid = false;

            } else if (ptr.wkType == WKPointer::WKMem) {
                DPRINTF(DQWake || Debug::NoSQSMB, "Mem Replay does not generate fw pointer\n");
                optr.valid = false;

            } else {
                if (op == 0) {
                    outputPointers[0].valid = false; // dest register should never be forwarded
                }

                if (inst->pointers[op].valid) {
                    DPRINTF(DQ, "op now = %i\n", op);
                    if (op != 0 || inst->destReforward) {
                        optr = inst->pointers[op];

                        optr.hasVal = true;
                        if (op == 0 && inst->destReforward) {
                            DPRINTF(DQWake, "Although it's wakeup ptr to dest,"
                                        " it is still forwarded because of destReforward flag\n");
                            optr.val = inst->getDestValue();
                        } else {
                            optr.val = ptr.val;
                        }

                        optr.ssrDelay = ptr.ssrDelay + 1;
                        if (op == 0 && optr.valid && inst->destReforward) {
                            inst->destReforward = false;
                        }

                        if (inst->isLoad() && op == memBypassOp) {
                            optr.wkType = WKPointer::WKBypass;
                        }

                        if (grab_from_local_fw && ptr.isLocal) {
                            ptr.isLocal = false;
                            DPRINTF(DQWake, "Pointer (%i) (%i %i) (%i) isLocal <- false\n",
                                    optr.valid, optr.bank, optr.index, optr.op);
                        }
                        DPRINTF(FFSquash || Debug::DQWake, "Put forwarding wakeup Pointer" ptrfmt
                                          " to outputPointers with val: (%i) (%llu)\n",
                                extptr(optr), optr.hasVal, optr.val.i);
                    }
                } else {
                    DPRINTF(DQWake, "Skip invalid pointer" ptrfmt "on op[%i] of inst[%llu]\n",
                            extptr(inst->pointers[op]), op, inst->seqNum);
                    optr.valid = false;
                    if (grab_from_local_fw && ptr.isLocal) {
                        ptr.isLocal = false;
                        DPRINTF(DQWake, "Pointer" ptrfmt "isLocal <- false\n", extptr(optr));
                    }
                }

                if (inst->isNormalBypass() && (inst->bypassOp == op) && inst->pointers[0].valid) {
                    auto wk_ptr = WKPointer(inst->pointers[0]);
                    wk_ptr.hasVal = true;
                    wk_ptr.val = ptr.val;
                    DPRINTF(DQWake,
                            "Bypassing load from " ptrfmt " to " ptrfmt "\n",
                            extptr(ptr), extptr(wk_ptr));
                    dq->extraWakeup(wk_ptr);
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
    RegWriteRxBuf++;

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
typename DataflowQueueBank<Impl>::DynInstPtr
DataflowQueueBank<Impl>::readInstsFromBank(const BasePointer &pointer) const
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
        const BasePointer &pointer, DataflowQueueBank::DynInstPtr &inst)
{
    DPRINTF(DQWrite || Debug::FFDisp, "insert inst[%llu] to" ptrfmt "\n",
            inst->seqNum, extptr(pointer));
    auto index = pointer.index;
    assert(!instArray[index]);
    instArray[index] = inst;

    for (unsigned i = 0; i < nOps; i++) {
        auto &ptr = prematureFwPointers[index][i];
        if (ptr.valid && ptr.term == inst->dqPosition.term) {
            inst->pointers[i] = ptr;
            DPRINTF(DQWrite || Debug::FFDisp, "Adapted premature pointer" ptrfmt "\n",
                    extptr(ptr));
        }

        ptr.valid = false;
    }
//    if (inst->isLoad() && inst->pointers[memBypassOp].valid) {
//        inst->bypassOp = memBypassOp;
//    }

    SRAMWriteInst++;
    SRAMWriteValue += nOps;

    DPRINTF(DQWrite || Debug::FFDisp, "Tail before writing: %u\n", tail);
    if (!instArray[tail]) {
        tail = index;
        DPRINTF(DQWrite || Debug::FFDisp, "Tail becomes %u\n", tail);
    }
}

template<class Impl>
void DataflowQueueBank<Impl>::checkReadiness(BasePointer pointer)
{
    auto index = pointer.index;
    assert(instArray[index]);
    DynInstPtr &inst = instArray[index];

    unsigned busy_count = inst->numBusyOps();

    if (busy_count == 0) {
        DPRINTF(DQWake||Debug::RSProbe1 || Debug::ObExec,
                "inst [%llu]: %s becomes ready on dispatch\n",
                inst->seqNum,
                inst->staticInst->disassemble(inst->instAddr()));
        directReady++;
        nearlyWakeup.set(index);
        readyQueue.push_back(pointer);
        QueueWriteReadyInstBuf++;
        DPRINTF(DQWake, "Push" ptrfmt "into ready queue\n", extptr(pointer));
    }
    if (busy_count == 1) {
        DPRINTF(DQWake, "inst [%llu] becomes nearly waken up\n", inst->seqNum);
        nearlyWakeup.set(index);
        RegWriteNbusy++;
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
    DPRINTF(DQWrite||Debug::DQGDL, "Tail set to %u\n", tail);
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
        panic("Clearing non-existing inst[%lu] in DQG%i.Bank%i",
                inst->seqNum, dq->groupID, bankID);
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

//    printf("Top@regStats: %p\n", top);
    SRAMWriteInst
        .name(name() + ".SRAMWriteInst")
        .desc("SRAMWriteInst");
    SRAMReadInst
        .name(name() + ".SRAMReadInst")
        .desc("SRAMReadInst");
    SRAMReadPointer
        .name(name() + ".SRAMReadPointer")
        .desc("SRAMReadPointer");
    SRAMWriteValue
        .name(name() + ".SRAMWriteValue")
        .desc("SRAMWriteValue");
    SRAMReadValue
        .name(name() + ".SRAMReadValue")
        .desc("SRAMReadValue");
    RegWriteValid
        .name(name() + ".RegWriteValid")
        .desc("RegWriteValid");
    RegReadValid
        .name(name() + ".RegReadValid")
        .desc("RegReadValid");
    RegWriteNbusy
        .name(name() + ".RegWriteNbusy")
        .desc("RegWriteNbusy");
    RegReadNbusy
        .name(name() + ".RegReadNbusy")
        .desc("RegReadNbusy");
    RegWriteRxBuf
        .name(name() + ".RegWriteRxBuf")
        .desc("RegWriteRxBuf");
    RegReadRxBuf
        .name(name() + ".RegReadRxBuf")
        .desc("RegReadRxBuf");
    QueueReadReadyInstBuf
        .name(name() + ".QueueReadReadyInstBuf")
        .desc("QueueReadReadyInstBuf");
    QueueWriteReadyInstBuf
        .name(name() + ".QueueWriteReadyInstBuf")
        .desc("QueueWriteReadyInstBuf");

}

template<class Impl>
bool DataflowQueueBank<Impl>::hasTooManyPendingInsts()
{
    return readyQueue.size() >= readyQueueSize;
}

template<class Impl>
void DataflowQueueBank<Impl>::squashReady(const BasePointer &squash_ptr)
{
//    DPRINTF(DQGOF, "Top: %p\n", top);
    DPRINTF(FFSquash, "Squashing ready\n");
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
            DPRINTF(ObFU, "Inst[%lu]: %s has been waiting for %i cycles\n",
                    inst->seqNum,
                    inst->staticInst->disassemble(inst->instAddr()),
                    inst->FUContentionDelay);
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
    RegWriteRxBuf += nOps;
}

}

#include "cpu/forwardflow//isa_specific.hh"

template class FF::DataflowQueueBank<FFCPUImpl>;
