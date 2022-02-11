/*
 * Copyright (c) 2011-2012, 2014 ARM Limited
 * Copyright (c) 2010 The University of Edinburgh
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
 * Copyright (c) 2004-2005 The Regents of The University of Michigan
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

#ifndef __CPU_PRED_FF_BPRED_UNIT_H
#define __CPU_PRED_FF_BPRED_UNIT_H

#include <vector>

#include "base/statistics.hh"
#include "base/types.hh"
#include "cpu/inst_seq.hh"
#include "cpu/static_inst.hh"
#include "params/BranchPredictor.hh"
#include "params/DerivO3CPU.hh"
#include "sim/sim_object.hh"

/**
 * Basically a wrapper class to hold history of FF branch predictor.
 * Compared with BPredUnit, this class has no RAS and BTB, and
 * accepts next-K PC instead of next PC for correcting.
 */
class FFBPredUnit : public SimObject
{
  public:
      typedef FFBranchPredictorParams Params;
    FFBPredUnit(const Params &p);

    /**
     * Predicts next-K PC
     * @param inst The branch instruction.
     * @param PC The predicted PC is passed back through this parameter.
     * @param tid The thread id.
     * @return Predicted next-K PC.
     */
    Addr predict(const StaticInstPtr &inst, const InstSeqNum &seqNum,
                 const TheISA::PCState &pc, ThreadID tid);

    /**
     * Tells the branch predictor to commit any updates until the given
     * sequence number.
     * @param done_sn The sequence number to commit any older updates up until.
     * @param tid The thread id.
     */
    void update(const InstSeqNum &done_sn, ThreadID tid);

    /**
     * Squashes all outstanding updates until a given sequence number.
     * @param squashed_sn The sequence number to squash any younger updates up
     * until.
     * @param tid The thread id.
     */
    void squash(const InstSeqNum &squashed_sn, ThreadID tid);

    /**
     * Squashes all outstanding updates until a given sequence number, and
     * corrects that sn's update with the proper address and taken/not taken.
     * @param squashed_sn The sequence number to squash any younger updates up
     * until.
     * @param corr_nextK_PC The correct next-K PC.
     * @param tid The thread id.
     */
    void squash(const InstSeqNum &squashed_sn,
                const TheISA::PCState &corr_nextK_PC,
                ThreadID tid);

    void dump();

    inline unsigned getNumThreads() const { return numThreads; }
    inline unsigned getNumLookAhead() const { return numLookAhead; }

    /** Perform sanity checks after a drain. */
    void drainSanityCheck() const;

    /**
     * @param bp_history Pointer to the history object.  The predictor
     * will need to update any state and delete the object.
     */
    virtual void squash(ThreadID tid, void *bp_history) = 0;

    /**
     * Looks up a given PC in the BP to acquire next-K PC.
     * @param inst_PC The PC to look up.
     * @param bp_history Pointer that will be set to an object that
     * has the branch predictor state associated with the lookup.
     * @return next-K PC
     */
    virtual Addr lookup(ThreadID tid, Addr instPC, void * &bp_history) = 0;

    /**
     * Updates the BP with taken/not taken information.
     * @param inst_PC The branch's PC that will be updated.
     * @param bp_history Pointer to the branch predictor state that is
     * associated with the branch lookup that is being updated.
     * @param squashed Set to true when this function is called during a
     * squash operation.
     * @param inst Static instruction information
     * @param corr_nextK_PC The resolved target of the branch (only needed
     * for squashed branches)
     * @todo Make this update flexible enough to handle a global predictor.
     */
    virtual void update(ThreadID tid, Addr instPC,
                   void *bp_history, bool squashed,
                   const StaticInstPtr &inst, Addr corr_nextK_PC) = 0;

    virtual void syncArchState(Addr resetPC, uint64_t pmemAddr, void *pmemPtr, size_t pmemSize, const void *regs) {}

    virtual void initNEMU(const DerivO3CPUParams &params) {}

    virtual bool isOracle() const { return false; }

  private:
    struct PredictorHistory {
        /**
         * Makes a predictor history struct that contains any
         * information needed to update the predictor.
         */
        PredictorHistory(const InstSeqNum &seq_num, Addr instPC,
                         void *bp_history, Addr _nextK_PC, ThreadID _tid,
                         const StaticInstPtr & inst)
            : seqNum(seq_num), pc(instPC),
              tid(_tid),
              bpHistory(bp_history),
              nextK_PC(_nextK_PC),
              inst(inst)
        {}

        bool operator==(const PredictorHistory &entry) const {
            return this->seqNum == entry.seqNum;
        }

        /** The sequence number for the predictor history entry. */
        InstSeqNum seqNum;

        /** The PC associated with the sequence number. */
        Addr pc;

        /** The thread id. */
        ThreadID tid;

        void *bpHistory;

        Addr nextK_PC;

        /** The branch instrction */
        const StaticInstPtr inst;
    };

    typedef std::deque<PredictorHistory> History;

    /** Number of the threads for which the branch history is maintained. */
    const unsigned numThreads;

    const unsigned numLookAhead;

    /**
     * The per-thread predictor history. This is used to update the predictor
     * as instructions are committed, or restore it to the proper state after
     * a squash.
     */
    std::vector<History> predHist;

    struct FFBPredUnitStats : public Stats::Group
    {
        FFBPredUnitStats(Stats::Group *parent);

        Stats::Scalar lookups;

        Stats::Scalar incorrect;

        Stats::Scalar squashed;

        Stats::Formula correctRatio;
    } stats;

};

#endif // __CPU_PRED_FF_BPRED_UNIT_H
