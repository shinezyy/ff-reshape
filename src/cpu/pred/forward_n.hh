//
// Created by yqszxx on 1/4/22.
//

#ifndef GEM5_FORWARD_N_H
#define GEM5_FORWARD_N_H

#include <deque>
#include <map>
#include <queue>
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

    Addr lookup(ThreadID tid, Addr instPC, void * &bp_history) override;

    void update(ThreadID tid, const TheISA::PCState &thisPC,
                void *bp_history, bool squashed,
                const StaticInstPtr &inst, Addr pred_nextK_PC, Addr corr_nextK_PC) override;

    void squash(ThreadID tid, void *bp_history) override;

private:
    static Addr hashHistory(const std::deque<Addr> &history);

    struct ForwardNStats : public Stats::Group
    {
        ForwardNStats(Stats::Group *parent);

        Stats::Scalar lookups;

        Stats::Scalar hit;

        Stats::Formula hitRate;

        Stats::Scalar pcMiss;

        Stats::Scalar histMiss;

        Stats::Scalar histTakenMiss;
    } stats;

    unsigned int histLength, histTakenLength;

    unsigned int traceStart, traceCount;

    // [pc][histPath][histTaken]
    std::map<Addr,
        std::map<Addr,
            std::map<uint64_t, Addr>>> predictor;

    const Addr invalidPC = 0xFFFFFFFFFFFFFFFFLL;

    std::deque<Addr> lastCtrlsForPred;
    std::deque<Addr> lastCtrlsForUpd;

    uint64_t histTaken;

    unsigned coldStartCount{0};

    struct BPState {

    } state;
};

#endif //GEM5_FORWARD_N_H
