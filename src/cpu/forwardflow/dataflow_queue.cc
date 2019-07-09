//
// Created by zyy on 19-6-10.
//

#include "dataflow_queue.hh"
#include "params/DerivFFCPU.hh"

namespace FF {

using namespace std;

using boost::dynamic_bitset;

template<class Impl>
tuple<bool, typename DataflowQueueBank<Impl>::DynInstPtr>
DataflowQueueBank<Impl>::wakeupInstsFromBank()
{
    if (pendingInstValid) {
        return make_tuple(true, pendingInst);
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

    return make_tuple(!!first, first);
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
bool DataflowQueueBank<Impl>::wakeup(DQPointer pointer)
{
    auto op = pointer.op;
    assert(!inputPointers[op].valid);
    inputPointers[op] = pointer;
    return true;
}

template<class Impl>
void DataflowQueueBank<Impl>::tick()
{
    if (!instGranted) {
        pendingInstValid = true;
    }
}

template<class Impl>
DataflowQueueBank<Impl>::DataflowQueueBank(DerivFFCPUParams *params)
        : nOps(params->numOperands),
          depth(params->DQDepth),
          nullDQPointer(DQPointer{false, 0, 0, 0, 0}),
          instArray(nOps, nullptr),
          nearlyWakeup(dynamic_bitset<>(depth)),
          pendingWakeupPointers(nOps, nullDQPointer),
          anyPending(false),
          inputPointers(nOps, nullDQPointer),
          pendingInst(nullptr),
          pendingInstValid(false),
          outputPointers(nOps, nullDQPointer)
{}

template<class Impl>
typename DataflowQueueBank<Impl>::DynInstPtr &
DataflowQueueBank<Impl>::readInstsFromBank(DQPointer pointer)
{
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
    // todo: write insts from bank to time buffer!
    for (unsigned i = 0; i < nBanks; i++) {
        bool wake_valid;
        DynInstPtr inst;

        tie(wake_valid, inst) = dqs[i].wakeupInstsFromBank();

        // read fu requests from banks
        //  and set fields for packets
        //  fu_req_ptrs = xxx
        //  update valid, dest bits and payload only (source need not be changed)
        toNextCycle->instValids[i] = wake_valid;
        toNextCycle->insts[i] = inst;
    }

    // todo: write forward pointers from bank to time buffer!
    for (unsigned b = 0; b < nBanks; b++) {
        const std::vector<DQPointer> forward_ptrs = dqs[b].readPointersFromBank();
        for (unsigned op = 1; op <= 3; op++) {
            toNextCycle->pointers[b * nOps + op] = forward_ptrs[op];
        }
    }

    // For each bank
    //  get ready instructions produced by last tick from time struct
    for (unsigned i = 0; i < nBanks; i++) {
        fu_requests[i].valid = fromLastCycle->instValids[i];
        if (fromLastCycle->instValids[i]) {
            fu_requests[i].payload = fromLastCycle->insts[i];
            fu_requests[i].destBits = coordinateFU(fromLastCycle->insts[i], i);
        }
    }

    //  For each valid ready instruction compete for a FU via the omega network
    //  FU should ensure that this is no write port hazard next cycle
    //      If FU grant an inst, pass its direct child pointer to this FU's corresponding Pointer Queue
    //      to wake up the child one cycle before its value arrived
    fu_granted_ptrs = bankFUNet.select(fu_req_ptrs);
    for (unsigned b = 0; b < nBanks; b++) {
        DynInstPtr &inst = fu_granted_ptrs[b]->payload;
        bool can_accept = fuWrappers[b].canServe(inst);
        if (can_accept) {
            fu_req_granted[fu_granted_ptrs[b]->source] = true;
            fuWrappers[b].consume(inst);

            // todo: check this line of code
            wakeQueues[b * nOps].push(WKPointer(fuWrappers[b].toWakeup));

            assert(wakeQueues[b * nOps].size() <= maxQueueDepth);
        }
    }

    // mark it for banks
    for (unsigned b = 0; b < nBanks; b++) {
        dqs[b].instGranted = fu_req_granted[b];
    }

    // push forward pointers to queues
    for (unsigned b = 0; b < nBanks; b++) {
        for (unsigned op = 1; op <= 3; op++) {
            auto &ptr = fromLastCycle->pointers[b * nOps + op];
            if (ptr.valid) {
                wakeQueues[b * nOps + op].push(WKPointer(ptr));
            }
        }
    }
    //  get indirect waking pointer form queues
    for (unsigned b = 0; b < nBanks; b++) {
        for (unsigned op = 1; op <= 3; op++) {
            auto &pkt = wakeup_requests[b * nOps + op];
            const auto &q = wakeQueues[b * nOps + op];
            DQPointer ptr = q.empty() ? nullDQPointer : DQPointer(q.front());
            pkt.valid = ptr.valid;
            pkt.payload = ptr;
            pkt.destBits = uint2Bits(ptr.bank * nOps + ptr.op);
        }
    }

    // For each bank: check status: whether I can consume from queue this cycle?
    // record the state
    wakeup_granted_ptrs = wakeupQueueBankNet.select(wakeup_req_ptrs);
    // check whether each bank can really accept
    for (unsigned b = 0; b < nBanks; b++) {
        if (dqs[b].canServeNew()) {
            for (unsigned op = 1; op <= 3; op++) {
                const auto &pkt = wakeup_granted_ptrs[b * nOps + op];
                DQPointer &ptr = pkt->payload;
                wake_req_granted[pkt->source] = true;
                assert(dqs[b].wakeup(ptr));

                // pop accepted pointers.
                assert(!wakeQueues[pkt->source].empty());
                wakeQueues[pkt->source].pop();
            }
        }
    }

    insert_granted_ptrs = pointerQueueBankNet.select(insert_req_ptrs);
    for (unsigned b = 0; b < nBanks; b++) {
        for (unsigned op = 0; op <= 3; op++) {
            const auto &pkt = insert_granted_ptrs[b * nOps + op];
            PointerPair &pair = pkt->payload;
            insert_req_granted[pkt->source] = true;

            DynInstPtr inst = dqs[pair.dest.bank].readInstsFromBank(pair.dest);

            if (inst) {
                markFwPointers(inst->pointers, pair);
            } else {
                auto &pointers =
                        dqs[pair.dest.bank].prematureFwPointers[pair.dest.index];
                markFwPointers(pointers, pair);
            }

            assert(!forwardPointerQueue[pkt->source].empty());
            forwardPointerQueue[pkt->source].pop();

        }
    }


    // For each bank, check
    //  whether there's an inst to be waken up
    //  whether there are multiple instruction to wake up, if so
    //      buffer it and reject further requests in following cycles
    for (auto &bank: dqs) {
        bank.tick();
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
        WritePorts(1),
        ReadPorts(1),
        writes(0),
        reads(0),
        nBanks(params->numDQBanks),
        nOps(params->numOperands),
        nFUGroups(params->numDQBanks),
        depth(params->DQDepth),
        queueSize(nBanks * depth),
        wakeQueues(nBanks, queue<WKPointer>()),
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
            DQPacket<DQPointer> &dq_pkt = wakeup_requests[index];
            dq_pkt.valid = false;
            dq_pkt.source = index;
            wakeup_req_ptrs[index] = &wakeup_requests[index];

            DQPacket<PointerPair> &pair_pkt = insert_requests[index];
            pair_pkt.valid = false;
            pair_pkt.source = index;
            insert_req_ptrs[index] = &insert_requests[index];
        }
    }
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
    unsigned index = u & indexMask;
    unsigned bank = (u >> indexWidth) & bankMask;
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
    assert(forwardPtrIndex < nBanks);

    DQPacket<PointerPair> pkt;
    pkt.valid = true;
    pkt.source = forwardPtrIndex;
    pkt.payload = pair;
    pkt.destBits = uint2Bits(pair.dest.bank * nOps + pair.dest.op);

    forwardPointerQueue[forwardPtrIndex].push(pkt);

    assert(forwardPointerQueue[forwardPtrIndex].size() <= maxQueueDepth);

    forwardPtrIndex++;
}

template<class Impl>
void DataflowQueues<Impl>::retireHead()
{
    assert(tail != head);
    DQPointer head_ptr = uint2Pointer(head);
    DynInstPtr head_inst = dqs[head_ptr.bank].readInstsFromBank(head_ptr);
    head_inst->clearInDQ();
    cpu->removeFrontInst(head_inst);
}

template<class Impl>
bool DataflowQueues<Impl>::isFull()
{
    return head == queueSize - 1 ? tail == 0 : head == tail - 1;
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
    return numPendingWakeups >= PendingWakeupThreshold ||
           numPendingWakeupMax >= PendingWakeupMaxThreshold;
}

template<class Impl>
bool DataflowQueues<Impl>::fwPointerQueueClogging()
{
    return numPendingFwPointers >= PendingFwPointerThreshold;
}

template<class Impl>
FFRegValue DataflowQueues<Impl>::readReg(DQPointer ptr)
{
    FFRegValue result = regFile[pointer2uint(ptr)];
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
    wakeQueues[readyMemInstPtr * nOps + 3].push(wk);
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
    assert(!dqs[allocated.bank].readInstsFromBank(allocated));
    dqs[allocated.bank].writeInstsToBank(allocated, inst);
    head++;

    inst->setInDQ();
    // we don't need to add to dependents or producers here,
    //  which is maintained in DIEWC by archState


    if (inst->isMemRef()) {
        memDepUnit.insert(inst);
    }
    dqs[allocated.bank].checkReadiness(allocated);

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

    blockedMemInsts.clear();
    retryMemInsts.clear();

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

}

#include "cpu/forwardflow/isa_specific.hh"

template class FF::DataflowQueues<FFCPUImpl>;
