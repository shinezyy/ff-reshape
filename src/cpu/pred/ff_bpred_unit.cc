/**
 * @file BP for forwardflow
 */

/*
 * Copyright (c) 2011-2012, 2014 ARM Limited
 * Copyright (c) 2010 The University of Edinburgh
 * Copyright (c) 2012 Mark D. Hill and David A. Wood
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

#include "cpu/pred/ff_bpred_unit.hh"

#include "debug/Branch.hh"

FFBPredUnit::FFBPredUnitStats::FFBPredUnitStats(Stats::Group *parent)
        : Stats::Group(parent, "ff_bpred_unit"),
          ADD_STAT(lookups, "Number of FF BP lookups"),
          ADD_STAT(correct, "Number of FF BP correct predictions"),
          ADD_STAT(correctRatio, "FF BP prediction correct ratio", correct / lookups)
{
    correctRatio.precision(4);
}

FFBPredUnit::FFBPredUnit(const Params &p)
    : SimObject(p),
      numThreads(p.numThreads),
      predHist(p.numThreads),
      stats(this)
{
}

Addr
FFBPredUnit::predict(const StaticInstPtr &inst, const InstSeqNum &seqNum,
                   TheISA::PCState &pc, ThreadID tid)
{
    Addr nextK_pc;
    void *bp_history = nullptr;

    ++stats.lookups;
    nextK_pc = lookup(tid, pc.instAddr(), bp_history);

    DPRINTF(Branch, "[tid:%i] [sn:%llu] "
                "Branch predictor predicted next-K PC=%#x for PC %s\n",
                tid, seqNum,  nextK_pc, pc);

    DPRINTF(Branch, "[tid:%i] [sn:%llu] "
                "Creating prediction history "
                "for PC %s\n", tid, seqNum, pc);

    PredictorHistory predict_record(seqNum, pc.instAddr(), bp_history,
                                    nextK_pc, tid, inst);
    predHist[tid].push_front(predict_record);

    return nextK_pc;
}

void
FFBPredUnit::update(const InstSeqNum &done_sn, ThreadID tid)
{
    DPRINTF(Branch, "[tid:%i] Committing branches until "
            "sn:%llu]\n", tid, done_sn);

    while (!predHist[tid].empty() &&
           predHist[tid].back().seqNum <= done_sn) {
        // Update the branch predictor with the correct results.
        DPRINTF(Branch, "Committing branch [sn:%lli].\n",
                predHist[tid].back().seqNum);
        update(tid, predHist[tid].back().pc,
                    predHist[tid].back().bpHistory, false,
                    predHist[tid].back().inst,
                    predHist[tid].back().nextK_PC);

        predHist[tid].pop_back();
    }
}

void
FFBPredUnit::squash(const InstSeqNum &squashed_sn, ThreadID tid)
{
    History &pred_hist = predHist[tid];

    while (!pred_hist.empty() &&
           pred_hist.front().seqNum > squashed_sn) {
        // This call should delete the bpHistory.
        squash(tid, pred_hist.front().bpHistory);

        DPRINTF(Branch, "[tid:%i] [squash sn:%llu] "
                "Removing history for [sn:%llu] "
                "PC %#x\n", tid, squashed_sn, pred_hist.front().seqNum,
                pred_hist.front().pc);

        pred_hist.pop_front();

        DPRINTF(Branch, "[tid:%i] [squash sn:%llu] predHist.size(): %i\n",
                tid, squashed_sn, predHist[tid].size());
    }
}

void
FFBPredUnit::squash(const InstSeqNum &squashed_sn,
                  const TheISA::PCState &corr_nextK_PC,
                  ThreadID tid)
{
    // Now that we know that a branch was mispredicted, we need to undo
    // all the branches that have been seen up until this branch and
    // fix up everything.
    // NOTE: This should be call conceivably in 2 scenarios:
    // (1) After an branch is executed, it updates its status in the ROB
    //     The commit stage then checks the ROB update and sends a signal to
    //     the fetch stage to squash history after the mispredict
    // (2) In the decode stage, you can find out early if a unconditional
    //     PC-relative, branch was predicted incorrectly. If so, a signal
    //     to the fetch stage is sent to squash history after the mispredict

    History &pred_hist = predHist[tid];

    ++stats.correct;

    DPRINTF(Branch, "[tid:%i] Squashing from sequence number %i, "
            "setting correct next-K PC to %s\n", tid, squashed_sn, corr_nextK_PC);

    // Squash All Branches AFTER this mispredicted branch
    squash(squashed_sn, tid);

    // If there's a squash due to a syscall, there may not be an entry
    // corresponding to the squash.  In that case, don't bother trying to
    // fix up the entry.
    if (!pred_hist.empty()) {

        auto hist_it = pred_hist.begin();
        //HistoryIt hist_it = find(pred_hist.begin(), pred_hist.end(),
        //                       squashed_sn);

        //assert(hist_it != pred_hist.end());
        if (pred_hist.front().seqNum != squashed_sn) {
            DPRINTF(Branch, "Front sn %i != Squash sn %i\n",
                    pred_hist.front().seqNum, squashed_sn);

            assert(pred_hist.front().seqNum == squashed_sn);
        }


        // There are separate functions for in-order and out-of-order
        // branch prediction, but not for update. Therefore, this
        // call should take into account that the mispredicted branch may
        // be on the wrong path (i.e., OoO execution), and that the counter
        // counter table(s) should not be updated. Thus, this call should
        // restore the state of the underlying predictor, for instance the
        // local/global histories. The counter tables will be updated when
        // the branch actually commits.

        // Remember the correct direction for the update at commit.
        pred_hist.front().nextK_PC = corr_nextK_PC.instAddr();

        update(tid, (*hist_it).pc,
               pred_hist.front().bpHistory, true, pred_hist.front().inst,
               corr_nextK_PC.instAddr());

    } else {
        DPRINTF(Branch, "[tid:%i] [sn:%llu] pred_hist empty, can't "
                "update\n", tid, squashed_sn);
    }
}

void
FFBPredUnit::dump()
{
    int i = 0;
    for (const auto& ph : predHist) {
        if (!ph.empty()) {
            auto pred_hist_it = ph.begin();

            cprintf("predHist[%i].size(): %i\n", i++, ph.size());

            while (pred_hist_it != ph.end()) {
                cprintf("sn:%llu], PC:%#x, tid:%i, next-K PC:%#x, "
                        "\n",
                        pred_hist_it->seqNum, pred_hist_it->pc,
                        pred_hist_it->tid, pred_hist_it->nextK_PC);
                pred_hist_it++;
            }

            cprintf("\n");
        }
    }
}

void
FFBPredUnit::drainSanityCheck() const
{
    // We shouldn't have any outstanding requests when we resume from
    // a drained system.
    for (M5_VAR_USED const auto& ph : predHist)
        assert(ph.empty());
}

