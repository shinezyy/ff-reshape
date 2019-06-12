//
// Created by zyy on 19-6-17.
//

#include "allocation.hh"

namespace FF
{

template<class Impl>
void
Allocation<Impl>::tick() {

    wroteToTimeBuffer = false;

    blockThisCycle = false;

    bool status_change = false;

    toIEWIndex = 0;

    // Check stall and squash signals.

    status_change = checkSignalsAndUpdate();

    allocate(status_change);

    if (status_change) {
        updateStatus();
    }

    if (wroteToTimeBuffer) {
        DPRINTF(Activity, "Activity this cycle.\n");
        cpu->activityThisCycle();
    }

    //  todo: whether FF needs to remove from history here?

    updateInProgress();
}

template <class Impl>
void
Allocation<Impl>::readInsts()
{
    int insts_from_decode = fromDecode->size;
    for (int i = 0; i < insts_from_decode; ++i) {
        DynInstPtr inst = fromDecode->insts[i];
        insts.push_back(inst);
    }
}

template<class Impl>
bool Allocation<Impl>::checkSignalsAndUpdate() {

    // Check if there's a squash signal, squash if there is
    // Check stall signals, block if necessary.
    // If status was blocked
    //     check if stall conditions have passed
    //         if so then go to unblocking
    // If status was Squashing
    //     check if squashing is not high.  Switch to running this cycle.
    // If status was serialize stall
    //     check if ROB is empty and no insts are in flight to the ROB

    readFreeEntries();
    readStallSignals();

    if (fromDIEWC->commitInfo.squash) {
        DPRINTF(Rename, "[tid:%u]: Squashing instructions due to squash from "
                "commit.\n", tid);

        // todo: squash
        // squash(fromCommit->commitInfo[tid].doneSeqNum, tid);

        return true;
    }

    if (checkStall()) {
        return block();
    }

    if (allocationStatus == Blocked) {
        DPRINTF(Rename, "[tid:%u]: Done blocking, switching to unblocking.\n",
                tid);

        allocationStatus = Unblocking;

        unblock();

        return true;
    }

    if (allocationStatus == Squashing) {
        // Switch status to running if rename isn't being told to block or
        // squash this cycle.
        if (resumeSerialize) {
            DPRINTF(Rename, "[tid:%u]: Done squashing, switching to serialize.\n",
                    tid);

            allocationStatus = SerializeStall;
            return true;
        } else if (resumeUnblocking) {
            DPRINTF(Rename, "[tid:%u]: Done squashing, switching to unblocking.\n",
                    tid);
            allocationStatus = Unblocking;
            return true;
        } else {
            DPRINTF(Rename, "[tid:%u]: Done squashing, switching to running.\n",
                    tid);

            allocationStatus = Running;
            return false;
        }
    }

    if (allocationStatus == SerializeStall) {
        // Stall ends once the ROB is free.
        DPRINTF(Rename, "[tid:%u]: Done with serialize stall, switching to "
                "unblocking.\n", tid);

        allocationStatus = Unblocking;

        unblock();

        DPRINTF(Rename, "[tid:%u]: Processing instruction [%lli] with "
                "PC %s.\n", tid, serial_inst->seqNum, serial_inst->pcState());

        // Put instruction into queue here.
        serializeInst->clearSerializeBefore();

        if (!skidBuffer.empty()) {
            skidBuffer.push_front();
        } else {
            insts.push_front();
        }

        DPRINTF(Rename, "[tid:%u]: Instruction must be processed by rename."
                " Adding to front of list.\n", tid);

        serializeInst = nullptr;

        return true;
    }

    // If we've reached this point, we have not gotten any signals that
    // cause rename to change its status.  Rename remains the same as before.
    return false;
}

template<class Impl>
bool Allocation<Impl>::checkStall() {
    bool ret_val = false;

    if (diewcStall) {
        DPRINTF(Allocation,"[tid:%i]: Stall from DIEWC stage detected.\n", tid);
        ret_val = true;

    } else if (calcFreeDQEntries() <= 0) {
        DPRINTF(Allocation,"[tid:%i]: Stall: DQ has 0 free entries.\n", tid);
        incrFullStat(FullSource::DQ);
        ret_val = true;

    } else if (calcFreeLQEntries() <= 0 && calcFreeSQEntries() <= 0) {
        DPRINTF(Allocation,"[tid:%i]: Stall: LSQ has 0 free entries.\n", tid);
        if (calcFreeLQEntries() <= 0) {
            incrFullStat(FullSource::LQ);
        } else {
            incrFullStat(FullSource::SQ);
        }
        ret_val = true;

    } else if (allocationStatus == SerializeStall &&
               (!emptyDQ || instsInProgress)) {
        DPRINTF(Allocation,"[tid:%i]: Stall: Serialize stall and ROB is not "
                "empty.\n",
                tid);
        ret_val = true;
    }

    return ret_val;
}

template<class Impl>
void Allocation<Impl>::allocate(bool &status_change) {
    if (allocationStatus == Blocked) {
        ++allocationBlockCycles;
    } else if (allocationStatus == Squashing) {
        ++allocationSquashCycles;
    } else if (allocationStatus == SerializeStall) {
        ++allocationSerializeStallCycles;

        if (resumeSerialize) {
            resumeSerialize = false;
            block();
            toDecode->allocationUnblock = false;
        } else if (allocationStatus == Unblocking) {
            if (resumeUnblocking) {
                block();
                resumeUnblocking = false;
                toDecode->allocationUnblock = false;
            }
        }
    }

    if (allocationStatus == Running || allocationStatus == Idle) {
        allocateInsts();

    } else if (allocationStatus == Unblocking) {
        allocateInsts();

        if (validInsts()) {
            skidInsert();
        }

        status_change = unblock() || status_change || blockThisCycle;
    }
}

template<class Impl>
void Allocation<Impl>::allocateInsts() {
    DynInstPtr inst = nullptr;

    auto inst_available = allocationStatus == Unblocking ?
            skidBuffer.size() : insts.size();

    InstQueue &to_allocate = allocationStatus == Unblocking ?
            skidBuffer: insts;

    if (serializeOnNextInst) {
        if (emptyDQ && instsInProgress == 0) {
            serializeOnNextInst = false;
        } else if (!to_allocate.empty()) {
            to_allocate.front()->setSerializeBefore();
        }
    }

    int allocated = 0;

    while (inst_available > 0 && toDIEWCIndex < allocationWidth) {
        inst = to_allocate.front();
        to_allocate.pop_front();

        if (inst.isSquashed()) {
            ++allocatedSquashInsts;
            --inst_available;
            continue;
        }

        if (!canAllocate()) {
            ++allocationDQFullEvents;
            break;
        }

        if ((inst.isIprAccess() || inst.isSerializeBefore()) &&
                !inst.isSerializeHandled()) {
            if (!inst.isTempSerializeBefore()) {
                allocatedSerilizing++;
                inst.setSerializeHandled();
            } else {
                allocatedTempSerilizing++;
            }

            allocationStatus = SerializeStall;
            serializeInst = inst;
            blockThisCycle = true;
            break;

        } else if ((inst->isStoreConditional() || inst->isSerializeAfter()) &&
                   !inst->isSerializeHandled()) {
            DPRINTF(Rename, "Serialize after instruction encountered.\n");

            allocatedSerializing++;

            inst->setSerializeHandled();

            serializeAfter(to_allocate);
        }

        inst->dqPosition = allocateDQEntry();

        if (inst->isLoad()) {
            loadsInProgress++;
        }
        if (inst->isStore()) {
            storesInProgress++;
        }
        ++allocated;

        // todo ppRename notify
        toDIEWC->insts[toDIEWCIndex] = inst;
        ++(toDIEWC->size);

        ++toIEWIndex;
        --inst_available;
    }

    instsInProgress += allocated;

    allocationAllocatedInsts += allocated;

    if (toDIEWCIndex) {
        wroteToTimeBuffer = true;
    }

    if (inst_available) {
        blockThisCycle = true;
    }

    if (blockThisCycle) {
        block();
        toDecode->allocationUnblock = false;
    }
}

template<class Impl>
bool Allocation<Impl>::canAllocate() {
    if (flatHead > flatTail) {
        return !(flatTail == 0 && flatHead == dqSize - 1);
    }

    return flatTail - flatHead > 1;
}

template<class Impl>
void Allocation<Impl>::skidInsert() {
    DynInstPtr inst = nullptr;
    while (!insts.empty()) {
        inst = insts.front();
        insts.pop_front();
        ++allocationSkidInsts;
        skidBuffer.push_back(inst);
    }

    if (skidBuffer.size() > skidBufferMax) {
        panic("Skidbuffer Exceeded Max Size");
    }
}

template<class Impl>
unsigned Allocation<Impl>::validInsts() {
    unsigned count = 0;
    for (int i = 0; i < fromDecode->size; i++) {
        if (!fromDecode->insts[i]->isSquashed()) {
            count++;
        }
    }
    return count;
}

template<class Impl>
void Allocation<Impl>::regStats() {
    allocationBlockCycles
        .name(name() + ".allocationBlockCycles")
        .desc("allocationBlockCycles");
    allocationSquashCycles
        .name(name() + ".allocationSquashCycles")
        .desc("allocationSquashCycles");
    allocationSerializeStallCycles
        .name(name() + ".allocationSerializeStallCycles")
        .desc("allocationSerializeStallCycles");
    allocatedSquashInsts
        .name(name() + ".allocatedSquashInsts")
        .desc("allocatedSquashInsts");
    allocationDQFullEvents
        .name(name() + ".allocationDQFullEvents")
        .desc("allocationDQFullEvents");
    allocatedSerilizing
        .name(name() + ".allocatedSerilizing")
        .desc("allocatedSerilizing");
    allocatedTempSerilizing
        .name(name() + ".allocatedTempSerilizing")
        .desc("allocatedTempSerilizing");
    allocatedSerializing
        .name(name() + ".allocatedSerializing")
        .desc("allocatedSerializing");
    allocationAllocatedInsts
        .name(name() + ".allocationAllocatedInsts")
        .desc("allocationAllocatedInsts");
    allocationSkidInsts
        .name(name() + ".allocationSkidInsts")
        .desc("allocationSkidInsts");
}

template<class Impl>
DQPointer Allocation<Impl>::PositionfromUint(unsigned u) {
    unsigned index = u & indexMask;
    unsigned bank = (u >> indexWidth) & bankMask;
    unsigned group = 0; //todo group is not supported yet
    return DQPointer{true, group, bank, index, 0};
}

template<class Impl>
DQPointer Allocation<Impl>::allocateDQEntry() {
    return PositionfromUint(flatHead++);
}

template<class Impl>
bool Allocation<Impl>::block() {
    skidInsert();
    if (allocationStatus != Blocked) {
        if (resumeUnblocking || allocationStatus != Unblocking) {
            toDecode->allocationBlock = true;
            toDecode->allocationUnblock = false;
            wroteToTimeBuffer = true;
        }
        if (allocationStatus != SerializeStall) {
            allocationStatus = Blocked;
            return true;
        }
    }
    return false;
}

template<class Impl>
bool Allocation<Impl>::unblock() {
    if (skidBuffer.empty() && allocationStatus != SerializeStall) {
        toDecode->allocationUnblock = true;
        wroteToTimeBuffer = true;
        allocationStatus = Running;
        return true;
    }
    return false;
}

template<class Impl>
void Allocation<Impl>::updateStatus() {
    bool any_ub = allocationStatus = Unblocking;
    if (any_ub) {
        if (overallStatus == Inactive) {
            overallStatus = Active;

            cpu->activateStage(O3CPU::AllocationIdx);
        }
    } else {
        if (overallStatus == Active) {
            overallStatus = Inactive;

            cpu->deactivateStage(O3CPU::AllocationIdx);
        }
    }
}

template<class Impl>
void Allocation<Impl>::updateInProgress() {
    instsInProgress -= fromDIEWC->diewcInfo.dispatched;
    loadsInProgress -= fromDIEWC->diewcInfo.dispatchedToLQ;
    storesInProgress -= fromDIEWC->diewcInfo.dispatchedToSQ;
    assert(instsInProgress >= 0);
    assert(loadsInProgress >= 0);
    assert(storesInProgress >= 0);
}

template<class Impl>
void Allocation<Impl>::readFreeEntries() {
    if (fromDIEWC->diewcInfo.usedDQ) {
        freeEntries.dqEntries = fromDIEWC->diewcInfo.freeDQEntrues;
    }
    if (fromDIEWC->diewcInfo.usedLSQ) {
        freeEntries.lqEntries = fromDIEWC->diewcInfo.freeLQEntrues;
        freeEntries.sqEntries = fromDIEWC->diewcInfo.freeSQEntrues;
    }
}

template<class Impl>
void Allocation<Impl>::readStallSignals() {
    if (fromDIEWC->diewcBlock) {
        backendStall = true;
    }

    if (fromDIEWC->diewcUnblock) {
        assert(backendStall);
        backendStall = false;
    }
}

template<class Impl>
int Allocation<Impl>::calcFreeDQEntries() {
    auto num_free = freeEntries.dqEntries -
                    (instsInProgress - fromDIEWC->diewcInfo.dispatched);
    return num_free;
}

template<class Impl>
int Allocation<Impl>::calcFreeLQEntries() {
    auto num_free = freeEntries.lqEntries -
                    (loadsInProgress - fromDIEWC->diewcInfo.dispatchedToLQ);
    return num_free;
}

template<class Impl>
int Allocation<Impl>::calcFreeSQEntries() {
    auto num_free = freeEntries.sqEntries -
                    (storesInProgress - fromDIEWC->diewcInfo.dispatchedToSQ);
    return num_free;
}

}
