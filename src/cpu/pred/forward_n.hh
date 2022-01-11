//
// Created by yqszxx on 1/4/22.
//

#ifndef GEM5_FORWARD_N_H
#define GEM5_FORWARD_N_H

#include <map>
#include <queue>

#include "base/statistics.hh"
#include "base/types.hh"
#include "cpu/inst_seq.hh"
#include "cpu/static_inst.hh"
#include "params/ForwardN.hh"
#include "sim/sim_object.hh"

namespace gem5
{

namespace branch_prediction
{

/**
 * Implements a local predictor that uses the PC to index into a table of
 * counters.  Note that any time a pointer to the bp_history is given, it
 * should be NULL using this predictor because it does not have any branch
 * predictor state that needs to be recorded or updated; the update can be
 * determined solely by the branch being taken or not taken.
 */
class ForwardN : public SimObject
{
public:

    ForwardN(const ForwardNParams &params);

    void predict(TheISA::PCState &pc);

    void result(const TheISA::PCState &correct_target);

private:
    struct ForwardNStats : public statistics::Group
    {
        ForwardNStats(statistics::Group *parent);

        statistics::Scalar lookups;

        statistics::Scalar correct;

        statistics::Formula correctRatio;

        statistics::Scalar hit;

        statistics::Formula hitRate;
    } stats;

    std::map<Addr, Addr> predictor;
    std::queue<Addr> pcBefore;
    std::queue<Addr> predHist;

    const Addr invalidPC = 0xFFFFFFFFFFFFFFFFLL;
};

} // namespace branch_prediction
} // namespace gem5

#endif //GEM5_FORWARD_N_H
