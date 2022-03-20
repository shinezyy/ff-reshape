//
// Created by yqszxx on 1/4/22.
// ChangeLog:
// gaozb 3/5/22 - adapt to PPM-like predictor
//

#include "debug/ForwardN.hh"
#include "forward_n.hh"

ForwardN::ForwardNStats::ForwardNStats(Stats::Group *parent)
        : Stats::Group(parent, "forward_n"),
          ADD_STAT(lookups, "Number of ForwardN lookups"),
          ADD_STAT(gtabHit, "Number of ForwardN global table hit"),
          ADD_STAT(gtabHitRate, "ForwardN prediction global table hit rate",
                   gtabHit / lookups),
          ADD_STAT(coldStart, "Number of cold-start lookups")
{
    gtabHitRate.precision(4);
}

ForwardN::ForwardN(const ForwardNParams &params)
        : FFBPredUnit(params),
          stats(this),
          traceStart(params.traceStart),
          traceCount(params.traceCount),
          btabBank(params.numBTabEntries),
          randGen(params.randNumSeed)
{
    DPRINTF(ForwardN, "ForwardN, N=%u, "
                      "numGTabBanks=%u, "
                      "numGTabEntries=%u, "
                      "numBTabEntries=%u, "
                      "histLenInitial=%u, "
                      "histLenGrowth=%f\n",
                      getNumLookAhead(),
                      params.numGTabBanks,
                      params.numGTabEntries,
                      params.numBTabEntries,
                      params.histLenInitial,
                      params.histLenGrowth
                      );

    int histLen = params.histLenInitial;
    for (int i = 0; i < params.numGTabBanks; i++) {
        gtabBanks.emplace_back(GTabBank(params.numGTabEntries, histLen));
        histTakenMaxLength = histLen;
        DPRINTF(ForwardN, "GTab Bank #%d: histLen=%d\n", i, histLen);

        // L(j) = g^(j-1)*L(1)
        histLen = int(params.histLenGrowth * histLen + 0.5);
    }

    state.computedInd.resize(gtabBanks.size());
    state.computedTag.resize(gtabBanks.size());

    state.pathHist = 0;
    state.histTaken = 0;
}

Addr ForwardN::lookup(ThreadID tid, const TheISA::PCState &pc, const StaticInstPtr &inst, void * &bp_history) {
    auto hist = new BPState(state);
    bp_history = hist;

    ++stats.lookups;

    hist->bank = -1;

    // Look for the bank with longest matching history
    for (int i = gtabBanks.size()-1; i >= 0; i--) {
        hist->computedInd[i] = bankHash(pc.pc(), state.pathHist, state.histTaken, gtabBanks[i]);
        hist->computedTag[i] = tagHash(pc.pc(), state.histTaken, gtabBanks[i]);

        if (hist->computedTag[i] == gtabBanks[i](hist->computedInd[i]).tag) {
            hist->bank = i;
            hist->predPC = gtabBanks[i](hist->computedInd[i]).pc;
            ++stats.gtabHit;
            break;
        }
    }
    // Look up base predictor
    if (hist->bank == -1) {
        hist->predPC = btabBank(btabHash(pc.pc())).pc;
    }

    if (hist->bank != gtabBanks.size() - 1)
        ++stats.coldStart;

    updateHistory(inst->isControl(), true, pc.pc());

    return hist->predPC;
}

void ForwardN::update(ThreadID tid, const TheISA::PCState &pc,
                      void *bp_history, bool squashed,
                      const StaticInstPtr &inst,
                     Addr pred_DBB, Addr corr_DBB) {

    auto bp_hist = static_cast<BPState *>(bp_history);

    if (pred_DBB != corr_DBB) {
        // prediction is incorrect

        // restore global histories, then recompute them
        restoreHistory(bp_hist);
        updateHistory(inst->isControl(), true, pc.pc());

        // update and/or allocate banks
        if (bp_hist->bank >= 0) {
            int indice = bp_hist->computedInd[bp_hist->bank];
            gtabBanks[bp_hist->bank](indice).pc = corr_DBB;
        } else {
            btabBank(btabHash(pc.pc())).pc = corr_DBB;
        }

        allocEntry(bp_hist->bank, pc.pc(), corr_DBB,
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

            gtabBanks[bp_hist->bank](indice).pc = corr_DBB;

            gtabBanks[bp_hist->bank](indice).useful = true;
            btabBank(btabHash(pc.pc())).meta = true;

        } else {
            btabBank(btabHash(pc.pc())).pc = corr_DBB;
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
    foldedXOR(hash, PC, sizeof(PC)*8, bank.getLogNumEntries());
    foldedXOR(hash, histTaken, bank.getHistLen(), bank.getLogNumEntries());
    assert(hash < (1<<bank.getLogNumEntries()));
    return hash;
}

Addr ForwardN::btabHash(Addr PC) {
    return PC % btabBank.entries.size();
}

void ForwardN::allocEntry(int bank, Addr PC, Addr corrDBB,
                          const std::vector<Addr> &computedInd, const std::vector<Addr> &computedTag) {

    auto reinit = [PC, corrDBB, this](int n, Addr indice, Addr tag) {
        gtabBanks[n](indice).tag = tag;
        gtabBanks[n](indice).useful = false;

        const auto &baseEntry = btabBank(btabHash(PC));
        if (baseEntry.meta) {
            gtabBanks[n](indice).pc = corrDBB;
        } else {
            gtabBanks[n](indice).pc = baseEntry.pc;
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
            int n = u(randGen);
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

//---------------------------------------------------------------------------------
//---------------------------------------------------------------------------------
// **** ForwardN::GTabBank ****
//---------------------------------------------------------------------------------
//---------------------------------------------------------------------------------

ForwardN::GTabBank::GTabBank(int numEntries, int histLen)
    : histLen(histLen)
{
    assert(!(numEntries & 1));
    logNumEntries = std::log2(numEntries);
    entries.resize(numEntries);
}

//---------------------------------------------------------------------------------
//---------------------------------------------------------------------------------
// **** ForwardN::BTabBank ****
//---------------------------------------------------------------------------------
//---------------------------------------------------------------------------------

ForwardN::BTabBank::BTabBank(int numEntries) {
    assert(!(numEntries & 1));
    logNumEntries = std::log2(numEntries);
    entries.resize(numEntries);
}
