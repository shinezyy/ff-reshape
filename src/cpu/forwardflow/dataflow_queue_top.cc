//
// Created by zyy on 2020/1/15.
//

#include "dataflow_queue_top.hh"
#include "params/DerivFFCPU.hh"
#include "params/FFFUPool.hh"

namespace FF
{

template<class Impl>
DQTop<Impl>::DQTop(DerivFFCPUParams *params)
        :
        c(params)
{

}

template<class Impl>
void DQTop<Impl>::cycleStart()
{
    // TODO: clean up new stuffs in multiple DQ groups

    // per group start
    for (auto &group: dqGroups) {
        group.cycleStart();
    }
}

template<class Impl>
void DQTop<Impl>::tick()
{
    // per group tick, because there is not combinational signals across groups
    // their "ticks" should be executed in parallel
    for (DataflowQueues &group: dqGroups) {
        group.tick();
    }

    // TODO: write data generated in this tick to inter-group connections
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
    auto p = uint2Pointer(tail);
    DPRINTF(DQ, "Scheduling non spec inst @ (%i %i)\n", p.bank, p.index);
    wk.wkType = WKPointer::WKMisc;
    centralizedExtraWakeup(wk);
}

template<class Impl>
void DQTop<Impl>::centralizedExtraWakeup(const WKPointer &wk)
{
    // store in a buffer? or insert directly?
}

template<class Impl>
bool DQTop<Impl>::hasTooManyPendingInsts()
{
    for (DataflowQueues &group: dqGroups) {
        if (group.hasTooManyPendingInsts()) {
            return true;
        }
    }
    return false;
}

template<class Impl>
void DQTop<Impl>::advanceHead()
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
        headTerm = (headTerm + 1) % c.termMax;
    }

    auto allocated = uint2Pointer(head);
    auto dead_inst = dqGroups[allocated.group][allocated.bank]->readInstsFromBank(allocated);
    if (dead_inst) {
        DPRINTF(FFCommit, "Dead inst[%llu] found unexpectedly\n", dead_inst->seqNum);
        assert(!dead_inst);
    }
}

template<class Impl>
void DQTop<Impl>::clearInflightPackets()
{
    for (DataflowQueues &group: dqGroups) {;
        group.clearInflightPackets()
    }
}

template<class Impl>
typename Impl::DynInstPtr
DQTop<Impl>::findBySeq(InstSeqNum seq)
{
    for (unsigned u = tail; logicallyLET(u, head); u = inc(u)) {
        auto p = c.uint2Pointer(u);
        auto inst = dqGroups[p.group][p.bank]->readInstsFromBank(p);
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
    auto head_ptr = uint2Pointer(tail);
    DataflowQueues &group = dqGroups[head_ptr.group];
    DataflowQueueBank *bank = group[head_ptr.bank];
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
    DPRINTF(DQ, "head: %i tail: %i u: %u\n", head, tail, u);
    if (head >= tail) {
        return (u <= head && u >= tail);
    } else {
        return (u <= head || u >= tail);
    }
}

template<class Impl>
bool DQTop<Impl>::insertBarrier(DynInstPtr &inst)
{
    memDepUnit.insertBarrier(inst);
    return insertNonSpec(inst);
}

template<class Impl>
bool DQTop<Impl>::insertNonSpec(DynInstPtr &inst)
{
    bool non_spec = false;
    if (inst->isStoreConditional()) {
        memDepUnit.insertNonSpec(inst);
        non_spec = true;
    }
    assert(inst);
    inst->miscDepReady = false;
    inst->hasMiscDep = true;
    // TODO: this is centralized now; Decentralize it with buffers
    return insert(inst, non_spec);
}

template<class Impl>
bool DQTop<Impl>::insert(DynInstPtr &inst, bool nonSpec)
{
    // TODO: this is centralized now; Decentralize it with buffers
    // todo: send to allocated DQ position
    assert(inst);

    bool jumped = false;

    DQPointer allocated = inst->dqPosition;
    DPRINTF(DQ, "allocated @(%d %d)\n", allocated.bank, allocated.index);
    DataflowQueues &group = dqGroups[allocated.group];
    auto dead_inst = group[allocated.bank]->readInstsFromBank(allocated);
    if (dead_inst) {
        DPRINTF(FFCommit, "Dead inst[%llu] found unexpectedly\n", dead_inst->seqNum);
        assert(!dead_inst);
    }
    group[allocated.bank]->writeInstsToBank(allocated, inst);
    if (isEmpty()) {
        tail = c.pointer2uint(allocated); //keep them together
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
    group[allocated.bank]->checkReadiness(allocated);

    return jumped;
}

template<class Impl>
void DQTop<Impl>::insertForwardPointer(PointerPair pair)
{
    DataflowQueues &group = dqGroups[pair.dest.group];
    group.insertForwardPointer(pair);
}

template<class Impl>
list<typename Impl::DynInstPtr>
DQTop<Impl>::getBankHeads()
{
}

template<class Impl>
list<typename Impl::DynInstPtr>
DQTop<Impl>::getBankTails()
{
    unsigned tail_group_id = c.uint2Pointer(tail).group;
    DataflowQueues tail_group = dqGroups[tail_group_id];
    // TODO: this will cause fragment, but is simple
    return tail_group.getBankTails();
}

template<class Impl>
bool DQTop<Impl>::stallToUnclog() const
{
    if (isFull()) return true;
    for (DataflowQueues &group: dqGroups) {
        if (group.stallToUnclog()) return true;
    }
    return false;
}

template<class Impl>
void DQTop<Impl>::retireHead(bool result_valid, FFRegValue v)
{
    assert(!isEmpty());
    DQPointer head_ptr = uint2Pointer(tail);
    alignTails();
    DPRINTF(FFCommit, "Position of inst to commit:(%d %d)\n",
            head_ptr.bank, head_ptr.index);
    DataflowQueues &group = dqGroups[head_ptr.group];
    DynInstPtr head_inst = group.banks[head_ptr.bank]->readInstsFromBank(head_ptr);

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
    group.banks[head_ptr.bank]->advanceTail();
    if (head != tail) {
        tail = inc(tail);
        DPRINTF(DQ, "tail becomes %u in retiring\n", tail);
    }

    DPRINTF(FFCommit, "Advance youngest ptr to %d, olddest ptr to %d\n", head, tail);
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
        auto tail_ptr = uint2Pointer(tail);
        auto &group = dqGroups[tail_ptr.group];
        auto &bank = group.banks[tail_ptr.bank];
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
void DQTop<Impl>::squash(DQPointer p, bool all, bool including)
{
    // TODO: implemented it;
    // The major problem is co-ordinate multiple groups
    notImplemented();
}

template<class Impl>
bool DQTop<Impl>::queuesEmpty()
{
    for (DataflowQueues &group: dqGroups) {
        if (!group.queuesEmpty()) return false;
    }
    return true;
}

template<class Impl>
void DQTop<Impl>::setTimeBuf(TimeBuffer<DQStruct> *dqtb)
{
    notImplemented();
}

template<class Impl>
void DQTop<Impl>::setLSQ(LSQ *lsq)
{
    for (DataflowQueues &group: dqGroups) {
        group.setLSQ(lsq);
    }
}

template<class Impl>
void DQTop<Impl>::setDIEWC(DIEWC *_diewc)
{
    for (DataflowQueues &group: dqGroups) {
        group.setDIEWC(_diewc);
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
void DQTop<Impl>::writebackLoad(DynInstPtr &inst)
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
        centralizedExtraWakeup(wk);

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
void DQTop<Impl>::wakeMemRelated(DynInstPtr &inst)
{
    if (inst->isMemRef()) {
        memDepUnit.wakeDependents(inst);
    }
}

template<class Impl>
void DQTop<Impl>::completeMemInst(DynInstPtr &inst)
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
void DQTop<Impl>::violation(DynInstPtr store, DynInstPtr violator)
{
    memDepUnit.violation(store, violator);
}

template<class Impl>
void DQTop<Impl>::notImplemented()
{
    panic("Not implemented!\n");
}

template<class Impl>
void DQTop<Impl>::takeOverFrom()
{
    resetState();
}

template<class Impl>
void DQTop<Impl>::drainSanityCheck() const
{

}

template<class Impl>
void DQTop<Impl>::resetState()
{
    for (DataflowQueues &group: dqGroups) {
        group.resetState();
    }
    blockedMemInsts.clear();
    retryMemInsts.clear();
}

template<class Impl>
void DQTop<Impl>::resetEntries()
{
    for (DataflowQueues &group: dqGroups) {
        group.resetEntries();
    }
}

template<class Impl>
void DQTop<Impl>::regStats()
{
    memDepUnit.regStats();
}

template<class Impl>
unsigned DQTop<Impl>::numInFlightFw()
{
    unsigned sum = 0;
    for (DataflowQueues &group: dqGroups) {
        sum += group.numInFlightFw();
    }
    return sum;
}

template<class Impl>
void DQTop<Impl>::dumpFwQSize()
{
    for (DataflowQueues &group: dqGroups) {
        group.dumpFwQSize();
    }
}

}
