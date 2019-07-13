//
// Created by zyy on 19-6-17.
//

#include "allocation.hh"
#include "base/trace.hh"
#include "config/the_isa.hh"
#include "cpu/forwardflow/comm.hh"
#include "debug/Activity.hh"
#include "debug/DAllocation.hh"
#include "params/DerivFFCPU.hh"

namespace FF
{

template<class Impl>
void
Allocation<Impl>::tick() {

    readInsts();

    wroteToTimeBuffer = false;

    blockThisCycle = false;

    bool status_change = false;

    toDIEWCIndex = 0;

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

    for (const auto& inst: insts) {
        if (inst && !inst->isSquashed()) {
            DPRINTF(DAllocation, "Inst[%d] arrived allocation\n", inst->seqNum);
        }
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

    if (fromDIEWC->diewc2diewc.squash) {
        DPRINTF(DAllocation, "Squashing instructions due to squash from "
                "commit.\n");

        // todo: squash
        // squash(fromCommit->diewc2diewc[tid].doneSeqNum, tid);

        return true;
    }

    if (checkStall()) {
        return block();
    }

    if (allocationStatus == Blocked) {
        DPRINTF(DAllocation, "Done blocking, switching to unblocking.\n");

        allocationStatus = Unblocking;

        unblock();

        return true;
    }

    if (allocationStatus == Squashing) {
        // Switch status to running if rename isn't being told to block or
        // squash this cycle.
        if (resumeSerialize) {
            DPRINTF(DAllocation, "Done squashing, switching to serialize.\n");

            allocationStatus = SerializeStall;
            return true;
        } else if (resumeUnblocking) {
            DPRINTF(DAllocation, "Done squashing, switching to unblocking.\n");
            allocationStatus = Unblocking;
            return true;
        } else {
            DPRINTF(DAllocation, "Done squashing, switching to running.\n");

            allocationStatus = Running;
            return false;
        }
    }

    if (allocationStatus == SerializeStall) {
        // Stall ends once the ROB is free.
        DPRINTF(DAllocation, "Done with serialize stall, switching to "
                "unblocking.\n");

        allocationStatus = Unblocking;

        unblock();

        DPRINTF(DAllocation, "Processing instruction [%lli] with "
                "PC %s.\n", serializeInst->seqNum, serializeInst->pcState());

        // Put instruction into queue here.
        serializeInst->clearSerializeBefore();

        if (!skidBuffer.empty()) {
            skidBuffer.pop_front();
        } else {
            insts.pop_front();
        }

        DPRINTF(DAllocation, "Instruction must be processed by rename."
                " Adding to front of list.\n");

        serializeInst = nullptr;

        return true;
    }

    DPRINTF(DAllocation, "Status not changed\n");
    // If we've reached this point, we have not gotten any signals that
    // cause rename to change its status.  Allocation remains the same as before.
    return false;
}

template<class Impl>
bool Allocation<Impl>::checkStall() {
    bool ret_val = false;

    if (diewcStall) {
        DPRINTF(DAllocation,"Stall from DIEWC stage detected.\n");
        ret_val = true;

    } else if (calcFreeDQEntries() <= 0) {
        DPRINTF(DAllocation,"Stall: DQ has 0 free entries.\n");
        incrFullStat(FullSource::DQ);
        ret_val = true;

    } else if (calcFreeLQEntries() <= 0 && calcFreeSQEntries() <= 0) {
        DPRINTF(DAllocation,"Stall: LSQ has 0 free entries.\n");
        if (calcFreeLQEntries() <= 0) {
            incrFullStat(FullSource::LQ);
        } else {
            incrFullStat(FullSource::SQ);
        }
        ret_val = true;

    } else if (allocationStatus == SerializeStall &&
               (!emptyDQ || instsInProgress)) {
        DPRINTF(DAllocation,"Stall: Serialize stall and ROB is not empty.\n");
        ret_val = true;
    }

    DPRINTF(DAllocation, "No stall detected\n");

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
            toDecode->renameUnblock[DummyTid] = false;
        } else if (allocationStatus == Unblocking) {
            if (resumeUnblocking) {
                block();
                resumeUnblocking = false;
                toDecode->renameUnblock[DummyTid] = false;
            }
        }
    }

    if (allocationStatus == Running || allocationStatus == Idle) {
        DPRINTF(DAllocation, "Allocate on Running\n");
        allocateInsts();

    } else if (allocationStatus == Unblocking) {
        DPRINTF(DAllocation, "Allocate on Unblocking\n");
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

    DPRINTF(DAllocation, "num of insts available = %lu\n", inst_available);

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

        if (inst->isSquashed()) {
            ++allocatedSquashInsts;
            --inst_available;
            DPRINTF(DAllocation, "skip because squashed\n");
            continue;
        }

        if (!canAllocate()) {
            ++allocationDQFullEvents;
            DPRINTF(DAllocation, "break because cannot allocate anymore\n");
            break;
        }

        if ((inst->isIprAccess() || inst->isSerializeBefore()) &&
                !inst->isSerializeHandled()) {
            if (!inst->isTempSerializeBefore()) {
                allocatedSerilizing++;
                inst->setSerializeHandled();
            } else {
                allocatedTempSerilizing++;
            }

            allocationStatus = SerializeStall;
            serializeInst = inst;
            blockThisCycle = true;
            break;

        } else if ((inst->isStoreConditional() || inst->isSerializeAfter()) &&
                   !inst->isSerializeHandled()) {
            DPRINTF(DAllocation, "Serialize after instruction encountered.\n");

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

        ++toDIEWCIndex;
        --inst_available;
    }

    instsInProgress += allocated;

    allocationAllocatedInsts += allocated;

    if (toDIEWCIndex) {
        wroteToTimeBuffer = true;
    }
    DPRINTF(DAllocation, "to DIEWC index: %d\n", toDIEWCIndex);

    if (inst_available) {
        blockThisCycle = true;
    }

    if (blockThisCycle) {
        block();
        toDecode->renameUnblock[DummyTid] = false;
    }
}

template<class Impl>
bool Allocation<Impl>::canAllocate() {
    if (flatHead > flatTail) {
        return !(flatTail == 0 && flatHead == dqSize - 1);
    } else if (flatTail != flatHead) {
        return flatTail - flatHead > 1;
    } else {
        // initiated just now (tail == head)
        return true;
    }

}

template<class Impl>
void Allocation<Impl>::skidInsert() {
    DynInstPtr inst = nullptr;
    while (!insts.empty()) {
        inst = insts.front();
        ++allocationSkidInsts;
        skidBuffer.push_back(inst);
        insts.pop_front();
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
    return DQPointer(true, group, bank, index, 0);
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
            toDecode->renameBlock[DummyTid] = true;
            toDecode->renameUnblock[DummyTid] = false;
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
        toDecode->renameUnblock[DummyTid] = true;
        wroteToTimeBuffer = true;
        allocationStatus = Running;
        return true;
    }
    return false;
}

template<class Impl>
void Allocation<Impl>::updateStatus() {
    bool any_ub = allocationStatus == Unblocking;
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
        freeEntries.dqEntries = fromDIEWC->diewcInfo.freeDQEntries;
    }
    if (fromDIEWC->diewcInfo.usedLSQ) {
        freeEntries.lqEntries = fromDIEWC->diewcInfo.freeLQEntries;
        freeEntries.sqEntries = fromDIEWC->diewcInfo.freeSQEntries;
    }
}

template<class Impl>
void Allocation<Impl>::readStallSignals() {
    if (fromDIEWC->diewcBlock) {
        DPRINTF(DAllocation, "received block from DIEWC\n");
        diewcStall = true;
    }

    if (fromDIEWC->diewcUnblock) {
        assert(diewcStall);
        if (fromDIEWC->diewcBlock) {
            DPRINTF(DAllocation, "Unblock from DIEWC override Blocking\n");
        }
        diewcStall = false;
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

template<class Impl>
Allocation<Impl>::Allocation(O3CPU* cpu, DerivFFCPUParams *params)
        : cpu(cpu),
          instsInProgress(0),
          loadsInProgress(0),
          storesInProgress(0),
          emptyDQ(false),
          serializeInst(nullptr),
          serializeOnNextInst(false),
          flatHead(0),
          flatTail(0),
          indexWidth((unsigned) ceilLog2(params->DQDepth)),
          indexMask((unsigned) (1 << indexWidth) - 1),
          bankMask((unsigned) (1 << ceilLog2(params->numDQBanks)) - 1),
          dqSize( params->DQDepth * params->numDQBanks),
          diewcStall(false)
{

    skidBufferMax = static_cast<unsigned int>(
            (params->decodeToRenameDelay + 1) * params->decodeWidth);
}

template<class Impl>
std::string Allocation<Impl>::name() const {
    return cpu->name() + ".allocation";
}

template<class Impl>
void Allocation<Impl>::incrFullStat(Allocation::FullSource source) {

}

template<class Impl>
void Allocation<Impl>::serializeAfter(Allocation::InstQueue &insts) {

}

template<class Impl>
void Allocation<Impl>::setDecodeQueue(TimeBuffer<DecodeStruct> *dcq)
{
    decodeQueue = dcq;
    fromDecode = decodeQueue->getWire(-1);
}

template<class Impl>
void Allocation<Impl>::setAllocQueue(TimeBuffer<AllocationStruct > *alq)
{
    allocationQueue = alq;
    toDIEWC = allocationQueue->getWire(0);
}

template<class Impl>
void Allocation<Impl>::setTimeBuffer(TimeBuffer<TimeStruct> *tf)
{
    timeBuffer = tf;
    fromDIEWC = tf->getWire(-1);
    toDecode = tf->getWire(0);
}

template<class Impl>
void Allocation<Impl>::startupStage()
{
    resetStage();
}

template<class Impl>
void Allocation<Impl>::resetStage()
{
    overallStatus = Inactive;

    resumeSerialize = false;
    resumeUnblocking = false;

    allocationStatus = Idle;

    //todo: init freeEntries with a conservative value is just OK?
    freeEntries.dqEntries = 8;
    freeEntries.lqEntries = 8;
    freeEntries.sqEntries = 8;

    serializeInst = nullptr;
    instsInProgress = 0;
    loadsInProgress = 0;
    storesInProgress = 0;
    serializeOnNextInst = false;
}

}


#include "cpu/forwardflow/isa_specific.hh"

template class FF::Allocation<FFCPUImpl>;
