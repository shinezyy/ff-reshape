#ifndef GEM5_FF_TRIVIAL_H
#define GEM5_FF_TRIVIAL_H

#include <deque>
#include <map>
#include <queue>
#include <random>
#include <utility>

#include "base/statistics.hh"
#include "base/types.hh"
#include "cpu/inst_seq.hh"
#include "cpu/pred/btb.hh"
#include "cpu/pred/ff_bpred_unit.hh"
#include "cpu/pred/tage_base.hh"
#include "cpu/static_inst.hh"
#include "params/FFTrivialBP.hh"
#include "params/TAGE.hh"

/**
 * Implements a local predictor that uses the PC to index into a table of
 * counters.  Note that any time a pointer to the bp_history is given, it
 * should be NULL using this predictor because it does not have any branch
 * predictor state that needs to be recorded or updated; the update can be
 * determined solely by the branch being taken or not taken.
 */
class FFTrivialBP : public FFBPredUnit
{
public:

    FFTrivialBP(const FFTrivialBPParams &params);

    Addr lookup(ThreadID tid, const TheISA::PCState &pc, const StaticInstPtr &inst, void * &bp_history) override;

    void update(ThreadID tid, const TheISA::PCState &pc,
                void *bp_history, bool squashed,
                const StaticInstPtr &inst,
                const TheISA::PCState &pred_DBB, const TheISA::PCState &corr_DBB) override;

    void squash(ThreadID tid, void *bp_history) override;

private:
    TAGEBase *tage;

    struct BPState {
        BPState(TAGEBase &tage) : info(tage.makeBranchInfo())
        {}
        virtual ~BPState()
        {
            delete info;
        }
        TAGEBase::BranchInfo *info;
    };

    unsigned int numLookAhead;

    DefaultBTB BTB;
};

#endif //GEM5_FF_TRIVIAL_H
