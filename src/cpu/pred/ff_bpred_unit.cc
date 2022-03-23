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
#include "debug/FFOracleBP.hh"

FFBPredUnit::FFBPredUnitStats::FFBPredUnitStats(Stats::Group *parent)
        : Stats::Group(parent, "ff_bpred_unit"),
          ADD_STAT(lookups, "Number of FF BP lookups"),
          ADD_STAT(incorrect, "Number of FF BP incorrect predictions"),
          ADD_STAT(squashed, "Number of FF BP squashed predictions"),
          ADD_STAT(correctRatio, "FF BP prediction correct ratio",
                1 - incorrect / lookups)
{
    correctRatio.precision(4);
}

FFBPredUnit::FFBPredUnit(const Params &p)
    : SimObject(p),
      numThreads(p.numThreads),
      numLookAhead(p.numLookAhead),
      stats(this),
      predDBB(p.predDBB)
{
}

Addr
FFBPredUnit::predict(const StaticInstPtr &inst,
                   const TheISA::PCState &pc, Info *&bp_info, ThreadID tid)
{
    Addr nextK_pc;
    void *bp_history = nullptr;

    ++stats.lookups;
    nextK_pc = lookup(tid, pc, inst, bp_history);

    DPRINTF(Branch, "[tid:%i] Branch predictor predicted next-K PC=%#x for PC %s\n",
                tid,  nextK_pc, pc);

    DPRINTF(Branch, "[tid:%i] Creating prediction history "
                "for PC %s\n", tid, pc);

    bp_info = new Info(pc, bp_history,
                        nextK_pc, tid, inst);
    return nextK_pc;
}

void
FFBPredUnit::update(Info *bp_info, Addr npc, Addr cpc, bool squashed)
{
    DPRINTF(Branch, "[tid:%i] Committing branch\n", bp_info->tid);

    bp_info->pc.npc(npc);

    update(bp_info->tid, bp_info->pc,
                bp_info->bpHistory, squashed,
                bp_info->inst,
                bp_info->predPC, cpc);

    if (bp_info->predPC != cpc) {
        ++stats.incorrect;
    }
}

void
FFBPredUnit::squash(Info *bp_info)
{
    // This call should delete the bpHistory.
    ++stats.squashed;
    squash(bp_info->tid, bp_info->bpHistory);

    DPRINTF(Branch, "[tid:%i] Removing history "
            "PC %#x\n", bp_info->tid, bp_info->pc);
}

void
FFBPredUnit::drainSanityCheck() const
{
}
