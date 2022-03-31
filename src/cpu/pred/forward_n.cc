//
// Created by yqszxx on 1/4/22.
// ChangeLog:
// gaozb 3/5/22 - adapt to PPM-like predictor
//

#include "debug/ForwardN.hh"
#include "forward_n.hh"

ForwardN::ForwardNStats::ForwardNStats(Stats::Group *parent, const ForwardNParams &params)
        : Stats::Group(parent, "forward_n"),
          ADD_STAT(lookups, "Number of ForwardN lookups"),
          ADD_STAT(gtabHit, "Number of ForwardN global table hit"),
          ADD_STAT(coldStart, "Number of cold-start lookups"),
          ADD_STAT(dbbSizeSD, "Standard deviation of DBB size"),
          ADD_STAT(averageDBBsize, "Average inst count of dynamic basic blocks.",
                    dbbSizeSum / dbbCount)
{
    gtabHit.init(params.numGTabBanks);
}

ForwardN::ForwardN(const ForwardNParams &params)
        : FFBPredUnit(params),
          stats(this, params),
          traceStart(params.traceStart),
          traceCount(params.traceCount),
          btabBank(params.numBTabEntries, params.pcSetSize, params.randNumSeed),
          randGen(params.randNumSeed),
          numLookAheadInst(params.numLookAheadInsts),
          dbbAverageWindowsSize(params.dbbAverageWindowsSize)
{
    DPRINTF(ForwardN, "ForwardN, N=%u%s, "
                      "numGTabBanks=%u, "
                      "numGTabEntries=%u, "
                      "numBTabEntries=%u, "
                      "histLenInitial=%u, "
                      "histLenGrowth=%f\n",
                      params.numLookAheadInsts,
                      isPredDBB() ? "/DBBsize" : "",
                      params.numGTabBanks,
                      params.numGTabEntries,
                      params.numBTabEntries,
                      params.histLenInitial,
                      params.histLenGrowth
                      );

    int histLen = params.histLenInitial;
    for (int i = 0; i < params.numGTabBanks; i++) {
        gtabBanks.emplace_back(GTabBank(params.numGTabEntries, histLen, params.pcSetSize, params.randNumSeed));
        histTakenMaxLength = histLen;
        DPRINTF(ForwardN, "GTab Bank #%d: histLen=%d\n", i, histLen);

        // L(j) = g^(j-1)*L(1)
        histLen = int(params.histLenGrowth * histLen + 0.5);
    }

    state.computedInd.resize(gtabBanks.size());
    state.computedTag.resize(gtabBanks.size());

    state.pathHist = 0;
    state.histTaken = 0;

    state.dbbCount = 0;
    state.dbbTotalSize = 0;
    state.instCount = 0;
}

Addr ForwardN::lookup(ThreadID tid, const TheISA::PCState &pc, const StaticInstPtr &inst,
                      void * &bp_history, unsigned stride) {
    auto hist = new BPState(state);
    bp_history = hist;

    ++stats.lookups;

    hist->bank = -1;

    // Look for the bank with longest matching history
    for (int i = gtabBanks.size()-1; i >= 0; i--) {
        hist->computedInd[i] = bankHash(pc.pc(), state.pathHist, state.histTaken, gtabBanks[i]);
        hist->computedTag[i] = tagHash(pc.pc(), state.histTaken, gtabBanks[i]);

        const auto &gEntry = gtabBanks[i](hist->computedInd[i]);
        if (hist->computedTag[i] == gEntry.tag) {
            auto ret = gtabBanks[i](hist->computedInd[i]).st.query(stride);
            if (ret.second) {
                hist->bank = i;
                hist->predPC = ret.first;
                ++stats.gtabHit[i];
                break;
            }
        }
    }
    // Look up base predictor
    if (hist->bank == -1) {
        auto ret = btabBank(btabHash(pc.pc())).st.query(stride);
        hist->predPC = ret.first;
    }

    if (hist->bank != gtabBanks.size() - 1)
        ++stats.coldStart;

    updateHistory(inst->isControl(), true, pc.pc());

    return hist->predPC;
}

void ForwardN::update(ThreadID tid, const TheISA::PCState &pc,
                      void *bp_history, bool squashed,
                      const StaticInstPtr &inst,
                     Addr pred_DBB, Addr corr_DBB, unsigned stride) {

    auto bp_hist = static_cast<BPState *>(bp_history);

    if (pred_DBB != corr_DBB) {
        // prediction is incorrect

        // restore global histories, then recompute them
        restoreHistory(bp_hist);
        updateHistory(inst->isControl(), true, pc.pc());

        // update and/or allocate banks
        if (bp_hist->bank >= 0) {
            int indice = bp_hist->computedInd[bp_hist->bank];
            gtabBanks[bp_hist->bank](indice).st.update(corr_DBB, stride);
        } else {
            btabBank(btabHash(pc.pc())).st.update(corr_DBB, stride);
        }

        allocEntry(bp_hist->bank, pc.pc(), corr_DBB, stride,
                    bp_hist->computedInd, bp_hist->computedTag);

        if (bp_hist->bank >= 0) {
            // update status bits
            int indice = bp_hist->computedInd[bp_hist->bank];
            gtabBanks[bp_hist->bank](indice).useful = false;
            btabBank(btabHash(pc.pc())).meta = false;
        }

    } else {
        // prediction is correct
        if (bp_hist->bank >= 0) {
            int indice = bp_hist->computedInd[bp_hist->bank];

            gtabBanks[bp_hist->bank](indice).st.update(corr_DBB, stride);

            gtabBanks[bp_hist->bank](indice).useful = true;
            btabBank(btabHash(pc.pc())).meta = true;

        } else {
            btabBank(btabHash(pc.pc())).st.update(corr_DBB, stride);
        }
    }

    if (pred_DBB != corr_DBB) {
        static int c = 0;
        if (c >= traceStart && c < traceStart + traceCount) {
            DPRINTF(ForwardN, "Mispred: pred=0x%016lX, act=0x%016lX, off=%d\n",
                    pred_DBB,
                    corr_DBB,
                    corr_DBB > pred_DBB ?
                        (signed int)(corr_DBB - pred_DBB) :
                        -(signed int)(pred_DBB - corr_DBB)
                    );
        }
        c++;
    }

    if (!squashed) {
        delete bp_hist;
    }
}

void ForwardN::foldedXOR(Addr &dst, Addr src, int srcLen, int dstLen) {
    while (srcLen > 0) {
        dst ^= src & ((1 << dstLen) - 1);
        src >>= dstLen;
        srcLen -= dstLen;
    }
}

Addr ForwardN::bankHash(Addr PC, Addr pathHist, uint64_t histTaken, const GTabBank &bank) {
    Addr hash = 0;
    foldedXOR(hash, PC, sizeof(PC)*8, bank.getLogNumEntries());
    foldedXOR(hash, pathHist, sizeof(pathHist)*8, bank.getLogNumEntries());
    foldedXOR(hash, histTaken, bank.getHistLen(), bank.getLogNumEntries());
    assert(hash < (1<<bank.getLogNumEntries()));
    return hash;
}

Addr ForwardN::tagHash(Addr PC, uint64_t histTaken, const GTabBank &bank) {
    Addr hash = 0;
    unsigned midBits = sizeof(PC)*4;
    foldedXOR(hash, (PC<<midBits) | ((PC>>midBits) & ((1UL<<midBits)-1)), sizeof(PC)*8, bank.getLogNumEntries());
    foldedXOR(hash, histTaken, bank.getHistLen(), bank.getLogNumEntries());
    assert(hash < (1<<bank.getLogNumEntries()));
    return hash;
}

Addr ForwardN::btabHash(Addr PC) {
    return PC % btabBank.entries.size();
}

void ForwardN::allocEntry(int bank, Addr PC, Addr corrDBB, unsigned stride,
                          const std::vector<Addr> &computedInd, const std::vector<Addr> &computedTag) {

    auto reinit = [PC, corrDBB, stride, this](int n, Addr indice, Addr tag) {
        gtabBanks[n](indice).tag = tag;
        gtabBanks[n](indice).useful = false;

        const auto &baseEntry = btabBank(btabHash(PC));
        if (baseEntry.meta) {
            gtabBanks[n](indice).st.update(corrDBB, stride);
        } else {
            auto lc = baseEntry.st.query(stride);
            if (!lc.second)
                lc.first = corrDBB;
            gtabBanks[n](indice).st.update(lc.first, stride);
        }
    };

    if (bank != gtabBanks.size() - 1) {
        bool allUseful = true;
        for (int n = bank + 1; n < gtabBanks.size(); n++) {
            if (!gtabBanks[n](computedInd[n]).useful) {
                allUseful = false;
                reinit(n, computedInd[n], computedTag[n]);
            }
        }
        if (allUseful) {
            std::uniform_int_distribution<size_t> u(bank + 1, gtabBanks.size() - 1);
            size_t n = u(randGen);
            reinit(n, computedInd[n], computedTag[n]);
        }
    }
}

void ForwardN::updateHistory(bool isControl, bool taken, Addr pc) {
    if (isControl) {
        state.pathHist ^= pc;

        state.histTaken <<= 1;
        state.histTaken |= taken;
        state.histTaken &= ((1 << histTakenMaxLength) - 1);
    }
}

void ForwardN::squash(ThreadID tid, void *bp_history) {
    auto bp_hist = static_cast<BPState *>(bp_history);

    // for speculative updates
    restoreHistory(bp_hist);

    delete bp_hist;
}

void ForwardN::restoreHistory(BPState *bp_hist)
{
    state.pathHist = bp_hist->pathHist;
    state.histTaken = bp_hist->histTaken;
}

void ForwardN::commit(ThreadID tid, const TheISA::PCState &pc, const StaticInstPtr &inst)
{
    adaptNumLookAhead(pc);
}

void ForwardN::adaptNumLookAhead(const TheISA::PCState &pc)
{
    ++state.instCount;

    if (pc.branching()) {
        state.dbbTotalSize += state.instCount;
        state.instCountQue.push_front(state.instCount);
        if (state.dbbCount < dbbAverageWindowsSize) {
            ++state.dbbCount;

        } else {
            state.dbbTotalSize -= state.instCountQue.back();
            state.instCountQue.pop_back();
        }
        assert(state.instCountQue.size() == state.dbbCount);

        // update stats
        stats.dbbSizeSum += state.instCount;
        stats.dbbCount++;
        stats.dbbSizeSD.sample(state.instCount);

        state.instCount = 0;
    }
}

unsigned ForwardN::getStride() const
{
    if (isPredDBB()) {
        if (state.dbbCount == 0)
            return 1U;
        else {
            return std::max(1U, unsigned(float(numLookAheadInst) / (state.dbbTotalSize / state.dbbCount) + 0.5));
        }

    } else {
        return numLookAheadInst;
    }
}

//---------------------------------------------------------------------------------
//---------------------------------------------------------------------------------
// **** ForwardN::PCset ****
//---------------------------------------------------------------------------------
//---------------------------------------------------------------------------------

ForwardN::PCset::PCset(unsigned setSize, unsigned randSeed)
    : s(setSize),
      randGen(randSeed)
{
}

void ForwardN::PCset::update(Addr pc, unsigned stride)
{
    bool hit = false;
    size_t freeIdx = (size_t)-1;

    for (size_t i=0;i<s.size();i++) {
        if (s[i].valid && s[i].pc == pc && s[i].stride == stride) {
            hit = true;
            break;
        }
        if (!s[i].valid)
            freeIdx = i;
    }

    if (!hit) {
        if (freeIdx == (size_t)-1) { // replace
            int minCount = INT_MAX;
            for (size_t i=0;i<s.size();i++) {
                assert(s[i].valid);
                if (minCount > s[i].count) {
                    freeIdx = i, minCount = s[i].count;
                }
            }
        }
        s[freeIdx].valid = true;
        s[freeIdx].pc = pc;
        s[freeIdx].stride = stride;
        s[freeIdx].count = 0;
    }

    // update the saturated counter
    for (size_t i=0;i<s.size();i++) {
        if (s[i].valid && s[i].stride == stride) {
            if (s[i].pc == pc)
                s[i].up();
            else
                s[i].down();
        }
    }

}

std::pair<Addr, bool> ForwardN::PCset::query(unsigned stride) const
{
    int maxCount = INT_MIN;
    size_t maxIdx;
    for (size_t i=0;i<s.size();i++) {
        if (s[i].valid && s[i].stride == stride) {
            if (s[i].count > maxCount) {
                maxCount = s[i].count;
                maxIdx = i;
            }
        }
    }
    if (maxCount == INT_MIN) {
        for (size_t i=0;i<s.size();i++) {
            if (s[i].valid)
                return std::make_pair(s[i].pc, false);
        }
        return std::make_pair(s[0].pc, false);
    }
    return std::make_pair(s[maxIdx].pc, true);
}

//---------------------------------------------------------------------------------
//---------------------------------------------------------------------------------
// **** ForwardN::GTabBank ****
//---------------------------------------------------------------------------------
//---------------------------------------------------------------------------------

ForwardN::GTabBank::GTabBank(int numEntries, int histLen, unsigned pcSetSize, unsigned randSeed)
    : histLen(histLen)
{
    assert(!(numEntries & 1));
    logNumEntries = std::log2(numEntries);
    for (size_t i=0;i<numEntries;i++) {
        entries.emplace_back(Entry(pcSetSize, randSeed));
    }
}

//---------------------------------------------------------------------------------
//---------------------------------------------------------------------------------
// **** ForwardN::BTabBank ****
//---------------------------------------------------------------------------------
//---------------------------------------------------------------------------------

ForwardN::BTabBank::BTabBank(int numEntries, unsigned pcSetSize, unsigned randSeed) {
    assert(!(numEntries & 1));
    logNumEntries = std::log2(numEntries);
    for (size_t i=0;i<numEntries;i++) {
        entries.emplace_back(Entry(pcSetSize, randSeed));
    }
}
