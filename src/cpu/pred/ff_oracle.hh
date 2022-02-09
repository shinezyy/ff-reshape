/*
 * Created by gzb on 2/8/22.
 */

#ifndef __CPU_PRED_FF_ORACLE_H
#define __CPU_PRED_FF_ORACLE_H

#include <deque>
#include <map>
#include <queue>
#include <utility>

#include "base/statistics.hh"
#include "base/types.hh"
#include "cpu/difftest.hh"
#include "cpu/inst_seq.hh"
#include "cpu/pred/ff_bpred_unit.hh"
#include "cpu/static_inst.hh"
#include "params/FFOracleBP.hh"
#include "sim/sim_object.hh"

class FFOracleNemuProxy : public RefProxy {
public:
  FFOracleNemuProxy(int coreid);
  FFOracleNemuProxy(int tid, bool nohype);
private:
};

class FFOracleBP : public FFBPredUnit
{
public:
    FFOracleBP(const FFOracleBPParams &params);

    Addr lookup(ThreadID tid, Addr instPC, void * &bp_history) override;

    void update(ThreadID tid, Addr instPC,
                void *bp_history, bool squashed,
                const StaticInstPtr &inst, Addr corr_nextK_PC) override;

    void squash(ThreadID tid, void *bp_history) override;

    void syncArchState(uint64_t pmemAddr, void *pmemPtr, size_t pmemSize, void *regs) override;

    void initNEMU(const DerivO3CPUParams &params) override;

  private:
    unsigned int numLookAhead;

    uint32_t diff_wdst[DIFFTEST_WIDTH];
    uint64_t diff_wdata[DIFFTEST_WIDTH];
    uint64_t diff_wpc[DIFFTEST_WIDTH];
    uint64_t nemu_reg[DIFFTEST_NR_REG];
    DiffState diff;
    FFOracleNemuProxy *proxy;
    uint64_t oracleIID;

    struct OracleEntry {
        uint64_t iid;
        Addr npc;
    };
    std::list<OracleEntry> orderedOracleEntries;

    using EntryIter = std::list<OracleEntry>::iterator;
    EntryIter frontPointer;

    struct BPState {
        uint64_t commit_iid;
        uint64_t front_iid;
        Addr pred_nextK_PC;
    };
    BPState state;

private:
    void reset();
    EntryIter lookAheadInsts();
    void advanceFront();
    void syncFront();
    void dumpState();
};

#endif // __CPU_PRED_FF_ORACLE_H
