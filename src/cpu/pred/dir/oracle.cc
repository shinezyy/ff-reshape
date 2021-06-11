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

#include "cpu/pred/dir/oracle.hh"

#include <boost/tokenizer.hpp>

#include "base/intmath.hh"
#include "base/logging.hh"
#include "base/trace.hh"
#include "debug/Fetch.hh"
#include "debug/OracleBP.hh"

OracleBP::OracleBP(const OracleBPParams *params)
    : DirectionPredictor(*params),
    branchTraceFile(params->outcomePath),
    trace(branchTraceFile)
{
    DPRINTF(Fetch, "instruction shift amount: %i\n",
            instShiftAmt);
    state.commit_bid = -1;
    state.front_bid = -1;
    reset();
}

void
OracleBP::reset()
{
    frontPointer = orderedOracleEntries.begin();
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
    auto hist = new BPState(state);
    bp_history = hist;

    DPRINTF(OracleBP,
            "Before direction prediction, front_bid = %u, commit_bid = %u\n",
            state.front_bid, state.commit_bid);

    advanceFront();
    syncFront();
    bool taken = getFrontDirection();


    hist->predTaken = taken;

    DPRINTF(OracleBP,
            "After direction prediction, front_bid = %u, commit_bid = %u\n",
            state.front_bid, state.commit_bid);
    return taken;
}

void
OracleBP::update(ThreadID tid, Addr branch_addr, bool taken, void *bp_history,
                bool squashed, const StaticInstPtr &inst, Addr corrTarget)
{
    auto history_state = static_cast<BPState *>(bp_history);

    if (squashed) {
        // do nothing
    } else {
        DPRINTF(OracleBP, "Committing bid %u\n", history_state->front_bid + 1);
        DPRINTF(OracleBP, "Back bid = %u\n",
                orderedOracleEntries.back().branchID);

        assert(history_state->front_bid + 1 == orderedOracleEntries.back().branchID);

        assert(!orderedOracleEntries.empty());
        orderedOracleEntries.pop_back();
        frontPointer = orderedOracleEntries.begin();

        DPRINTF(OracleBP, "Oracle table size: %lu.\n", orderedOracleEntries.size());

        state.commit_bid++;
        dumpState();

        if (taken != history_state->predTaken) {
            inform("Predicted direction is overridden unexpectedly! @0x%x:"
                    "pred: %i, executed: %i\n",
                    branch_addr,
                    history_state->predTaken, taken);
            DPRINTF(OracleBP, "Predicted direction is overridden unexpectedly! @0x%x:"
                    "pred: %i, executed: %i\n",
                    branch_addr,
                    history_state->predTaken, taken);
        }

        delete history_state;
    }
}

void
OracleBP::uncond(ThreadID tid, Addr pc, void *&bp_history)
{
    auto history_state = new BPState(state);
    bp_history = history_state;

    DPRINTF(OracleBP,
            "Before uncond notification, front_bid = %u, commit_bid = %u\n",
            state.front_bid, state.commit_bid);

    history_state->predTaken = true;
    advanceFront();
    syncFront();

    DPRINTF(OracleBP,
            "After uncond notification, front_bid = %u, commit_bid = %u\n",
            state.front_bid, state.commit_bid);
}


Addr OracleBP::getOracleAddr() {
    return getFrontTarget();
}

void OracleBP::syncFront() {
    DPRINTF(OracleBP, "Oracle table size: %lu.\n", orderedOracleEntries.size());

    if (orderedOracleEntries.size() == 0) {
        readNextBranch();
        frontPointer = orderedOracleEntries.begin();
        dumpState();

    } else if (state.front_bid > orderedOracleEntries.front().branchID) {
        // younger, must read from file
        readNextBranch();
        assert(orderedOracleEntries.size() < 3000);
        DPRINTF(OracleBP,
                "state.front_bid = %u, orderedOracleEntries.front().branchID = %u\n",
                state.front_bid, orderedOracleEntries.front().branchID);
        assert(state.front_bid == orderedOracleEntries.front().branchID);
        frontPointer = orderedOracleEntries.begin();
        dumpState();

    } else {
        if (frontPointer == orderedOracleEntries.end() ||
                state.front_bid != frontPointer->branchID) {

            frontPointer = orderedOracleEntries.begin();

            while (frontPointer != orderedOracleEntries.end() &&
                    state.front_bid != frontPointer->branchID) {
                frontPointer++;
            }

            dumpState();

            assert(frontPointer != orderedOracleEntries.end());
        }
    }
}

bool OracleBP::getFrontDirection() {
    DPRINTF(OracleBP, "Current front direction: %i, front branch addr: 0x%x, "
            "front target addr: 0x%x\n",
            frontPointer->taken,
            frontPointer->branchAddr,
            frontPointer->targetAddr
           );
    return frontPointer->taken;
}

void OracleBP::advanceFront() {
    state.front_bid++;
}

void OracleBP::readNextBranch() {
    ProtoMessage::BranchInfo branch_info;
    if (!trace.read(branch_info)) {
        panic("Failed to obtain branch info from %s\n", branchTraceFile);
    }
    orderedOracleEntries.push_front(
            {(int32_t) branch_info.branch_id(),
             branch_info.taken(),
             branch_info.branch_addr(),
             branch_info.target_addr()});
}

Addr OracleBP::getFrontTarget() {
    DPRINTF(OracleBP, "Current front direction: %i, front branch addr: 0x%x, "
            "front target addr: 0x%x\n",
            frontPointer->taken,
            frontPointer->branchAddr,
            frontPointer->targetAddr
           );
    return frontPointer->targetAddr;
}

void OracleBP::squash(ThreadID tid, void *bp_history) {
    auto history_state = static_cast<BPState *>(bp_history);
    if (history_state->front_bid < state.front_bid) {
        state.front_bid = history_state->front_bid;

        DPRINTF(OracleBP,
                "After squash\n");
        dumpState();

        for (frontPointer = orderedOracleEntries.begin();
                frontPointer != orderedOracleEntries.end();
                frontPointer++) {
            if (frontPointer->branchID == state.front_bid) {
                break;
            }
        }
        if (frontPointer == orderedOracleEntries.end()) {
            assert(orderedOracleEntries.empty() ||
                    orderedOracleEntries.back().branchID == state.front_bid + 1 ||
                    state.front_bid == -1);
        }
        dumpState();
    } else {
        DPRINTF(OracleBP, "recorded fbid = %u, state.fbid = %u\n",
                history_state->front_bid, state.front_bid);
    }
    delete history_state;
}

bool OracleBP::getLastDirection() {
    return getFrontDirection();
}

void OracleBP::dumpState() {
    if (orderedOracleEntries.size() > 0) {
        DPRINTF(OracleBP, "cbid: %u, fbid: %u, frontPointer bid: %u, "
                "table back: %u, table front: %u\n",
                state.commit_bid, state.front_bid, frontPointer->branchID,
                orderedOracleEntries.back().branchID,
                orderedOracleEntries.front().branchID
               );
    } else {
        DPRINTF(OracleBP, "cbid: %u, fbid: %u, frontPointer bid: %u\n",
                state.commit_bid, state.front_bid, frontPointer->branchID
               );
    }
}

OracleBP*
OracleBPParams::create() const
{
    return new OracleBP(this);
}
