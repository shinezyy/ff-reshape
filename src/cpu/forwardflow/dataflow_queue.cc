//
// Created by zyy on 19-6-10.
//

#include <random>

#include "cpu/forwardflow/dataflow_queue.hh"
#include "debug/DQ.hh"
#include "debug/DQB.hh"  // DQ bank
#include "debug/DQGDL.hh"
#include "debug/DQGOF.hh"
#include "debug/DQOmega.hh"
#include "debug/DQPair.hh"  // DQ read
#include "debug/DQRead.hh"  // DQ read
#include "debug/DQV2.hh"
#include "debug/DQWake.hh"
#include "debug/DQWakeV1.hh"
#include "debug/DQWrite.hh"  // DQ write
#include "debug/FFCommit.hh"
#include "debug/FFDisp.hh"
#include "debug/FFExec.hh"
#include "debug/FFSquash.hh"
#include "debug/FUW.hh"
#include "debug/ObExec.hh"
#include "debug/ObFU.hh"
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
DataflowQueues<Impl>::DataflowQueues(DerivFFCPUParams *params,
        unsigned gid, DQCommon *_c, DQTop *_top)
        :
        writes(0),
        reads(0),
        nBanks(params->numDQBanks),
        nOps(params->numOperands),
        nFUGroups(params->numDQBanks),
        depth(params->DQDepth),
        queueSize(nBanks * depth),

        wakeQueues(nBanks * nOps),
        forwardPointerQueue(nBanks * nOps),

        c(_c),

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
        fu_granted_ptrs(nBanks * OpGroups::nOpGroups),

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
        NarrowLocalForward(params->NarrowLocalForward),
        AgedWakeQueuePush(params->AgedWakeQueuePush),
        AgedWakeQueuePktSel(params->AgedWakeQueuePktSel),
        pushFF(nBanks * nOps, false),
        interGroupBW(params->interGroupBW)
{
    setGroupID(gid);
    setTop(_top);

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
        fuWrappers[b].init(fuPool->fw, groupID, b);
        // initiate fu bit map here
        fuWrappers[b].fillMyBitMap(fuGroupCaps, b);
        fuWrappers[b].fillLatTable(opLat);
        fuWrappers[b].setDQ(this);

        dqs.push_back(new XDataflowQueueBank(params, b, this, top));
        readyInstsQueues.push_back(new XReadyInstsQueue(params, name(), b % 2));
        dqs[b]->readyInstsQueue = readyInstsQueues[b];
//        dqs[b]->setTop(top);
    }

    for (unsigned i = 0; i < nBanks * OpGroups::nOpGroups; i++) {
        DQPacket<DynInstPtr> &fu_pkt = fu_requests[i];
        fu_pkt.valid = false;
        fu_pkt.source = i;

        fu_req_ptrs[i] = &fu_requests[i];
    }
    fuPointer[OpGroups::MultDiv] = std::vector<unsigned>{0, 4, 1, 5};
    fuPointer[OpGroups::FPAdd] = std::vector<unsigned>{2, 6, 3, 7};

    resetState();

    nullWKPkt.valid = false;
    nullWKPkt.destBits = boost::dynamic_bitset<>(ceilLog2(nOps * nBanks) + 1);

    nullFWPkt.valid = false;
    nullFWPkt.destBits = boost::dynamic_bitset<>(ceilLog2(nOps * nBanks) + 1);

    nullInstPkt.valid = false;
    nullInstPkt.destBits = boost::dynamic_bitset<>(ceilLog2(nBanks * nOpGroups) + 1);
}


template<class Impl>
void DataflowQueues<Impl>::tick()
{
    tieWire();

    // fu preparation
    for (auto &wrapper: fuWrappers) {
        wrapper.startCycle();
    }
    readPairQueueHeads();

    digestPairs();

    readReadyInstsFromLastCycle();

    selectReadyInsts();

    DPRINTF(DQ, "Wrappers setting instructions to be executed and waken up"
            " (actually in last cycle)\n");
    for (auto &wrapper: fuWrappers) {
        wrapper.setWakeup();
    }

    for (auto &wrapper: fuWrappers) {
        wrapper.executeInsts();
    }

    for (auto &wrapper: fuWrappers) {
        wrapper.setWakeupPostExec();
    }

    setWakeupPointersFromFUs(); // this func also insert them into wake queues

    readPointersFromLastCycleToWakeQueues();

    readWakeQueueHeads();

    selectPointersFromWakeQueues();
    // DPRINTF(DQ, "Tell dq banks who was granted\n");
    // mark it for banks
    for (unsigned b = 0; b < nBanks; b++) {
        dqs[b]->countUpPendingInst();
    }

    writeFwPointersToNextCycle(); // this func read forward pointers from banks

    wakeupInsts(); // this func also write insts to Next cycle

    diewc->tryResetRef();

        // For each bank, check
    //  whether there's an inst to be waken up
    //  whether there are multiple instruction to wake up, if so
    //      buffer it and reject further requests in following cycles
    // DPRINTF(DQ, "Bank check pending\n");
    for (auto bank: dqs) {
        bank->checkPending();
    }

    // DPRINTF(DQ, "Selecting wakeup pointers from requests\n");
    // For each bank: check status: whether I can consume from queue this cycle?
    // record the state
    genFUValidMask();

    rearrangePrioList();

    countUpPointers();

    tryResetRef();

    // if (NarrowXBarWakeup && NarrowLocalForward) {
    //     mergeLocalWKPointers();
    // }

    pickInterGroupPointers();

    for (unsigned i = 0; i < nBanks; i++) {
        dqs[i]->countUpPendingPointers();
    }
}

template<class Impl>
void DataflowQueues<Impl>::cycleStart()
{
    for (auto bank: dqs) {
        bank->cycleStart();
    }
    llBlocked = llBlockedNext;
    llBlockedNext = false;
    if (llBlocked) {
        DPRINTF(FUW, "ll blocked last cycle\n");
    }
    clearSent();

    fuFAIndex = 0;
    fuMDIndex = 0;
}


template<class Impl>
std::pair<unsigned, boost::dynamic_bitset<> >
DataflowQueues<Impl>::coordinateFU(
        DataflowQueues::DynInstPtr &inst, unsigned from)
{
    // find another FU group with desired capability.
    DPRINTF(FUW, "Coordinating req %u.%u  with llb:%i\n", from/2, from%2, llBlocked);
    int type = from%2;
    int md = OpGroups::MultDiv,
        fa = OpGroups::FPAdd;
    const unsigned group_ipr = 3;
    unsigned target;
    if (inst->opClass() == OpClass::IprAccess) {
        target = group_ipr;

    } else if (type == md){ //OpGroups::MultDiv
        target = fuPointer[md][fuMDIndex++];

    } else { // type == fa
        target = fuPointer[fa][fuFAIndex++];
    }
    DPRINTF(FUW || ObExec, "Routing inst[%lu] to %i\n", inst->seqNum, target);
    return std::make_pair(target, c->uint2Bits(target));
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
        pkt.destBits = c->uint2Bits(d);

        DPRINTF(DQGOF || Debug::DQWake || Debug::FFDisp,
                "Insert Fw Pair" ptrfmt "->" ptrfmt "\n",
                extptr(pair.dest), extptr(pair.payload));

        forwardPointerQueue[forwardPtrIndex].push_back(pkt);
        QueueWritePairBuf++;

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
    } else {
        DPRINTF(DQV2, "Skip invalid pair dest:" ptrfmt "\n",
                extptr(pkt.payload.dest));
    }
}

template<class Impl>
bool DataflowQueues<Impl>::stallToUnclog() const
{
    auto x1 = wakeupQueueClogging();
    auto x2 = fwPointerQueueClogging();
    return x1 || x2;
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

    if (!top->validPosition(c->pointer2uint(src))) {
        // src is committed
        readFromCommitted = true;

    } else if (top->logicallyLT(c->pointer2uint(src), c->pointer2uint(dest))) {
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
//    regFile[c->pointer2uint(ptr)] = val;
}

template<class Impl>
void DataflowQueues<Impl>::squashFU(InstSeqNum head_next_seq)
{

    for (auto &wrapper: fuWrappers) {
        wrapper.squash(head_next_seq);
    }
}

template<class Impl>
void DataflowQueues<Impl>::squashAll()
{
    for (auto &bank: dqs) {
        bank->clear(true);
    }
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
}

template<class Impl>
void DataflowQueues<Impl>::squashReady(InstSeqNum seq)
{
    for (auto &q: readyInstsQueues) {
        q->squash(seq);
    }
}


template<class Impl>
void
DataflowQueues<Impl>::markFwPointers(
        std::array<DQPointer, 4> &pointers, PointerPair &pair, DynInstPtr &inst)
{
    unsigned op = pair.dest.op;
    if (pointers[op].valid) {
        SRAMWritePointer++;
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
    if (pair.dest.valid) {
        if (inst) {
            DPRINTF(FFDisp || Debug::DQWake, "Pair arrives at inst[%lu] "
                    "@" ptrfmt " \n", inst->seqNum, extptr(pair.dest));
        } else {
            DPRINTF(FFDisp || Debug::DQWake, "Pair arrives at null inst "
                    "@" ptrfmt " \n", extptr(pair.dest));
        }
        DPRINTFR(FFDisp || Debug::DQWake,
                "and let it forward its value to" ptrfmt "\n",
                extptr(pair.payload));
    }
}


template<class Impl>
void DataflowQueues<Impl>::resetState()
{
    head = 0;
    tail = 0;
    forwardPtrIndex = 0;

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
    KeySrcPacket
        .name(name() + ".KeySrcPacket")
        .desc("KeySrcPacket");

    TotalPackets = SrcOpPackets + DestOpPackets + MemPackets + \
                   OrderPackets + MiscPackets;


    QueueWriteTxBuf
        .name(name() + ".QueueWriteTxBuf")
        .desc("QueueWriteTxBuf");
    QueueReadTxBuf
        .name(name() + ".QueueReadTxBuf")
        .desc("QueueReadTxBuf");
    QueueReadPairBuf
        .name(name() + ".QueueReadPairBuf")
        .desc("QueueReadPairBuf");
    QueueWritePairBuf
        .name(name() + ".QueueWritePairBuf")
        .desc("QueueWritePairBuf");
    CombWKNet
        .name(name() + ".CombWKNet")
        .desc("CombWKNet");
    CombFWNet
        .name(name() + ".CombFWNet")
        .desc("CombFWNet");
    CombSelNet
        .name(name() + ".CombSelNet")
        .desc("CombSelNet");

    SRAMWritePointer
        .name(name() + ".SRAMWritePointer")
        .desc("SRAMWritePointer");
}

template<class Impl>
void DataflowQueues<Impl>::readPairQueueHeads()
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
                QueueReadPairBuf++;
                DPRINTF(DQGOF, "Read valid pair:" ptrfmt "\n", extptr(fw_pkt.payload.dest));
            }
        }
    }
}

template<class Impl>
void DataflowQueues<Impl>::readWakeQueueHeads()
{
    for (unsigned b = 0; b < nBanks; b++) {
        for (unsigned op = 0; op < nOps; op++) {
            unsigned i = b * nOps + op;
            // read wakeup pointers
            auto &wk_pkt = wakeup_requests[i];
            const auto &q = wakeQueues[i];
            if (q.empty()) {
                wk_pkt.valid = false;
            } else {
                QueueReadTxBuf++;
                const WKPointer &ptr = q.front();
                DPRINTF(DQWake||Debug::RSProbe1,
                        "WKQ[%i] Found valid wakePtr:(%i) (%i %i) (%i)\n",
                        i, ptr.valid, ptr.bank, ptr.index, ptr.op);
                wk_pkt.valid = ptr.valid;
                wk_pkt.payload = ptr;
                unsigned d = ptr.bank * nOps + ptr.op;
                wk_pkt.dest = d;
                wk_pkt.destBits = c->uint2Bits(d);
                //                wk_pkt.source = i;
            }
        }
    }
}

template<class Impl>
void DataflowQueues<Impl>::setTimeBuf(TimeBuffer<DQTopTs> *dqtb)
{
    DQTS = dqtb;
    topFromLast = dqtb->getWire(-1);
    topToNext = dqtb->getWire(0);
}

template<class Impl>
void DataflowQueues<Impl>::dumpInstPackets(vector<DQPacket<DynInstPtr> *> &v)
{
    for (auto& pkt: v) {
        assert(pkt);
        DPRINTF(DQV2, "&pkt: %p, ", pkt);
        DPRINTFR(DQV2, "v: %d, dest: %lu, src: %d,",
                pkt->valid, pkt->destBits.to_ulong(), pkt->source);
        DPRINTFR(DQV2, " inst: %p\n", pkt->payload);

        if (Debug::ObFU && pkt->valid && pkt->payload) {
            auto inst = pkt->payload;
            DPRINTF(ObFU, "Inst[%d]:%s is competing for an FU\n",
                    inst->seqNum, inst->staticInst->disassemble(inst->instAddr()));
        }
    }
}

template<class Impl>
void DataflowQueues<Impl>::dumpPairPackets(vector<DQPacket<PointerPair> *> &v)
{
    for (auto& pkt: v) {
        assert(pkt);
//        DPRINTF(DQV2, "&pkt: %p, ", pkt);
        DPRINTF(DQV2, "v: %d, dest: %lu, src: %d,",
                 pkt->valid, pkt->destBits.to_ulong(), pkt->source);
        DPRINTFR(DQV2, " pair dest:" ptrfmt "\n", extptr(pkt->payload.dest));
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
void DataflowQueues<Impl>::setDIEWC(DIEWC *_diewc)
{
    diewc = _diewc;
    for (auto &wrapper: fuWrappers) {
        wrapper.setExec(diewc);
    }
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
    panic("Not implemented!\n");
    list<DynInstPtr> heads;
    auto ptr_i = head;
    for (unsigned count = 0; count < nBanks; count++) {
        auto ptr = c->uint2Pointer(ptr_i);
        DynInstPtr inst = dqs[ptr.bank]->readInstsFromBank(ptr);
        if (inst) {
            DPRINTF(DQRead, "read inst[%d] from DQ\n", inst->seqNum);
        } else {
            DPRINTF(DQRead, "inst@[%d] is null\n", ptr_i);
        }
        heads.push_back(inst);
        ptr_i = top->dec(ptr_i);
    }
    return heads;
}

template<class Impl>
list<typename Impl::DynInstPtr>
DataflowQueues<Impl>::getBankTails()
{
    list<DynInstPtr> tails;
    unsigned ptr_i = top->getTailPtr();
    for (unsigned count = 0; count < nBanks; count++) {
        auto ptr = c->uint2Pointer(ptr_i);
        if (ptr.group == groupID) {
            DynInstPtr inst = dqs[ptr.bank]->readInstsFromBank(ptr);
            if (inst) {
                DPRINTF(DQRead || Debug::DQGDL, "read inst[%llu] from DQ\n", inst->seqNum);
            } else {
                DPRINTF(DQRead || Debug::DQGDL, "inst@[%d] is null\n", ptr_i);
            }
            tails.push_back(inst);
        } else {
            DPRINTF(DQRead || Debug::DQGDL, "set boundary inst to null\n");
            tails.emplace_back(nullptr);
        }
        ptr_i = top->inc(ptr_i);
    }
    return tails;
}

template<class Impl>
void DataflowQueues<Impl>::tryFastCleanup()
{

}

template<class Impl>
unsigned DataflowQueues<Impl>::numInFlightFw()
{
    return numPendingFwPointers;
}

template<class Impl>
void DataflowQueues<Impl>::digestPairs()
{
    if (Debug::DQV2) {
        DPRINTF(DQV2, "Pair packets before selection\n");
        dumpPairPackets(insert_req_ptrs);
    }

    auto pair_valid_or = []
            (bool x, DQPacket<PointerPair>* y)
    {return x || y->valid;};
    bool any_valid = std::accumulate(insert_req_ptrs.begin(),insert_req_ptrs.end(),
                                     false, pair_valid_or);

    if (any_valid) {

    insert_granted_ptrs = pointerQueueBankXBar.select(insert_req_ptrs, &nullFWPkt);
    CombFWNet++;

    if (Debug::DQV2) {
        DPRINTF(DQV2, "Selected pairs:\n");
        dumpPairPackets(insert_granted_ptrs);
    }

    if (Debug::DQV2) {
        DPRINTF(DQV2, "Pair packets after selection\n");
        dumpPairPackets(insert_req_ptrs);
    }

    for (auto &ptr : insert_granted_ptrs) {
        if (ptr->valid) {
            DPRINTF(DQPair, "pair[%d] pointer" ptrfmt " granted\n",
                    ptr->source, extptr(ptr->payload.dest));
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
                DPRINTF(DQPair, "Insert fw pointer" ptrfmt "after inst[%llu] reached\n",
                        extptr(pair.dest), inst->seqNum);
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
                DPRINTF(DQPair, "Insert fw pointer" ptrfmt "before inst reached\n",
                        extptr(pair.dest));
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
}

template<class Impl>
void DataflowQueues<Impl>::writebackLoad(DynInstPtr &inst)
{

}

template<class Impl>
void DataflowQueues<Impl>::extraWakeup(const WKPointer &wk)
{
    auto q = allocateWakeQ();
    DPRINTF(DQWake||Debug::RSProbe2,
            "Push (extra) wakeup pointer" ptrfmt "to temp wakequeue[%u]\n",
            extptr(wk), q);
    tempWakeQueues[q].push_back(wk);
    DPRINTF(DQWake, "After push, numPendingWakeups = %u\n", numPendingWakeups);
}

template<class Impl>
void DataflowQueues<Impl>::alignTails()
{
    for (unsigned count = 0; count < nBanks; count++) {
        auto u = (count + top->getTailPtr()) % queueSize;
        auto ptr = c->uint2Pointer(u);
        DPRINTF(DQGDL, "Aligning ptr:" ptrfmt "\n", extptr(ptr));
        dqs[ptr.bank]->setTail(ptr.index);
    }
}

template<class Impl>
typename Impl::DynInstPtr DataflowQueues<Impl>::findBySeq(InstSeqNum seq)
{
    panic("Not implemented!\n");
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
                printf(ptrfmt, extptr(p));
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
                printf(ptrfmt, extptr(p.payload.dest));
            }
            printf("\n");
        }
    }
}

template<class Impl>
void DataflowQueues<Impl>::dumpFwQSize()
{
    DPRINTF(DQV2 || Debug::RSProbe1, "fw queue in flight = %i, cannot reset oldestRef\n", numPendingFwPointers);
    for (unsigned b = 0; b < nBanks; b++) {
        for (unsigned op = 0; op < nOps; op++) {
            DPRINTFR(DQV2 || Debug::RSProbe1, "fw queue %i.%i size: %llu\n",
                    b, op, forwardPointerQueue[b*nOps + op].size());
        }
    }
}


template<class Impl>
std::pair<InstSeqNum, Addr> DataflowQueues<Impl>::clearHalfWKQueue()
{
    unsigned oldest_to_squash = top->getHeadPtr();
    for (auto &q: wakeQueues) {
        if (q.empty()) {
            continue;
        }
        for (size_t i = 0, e = q.size(); i < e/2; i++) {
            const auto &ele = q[e - 1 - i];
            if (ele.valid) {
                unsigned p = c->pointer2uint(DQPointer(ele));
                if (top->validPosition(p)) {
                    auto p_ptr = c->uint2Pointer(p);
                    auto p_inst = dqs[p_ptr.bank]->readInstsFromBank(p_ptr);

                    if (top->logicallyLT(p, oldest_to_squash) && p_inst) {
                        oldest_to_squash = p;
                    }
                }
            }
            q.pop_back();
            numPendingWakeups--;
        }
    }
    if (oldest_to_squash == top->getHeadPtr()) {
        warn("oldest_to_squash == getHeadPtr, this case is infrequent\n");
    }
    auto ptr = c->uint2Pointer(oldest_to_squash);
    auto inst = dqs[ptr.bank]->readInstsFromBank(ptr);

    auto sec_ptr = c->uint2Pointer(top->dec(oldest_to_squash));
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
    unsigned oldest_to_squash = top->getHeadPtr();
    for (auto &q: forwardPointerQueue) {
        if (q.empty()) {
            continue;
        }
        for (size_t i = 0, e = q.size(); i < e/2; i++) {
            const auto &ele = q[e - 1 - i];
            if (ele.valid) {
                unsigned p = c->pointer2uint(ele.payload.payload);
                if (top->validPosition(p)) {
                    auto p_ptr = c->uint2Pointer(p);
                    auto p_inst = dqs[p_ptr.bank]->readInstsFromBank(p_ptr);
                    if (top->logicallyLT(p, oldest_to_squash) && p_inst) {
                        oldest_to_squash = p;
                    }
                }
            }
            q.pop_back();
            numPendingFwPointers--;
        }
    }
    if (oldest_to_squash == top->getHeadPtr()) {
        warn("oldest_to_squash == getHeadPtr, this case is infrequent\n");
    }
    auto ptr = c->uint2Pointer(oldest_to_squash);
    auto inst = dqs[ptr.bank]->readInstsFromBank(ptr);

    auto sec_ptr = c->uint2Pointer(top->dec(oldest_to_squash));
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
    DPRINTF(DQWake, "Processing wakeup queue full\n");
    DPRINTF(DQWake|| Debug::RSProbe1, "Dump before clear:\n");
    if (Debug::DQWake|| Debug::RSProbe1) {
        dumpQueues();
    }

    InstSeqNum seq;
    Addr pc;

    std::tie(seq, pc) = clearHalfWKQueue();
    top->notifyHalfSquash(seq, pc);

    bool clear_another_queue = false;
    for (const auto& q: wakeQueues) {
        if (q.size() > maxQueueDepth/2) {
            clear_another_queue = true;
        }
    }

    if (clear_another_queue) {
        std::tie(seq, pc) = clearHalfFWQueue();
        top->notifyHalfSquash(seq, pc);
    }

    DPRINTF(DQ || Debug::RSProbe1, "Dump after clear:\n");
    if (Debug::DQ || Debug::RSProbe1) {
        dumpQueues();
    }
}

template<class Impl>
void DataflowQueues<Impl>::processFWQueueFull()
{
    DPRINTF(FFDisp, "Processing pair queue full\n");
    DPRINTF(FFDisp|| Debug::RSProbe1, "Dump before clear:\n");
    if (Debug::FFDisp|| Debug::RSProbe1) {
        dumpQueues();
    }

    InstSeqNum seq;
    Addr pc;

    std::tie(seq, pc) = clearHalfFWQueue();
    top->notifyHalfSquash(seq, pc);

    bool clear_another_queue = false;
    for (const auto& q: wakeQueues) {
        if (q.size() > maxQueueDepth/2) {
            clear_another_queue = true;
        }
    }

    if (clear_another_queue) {
        std::tie(seq, pc) = clearHalfWKQueue();
        top->notifyHalfSquash(seq, pc);
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
    assert(total_queue_len <= nOps * nBanks * maxQueueDepth);
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
    if (Debug::DQV2 || Debug::RSProbe2) {
        dumpWkQ();
    }
}

template<class Impl>
void
DataflowQueues<Impl>::dumpWkQSize()
{
    for (unsigned b = 0; b < nBanks; b++) {
        for (unsigned op = 0; op < nOps; op++) {
            DPRINTFR(DQV2 || RSProbe1, "wake queue %i.%i size: %llu\n",
                    b, op, wakeQueues[b*nOps + op].size());
        }
    }
}

template<class Impl>
void
DataflowQueues<Impl>::readPointersFromLastCycleToWakeQueues()
{
    DPRINTF(DQ, "Reading wk pointers from banks to wake queus\n");
    // push forward pointers to queues
    for (unsigned b = 0; b < nBanks; b++) {
        for (unsigned op = 0; op < nOps; op++) {
            auto &ptr = fromLastCycle->pointers[b * nOps + op];
            if (ptr.valid) {
                DPRINTF(DQWake||Debug::RSProbe2,
                        "Push WakePtr" ptrfmt "to wakequeue[%u]\n",
                        extptr(ptr), b);
                pushToWakeQueue(b * nOps + op, WKPointer(ptr));
                numPendingWakeups++;
                DPRINTF(DQWake, "After push, numPendingWakeups = %u\n", numPendingWakeups);
                if (wakeQueues[b * nOps + op].size() > maxQueueDepth) {
                    processWKQueueFull();
                }
            } else {
                DPRINTF(DQWakeV1, "Ignore Invalid WakePtr" ptrfmt "\n", extptr(ptr));
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
            if (!top->validPosition(c->pointer2uint(DQPointer(wk_ptr)))) {
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
    KeySrcPacket++;
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
    QueueWriteTxBuf++;
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
        if (c->pointer2uint(lptr) < c->pointer2uint(rptr)) {
            return -1;
        } else if (c->pointer2uint(lptr) > c->pointer2uint(rptr)) {
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
        for (int i: c->MultDivOps) {
            if (i == op) {
                return true;
            }
        }
        return false;
    }
    if (op_group == OpGroups::FPAdd) {
        for (int i: c->FPAddOps) {
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
    panic("Not implemented\n");
    std::shuffle(std::begin(fuPointer[OpGroups::MultDiv]),
            std::end(fuPointer[OpGroups::MultDiv]), gen);
    std::shuffle(std::begin(fuPointer[OpGroups::FPAdd]),
            std::end(fuPointer[OpGroups::FPAdd]), gen);
    DPRINTF(FUW || ObExec, "shuffled pointers:\n");
    for (const auto x:fuPointer[OpGroups::MultDiv]) {
        DPRINTFR(FUW || ObExec, "%i ", x);
    }
    DPRINTFR(FUW || ObExec, "\n");
    for (const auto x:fuPointer[OpGroups::FPAdd]) {
        DPRINTFR(FUW || ObExec, "%i ", x);
    }
    DPRINTFR(FUW || ObExec, "\n");
}

template<class Impl>
void
DataflowQueues<Impl>::mergeLocalWKPointers()
{
    for (auto bank: dqs) {
        bank->mergeLocalWKPointers();
    }
}

template<class Impl>
void DataflowQueues<Impl>::put2OutBuffer(const WKPointer &wk_pointer)
{
    outQueue.push_back(wk_pointer);
}

template<class Impl>
void DataflowQueues<Impl>::receivePointers(const WKPointer &wk_pointer)
{
    if (wk_pointer.group == groupID) {
        extraWakeup(wk_pointer);
    } else {
        put2OutBuffer(wk_pointer);
    }
}

template<class Impl>
void DataflowQueues<Impl>::clearSent()
{
    interGroupSent = 0;
}

template<class Impl>
void DataflowQueues<Impl>::transmitPointers()
{
    while (!outQueue.empty() && interGroupSent < interGroupBW) {
        const WKPointer &wk_pointer = outQueue.front();
        top->sendToNextGroup(groupID, wk_pointer);
        interGroupSent++;
        outQueue.pop_front();
    }
}

template<class Impl>
typename DataflowQueues<Impl>::XDataflowQueueBank *
DataflowQueues<Impl>::operator[](unsigned bank)
{
    return dqs[bank];
}

template<class Impl>
void DataflowQueues<Impl>::setGroupID(unsigned id)
{
    groupID = id;

    std::ostringstream s;
    s << "DQGroup" << groupID;
    _name = s.str();
}

template<class Impl>
void DataflowQueues<Impl>::tieWire()
{
    fromLastCycle = &(topFromLast->groupTs[groupID]);
    toNextCycle = &(topToNext->groupTs[groupID]);
}

template<class Impl>
void DataflowQueues<Impl>::pickInterGroupPointers()
{
    DPRINTF(DQWake, "Checking inter-group pointers in time buffer\n");
    // from normal queue
    for (unsigned i = 0; i < c->nBanks * c->nOps; i++) {
        DQPointer &p = toNextCycle->pointers[i];
        if (p.valid && p.group != groupID) {
            DPRINTF(DQWake,
                    "Move" ptrfmt "to next group buffer and invalidate it\n", extptr(p));
            put2OutBuffer(WKPointer(p));
            p.valid = false;
        }
    }

    // from extra queue
    DPRINTF(DQWake, "Checking inter-group pointers in temp wake queue\n");
    for (unsigned i = 0; i < nOps*nBanks; i++) {
        auto it = tempWakeQueues[i].begin();
        while (it != tempWakeQueues[i].end()) {
            const auto &p = *it;
            if (p.valid && p.group != groupID) {
                put2OutBuffer(p);
                DPRINTF(DQWake,
                        "Move" ptrfmt "to next group buffer and invalidate it\n", extptr(p));
                it = tempWakeQueues[i].erase(it);
            } else {
                it++;
            }
        }
    }
}

template<class Impl>
void DataflowQueues<Impl>::setWakeupPointersFromFUs()
{
    for (unsigned b = 0; b < nBanks; b++) {
        const DQPointer &src = fuWrappers[b].toWakeup[SrcPtr];
        const DQPointer &dest = fuWrappers[b].toWakeup[DestPtr];

        int q1 = -1, q2 = -1;
        if (dest.valid) {
            q1 = allocateWakeQ();
            DPRINTF(DQWake || Debug::RSProbe2,
                    "Got wakeup pointer to" ptrfmt ", val %i: %lu, pushed to wakeQueue[%i]\n",
                    extptr(dest), dest.hasVal, dest.val.i, q1);
            if (dest.group == groupID) {
                pushToWakeQueue(q1, WKPointer(dest));
                numPendingWakeups++;
                DPRINTF(DQWake, "After push, numPendingWakeups = %u\n", numPendingWakeups);
            } else {
                put2OutBuffer(WKPointer(dest));
                DPRINTF(DQWake, "Move to inter-group buffer\n");
            }

        }

        if (src.valid) {
            q2 = allocateWakeQ();
            DPRINTF(DQWake || Debug::RSProbe2,
                    "Got inverse wakeup pointer to" ptrfmt ", pushed to wakeQueue[%i]\n",
                    extptr(src), q2);
            pushToWakeQueue(q2, WKPointer(src));
            numPendingWakeups++;
            DPRINTF(DQWake, "After push, numPendingWakeups = %u\n", numPendingWakeups);

        }

        if ((q1 >= 0 && wakeQueues[q1].size() > maxQueueDepth) ||
                (q2 >= 0 && wakeQueues[q2].size() > maxQueueDepth)) {
            processWKQueueFull();
        }
    }
}

template<class Impl>
void DataflowQueues<Impl>::readReadyInstsFromLastCycle()
{
    DPRINTF(DQ, "Setup fu requests\n");
    // For each bank
    //  get ready instructions produced by last tick from time struct
    for (unsigned i = 0; i < nBanks*OpGroups::nOpGroups; i++) {
        fu_requests[i].valid = fromLastCycle->instValids[i];
        if (fromLastCycle->instValids[i]) {
            DPRINTF(DQ || Debug::DQWake || ObExec,
                    "inst source: %i, &inst: %lu\n",
                    i, fromLastCycle->insts[i]->seqNum);

            fu_requests[i].payload = fromLastCycle->insts[i];
            std::tie(fu_requests[i].dest, fu_requests[i].destBits) =
                coordinateFU(fromLastCycle->insts[i], i);
        } else {
            DPRINTF(DQV2, "From last cycle[%d] invalid\n", i);
        }
    }
}


template<class Impl>
void DataflowQueues<Impl>::selectReadyInsts()
{
    // shuffleNeighbors();
    // For each bank

    //  For each valid ready instruction compete for a FU via the omega network
    //  FU should ensure that this is no write port hazard next cycle
    //      If FU grant an inst, pass its direct child pointer to this FU's corresponding Pointer Queue
    //      to wake up the child one cycle before its value arrived

    DPRINTF(DQ, "FU selecting ready insts from banks\n");
    if (Debug::DQV2 || Debug::ObFU) {
        DPRINTF(DQV2, "FU req packets:\n");
        dumpInstPackets(fu_req_ptrs);
    }

    auto inst_pkt_valid_or = []
        (bool x, DQPacket<DynInstPtr>* y)
        {return x || y->valid;};
    bool any_valid = std::accumulate(fu_req_ptrs.begin(), fu_req_ptrs.end(),
            false, inst_pkt_valid_or);
    if (any_valid) {

        fu_granted_ptrs = bankFUXBar.select(fu_req_ptrs,
                &nullInstPkt, nBanks * nOpGroups);
        CombSelNet++;
        for (unsigned b = 0; b < nBanks; b++) {
            for (unsigned x = 0; x < 2; x++) {

                unsigned idx = b * 2 + x;
                assert(fu_granted_ptrs[idx]);
                if (!fu_granted_ptrs[idx]->valid || !fu_granted_ptrs[idx]->payload) {
                    continue;
                }
                DynInstPtr &inst = fu_granted_ptrs[idx]->payload;

                DPRINTF(DQWake || Debug::ObFU || Debug::ObExec,
                        "Inst[%d]:%s (req index: %i) selected by fu %u\n",
                        inst->seqNum,
                        inst->staticInst->disassemble(inst->instAddr()),
                        idx,
                        b);

                unsigned source_bank = fu_granted_ptrs[idx]->source/2;

                if (inst->isSquashed()) {
                    DPRINTF(FFSquash, "Skip squashed inst[%llu] from bank[%u] \n",
                            inst->seqNum, source_bank);
                    dqs[source_bank]->clearPending(inst);

                } else {
                    InstSeqNum waitee;
                    bool can_accept = fuWrappers[b].canServe(inst, waitee);
                    if (waitee && waitee > inst->seqNum) {
                        oldWaitYoung++;
                    }
                    if (can_accept) {
                        DPRINTF(DQWake, "Inst[%lli] from bank[%u] accepted\n",
                                inst->seqNum, source_bank);
                        fuWrappers[b].consume(inst);
                        dqs[source_bank]->clearPending(inst);

                    } else if (opLat[inst->opClass()] > 1){
                        readyWaitTime[b] += 1;
                        llBlockedNext = true;
                    }
                }
            }
        }
    }
}

template<class Impl>
void DataflowQueues<Impl>:: writeFwPointersToNextCycle()
{
    // todo: write forward pointers from bank to time buffer!
    for (unsigned b = 0; b < nBanks; b++) {
        std::vector<DQPointer> forward_ptrs = dqs[b]->readPointersFromBank();

        if (NarrowXBarWakeup && NarrowLocalForward) {
            for (unsigned op = 0; op < nOps; op++) {
                auto &ptr = forward_ptrs[op];
                if (ptr.valid && (ptr.bank == b)) {
                    // TODO: move to time buf
                    auto &pending_ptr = dqs[b]->localWKPointers[ptr.op];
                    if (!pending_ptr.valid) {
                        pending_ptr = WKPointer(ptr);
                        pending_ptr.isLocal = true;
                        DPRINTF(DQ, "Move wk ptr (%i) (%i %i) (%i) to local\n",
                                ptr.valid, ptr.bank, ptr.index, ptr.op);
                        ptr.valid = false;
                    }
                }
            }
        }

        for (unsigned op = 0; op < nOps; op++) {
            const auto &ptr = forward_ptrs[op];
            // dqs[b]->dumpOutPointers();
            if (ptr.valid) {
                DPRINTF(DQWake, "Putting wk ptr" ptrfmt "to time buffer\n", extptr(ptr));
            } else {
                DPRINTF(DQV2, "Putting wk ptr" ptrfmt "to time buffer\n", extptr(ptr));
            }
            toNextCycle->pointers[b * nOps + op] = forward_ptrs[op];
        }
    }
}

template<class Impl>
void DataflowQueues<Impl>::wakeupInsts()
{
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
}

template<class Impl>
void DataflowQueues<Impl>::selectPointersFromWakeQueues()
{
    auto wk_ptr_valid_or = []
        (bool x, DQPacket<WKPointer>* y)
        {return x || y->valid;};
    bool any_valid = std::accumulate(wakeup_req_ptrs.begin(),wakeup_req_ptrs.end(),
            false, wk_ptr_valid_or);

    if (any_valid) {
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
        CombWKNet++;

        for (auto &ptr : wakeup_granted_ptrs) {
            if (ptr->valid) {
                DPRINTF(DQWake||Debug::RSProbe2,
                        "WakePtr[%d] pointer" ptrfmt "granted\n",
                        ptr->source, extptr(ptr->payload));
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
                    // DPRINTF(DQWake, "granted[%i.%i]: dest:%i" ptrfmt "\n",
                    //         b, op, pkt->valid, extptr(pkt->payload));
                    DPRINTF(DQWake, "granted[%i.%i]: dest:[%llu] (%i) " ptrfmt "\n",
                            b, op, pkt->destBits.to_ulong(),
                            pkt->valid, extptr(pkt->payload));

                    WKPointer &ptr = pkt->payload;
                    if (ptr.wkType == WKPointer::WKOp) {
                        if (ptr.op == 0) {
                            DestOpPackets++;
                        } else {
                            SrcOpPackets++;
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

    } else {
        for (unsigned b = 0; b < nBanks; b++) {
            dqs[b]->canServeNew();
        }
    }

    if (Debug::QClog) {
        dumpQueues();
    }
}

} // namespace

#include "cpu/forwardflow/isa_specific.hh"

template class FF::DataflowQueues<FFCPUImpl>;
