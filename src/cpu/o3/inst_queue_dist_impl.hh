/*
 * Copyright (c) 2011-2014, 2017-2020 ARM Limited
 * Copyright (c) 2013 Advanced Micro Devices, Inc.
 * All rights reserved.
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
 */

#ifndef __CPU_O3_INST_QUEUE_IMPL_HH__
#define __CPU_O3_INST_QUEUE_IMPL_HH__

#include <limits>
#include <vector>

#include "base/logging.hh"
#include "cpu/o3/fu_pool.hh"
#include "cpu/o3/inst_queue_dist.hh"
#include "debug/IQ.hh"
#include "enums/OpClass.hh"
#include "params/DerivO3CPU.hh"
#include "sim/core.hh"

// clang complains about std::set being overloaded with Packet::set if
// we open up the entire namespace std
using std::list;

template <class Impl>
InstructionQueueDist<Impl>::FUCompletion::FUCompletion(const DynInstPtr &_inst,
    int fu_idx, InstructionQueueDist<Impl> *iq_ptr)
    : Event(Stat_Event_Pri, AutoDelete),
      inst(_inst), fuIdx(fu_idx), iqPtr(iq_ptr), freeFU(false)
{
}

template <class Impl>
void
InstructionQueueDist<Impl>::FUCompletion::process()
{
    iqPtr->processFUCompletion(inst, freeFU ? fuIdx : -1);
    inst = NULL;
}


template <class Impl>
const char *
InstructionQueueDist<Impl>::FUCompletion::description() const
{
    return "Functional unit completion";
}

template <class Impl>
InstructionQueueDist<Impl>::InstructionQueueDist(O3CPU *cpu_ptr, IEW *iew_ptr,
                                         const DerivO3CPUParams &params)
    : cpu(cpu_ptr),
      iewStage(iew_ptr),
      fuPool(params.fuPool),
      iqPolicy(params.smtIQPolicy),
      numThreads(params.numThreads),
      numEntries(params.numIQEntries),
      totalWidth(params.issueWidth),
      commitToIEWDelay(params.commitToIEWDelay),
      iqStats(cpu, totalWidth),
      iqIOStats(cpu)
{
    assert(fuPool);
    numFU = fuPool->size();

    readyInsts = std::vector<ReadyInstQueue>(numFU);
    listOrders = std::vector<ListOrder>(numFU);
    queueOnList = std::vector<bool>(numFU);
    readyIt = std::vector<ListOrderIt>(numFU);

    // Set the number of total physical registers
    // As the vector registers have two addressing modes, they are added twice
    numPhysRegs = params.numPhysIntRegs + params.numPhysFloatRegs +
                    params.numPhysVecRegs +
                    params.numPhysVecRegs * TheISA::NumVecElemPerVecReg +
                    params.numPhysVecPredRegs +
                    params.numPhysCCRegs;

    //Create an entry for each physical register within the
    //dependency graph.
    dependGraph.resize(numPhysRegs);

    // Resize the register scoreboard.
    regScoreboard.resize(numPhysRegs);

    for (ThreadID tid = 0; tid < Impl::MaxThreads; tid++) {
        //Initialize Mem Dependence Units
        memDepUnit[tid].init(params, tid, cpu_ptr);
        //Initialize Distributed Inst Queue
        distInstLists[tid] = std::vector<InstList>(numFU);
    }

    resetState();

    //Figure out resource sharing policy
    if (iqPolicy == SMTQueuePolicy::Dynamic) {
        //Set Max Entries to Total ROB Capacity
        for (ThreadID tid = 0; tid < numThreads; tid++) {
            maxEntries[tid] = numEntries;
        }

    } else if (iqPolicy == SMTQueuePolicy::Partitioned) {
        //@todo:make work if part_amt doesnt divide evenly.
        int part_amt = numEntries / numThreads;

        //Divide ROB up evenly
        for (ThreadID tid = 0; tid < numThreads; tid++) {
            maxEntries[tid] = part_amt;
        }

        DPRINTF(IQ, "IQ sharing policy set to Partitioned:"
                "%i entries per thread.\n",part_amt);
    } else if (iqPolicy == SMTQueuePolicy::Threshold) {
        double threshold =  (double)params.smtIQThreshold / 100;

        int thresholdIQ = (int)((double)threshold * numEntries);

        //Divide up by threshold amount
        for (ThreadID tid = 0; tid < numThreads; tid++) {
            maxEntries[tid] = thresholdIQ;
        }

        DPRINTF(IQ, "IQ sharing policy set to Threshold:"
                "%i entries per thread.\n",thresholdIQ);
   }
    for (ThreadID tid = numThreads; tid < Impl::MaxThreads; tid++) {
        maxEntries[tid] = 0;
    }
}

template <class Impl>
InstructionQueueDist<Impl>::~InstructionQueueDist()
{
    dependGraph.reset();
#ifdef DEBUG
    cprintf("Nodes traversed: %i, removed: %i\n",
            dependGraph.nodesTraversed, dependGraph.nodesRemoved);
#endif
}

template <class Impl>
std::string
InstructionQueueDist<Impl>::name() const
{
    return cpu->name() + ".iq";
}

template <class Impl>
InstructionQueueDist<Impl>::
IQStats::IQStats(O3CPU *cpu, const unsigned &total_width)
    : Stats::Group(cpu),
    ADD_STAT(instsAdded,
             "Number of instructions added to the IQ (excludes non-spec)"),
    ADD_STAT(nonSpecInstsAdded,
             "Number of non-speculative instructions added to the IQ"),
    ADD_STAT(instsIssued, "Number of instructions issued"),
    ADD_STAT(intInstsIssued, "Number of integer instructions issued"),
    ADD_STAT(floatInstsIssued, "Number of float instructions issued"),
    ADD_STAT(branchInstsIssued, "Number of branch instructions issued"),
    ADD_STAT(memInstsIssued, "Number of memory instructions issued"),
    ADD_STAT(miscInstsIssued, "Number of miscellaneous instructions issued"),
    ADD_STAT(squashedInstsIssued, "Number of squashed instructions issued"),
    ADD_STAT(squashedInstsExamined,
             "Number of squashed instructions iterated over during squash; "
             "mainly for profiling"),
    ADD_STAT(squashedOperandsExamined,
             "Number of squashed operands that are examined and possibly "
             "removed from graph"),
    ADD_STAT(squashedNonSpecRemoved,
             "Number of squashed non-spec instructions that were removed"),
    ADD_STAT(numIssuedDist, "Number of insts issued each cycle"),
    ADD_STAT(statFuBusy, "attempts to use FU when none available"),
    ADD_STAT(statIssuedInstType, "Type of FU issued"),
    ADD_STAT(issueRate, "Inst issue rate",
             instsIssued / cpu->baseStats.numCycles),
    ADD_STAT(fuBusy, "FU busy when requested"),
    ADD_STAT(fuBusyRate, "FU busy rate (busy events/executed inst)")
{
    instsAdded
        .prereq(instsAdded);

    nonSpecInstsAdded
        .prereq(nonSpecInstsAdded);

    instsIssued
        .prereq(instsIssued);

    intInstsIssued
        .prereq(intInstsIssued);

    floatInstsIssued
        .prereq(floatInstsIssued);

    branchInstsIssued
        .prereq(branchInstsIssued);

    memInstsIssued
        .prereq(memInstsIssued);

    miscInstsIssued
        .prereq(miscInstsIssued);

    squashedInstsIssued
        .prereq(squashedInstsIssued);

    squashedInstsExamined
        .prereq(squashedInstsExamined);

    squashedOperandsExamined
        .prereq(squashedOperandsExamined);

    squashedNonSpecRemoved
        .prereq(squashedNonSpecRemoved);
/*
    queueResDist
        .init(Num_OpClasses, 0, 99, 2)
        .name(name() + ".IQ:residence:")
        .desc("cycles from dispatch to issue")
        .flags(total | pdf | cdf )
        ;
    for (int i = 0; i < Num_OpClasses; ++i) {
        queueResDist.subname(i, opClassStrings[i]);
    }
*/
    numIssuedDist
        .init(0,total_width,1)
        .flags(Stats::pdf)
        ;
/*
    dist_unissued
        .init(Num_OpClasses+2)
        .name(name() + ".unissued_cause")
        .desc("Reason ready instruction not issued")
        .flags(pdf | dist)
        ;
    for (int i=0; i < (Num_OpClasses + 2); ++i) {
        dist_unissued.subname(i, unissued_names[i]);
    }
*/
    statIssuedInstType
        .init(cpu->numThreads,Enums::Num_OpClass)
        .flags(Stats::total | Stats::pdf | Stats::dist)
        ;
    statIssuedInstType.ysubnames(Enums::OpClassStrings);

    //
    //  How long did instructions for a particular FU type wait prior to issue
    //
/*
    issueDelayDist
        .init(Num_OpClasses,0,99,2)
        .name(name() + ".")
        .desc("cycles from operands ready to issue")
        .flags(pdf | cdf)
        ;
    for (int i=0; i<Num_OpClasses; ++i) {
        std::stringstream subname;
        subname << opClassStrings[i] << "_delay";
        issueDelayDist.subname(i, subname.str());
    }
*/
    issueRate
        .flags(Stats::total)
        ;

    statFuBusy
        .init(Num_OpClasses)
        .flags(Stats::pdf | Stats::dist)
        ;
    for (int i=0; i < Num_OpClasses; ++i) {
        statFuBusy.subname(i, Enums::OpClassStrings[i]);
    }

    fuBusy
        .init(cpu->numThreads)
        .flags(Stats::total)
        ;

    fuBusyRate
        .flags(Stats::total)
        ;
    fuBusyRate = fuBusy / instsIssued;
}

template <class Impl>
InstructionQueueDist<Impl>::
IQIOStats::IQIOStats(Stats::Group *parent)
    : Stats::Group(parent),
    ADD_STAT(intInstQueueReads, "Number of integer instruction queue reads"),
    ADD_STAT(intInstQueueWrites, "Number of integer instruction queue writes"),
    ADD_STAT(intInstQueueWakeupAccesses, "Number of integer instruction queue "
                                         "wakeup accesses"),
    ADD_STAT(fpInstQueueReads, "Number of floating instruction queue reads"),
    ADD_STAT(fpInstQueueWrites, "Number of floating instruction queue writes"),
    ADD_STAT(fpInstQueueWakeupAccesses, "Number of floating instruction queue "
                                        "wakeup accesses"),
    ADD_STAT(vecInstQueueReads, "Number of vector instruction queue reads"),
    ADD_STAT(vecInstQueueWrites, "Number of vector instruction queue writes"),
    ADD_STAT(vecInstQueueWakeupAccesses, "Number of vector instruction queue "
                                         "wakeup accesses"),
    ADD_STAT(intAluAccesses, "Number of integer alu accesses"),
    ADD_STAT(fpAluAccesses, "Number of floating point alu accesses"),
    ADD_STAT(vecAluAccesses, "Number of vector alu accesses")
{
    using namespace Stats;
    intInstQueueReads
        .flags(total);

    intInstQueueWrites
        .flags(total);

    intInstQueueWakeupAccesses
        .flags(total);

    fpInstQueueReads
        .flags(total);

    fpInstQueueWrites
        .flags(total);

    fpInstQueueWakeupAccesses
        .flags(total);

    vecInstQueueReads
        .flags(total);

    vecInstQueueWrites
        .flags(total);

    vecInstQueueWakeupAccesses
        .flags(total);

    intAluAccesses
        .flags(total);

    fpAluAccesses
        .flags(total);

    vecAluAccesses
        .flags(total);
}

template <class Impl>
void
InstructionQueueDist<Impl>::resetState()
{
    //Initialize thread IQ counts
    for (ThreadID tid = 0; tid < Impl::MaxThreads; tid++) {
        count[tid] = 0;
        instList[tid].clear();
        for (int idx = 0; idx < numFU; ++idx){
            distInstLists[tid][idx].clear();
        }
    }

    // Initialize the number of free IQ entries.
    freeEntries = numEntries;

    // Note that in actuality, the registers corresponding to the logical
    // registers start off as ready.  However this doesn't matter for the
    // IQ as the instruction should have been correctly told if those
    // registers are ready in rename.  Thus it can all be initialized as
    // unready.
    for (int i = 0; i < numPhysRegs; ++i) {
        regScoreboard[i] = false;
    }

    for (ThreadID tid = 0; tid < Impl::MaxThreads; ++tid) {
        squashedSeqNum[tid] = 0;
    }

    for (int i = 0; i < numFU; ++i) {
        while (!readyInsts[i].empty())
            readyInsts[i].pop();
        queueOnList[i] = false;
        readyIt[i] = listOrders[i].end();
        listOrders[i].clear();
    }
    nonSpecInsts.clear();
    deferredMemInsts.clear();
    blockedMemInsts.clear();
    retryMemInsts.clear();
    wbOutstanding = 0;
}

template <class Impl>
void
InstructionQueueDist<Impl>::setActiveThreads(list<ThreadID> *at_ptr)
{
    activeThreads = at_ptr;
}

template <class Impl>
void
InstructionQueueDist<Impl>::setIssueToExecuteQueue(TimeBuffer<IssueStruct> *i2e_ptr)
{
      issueToExecuteQueue = i2e_ptr;
}

template <class Impl>
void
InstructionQueueDist<Impl>::setTimeBuffer(TimeBuffer<TimeStruct> *tb_ptr)
{
    timeBuffer = tb_ptr;

    fromCommit = timeBuffer->getWire(-commitToIEWDelay);
}

template <class Impl>
bool
InstructionQueueDist<Impl>::isDrained() const
{
    bool drained = dependGraph.empty() &&
                   instsToExecute.empty() &&
                   wbOutstanding == 0;
    for (ThreadID tid = 0; tid < numThreads; ++tid)
        drained = drained && memDepUnit[tid].isDrained();

    return drained;
}

template <class Impl>
void
InstructionQueueDist<Impl>::drainSanityCheck() const
{
    assert(dependGraph.empty());
    assert(instsToExecute.empty());
    for (ThreadID tid = 0; tid < numThreads; ++tid)
        memDepUnit[tid].drainSanityCheck();
}

template <class Impl>
void
InstructionQueueDist<Impl>::takeOverFrom()
{
    resetState();
}

template <class Impl>
int
InstructionQueueDist<Impl>::entryAmount(ThreadID num_threads)
{
    if (iqPolicy == SMTQueuePolicy::Partitioned) {
        return numEntries / num_threads;
    } else {
        return 0;
    }
}


template <class Impl>
void
InstructionQueueDist<Impl>::resetEntries()
{
    if (iqPolicy != SMTQueuePolicy::Dynamic || numThreads > 1) {
        int active_threads = activeThreads->size();

        list<ThreadID>::iterator threads = activeThreads->begin();
        list<ThreadID>::iterator end = activeThreads->end();

        while (threads != end) {
            ThreadID tid = *threads++;

            if (iqPolicy == SMTQueuePolicy::Partitioned) {
                maxEntries[tid] = numEntries / active_threads;
            } else if (iqPolicy == SMTQueuePolicy::Threshold &&
                       active_threads == 1) {
                maxEntries[tid] = numEntries;
            }
        }
    }
}

template <class Impl>
unsigned
InstructionQueueDist<Impl>::numFreeEntries()
{
    return freeEntries;
}

template <class Impl>
unsigned
InstructionQueueDist<Impl>::numFreeEntries(ThreadID tid)
{
    return maxEntries[tid] - count[tid];
}

// Might want to do something more complex if it knows how many instructions
// will be issued this cycle.
template <class Impl>
bool
InstructionQueueDist<Impl>::isFull()
{
    if (freeEntries == 0) {
        return(true);
    } else {
        return(false);
    }
}

template <class Impl>
bool
InstructionQueueDist<Impl>::isFull(ThreadID tid)
{
    if (numFreeEntries(tid) == 0) {
        return(true);
    } else {
        return(false);
    }
}

template <class Impl>
bool
InstructionQueueDist<Impl>::hasReadyInsts()
{
    for (int i = 0; i < numFU; ++i) {
        if (!listOrders[i].empty() || !readyInsts[i].empty()){
            return true;
        }
    }

    return false;
}

template <class Impl>
void
InstructionQueueDist<Impl>::insert(const DynInstPtr &new_inst)
{
    if (new_inst->isFloating()) {
        iqIOStats.fpInstQueueWrites++;
    } else if (new_inst->isVector()) {
        iqIOStats.vecInstQueueWrites++;
    } else {
        iqIOStats.intInstQueueWrites++;
    }
    // Make sure the instruction is valid
    assert(new_inst);


    assert(freeEntries != 0);

    instList[new_inst->threadNumber].push_back(new_inst);

    // For now, we don't add any constraints here
    OpClass op_class = new_inst->opClass();
    int fu_idx = fuPool->allocUnit(op_class);
    new_inst->setFuIdx(fu_idx);
    if (fu_idx > FUPool::NoFreeFU){
        distInstLists[new_inst->threadNumber][fu_idx].push_back(new_inst);
    }

    DPRINTF(IQ, "Adding instruction [sn:%llu] PC %s fuidx:%i to the IQ.\n",
            new_inst->seqNum, new_inst->pcState(), fu_idx);

    --freeEntries;

    new_inst->setInIQ();

    // Look through its source registers (physical regs), and mark any
    // dependencies.
    addToDependents(new_inst);

    // Have this instruction set itself as the producer of its destination
    // register(s).
    addToProducers(new_inst);

    if (new_inst->isMemRef()) {
        memDepUnit[new_inst->threadNumber].insert(new_inst);
    } else {
        addIfReady(new_inst);
    }

    ++iqStats.instsAdded;

    count[new_inst->threadNumber]++;

    assert(freeEntries == (numEntries - countInsts()));
}

template <class Impl>
void
InstructionQueueDist<Impl>::insertNonSpec(const DynInstPtr &new_inst)
{
    // @todo: Clean up this code; can do it by setting inst as unable
    // to issue, then calling normal insert on the inst.
    if (new_inst->isFloating()) {
        iqIOStats.fpInstQueueWrites++;
    } else if (new_inst->isVector()) {
        iqIOStats.vecInstQueueWrites++;
    } else {
        iqIOStats.intInstQueueWrites++;
    }

    assert(new_inst);

    nonSpecInsts[new_inst->seqNum] = new_inst;


    assert(freeEntries != 0);

    instList[new_inst->threadNumber].push_back(new_inst);

    // For now, we don't add any constraints here
    OpClass op_class = new_inst->opClass();
    int fu_idx = fuPool->allocUnit(op_class);
    new_inst->setFuIdx(fu_idx);
    if (fu_idx > FUPool::NoFreeFU){
        distInstLists[new_inst->threadNumber][fu_idx].push_back(new_inst);
    }
    DPRINTF(IQ, "Adding non-speculative instruction [sn:%llu] PC %s fuidx:%i"
                "to the IQ.\n",
            new_inst->seqNum, new_inst->pcState(), fu_idx);

    --freeEntries;

    new_inst->setInIQ();

    // Have this instruction set itself as the producer of its destination
    // register(s).
    addToProducers(new_inst);

    // If it's a memory instruction, add it to the memory dependency
    // unit.
    if (new_inst->isMemRef()) {
        memDepUnit[new_inst->threadNumber].insertNonSpec(new_inst);
    }

    ++iqStats.nonSpecInstsAdded;

    count[new_inst->threadNumber]++;

    assert(freeEntries == (numEntries - countInsts()));
}

template <class Impl>
void
InstructionQueueDist<Impl>::insertBarrier(const DynInstPtr &barr_inst)
{
    memDepUnit[barr_inst->threadNumber].insertBarrier(barr_inst);

    insertNonSpec(barr_inst);
}

template <class Impl>
typename Impl::DynInstPtr
InstructionQueueDist<Impl>::getInstToExecute()
{
    assert(!instsToExecute.empty());
    DynInstPtr inst = std::move(instsToExecute.front());
    instsToExecute.pop_front();
    if (inst->isFloating()) {
        iqIOStats.fpInstQueueReads++;
    } else if (inst->isVector()) {
        iqIOStats.vecInstQueueReads++;
    } else {
        iqIOStats.intInstQueueReads++;
    }
    return inst;
}

template <class Impl>
void
InstructionQueueDist<Impl>::addToOrderList(int fu_idx)
{
    assert(!readyInsts[fu_idx].empty());

    ListOrderEntry queue_entry;

    queue_entry.fu_idx = fu_idx;
    queue_entry.oldestInst = readyInsts[fu_idx].top()->seqNum;

    ListOrder& listOrder = listOrders[fu_idx];

    ListOrderIt list_it = listOrder.begin();
    ListOrderIt list_end_it = listOrder.end();

    while (list_it != list_end_it) {
        if ((*list_it).oldestInst > queue_entry.oldestInst) {
            break;
        }

        list_it++;
    }

    readyIt[fu_idx] = listOrder.insert(list_it, queue_entry);
    queueOnList[fu_idx] = true;

    DPRINTF(IQ, "[%s] fu=[%i] ready size: %d list size: %d (%d)\n",
            __FUNCTION__,
            fu_idx, readyInsts[fu_idx].size(), listOrder.size(), listOrders[fu_idx].size());
}

template <class Impl>
void
InstructionQueueDist<Impl>::moveToYoungerInst(ListOrderIt list_order_it)
{
    // Get iterator of next item on the list
    // Delete the original iterator
    // Determine if the next item is either the end of the list or younger
    // than the new instruction.  If so, then add in a new iterator right here.
    // If not, then move along.
    ListOrderEntry queue_entry;
    unsigned fu_idx = (*list_order_it).fu_idx;
    ListOrderIt next_it = list_order_it;

    ++next_it;

    queue_entry.fu_idx = fu_idx;
    queue_entry.oldestInst = readyInsts[fu_idx].top()->seqNum;

    ListOrder& listOrder = listOrders[fu_idx];

    while (next_it != listOrder.end() &&
           (*next_it).oldestInst < queue_entry.oldestInst) {
        ++next_it;
    }

    readyIt[fu_idx] = listOrder.insert(next_it, queue_entry);
}

template <class Impl>
void
InstructionQueueDist<Impl>::processFUCompletion(const DynInstPtr &inst, int fu_idx)
{
    DPRINTF(IQ, "Processing FU completion [sn:%llu]\n", inst->seqNum);
    assert(!cpu->switchedOut());
    // The CPU could have been sleeping until this op completed (*extremely*
    // long latency op).  Wake it if it was.  This may be overkill.
   --wbOutstanding;
    iewStage->wakeCPU();

    if (fu_idx > -1)
        fuPool->freeUnitNextCycle(fu_idx);

    // @todo: Ensure that these FU Completions happen at the beginning
    // of a cycle, otherwise they could add too many instructions to
    // the queue.
    issueToExecuteQueue->access(-1)->size++;
    instsToExecute.push_back(inst);
}

// @todo: Figure out a better way to remove the squashed items from the
// lists.  Checking the top item of each list to see if it's squashed
// wastes time and forces jumps.
template <class Impl>
void
InstructionQueueDist<Impl>::scheduleReadyInsts()
{
    DPRINTF(IQ, "Attempting to schedule ready instructions from "
                "the IQ.\n");

    IssueStruct *i2e_info = issueToExecuteQueue->access(0);

    DynInstPtr mem_inst;
    while (mem_inst = std::move(getDeferredMemInstToExecute())) {
        addReadyMemInst(mem_inst);
    }

    // See if any cache blocked instructions are able to be executed
    while (mem_inst = std::move(getBlockedMemInstToExecute())) {
        addReadyMemInst(mem_inst);
    }

    int total_issued = 0;
    // For each distributed-IQ, pick a oldest ready inst.
    for (int fu_idx = 0; fu_idx < numFU; fu_idx++) {

        ListOrder& listOrder = listOrders[fu_idx];
        ListOrderIt order_it = listOrder.begin();
        ListOrderIt order_end_it = listOrder.end();
        bool fu_issued = false;

        DPRINTF(IQ, "Schedule FU[%i] list size:%d ready size: %d\n",
                fu_idx, listOrder.size(), readyInsts[fu_idx].size());

        while (order_it != order_end_it && !fu_issued) {
            assert(!readyInsts[fu_idx].empty());
            DynInstPtr issuing_inst = readyInsts[fu_idx].top();

            if (issuing_inst->isFloating()) {
                iqIOStats.fpInstQueueReads++;
            } else if (issuing_inst->isVector()) {
                iqIOStats.vecInstQueueReads++;
            } else {
                iqIOStats.intInstQueueReads++;
            }

            assert(issuing_inst->seqNum == (*order_it).oldestInst);

            if (issuing_inst->isSquashed()) {
                readyInsts[fu_idx].pop();

                if (!readyInsts[fu_idx].empty()) {
                    moveToYoungerInst(order_it);
                } else {
                    readyIt[fu_idx] = listOrder.end();
                    queueOnList[fu_idx] = false;
                }

                listOrder.erase(order_it++);

                ++iqStats.squashedInstsIssued;

                continue;
            }

            int idx = fuPool->getUnit(fu_idx);

            // In our distributed implementation,
            // every op_class(even No_OpClass) must go to a function unit.
            // TODO: remove this assert to handle speculative execute
            //  instructions with idx == FUPool::NoCapableFU
            assert(idx > FUPool::NoCapableFU);

            Cycles op_latency = Cycles(1);
            ThreadID tid = issuing_inst->threadNumber;

            OpClass op_class = issuing_inst->opClass();
            if (issuing_inst->isFloating()) {
                iqIOStats.fpAluAccesses++;
            } else if (issuing_inst->isVector()) {
                iqIOStats.vecAluAccesses++;
            } else {
                iqIOStats.intAluAccesses++;
            }
            if (idx > FUPool::NoFreeFU) {
                op_latency = fuPool->getOpLatency(op_class);
            }

            DPRINTF(IQ, "FU[%d] op_class: %d idx: %d\n", fu_idx, op_class, idx);

            // If we have an instruction that doesn't require a FU, or a
            // valid FU, then schedule for execution.
            if (idx != FUPool::NoFreeFU) {
                if (op_latency == Cycles(1)) {
                    i2e_info->size++;
                    instsToExecute.push_back(issuing_inst);

                    // Add the FU onto the list of FU's to be freed next
                    // cycle if we used one.
                    if (idx >= 0)
                        fuPool->freeUnitNextCycle(idx);
                } else {
                    bool pipelined = fuPool->isPipelined(op_class);
                    // Generate completion event for the FU
                    ++wbOutstanding;
                    FUCompletion *execution = new FUCompletion(issuing_inst,
                                                               idx, this);

                    cpu->schedule(execution,
                                  cpu->clockEdge(Cycles(op_latency - 1)));

                    if (!pipelined) {
                        // If FU isn't pipelined, then it must be freed
                        // upon the execution completing.
                        execution->setFreeFU();
                    } else {
                        // Add the FU onto the list of FU's to be freed next cycle.
                        fuPool->freeUnitNextCycle(idx);
                    }
                }

                DPRINTF(IQ, "Thread %i: Issuing instruction PC %s "
                            "[sn:%llu]\n",
                        tid, issuing_inst->pcState(),
                        issuing_inst->seqNum);

                readyInsts[idx].pop();

                if (!readyInsts[idx].empty()) {
                    moveToYoungerInst(order_it);
                } else {
                    readyIt[idx] = listOrder.end();
                    queueOnList[idx] = false;
                }

                issuing_inst->setIssued();
                ++total_issued;
                fu_issued = true;

#if TRACING_ON
                issuing_inst->issueTick = curTick() - issuing_inst->fetchTick;
#endif

                if (!issuing_inst->isMemRef()) {
                    // Memory instructions can not be freed from the IQ until they
                    // complete.
                    ++freeEntries;
                    count[tid]--;
                    issuing_inst->clearInIQ();
                } else {
                    memDepUnit[tid].issue(issuing_inst);
                }

                listOrder.erase(order_it++);
                iqStats.statIssuedInstType[tid][op_class]++;
            } else {
                iqStats.statFuBusy[op_class]++;
                iqStats.fuBusy[tid]++;
                ++order_it;
            }
        }
    }

    iqStats.numIssuedDist.sample(total_issued);
    iqStats.instsIssued+= total_issued;

    // If we issued any instructions, tell the CPU we had activity.
    // @todo If the way deferred memory instructions are handeled due to
    // translation changes then the deferredMemInsts condition should be removed
    // from the code below.
    if (total_issued || !retryMemInsts.empty() || !deferredMemInsts.empty()) {
        cpu->activityThisCycle();
    } else {
        DPRINTF(IQ, "Not able to schedule any instructions.\n");
    }
}

template <class Impl>
void
InstructionQueueDist<Impl>::scheduleNonSpec(const InstSeqNum &inst)
{
    DPRINTF(IQ, "Marking nonspeculative instruction [sn:%llu] as ready "
            "to execute.\n", inst);

    NonSpecMapIt inst_it = nonSpecInsts.find(inst);

    assert(inst_it != nonSpecInsts.end());

    ThreadID tid = (*inst_it).second->threadNumber;

    (*inst_it).second->setAtCommit();

    (*inst_it).second->setCanIssue();

    if (!(*inst_it).second->isMemRef()) {
        addIfReady((*inst_it).second);
    } else {
        memDepUnit[tid].nonSpecInstReady((*inst_it).second);
    }

    (*inst_it).second = NULL;

    nonSpecInsts.erase(inst_it);
}

template <class Impl>
void
InstructionQueueDist<Impl>::commit(const InstSeqNum &inst, ThreadID tid)
{
    DPRINTF(IQ, "[tid:%i] Committing instructions older than [sn:%llu]\n",
            tid,inst);

    // Update unified IQ
    ListIt iq_it = instList[tid].begin();

    while (iq_it != instList[tid].end() &&
           (*iq_it)->seqNum <= inst) {
        ++iq_it;
        instList[tid].pop_front();
    }
    assert(freeEntries == (numEntries - countInsts()));

    // Update distributed IQ

    // This is slow, but it works...
    for (int fu_idx = 0; fu_idx < numFU; fu_idx++){
        InstList& iList = distInstLists[tid][fu_idx];
        iq_it = iList.begin();
        while (iq_it != iList.end() && (*iq_it)->seqNum <= inst){
            ++iq_it;
            iList.pop_front();
        }
    }
    // TODO: check instruction count in distributed IQ
}

template <class Impl>
int
InstructionQueueDist<Impl>::wakeDependents(const DynInstPtr &completed_inst)
{
    int dependents = 0;

    // The instruction queue here takes care of both floating and int ops
    if (completed_inst->isFloating()) {
        iqIOStats.fpInstQueueWakeupAccesses++;
    } else if (completed_inst->isVector()) {
        iqIOStats.vecInstQueueWakeupAccesses++;
    } else {
        iqIOStats.intInstQueueWakeupAccesses++;
    }

    DPRINTF(IQ, "Waking dependents of completed instruction.\n");

    assert(!completed_inst->isSquashed());

    // Tell the memory dependence unit to wake any dependents on this
    // instruction if it is a memory instruction.  Also complete the memory
    // instruction at this point since we know it executed without issues.
    ThreadID tid = completed_inst->threadNumber;
    if (completed_inst->isMemRef()) {
        memDepUnit[tid].completeInst(completed_inst);

        DPRINTF(IQ, "Completing mem instruction PC: %s [sn:%llu]\n",
            completed_inst->pcState(), completed_inst->seqNum);

        ++freeEntries;
        completed_inst->memOpDone(true);
        count[tid]--;
    } else if (completed_inst->isReadBarrier() ||
               completed_inst->isWriteBarrier()) {
        // Completes a non mem ref barrier
        memDepUnit[tid].completeInst(completed_inst);
    }

    for (int dest_reg_idx = 0;
         dest_reg_idx < completed_inst->numDestRegs();
         dest_reg_idx++)
    {
        PhysRegIdPtr dest_reg =
            completed_inst->renamedDestRegIdx(dest_reg_idx);

        // Special case of uniq or control registers.  They are not
        // handled by the IQ and thus have no dependency graph entry.
        if (dest_reg->isFixedMapping()) {
            DPRINTF(IQ, "Reg %d [%s] is part of a fix mapping, skipping\n",
                    dest_reg->index(), dest_reg->className());
            continue;
        }

        // Avoid waking up dependents if the register is pinned
        dest_reg->decrNumPinnedWritesToComplete();
        if (dest_reg->isPinned())
            completed_inst->setPinnedRegsWritten();

        if (dest_reg->getNumPinnedWritesToComplete() != 0) {
            DPRINTF(IQ, "Reg %d [%s] is pinned, skipping\n",
                    dest_reg->index(), dest_reg->className());
            continue;
        }

        DPRINTF(IQ, "Waking any dependents on register %i (%s).\n",
                dest_reg->index(),
                dest_reg->className());

        //Go through the dependency chain, marking the registers as
        //ready within the waiting instructions.
        DynInstPtr dep_inst = dependGraph.pop(dest_reg->flatIndex());

        while (dep_inst) {
            DPRINTF(IQ, "Waking up a dependent instruction, [sn:%llu] "
                    "PC %s.\n", dep_inst->seqNum, dep_inst->pcState());

            // Might want to give more information to the instruction
            // so that it knows which of its source registers is
            // ready.  However that would mean that the dependency
            // graph entries would need to hold the src_reg_idx.
            dep_inst->markSrcRegReady();

            addIfReady(dep_inst);

            dep_inst = dependGraph.pop(dest_reg->flatIndex());

            ++dependents;
        }

        // Reset the head node now that all of its dependents have
        // been woken up.
        assert(dependGraph.empty(dest_reg->flatIndex()));
        dependGraph.clearInst(dest_reg->flatIndex());

        // Mark the scoreboard as having that register ready.
        regScoreboard[dest_reg->flatIndex()] = true;
    }
    return dependents;
}

template <class Impl>
void
InstructionQueueDist<Impl>::addReadyMemInst(const DynInstPtr &ready_inst)
{
#if TRACING_ON
    OpClass op_class = ready_inst->opClass();
#endif
    int fu_idx = ready_inst->getFuIdx();

    readyInsts[fu_idx].push(ready_inst);

    // Will need to reorder the list if either a queue is not on the list,
    // or it has an older instruction than last time.
    if (!queueOnList[fu_idx]) {
        addToOrderList(fu_idx);
    } else if (readyInsts[fu_idx].top()->seqNum  <
               (*readyIt[fu_idx]).oldestInst) {
        DPRINTF(IQ, "[%s] erase order list of fu[%i]\n",
                __FUNCTION__, fu_idx);
        listOrders[fu_idx].erase(readyIt[fu_idx]);
        addToOrderList(fu_idx);
    }

    DPRINTF(IQ, "Instruction is ready to issue, putting it onto "
            "the ready list, PC %s opclass:%i [sn:%llu].\n",
            ready_inst->pcState(), op_class, ready_inst->seqNum);
}

template <class Impl>
void
InstructionQueueDist<Impl>::rescheduleMemInst(const DynInstPtr &resched_inst)
{
    DPRINTF(IQ, "Rescheduling mem inst [sn:%llu]\n", resched_inst->seqNum);

    // Reset DTB translation state
    resched_inst->translationStarted(false);
    resched_inst->translationCompleted(false);

    resched_inst->clearCanIssue();
    memDepUnit[resched_inst->threadNumber].reschedule(resched_inst);
}

template <class Impl>
void
InstructionQueueDist<Impl>::replayMemInst(const DynInstPtr &replay_inst)
{
    memDepUnit[replay_inst->threadNumber].replay();
}

template <class Impl>
void
InstructionQueueDist<Impl>::deferMemInst(const DynInstPtr &deferred_inst)
{
    deferredMemInsts.push_back(deferred_inst);
}

template <class Impl>
void
InstructionQueueDist<Impl>::blockMemInst(const DynInstPtr &blocked_inst)
{
    blocked_inst->clearIssued();
    blocked_inst->clearCanIssue();
    blockedMemInsts.push_back(blocked_inst);
}

template <class Impl>
void
InstructionQueueDist<Impl>::cacheUnblocked()
{
    retryMemInsts.splice(retryMemInsts.end(), blockedMemInsts);
    // Get the CPU ticking again
    cpu->wakeCPU();
}

template <class Impl>
typename Impl::DynInstPtr
InstructionQueueDist<Impl>::getDeferredMemInstToExecute()
{
    for (ListIt it = deferredMemInsts.begin(); it != deferredMemInsts.end();
         ++it) {
        if ((*it)->translationCompleted() || (*it)->isSquashed()) {
            DynInstPtr mem_inst = std::move(*it);
            deferredMemInsts.erase(it);
            return mem_inst;
        }
    }
    return nullptr;
}

template <class Impl>
typename Impl::DynInstPtr
InstructionQueueDist<Impl>::getBlockedMemInstToExecute()
{
    if (retryMemInsts.empty()) {
        return nullptr;
    } else {
        DynInstPtr mem_inst = std::move(retryMemInsts.front());
        retryMemInsts.pop_front();
        return mem_inst;
    }
}

template <class Impl>
void
InstructionQueueDist<Impl>::violation(const DynInstPtr &store,
                                  const DynInstPtr &faulting_load)
{
    iqIOStats.intInstQueueWrites++;
    memDepUnit[store->threadNumber].violation(store, faulting_load);
}

template <class Impl>
void
InstructionQueueDist<Impl>::squash(ThreadID tid)
{
    DPRINTF(IQ, "[tid:%i] Starting to squash instructions in "
            "the IQ.\n", tid);

    // Read instruction sequence number of last instruction out of the
    // time buffer.
    squashedSeqNum[tid] = fromCommit->commitInfo[tid].doneSeqNum;

    doSquash(tid);

    // Also tell the memory dependence unit to squash.
    memDepUnit[tid].squash(squashedSeqNum[tid], tid);
}

template <class Impl>
void
InstructionQueueDist<Impl>::doSquash(ThreadID tid)
{
    // Start at the tail.
    ListIt squash_it = instList[tid].end();
    --squash_it;

    DPRINTF(IQ, "[tid:%i] Squashing until sequence number %i!\n",
            tid, squashedSeqNum[tid]);

    // Squash any instructions younger than the squashed sequence number
    // given.
    while (squash_it != instList[tid].end() &&
           (*squash_it)->seqNum > squashedSeqNum[tid]) {

        DynInstPtr squashed_inst = (*squash_it);
        if (squashed_inst->isFloating()) {
            iqIOStats.fpInstQueueWrites++;
        } else if (squashed_inst->isVector()) {
            iqIOStats.vecInstQueueWrites++;
        } else {
            iqIOStats.intInstQueueWrites++;
        }

        // Only handle the instruction if it actually is in the IQ and
        // hasn't already been squashed in the IQ.
        if (squashed_inst->threadNumber != tid ||
            squashed_inst->isSquashedInIQ()) {
            --squash_it;
            continue;
        }

        if (!squashed_inst->isIssued() ||
            (squashed_inst->isMemRef() &&
             !squashed_inst->memOpDone())) {

            DPRINTF(IQ, "[tid:%i] Instruction [sn:%llu] PC %s squashed.\n",
                    tid, squashed_inst->seqNum, squashed_inst->pcState());

            bool is_acq_rel = squashed_inst->isFullMemBarrier() &&
                         (squashed_inst->isLoad() ||
                          (squashed_inst->isStore() &&
                             !squashed_inst->isStoreConditional()));

            // Remove the instruction from the dependency list.
            if (is_acq_rel ||
                (!squashed_inst->isNonSpeculative() &&
                 !squashed_inst->isStoreConditional() &&
                 !squashed_inst->isAtomic() &&
                 !squashed_inst->isReadBarrier() &&
                 !squashed_inst->isWriteBarrier())) {

                for (int src_reg_idx = 0;
                     src_reg_idx < squashed_inst->numSrcRegs();
                     src_reg_idx++)
                {
                    PhysRegIdPtr src_reg =
                        squashed_inst->renamedSrcRegIdx(src_reg_idx);

                    // Only remove it from the dependency graph if it
                    // was placed there in the first place.

                    // Instead of doing a linked list traversal, we
                    // can just remove these squashed instructions
                    // either at issue time, or when the register is
                    // overwritten.  The only downside to this is it
                    // leaves more room for error.

                    if (!squashed_inst->isReadySrcRegIdx(src_reg_idx) &&
                        !src_reg->isFixedMapping()) {
                        dependGraph.remove(src_reg->flatIndex(),
                                           squashed_inst);
                    }

                    ++iqStats.squashedOperandsExamined;
                }

            } else if (!squashed_inst->isStoreConditional() ||
                       !squashed_inst->isCompleted()) {
                NonSpecMapIt ns_inst_it =
                    nonSpecInsts.find(squashed_inst->seqNum);

                // we remove non-speculative instructions from
                // nonSpecInsts already when they are ready, and so we
                // cannot always expect to find them
                if (ns_inst_it == nonSpecInsts.end()) {
                    // loads that became ready but stalled on a
                    // blocked cache are alreayd removed from
                    // nonSpecInsts, and have not faulted
                    assert(squashed_inst->getFault() != NoFault ||
                           squashed_inst->isMemRef());
                } else {

                    (*ns_inst_it).second = NULL;

                    nonSpecInsts.erase(ns_inst_it);

                    ++iqStats.squashedNonSpecRemoved;
                }
            }

            // Might want to also clear out the head of the dependency graph.

            // Mark it as squashed within the IQ.
            squashed_inst->setSquashedInIQ();

            // @todo: Remove this hack where several statuses are set so the
            // inst will flow through the rest of the pipeline.
            squashed_inst->setIssued();
            squashed_inst->setCanCommit();
            squashed_inst->clearInIQ();

            //Update Thread IQ Count
            count[squashed_inst->threadNumber]--;

            ++freeEntries;
        }

        // IQ clears out the heads of the dependency graph only when
        // instructions reach writeback stage. If an instruction is squashed
        // before writeback stage, its head of dependency graph would not be
        // cleared out; it holds the instruction's DynInstPtr. This prevents
        // freeing the squashed instruction's DynInst.
        // Thus, we need to manually clear out the squashed instructions' heads
        // of dependency graph.
        for (int dest_reg_idx = 0;
             dest_reg_idx < squashed_inst->numDestRegs();
             dest_reg_idx++)
        {
            PhysRegIdPtr dest_reg =
                squashed_inst->renamedDestRegIdx(dest_reg_idx);
            if (dest_reg->isFixedMapping()){
                continue;
            }
            assert(dependGraph.empty(dest_reg->flatIndex()));
            dependGraph.clearInst(dest_reg->flatIndex());
        }
        instList[tid].erase(squash_it--);
        ++iqStats.squashedInstsExamined;
    }

    for (int fu_idx = 0; fu_idx < numFU; fu_idx++){
        InstList& iList = distInstLists[tid][fu_idx];
        squash_it = iList.end();
        --squash_it;
        while (squash_it != iList.end() &&
               (*squash_it)->seqNum > squashedSeqNum[tid]){
            iList.erase(squash_it--);
        }
    }
}

template <class Impl>
bool
InstructionQueueDist<Impl>::addToDependents(const DynInstPtr &new_inst)
{
    // Loop through the instruction's source registers, adding
    // them to the dependency list if they are not ready.
    int8_t total_src_regs = new_inst->numSrcRegs();
    bool return_val = false;

    for (int src_reg_idx = 0;
         src_reg_idx < total_src_regs;
         src_reg_idx++)
    {
        // Only add it to the dependency graph if it's not ready.
        if (!new_inst->isReadySrcRegIdx(src_reg_idx)) {
            PhysRegIdPtr src_reg = new_inst->renamedSrcRegIdx(src_reg_idx);

            // Check the IQ's scoreboard to make sure the register
            // hasn't become ready while the instruction was in flight
            // between stages.  Only if it really isn't ready should
            // it be added to the dependency graph.
            if (src_reg->isFixedMapping()) {
                continue;
            } else if (!regScoreboard[src_reg->flatIndex()]) {
                DPRINTF(IQ, "Instruction PC %s has src reg %i (%s) that "
                        "is being added to the dependency chain.\n",
                        new_inst->pcState(), src_reg->index(),
                        src_reg->className());

                dependGraph.insert(src_reg->flatIndex(), new_inst);

                // Change the return value to indicate that something
                // was added to the dependency graph.
                return_val = true;
            } else {
                DPRINTF(IQ, "Instruction PC %s has src reg %i (%s) that "
                        "became ready before it reached the IQ.\n",
                        new_inst->pcState(), src_reg->index(),
                        src_reg->className());
                // Mark a register ready within the instruction.
                new_inst->markSrcRegReady(src_reg_idx);
            }
        }
    }

    return return_val;
}

template <class Impl>
void
InstructionQueueDist<Impl>::addToProducers(const DynInstPtr &new_inst)
{
    // Nothing really needs to be marked when an instruction becomes
    // the producer of a register's value, but for convenience a ptr
    // to the producing instruction will be placed in the head node of
    // the dependency links.
    int8_t total_dest_regs = new_inst->numDestRegs();

    for (int dest_reg_idx = 0;
         dest_reg_idx < total_dest_regs;
         dest_reg_idx++)
    {
        PhysRegIdPtr dest_reg = new_inst->renamedDestRegIdx(dest_reg_idx);

        // Some registers have fixed mapping, and there is no need to track
        // dependencies as these instructions must be executed at commit.
        if (dest_reg->isFixedMapping()) {
            continue;
        }

        if (!dependGraph.empty(dest_reg->flatIndex())) {
            dependGraph.dump();
            panic("Dependency graph %i (%s) (flat: %i) not empty!",
                  dest_reg->index(), dest_reg->className(),
                  dest_reg->flatIndex());
        }

        dependGraph.setInst(dest_reg->flatIndex(), new_inst);

        // Mark the scoreboard to say it's not yet ready.
        regScoreboard[dest_reg->flatIndex()] = false;
    }
}

template <class Impl>
void
InstructionQueueDist<Impl>::addIfReady(const DynInstPtr &inst)
{
    // If the instruction now has all of its source registers
    // available, then add it to the list of ready instructions.
    if (inst->readyToIssue()) {

        //Add the instruction to the proper ready list.
        if (inst->isMemRef()) {

            DPRINTF(IQ, "Checking if memory instruction can issue.\n");

            // Message to the mem dependence unit that this instruction has
            // its registers ready.
            memDepUnit[inst->threadNumber].regsReady(inst);

            return;
        }

#if TRACING_ON
        OpClass op_class = inst->opClass();
#endif
        int fu_idx = inst->getFuIdx();

        DPRINTF(IQ, "Instruction is ready to issue, putting it onto "
                "the ready list, PC %s opclass:%i fuidx:%i [sn:%llu].\n",
                inst->pcState(), op_class, fu_idx, inst->seqNum);

        readyInsts[fu_idx].push(inst);

        // Will need to reorder the list if either a queue is not on the list,
        // or it has an older instruction than last time.
        if (!queueOnList[fu_idx]) {
            addToOrderList(fu_idx);
        } else if (readyInsts[fu_idx].top()->seqNum  <
                   (*readyIt[fu_idx]).oldestInst) {
            DPRINTF(IQ, "[%s] erase order list of fu[%i]\n",
                    __FUNCTION__, fu_idx);
            listOrders[fu_idx].erase(readyIt[fu_idx]);
            addToOrderList(fu_idx);
        }
        DPRINTF(IQ, "[%s] fu_idx:[%i] ready size:%i order size:%i\n",
                __FUNCTION__, fu_idx, readyInsts[fu_idx].size(), listOrders[fu_idx].size());
    }
}

template <class Impl>
int
InstructionQueueDist<Impl>::countInsts()
{
    return numEntries - freeEntries;
}

template <class Impl>
void
InstructionQueueDist<Impl>::dumpLists()
{
    for (int i = 0; i < numFU; ++i) {
        cprintf("Ready list %i size: %i\n", i, readyInsts[i].size());

        cprintf("\n");
    }

    cprintf("Non speculative list size: %i\n", nonSpecInsts.size());

    NonSpecMapIt non_spec_it = nonSpecInsts.begin();
    NonSpecMapIt non_spec_end_it = nonSpecInsts.end();

    cprintf("Non speculative list: ");

    while (non_spec_it != non_spec_end_it) {
        cprintf("%s [sn:%llu]", (*non_spec_it).second->pcState(),
                (*non_spec_it).second->seqNum);
        ++non_spec_it;
    }

    cprintf("\n");

    for (int fu_idx = 0; fu_idx < numFU; ++fu_idx) {
        ListOrderIt list_order_it = listOrders[fu_idx].begin();
        ListOrderIt list_order_end_it = listOrders[fu_idx].end();

        int i = 1;
        cprintf("List order (fu: %i):", fu_idx);

        while (list_order_it != list_order_end_it) {
            cprintf("%i [sn:%llu] ", i, (*list_order_it).oldestInst);

            ++list_order_it;
            ++i;
        }

        cprintf("\n");
    }

}

template <class Impl>
void
InstructionQueueDist<Impl>::dumpInsts()
{
    for (ThreadID tid = 0; tid < numThreads; ++tid) {
        int num = 0;
        int valid_num = 0;
        ListIt inst_list_it = instList[tid].begin();

        while (inst_list_it != instList[tid].end()) {
            cprintf("Instruction:%i\n", num);
            if (!(*inst_list_it)->isSquashed()) {
                if (!(*inst_list_it)->isIssued()) {
                    ++valid_num;
                    cprintf("Count:%i\n", valid_num);
                } else if ((*inst_list_it)->isMemRef() &&
                           !(*inst_list_it)->memOpDone()) {
                    // Loads that have not been marked as executed
                    // still count towards the total instructions.
                    ++valid_num;
                    cprintf("Count:%i\n", valid_num);
                }
            }

            cprintf("PC: %s\n[sn:%llu]\n[tid:%i]\n"
                    "Issued:%i\nSquashed:%i\n",
                    (*inst_list_it)->pcState(),
                    (*inst_list_it)->seqNum,
                    (*inst_list_it)->threadNumber,
                    (*inst_list_it)->isIssued(),
                    (*inst_list_it)->isSquashed());

            if ((*inst_list_it)->isMemRef()) {
                cprintf("MemOpDone:%i\n", (*inst_list_it)->memOpDone());
            }

            cprintf("\n");

            inst_list_it++;
            ++num;
        }
    }

    cprintf("Insts to Execute list:\n");

    int num = 0;
    int valid_num = 0;
    ListIt inst_list_it = instsToExecute.begin();

    while (inst_list_it != instsToExecute.end())
    {
        cprintf("Instruction:%i\n",
                num);
        if (!(*inst_list_it)->isSquashed()) {
            if (!(*inst_list_it)->isIssued()) {
                ++valid_num;
                cprintf("Count:%i\n", valid_num);
            } else if ((*inst_list_it)->isMemRef() &&
                       !(*inst_list_it)->memOpDone()) {
                // Loads that have not been marked as executed
                // still count towards the total instructions.
                ++valid_num;
                cprintf("Count:%i\n", valid_num);
            }
        }

        cprintf("PC: %s\n[sn:%llu]\n[tid:%i]\n"
                "Issued:%i\nSquashed:%i\n",
                (*inst_list_it)->pcState(),
                (*inst_list_it)->seqNum,
                (*inst_list_it)->threadNumber,
                (*inst_list_it)->isIssued(),
                (*inst_list_it)->isSquashed());

        if ((*inst_list_it)->isMemRef()) {
            cprintf("MemOpDone:%i\n", (*inst_list_it)->memOpDone());
        }

        cprintf("\n");

        inst_list_it++;
        ++num;
    }
}

#endif//__CPU_O3_INST_QUEUE_IMPL_HH__