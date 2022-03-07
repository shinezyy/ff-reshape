/*
 * Copyright (c) 2014 The University of Wisconsin
 *
 * Copyright (c) 2006 INRIA (Institut National de Recherche en
 * Informatique et en Automatique  / French National Research Institute
 * for Computer Science and Applied Mathematics)
 *
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

#include "arch/utility.hh"
#include "base/random.hh"
#include "config/the_isa.hh"
#include "debug/FFTrivialBP.hh"
#include "ff_trivial.hh"

FFTrivialBP::FFTrivialBP(const FFTrivialBPParams &params)
        : FFBPredUnit(params),
          tage(params.tage),
          numLookAhead(params.numLookAhead),
          BTB(params.BTBEntries,
              params.BTBTagSize,
              params.instShiftAmt,
              params.numThreads)
{
}

Addr FFTrivialBP::lookup(ThreadID tid, const TheISA::PCState &instPC, const StaticInstPtr &inst, void * &bp_history) {
    BPState tstate(*tage);
    BPState *bi[2] = {new BPState(*tage), &tstate};
    int ind = 0;

    TheISA::PCState pc(instPC);

    for (int i=0; i<numLookAhead; i++) {
        // predicate direction
        bool taken = tage->tagePredict(tid, pc.pc(), true, bi[ind]->info);

        tage->updateHistories(tid, pc.pc(), taken, bi[ind]->info, true);

        // predicate target
        if (taken) {
            if (BTB.valid(pc.pc(), tid)) {
                pc = BTB.lookup(pc.pc(), tid);
            } else {
                taken = false;
            }
        }
        if (!taken) {
            TheISA::advancePC(pc, inst);
        }

        ind = 1;
    }
    bp_history = bi[0];

    return pc.pc();
}

void FFTrivialBP::update(ThreadID tid, const TheISA::PCState &pc,
                         void *bp_history, bool squashed,
                         const StaticInstPtr &inst,
                         const TheISA::PCState &pred_DBB, const TheISA::PCState &corr_DBB) {

    BPState *bi = static_cast<BPState*>(bp_history);
    TAGEBase::BranchInfo *tage_bi = bi->info;
    bool taken = pc.branching();

    if (squashed) {
        // This restores the global history, then update it
        // and recomputes the folded histories.
        tage->squash(tid, taken, tage_bi, pc.npc());
        return;
    }

    int nrand = random_mt.random<int>() & 3;
    if (tage_bi->condBranch) {
        tage->updateStats(taken, tage_bi);
        tage->condBranchUpdate(tid, pc.pc(), taken, tage_bi, nrand,
                               pc.npc(), tage_bi->tagePred);
    }

    // optional non speculative update of the histories
    tage->updateHistories(tid, pc.pc(), taken, tage_bi, false, inst,
                          pc.npc());

    BTB.update(pc.pc(), pc.npc(), tid);

    delete bi;
}

void FFTrivialBP::squash(ThreadID tid, void *bp_history) {
    auto bi = static_cast<BPState *>(bp_history);
    // Nothing to do
    delete bi;
}
