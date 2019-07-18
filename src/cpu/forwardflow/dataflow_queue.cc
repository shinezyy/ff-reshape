//
// Created by zyy on 19-6-10.
//

#include "cpu/forwardflow/dataflow_queue.hh"
#include "dataflow_queue.hh"
#include "debug/DQ.hh"
#include "debug/DQB.hh"  // DQ bank
#include "debug/DQOmega.hh"
#include "debug/DQRead.hh"  // DQ read
#include "debug/DQWake.hh"  // DQ wakeup
#include "debug/DQWrite.hh"  // DQ write
#include "debug/FFCommit.hh"
#include "params/DerivFFCPU.hh"
#include "params/FFFUPool.hh"

namespace FF {

using namespace std;

using boost::dynamic_bitset;


template<class Impl>
void
DataflowQueueBank<Impl>::advanceTail()
{
    instArray.at(tail) = nullptr;
    tail = (tail + 1) % depth;
}

template<class Impl>
typename Impl::DynInstPtr
DataflowQueueBank<Impl>::tryWakeTail()
{
    DynInstPtr tail_inst = instArray.at(tail);
    if (!tail_inst) {
        return nullptr;
    }

    if (tail_inst->fuGranted) {
        return nullptr;
    }
    for (unsigned op = 1; op < nOps; op++) {
        if (tail_inst->hasOp[op] && !tail_inst->opReady[op]) {
            return nullptr;
        }
    }
    if (!tail_inst->memOpFulfilled() || !tail_inst->miscOpFulfilled()) {
        return nullptr;
    }
    DPRINTF(DQWake, "inst[%d] is ready but not wakeup!,"
            " and is waken up by head checker\n", tail_inst->seqNum);
    return tail_inst;
}

template<class Impl>
typename Impl::DynInstPtr
DataflowQueueBank<Impl>::wakeupInstsFromBank()
{
    if (pendingInstValid) {
        return pendingInst;
    }

    DynInstPtr first = nullptr;
    auto first_index = -1;
    auto first_op = 0;
    unsigned wakeup_count = 0;

    auto &pointers = anyPending ? pendingWakeupPointers : inputPointers;

    for (unsigned op = 1; op < nOps; op++) {
        auto &ptr = pointers[op];
        if (!ptr.valid) continue;

        wakeup_count++;
        if (nearlyWakeup[ptr.index] && !first) {
            first = instArray[ptr.index];
            first_index = ptr.index;
        }
    }
    if (wakeup_count > 1) {
        for (unsigned op = 1; op < nOps; op++) {
            auto &ptr = pointers[op];
            if (ptr.index != first_index) {
                pendingWakeupPointers[op] = ptr;
            }
        }
    }


    // if any pending then inputPointers are all invalid, vise versa (they are mutex)
    // todo ! this function must be called after readPointersFromBank(),
    //  because of the following lines
    pendingWakeupPointers[first_op].valid = false;
    inputPointers[first_op].valid = false;

    pendingInst = first; // if it's rejected, then marks valid in tick()

    if (wakeup_count == 0) {
        first = tryWakeTail();
    }
    if (first) {
        DPRINTF(DQB, "Wakeup valid inst: %p\n", first);
    }

    return first;
}

template<class Impl>
const std::vector<DQPointer>
DataflowQueueBank<Impl>::readPointersFromBank()
{
    outputPointers[0].valid = false; // dest register should never be forwarded
    for (unsigned op = 1; op < nOps; op++) {
        const auto &ptr = inputPointers[op];
        auto &optr = outputPointers[op];

        if (!ptr.valid) {
            optr.valid = false;
        } else {
            optr = instArray[ptr.index]->pointers[op];
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
            flag &= false;
        }
    }
    anyPending = !flag;
    return flag;
}

template<class Impl>
bool DataflowQueueBank<Impl>::wakeup(WKPointer pointer)
{
    auto op = pointer.op;
    assert(!inputPointers[op].valid);
    inputPointers[op] = pointer;
    return true;
}

template<class Impl>
void DataflowQueueBank<Impl>::tick()
{
    if (pendingInst && !instGranted) {
        pendingInstValid = true;
    }
}

template<class Impl>
DataflowQueueBank<Impl>::DataflowQueueBank(DerivFFCPUParams *params)
        : nOps(params->numOperands),
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
          prematureFwPointers(depth)
{
    for (auto &line: prematureFwPointers) {
        for (auto &ptr: line) {
            ptr.valid = false;
        }
    }
}

template<class Impl>
typename DataflowQueueBank<Impl>::DynInstPtr &
DataflowQueueBank<Impl>::readInstsFromBank(DQPointer pointer)
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
    DPRINTF(DQWrite, "insert inst[%d] to (%d %d)\n",
            inst->seqNum, pointer.bank, pointer.index);
    auto index = pointer.index;
    assert(!instArray[index]);
    instArray[index] = inst;
}

template<class Impl>
void DataflowQueueBank<Impl>::checkReadiness(DQPointer pointer)
{
    auto index = pointer.index;
    assert(instArray[index]);
    DynInstPtr &inst = instArray[index];

    bool ready = inst->memDepReady && inst->miscDepReady;
    for (unsigned op = 1; op < 4; op++) {
        ready &= inst->opFulfilled(op);
    }
    if (ready) {
        readyIndices.push_back(pointer.index);
    }
}

template<class Impl>
void DataflowQueueBank<Impl>::resetState()
{
    for (auto &it: instArray) {
        it = nullptr;
    }
}

template<class Impl>
void DataflowQueues<Impl>::tick()
{
    clear();

    // fu preparation
    for (auto &wrapper: fuWrappers) {
        wrapper.startCycle();
    }

    readQueueHeads();

//    DPRINTF(Omega, "DQ tick reach 1\n");
    // todo: write insts from bank to time buffer!
    for (unsigned i = 0; i < nBanks; i++) {
        DynInstPtr inst;

        inst = dqs[i].wakeupInstsFromBank();

        // read fu requests from banks
        //  and set fields for packets
        //  fu_req_ptrs = xxx
        //  update valid, dest bits and payload only (source need not be changed)
        toNextCycle->instValids[i] = !!inst;
        toNextCycle->insts[i] = inst;
        if (toNextCycle->instValids[i]) {
            DPRINTF(Omega, "notNext cycle inst[%d] valid, &inst: %p\n",
                    i, toNextCycle->insts[i]);
        }
    }

//    DPRINTF(Omega, "DQ tick reach 2\n");
    // todo: write forward pointers from bank to time buffer!
    for (unsigned b = 0; b < nBanks; b++) {
        const std::vector<DQPointer> forward_ptrs = dqs[b].readPointersFromBank();
        for (unsigned op = 1; op <= 3; op++) {
            toNextCycle->pointers[b * nOps + op] = forward_ptrs[op];
        }
    }

//    DPRINTF(Omega, "DQ tick reach 3\n");
    // For each bank
    //  get ready instructions produced by last tick from time struct
    for (unsigned i = 0; i < nBanks; i++) {
        fu_requests[i].valid = fromLastCycle->instValids[i];
        if (fromLastCycle->instValids[i]) {

            DPRINTF(Omega, "inst[%d] valid, &inst: %p\n",
                    i, fromLastCycle->insts[i]);

            fu_requests[i].payload = fromLastCycle->insts[i];
            fu_requests[i].destBits = coordinateFU(fromLastCycle->insts[i], i);
        }
    }
//    DPRINTF(Omega, "DQ tick reach 4\n");
    // For each bank

    //  For each valid ready instruction compete for a FU via the omega network
    //  FU should ensure that this is no write port hazard next cycle
    //      If FU grant an inst, pass its direct child pointer to this FU's corresponding Pointer Queue
    //      to wake up the child one cycle before its value arrived

    fu_granted_ptrs = bankFUNet.select(fu_req_ptrs);
    for (unsigned b = 0; b < nBanks; b++) {
        assert(fu_granted_ptrs[b]);
        if (!fu_granted_ptrs[b]->valid || !fu_granted_ptrs[b]->payload) {
            continue;
        }
        DynInstPtr &inst = fu_granted_ptrs[b]->payload;

        DPRINTF(DQWake, "Inst[%d] selected\n", inst->seqNum);

        bool can_accept = fuWrappers[b].canServe(inst);
        if (can_accept) {
            fu_req_granted[fu_granted_ptrs[b]->source] = true;
            fuWrappers[b].consume(inst);
        }
    }

//    DPRINTF(Omega, "DQ tick reach 5\n");
    // mark it for banks
    for (unsigned b = 0; b < nBanks; b++) {
        dqs[b].instGranted = fu_req_granted[b];
    }

//    DPRINTF(Omega, "DQ tick reach 6\n");
    // push forward pointers to queues
    for (unsigned b = 0; b < nBanks; b++) {
        for (unsigned op = 1; op <= 3; op++) {
            auto &ptr = fromLastCycle->pointers[b * nOps + op];
            if (ptr.valid) {
                wakeQueues[b * nOps + op].push(WKPointer(ptr));
                assert(wakeQueues[b * nOps + op].size() <= maxQueueDepth);
            }
        }

        // todo: check this line of code
        if (fuWrappers[b].toWakeup.valid) {
            DPRINTF(DQWake, "Got wakeup pointer to (%d %d)(%d)\n",
                    fuWrappers[b].toWakeup.bank,
                    fuWrappers[b].toWakeup.index,
                    fuWrappers[b].toWakeup.op);
            wakeQueues[b * nOps].push(WKPointer(fuWrappers[b].toWakeup));
            assert(wakeQueues[b * nOps].size() <= maxQueueDepth);
        }
    }

//    DPRINTF(Omega, "DQ tick reach 7\n");
    // For each bank: check status: whether I can consume from queue this cycle?
    // record the state
    wakeup_granted_ptrs = wakeupQueueBankNet.select(wakeup_req_ptrs);
    // check whether each bank can really accept
    for (unsigned b = 0; b < nBanks; b++) {
        if (dqs[b].canServeNew()) {
            for (unsigned op = 1; op <= 3; op++) {
                const auto &pkt = wakeup_granted_ptrs[b * nOps + op];
                if (!pkt->valid) {
                    continue;
                }
                WKPointer &ptr = pkt->payload;
                wake_req_granted[pkt->source] = true;
                assert(dqs[b].wakeup(ptr));

                // pop accepted pointers.
                assert(!wakeQueues[pkt->source].empty());
                if (wakeQueues[pkt->source].size() == numPendingWakeupMax) {
                    numPendingWakeupMax--;
                }
                wakeQueues[pkt->source].pop();
                numPendingWakeups--;
            }
        }
    }

    DPRINTF(DQ, "Pair packets before selection\n");
    dumpPairPackets(insert_req_ptrs);
    insert_granted_ptrs = pointerQueueBankNet.select(insert_req_ptrs);

    DPRINTF(DQ, "Selected pairs:\n");
    dumpPairPackets(insert_granted_ptrs);

    DPRINTF(DQ, "Pair packets after selection\n");
    dumpPairPackets(insert_req_ptrs);

    for (auto &ptr : insert_granted_ptrs) {
        if (ptr->valid) {
            DPRINTF(DQ, "pair[%d] (pointer(%d) (%d %d)(%d)) granted\n",
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

            DynInstPtr inst = dqs[pair.dest.bank].readInstsFromBank(pair.dest);

            if (inst) {
                DPRINTF(DQ, "Insert fw pointer (%d %d) (%d) after inst reached\n",
                        pair.dest.bank, pair.dest.index, pair.dest.op);
                markFwPointers(inst->pointers, pair);
            } else {
                DPRINTF(DQ, "Insert fw pointer (%d %d) (%d) before inst reached\n",
                        pair.dest.bank, pair.dest.index, pair.dest.op);
                auto &pointers =
                        dqs[pair.dest.bank].prematureFwPointers[pair.dest.index];
                markFwPointers(pointers, pair);
            }

            assert(!forwardPointerQueue[pkt->source].empty());
            if (forwardPointerQueue[pkt->source].size() == numPendingFwPointerMax) {
                numPendingFwPointerMax--;
            }
            forwardPointerQueue[pkt->source].pop();
            numPendingFwPointers--;
        }
    }


    // For each bank, check
    //  whether there's an inst to be waken up
    //  whether there are multiple instruction to wake up, if so
    //      buffer it and reject further requests in following cycles
//    DPRINTF(Omega, "DQ tick reach 9\n");
    for (auto &bank: dqs) {
        bank.tick();
    }
//    DPRINTF(Omega, "DQ tick reach 10\n");

    for (auto &wrapper: fuWrappers) {
        wrapper.tick();
        wrapper.endCycle();
    }
}

template<class Impl>
void DataflowQueues<Impl>::clear()
{
//    fill(wakenValids.begin(), wakenValids.end(), false);
    fill(fu_req_granted.begin(), fu_req_granted.end(), false);
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
        dqs(nBanks, XDataflowQueueBank(params)),
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

        maxQueueDepth(params->pendingQueueDepth),
        PendingWakeupThreshold(params->PendingWakeupThreshold),
        PendingWakeupMaxThreshold(params->PendingWakeupMaxThreshold),
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
        fuWrappers[b].setDQ(this);
    }
    memDepUnit.init(params, DummyTid);
    memDepUnit.setIQ(this);
    resetState();
}

template<class Impl>
boost::dynamic_bitset<>
DataflowQueues<Impl>::uint2Bits(unsigned from)
{
    auto res = boost::dynamic_bitset<>(32);
    for (unsigned i = 0; i < 32; i++, from >>= 1) {
        res[i] = from & 1;
    }
    return res;
}

template<class Impl>
DQPointer
DataflowQueues<Impl>::uint2Pointer(unsigned u)
{
    unsigned bank = u & bankMask;
    unsigned index = (u & indexMask) >> bankWidth;
    unsigned group = 0; //todo group is not supported yet
    return DQPointer{true, group, bank, index, 0};
}


template<class Impl>
unsigned DataflowQueues<Impl>::pointer2uint(DQPointer ptr)
{
    return ptr.index + ptr.bank * depth;
}

template<class Impl>
boost::dynamic_bitset<>
DataflowQueues<Impl>::coordinateFU(
        DataflowQueues::DynInstPtr &inst, unsigned bank)
{
    // find another FU group with desired capability.
    if (llBlocked) {
        auto fu_bitmap = fuGroupCaps[inst->opClass()];
        do {
            fuPointer[bank] = (fuPointer[bank] + 1) % nBanks;
        } while (!fu_bitmap[fuPointer[bank]]);
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

        forwardPointerQueue[forwardPtrIndex].push(pkt);

        if (forwardPointerQueue[forwardPtrIndex].size() > maxQueueDepth) {
//        for (const auto &q: forwardPointerQueue) {
//            DPRINTF(DQ, "fw ptr queue size: %i\n", q.size());
//        }
        }

        assert(forwardPointerQueue[forwardPtrIndex].size() <= maxQueueDepth);

        numPendingFwPointers++;
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
    assert(tail != head);
    DQPointer head_ptr = uint2Pointer(tail);
    DPRINTF(FFCommit, "Position of inst to commit:(%d %d)\n",
            head_ptr.bank, head_ptr.index);
    DynInstPtr head_inst = dqs[head_ptr.bank].readInstsFromBank(head_ptr);
    assert(head_inst);
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
    cpu->removeFrontInst(head_inst);
    dqs[head_ptr.bank].advanceTail();
}

template<class Impl>
bool DataflowQueues<Impl>::isFull()
{
    bool res = head == queueSize - 1 ? tail == 0 : head == tail - 1;
    if (res) {
        DPRINTF(DQ, "SQ is full head = %d, tail = %d\n", head, tail);
    }
    return res;
}

template<class Impl>
bool DataflowQueues<Impl>::isEmpty()
{
    return head == tail && !getHead();
}


template<class Impl>
typename DataflowQueues<Impl>::DynInstPtr
DataflowQueues<Impl>::getHead()
{
    auto head_ptr = uint2Pointer(head);
    XDataflowQueueBank &bank = dqs[head_ptr.bank];
    auto &inst = bank.readInstsFromBank(head_ptr);
    return inst;
}

template<class Impl>
typename DataflowQueues<Impl>::DynInstPtr
DataflowQueues<Impl>::getTail()
{
    auto head_ptr = uint2Pointer(tail);
    XDataflowQueueBank &bank = dqs[head_ptr.bank];
    auto &inst = bank.readInstsFromBank(head_ptr);
    return inst;
}

template<class Impl>
bool DataflowQueues<Impl>::stallToUnclog()
{
    return isFull() || wakeupQueueClogging() || fwPointerQueueClogging();

}

template<class Impl>
typename DataflowQueues<Impl>::DynInstPtr
DataflowQueues<Impl>::findInst(InstSeqNum num) const
{
    unsigned count = 0;
    DynInstPtr inst = nullptr;
    for (const auto &bank: dqs) {
        if (DynInstPtr tmp = bank.findInst(num)) {
            count += 1;
            inst = tmp;
        }
    }
    assert(count == 1);
    return inst;
}

template<class Impl>
bool DataflowQueues<Impl>::wakeupQueueClogging()
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
bool DataflowQueues<Impl>::fwPointerQueueClogging()
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
FFRegValue DataflowQueues<Impl>::readReg(DQPointer ptr)
{
    auto b = ptr.bank;
    auto inst = dqs[b].readInstsFromBank(ptr);
    FFRegValue result;
    if (inst) {
        // not committed yet
        result = inst->getDestValue();
    } else {
        assert(committedValues.count(ptr));
        result = committedValues[ptr];
    }
    return result;
}

template<class Impl>
void DataflowQueues<Impl>::setReg(DQPointer ptr, FFRegValue val)
{
    regFile[pointer2uint(ptr)] = val;
}

template<class Impl>
void DataflowQueues<Impl>::addReadyMemInst(DynInstPtr inst)
{
    // todo: when dispatch mem inst, they should be marked as dependant
    //  on other mem insts
    WKPointer wk(inst->dqPosition);
    wk.wkType = WKPointer::WKMem;
    wakeQueues[nonSpecBankPtr * nOps + 3].push(wk);
    incNonSpecBankPtr();
}

template<class Impl>
void DataflowQueues<Impl>::squash(InstSeqNum seqNum)
{

}

template<class Impl>
bool DataflowQueues<Impl>::insert(DynInstPtr &inst)
{
    // todo: send to allocated DQ position
    assert(inst);
    assert(!isFull());

    DQPointer allocated = inst->dqPosition;
    DPRINTF(DQ, "allocated bank: %d\n", allocated.bank);
    assert(!dqs[allocated.bank].readInstsFromBank(allocated));
//    DPRINTF(DQ, "insert reach 1\n");
    dqs[allocated.bank].writeInstsToBank(allocated, inst);
    head++;
//    DPRINTF(DQ, "insert reach 2\n");

    inst->setInDQ();
    // we don't need to add to dependents or producers here,
    //  which is maintained in DIEWC by archState


//    DPRINTF(DQ, "insert reach 3\n");
    if (inst->isMemRef()) {
        memDepUnit.insert(inst);
    }
//    DPRINTF(DQ, "insert reach 4\n");
    dqs[allocated.bank].checkReadiness(allocated);
//    DPRINTF(DQ, "insert reach 5\n");

    return true;
}

template<class Impl>
void DataflowQueues<Impl>::insertBarrier(DynInstPtr &inst)
{
    memDepUnit.insertBarrier(inst);
    insertNonSpec(inst);
}

template<class Impl>
void DataflowQueues<Impl>::insertNonSpec(DynInstPtr &inst)
{
    assert(inst);
    inst->miscDepReady = false;
    inst->hasMiscDep = true;
    insert(inst);
}

template<class Impl>
void
DataflowQueues<Impl>::markFwPointers(
        std::array<DQPointer, 4> &pointers, PointerPair &pair)
{
    unsigned op = pair.dest.op;
    assert(!pointers[op].valid);
    pointers[op] = pair.payload;
}

template<class Impl>
void DataflowQueues<Impl>::rescheduleMemInst(DynInstPtr &inst)
{
    inst->translationStarted(false);
    inst->translationCompleted(false);
    inst->clearCanIssue();
    inst->hasMiscDep = true;  // this is rare, do not send a packet here

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
    cpu->wakeCPU();
}

template<class Impl>
void DataflowQueues<Impl>::blockMemInst(DataflowQueues::DynInstPtr &inst)
{
    inst->translationStarted(false);
    inst->translationCompleted(false);
    inst->clearCanIssue();
    inst->clearIssued();
    blockedMemInsts.push_back(inst);
}

template<class Impl>
unsigned DataflowQueues<Impl>::numInDQ()
{
    return head < tail ? head + queueSize - tail + 1 : head - tail + 1;
}

template<class Impl>
unsigned DataflowQueues<Impl>::numFree()
{
    return queueSize - numInDQ();
}

template<class Impl>
void DataflowQueues<Impl>::drainSanityCheck() const
{
    assert(tail == head);
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
        bank.resetState();
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
        DPRINTF(DQOmega, "&pkt: %p, ", pkt);
        DPRINTFR(DQOmega, "v: %d, dest: %lu, src: %d,",
                pkt->valid, pkt->destBits.to_ulong(), pkt->source);
        DPRINTFR(DQOmega, " inst: %p\n", pkt->payload);
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
void DataflowQueues<Impl>::setDIEWC(DIEWC *diewc)
{
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
    WKPointer wk(uint2Pointer(head));
    wk.wkType = WKPointer::WKMisc;
    wakeQueues[nonSpecBankPtr*nOps + 3].push(wk);
    incNonSpecBankPtr();
}

template<class Impl>
void DataflowQueues<Impl>::incNonSpecBankPtr()
{
    nonSpecBankPtr = (nonSpecBankPtr + 1) % nBanks;
}

template<class Impl>
list<typename Impl::DynInstPtr>
DataflowQueues<Impl>::getBankHeads()
{
    list<DynInstPtr> heads;
    auto ptr_i = head;
    for (unsigned count = 0; count < nBanks; count++) {
        auto ptr = uint2Pointer(ptr_i);
        DynInstPtr inst = dqs[ptr.bank].readInstsFromBank(ptr);
        if (inst) {
            DPRINTF(DQRead, "read inst[%d] from DQ\n", inst->seqNum);
        } else {
            DPRINTF(DQRead, "inst@[%d] is null\n", ptr_i);
        }
        heads.push_back(inst);
        ptr_i = (ptr_i + 1) % queueSize;
    }
    return heads;
}

template<class Impl>
list<typename Impl::DynInstPtr>
DataflowQueues<Impl>::getBankTails()
{
    list<DynInstPtr> tails;
    auto ptr_i = tail;
    for (unsigned count = 0; count < nBanks; count++) {
        auto ptr = uint2Pointer(ptr_i);
        DynInstPtr inst = dqs[ptr.bank].readInstsFromBank(ptr);
        if (inst) {
            DPRINTF(DQRead, "read inst[%d] from DQ\n", inst->seqNum);
        } else {
            DPRINTF(DQRead, "inst@[%d] is null\n", ptr_i);
        }
        tails.push_back(inst);
        ptr_i = (ptr_i + 1) % queueSize;
    }
    return tails;
}

}

#include "cpu/forwardflow/isa_specific.hh"

template class FF::DataflowQueues<FFCPUImpl>;
