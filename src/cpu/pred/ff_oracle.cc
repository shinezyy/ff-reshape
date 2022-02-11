/*
 * Created by gzb on 2/8/22.
 */

#include "cpu/pred/ff_oracle.hh"

#include <dlfcn.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <random>

#include "base/trace.hh"
#include "cpu/nemu_common.hh"
#include "debug/FFOracleBP_lookup.hh"
#include "debug/FFOracleBP_misc.hh"
#include "debug/FFOracleBP_squash.hh"
#include "debug/FFOracleBP_update.hh"

#ifndef REF_SO
# error Please define REF_SO to the path of NEMU shared object file
#endif

FFOracleBP::FFOracleBP(const FFOracleBPParams &params)
    : FFBPredUnit(params),
        numLookAhead(getNumLookAhead()),
        presetAccuracy(params.presetAccuracy),
        proxy(nullptr),
        oracleIID(0),
        inited(false),
        randGen(params.randNumSeed),
        bdGen(params.presetAccuracy)
{
    assert(numLookAhead >= 1);
    assert(0 <= params.presetAccuracy && params.presetAccuracy <= 1);
    DPRINTF(FFOracleBP_misc, "Look ahead: %u insts. Preset accuracy: %f%%.\n",
                    numLookAhead,
                    params.presetAccuracy * 100);
    state.commit_iid = numLookAhead - 1;
    state.front_iid = numLookAhead - 1;
    reset();
}

void FFOracleBP::reset()
{
    frontPointer = orderedOracleEntries.begin();
}

void FFOracleBP::initNEMU(const DerivO3CPUParams &params)
{
    assert(!proxy);
    diff.nemu_reg = nemu_reg;
    diff.cpu_id = params.cpu_id;
    // Use a different TID for each NEMU instance,
    // because of the fixed virtual address mapping for pmem inside NEMU.
    proxy = new FFOracleNemuProxy(params.numThreads + params.cpu_id,true);
    diff.dynamic_config.ignore_illegal_mem_access = true; // FIXME: how about mmio?
    diff.dynamic_config.debug_difftest = false;
    proxy->update_config(&diff.dynamic_config);
}

void FFOracleBP::syncArchState(Addr resetPC, paddr_t pmemAddr, void *pmemPtr, size_t pmemSize, const void *regs)
{
    DPRINTF(FFOracleBP_misc, "Will start memcpy to NEMU\n");
    proxy->memcpy(pmemAddr, pmemStart+pmemSize*diff.cpu_id, pmemSize, DUT_TO_REF);

    DPRINTF(FFOracleBP_misc, "Will start regcpy to NEMU\n");
    uint64_t tmp_regs[DIFFTEST_NR_REG];
    proxy->regcpy(tmp_regs, REF_TO_DUT);
    memcpy(tmp_regs, regs, sizeof(uint64_t) * 32);
    diff.nemu_this_pc = resetPC;
    tmp_regs[DIFFTEST_THIS_PC] = resetPC;
    proxy->regcpy(tmp_regs, DUT_TO_REF);

    DPRINTF(FFOracleBP_misc, "reset PC = %#x.\n", resetPC);
}

Addr FFOracleBP::lookup(ThreadID tid, Addr instPC, void * &bp_history) {
    Addr predPC;
    assert(tid == 0);

    auto hist = new BPState(state);
    bp_history = hist;

    advanceFront();
    syncFront();

    if (bdGen(randGen)) {
        predPC = frontPointer->pc;
    } else {
        // Generate wrong prediction
        predPC = 0x0;
    }
    hist->instPC = instPC;

    DPRINTF(FFOracleBP_lookup, "+ lookup PC=%#x hist_iid=%u next-%u PC=%#x\n",
                        instPC, hist->front_iid, numLookAhead, predPC);
    return predPC;
}

void FFOracleBP::update(ThreadID tid, Addr instPC,
                        void *bp_history, bool squashed,
                        const StaticInstPtr &inst, Addr corr_nextK_PC) {

    auto history_state = static_cast<BPState *>(bp_history);

    DPRINTF(FFOracleBP_update, "- update%s PC=%x hist_iid=%u\n",
                                squashed ? " squashed" : " ", instPC,
                                history_state->front_iid);

    if (squashed) {
        // do nothing
    } else {
        DPRINTF(FFOracleBP_misc, "Committing iid %u\n", history_state->front_iid + 1);
        DPRINTF(FFOracleBP_misc, "Back iid = %u\n",
                orderedOracleEntries.back().iid);

        assert(history_state->front_iid + 1 == orderedOracleEntries.back().iid);

        assert(!orderedOracleEntries.empty());
        orderedOracleEntries.pop_back();

        DPRINTF(FFOracleBP_misc, "Oracle table size: %lu.\n", orderedOracleEntries.size());

        state.commit_iid++;
        dumpState();
    }
}

void FFOracleBP::squash(ThreadID tid, void *bp_history)
{
    auto history_state = static_cast<BPState *>(bp_history);

    // Am I a perfect predictor?
    if (std::fabs(presetAccuracy - 1.0) < std::numeric_limits<double>::epsilon()) {
        warn("Oracle BP fault: mispredicted @ %#x (iid=%u).\n",
                history_state->instPC, history_state->front_iid + 1);
    }

    DPRINTF(FFOracleBP_squash, "- squash hist_iid=%lu\n", history_state->front_iid);

    if (history_state->front_iid < state.front_iid) {
        state.front_iid = history_state->front_iid;

        DPRINTF(FFOracleBP_misc, "After squash\n");
        dumpState();

        for (frontPointer = orderedOracleEntries.begin();
                frontPointer != orderedOracleEntries.end();
                frontPointer++) {
            if (frontPointer->iid == state.front_iid) {
                break;
            }
        }
        if (frontPointer == orderedOracleEntries.end()) {
            assert(orderedOracleEntries.empty() ||
                    orderedOracleEntries.back().iid == state.front_iid + 1 ||
                    state.front_iid == numLookAhead - 2);
        }
        dumpState();
    } else {
        DPRINTF(FFOracleBP_misc, "recorded front iid = %u, state.front_iid = %u\n",
                history_state->front_iid, state.front_iid);
    }
    delete history_state;
}

void FFOracleBP::lookAheadInsts(unsigned len)
{
    DPRINTF(FFOracleBP_misc, "Start feeding %i insts.\n", len);
    for (unsigned i = 0; i < len; i++) {
        DPRINTF(FFOracleBP_misc, "Got PC [sn%lu]: %#x\n", oracleIID, diff.nemu_this_pc);
        orderedOracleEntries.push_front(OracleEntry{oracleIID, diff.nemu_this_pc});
        oracleIID++;

        proxy->exec(1);
        proxy->regcpy(diff.nemu_reg,REF_TO_DIFFTEST);
        diff.nemu_this_pc = diff.nemu_reg[DIFFTEST_THIS_PC];
    }
    assert(orderedOracleEntries.size() < 30000);
}

void FFOracleBP::advanceFront() {
    state.front_iid++;
}

void FFOracleBP::syncFront() {
    DPRINTF(FFOracleBP_misc, "syncFront\n");
    DPRINTF(FFOracleBP_misc, "Oracle table size: %lu.\n", orderedOracleEntries.size());

    if (!inited) {
        lookAheadInsts(numLookAhead);
        orderedOracleEntries.clear();
        inited = true;
    }

    if (orderedOracleEntries.size() == 0) {
        lookAheadInsts(1);
        frontPointer = orderedOracleEntries.begin();
        assert(state.front_iid == frontPointer->iid);
        dumpState();

    } else if (state.front_iid > orderedOracleEntries.front().iid) {
        // younger, must feed more
        DPRINTF(FFOracleBP_misc, "before state.front_iid=%lu frontPointer->iid=%lu.\n",
                                    state.front_iid, frontPointer->iid);
        lookAheadInsts(1);
        frontPointer = orderedOracleEntries.begin();
        assert(state.front_iid == frontPointer->iid);
        dumpState();

    } else {
        if (frontPointer == orderedOracleEntries.end() ||
                state.front_iid != frontPointer->iid) {

            frontPointer = orderedOracleEntries.begin();

            while (frontPointer != orderedOracleEntries.end() &&
                    state.front_iid != frontPointer->iid) {
                frontPointer++;
            }

            dumpState();

            assert(frontPointer != orderedOracleEntries.end());
        }
    }
}


void FFOracleBP::dumpState()
{
    if (orderedOracleEntries.size() > 0) {
        DPRINTF(FFOracleBP_misc, "commit iid: %lu, front iid: %lu, frontPointer iid: %lu, "
                "table back: %lu, table front: %lu\n",
                state.commit_iid, state.front_iid, frontPointer->iid,
                orderedOracleEntries.back().iid,
                orderedOracleEntries.front().iid
               );
    } else {
        DPRINTF(FFOracleBP_misc, "commit iid: %lu, front iid: %lu, frontPointer iid: %lu\n",
                state.commit_iid, state.front_iid, frontPointer->iid
               );
    }
}


//---------------------------------------------------------------------------------
//---------------------------------------------------------------------------------
// **** FFOracleNemuProxy ****
//---------------------------------------------------------------------------------
//---------------------------------------------------------------------------------

FFOracleNemuProxy::FFOracleNemuProxy(int coreid) {

  void *handle = dlmopen(LM_ID_NEWLM, REF_SO, RTLD_LAZY | RTLD_DEEPBIND);
  puts("Using " REF_SO " for oracle BP");
  if (!handle){
    printf("%s\n", dlerror());
    assert(0);
  }

  this->memcpy = (void (*)(paddr_t, void *, size_t, bool))dlsym(handle, "difftest_memcpy");
  assert(this->memcpy);

  regcpy = (void (*)(void *, bool))dlsym(handle, "difftest_regcpy");
  assert(regcpy);

  csrcpy = (void (*)(void *, bool))dlsym(handle, "difftest_csrcpy");
  assert(csrcpy);

  uarchstatus_cpy = (void (*)(void *, bool))dlsym(handle, "difftest_uarchstatus_cpy");
  assert(uarchstatus_cpy);

  exec = (void (*)(uint64_t))dlsym(handle, "difftest_exec");
  assert(exec);

  guided_exec = (vaddr_t (*)(void *))dlsym(handle, "difftest_guided_exec");
  assert(guided_exec);

  update_config = (vaddr_t (*)(void *))dlsym(handle, "update_dynamic_config");
  assert(update_config);

  store_commit = (int (*)(uint64_t*, uint64_t*, uint8_t*))dlsym(handle, "difftest_store_commit");
  assert(store_commit);

  raise_intr = (void (*)(uint64_t))dlsym(handle, "difftest_raise_intr");
  assert(raise_intr);

  isa_reg_display = (void (*)(void))dlsym(handle, "isa_reg_display");
  assert(isa_reg_display);

  query = (void (*)(void*, uint64_t))dlsym(handle, "difftest_query_ref");
#ifdef ENABLE_RUNHEAD
  assert(query);
#endif

  auto nemu_difftest_set_mhartid = (void (*)(int))dlsym(handle, "difftest_set_mhartid");
  if (NUM_CORES > 1) {
    assert(nemu_difftest_set_mhartid);
    nemu_difftest_set_mhartid(coreid);
  }

  auto nemu_init = (void (*)(void))dlsym(handle, "difftest_init");
  assert(nemu_init);

  nemu_init();
}

FFOracleNemuProxy::FFOracleNemuProxy(int tid, bool nohype) {
  assert(nohype);
  void *handle = dlmopen(LM_ID_NEWLM, REF_SO, RTLD_LAZY | RTLD_DEEPBIND);
  puts("Using " REF_SO " for oracle BP");
  if (!handle){
    printf("%s\n", dlerror());
    assert(0);
  }

  this->memcpy = (void (*)(paddr_t, void *, size_t, bool))dlsym(handle, "difftest_memcpy");
  assert(this->memcpy);

  regcpy = (void (*)(void *, bool))dlsym(handle, "difftest_regcpy");
  assert(regcpy);

  csrcpy = (void (*)(void *, bool))dlsym(handle, "difftest_csrcpy");
  assert(csrcpy);

  uarchstatus_cpy = (void (*)(void *, bool))dlsym(handle, "difftest_uarchstatus_cpy");
  assert(uarchstatus_cpy);

  exec = (void (*)(uint64_t))dlsym(handle, "difftest_exec");
  assert(exec);

  update_config = (vaddr_t (*)(void *))dlsym(handle, "update_dynamic_config");
  assert(update_config);

  raise_intr = (void (*)(uint64_t))dlsym(handle, "difftest_raise_intr");
  assert(raise_intr);

  isa_reg_display = (void (*)(void))dlsym(handle, "isa_reg_display");
  assert(isa_reg_display);

  auto nemu_nohype_init = (void (*)(int))dlsym(handle, "difftest_nohype_init");
  assert(nemu_nohype_init);

  nemu_nohype_init(tid);
}
