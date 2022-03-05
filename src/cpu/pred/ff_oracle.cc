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

FFOracleBP::FFOracleBPStats::FFOracleBPStats(Stats::Group *parent)
        : Stats::Group(parent, "ff_oracle_bp"),
          ADD_STAT(incorrectAfterSC, "Incorrect predictions after SC.")
{
}

FFOracleBP::FFOracleBP(const FFOracleBPParams &params)
    : FFBPredUnit(params),
        numLookAhead(getNumLookAhead()),
        presetAccuracy(params.presetAccuracy),
        proxy(nullptr),
        oracleIID(0),
        randGen(params.randNumSeed),
        bdGen(params.presetAccuracy),
        scInFlight(false),
        decoder(new TheISA::Decoder()),
        stats(this)
{
    assert(numLookAhead >= 1);
    assert(0 <= params.presetAccuracy && params.presetAccuracy <= 1);
    DPRINTF(FFOracleBP_misc, "Look ahead: %u insts. Preset accuracy: %f%%.\n",
                    numLookAhead,
                    params.presetAccuracy * 100);
    state.commit_iid = numLookAhead - 1;
    state.front_iid = numLookAhead - 1;
    state.front_seqNum = 0;
    seqNum = 0;
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
    static ThreadID nemuID = 1;
    proxy = new FFOracleNemuProxy(params.numThreads * nemuID + params.cpu_id, true);
    ++nemuID;
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

    state.front_seqNum++;

    syncFront();
    if (state.front_seqNum == frontPointerDBB->exitSeqNum) {
        advanceFront();
    }

    predPC = frontPointer->pc;
    if (!bdGen(randGen)) {
        // Generate wrong prediction
        std::uniform_int_distribution<size_t> u(1, numLookAhead);
        predPC += ((predPC & 3) ? 2 : 4) * u(randGen);
        assert (predPC != frontPointer->pc);
    }
    hist->instPC = instPC;
    hist->scInFlight = frontPointer->scInFlight;

    DPRINTF(FFOracleBP_lookup, "+ lookup PC=%#x hist_iid=%u next-%u PC=%#x\n",
                        instPC, hist->front_iid, numLookAhead, predPC);
    return predPC;
}

void FFOracleBP::update(ThreadID tid, const TheISA::PCState &thisPC,
                        void *bp_history, bool squashed,
                        const StaticInstPtr &inst,
                        const TheISA::PCState &pred_nextK_PC, const TheISA::PCState &corr_nextK_PC) {

    auto history_state = static_cast<BPState *>(bp_history);

    DPRINTF(FFOracleBP_update, "- update%s PC=%x hist_iid=%u\n",
                                squashed ? " squashed" : " ", thisPC.pc(),
                                history_state->front_iid);

    if (squashed) {
        // Am I a perfect predictor?
        if (std::fabs(presetAccuracy - 1.0) < std::numeric_limits<double>::epsilon()) {
            if (!history_state->scInFlight)
                warn("Oracle BP fault: mispredicted @ %#x (iid=%u).\n",
                        history_state->instPC, history_state->front_iid + 1);
        }

        if (history_state->scInFlight)
            ++stats.incorrectAfterSC;

    } else {
        DPRINTF(FFOracleBP_misc, "Committing iid %u\n", history_state->front_iid + 1);
        DPRINTF(FFOracleBP_misc, "Back iid = %u\n",
                orderedOracleEntries.back().iid);

        if (!orderedOracleEntries.empty() &&
                history_state->front_seqNum + 1 == orderedOracleEntries.back().exitSeqNum) {
            orderedOracleEntries.pop_back();
        }

        DPRINTF(FFOracleBP_misc, "Oracle table size: %lu.\n", orderedOracleEntries.size());

        state.commit_iid++;
        dumpState();
    }
}

void FFOracleBP::squash(ThreadID tid, void *bp_history)
{
    auto history_state = static_cast<BPState *>(bp_history);

    DPRINTF(FFOracleBP_squash, "- squash hist_iid=%lu\n", history_state->front_iid);

    if (history_state->front_seqNum < state.front_seqNum) {
        state.front_seqNum = history_state->front_seqNum;
    }

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
        syncFrontDBB();

        dumpState();
    } else {
        DPRINTF(FFOracleBP_misc, "recorded front iid = %u, state.front_iid = %u\n",
                history_state->front_iid, state.front_iid);
    }
    delete history_state;
}

void FFOracleBP::lookAheadInsts(unsigned len, bool record)
{
    DPRINTF(FFOracleBP_misc, "Start feeding %i insts.\n", len);
    for (unsigned numDBB = 0; numDBB < len; ) {
        bool exitDBB = false;
        Addr pc = diff.nemu_this_pc;
        DPRINTF(FFOracleBP_misc, "Got PC [sn%lu]: %#x\n", oracleIID, pc);
        ++seqNum;

        if (!scInFlight) {
            // prevent SC from modifying memory
            diff.sync.lrscValid = false;
            proxy->uarchstatus_cpy(&diff.sync, DIFFTEST_TO_REF);
            // Even if SC not success, SPF (if raised) also needs to be reported,
            // which forces us to save the register state to roll back from the exception.
            proxy->regcpy(scBreakpoint.reg, REF_TO_DIFFTEST);

            nemuStep();

            // disassemble inst @ PC
            uint32_t inst = proxy->riscv_get_last_exec_inst();
            TheISA::PCState pcState(pc);
            decoder->reset();
            decoder->moreBytes(0, 0, inst);
            assert(decoder->instReady());
            StaticInstPtr staticInst = decoder->decode(pcState);

            pcState.npc(diff.nemu_this_pc);
            if (pcState.branching()) { // entering new basic block
                ++numDBB, exitDBB = true;
            }

            auto checkSC = [this](const StaticInstPtr &inst) {
                if (inst->isStoreConditional() && !scInFlight) {
                    scBreakpoint.bpState = state;
                    numInstAfterSC = 0;
                    scInFlight = true;
                }
            };

            if (staticInst->isMacroop()) {
                StaticInstPtr mircoOp;
                do {
                    mircoOp = staticInst->fetchMicroop(pcState.upc());
                    checkSC(mircoOp);
                    pcState.uAdvance();
                } while (!mircoOp->isLastMicroop());
            } else {
                checkSC(staticInst);
            }

        } else {
            ++numInstAfterSC;
            // naive prediction.
            diff.nemu_this_pc += (diff.nemu_this_pc & 3) ? 2 : 4;
            ++numDBB, exitDBB = true;
        }

        if (exitDBB && record) {
            orderedOracleEntries.push_front(OracleEntry{oracleIID, pc, scInFlight, seqNum});
            assert(orderedOracleEntries.size() < 300000);
            oracleIID++;
        }
    }
}

void FFOracleBP::syncStoreConditional(bool lrValid, ThreadID tid) {
    assert(scInFlight);

    // synchronize LR/SC flag of NEMU
    diff.sync.lrscValid = lrValid;
    proxy->uarchstatus_cpy(&diff.sync, DIFFTEST_TO_REF);

    // restore context
    diff.nemu_this_pc = scBreakpoint.reg[DIFFTEST_THIS_PC];
    proxy->regcpy(scBreakpoint.reg, DIFFTEST_TO_REF);

    scInFlight = false;
    nemuStep();
    lookAheadInsts(numInstAfterSC, false);
}

void FFOracleBP::nemuStep() {
    proxy->exec(1);

    proxy->regcpy(diff.nemu_reg, REF_TO_DIFFTEST);
    diff.nemu_this_pc = diff.nemu_reg[DIFFTEST_THIS_PC];
}

void FFOracleBP::advanceFront() {
    state.front_iid++;
}

void FFOracleBP::syncFront() {
    DPRINTF(FFOracleBP_misc, "syncFront\n");
    DPRINTF(FFOracleBP_misc, "Oracle table size: %lu.\n", orderedOracleEntries.size());

    if (orderedOracleEntries.size() == 0) {
        lookAheadInsts(numLookAhead, true);
        advanceFront();
    }

    if (orderedOracleEntries.size() == numLookAhead) {
        lookAheadInsts(1, true);
        frontPointer = orderedOracleEntries.begin();
        assert(state.front_iid == frontPointer->iid);
        syncFrontDBB();
        dumpState();

    } else if (state.front_iid > orderedOracleEntries.front().iid) {
        // younger, must feed more
        DPRINTF(FFOracleBP_misc, "before state.front_iid=%lu frontPointer->iid=%lu.\n",
                                    state.front_iid, frontPointer->iid);
        lookAheadInsts(1, true);
        frontPointer = orderedOracleEntries.begin();
        assert(state.front_iid == frontPointer->iid);
        syncFrontDBB();
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
            syncFrontDBB();
        }
    }
}

void FFOracleBP::syncFrontDBB()
{
    frontPointerDBB = frontPointer;
    // FIXME: time consuming. adapt to O(1)
    for (unsigned i=0;i<numLookAhead;i++) {
        ++frontPointerDBB;
        assert(frontPointerDBB != orderedOracleEntries.end());
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

  vaddr_read_safe = (uint64_t (*)(uint64_t, int))dlsym(handle, "vaddr_read_safe");
  assert(vaddr_read_safe);

  riscv_get_last_exec_inst = (uint32_t (*)())dlsym(handle, "riscv_get_last_exec_inst");
  assert(riscv_get_last_exec_inst);

  nemu_nohype_init(tid);
}
