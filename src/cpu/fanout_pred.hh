//
// Created by zyy on 19-8-26.
//

#ifndef GEM5_FANOUT_PRED_HH
#define GEM5_FANOUT_PRED_HH

#include <cstdint>
#include <vector>

struct BaseCPUParams;

class FanoutPred {
private:
    const float lambda;

    const unsigned depth;

    const unsigned mask;

    std::vector<unsigned> table;

public:
    explicit FanoutPred(BaseCPUParams *params);

    void update(uint64_t pc, unsigned reg_idx, unsigned fanout);

    unsigned lookup(uint64_t pc, unsigned reg_idx);

    unsigned hash(uint64_t pc, unsigned reg_idx);
};


#endif //GEM5_FANOUT_PRED_HH
