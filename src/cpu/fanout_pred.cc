//
// Created by zyy on 19-8-26.
//

#include <base/intmath.hh>
#include <cpu/fanout_pred.hh>
#include <params/BaseCPU.hh>

FanoutPred::FanoutPred(BaseCPUParams *params)
        : lambda(params->FanoutPredLambda),
          depth(params->FanoutPredTableSize),
          mask(static_cast<unsigned>((1 << ceilLog2(depth)) - 1)),
          table(depth, 0)
{

}

void FanoutPred::update(uint64_t pc, unsigned reg_idx, unsigned fanout)
{
    auto &entry = table.at(hash(pc, reg_idx));
    if (entry == 0) {
        entry = fanout;
    } else {
        float new_fanout = lambda * fanout + (float) (1.0 - lambda) * entry;
        assert(new_fanout >= 0);
        entry = static_cast<unsigned int>(new_fanout);
    }
}

unsigned FanoutPred::lookup(uint64_t pc, unsigned reg_idx)
{
    return table.at(hash(pc, reg_idx));
}

unsigned FanoutPred::hash(uint64_t pc, unsigned reg_idx)
{
    return static_cast<unsigned>((pc ^ reg_idx)) % depth;
}
