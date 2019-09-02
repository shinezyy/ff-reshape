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

    void update(uint64_t pc, unsigned reg_idx, unsigned fanout,
            bool verbose, uint64_t history);

    unsigned lookup(uint64_t pc, unsigned reg_idx, uint64_t history);

    unsigned hash(uint64_t pc, unsigned reg_idx, uint64_t history);
};


#endif //GEM5_FANOUT_PRED_HH
