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

#ifndef __CPU_FF_MEM_DEP_UNIT_HH__
#define __CPU_FF_MEM_DEP_UNIT_HH__

#include <list>
#include <memory>
#include <set>
#include <unordered_map>

#ifdef __CLION_CODING__
#include "cpu/ff_base_dyn_inst.hh"
#include "cpu/forwardflow/dataflow_queue_top.hh"
#include "cpu/forwardflow/dyn_inst.hh"
#include "cpu/forwardflow/lsq.hh"

#endif


#include "base/statistics.hh"
#include "cpu/forwardflow/dq_pointer.hh"
#include "cpu/inst_seq.hh"
#include "debug/MemDepUnit.hh"

struct DerivFFCPUParams;

namespace FF{

struct SNHash {
    size_t operator() (const InstSeqNum &seq_num) const {
        unsigned a = (unsigned)seq_num;
        unsigned hash = (((a >> 14) ^ ((a >> 2) & 0xffff))) & 0x7FFFFFFF;

        return hash;
    }
};

/**
 * Memory dependency unit class.  This holds the memory dependence predictor.
 * As memory operations are issued to the IQ, they are also issued to this
 * unit, which then looks up the prediction as to what they are dependent
 * upon.  This unit must be checked prior to a memory operation being able
 * to issue.  Although this is templated, it's somewhat hard to make a generic
 * memory dependence unit.  This one is mostly for store sets; it will be
 * quite limited in what other memory dependence predictions it can also
 * utilize.  Thus this class should be most likely be rewritten for other
 * dependence prediction schemes.
 */
template <class MemDepPred, class Impl>
class MemDepUnit
{
  protected:
    std::string _name;

  public:

#ifdef __CLION_CODING__
    template<class Impl>
    class FullInst: public BaseDynInst<Impl>, public BaseO3DynInst<Impl> {
    };

    using DynInstPtr = FullInst<Impl>*;

    using InstructionQueue = DQTop<Impl>;
#else
    typedef typename Impl::DynInstPtr DynInstPtr;

    typedef typename Impl::CPUPol::DQTop InstructionQueue;
#endif


    /** Empty constructor. Must call init() prior to using in this case. */
    MemDepUnit();

    /** Constructs a MemDepUnit with given parameters. */
    MemDepUnit(DerivFFCPUParams *params);

    /** Frees up any memory allocated. */
    ~MemDepUnit();

    /** Returns the name of the memory dependence unit. */
    std::string name() const { return _name; }

    /** Initializes the unit with parameters and a thread id. */
    void init(DerivFFCPUParams *params, ThreadID tid);

    /** Registers statistics. */
    void regStats();

    /** Determine if we are drained. */
    bool isDrained() const;

    /** Perform sanity checks after a drain. */
    void drainSanityCheck() const;

    /** Takes over from another CPU's thread. */
    void takeOverFrom();

    /** Sets the pointer to the IQ. */
    void setIQ(InstructionQueue *iq_ptr);

    /** Inserts a memory instruction. */
    PointerPair insert(DynInstPtr &inst);

    /** Inserts a non-speculative memory instruction. */
    void insertNonSpec(DynInstPtr &inst);

    /** Inserts a barrier instruction. */
    void insertBarrier(DynInstPtr &barr_inst);


    /** Reschedules an instruction to be re-executed. */
    void reschedule(DynInstPtr &inst);

    /** Replays all instructions that have been rescheduled by moving them to
     *  the ready list.
     */
    void replay();

    /** Completes a barrier instruction. */
    void completeBarrier(DynInstPtr &inst);

    /** Squashes all instructions up until a given sequence number for a
     *  specific thread.
     */
    void squash(const InstSeqNum &squashed_num, ThreadID tid);

  private:
    typedef typename std::list<DynInstPtr>::iterator ListIt;

    class MemDepEntry;

    typedef std::shared_ptr<MemDepEntry> MemDepEntryPtr;

    /** Memory dependence entries that track memory operations, marking
     *  when the instruction is ready to execute and what instructions depend
     *  upon it.
     */
    class MemDepEntry {
      public:
        /** Constructs a memory dependence entry. */
        MemDepEntry(DynInstPtr &new_inst)
            : inst(new_inst), regsReady(false), memDepReady(false),
              completed(false), squashed(false)
        {
            if (inst->isStore() || inst->isMemBarrier() || inst->isWriteBarrier()) {
                latestPosition = new_inst->dqPosition;
            } else {
                latestPosition.valid = false;
            }
#ifdef DEBUG
            ++memdep_count;

            DPRINTF(MemDepUnit, "Memory dependency entry created.  "
                    "memdep_count=%i %s\n", memdep_count, inst->pcState());
#endif
        }

        /** Frees any pointers. */
        ~MemDepEntry()
        {
            for (int i = 0; i < dependInsts.size(); ++i) {
                dependInsts[i] = NULL;
            }
#ifdef DEBUG
            --memdep_count;

            DPRINTF(MemDepUnit, "Memory dependency entry deleted.  "
                    "memdep_count=%i %s\n", memdep_count, inst->pcState());
#endif
        }

        /** Returns the name of the memory dependence entry. */
        std::string name() const { return "memdepentry"; }

        /** The instruction being tracked. */
        DynInstPtr inst;

        /** The iterator to the instruction's location inside the list. */
        ListIt listIt;

        /** A vector of any dependent instructions. */
        std::vector<MemDepEntryPtr> dependInsts;

        /** If the registers are ready or not. */
        bool regsReady;
        /** If all memory dependencies have been satisfied. */
        bool memDepReady;
        /** If the instruction is completed. */
        bool completed;
        /** If the instruction is squashed. */
        bool squashed;

        bool positionInvalid{false};

        TermedPointer latestPosition;

        /** For debugging. */
#ifdef DEBUG
        static int memdep_count;
        static int memdep_insert;
        static int memdep_erase;
#endif
    };

    /** Moves an entry to the ready list. */
    inline void moveToReady(DynInstPtr &ready_inst_entry);

    typedef std::unordered_map<InstSeqNum, MemDepEntryPtr, SNHash> MemDepHash;

    typedef typename MemDepHash::iterator MemDepHashIt;

    MemDepHash barrierTable;

    /** A list of all instructions in the memory dependence unit. */

    /** A list of all instructions that are going to be replayed. */
    std::list<DynInstPtr> instsToReplay;

    /** The memory dependence predictor.  It is accessed upon new
     *  instructions being added to the IQ, and responds by telling
     *  this unit what instruction the newly added instruction is dependent
     *  upon.
     */
    struct BarrierInfo {
        bool valid;
        InstSeqNum SN;
        DQPointer position;
    };

    BarrierInfo loadBarrier, storeBarrier;

    /** Pointer to the IQ. */
    InstructionQueue *iqPtr;

    /** The thread id of this memory dependence unit. */
    int id;

  private:
    void checkAndSquashBarrier(BarrierInfo &info);
};

}

#endif // __CPU_FF_MEM_DEP_UNIT_HH__
