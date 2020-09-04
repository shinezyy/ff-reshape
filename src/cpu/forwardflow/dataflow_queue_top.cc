//
// Created by zyy on 2020/1/15.
//

#include "dataflow_queue_top.hh"
#include "debug/DQ.hh"
#include "debug/DQGDL.hh"
#include "debug/DQGDisp.hh"
#include "debug/DQGOF.hh"
#include "debug/DQV2.hh"
#include "debug/DQWake.hh"
#include "debug/FFCommit.hh"
#include "debug/FFDisp.hh"
#include "debug/FFSquash.hh"
#include "debug/NoSQSMB.hh"
#include "debug/RSProbe1.hh"
#include "params/DerivFFCPU.hh"
#include "params/FFFUPool.hh"

namespace FF
{

using namespace std;

template<class Impl>
DQTop<Impl>::DQTop(DerivFFCPUParams *params)
        :
        c(params),
        head(0),
        tail(0),
        headTerm(0),
        dispatchWidth(params->dispatchWidth),
        halfSquash(false),
        halfSquashSeq(0),
        halfSquashPC(0),
        pseudoCenterWKPointerBuffer(params->numDQGroups),
        center2GroupRate(16),
        interGroupBuffer(params->numDQGroups),
        group2GroupRate(4)
{

    memDepUnit.init(params, DummyTid);
    memDepUnit.setIQ(this);

    for (unsigned g = 0; g < params->numDQGroups; g++) {
        dqGroups.push_back(new DataflowQueues(params, g, &c, this));
    }
    committingGroup = dqGroups[0];
    dispatchingGroup = dqGroups[0];
    clearPairBuffer();
    clearInstBuffer();
    // printf("inter buffer size: %zu\n", interGroupBuffer.size());
}

template<class Impl>
void DQTop<Impl>::cycleStart()
{
    // TODO: clean up new stuffs in multiple DQ groups

    // per group start
    for (auto &group: dqGroups) {
        group->cycleStart();
    }
    halfSquash = false;
    halfSquashSeq = 0;
}

template<class Impl>
void DQTop<Impl>::tick()
{
    // for each group dispatch
    distributeInstsToGroup();

    if (c.nGroups == 1) {
        groupsRxFromCenterBuffer();
        for (auto group: dqGroups) {
            group->mergeExtraWKPointers();
        }
    }

    // for each group dispatch pairs
    distributePairsToGroup();

    // per group tick, because there is no combinational signals across groups
    // their "ticks" can be executed in parallel
    for (auto group: dqGroups) {
        group->tick();
    }

    // for each group receive from prev group
    groupsRxFromPrevGroup();
    // for each group send
    // this execute order is  to guarantee 2 cycle latency
    groupsTxPointers();

    if (c.nGroups > 1) {
        // for each group receive from center buffer
        // this will be executed before lsq/mdu send to guarantee 2 cycle latency
        groupsRxFromCenterBuffer();
        for (auto group: dqGroups) {
            group->mergeExtraWKPointers();
        }
    }

    replayMemInsts();
    DPRINTF(DQ, "Size of blockedMemInsts: %llu, size of retryMemInsts: %llu\n",
            blockedMemInsts.size(), retryMemInsts.size());


}

template<class Impl>
void DQTop<Impl>::replayMemInst(DynInstPtr &inst)
{
    memDepUnit.replay();
}

template<class Impl>
void DQTop<Impl>::scheduleNonSpec()
{
    if (!getTail()) {
        DPRINTF(FFSquash, "Ignore scheduling attempt to squashing inst\n");
        return;
    }
    WKPointer wk = WKPointer(getTail()->dqPosition);
    auto p = c.uint2Pointer(tail);
    DPRINTF(DQ, "Scheduling non spec inst @ (%i %i)\n", p.bank, p.index);
    wk.wkType = WKPointer::WKMisc;
    centralizedExtraWakeup(wk);
}

template<class Impl>
void DQTop<Impl>::reExecTailLoad()
{
    if (!getTail()) {
        DPRINTF(FFSquash, "Ignore scheduling attempt to squashing inst\n");
        return;
    }
    if (!getTail()->isLoad()) {
        DPRINTF(NoSQSMB, "Head inst is not load!\n");
        return;
    }
    WKPointer wk = WKPointer(getTail()->dqPosition);
    DPRINTF(DQ, "Scheduling tail load inst " ptrfmt "\n", extptr(wk));
    wk.wkType = WKPointer::WKLdReExec;
    centralizedExtraWakeup(wk);
}


template<class Impl>
void DQTop<Impl>::centralizedExtraWakeup(const WKPointer &wk)
{
    // store in a buffer? or insert directly?
    pseudoCenterWKPointerBuffer[wk.group].push_back(wk);
    RegWriteCenterWKBuf++;
}

template<class Impl>
bool DQTop<Impl>::hasTooManyPendingInsts()
{
    for (auto group: dqGroups) {
        if (group->hasTooManyPendingInsts()) {
            return true;
        }
    }
    return false;
}

template<class Impl>
void DQTop<Impl>::advanceHead()
{
    if (isEmpty() && centerInstBuffer.empty()) {
        clearInflightPackets();
        head = inc(head);
        tail = inc(tail);
        if (head == 0) {
            headTerm = (headTerm + 1) % c.termMax;
            DPRINTF(FFSquash, "Update head term to %u\n", headTerm);
        }
        return;
    } else {
        assert(!isFull());
        head = inc(head);
    }

    if (head == 0) {
        headTerm = (headTerm + 1) % c.termMax;
        DPRINTF(FFSquash, "Update head term to %u\n", headTerm);
    }

    auto allocated = c.uint2Pointer(head);
    DPRINTF(DQGOF || Debug::FFDisp, "Head = %u, newly allocated at" ptrfmt "\n", head, extptr(allocated));
    auto dead_inst = (*dqGroups[allocated.group])[allocated.bank]->readInstsFromBank(allocated);
    if (dead_inst) {
        DPRINTF(FFCommit, "Dead inst[%llu] found unexpectedly\n", dead_inst->seqNum);
        assert(!dead_inst);
    }
}

template<class Impl>
void DQTop<Impl>::clearInflightPackets()
{
    for (auto group: dqGroups) {
        group->clearInflightPackets();
    }
}

template<class Impl>
typename Impl::DynInstPtr
DQTop<Impl>::findBySeq(InstSeqNum seq)
{
    for (unsigned u = tail; logicallyLET(u, head); u = inc(u)) {
        auto p = c.uint2Pointer(u);
        auto inst = (*dqGroups[p.group])[p.bank]->readInstsFromBank(p);
        if (inst && inst->seqNum == seq) {
            return inst;
        }
    }
    panic("It must be it DQ!\n");
}

template<class Impl>
typename Impl::DynInstPtr
DQTop<Impl>::getTail()
{
    DynInstPtr inst = getTailInDQ();
    if (!inst && !centerInstBuffer.empty()) {
        return centerInstBuffer.front();
    }
    return inst;
}

template<class Impl>
typename Impl::DynInstPtr
DQTop<Impl>::getTailInDQ()
{
    auto head_ptr = c.uint2Pointer(tail);
    DataflowQueues *group = dqGroups[head_ptr.group];
    DataflowQueueBank *bank = (*group)[head_ptr.bank];
    const auto &inst = bank->readInstsFromBank(head_ptr);
    return inst;
}


template<class Impl>
bool DQTop<Impl>::isFull() const
{
    bool res = head == c.dqSize - 1 ? tail == 0 : head == tail - 1;
    if (res) {
        DPRINTF(DQ, "DQ is full head = %d, tail = %d\n", head, tail);
    }
    return res;
}

template<class Impl>
bool DQTop<Impl>::logicallyLT(unsigned left, unsigned right) const
{
    unsigned x = left, y = right;
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
bool DQTop<Impl>::logicallyLET(unsigned left, unsigned right) const
{
    return logicallyLT(left, right) || left == right;
}

template<class Impl>
unsigned DQTop<Impl>::dec(unsigned u) const
{
    return u == 0 ? c.dqSize - 1 : u - 1;
}

template<class Impl>
unsigned DQTop<Impl>::inc(unsigned u) const
{
    return (u+1) % c.dqSize;
}

template<class Impl>
void DQTop<Impl>::maintainOldestUsed()
{
    if (!validPosition(oldestUsed)) {
        oldestUsed = getTailPtr();
        DPRINTF(DQ || Debug::RSProbe1, "Set oldestUsed to tail: %u\n", oldestUsed);
    }
}

template<class Impl>
bool DQTop<Impl>::validPosition(unsigned u) const
{
    DPRINTF(DQV2, "head: %i tail: %i u: %u\n", head, tail, u);
    if (head >= tail) {
        return (u <= head && u >= tail);
    } else {
        return (u <= head || u >= tail);
    }
}

template<class Impl>
std::pair<bool, PointerPair>
DQTop<Impl>::insertBarrier(DynInstPtr &inst)
{
    memDepUnit.insertBarrier(inst);
    return insertNonSpec(inst);
}

template<class Impl>
std::pair<bool, PointerPair>
DQTop<Impl>::insertNonSpec(DynInstPtr &inst)
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
std::pair<bool, PointerPair>
DQTop<Impl>::insert(DynInstPtr &inst, bool nonSpec)
{
    // TODO: this is centralized now; Decentralize it with buffers
    // todo: send to allocated DQ position
    assert(inst);

    bool jumped = false;

    TermedPointer allocated = inst->dqPosition;

    // this is for checking; we do not need to decentralize it
    DPRINTF(DQWake || Debug::DQGDisp,
            "Inst[%lli]: %s allocated @" ptrfmt "\n",
            inst->seqNum,
            inst->staticInst->disassemble(inst->instAddr()),
            extptr(allocated));
    auto group = dqGroups[allocated.group];
    auto dead_inst = (*group)[allocated.bank]->readInstsFromBank(allocated);
    if (dead_inst) {
        DPRINTF(FFCommit, "Dead inst[%llu] found unexpectedly\n", dead_inst->seqNum);
        assert(!dead_inst);
    }

    // update tail before insertion to avoid ``readInst'' read newly inserted inst
    if (isEmpty()) {
        tail = c.pointer2uint(allocated); //keep them together
        DPRINTF(DQ || Debug::DQGDL, "tail becomes %u to keep with head\n", tail);
        jumped = true;
        dispatchingGroup->alignTails();
    }
    centerInstBuffer.push_back(inst);
    RegWriteCenterInstBuf++;

    inst->setInDQ();
    // we don't need to add to dependents or producers here,
    //  which is maintained in DIEWC by archState

    PointerPair pair;
    pair.dest.valid = false;
    if (inst->isMemRef() && !nonSpec) {
        pair = memDepUnit.insert(inst);
    }

    return std::make_pair(jumped, pair);
}

template<class Impl>
void DQTop<Impl>::insertForwardPointer(PointerPair pair)
{
    if (pair.dest.valid) {
        centerPairBuffer.push_back(pair);
        RegWriteCenterPairBuf++;
    }
}

template<class Impl>
list<typename Impl::DynInstPtr>
DQTop<Impl>::getBankHeads()
{
    c.notImplemented();
    return {nullptr};
}

template<class Impl>
list<typename Impl::DynInstPtr>
DQTop<Impl>::getBankTails()
{
    unsigned tail_group_id = c.uint2Pointer(tail).group;
    DPRINTF(DQGDL, "Reading tails from group %u\n", tail_group_id);
    DataflowQueues *tail_group = dqGroups[tail_group_id];
    // TODO: this will cause fragment, but is simple
    return tail_group->getBankTails();
}

template<class Impl>
bool DQTop<Impl>::stallToUnclog() const
{
    if (isFull()) return true;
    for (auto group: dqGroups) {
        if (group->stallToUnclog()) return true;
    }
    return false;
}

template<class Impl>
void DQTop<Impl>::retireHead(bool result_valid, FFRegValue v)
{
    assert(!isEmpty());
    DQPointer head_ptr = c.uint2Pointer(tail);
    alignTails();
    DPRINTF(FFCommit, "Position of inst to commit:(%d %d)\n",
            head_ptr.bank, head_ptr.index);
    DataflowQueues &group = *dqGroups[head_ptr.group];
    DynInstPtr head_inst = group[head_ptr.bank]->readInstsFromBank(head_ptr);

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
    group[head_ptr.bank]->advanceTail();
    if (head != tail) {
        tail = inc(tail);
        DPRINTF(DQ, "tail becomes %u in retiring\n", tail);
    }

    DPRINTF(FFCommit, "Advance youngest ptr to %d, oldest ptr to %d\n", head, tail);
}

template<class Impl>
bool DQTop<Impl>::isEmpty() const
{
    DPRINTF(DQ, "head: %u, tail: %u\n", head, tail);
    return head == tail && !getHead();
}

template<class Impl>
unsigned DQTop<Impl>::numInDQ() const
{
    return head < tail ? head + c.dqSize - tail + 1 : head - tail + 1;
}

template<class Impl>
unsigned DQTop<Impl>::numFree() const
{
    return c.dqSize - numInDQ();
}

template<class Impl>
void DQTop<Impl>::tryFastCleanup()
{
// todo: clean bubbles left by squashed instructions
    auto inst = getTail();
    if (inst) {
        DPRINTF(FFSquash, "Strangely reaching fast cleanup when DQ tail is not null!\n");
    }
    unsigned old_tail = tail;
    while (!inst && !isEmpty()) {
        auto tail_ptr = c.uint2Pointer(tail);
        auto &group = *dqGroups[tail_ptr.group];
        auto bank = group[tail_ptr.bank];
        bank->setTail(c.uint2Pointer(tail).index);
        bank->advanceTail();

        tail = inc(tail);
        DPRINTF(FFSquash, "tail becomes %u in fast clean up\n", tail);
        inst = getTail();
        diewc->DQPointerJumped = true;
    }
    if (isEmpty()) {
        tail = inc(tail);
        head = inc(head);
        if (head == 0) {
            headTerm = (headTerm + 1) % c.termMax;
            DPRINTF(FFSquash, "Update head term to %u\n", headTerm);
        }
    }
    DPRINTF(FFCommit || Debug::FFSquash,
            "Fastly advance youngest ptr to %d, oldest ptr to %d" ptrfmt "\n",
            head, tail, extptr(c.uint2Pointer(tail)));
    for (auto group: dqGroups) {
        group->clearPending2SquashedRange(old_tail, tail);
    }
}

template<class Impl>
void DQTop<Impl>::squash(BasePointer p, bool all, bool including)
{
    centerInstBuffer.clear();

    if (all) {
        DPRINTF(FFSquash, "DQ: squash ALL instructions\n");
        for (auto group: dqGroups) {
            group->squashAll();
        }
        head = 0;
        tail = 0;
        memDepUnit.squash(0, DummyTid);
        diewc->DQPointerJumped = true;
        cpu->removeInstsUntil(0, DummyTid);
        headTerm = (headTerm + 1) % c.termMax;
        DPRINTF(FFSquash, "Update head term to %u\n", headTerm);
        return;
    }

    unsigned u = c.pointer2uint(p);
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

    auto head_next_p = c.uint2Pointer(head_next);

    auto head_inst_next = (*dqGroups[head_next_p.group])[head_next_p.bank]->readInstsFromBank(head_next_p);
    if (!(head_inst_next || head_next == tail)) {
        DPRINTF(FFSquash, "head: %i, tail: %i, head_next: %i\n", head, tail, head_next);
        assert(head_inst_next || head_next == tail);
    }
    DPRINTF(FFSquash, "DQ: squash all instruction after inst[%llu]\n",
            head_inst_next->seqNum);

    memDepUnit.squash(head_inst_next->seqNum, DummyTid);

    cpu->removeInstsUntil(head_inst_next->seqNum, DummyTid);

    if (head != head_next) {
        while (validPosition(u) && logicallyLET(u, head)) {
            auto ptr = c.uint2Pointer(u);
            auto bank = (*dqGroups[ptr.group])[ptr.bank];
            bank->erase(ptr, true);
            if (u == head) {
                break;
            }
            u = inc(u);
            if (u == 0) {
                headTerm = (headTerm + 1) % c.termMax;
                DPRINTF(FFSquash, "Update head term to %u\n", headTerm);
            }
        }
        DPRINTF(FFSquash, "DQ logic head becomes %d, physical is %d, tail is %d\n",
                head_next, head, tail);

        for (auto group: dqGroups) {
            group->squashReady(head_inst_next->seqNum);
            group->squashFU(head_inst_next->seqNum);
            for (auto bank: group->dqs) {
                // squash ready
                bank->squashReady(p);
            }
        }

    } else {
        DPRINTF(FFSquash, "Very rare case: the youngset inst mispredicted, do nothing\n");
    }
}

template<class Impl>
bool DQTop<Impl>::queuesEmpty()
{
    for (DataflowQueues *group: dqGroups) {
        if (!group->queuesEmpty()) return false;
    }
    return true;
}

template<class Impl>
void DQTop<Impl>::setTimeBuf(TimeBuffer<DQTopTS> *dqtb)
{
    for (unsigned g = 0; g < c.nGroups; g++){
        dqGroups[g]->setTimeBuf(dqtb);
    }
}

template<class Impl>
void DQTop<Impl>::setLSQ(LSQ *lsq)
{
    for (DataflowQueues *group: dqGroups) {
        group->setLSQ(lsq);
    }
}

template<class Impl>
void DQTop<Impl>::setDIEWC(DIEWC *_diewc)
{
    diewc = _diewc;
    for (DataflowQueues *group: dqGroups) {
        group->setDIEWC(_diewc);
    }
}

template<class Impl>
void DQTop<Impl>::deferMemInst(DynInstPtr &inst)
{
    deferredMemInsts.push_back(inst);
}

template<class Impl>
typename Impl::DynInstPtr
DQTop<Impl>::getDeferredMemInstToExecute()
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
typename Impl::DynInstPtr DQTop<Impl>::getBlockedMemInst()
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
void DQTop<Impl>::rescheduleMemInst(DynInstPtr &inst, bool isStrictOrdered, bool isFalsePositive)
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
void DQTop<Impl>::replayMemInsts()
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
void DQTop<Impl>::blockMemInst(DynInstPtr &inst)
{
    inst->translationStarted(false);
    inst->translationCompleted(false);
    inst->clearCanIssue();
    inst->clearIssued();

    // Omegaflow specific:
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
void DQTop<Impl>::cacheUnblocked()
{
    retryMemInsts.splice(retryMemInsts.end(), blockedMemInsts);
    DPRINTF(DQ, "blocked mem insts size: %llu, retry mem inst size: %llu\n",
            blockedMemInsts.size(), retryMemInsts.size());
    cpu->wakeCPU();
}

template<class Impl>
bool DQTop<Impl>::checkViolation(DynInstPtr &inst) {
    assert(inst->isLoad());
    if (inst->bypassOp) {
        // compare bypassed value against dest
        if (inst->bypassVal.i != inst->getDestValue().i) {
            return true;
        } else {
            inst->loadVerified = true;
        }
    } else {
        // compare old dest against new dest
        inst->loadVerifying = false;
        if (inst->speculativeLoadValue.i != inst->getDestValue().i) {
            return true;
        } else {
            inst->loadVerified = true;
        }
    }
    DPRINTF(NoSQSMB, "No violation detected, mark inst as verified\n");
    return false;
}


template<class Impl>
bool DQTop<Impl>::writebackLoad(DynInstPtr &inst)
{
    DPRINTF(DQWake, "Writeback Load[%lu]\n", inst->seqNum);
    assert(!inst->loadVerified);

    if (inst->bypassOp || (inst->loadVerifying && inst->execCount == 2)) {
        bool violation = checkViolation(inst);
        if (violation) {
            DPRINTF(NoSQSMB, "violation detected!\n");
            return true;
        }
    }

    bool not_verifying = !inst->bypassOp && // if bypassOp writebackLoad must be verifying
            inst->execCount == 1; // ==0: impossible; ==2: verifying

    if (inst->pointers[0].valid && not_verifying) {
        WKPointer wk(inst->pointers[0]);
        DPRINTF(DQWake,
                "Sending pointer to consumer" ptrfmt
                "that depends on load[%llu]" ptrfmt "loaded value: %llu\n",
                extptr(wk),
                inst->seqNum, extptr(inst->dqPosition), inst->getDestValue().i
        );
        wk.hasVal = true;
        wk.val = inst->getDestValue();
        centralizedExtraWakeup(wk);

        completeMemInst(inst);
    } else {
        auto &p = inst->dqPosition;
        DPRINTF(DQWake, "Mark itself[%llu]" ptrfmt "ready\n",
                inst->seqNum, extptr(p));
        inst->opReady[0] = true;
        completeMemInst(inst);
    }
    return false;
}

template<class Impl>
void DQTop<Impl>::completeMemInst(DynInstPtr &inst)
{
    inst->receivedDest = true;
    if (inst->isMemRef()) {
        // complateMemInst
        inst->memOpDone(true);

    } else if (inst->isMemBarrier() || inst->isWriteBarrier()) {
        memDepUnit.completeBarrier(inst);
    }
}

template<class Impl>
void DQTop<Impl>::takeOverFrom()
{
    resetState();
}

template<class Impl>
void DQTop<Impl>::drainSanityCheck() const
{
    assert(this->isEmpty());
    memDepUnit.drainSanityCheck();
}

template<class Impl>
void DQTop<Impl>::resetState()
{
    for (DataflowQueues *group: dqGroups) {
        group->resetState();
    }
    blockedMemInsts.clear();
    retryMemInsts.clear();
}

template<class Impl>
void DQTop<Impl>::resetEntries()
{
    for (DataflowQueues *group: dqGroups) {
        group->resetEntries();
    }
}

template<class Impl>
void DQTop<Impl>::regStats()
{
    memDepUnit.regStats();

    for (auto group: dqGroups) {
        group->regStats();
    }
    HalfSquashes
            .name(name() + ".HalfSquashes")
            .desc("HalfSquashes");

    RegReadCenterInstBuf
        .name(name() + ".RegReadCenterInstBuf")
        .desc("RegReadCenterInstBuf");
    RegReadCenterPairBuf
        .name(name() + ".RegReadCenterPairBuf")
        .desc("RegReadCenterPairBuf");
    RegReadCenterWKBuf
        .name(name() + ".RegReadCenterWKBuf")
        .desc("RegReadCenterWKBuf");
    RegReadInterGroupWKBuf
        .name(name() + ".RegReadInterGroupWKBuf")
        .desc("RegReadInterGroupWKBuf");
    RegWriteCenterInstBuf
        .name(name() + ".RegWriteCenterInstBuf")
        .desc("RegWriteCenterInstBuf");
    RegWriteCenterPairBuf
        .name(name() + ".RegWriteCenterPairBuf")
        .desc("RegWriteCenterPairBuf");
    RegWriteCenterWKBuf
        .name(name() + ".RegWriteCenterWKBuf")
        .desc("RegWriteCenterWKBuf");
    RegWriteInterGroupWKBuf
        .name(name() + ".RegWriteInterGroupWKBuf")
        .desc("RegWriteInterGroupWKBuf");
}

template<class Impl>
unsigned DQTop<Impl>::numInFlightFw()
{
    unsigned sum = centerPairBuffer.size();
    for (DataflowQueues *group: dqGroups) {
        sum += group->numInFlightFw();
    }
    return sum;
}

template<class Impl>
void DQTop<Impl>::dumpFwQSize()
{
    for (DataflowQueues *group: dqGroups) {
        group->dumpFwQSize();
    }
}

template<class Impl>
void DQTop<Impl>::clearInstBuffer()
{
    centerInstBuffer.clear();
}

template<class Impl>
void DQTop<Impl>::distributeInstsToGroup()
{
    auto it = centerInstBuffer.begin();
    while (it != centerInstBuffer.end()) {
        DynInstPtr &inst = *it;
        if (!inst) {
            DPRINTF(DQGDisp,
                    "Null inst found in centerInstBuffer\n");
        } else {
            if (inst->dqPosition.group != dispatchingGroup->getGroupID()) {
                schedSwitchDispatchingGroup(inst);
                DPRINTF(DQGDisp, "Switch dispatching group, break this cycle\n");
                break;
            }
            auto bank = inst->dqPosition.bank;
            DPRINTF(DQGDisp,
                    "Writing Inst[%llu] to" ptrfmt "\n", inst->seqNum, extptr(inst->dqPosition));
            dispatchingGroup->dqs[bank]->writeInstsToBank(inst->dqPosition, inst);
            dispatchingGroup->dqs[bank]->checkReadiness(inst->dqPosition);

            RegReadCenterInstBuf++;
        }
        it = centerInstBuffer.erase(it);
    }
}

template<class Impl>
void DQTop<Impl>::switchDispatchingGroup()
{
    assert(switchOn);
    if (dispatchingGroup->getGroupID() + 1 == switchOn->dqPosition.group) {
        dispatchingGroup = dqGroups[dispatchingGroup->getGroupID() + 1];
    } else {
        assert(switchOn->dqPosition.group == 0);
        // wrapped around
        dispatchingGroup = dqGroups[0];
    }
}

template<class Impl>
void DQTop<Impl>::distributePairsToGroup()
{
    if (switchDispGroup) {
        DPRINTF(FFDisp, "Skip distributing pairs this cycle "
                "because of switching dispatch group.\n");
        return;
    }
    auto it = centerPairBuffer.begin();
    while (!centerPairBuffer.empty()) {
        PointerPair &p = *it;
        if (p.dest.valid) {
            if (p.dest.group >= c.nGroups) {
                panic("Got target group ID: %u", p.dest.group);
            }
            auto group = dqGroups[p.dest.group];
            group->insertForwardPointer(p);
            RegReadCenterPairBuf++;
        }
        it = centerPairBuffer.erase(it);
    }
}

template<class Impl>
unsigned DQTop<Impl>::getNextGroup(unsigned group_id) const
{
    return (group_id + 1) % c.nGroups;
}

template<class Impl>
unsigned DQTop<Impl>::getPrevGroup(unsigned group_id) const
{
    auto x = group_id - 1;
    if (x >= 0) {
        return x;
    } else {
        return c.nGroups - 1;
    }
}

template<class Impl>
void DQTop<Impl>::sendToNextGroup(unsigned sending_group, const WKPointer &wk_pointer)
{
    unsigned recv_group = getNextGroup(sending_group);
    DPRINTF(DQGDL || Debug::DQWake, ptrfmt "lands in group %u\n",
            extptr(wk_pointer), recv_group);
    interGroupBuffer[recv_group].push_back(wk_pointer);
    RegWriteInterGroupWKBuf++;
//    unsigned recv_group = getNextGroup(sending_group);
//    DataflowQueues *receiver = dqGroups[recv_group];
//    receiver->receivePointers(wk_pointer);
}

template<class Impl>
typename Impl::DynInstPtr
DQTop<Impl>::getHead() const
{
    if (!centerInstBuffer.empty()) {
        return centerInstBuffer.back();
    } else {
        return readInst(c.uint2Pointer(head));
    }
}

template<class Impl>
typename Impl::DynInstPtr
DQTop<Impl>::readInst(const BasePointer &p) const
{
    auto group = dqGroups[p.group];
    const auto &inst = group->readInst(p);
    if (!inst) {
        for (const auto &buf_inst: centerInstBuffer) {
            if (buf_inst && buf_inst->dqPosition == p) {
                return buf_inst;
            }
        }
        return nullptr;
    } else {
        return inst;
    }
}

template<class Impl>
bool DQTop<Impl>::notifyHalfSquash(InstSeqNum new_seq, Addr new_pc)
{
    if (halfSquash) { // old valid
        if (new_seq <= halfSquashSeq) {
            halfSquashSeq = new_seq;
            halfSquashPC = new_pc;
            DPRINTF(FFSquash, "Scheduled half squash @ inst[%lu]\n", halfSquashSeq);
        } else {
            return false;
        }
    } else {
        halfSquashSeq = new_seq;
        halfSquashPC = new_pc;
        halfSquash = true;
        HalfSquashes++;
        DPRINTF(FFSquash, "Scheduled half squash @ inst[%lu]\n", halfSquashSeq);
    }
    return true;
}


template<class Impl>
void DQTop<Impl>::alignTails()
{
    committingGroup->alignTails();
}

template<class Impl>
void DQTop<Impl>::updateCommittingGroup(DataflowQueues *dq)
{
    committingGroup = dq;
}

template<class Impl>
void DQTop<Impl>::addReadyMemInst(DynInstPtr inst, bool isOrderDep)
{
    if (inst->isSquashed()) {
        DPRINTF(DQWake, "Cancel replaying mem inst[%llu] because it was squashed\n", inst->seqNum);
        return;
    }
    DPRINTF(DQWake, "Replaying mem inst[%llu]\n", inst->seqNum);
    WKPointer wk(inst->dqPosition);
    if (isOrderDep) { // default to True
        // pass
        // if (inst->isLoad()) {
        // } else {
        //     wk.wkType = WKPointer::WKOrder;
        //     centralizedExtraWakeup(wk);
        // }

    } else {
        wk.wkType = WKPointer::WKMem;
        centralizedExtraWakeup(wk);
    }
}

template<class Impl>
void DQTop<Impl>::endCycle()
{
    checkFlagsAndUpdate();
}

template<class Impl>
void DQTop<Impl>::groupsTxPointers()
{
    for (auto group: dqGroups) {
        group->transmitPointers();
    }
}

template<class Impl>
void DQTop<Impl>::groupsRxFromCenterBuffer()
{
    DPRINTF(DQGOF, "Rx from center-buffer, size: %llu\n", pseudoCenterWKPointerBuffer.size());
    RegReadCenterWKBuf += groupsRxFromBuffers(pseudoCenterWKPointerBuffer, center2GroupRate);
}

template<class Impl>
void DQTop<Impl>::groupsRxFromPrevGroup()
{
    DPRINTF(DQGOF, "Rx from inter-buffer, size: %llu\n", interGroupBuffer.size());
    RegReadInterGroupWKBuf += groupsRxFromBuffers(interGroupBuffer, group2GroupRate);
}

template<class Impl>
unsigned DQTop<Impl>::groupsRxFromBuffers(std::vector<std::list<WKPointer>> &queues, unsigned limit)
{
    unsigned total = 0;
    for (unsigned g = 0 ; g < c.nGroups; g++) {
        DPRINTF(DQGOF, "Group %u\n", g);
        auto &queue = queues[g];
        unsigned count = 0;
        while (!queue.empty() && count++ < limit) {
            count++;
            DPRINTF(DQGOF, "Receiving pointer" ptrfmt "\n", extptr(queue.front()));
            dqGroups[g]->receivePointers(queue.front());
            queue.pop_front();
        }
        total += count;
    }
    return total;
}

template<class Impl>
void DQTop<Impl>::clearPairBuffer()
{
    centerPairBuffer.clear();
}

template<class Impl>
void DQTop<Impl>::schedSwitchDispatchingGroup(DynInstPtr &inst)
{
    switchOn = inst;
    switchDispGroup = true;
}

template<class Impl>
void DQTop<Impl>::checkFlagsAndUpdate()
{
    if (switchDispGroup) {
        switchDispatchingGroup();

        switchOn = nullptr;
        switchDispGroup = false;
    }
}

template<class Impl>
unsigned DQTop<Impl>::decIndex(unsigned u)
{
    if (u != 0) {
        return u - 1;
    } else {
        return dispatchWidth - 1;
    }
}

template<class Impl>
unsigned DQTop<Impl>::incIndex(unsigned u)
{
    return (u+1) % dispatchWidth;
}


}

#include "cpu/forwardflow/isa_specific.hh"

template class FF::DQTop<FFCPUImpl>;
