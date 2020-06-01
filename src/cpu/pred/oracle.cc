/*
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

#include "cpu/pred/oracle.hh"

#include <boost/tokenizer.hpp>

#include "base/intmath.hh"
#include "base/logging.hh"
#include "base/trace.hh"
#include "debug/Fetch.hh"

OracleBP::OracleBP(const OracleBPParams *params)
    : BPredUnit(params),
    branchTraceFile(params->outcomePath),
    trace(branchTraceFile)
{
    DPRINTF(Fetch, "instruction shift amount: %i\n",
            instShiftAmt);
}

void
OracleBP::reset()
{
}

void
OracleBP::btbUpdate(ThreadID tid, Addr branch_addr, void * &bp_history)
{
// Place holder for a function that is called to update predictor history when
// a BTB entry is invalid or not found.
}


bool
OracleBP::lookup(ThreadID tid, Addr branch_addr, void * &bp_history)
{
    bool taken = getNextOutcome(branch_addr, true);
    return taken;
}

void
OracleBP::update(ThreadID tid, Addr branch_addr, bool taken, void *bp_history,
                bool squashed)
{
}

void
OracleBP::uncondBranch(ThreadID tid, Addr pc, void *&bp_history)
{
    getNextOutcome(pc, false);
}

bool
OracleBP::getNextOutcome(Addr pc, bool isConditional)
{
    if (!directionValid || !isConditional) {
        if (!trace.read(branchInfo)) {
            panic("Failed to obtain branch info from %s\n", branchTraceFile);
        }
        directionValid = isConditional;
        addrValid = true;
    }
    if (pc == branchInfo.branch_addr()) {
        inform("pc == pc_recorded, pc: 0x%lx, pc_recorded: 0x%lx\n",
               pc, branchInfo.branch_addr());

        directionValid = false;
        return branchInfo.taken();

    } else {
        inform("pc != pc_recorded, pc: 0x%lx, pc_recorded: 0x%lx\n",
               pc, branchInfo.branch_addr());

        return false;
    }
}

Addr OracleBP::getOracleAddr() {
    assert(!directionValid);
    assert(addrValid);
    addrValid = false;
    return branchInfo.target_addr();
}

OracleBP*
OracleBPParams::create()
{
    return new OracleBP(this);
}
