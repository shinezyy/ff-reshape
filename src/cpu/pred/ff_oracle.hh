/*
 * Created by gzb on 2/8/22.
 */

#ifndef __CPU_PRED_FF_ORACLE_H
#define __CPU_PRED_FF_ORACLE_H

#include <deque>
#include <map>
#include <queue>
#include <random>
#include <utility>

#include "arch/decoder.hh"
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
  FFOracleNemuProxy(int tid, bool nohype);
public:
  uint64_t (*vaddr_read_safe)(uint64_t addr, int len) = NULL;
  uint32_t (*riscv_get_last_exec_inst)() = NULL;
};

class FFOracleBP : public FFBPredUnit
{
public:
    FFOracleBP(const FFOracleBPParams &params);

    Addr lookup(ThreadID tid, Addr instPC, void * &bp_history) override;

    void update(ThreadID tid, const TheISA::PCState &thisPC,
                void *bp_history, bool squashed,
                const StaticInstPtr &inst, Addr pred_nextK_PC, Addr corr_nextK_PC) override;

    void squash(ThreadID tid, void *bp_history) override;

    void syncStoreConditional(bool lrValid, ThreadID tid) override;

    void syncArchState(Addr resetPC, uint64_t pmemAddr, void *pmemPtr, size_t pmemSize, const void *regs) override;

    void initNEMU(const DerivO3CPUParams &params) override;

    bool isOracle() const override { return true; }

  private:
    void nemuStep();

  private:
    unsigned int numLookAhead;
    double presetAccuracy;

    uint32_t diff_wdst[DIFFTEST_WIDTH];
    uint64_t diff_wdata[DIFFTEST_WIDTH];
    uint64_t diff_wpc[DIFFTEST_WIDTH];
    uint64_t nemu_reg[DIFFTEST_NR_REG];
    DiffState diff;
    FFOracleNemuProxy *proxy;
    uint64_t oracleIID;
    bool inited;

    struct OracleEntry {
        uint64_t iid;
        Addr pc;
        bool scInFlight;
    };
    std::list<OracleEntry> orderedOracleEntries;

    using EntryIter = std::list<OracleEntry>::iterator;
    EntryIter frontPointer;

    struct BPState {
        uint64_t commit_iid;
        uint64_t front_iid;
        Addr instPC;
        bool scInFlight;
    };
    BPState state;

    std::default_random_engine randGen;
    std::bernoulli_distribution bdGen;

    bool scInFlight;
    struct SCBreakPoint {
        BPState bpState;
        uint64_t reg[DIFFTEST_NR_REG];
    };
    SCBreakPoint scBreakpoint;
    uint64_t numInstAfterSC{0};

    TheISA::Decoder *decoder;

    struct FFOracleBPStats : public Stats::Group
    {
        FFOracleBPStats(Stats::Group *parent);

        Stats::Scalar incorrectAfterSC;

    } stats;

private:
    void reset();
    void lookAheadInsts(unsigned len, bool record);
    void advanceFront();
    void syncFront();
    void dumpState();
};

#endif // __CPU_PRED_FF_ORACLE_H
