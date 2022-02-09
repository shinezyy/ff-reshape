/*
 * Created by gzb on 2/8/22.
 */

#include "cpu/pred/ff_oracle.hh"

#include <dlfcn.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "base/trace.hh"
#include "cpu/nemu_common.hh"
#include "debug/FFOracleBP.hh"

#ifndef REF_SO
# error Please define REF_SO to the path of NEMU shared object file
#endif

FFOracleBP::FFOracleBP(const FFOracleBPParams &params)
    : FFBPredUnit(params),
        numLookAhead(params.numLookAhead),
        proxy(nullptr),
        oracleIID(0)
{
    DPRINTF(FFOracleBP, "FFOracleBP: look ahead %u insts\n", numLookAhead);
    state.commit_iid = numLookAhead - 2;
    state.front_iid = numLookAhead - 2;
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
    // diff.wpc = diff_wpc;
    // diff.wdata = diff_wdata;
    // diff.wdst = diff_wdst;
    diff.nemu_this_pc = 0x80000000u;
    diff.cpu_id = params.cpu_id;
    proxy = new FFOracleNemuProxy(params.cpu_id,true);
    //proxy->regcpy(gem5_reg, REF_TO_DUT);
    diff.dynamic_config.ignore_illegal_mem_access = false;
    diff.dynamic_config.debug_difftest = false;
    proxy->update_config(&diff.dynamic_config);
}

void FFOracleBP::syncArchState(paddr_t pmemAddr, void *pmemPtr, size_t pmemSize, void *regs)
{
    DPRINTF(FFOracleBP, "Will start memcpy to NEMU\n");
    proxy->memcpy(pmemAddr, pmemStart+pmemSize*diff.cpu_id, pmemSize, DUT_TO_REF);
    DPRINTF(FFOracleBP, "Will start regcpy to NEMU\n");
    proxy->regcpy(regs, DUT_TO_REF);
}

Addr FFOracleBP::lookup(ThreadID tid, Addr instPC, void * &bp_history) {
    assert(tid == 0);

    auto hist = new BPState(state);
    bp_history = hist;

    advanceFront();
    syncFront();

    hist->pred_nextK_PC = frontPointer->npc;
    return hist->pred_nextK_PC;
}

void FFOracleBP::update(ThreadID tid, Addr instPC,
                        void *bp_history, bool squashed,
                        const StaticInstPtr &inst, Addr corr_nextK_PC) {

    auto history_state = static_cast<BPState *>(bp_history);

    DPRINTF(FFOracleBP, "update PC=%x\n", instPC);

    DPRINTF(FFOracleBP, "Committing bid %u\n", history_state->front_iid + 1);
    DPRINTF(FFOracleBP, "Back bid = %u\n",
            orderedOracleEntries.back().iid);

    assert(history_state->front_iid + 1 == orderedOracleEntries.back().iid);

    assert(!orderedOracleEntries.empty());
    orderedOracleEntries.pop_back();
    //frontPointer = orderedOracleEntries.begin();

    DPRINTF(FFOracleBP, "Oracle table size: %lu.\n", orderedOracleEntries.size());

    state.commit_iid++;
    dumpState();
}

void FFOracleBP::squash(ThreadID tid, void *bp_history)
{
    auto history_state = static_cast<BPState *>(bp_history);
    if (history_state->front_iid < state.front_iid) {
        state.front_iid = history_state->front_iid;

        DPRINTF(FFOracleBP, "After squash\n");
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
        DPRINTF(FFOracleBP, "recorded front iid = %u, state.front_iid = %u\n",
                history_state->front_iid, state.front_iid);
    }
    delete history_state;
}

FFOracleBP::EntryIter FFOracleBP::lookAheadInsts()
{
    EntryIter front;
    unsigned feedSize = (orderedOracleEntries.empty() ? numLookAhead * 2 : numLookAhead);
    DPRINTF(FFOracleBP, "Start feeding %i insts.\n", feedSize);

    for (unsigned i = 0; i < feedSize; i++) {
        proxy->exec(1);
        proxy->regcpy(diff.nemu_reg,REF_TO_DIFFTEST);
        uint64_t npc = diff.nemu_reg[DIFFTEST_THIS_PC];
        DPRINTF(FFOracleBP, "Got NPC: %#x.\n", npc);

        orderedOracleEntries.push_front(OracleEntry{oracleIID++,
                                    npc
                                    });
        if ((i==0) || (i==numLookAhead))
            front = orderedOracleEntries.begin();
    }
    assert(orderedOracleEntries.size() < 3000);
    return front;
}

void FFOracleBP::advanceFront() {
    state.front_iid++;
}

void FFOracleBP::syncFront() {
    DPRINTF(FFOracleBP, "Oracle table size: %lu.\n", orderedOracleEntries.size());

    if (orderedOracleEntries.size() == 0) {
        frontPointer = lookAheadInsts();
        dumpState();

    } else if (state.front_iid > orderedOracleEntries.front().iid) {
        // younger, must feed more
        frontPointer = lookAheadInsts();
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
        DPRINTF(FFOracleBP, "commit iid: %lu, front iid: %lu, frontPointer iid: %lu, "
                "table back: %lu, table front: %lu\n",
                state.commit_iid, state.front_iid, frontPointer->iid,
                orderedOracleEntries.back().iid,
                orderedOracleEntries.front().iid
               );
    } else {
        DPRINTF(FFOracleBP, "commit iid: %lu, front iid: %lu, frontPointer iid: %lu\n",
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
  puts("Using " REF_SO " for difftest");
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
  puts("Using " REF_SO " for difftest");
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
