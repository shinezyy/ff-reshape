//
// Created by zyy on 19-8-26.
//

#include <base/intmath.hh>
#include <base/trace.hh>
#include <cpu/fanout_pred.hh>
#include <debug/FanoutPred1.hh>
#include <params/BaseCPU.hh>

FanoutPred::FanoutPred(BaseCPUParams *params)
        : lambda(params->FanoutPredLambda),
          depth(params->FanoutPredTableSize),
          mask(static_cast<unsigned>((1 << ceilLog2(depth)) - 1)),
          history_len(5),
          history_mask((1 << history_len) - 1),
          foldLen(10),
          foldMask((1 <<foldLen) - 1),
          numDiambgFuncs(2),
          diambgTableSize(1024),
          diambgEntryMax(50),
          table(depth, 0)
{
    privTable[0].resize(diambgTableSize, false);
    privTable[1].resize(diambgTableSize, false);
}

void FanoutPred::update(uint64_t pc, unsigned reg_idx, unsigned fanout,
        bool verbose, uint64_t history)
{
    if (!isPossibleLF(pc, reg_idx)) {
        if (fanout > 3) {

        } else {
            return;
        }
    }

    float lmd = lambda;
    if (fanout <= 3) {
        if (fanout > 1) {
            return;
        }
        lmd = lambda / 20;
    }
    if (verbose) {
        DPRINTF(FanoutPred1, "Old prediction for %llu ^ %u is %u, groud truth is %u\n",
                pc, reg_idx, table.at(hash(pc, reg_idx, history)), fanout);
    }
    auto &entry = table.at(hash(pc, reg_idx, history));
    if (entry == 0) {
        entry = fanout;
    } else {
        float new_fanout = lmd * fanout + (float) (1.0 - lmd) * entry;
        assert(new_fanout >= 0);
        entry = static_cast<unsigned int>(new_fanout);
    }

    if (verbose) {
        DPRINTF(FanoutPred1, "New prediction for %llu ^ %u is %u\n",
                pc, reg_idx, table.at(hash(pc, reg_idx, history)));
    }
}

unsigned FanoutPred::lookup(uint64_t pc, unsigned reg_idx, uint64_t history)
{
    unsigned div = 1;
    div += !isPossibleLF(pc, reg_idx);
    return table.at(hash(pc, reg_idx, history)) / div;
}

unsigned FanoutPred::hash(uint64_t pc, unsigned reg_idx, uint64_t history)
{
    pc = pc >> 2;
    return static_cast<unsigned>(((pc) << history_len) | (history & history_mask)) % depth;
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
    return (pc ^ reg_idx) % depth;
}

unsigned FanoutPred::pcFoldHash(uint64_t pc)
{
    return ((pc & foldMask) ^ (pc >> foldLen)) % depth;
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
    if (numPossible >= diambgEntryMax) {
        //clear all
        for (unsigned u = 0; u < numDiambgFuncs; u++) {
            std::fill(privTable[u].begin(), privTable[u].end(), false);
        }
    }

    for (unsigned u = 0; u < numDiambgFuncs; u++) {
        privTable[u][filterHash(u, pc, reg_idx)] = true;
    }
}
