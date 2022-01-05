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
          ADD_STAT(incorrect, statistics::units::Count::get(),
                   "Number of ForwardN incorrect predictions"),
          ADD_STAT(correctRatio, statistics::units::Ratio::get(),
                   "ForwardN prediction correct ratio", correct / lookups)
{
    correctRatio.precision(3);
}

ForwardN::ForwardN(const ForwardNParams &params)
        : SimObject(params),
          stats(this)
{
    DPRINTF(ForwardN, "ForwardN is here\n");
}

void ForwardN::predict(const StaticInstPtr &inst, TheISA::PCState &pc) {
    ++stats.lookups;

    DPRINTF(ForwardN, "Predict inst=`%s'\n",
            inst->disassemble(pc.pc())
    );

    pc.advance();
}

void ForwardN::result(const StaticInstPtr &inst,
                      TheISA::PCState &pc,
                      bool correct,
                      const TheISA::PCState &correct_target) {
    if (correct) {
        ++stats.correct;
    } else {
        ++stats.incorrect;
    }

    DPRINTF(ForwardN, "Result of inst=`%s': correct=%c, target=0x%016lX\n",
            inst->disassemble(pc.pc()),
            correct ? 'T' : 'F',
            correct_target.pc()
    );
}

} // namespace branch_prediction
} // namespace gem5
