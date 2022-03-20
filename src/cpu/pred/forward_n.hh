//
// Created by yqszxx on 1/4/22.
// ChangeLog:
// gaozb 3/5/22 - adapt to PPM-like predictor
//

#ifndef GEM5_FORWARD_N_H
#define GEM5_FORWARD_N_H

#include <deque>
#include <map>
#include <queue>
#include <random>
#include <utility>

#include "base/statistics.hh"
#include "base/types.hh"
#include "cpu/inst_seq.hh"
#include "cpu/pred/ff_bpred_unit.hh"
#include "cpu/static_inst.hh"
#include "params/ForwardN.hh"

/**
 * Implements a local predictor that uses the PC to index into a table of
 * counters.  Note that any time a pointer to the bp_history is given, it
 * should be NULL using this predictor because it does not have any branch
 * predictor state that needs to be recorded or updated; the update can be
 * determined solely by the branch being taken or not taken.
 */
class ForwardN : public FFBPredUnit
{
public:

    ForwardN(const ForwardNParams &params);

    Addr lookup(ThreadID tid, const TheISA::PCState &pc, const StaticInstPtr &inst, void * &bp_history) override;

    void update(ThreadID tid, const TheISA::PCState &pc,
                void *bp_history, bool squashed,
                const StaticInstPtr &inst,
                Addr pred_DBB, Addr corr_DBB) override;

    void squash(ThreadID tid, void *bp_history) override;


private:
    class GTabBank {
    public:
        GTabBank(int numEntries, int histLen);

        inline int getLogNumEntries() const { return logNumEntries; }
        inline int getHistLen() const { return histLen; }

    public:
        struct Entry {
            Entry() : useful(false) {}
            Addr pc;
            Addr tag;
            bool useful;
        };

        std::vector<Entry> entries;
        int logNumEntries;
        int histLen;

        inline Entry &operator()(unsigned ind) {
            return entries[ind];
        }
    };

    class BTabBank {
    public:
        BTabBank(int numEntries);

        struct Entry {
            Entry() : meta(false) {}
            Addr pc;
            bool meta;
        };

        std::vector<Entry> entries;
        int logNumEntries;

        inline Entry &operator()(unsigned ind) {
            return entries[ind];
        }
    };

private:

    struct ForwardNStats : public Stats::Group
    {
        ForwardNStats(Stats::Group *parent);

        Stats::Scalar lookups;

        Stats::Scalar gtabHit;

        Stats::Formula gtabHitRate;

        Stats::Scalar coldStart;
    } stats;

    unsigned int histTakenMaxLength;

    unsigned int traceStart, traceCount;

    std::vector<GTabBank> gtabBanks;
    BTabBank btabBank;

    std::default_random_engine randGen;

    const Addr invalidPC = 0xFFFFFFFFFFFFFFFFLL;

    struct BPState {
        int bank;
        std::vector<Addr> computedInd;
        std::vector<Addr> computedTag;
        Addr predPC;

        Addr pathHist;
        uint64_t histTaken;
    } state;

private:
    void foldedXOR(Addr &dst, Addr src, int srcLen, int dstLen);
    Addr bankHash(Addr PC, Addr pathHist, uint64_t histTaken, const GTabBank &bank);
    Addr tagHash(Addr PC, uint64_t histTaken, const GTabBank &bank);
    Addr btabHash(Addr PC);
    void allocEntry(int bank, Addr PC, Addr corrDBB,
                    const std::vector<Addr> &computedInd, const std::vector<Addr> &computedTag);
    void updateHistory(bool isControl, bool taken, Addr pc);
    void restoreHistory(BPState *bp_hist);

};

#endif //GEM5_FORWARD_N_H
