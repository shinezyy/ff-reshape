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

#include <algorithm>

#include "arch/utility.hh"
#include "base/random.hh"
#include "config/the_isa.hh"
#include "debug/FFTrivialBP.hh"
#include "ff_trivial.hh"

FFTrivialBP::FFTrivialBPStats::FFTrivialBPStats(Stats::Group *parent)
        : Stats::Group(parent, "ff_trivial_bp"),
          ADD_STAT(icache_lookups, "Number of dummy icache lookups"),
          ADD_STAT(btb_lookups, "Number of BTB lookups"),
          ADD_STAT(icache_hits, "Number of dummy icache hits"),
          ADD_STAT(btb_hits, "Number of BTB hits"),
          ADD_STAT(icache_hit_ratio, "dummy icache hit ratio",
              icache_hits / icache_lookups),
          ADD_STAT(btb_hit_ratio, "BTB hit ratio",
              btb_hits / btb_lookups),
          ADD_STAT(mispredAffectedByCacheMiss, "mispredAffectedByCacheMiss"),
          ADD_STAT(correctAffectedByCacheMiss, "correctAffectedByCacheMiss"),
          ADD_STAT(mispredAffectedByBTBMiss, "mispredAffectedByBTBMiss"),
          ADD_STAT(correctAffectedByBTBMiss, "correctAffectedByBTBMiss"),
          ADD_STAT(indirectLookups, "Number of indirect predictor lookups."),
          ADD_STAT(indirectHits, "Number of indirect target hits."),
          ADD_STAT(indirectMisses, "Number of indirect misses."),
          ADD_STAT(indirectMispredicted, "Number of mispredicted indirect"
                " branches."),
          ADD_STAT(tageCommit, "tageCommit"),
          ADD_STAT(tageIncorrect, "tageIncorrect"),
          ADD_STAT(tageCorrectRatio, "TAGE correct ratio",
              1 - tageIncorrect / tageCommit),
          ADD_STAT(uncondCommit, ""),
          ADD_STAT(uncondIncorrect, ""),
          ADD_STAT(uncondCorrectRatio, "",
              1 - uncondIncorrect / uncondCommit),
          ADD_STAT(btbCommit, ""),
          ADD_STAT(btbIncorrect, ""),
          ADD_STAT(btbCorrectRatio, "",
              1 - btbIncorrect / btbCommit),
          ADD_STAT(indirCommit, ""),
          ADD_STAT(indirIncorrect, ""),
          ADD_STAT(indirCorrectRatio, "",
              1 - indirIncorrect / indirCommit),
          ADD_STAT(noTakenCommit, ""),
          ADD_STAT(noTakenIncorrect, ""),
          ADD_STAT(noTakenCorrectRatio, "",
              1 - noTakenIncorrect / noTakenCommit),
          ADD_STAT(RASCommit, ""),
          ADD_STAT(RASIncorrect, ""),
          ADD_STAT(notCtrlCommit, ""),
          ADD_STAT(notCtrlIncorrect, ""),
          ADD_STAT(notCtrlCorrectRatio, "",
              1 - notCtrlIncorrect / notCtrlCommit)
{
    icache_hit_ratio.precision(4);
    btb_hit_ratio.precision(4);
}

FFTrivialBP::FFTrivialBP(const FFTrivialBPParams &params)
        : FFBPredUnit(params),
          tage(params.tage),
          numLookAhead(params.numLookAheadInsts),
          BTB(params.BTBEntries,
              params.BTBTagSize,
              params.instShiftAmt,
              params.numThreads),
          iPred(params.indirectBranchPred),
          icache(params.ICEntries,
              params.instShiftAmt,
              params.numThreads),
          stats(this)
{
    RAS.init(params.RASSize);
    state.preRunCount = 0;
    state.seqNum = 1;
}

void FFTrivialBP::specLookup(int numInst, ThreadID tid)
{
    for (int i=0; i<numInst; i++) {
        StaticInstPtr ci(nullptr);
        ++stats.icache_lookups;
        if (icache.valid(state.pc.pc(), tid)) {
            auto ret = icache.lookup(state.pc.pc(), tid);
            ci = ret.first;
            state.pc = ret.second;
            ++stats.icache_hits;
        } else {
            state.affectedByCacheMiss = true;
        }

        TAGEState *bi = new TAGEState(*tage);

        if (iPred) {
            iPred->genIndirectInfo(tid, bi->iPredInfo);
        }

        // predicate the direction
        bool taken = false;
        if (ci && ci->isControl()) {
            taken = tage->tagePredict(tid, state.pc.pc(), !ci->isUncondCtrl(), bi->info);
            if (ci->isUncondCtrl()) {
                taken = true;
                bi->useUncond = true;
            } else {
                bi->useTage = true;
                bi->tageTaken = taken;
            }
            tage->updateHistories(tid, state.pc.pc(), taken, bi->info, true);
        } else {
            bi->useNotCtrl = true;
        }
        const bool tage_taken = taken;

        // predicate the target
        if (taken) {
            if (ci && ci->isCall()) {
                RAS.push(state.pc);
                bi->pushedRAS = true;

                // Record that it was a call so that the top RAS entry can
                // be popped off if the speculation is incorrect.
                bi->wasCall = true;
            }
            if (ci && ci->isReturn()) {
                bi->wasReturn = true;
                // If it's a function return call, then look up the address
                // in the RAS.
                TheISA::PCState rasTop = RAS.top();
                state.pc = TheISA::buildRetPC(state.pc, rasTop);

                // Record the top entry of the RAS, and its index.
                bi->usedRAS = true;
                bi->RASIndex = RAS.topIdx();
                bi->RASTarget = rasTop;

                RAS.pop();

            } else if ((ci && ci->isDirectCtrl()) || !iPred) {
                ++stats.btb_lookups;
                if (BTB.valid(state.pc.pc(), tid)) {
                    ++stats.btb_hits;
                    state.pc = BTB.lookup(state.pc.pc(), tid);
                    bi->useBTB = true;
                } else {
                    // Alter the direction
                    taken = false;
                    if (bi->pushedRAS && !bi->usedRAS) {
                        RAS.pop();
                        bi->pushedRAS = false;
                    } else {
                        tage->btbUpdate(tid, state.pc.instAddr(), bi->info);
                    }

                    state.affectedByBTBMiss = true;
                    state.pc.advance();
                    bi->useNoTaken = true;
                }

            } else {
                bi->wasIndirect = true;
                ++stats.indirectLookups;
                //Consult indirect predictor on indirect control
                TheISA::PCState target = state.pc;
                if (iPred->lookup(state.pc.instAddr(), target, tid)) {
                    ++stats.indirectHits;
                    bi->useIndir = true;
                } else {
                    ++stats.indirectMisses;
                    // Alter the direction
                    taken = false;
                    if (bi->pushedRAS && !bi->usedRAS) {
                        RAS.pop();
                        bi->pushedRAS = false;
                    }
                    target.advance();
                    bi->useNoTaken = true;
                }
                iPred->recordIndirect(state.pc.instAddr(), target.instAddr(), state.seqNum,
                        tid);
                state.pc = target;
            }

        } else {
            state.pc.advance();
            bi->useNoTaken = true;
        }

        if (iPred && ci && ci->isControl()) {
            iPred->updateDirectionInfo(tid, tage_taken);
        }

        assert(bi->useTage + bi->useUncond + bi->useNotCtrl == 1);
        assert(bi->useBTB + bi->useIndir + bi->usedRAS + bi->useNoTaken == 1);

        bi->taken = taken;
        bi->predPC = state.pc.pc();
        bi->affectedByCacheMiss = state.affectedByCacheMiss;
        bi->affectedByBTBMiss = state.affectedByBTBMiss;
        bi->seqNum = state.seqNum;
        state.inflight.push_front(bi);

        ++state.seqNum;
    }
}

void FFTrivialBP::resetSpecLookup(const TheISA::PCState &pc0, InstSeqNum seqNum) {
    state.pc = pc0;
    state.affectedByCacheMiss = false;
    state.affectedByBTBMiss = false;
    state.seqNum = seqNum;
}

Addr FFTrivialBP::lookup(ThreadID tid, const TheISA::PCState &instPC, const StaticInstPtr &inst,
                         void * &bp_history, unsigned numLookAhead) {
    auto bps = new BPState(state);
    bp_history = bps;

    icache.update(instPC.pc(), inst, instPC, tid);

    if (state.preRunCount == 0) {
        resetSpecLookup(instPC, 1);
        specLookup(numLookAhead, tid);
        state.preRunCount += numLookAhead;

    } else {
        specLookup(1, tid);
    }

    return state.inflight.front()->predPC;
}

void FFTrivialBP::update(ThreadID tid, const TheISA::PCState &pc,
                         void *bp_history, bool squashed,
                         const StaticInstPtr &inst,
                         Addr pred_DBB, Addr corr_DBB, unsigned numLookAhead) {

    BPState *bps = static_cast<BPState*>(bp_history);

    if (!squashed) {
        delete bps;
    }
}

void FFTrivialBP::squashInflight(ThreadID tid) {
    InstSeqNum squash_sn = state.inflight.back()->seqNum;
    if (iPred) {
        iPred->squash(squash_sn, tid);
    }

    for (int i=state.inflight.size()-2; i>=0 ;i--) {
        auto cts = state.inflight[i];
        assert(cts->seqNum > squash_sn);

        if (cts->usedRAS) {
            RAS.restore(cts->RASIndex,
                             cts->RASTarget);
        } else if (cts->wasCall && cts->pushedRAS) {
            RAS.pop();
        }

        if (iPred) {
            iPred->deleteIndirectInfo(tid, cts->iPredInfo);
        }
    }
}

void FFTrivialBP::commit(ThreadID tid, const TheISA::PCState &pc, const StaticInstPtr &inst) {
    assert(!state.inflight.empty());
    TAGEState *ts = state.inflight.back();
    TAGEBase::BranchInfo *tage_bi = ts->info;
    Addr corrTarget = pc.npc();
    bool tsMispred = (corrTarget != ts->predPC);
    bool taken = pc.branching();

    // update stats
    if (ts->useTage)
        stats.tageCommit++;
    if (ts->useUncond)
        stats.uncondCommit++;
    if (ts->useBTB)
        stats.btbCommit++;
    if (ts->useIndir)
        stats.indirCommit++;
    if (ts->useNoTaken)
        stats.noTakenCommit++;
    if (ts->usedRAS)
        stats.RASCommit++;
    if (ts->useNotCtrl)
        stats.notCtrlCommit++;

    if (tsMispred) {
        if (ts->usedRAS)
            stats.RASIncorrect++;
        if (ts->useBTB)
            stats.btbIncorrect++;
        if (ts->useIndir)
            stats.indirIncorrect++;
        if (ts->affectedByCacheMiss)
            stats.mispredAffectedByCacheMiss++;
        if (ts->affectedByBTBMiss)
            stats.mispredAffectedByBTBMiss++;
    } else {
        if (ts->affectedByCacheMiss)
            stats.correctAffectedByCacheMiss++;
        if (ts->affectedByBTBMiss)
            stats.correctAffectedByBTBMiss++;
    }
    if (ts->useTage && ts->tageTaken != taken)
        stats.tageIncorrect++;
    if (ts->useUncond && !inst->isUncondCtrl())
        stats.uncondIncorrect++;
    if (ts->useNoTaken && taken)
        stats.noTakenIncorrect++;
    if (ts->useNotCtrl && inst->isControl())
        stats.notCtrlIncorrect++;

    if (inst->isControl()) {
        int nrand = random_mt.random<int>() & 3;
        if (tage_bi->condBranch) {
            tage->updateStats(taken, tage_bi);
            tage->condBranchUpdate(tid, pc.pc(), taken, tage_bi, nrand,
                                pc.npc(), tage_bi->tagePred);
        }

        // optional non speculative update of the histories
        tage->updateHistories(tid, pc.pc(), taken, tage_bi, false, inst,
                            pc.npc());
    }

    if (tsMispred) {
        squashInflight(tid);

        if (taken) {
            if (ts->wasReturn && !ts->usedRAS) {
                RAS.pop();
                ts->usedRAS = true;
            }
            if (ts->wasIndirect) {
                ++stats.indirectMispredicted;
                if (iPred) {
                    iPred->recordTarget(
                        ts->seqNum, ts->iPredInfo,
                        corrTarget, tid);
                }
            } else {
                BTB.update(pc.pc(), pc.npc(), tid);
            }

        } else {
            //Actually not Taken
            if (ts->usedRAS) {
                RAS.restore(ts->RASIndex, ts->RASTarget);
                ts->usedRAS = false;

            } else if (ts->wasCall && ts->pushedRAS) {
                //Was a Call but predicated false. Pop RAS here
                RAS.pop();
                ts->pushedRAS = false;
            }
        }

        if (iPred && inst->isControl()) {
            iPred->changeDirectionPrediction(tid,
                ts->iPredInfo, taken);
        }

        // This restores the global history, then update it
        // and recomputes the folded histories.
        tage->squash(tid, taken, tage_bi, pc.npc());
        ts->predPC = pc.npc();

        TheISA::PCState npcState(pc.npc());
#if THE_ISA == RISCV_ISA
        // use the PC status of the preceding inst as a guess.
        // if the dummy icache hits, the status will be updated to the correct value.
        if (pc.compressed())
            npcState.npc(pc.npc() + 2);
        else
            npcState.npc(pc.npc() + 4);
#endif
        resetSpecLookup(npcState, ts->seqNum + 1);

        // Rerun in-flight predictions
        int oldSize = state.inflight.size();
        int toRerun = oldSize - 1;
        state.inflight.erase(state.inflight.begin(), state.inflight.end() - 1);
        assert(state.inflight.size() == 1);
        specLookup(toRerun, tid);
        assert(state.inflight.size() == oldSize);
    }

    if (iPred) {
        iPred->commit(ts->seqNum, tid, ts->iPredInfo);
    }
    delete ts;
    state.inflight.pop_back(); // remote ts
}

void FFTrivialBP::squash(ThreadID tid, void *bp_history) {
    auto bps = static_cast<BPState *>(bp_history);
    state = *bps;

    if (!bps->inflight.empty()) {
        auto ts = bps->inflight.front();
        tage->squash(tid, ts->taken, ts->info, ts->predPC);
        RAS.restore(ts->RASIndex, ts->RASTarget);
        if (iPred) {
            iPred->squash(0, tid);
            // FIXME: restore the indirect predictor.
        }

    } else {
        assert(bps->preRunCount == 0);
        RAS.reset();
        if (iPred) {
            iPred->squash(0, tid);
        }
    }
    delete bps;
}

//---------------------------------------------------------------------------------
//---------------------------------------------------------------------------------
// **** FFTrivial::ICache ****
//---------------------------------------------------------------------------------
//---------------------------------------------------------------------------------

// The ICache is brought from cpu/pred/btb.cc

FFTrivialBP::ICache::ICache(unsigned _numEntries,
                       unsigned _instShiftAmt,
                       unsigned _num_threads)
    : numEntries(_numEntries),
      instShiftAmt(_instShiftAmt),
      log2NumThreads(floorLog2(_num_threads))
{
    if (!isPowerOf2(numEntries)) {
        fatal("Temporary ICache entries is not a power of 2!");
    }

    set.resize(numEntries);

    for (unsigned i = 0; i < numEntries; ++i) {
        set[i].valid = false;
    }

    idxMask = numEntries - 1;

    tagShiftAmt = instShiftAmt + floorLog2(numEntries);
}

void
FFTrivialBP::ICache::reset()
{
    for (unsigned i = 0; i < numEntries; ++i) {
        set[i].valid = false;
    }
}

inline
unsigned
FFTrivialBP::ICache::getIndex(Addr instPC, ThreadID tid)
{
    // Need to shift PC over by the word offset.
    return ((instPC >> instShiftAmt)
            ^ (tid << (tagShiftAmt - instShiftAmt - log2NumThreads)))
            & idxMask;
}

inline
Addr
FFTrivialBP::ICache::getTag(Addr instPC)
{
    return (instPC >> tagShiftAmt);
}

bool
FFTrivialBP::ICache::valid(Addr instPC, ThreadID tid)
{
    unsigned set_idx = getIndex(instPC, tid);

    Addr inst_tag = getTag(instPC);

    assert(set_idx < numEntries);

    if (set[set_idx].valid
        && inst_tag == set[set_idx].tag
        && set[set_idx].tid == tid) {
        return true;
    } else {
        return false;
    }
}

// @todo Create some sort of return struct that has both whether or not the
// address is valid, and also the address.  For now will just use addr = 0 to
// represent invalid entry.
std::pair<StaticInstPtr, TheISA::PCState>
FFTrivialBP::ICache::lookup(Addr instPC, ThreadID tid)
{
    unsigned set_idx = getIndex(instPC, tid);

    Addr inst_tag = getTag(instPC);

    assert(set_idx < numEntries);

    if (set[set_idx].valid
        && inst_tag == set[set_idx].tag
        && set[set_idx].tid == tid) {
        return std::make_pair(set[set_idx].inst, set[set_idx].pcState);
    } else {
        return std::make_pair(nullptr, 0);
    }
}

void
FFTrivialBP::ICache::update(Addr instPC, StaticInstPtr inst, const TheISA::PCState &pcState, ThreadID tid)
{
    unsigned set_idx = getIndex(instPC, tid);

    assert(set_idx < numEntries);

    set[set_idx].tid = tid;
    set[set_idx].valid = true;
    set[set_idx].inst = inst;
    set[set_idx].pcState = pcState;
    set[set_idx].tag = getTag(instPC);
}
