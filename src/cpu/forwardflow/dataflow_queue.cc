//
// Created by zyy on 19-6-10.
//

#include "dataflow_queue.hh"

namespace FF{

using namespace std;

using boost::dynamic_bitset;

template<class Impl>
tuple<bool, typename DataflowQueueBank<Impl>::DynInstPtr &>
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
        auto & ptr = pointers[op];
        if (!ptr.valid) continue;

        wakeup_count++;
        if (nearlyWakeup[ptr.index] && first == nullptr) {
            first = banks[ptr.index];
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

    return first;
}

template<class Impl>
const std::vector<DQPointer>
DataflowQueueBank<Impl>::readPointersFromBank()
{
    outputPointers[0].valid = false; // dest register should never be forwarded
    for (unsigned op = 1; op < nOps; op++) {
        const auto & ptr = inputPointers[op];
        auto &optr = outputPointers[op];

        if (!ptr.valid) {
            optr.valid = false;
        } else {
            optr = banks[ptr.index].pointers[op];
        }
    }
    return outputPointers;
}

template<class Impl>
bool DataflowQueueBank<Impl>::canServeNew() {
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
bool DataflowQueueBank<Impl>::wakeup(DQPointer pointer) {
    auto op = pointer.op;
    assert(!inputPointers[op].valid);
    inputPointers[op] = pointer;
    return true;
}

template<class Impl>
void DataflowQueueBank<Impl>::tick() {
    if (!instGranted) {
        pendingInstValid = true;
    }
}

template<class Impl>
DataflowQueueBank<Impl>::DataflowQueueBank(DerivFFCPUParams *params)
        : nOps(params->numOperands),
          depth(params->DQDepth),
          banks(nOps, nullptr),
          nullDQPointer(DQPointer{false, 0, 0, 0, 0}),
          nearlyWakeup(dynamic_bitset<>(depth)),
          pendingWakeupPointers(nOps, nullDQPointer),
          anyPending(false),
          inputPointers(nOps, nullDQPointer),
          pendingInst(nullptr),
          pendingInstValid(false),
          outputPointers(nOps, nullDQPointer)
{}

template<class Impl>
void DataflowQueues<Impl>::tick() {
    clear();
    // todo: write insts from bank to time buffer!
    for (unsigned i = 0; i < nBanks; i++) {
        tie(wakenValids[i], wakenInsts[i]) = dqs[i].wakeupInstsFromBank();

        // read fu requests from banks
        //  and set fields for packets
        //  fu_request_ptrs = xxx
        //  update valid, dest bits and payload only (source need not be changed)
        toNextCycle->instValids[i] = wakenValids[i];
        toNextCycle->insts[i] = wakenInsts[i];
    }

    // todo: write forward pointers from bank to time buffer!
    for (unsigned b = 0; b < nBanks; b++) {
        const std::vector<DQPointer> forward_ptrs = dqs[b].readPointersFromBank();
        for (unsigned op = 1; op <= 3; op++) {
            toNextCycle->pointers[b*nOps + op] = forward_ptrs[op];
        }
    }

    // For each bank
    //  get ready instructions produced by last tick from time struct
    for (unsigned i = 0; i < nBanks; i++) {
        fu_requests[i].valid = fromLastCycle->instValids[i];
        if (fromLastCycle->instValids[i]) {
            fu_requests[i].payload = fromLastCycle->insts[i];
            fu_requests[i].destBits = coordinateFU(fromLastCycle->insts[i]);
        }
    }

    //  For each valid ready instruction compete for a FU via the omega network
    //  FU should ensure that this is no write port hazard next cycle
    //      If FU grant an inst, pass its direct child pointer to this FU's corresponding Pointer Queue
    //      to wake up the child one cycle before its value arrived
    fu_granted_ptrs = bankFUNet.select(fu_request_ptrs);
    for (unsigned b = 0; b < nBanks; b++) {
        DynInstPtr &inst = fu_granted_ptrs[b].payload;
        bool can_accept = fuWrappers[b].canServe(inst);
        if (can_accept) {
            fu_req_granted[fu_granted_ptrs[b].source] = true;
            fuWrappers[b].consume(inst);
            queues[b*nOps].push(inst.pointers[0]);
            assert(!queues[b*nOps].size() > maxQueueDepth);
        }
    }

    // mark it for banks
    for (unsigned b = 0; b < nBanks; b++) {
        dqs[b].instGranted = fu_req_granted[b];
    }

    // push forward pointers to queues
    for (unsigned b = 0; b < nBanks; b++) {
        for (unsigned op = 1; op <= 3; op++) {
            auto &ptr = fromLastCycle->pointers[b*nOps + op];
            if (ptr.valid) {
                queues[b*nOps + op].push(ptr);
            }
        }
    }
    //  get indirect waking pointer form queues
    for (unsigned b = 0; b < nBanks; b++) {
        for (unsigned op = 1; op <= 3; op++) {
            auto &pkt = bank_requests[b*nOps + op];
            const auto &q = queues[b*nOps + op];
            DQPointer &ptr = q.empty() ? nullDQPointer : q.front();
            pkt.valid = ptr.valid;
            pkt.payload = ptr;
            pkt.destBits = fromUint(ptr.bank * nOps + ptr.op);
        }
    }

    // For each bank: check status: whether I can consume from queue this cycle?
    // record the state
    bank_granted_ptrs = queueBankNet.select(bank_request_ptrs);
    // check whether each bank can really accept
    for (unsigned b = 0; b < nBanks; b++) {
        if (dqs[b].canServeNew()) {
            for (unsigned op = 1; op <= 3; op++) {
                const auto &pkt = bank_granted_ptrs[b*nOps + op];
                DQPointer &ptr = pkt->payload;
                bank_req_granted[pkt->source] = true;
                assert(dqs[b].wakeup(ptr));

                // pop accepted pointers.
                assert(!queues[pkt->source].empty());
                queues[pkt->source].pop();
            }
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
bool DataflowQueues<Impl>::insert(DynInstPtr &ptr) {
    return false;
}

template<class Impl>
void DataflowQueues<Impl>::clear() {
    fill(wakenValids.begin(), wakenValids.end(), false);
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
        queues(nBanks, queue<DQPointer>(params->pendingQueueDepth)),
        dqs(nBanks, DataflowQueueBank(params)),
        wakenValids(nBanks, false),
        wakenInsts(nBanks, nullptr),
        bankFUNet(nBanks, true),
        queueBankNet(nBanks*nOps, true),

        fu_requests(nBanks),
        fu_req_granted(nBanks),
        fu_request_ptrs(nBanks),
        fu_granted_ptrs(nBanks),

        bank_requests(nBanks*nOps),
        bank_req_granted(nBanks*nOps),
        bank_request_ptrs(nBanks*nOps),
        bank_granted_ptrs(nBanks*nOps),

        fuWrappers(nBanks), // todo: fix it
        maxQueueDepth(params->pendingQueueDepth)
{
    // init inputs to omega networks
    for (unsigned b = 0; b < nBanks; b++) {
        Packet<DynInstPtr> &fu_pkt = fu_requests[b];
        fu_pkt.valid = false;
        fu_pkt.source = b;

        fu_request_ptrs[b] = &fu_requests[b];

        for (unsigned op = 0; op < nBanks; op++) {
            Packet<DQPointer> &dq_pkt = bank_requests[b*nOps + op];
            dq_pkt.valid = false;
            dq_pkt.source = b*nOps + op;
            bank_request_ptrs[b*nOps + op] = &bank_requests[b*nOps + op];
        }
    }
}

template<class Impl>
boost::dynamic_bitset<> DataflowQueues<Impl>::fromUint(unsigned from) {
    auto res = boost::dynamic_bitset<>(32);
    for (unsigned i = 0; i < 32; i++, from >>= 1) {
        res[i] = from & 1;
    }
    return res;
}


}
