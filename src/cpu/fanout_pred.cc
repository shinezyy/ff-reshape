//
// Created by zyy on 19-8-26.
//
#include "cpu/fanout_pred.hh"

#include <cmath>
#include <random>
#include <debug/FanoutPred.hh>

#include "base/intmath.hh"
#include "base/trace.hh"
#include "debug/FanoutPred1.hh"
#include "debug/Reshape2.hh"
#include "params/BaseCPU.hh"

using namespace boost;


FanoutPred::FanoutPred(BaseCPUParams *params)
        : lambda(params->FanoutPredLambda),
          depth(params->FanoutPredTableSize),
          mask(static_cast<unsigned>((1 << ceilLog2(depth)) - 1)),
          history_len(5),
          history_mask((1 << history_len) - 1),
          foldLen(10),
          foldMask((1 <<foldLen) - 1),
          numDiambgFuncs(2),
          disambgTableSize(2048),
          disambgEntryMax(400),
          table(depth, Neuron(params)),

          fpPathLen(params->FPPathLen),
          fpPathBits(params->FPPathBits),
          fpGHRLen(params->FPGHRLen),
          fpLPHLen(params->FPLPHLen),
          largeFanoutThreshold(params->LargeFanoutThreshold),
          gen(0xdeadbeaf),
          profitDiscount(params->ProfitDiscount)
{
    privTable[0].resize(disambgTableSize, false);
    privTable[1].resize(disambgTableSize, false);
}

void FanoutPred::update(uint64_t pc, unsigned reg_idx, unsigned fanout,
        bool verbose, FPFeatures *fp_feat, int contrib)
{
    if (!isPossibleLF(pc, reg_idx)) {
        if (fanout > 3) {
            markAsPossible(pc, reg_idx);
        } else {
            return;
        }
    }

    const auto &ghr = fp_feat->globalBranchHist;
    auto index = hash(pc, reg_idx, fp_feat);
    auto &entry = table.at(index);

    DPRINTF(Reshape2, "Updating hash slot[%u]\n", index);
    if (entry.probing && verbose) {
        DPRINTF(FanoutPred1, "Updating hash slot[%u]\n", index);
        DPRINTF(FanoutPred1, "Old prediction for [0x%llx ^ %u, history: 0x%lx]"
                " is %u, groud truth is %u\n",
                pc, reg_idx, ghr.to_ulong(), fp_feat->predValue, fanout);
    }

    entry.fit(fp_feat, fanout > largeFanoutThreshold);
//    entry.predict(history->globalHistory);
//    if (entry.probing && Debug::ZPerceptron) {
//        std::cout << "New local: " << entry.localHistory << std::endl;
//    }
    if (entry.probing && verbose) {
        DPRINTF(FanoutPred1, "New prediction for [0x%llx ^ %u, history: 0x%lx]"
                " is %u\n",
                pc, reg_idx, ghr.to_ulong(), entry.predict(fp_feat));
    }

    if (entry.predict(fp_feat) >= 0) {
        // update value predictor
        float lmd = lambda;
        if (entry.fanout == 0) {
            entry.fanout = fanout;
        } else {
            float new_fanout = lmd * fanout + (float) (1.0 - lmd) * entry.fanout;
            assert(new_fanout >= 0);
            entry.fanout = round(new_fanout);
        }
    }
    if (fp_feat->pred && fp_feat->predProfit) {
        float contrib_lmd = 0.5;
        entry.contrib = contrib_lmd * contrib + (float) (1.0 - contrib_lmd) * entry.contrib;
        DPRINTF(Reshape2, "new contrib: %f\n", entry.contrib);

        overallReshapeTimes += 1;
        overallProfit += contrib;
        if (overallReshapeTimes >= 1) {
            expectedProfit = overallProfit / overallReshapeTimes;
        }
    }
}

unsigned FanoutPred::hash(uint64_t pc, unsigned reg_idx, FPFeatures *fp_feat)
{
    pc = pc >> 2;
    return static_cast<unsigned>((pc ^ fp_feat->lastCallSite) % depth);
    // return static_cast<unsigned>(pc) % depth;
}

unsigned FanoutPred::filterHash(int funcID, uint64_t pc, unsigned reg_idx)
{
    if (funcID == 0) {
        return pcRegHash(pc, reg_idx);
    } else if (funcID == 1) {
        return pcFoldHash(pc);
    }
    panic("Func ID %i not implemented\n", funcID);
}

unsigned FanoutPred::pcRegHash(uint64_t pc, unsigned reg_idx)
{
    return (pc ^ reg_idx) % disambgTableSize;
}

unsigned FanoutPred::pcFoldHash(uint64_t pc)
{
    return ((pc & foldMask) ^ (pc >> foldLen)) % disambgTableSize;
}

bool FanoutPred::isPossibleLF(uint64_t pc, unsigned reg_idx)
{
    bool flag = true;
    for (unsigned u = 0; u < numDiambgFuncs; u++) {
        flag &= privTable[u][filterHash(u, pc, reg_idx)];
    }
    return flag;
}

void FanoutPred::markAsPossible(uint64_t pc, unsigned reg_idx)
{
    if (numPossible >= disambgEntryMax) {
        //clear all
        numPossible = 0;
        // std::random_device rd;
        // std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 100);
        for (unsigned u = 0; u < numDiambgFuncs; u++) {
            for (unsigned i = 0; i < privTable[u].size(); i++) {
                privTable[u][i] = (dis(gen) > 10) & privTable[u][i];
                numPossible += privTable[u][i];
            }
            // std::fill(privTable[u].begin(), privTable[u].end(), false);
        }
    }

    for (unsigned u = 0; u < numDiambgFuncs; u++) {
        privTable[u][filterHash(u, pc, reg_idx)] = true;
        numPossible++;
    }
}

std::tuple<bool, int32_t, float>
FanoutPred::lookup(
        uint64_t pc, unsigned reg_idx, FPFeatures *fp_feat)
{
    if (!isPossibleLF(pc, reg_idx)) {
        return std::make_tuple(false, 1, 0.0);
    }
    uint32_t index = hash(pc, reg_idx, fp_feat);
    dynamic_bitset<> &ghr = fp_feat->globalBranchHist;
    Neuron &entry = table.at(index);

    DPRINTF(Reshape2, "Looking up hash slot %i\n",index);

    if (entry.probing && Debug::FanoutPred1) {
        std::cout << "Using local: " << entry.localHistory
                  << ", global: " << ghr << std::endl;
    }

    int32_t prediction = entry.predict(fp_feat);
    bool result = prediction >= 0;

    if (result && entry.contrib) {
        DPRINTF(Reshape2, "Predicted to be LF with contrib %f\n", entry.contrib);
    }

    return std::make_tuple(result, entry.fanout, entry.contrib +
            expectedProfit/profitDiscount);
}

FanoutPred::Neuron::Neuron(const BaseCPUParams *params) :
        globalHistoryLen(params->FPGHRLen),
        localHistoryLen(params->FPLPHLen),
        localHistory(localHistoryLen),
        pathLen(params->FPPathLen),
        pathBitsWidth(params->FPPathBits),
        weights(globalHistoryLen + localHistoryLen + pathLen*pathBitsWidth + 1,
                SignedSatCounter(params->FPCtrBits, 0)),
        theta(static_cast<int32_t>(
                1.93 * (globalHistoryLen + localHistoryLen + pathLen*pathBitsWidth))),
        contrib(1.001)
{

}

int32_t FanoutPred::Neuron::predict(FPFeatures *fp_feat)
{
    int32_t sum = weights.back().read();

    for (uint32_t i = 0; i < globalHistoryLen; i++) {
        sum += b2s(fp_feat->globalBranchHist[i]) * weights[i].read();
    }

    for (uint32_t i = 0; i < localHistoryLen; i++) {
        sum += b2s(fp_feat->localBranchHist[i]) *
                weights[globalHistoryLen + i].read();
    }

    for (unsigned p = 0; p < pathLen; p++) {
        for (unsigned b = 0; b < pathBitsWidth; b++) {
            sum += b2s(extractBit(fp_feat->pastPCs[p], b))*
            weights[globalHistoryLen + localHistoryLen + p*pathBitsWidth + b].read();
        }
    }
    return sum;
}

void FanoutPred::Neuron::dump() const
{

}

void FanoutPred::Neuron::fit(FPFeatures *fp_feat, bool large)
{
    if (fp_feat->pred == large &&
        abs(fp_feat->predValue) > theta) {
        return;
    }

    if (probing) {
        DPRINTFR(FanoutPred1, "Old prediction: %d, theta: %d\n",
                 fp_feat->predValue, theta);
    }
    if (large) {
        weights.back().increment();
    } else {
        weights.back().decrement();
    }

    const auto &ghr = fp_feat->globalBranchHist;

    for (uint32_t i = 0; i < globalHistoryLen; i++) {
        weights[i].add(b2s(large) * b2s(ghr[i]));
    }

    for (uint32_t i = 0; i < localHistoryLen; i++) {
        weights[globalHistoryLen + i].add(b2s(large) * b2s(localHistory[i]));
    }

    for (unsigned p = 0; p < pathLen; p++) {
        for (unsigned b = 0; b < pathBitsWidth; b++) {
            weights[globalHistoryLen + localHistoryLen + p*pathBitsWidth + b].add(
                    b2s(large) * b2s(extractBit(fp_feat->pastPCs[p], b)));
        }
    }
}

int FanoutPred::Neuron::b2s(bool large)
{
    // 1 -> 1; 0 -> -1
    return (large << 1) - 1;
}

bool FanoutPred::Neuron::extractBit(Addr addr, unsigned bit)
{
    return static_cast<bool>((addr >> (pcOffset + bit)) & 1);
}
