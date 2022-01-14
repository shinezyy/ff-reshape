//
// Created by yqszxx on 1/4/22.
//

#include "debug/ForwardN.hh"
#include "forward_n.hh"

namespace gem5
{

namespace branch_prediction
{

ForwardN::ForwardNStats::ForwardNStats(statistics::Group *parent)
        : statistics::Group(parent, "forward_n"),
          ADD_STAT(lookups, statistics::units::Count::get(),
                   "Number of ForwardN lookups"),
          ADD_STAT(correct, statistics::units::Count::get(),
                   "Number of ForwardN correct predictions"),
          ADD_STAT(correctRatio, statistics::units::Ratio::get(),
                   "ForwardN prediction correct ratio", correct / lookups),
          ADD_STAT(hit, statistics::units::Count::get(),
                   "Number of ForwardN hit"),
          ADD_STAT(hitRate, statistics::units::Ratio::get(),
                   "ForwardN prediction hit rate", hit / lookups),
          ADD_STAT(pcMiss, statistics::units::Count::get(),
                   "ForwardN prediction miss count caused by current pc"),
          ADD_STAT(histMiss, statistics::units::Count::get(),
                   "ForwardN prediction miss count caused by history hash")
{
    correctRatio.precision(4);
    hitRate.precision(4);
}

ForwardN::ForwardN(const ForwardNParams &params)
        : SimObject(params),
          stats(this),
          histLength(params.histLength),
          histTakenLength(params.histTakenLength),
          traceStart(params.traceStart),
          traceCount(params.traceCount),
          histTaken(0)
{
    DPRINTF(ForwardN, "ForwardN, N=64, "
                      "histLength=%u, "
                      "histTakenLength=%u\n",
                      histLength,
                      histTakenLength
                      );

    for (int i = 0; i < 64; i++) {
        pcBefore.push(std::make_tuple(invalidPC, false, false));
        predHist.push(invalidPC);
    }

    for (int i = 0; i < histLength; i++) {
        lastCtrlsForPred.push_back(invalidPC);
        lastCtrlsForUpd.push_back(invalidPC);
    }
}

void ForwardN::predict(TheISA::PCState &pc, const StaticInstPtr &inst) {
    ++stats.lookups;

    Addr lastPCsHash = hashHistory(lastCtrlsForPred);

    Addr oldPC = pc.pc();
    if (predictor.count(pc.pc())) {
        if (predictor[pc.pc()].count(lastPCsHash)) {
            ++stats.hit;
            pc.pc(predictor[pc.pc()][lastPCsHash][histTaken]);
            predHist.push(pc.pc());
        } else {
            ++stats.histMiss;
            predHist.push(invalidPC);
        }
    } else {
        ++stats.pcMiss;
        predHist.push(invalidPC);
    }

    if (inst->isControl()) {
        lastCtrlsForPred.push_back(oldPC);
        lastCtrlsForPred.pop_front();
    }
}

void ForwardN::result(const TheISA::PCState &correct_target,
                      const StaticInstPtr &inst,
                      const TheISA::PCState &pc) {
    pcBefore.push(std::make_tuple(pc.pc(), inst->isControl(), pc.branching()));

    Addr pcNBefore = std::get<0>(pcBefore.front());
    bool isControlNBefore = std::get<1>(pcBefore.front());
    bool isBranchingNBefore = std::get<2>(pcBefore.front());
    pcBefore.pop();

    Addr lastPCsHash = hashHistory(lastCtrlsForUpd);

    static uint64_t histTakenUpd = 0;

    predictor[pcNBefore][lastPCsHash][histTakenUpd] = correct_target.pc();

    if (inst->isControl()) {
        histTaken <<= 1;
        histTaken |= pc.branching();
        histTaken &= ((1 << histTakenLength) - 1);
    }

    if (isControlNBefore) {
        lastCtrlsForUpd.push_back(pcNBefore);
        lastCtrlsForUpd.pop_front();

        histTakenUpd <<= 1;
        histTakenUpd |= isBranchingNBefore;
        histTakenUpd &= ((1 << histTakenLength) - 1);
    }

    Addr prediction = predHist.front();
    predHist.pop();
    if (prediction == correct_target.pc()) {
        ++stats.correct;
    } else {
        static int c = 0;
        if (c >= traceStart && c < traceStart + traceCount) {
            DPRINTF(ForwardN, "Mispred: pred=0x%016lX, act=0x%016lX, off=%d\n",
                    prediction,
                    correct_target.pc(),
                    correct_target.pc() > prediction ?
                        (signed int)(correct_target.pc() - prediction) :
                        -(signed int)(prediction - correct_target.pc())
                    );
        }
        c++;
    }
}

Addr ForwardN::hashHistory(const std::deque<Addr> &history) {
    Addr hash = 0;
    std::for_each(
            history.begin(),
            history.end(),
            [&hash](Addr a) {
                hash ^= a;
            });
    return hash;
}

} // namespace branch_prediction
} // namespace gem5
