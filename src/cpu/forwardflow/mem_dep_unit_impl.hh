/*
 * Copyright (c) 2012, 2014 ARM Limited
 * All rights reserved
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Copyright (c) 2004-2006 The Regents of The University of Michigan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Authors: Kevin Lim
 */

#ifndef __CPU_FF_MEM_DEP_UNIT_IMPL_HH__
#define __CPU_FF_MEM_DEP_UNIT_IMPL_HH__

#include <map>

#include "cpu/forwardflow/mem_dep_unit.hh"
#include "debug/MemDepUnit.hh"
#include "debug/NoSQSMB.hh"
#include "params/DerivFFCPU.hh"

namespace FF{

template <class MemDepPred, class Impl>
MemDepUnit<MemDepPred, Impl>::MemDepUnit()
    : iqPtr(NULL)
{
    loadBarrier.valid = false;
    storeBarrier.valid = false;
}

template <class MemDepPred, class Impl>
MemDepUnit<MemDepPred, Impl>::MemDepUnit(DerivFFCPUParams *params)
    : _name(params->name + ".memdepunit"),
      iqPtr(NULL)
{
    loadBarrier.valid = false;
    storeBarrier.valid = false;
    DPRINTF(MemDepUnit, "Creating MemDepUnit object.\n");
}

template <class MemDepPred, class Impl>
MemDepUnit<MemDepPred, Impl>::~MemDepUnit()
= default;

template <class MemDepPred, class Impl>
void
MemDepUnit<MemDepPred, Impl>::init(DerivFFCPUParams *params, ThreadID tid)
{
    DPRINTF(MemDepUnit, "Creating MemDepUnit %i object.\n",tid);

    _name = csprintf("%s.memDep%d", params->name, tid);
    id = tid;
}

template <class MemDepPred, class Impl>
void
MemDepUnit<MemDepPred, Impl>::regStats()
{
}

template <class MemDepPred, class Impl>
bool
MemDepUnit<MemDepPred, Impl>::isDrained() const
{
    bool drained = instsToReplay.empty();

    return drained;
}

template <class MemDepPred, class Impl>
void
MemDepUnit<MemDepPred, Impl>::drainSanityCheck() const
{
    assert(instsToReplay.empty());
}

template <class MemDepPred, class Impl>
void
MemDepUnit<MemDepPred, Impl>::takeOverFrom()
{
    // Be sure to reset all state.
    loadBarrier.valid = storeBarrier.valid = false;
    loadBarrier.SN = storeBarrier.SN = 0;
}

template <class MemDepPred, class Impl>
void
MemDepUnit<MemDepPred, Impl>::setIQ(InstructionQueue *iq_ptr)
{
    iqPtr = iq_ptr;
}

template <class MemDepPred, class Impl>
PointerPair
MemDepUnit<MemDepPred, Impl>::insert(DynInstPtr &inst)
{
    // Check any barriers and the dependence predictor for any
    // producing memrefs/stores.
    InstSeqNum producing_barrier = 0;
    if (inst->isLoad() && loadBarrier.valid) {
        DPRINTF(MemDepUnit, "Load barrier [sn:%lli] in flight\n",
                loadBarrier.SN);
        producing_barrier = loadBarrier.SN;
    } else if (inst->isStore() && storeBarrier.valid) {
        DPRINTF(MemDepUnit, "Store barrier [sn:%lli] in flight\n",
                storeBarrier.SN);
        producing_barrier = storeBarrier.SN;
    } else {
        DPRINTF(MemDepUnit, "No barrier found\n");
    }

    MemDepEntryPtr barrier_entry = NULL;

    // If there is a producing store, try to find the entry.
    if (producing_barrier != 0) {
        DPRINTF(MemDepUnit, "Searching for producer\n");
        MemDepHashIt hash_it = barrierTable.find(producing_barrier);

        if (hash_it != barrierTable.end()) {
            barrier_entry = (*hash_it).second;
            DPRINTF(MemDepUnit, "Producing barrier found\n");
        } else {
            panic("Producing barrier not found\n");
        }
    }

    PointerPair pair;
    pair.dest.valid = false;
    // If no store entry, then instruction can issue as soon as the registers
    // are ready.
    if (!barrier_entry) {
        DPRINTF(MemDepUnit, "No dependency for inst PC "
                "%s [sn:%lli].\n", inst->pcState(), inst->seqNum);
        inst->hasOrderDep = false;

    } else {
        // Otherwise make the instruction dependent on the store/barrier.
        DPRINTF(MemDepUnit, "Adding to dependency list; "
                "inst PC %s is dependent on [sn:%lli].\n",
                inst->pcState(), producing_barrier);

        if (!barrier_entry->positionInvalid) {

            inst->hasOrderDep = true;

            // Clear the bit saying this instruction can issue.
            inst->clearCanIssue();

            auto position = inst->dqPosition;
            inst->bypassOp = memBypassOp;
            position.op = memBypassOp;

            pair.dest = barrier_entry->latestPosition;
            pair.payload = position;

            barrier_entry->latestPosition = position;
            DPRINTF(NoSQSMB, "Creating a valid SMB pair:" ptrfmt "->" ptrfmt "\n",
                    extptr(pair.dest), extptr(pair.payload));

        } else {
            inst->hasOrderDep = false;
            DPRINTF(NoSQSMB, "Store @ " ptrfmt " is found to be invalidated!\n",
                    extptr(barrier_entry->latestPosition));
        }
    }

    return pair;
}

template <class MemDepPred, class Impl>
void
MemDepUnit<MemDepPred, Impl>::insertNonSpec(DynInstPtr &inst)
{

}

template <class MemDepPred, class Impl>
void
MemDepUnit<MemDepPred, Impl>::insertBarrier(DynInstPtr &barr_inst)
{
    InstSeqNum barr_sn = barr_inst->seqNum;
    // Memory barriers block loads and stores, write barriers only stores.
    if (barr_inst->isMemBarrier()) {
        loadBarrier.valid = true;
        loadBarrier.SN = barr_sn;
        storeBarrier.valid = true;
        storeBarrier.SN = barr_sn;
        DPRINTF(MemDepUnit, "Inserted a memory barrier %s SN:%lli\n",
                barr_inst->pcState(),barr_sn);
    } else if (barr_inst->isWriteBarrier()) {
        storeBarrier.valid = true;
        storeBarrier.SN = barr_sn;
        DPRINTF(MemDepUnit, "Inserted a write barrier\n");
    }
    MemDepEntryPtr inst_entry = std::make_shared<MemDepEntry>(barr_inst);
    barrierTable.emplace(barr_sn, inst_entry);
}

template <class MemDepPred, class Impl>
void
MemDepUnit<MemDepPred, Impl>::reschedule(DynInstPtr &inst)
{
    instsToReplay.push_back(inst);
}

template <class MemDepPred, class Impl>
void
MemDepUnit<MemDepPred, Impl>::replay()
{
    DynInstPtr temp_inst;

    // For now this replay function replays all waiting memory ops.
    while (!instsToReplay.empty()) {
        temp_inst = instsToReplay.front();
        DPRINTF(MemDepUnit, "Replaying mem instruction PC %s [sn:%lli].\n",
                temp_inst->pcState(), temp_inst->seqNum);

        moveToReady(temp_inst);

        instsToReplay.pop_front();
    }
}

template <class MemDepPred, class Impl>
void
MemDepUnit<MemDepPred, Impl>::completeBarrier(DynInstPtr &inst)
{
    InstSeqNum barr_sn = inst->seqNum;
    DPRINTF(MemDepUnit, "barrier completed: %s SN:%lli\n", inst->pcState(),
            inst->seqNum);
    if (inst->isMemBarrier()) {
        if (loadBarrier.SN == barr_sn)
            loadBarrier.valid = false;
        if (storeBarrier.SN == barr_sn)
            storeBarrier.valid = false;
    } else if (inst->isWriteBarrier()) {
        if (storeBarrier.SN == barr_sn)
            storeBarrier.valid = false;
    }
}

template <class MemDepPred, class Impl>
void
MemDepUnit<MemDepPred, Impl>::squash(const InstSeqNum &squashed_num,
                                     ThreadID tid)
{
    if (!instsToReplay.empty()) {
        ListIt replay_it = instsToReplay.begin();
        while (replay_it != instsToReplay.end()) {
            if ((*replay_it)->threadNumber == tid &&
                (*replay_it)->seqNum > squashed_num) {
                instsToReplay.erase(replay_it++);
            } else {
                ++replay_it;
            }
        }
    }
    checkAndSquashBarrier(loadBarrier);
}


template <class MemDepPred, class Impl>
inline void
MemDepUnit<MemDepPred, Impl>::moveToReady(DynInstPtr &woken_inst)
{
    DPRINTF(MemDepUnit, "Notify DQ to wakeup [sn:%lli].\n",
            woken_inst->seqNum);
    iqPtr->addReadyMemInst(woken_inst, false);
}

template<class MemDepPred, class Impl>
void MemDepUnit<MemDepPred, Impl>::checkAndSquashBarrier(BarrierInfo &barrier) {
    if (barrier.valid) {
        MemDepEntryPtr entry = barrierTable[barrier.SN];
        if (entry->latestPosition.op != 0) {
            entry->positionInvalid = true;
        }
    }
}


}

#endif//__CPU_FF_MEM_DEP_UNIT_IMPL_HH__
