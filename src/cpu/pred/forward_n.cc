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
                   gtabHit / lookups)
{
    gtabHitRate.precision(4);
}

ForwardN::ForwardN(const ForwardNParams &params)
        : FFBPredUnit(params),
          stats(this),
          traceStart(params.traceStart),
          traceCount(params.traceCount),
          btabBank(params.numBTabEntries),
          randGen(params.randNumSeed),
          histTaken(0)
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

    for (int i = 0; i < params.histLength; i++) {
        lastCtrlsForPred.push_back(invalidPC);
        lastCtrlsForUpd.push_back(invalidPC);
    }

    state.computedInd.resize(gtabBanks.size());
    state.computedTag.resize(gtabBanks.size());
}

Addr ForwardN::lookup(ThreadID tid, Addr instPC, void * &bp_history) {
    auto hist = new BPState(state);
    bp_history = hist;

    ++stats.lookups;

    hist->bank = -1;

    // Look for the bank with longest matching history
    for (int i = gtabBanks.size()-1; i >= 0; i--) {
        hist->computedInd[i] = bankHash(instPC, lastCtrlsForPred, histTaken, gtabBanks[i]);
        hist->computedTag[i] = tagHash(instPC, histTaken, gtabBanks[i]);

        if (hist->computedTag[i] == gtabBanks[i](hist->computedInd[i]).tag) {
            hist->bank = i;
            hist->predPC = gtabBanks[i](hist->computedInd[i]).pc;
            ++stats.gtabHit;
            break;
        }
    }
    // Look up base predictor
    if (hist->bank == -1) {
        hist->predPC = btabBank(btabHash(instPC)).pc;
    }

    if (hist->bank != gtabBanks.size() - 1)
        ++stats.coldStart;

    return hist->predPC;
}

void ForwardN::update(ThreadID tid, const TheISA::PCState &thisPC,
                void *bp_history, bool squashed,
                const StaticInstPtr &inst,
                const TheISA::PCState &pred_DBB, const TheISA::PCState &corr_DBB) {

    auto bp_hist = static_cast<BPState *>(bp_history);

    static uint64_t histTakenUpd = 0;

    if (squashed) {
        // prediction is incorrect
        if (bp_hist->bank >= 0) {
            int indice = bp_hist->computedInd[bp_hist->bank];
            gtabBanks[bp_hist->bank](indice).pc = corr_DBB.pc();
        } else {
            btabBank(btabHash(thisPC.pc())).pc = corr_DBB.pc();
        }

        allocEntry(bp_hist->bank, thisPC.pc(), corr_DBB.pc(),
                    bp_hist->computedInd, bp_hist->computedTag);

        if (bp_hist->bank >= 0) {
            // update status bits
            int indice = bp_hist->computedInd[bp_hist->bank];
            gtabBanks[bp_hist->bank](indice).useful = false;
            btabBank(btabHash(thisPC.pc())).meta = false;
        }

    } else {
        // prediction is correct
        if (bp_hist->bank >= 0) {
            int indice = bp_hist->computedInd[bp_hist->bank];

            gtabBanks[bp_hist->bank](indice).pc = corr_DBB.pc();

            gtabBanks[bp_hist->bank](indice).useful = true;
            btabBank(btabHash(thisPC.pc())).meta = true;

        } else {
            btabBank(btabHash(thisPC.pc())).pc = corr_DBB.pc();
        }
    }

    // Update global history
    if (inst->isControl()) {
        lastCtrlsForUpd.push_back(thisPC.pc());
        lastCtrlsForUpd.pop_front();

        histTakenUpd <<= 1;
        histTakenUpd |= thisPC.branching();
        histTakenUpd &= ((1 << histTakenMaxLength) - 1);
    }

    // FIXME: this must be updated speculatively
    if (inst->isControl()) {
        lastCtrlsForPred.push_back(thisPC.pc());
        lastCtrlsForPred.pop_front();

        histTaken <<= 1;
        histTaken |= thisPC.branching();
        histTaken &= ((1 << histTakenMaxLength) - 1);
    }

    if (squashed) {
        static int c = 0;
        if (c >= traceStart && c < traceStart + traceCount) {
            DPRINTF(ForwardN, "Mispred: pred=0x%016lX, act=0x%016lX, off=%d\n",
                    pred_DBB.pc(),
                    corr_DBB.pc(),
                    corr_DBB.pc() > pred_DBB.pc() ?
                        (signed int)(corr_DBB.pc() - pred_DBB.pc()) :
                        -(signed int)(pred_DBB.pc() - corr_DBB.pc())
                    );
        }
        c++;
    }
}

void ForwardN::foldedXOR(Addr &dst, Addr src, int srcLen, int dstLen) {
    while (srcLen > 0) {
        dst ^= src & ((1 << dstLen) - 1);
        src >>= dstLen;
        srcLen -= dstLen;
    }
}

Addr ForwardN::bankHash(Addr PC, const std::deque<Addr> &history, uint64_t histTaken, const GTabBank &bank) {
    Addr hash = 0;
    foldedXOR(hash, PC, sizeof(PC)*8, bank.getLogNumEntries());
    std::for_each(
            history.begin(),
            history.end(),
            [&hash, &bank, this](Addr a) {
                foldedXOR(hash, a, sizeof(a)*8, bank.getLogNumEntries());
            });
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


void ForwardN::squash(ThreadID tid, void *bp_history) {
    auto bp_hist = static_cast<BPState *>(bp_history);

    delete bp_hist;
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
