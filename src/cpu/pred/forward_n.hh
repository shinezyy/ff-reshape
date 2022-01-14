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

    void predict(TheISA::PCState &pc, const StaticInstPtr &inst);

    void result(const TheISA::PCState &correct_target,
                const StaticInstPtr &inst,
                const TheISA::PCState &pc);

private:
    static Addr hashHistory(const std::deque<Addr> &history);

    struct ForwardNStats : public statistics::Group
    {
        ForwardNStats(statistics::Group *parent);

        statistics::Scalar lookups;

        statistics::Scalar correct;

        statistics::Formula correctRatio;

        statistics::Scalar hit;

        statistics::Formula hitRate;

        statistics::Scalar pcMiss;

        statistics::Scalar histMiss;
    } stats;

    unsigned int histLength, histTakenLength;

    unsigned int traceStart, traceCount;

    // [pc][histPath][histTaken]
    std::map<Addr,
        std::map<Addr,
            std::map<uint64_t, Addr>>> predictor;

    // (pc, isControl, taken)
    std::queue<std::tuple<Addr, bool, bool>> pcBefore;
    std::queue<Addr> predHist;

    const Addr invalidPC = 0xFFFFFFFFFFFFFFFFLL;

    std::deque<Addr> lastCtrlsForPred;
    std::deque<Addr> lastCtrlsForUpd;

    uint64_t histTaken;
};

} // namespace branch_prediction
} // namespace gem5

#endif //GEM5_FORWARD_N_H
