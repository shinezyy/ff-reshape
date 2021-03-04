#ifndef __COMMON_H__
#define __COMMON_H__

#include <stdint.h>
#include <assert.h>
#include <string.h>

typedef uint64_t rtlreg_t;

typedef uint64_t paddr_t;
typedef uint64_t vaddr_t;

typedef uint16_t ioaddr_t;

#include "nemu_macro.hh"

// 0~31: GPRs, 32~63 FPRs
enum {
  DIFFTEST_THIS_PC = 64,
  DIFFTEST_MSTATUS,
  DIFFTEST_MCAUSE,
  DIFFTEST_MEPC,
  DIFFTEST_SSTATUS,
  DIFFTEST_SCAUSE,
  DIFFTEST_SEPC,
  DIFFTEST_SATP,
  DIFFTEST_MIP,
  DIFFTEST_MIE,
  DIFFTEST_MSCRATCH,
  DIFFTEST_SSCRATCH,
  DIFFTEST_MIDELEG,
  DIFFTEST_MEDELEG,
  DIFFTEST_MTVAL,
  DIFFTEST_STVAL,
  DIFFTEST_MTVEC,
  DIFFTEST_STVEC,
  DIFFTEST_MODE,
  DIFFTEST_INST_PAYLOAD,
  DIFFTEST_RVC,
  DIFFTEST_STORE_ADDR,
  DIFFTEST_STORE_VALUE,
  DIFFTEST_HAS_MEM_XPT,
  DIFFTEST_NR_REG
};

struct SyncChannel {
  uint64_t scFailed; // sc inst commited, it failed beacuse lr_valid === 0
};

struct SyncState {
  uint64_t lrscValid;
  uint64_t lrscAddr;
};

struct DiffState {
  // Regs and mode for single step difftest
  int commit;
  uint64_t *nemu_reg;
  uint64_t *gem5_reg;
  uint32_t this_inst;
  int skip;
  int isRVC;
  uint64_t *wpc;
  uint64_t *wdata;
  uint32_t *wdst;
  int wen;
  uint64_t intrNO;
  uint64_t cause; // for disambiguate_exec
  int priviledgeMode;
  uint64_t npc;

  // Microarchitucural signal needed to sync status
  struct SyncChannel sync;
  // lrscValid needs to be synced as nemu does not know
  // how many cycles were used to finish a lr/sc pair,
  // this will lead to different sc results.
};

struct DisambiguationState {
  uint64_t exceptionNo;
  uint64_t mtval;
  uint64_t stval;
};

extern void (*ref_difftest_memcpy_from_dut)(paddr_t dest, void *src, size_t n);
extern void (*ref_difftest_memcpy_from_ref)(void *dest, paddr_t src, size_t n);
extern void (*ref_difftest_getregs)(void *c);
extern void (*ref_difftest_setregs)(const void *c);
extern void (*ref_difftest_get_mastatus)(void *s);
extern void (*ref_difftest_set_mastatus)(const void *s);
extern void (*ref_difftest_get_csr)(void *c);
extern void (*ref_difftest_set_csr)(const void *c);
extern vaddr_t (*ref_disambiguate_exec)(void *disambiguate_para);

void init_difftest();
int difftest_step(DiffState *s);
void difftest_display(uint8_t mode);

#define DIFFTEST_WIDTH 8

enum DiffAt {
    NoneDiff = 0,
    PCDiff,
    NPCDiff,
    InstDiff,
    ValueDiff,
};

extern uint8_t *pmemStart;
extern uint64_t pmemSize;
extern const char *reg_name[];

#endif
