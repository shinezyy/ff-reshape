//
// Created by zyy on 19-8-26.
//
#include "cpu/fanout_pred.hh"

#include <cmath>
#include <random>

#include "base/intmath.hh"
#include "base/trace.hh"
#include "debug/FanoutPred1.hh"
#include "params/BaseCPU.hh"

FanoutPred::FanoutPred(BaseCPUParams *params)
        : lambda(params->FanoutPredLambda),
          depth(params->FanoutPredTableSize),
          mask(static_cast<unsigned>((1 << ceilLog2(depth)) - 1)),
          history_len(5),
          history_mask((1 << history_len) - 1),
          foldLen(10),
          foldMask((1 <<foldLen) - 1),
          numDiambgFuncs(2),
          disambgTableSize(1024),
          disambgEntryMax(200),
          table(depth, 0)
{
    privTable[0].resize(disambgTableSize, false);
    privTable[1].resize(disambgTableSize, false);
}

void FanoutPred::update(uint64_t pc, unsigned reg_idx, unsigned fanout,
        bool verbose, uint64_t history)
{
    if (!isPossibleLF(pc, reg_idx)) {
        if (fanout > 3) {
            markAsPossible(pc, reg_idx);
        } else {
            return;
        }
    }

    float lmd = lambda;
    // if (fanout <= 3) {
    //     lmd = lambda / 5;
    // }
    if (verbose) {
        DPRINTF(FanoutPred1, "Updating hash slot[%u]\n", hash(pc, reg_idx, history));
        DPRINTF(FanoutPred1, "Old prediction for [0x%llx ^ %u, history: 0x%x]"
                " is %u, groud truth is %u\n",
                pc, reg_idx, history & history_mask,
                table.at(hash(pc, reg_idx, history)), fanout);
    }
    auto &entry = table.at(hash(pc, reg_idx, history));
    if (entry == 0) {
        entry = fanout;
    } else {
        float new_fanout = lmd * fanout + (float) (1.0 - lmd) * entry;
        assert(new_fanout >= 0);
        entry = round(new_fanout);
    }

    if (verbose) {
        DPRINTF(FanoutPred1, "New prediction for [0x%llx ^ %u, history: 0x%x]"
                " is %u\n",
                pc, reg_idx, history & history_mask,
                table.at(hash(pc, reg_idx, history)));
    }
}

unsigned FanoutPred::lookup(uint64_t pc, unsigned reg_idx, uint64_t history)
{
    if (true) {
        return table.at(hash(pc, reg_idx, history));
    } else {
        return 1;
    }
}

unsigned FanoutPred::hash(uint64_t pc, unsigned reg_idx, uint64_t history)
{
    pc = pc >> 2;
    return static_cast<unsigned>(pc ^ (history & history_mask)) % depth;
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
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 10);
        for (unsigned u = 0; u < numDiambgFuncs; u++) {
            for (unsigned i = 0; i < privTable[u].size(); i++) {
                privTable[u][i] = (dis(gen) > 1) & privTable[u][i];
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
