//
// Created by zyy on 19-8-26.
//

#ifndef GEM5_FANOUT_PRED_HH
#define GEM5_FANOUT_PRED_HH

#include <array>
#include <cstdint>
#include <vector>

#include "cpu/fanout_pred_features.hh"

struct BaseCPUParams;

class FanoutPred {
private:
    const float lambda;

    const unsigned depth;

    const unsigned mask;

    const unsigned history_len;

    const unsigned history_mask;

    unsigned filterHash(int funcID, uint64_t pc, unsigned reg_idx);

    bool isPossibleLF(uint64_t pc, unsigned reg_idx);

    void markAsPossible(uint64_t pc, unsigned reg_idx);

    unsigned pcRegHash(uint64_t pc, unsigned reg_idx);

    unsigned pcFoldHash(uint64_t pc);

    const unsigned foldLen;

    const unsigned foldMask;

    const unsigned numDiambgFuncs;

    const unsigned disambgTableSize;

    const unsigned disambgEntryMax;

    unsigned numPossible{};

    std::vector<unsigned> table;

    std::array<std::vector<bool>, 2> privTable;

public:
    explicit FanoutPred(BaseCPUParams *params);

    void update(uint64_t pc, unsigned reg_idx, unsigned fanout,
            bool verbose, uint64_t history);

    unsigned lookup(uint64_t pc, unsigned reg_idx, uint64_t history);

    unsigned hash(uint64_t pc, unsigned reg_idx, uint64_t history);
};


#endif //GEM5_FANOUT_PRED_HH
