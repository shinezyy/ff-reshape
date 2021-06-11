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

#ifndef __CPU_PRED_BPRED_UNIT_HH__
#define __CPU_PRED_BPRED_UNIT_HH__

#include <deque>

#include <boost/dynamic_bitset.hpp>

#include "base/statistics.hh"
#include "base/types.hh"
#include "cpu/inst_seq.hh"
#include "cpu/pred/dir/direction_pred.hh"
#include "cpu/pred/itgt/indirect.hh"
#include "cpu/pred/loop_info.hh"
#include "cpu/pred/ras.hh"
#include "cpu/pred/tgt/direct_target_pred.hh"
#include "cpu/static_inst.hh"
#include "params/BranchPredictor.hh"
#include "sim/probe/pmu.hh"
#include "sim/sim_object.hh"

/**
 * Basically a wrapper class to hold both the branch predictor
 * and the BTB.
 */
class BPredUnit : public SimObject
{
  public:
      typedef BranchPredictorParams Params;
    /**
     * @param params The params object, that has the size of the BP and BTB.
     */
    BPredUnit(const Params &p);

    void regProbePoints() override;

    /** Perform sanity checks after a drain. */
    void drainSanityCheck() const;

    /**
     * Predicts whether or not the instruction is a taken branch, and the
     * target of the branch if it is taken.
     * @param inst The branch instruction.
     * @param PC The predicted PC is passed back through this parameter.
     * @param tid The thread id.
     * @return Returns if the branch is taken or not.
     */
    bool predict(const StaticInstPtr &inst, const InstSeqNum &seqNum,
                 TheISA::PCState &pc, ThreadID tid);

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
     * @param corr_target The correct branch target.
     * @param actually_taken The correct branch direction.
     * @param tid The thread id.
     */
    void squash(const InstSeqNum &squashed_sn,
                const TheISA::PCState &corr_target,
                bool actually_taken, ThreadID tid);

    virtual Addr getLastCallsite(ThreadID tid);

    boost::dynamic_bitset<> getCurrentGHR(ThreadID tid);

    void dump();

    virtual bool canPredictLoop() {
        return false;
    }

    virtual std::unique_ptr<LoopInfo> moveLastLoopInfo();

  private:
    struct PredictorHistory {
        /**
         * Makes a predictor history struct that contains any
         * information needed to update the predictor, BTB, and RAS.
         */
        PredictorHistory(const InstSeqNum &seq_num, Addr instPC,
                         bool pred_taken, void *bp_history,
                         void *indirect_history, ThreadID _tid,
                         const StaticInstPtr & inst)
            : seqNum(seq_num), pc(instPC), bpHistory(bp_history),
              indirectHistory(indirect_history), RASTarget(0), RASIndex(0),
              tid(_tid), predTaken(pred_taken), usedRAS(0), pushedRAS(0),
              wasCall(0), wasReturn(0), wasIndirect(0), target(MaxAddr),
              inst(inst)
        {}

        bool operator==(const PredictorHistory &entry) const {
            return this->seqNum == entry.seqNum;
        }

        /** The sequence number for the predictor history entry. */
        InstSeqNum seqNum;

        /** The PC associated with the sequence number. */
        Addr pc;

        /** Pointer to the history object passed back from the branch
         * predictor.  It is used to update or restore state of the
         * branch predictor.
         */
        void *bpHistory;

        void *indirectHistory;

        /** The RAS target (only valid if a return). */
        TheISA::PCState RASTarget;

        /** The RAS index of the instruction (only valid if a call). */
        unsigned RASIndex;

        /** The thread id. */
        ThreadID tid;

        /** Whether or not it was predicted taken. */
        bool predTaken;

        /** Whether or not the RAS was used. */
        bool usedRAS;

        /* Whether or not the RAS was pushed */
        bool pushedRAS;

        /** Whether or not the instruction was a call. */
        bool wasCall;

        /** Whether or not the instruction was a return. */
        bool wasReturn;

        /** Wether this instruction was an indirect branch */
        bool wasIndirect;

        /** Target of the branch. First it is predicted, and fixed later
         *  if necessary
         */
        Addr target;

        /** The branch instrction */
        const StaticInstPtr inst;
    };

    typedef std::deque<PredictorHistory> History;

    /** Number of the threads for which the branch history is maintained. */
    const unsigned numThreads;


    /**
     * The per-thread predictor history. This is used to update the predictor
     * as instructions are committed, or restore it to the proper state after
     * a squash.
     */
    std::vector<History> predHist;

    /** The branch direction predictor */
    DirectionPredictor * dirPred;

    /** The direct target predictor, usually a BTB */
    // DirectTargetPredictor * tPred;

    /** The BTB. */
    DirectTargetPredictor * tgtPred;

    /** The per-thread return address stack. */
    std::vector<ReturnAddrStack> RAS;

    /** The indirect target predictor. */
    IndirectPredictor * itPred;

    /** Whether this predictor only use pc to predict. */
    bool useInstInfo;

    struct BPredUnitStats : public Stats::Group {
        BPredUnitStats(Stats::Group *parent);

        /** Stat for number of BP lookups. */
        Stats::Scalar lookups;
        /** Stat for number of conditional branches predicted. */
        Stats::Scalar condPredicted;
        /** Stat for number of conditional branches predicted incorrectly. */
        Stats::Scalar condIncorrect;
        /** Stat for number of BTB lookups. */
        Stats::Scalar BTBLookups;
        /** Stat for number of BTB hits. */
        Stats::Scalar BTBHits;
        /** Stat for percent times an entry in BTB found. */
        Stats::Formula BTBHitPct;
        /** Stat for number of times the RAS is used to get a target. */
        Stats::Scalar RASUsed;
        /** Stat for number of times the RAS is incorrect. */
        Stats::Scalar RASIncorrect;

        /** Stat for the number of indirect target lookups.*/
        Stats::Scalar indirectLookups;
        /** Stat for the number of indirect target hits.*/
        Stats::Scalar indirectHits;
        /** Stat for the number of indirect target misses.*/
        Stats::Scalar indirectMisses;
        /** Stat for the number of indirect target mispredictions.*/
        Stats::Scalar indirectMispredicted;
    } stats;

  protected:
    /** Number of bits to shift instructions by for predictor addresses. */
    const unsigned instShiftAmt;

    /**
     * @{
     * @name PMU Probe points.
     */

    /**
     * Helper method to instantiate probe points belonging to this
     * object.
     *
     * @param name Name of the probe point.
     * @return A unique_ptr to the new probe point.
     */
    ProbePoints::PMUUPtr pmuProbePoint(const char *name);


    /**
     * Branches seen by the branch predictor
     *
     * @note This counter includes speculative branches.
     */
    ProbePoints::PMUUPtr ppBranches;

    /** Miss-predicted branches */
    ProbePoints::PMUUPtr ppMisses;

    /** @} */
};

#endif // __CPU_PRED_BPRED_UNIT_HH__
