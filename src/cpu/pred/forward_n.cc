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
                   "ForwardN prediction hit rate", hit / lookups)
{
    correctRatio.precision(4);
    hitRate.precision(4);
}

ForwardN::ForwardN(const ForwardNParams &params)
        : SimObject(params),
          stats(this),
          traceStart(params.traceStart),
          traceCount(params.traceCount)
{
    DPRINTF(ForwardN, "ForwardN is here\n");

    for (int i = 0; i < 64; i++) {
        pcBefore.push(invalidPC);
        predHist.push(invalidPC);
    }
}

void ForwardN::predict(TheISA::PCState &pc) {
    ++stats.lookups;

    pcBefore.push(pc.pc());

    if (predictor.count(pc.pc())) {
        ++stats.hit;
        pc.pc(predictor[pc.pc()]);
        predHist.push(pc.pc());
    } else {
        predHist.push(invalidPC);
    }
}

void ForwardN::result(const TheISA::PCState &correct_target) {
    Addr pcNBefore = pcBefore.front();
    pcBefore.pop();

    predictor[pcNBefore] = correct_target.pc();

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

} // namespace branch_prediction
} // namespace gem5
