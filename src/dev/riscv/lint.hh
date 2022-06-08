//
// Created by zyy on 2020/11/24.
//

#ifndef GEM5_LINT_HH
#define GEM5_LINT_HH


#include "cpu/intr_control.hh"
#include "debug/Lint.hh"
#include "dev/io_device.hh"
#include "params/Lint.hh"

#define CLINT_MSIP     0x0000
#define CLINT_MTIMECMP 0x4000
#define CLINT_FREQ     0x8000
#define CLINT_INC      0x8008
#define CLINT_MTIME    0xBFF8
#define INT_TIMER_MACHINE 7

class Lint: public BasicPioDevice
{
  private:
    Tick interval;
    int lint_id;
    /** Pointer to the interrupt controller */
    IntrControl *intrctrl;
    bool int_enable;
    uint64_t freq,inc,mtime;
    uint64_t msip,mtimecmp;
    EventFunctionWrapper update_lint_event;
  public:
    typedef LintParams Params;
    Lint(const Params *p);

    Tick read(PacketPtr pkt) override;
    Tick write(PacketPtr pkt) override;
    void update_mtip(void);
    void update_time(void);
};


#endif //GEM5_LINT_HH
