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
      depPred(params->store_set_clear_period, params->SSITSize,
              params->LFSTSize),
      iqPtr(NULL)
{
    loadBarrier.valid = false;
    storeBarrier.valid = false;
    DPRINTF(MemDepUnit, "Creating MemDepUnit object.\n");
}

template <class MemDepPred, class Impl>
MemDepUnit<MemDepPred, Impl>::~MemDepUnit()
{
    for (ThreadID tid = 0; tid < Impl::MaxThreads; tid++) {

        ListIt inst_list_it = instList[tid].begin();

        MemDepHashIt hash_it;

        while (!instList[tid].empty()) {
            hash_it = memDepHash.find((*inst_list_it)->seqNum);

            assert(hash_it != memDepHash.end());

            memDepHash.erase(hash_it);

            instList[tid].erase(inst_list_it++);
        }
    }

#ifdef DEBUG
    assert(MemDepEntry::memdep_count == 0);
#endif
}

template <class MemDepPred, class Impl>
void
MemDepUnit<MemDepPred, Impl>::init(DerivFFCPUParams *params, ThreadID tid)
{
    DPRINTF(MemDepUnit, "Creating MemDepUnit %i object.\n",tid);

    _name = csprintf("%s.memDep%d", params->name, tid);
    id = tid;

    depPred.init(params->store_set_clear_period, params->SSITSize,
            params->LFSTSize);
}

template <class MemDepPred, class Impl>
void
MemDepUnit<MemDepPred, Impl>::regStats()
{
    insertedLoads
        .name(name() + ".insertedLoads")
        .desc("Number of loads inserted to the mem dependence unit.");

    insertedStores
        .name(name() + ".insertedStores")
        .desc("Number of stores inserted to the mem dependence unit.");

    conflictingLoads
        .name(name() + ".conflictingLoads")
        .desc("Number of conflicting loads.");

    conflictingStores
        .name(name() + ".conflictingStores")
        .desc("Number of conflicting stores.");
}

template <class MemDepPred, class Impl>
bool
MemDepUnit<MemDepPred, Impl>::isDrained() const
{
    bool drained = instsToReplay.empty()
                 && memDepHash.empty()
                 && instsToReplay.empty();
    for (int i = 0; i < Impl::MaxThreads; ++i)
        drained = drained && instList[i].empty();

    return drained;
}

template <class MemDepPred, class Impl>
void
MemDepUnit<MemDepPred, Impl>::drainSanityCheck() const
{
    assert(instsToReplay.empty());
    assert(memDepHash.empty());
    for (int i = 0; i < Impl::MaxThreads; ++i)
        assert(instList[i].empty());
    assert(instsToReplay.empty());
    assert(memDepHash.empty());
}

template <class MemDepPred, class Impl>
void
MemDepUnit<MemDepPred, Impl>::takeOverFrom()
{
    // Be sure to reset all state.
    loadBarrier.valid = storeBarrier.valid = false;
    loadBarrier.SN = storeBarrier.SN = 0;
    depPred.clear();
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
    ThreadID tid = inst->threadNumber;

    MemDepEntryPtr inst_entry = std::make_shared<MemDepEntry>(inst);

    // Add the MemDepEntry to the hash.
    memDepHash.insert(
        std::pair<InstSeqNum, MemDepEntryPtr>(inst->seqNum, inst_entry));
    if (inst->isStore()) {
        DPRINTF(NoSQSMB, "Inserting store @ " ptrfmt "\n",
                extptr(inst->dqPosition));
    };
#ifdef DEBUG
    MemDepEntry::memdep_insert++;
#endif

    instList[tid].push_back(inst);

    inst_entry->listIt = --(instList[tid].end());

    // Check any barriers and the dependence predictor for any
    // producing memrefs/stores.
    InstSeqNum producing_store;
    if (inst->isLoad() && loadBarrier.valid) {
        DPRINTF(MemDepUnit, "Load barrier [sn:%lli] in flight\n",
                loadBarrier.SN);
        producing_store = loadBarrier.SN;
    } else if (inst->isStore() && storeBarrier.valid) {
        DPRINTF(MemDepUnit, "Store barrier [sn:%lli] in flight\n",
                storeBarrier.SN);
        producing_store = storeBarrier.SN;
    } else {
        producing_store = depPred.checkInst(inst->instAddr());
    }

    MemDepEntryPtr store_entry = NULL;

    // If there is a producing store, try to find the entry.
    if (producing_store != 0) {
        DPRINTF(MemDepUnit, "Searching for producer\n");
        MemDepHashIt hash_it = memDepHash.find(producing_store);

        if (hash_it != memDepHash.end()) {
            store_entry = (*hash_it).second;
            DPRINTF(MemDepUnit, "Producer found\n");
        }
    }

    PointerPair pair;
    pair.dest.valid = false;
    // If no store entry, then instruction can issue as soon as the registers
    // are ready.
    if (!store_entry) {
        DPRINTF(MemDepUnit, "No dependency for inst PC "
                "%s [sn:%lli].\n", inst->pcState(), inst->seqNum);

        inst_entry->memDepReady = true;
        inst->hasOrderDep = false;

        if (inst->readyToIssue()) {
            inst_entry->regsReady = true;

            moveToReady(inst_entry);
        }
    } else {
        // Otherwise make the instruction dependent on the store/barrier.
        DPRINTF(MemDepUnit, "Adding to dependency list; "
                "inst PC %s is dependent on [sn:%lli].\n",
                inst->pcState(), producing_store);


        if (!store_entry->positionInvalid && inst->isLoad()) {

            if (inst->readyToIssue()) {
                inst_entry->regsReady = true;
            }
            inst->hasOrderDep = true;

            // Clear the bit saying this instruction can issue.
            inst->clearCanIssue();

            // Add this instruction to the list of dependents.
            store_entry->dependInsts.push_back(inst_entry);

            auto position = inst->findSpareSourcePointer();
            inst->bypassOp = position.op;

            pair.dest = store_entry->latestPosition;
            pair.payload = position;

            store_entry->latestPosition = position;
            DPRINTF(NoSQSMB, "Creating a valid SMB pair:" ptrfmt "->" ptrfmt "\n",
                    extptr(pair.dest), extptr(pair.payload));

        } else if (!inst->isLoad()) {
            inst->hasOrderDep = false;
            DPRINTF(NoSQSMB, "Inst[%lu] is no load, skip\n", inst->seqNum);

        } else {
            inst->hasOrderDep = false;
            DPRINTF(NoSQSMB, "Store @ " ptrfmt " is found to be invalidated!\n",
                    extptr(store_entry->latestPosition));
        }

        if (inst->isLoad()) {
            ++conflictingLoads;
        } else {
            ++conflictingStores;
        }
    }

    if (inst->isStore()) {
        DPRINTF(MemDepUnit, "Inserting store PC %s [sn:%lli].\n",
                inst->pcState(), inst->seqNum);

        depPred.insertStore(inst->instAddr(), inst->seqNum, inst->threadNumber);

        ++insertedStores;
    } else if (inst->isLoad()) {
        ++insertedLoads;
    } else {
        panic("Unknown type! (most likely a barrier).");
    }

    return pair;
}

template <class MemDepPred, class Impl>
void
MemDepUnit<MemDepPred, Impl>::insertNonSpec(DynInstPtr &inst)
{
    ThreadID tid = inst->threadNumber;

    MemDepEntryPtr inst_entry = std::make_shared<MemDepEntry>(inst);

    // Insert the MemDepEntry into the hash.
    memDepHash.insert(
        std::pair<InstSeqNum, MemDepEntryPtr>(inst->seqNum, inst_entry));
#ifdef DEBUG
    MemDepEntry::memdep_insert++;
#endif

    // Add the instruction to the list.
    instList[tid].push_back(inst);

    inst_entry->listIt = --(instList[tid].end());

    // Might want to turn this part into an inline function or something.
    // It's shared between both insert functions.
    if (inst->isStore()) {
        DPRINTF(MemDepUnit, "Inserting store PC %s [sn:%lli].\n",
                inst->pcState(), inst->seqNum);

        depPred.insertStore(inst->instAddr(), inst->seqNum, inst->threadNumber);

        ++insertedStores;
    } else if (inst->isLoad()) {
        ++insertedLoads;
    } else {
        panic("Unknown type! (most likely a barrier).");
    }
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

    ThreadID tid = barr_inst->threadNumber;

    MemDepEntryPtr inst_entry = std::make_shared<MemDepEntry>(barr_inst);

    // Add the MemDepEntry to the hash.
    memDepHash.insert(
        std::pair<InstSeqNum, MemDepEntryPtr>(barr_sn, inst_entry));
#ifdef DEBUG
    MemDepEntry::memdep_insert++;
#endif

    // Add the instruction to the instruction list.
    instList[tid].push_back(barr_inst);

    inst_entry->listIt = --(instList[tid].end());
}

template <class MemDepPred, class Impl>
void
MemDepUnit<MemDepPred, Impl>::regsReady(DynInstPtr &inst)
{
    DPRINTF(MemDepUnit, "Marking registers as ready for "
            "instruction PC %s [sn:%lli].\n",
            inst->pcState(), inst->seqNum);

    MemDepEntryPtr inst_entry = findInHash(inst);

    inst_entry->regsReady = true;

    if (inst_entry->memDepReady) {
        DPRINTF(MemDepUnit, "Instruction has its memory "
                "dependencies resolved, adding it to the ready list.\n");

        moveToReady(inst_entry);
    } else {
        DPRINTF(MemDepUnit, "Instruction still waiting on "
                "memory dependency.\n");
    }
}

template <class MemDepPred, class Impl>
void
MemDepUnit<MemDepPred, Impl>::nonSpecInstReady(DynInstPtr &inst)
{
    DPRINTF(MemDepUnit, "Marking non speculative "
            "instruction PC %s as ready [sn:%lli].\n",
            inst->pcState(), inst->seqNum);

    MemDepEntryPtr inst_entry = findInHash(inst);

    moveToReady(inst_entry);
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

        MemDepEntryPtr inst_entry = findInHash(temp_inst);

        DPRINTF(MemDepUnit, "Replaying mem instruction PC %s [sn:%lli].\n",
                temp_inst->pcState(), temp_inst->seqNum);

        moveToReady(inst_entry);

        instsToReplay.pop_front();
    }
}

template <class MemDepPred, class Impl>
void
MemDepUnit<MemDepPred, Impl>::completed(DynInstPtr &inst)
{
    DPRINTF(MemDepUnit, "Completed mem instruction PC %s [sn:%lli].\n",
            inst->pcState(), inst->seqNum);

    ThreadID tid = inst->threadNumber;

    // Remove the instruction from the hash and the list.
    MemDepHashIt hash_it = memDepHash.find(inst->seqNum);

    assert(hash_it != memDepHash.end());

    instList[tid].erase((*hash_it).second->listIt);

    (*hash_it).second = NULL;

    memDepHash.erase(hash_it);
#ifdef DEBUG
    MemDepEntry::memdep_erase++;
#endif
}

template <class MemDepPred, class Impl>
void
MemDepUnit<MemDepPred, Impl>::completeBarrier(DynInstPtr &inst)
{
    wakeDependents(inst);
    completed(inst);

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
MemDepUnit<MemDepPred, Impl>::wakeDependents(DynInstPtr &inst)
{
    // Only stores and barriers have dependents.
    if (!inst->isStore() && !inst->isMemBarrier() && !inst->isWriteBarrier()) {
        return;
    }

    MemDepEntryPtr inst_entry = findInHash(inst);

    for (int i = 0; i < inst_entry->dependInsts.size(); ++i ) {
        MemDepEntryPtr woken_inst = inst_entry->dependInsts[i];

        if (!woken_inst->inst) {
            // Potentially removed mem dep entries could be on this list
            continue;
        }

        DPRINTF(MemDepUnit, "Waking up a dependent inst, "
                "[sn:%lli].\n",
                woken_inst->inst->seqNum);

        // if (woken_inst->regsReady && !woken_inst->squashed) {
        //     moveToReady(woken_inst);
        // !!! note that now we do not wake up dependants from mdu
        // } else {
        //     woken_inst->memDepReady = true;
        // }
        if (!woken_inst->squashed) {
            moveToReady(woken_inst);
            // moveToReady -> addReadyMemInst -> send wake ptr to entry
        }
    }

    inst_entry->dependInsts.clear();
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

    ListIt squash_it = instList[tid].end();
    --squash_it;

    MemDepHashIt hash_it;

    for (auto &pair: memDepHash) {
        if (pair.second->latestPosition.valid &&
                pair.second->latestPosition.op != 0) {
            // it is not pointing to a store
            pair.second->positionInvalid = true;
            DPRINTF(NoSQSMB, "Marking store @ " ptrfmt " invalid\n",
                    extptr(pair.second->latestPosition));
        }
    }

    while (!instList[tid].empty() &&
           (*squash_it)->seqNum > squashed_num) {

        DPRINTF(MemDepUnit, "Squashing inst [sn:%lli]\n",
                (*squash_it)->seqNum);

        if ((*squash_it)->seqNum == loadBarrier.SN)
              loadBarrier.valid = false;

        if ((*squash_it)->seqNum == storeBarrier.SN)
              storeBarrier.valid = false;

        hash_it = memDepHash.find((*squash_it)->seqNum);

        assert(hash_it != memDepHash.end());

        (*hash_it).second->squashed = true;

        (*hash_it).second = NULL;

        memDepHash.erase(hash_it);
#ifdef DEBUG
        MemDepEntry::memdep_erase++;
#endif

        instList[tid].erase(squash_it--);
    }

    // Tell the dependency predictor to squash as well.
    depPred.squash(squashed_num, tid);
}

template <class MemDepPred, class Impl>
void
MemDepUnit<MemDepPred, Impl>::violation(DynInstPtr &store_inst,
                                        DynInstPtr &violating_load)
{
    DPRINTF(MemDepUnit, "Passing violating PCs to store sets,"
            " load: %#x, store: %#x\n", violating_load->instAddr(),
            store_inst->instAddr());
    // Tell the memory dependence unit of the violation.
    depPred.violation(store_inst->instAddr(), violating_load->instAddr());
}

template <class MemDepPred, class Impl>
void
MemDepUnit<MemDepPred, Impl>::fpBypass(DynInstPtr &violating_load)
{
    DPRINTF(MemDepUnit, "Passing violating PCs to predictor,"
            " load: %#x\n", violating_load->instAddr());
    // Tell the memory dependence unit of the violation.
    depPred.fpBypass(violating_load->instAddr());

}

template <class MemDepPred, class Impl>
void
MemDepUnit<MemDepPred, Impl>::issue(DynInstPtr &inst)
{
    DPRINTF(MemDepUnit, "Issuing instruction PC %#x [sn:%lli].\n",
            inst->instAddr(), inst->seqNum);

    depPred.issued(inst->instAddr(), inst->seqNum, inst->isStore());
}

template <class MemDepPred, class Impl>
inline typename MemDepUnit<MemDepPred,Impl>::MemDepEntryPtr &
MemDepUnit<MemDepPred, Impl>::findInHash(const DynInstPtr &inst)
{
    MemDepHashIt hash_it = memDepHash.find(inst->seqNum);

    assert(hash_it != memDepHash.end());

    return (*hash_it).second;
}

template <class MemDepPred, class Impl>
inline void
MemDepUnit<MemDepPred, Impl>::moveToReady(MemDepEntryPtr &woken_inst_entry)
{
    DPRINTF(MemDepUnit, "Adding instruction [sn:%lli] "
            "to the ready list.\n", woken_inst_entry->inst->seqNum);

    assert(!woken_inst_entry->squashed);

    iqPtr->addReadyMemInst(woken_inst_entry->inst);
}


template <class MemDepPred, class Impl>
void
MemDepUnit<MemDepPred, Impl>::dumpLists()
{
    for (ThreadID tid = 0; tid < Impl::MaxThreads; tid++) {
        cprintf("Instruction list %i size: %i\n",
                tid, instList[tid].size());

        ListIt inst_list_it = instList[tid].begin();
        int num = 0;

        while (inst_list_it != instList[tid].end()) {
            cprintf("Instruction:%i\nPC: %s\n[sn:%i]\n[tid:%i]\nIssued:%i\n"
                    "Squashed:%i\n\n",
                    num, (*inst_list_it)->pcState(),
                    (*inst_list_it)->seqNum,
                    (*inst_list_it)->threadNumber,
                    (*inst_list_it)->isIssued(),
                    (*inst_list_it)->isSquashed());
            inst_list_it++;
            ++num;
        }
    }

    cprintf("Memory dependence hash size: %i\n", memDepHash.size());

#ifdef DEBUG
    cprintf("Memory dependence entries: %i\n", MemDepEntry::memdep_count);
#endif
}

}

#endif//__CPU_FF_MEM_DEP_UNIT_IMPL_HH__
