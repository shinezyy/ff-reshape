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

#include "debug/FFTrivialBP.hh"
#include "ff_trivial.hh"

FFTrivialBP::FFTrivialBP(const FFTrivialBPParams &params)
        : FFBPredUnit(params),
          tage(params.tage),
          numLookAhead(params.numLookAhead)
{
}

Addr FFTrivialBP::lookup(ThreadID tid, Addr instPC, bool isControl, void * &bp_history) {
    auto bi = new BPState(*tage);
    bp_history = bi;

    Addr pc = instPC;

    for (int i=0; i<numLookAhead; i++) {
        bool taken = tage->tagePredict(tid, pc, isControl, bi->info);

        tage->updateHistories(tid, pc, taken, bi->info, true);

        // TODO: add BTB
        if (taken) {
            pc = 0;
        } else {
            pc = 0;
        }
    }

    return pc;
}

void FFTrivialBP::update(ThreadID tid, const TheISA::PCState &thisPC,
                void *bp_history, bool squashed,
                const StaticInstPtr &inst,
                const TheISA::PCState &pred_DBB, const TheISA::PCState &corr_DBB) {

    // TODO
}

void FFTrivialBP::squash(ThreadID tid, void *bp_history) {
    auto bi = static_cast<BPState *>(bp_history);

    // TODO

    delete bi;
}
