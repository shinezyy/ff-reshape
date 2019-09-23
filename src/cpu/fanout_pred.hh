//
// Created by zyy on 19-8-26.
//

#ifndef GEM5_FANOUT_PRED_HH
#define GEM5_FANOUT_PRED_HH

#include <array>
#include <cstdint>
#include <random>
#include <vector>

#include "cpu/fanout_pred_features.hh"
#include "cpu/pred/sat_counter.hh"

struct BaseCPUParams;

class FanoutPred {
public:

    struct Neuron {
        bool probing{false};

        const uint32_t globalHistoryLen;

        const uint32_t localHistoryLen;
        boost::dynamic_bitset<> localHistory;

        const uint32_t pathLen;

        const uint32_t pathBitsWidth;

        std::vector<SignedSatCounter> weights;

        explicit Neuron (const BaseCPUParams *params);

        int32_t predict(FPFeatures *fp_feat);

        void fit(FPFeatures *fp_feat, bool large);

        int32_t theta;

        // 1 -> 1; 0 -> -1; bool to signed
        static int b2s(bool);

        void dump() const;

        const unsigned pcOffset{2};

        bool extractBit(Addr addr, unsigned bit);

        unsigned fanout{};

        float contrib;
    };

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

    std::vector<Neuron> table;

    std::array<std::vector<bool>, 2> privTable;

    const unsigned fpPathLen;
    const unsigned fpPathBits;

    const unsigned fpGHRLen;
    const unsigned fpLPHLen;

    const unsigned largeFanoutThreshold;

    std::mt19937 gen;

    double overallProfit{0.0};

    double overallReshapeTimes{0.0};

    double expectedProfit{0.0};

    float profitDiscount;

public:
    explicit FanoutPred(BaseCPUParams *params);

    void update(uint64_t pc, unsigned reg_idx, unsigned fanout,
            bool verbose, FPFeatures *fp_feat, int contrib);

    std::tuple<bool, int32_t, float> lookup(
            uint64_t pc, unsigned reg_idx, FPFeatures *fp_feat);

    unsigned hash(uint64_t pc, unsigned reg_idx, FPFeatures *fp_feat);

};


#endif //GEM5_FANOUT_PRED_HH
