//
// Created by zyy on 2021/5/7.
//

#ifndef __GEM5_NEMUCPU_HH__
#define __GEM5_NEMUCPU_HH__

#include <atomic>
#include <mutex>
#include <queue>
#include <thread>

#include "cpu/base.hh"
#include "cpu/simple_thread.hh"
#include "debug/NemuCPU.hh"
#include "params/NemuCPU.hh"
#include "sim/sim_events.hh"

class NemuCPU: public BaseCPU
{
  public:
    NemuCPU(const NemuCPUParams &params);

    SimpleThread *thread;

    ThreadContext *tc;

    void init() override;

  protected:
    EventFunctionWrapper tickEvent;

    int nNEMU{1};

    CountedExitEvent *execCompleteEvent;

    uint64_t commitInstCount{};

  public:
    class NemuCpuPort : public RequestPort
    {
      public:
        NemuCpuPort(const std::string &_name, NemuCPU *_cpu)
                : RequestPort(_name, _cpu), cpu(_cpu)
        { }

      protected:
        /** KVM cpu pointer for finishMMIOPending() callback */
        NemuCPU *cpu;

        bool recvTimingResp(PacketPtr pkt) override;

        void recvReqRetry() override;
    };

    /** Port for data requests */
    NemuCpuPort dataPort;

    /** Unused dummy port for the instruction interface */
    NemuCpuPort instPort;

    Port &getDataPort() override {return dataPort;}

    Port &getInstPort() override {return instPort;}

    void wakeup(ThreadID tid) override;

    Counter totalInsts() const override;

    Counter totalOps() const override;

    void tick();

    void activateContext(ThreadID thread_num) override;
};


#endif //__GEM5_NEMUCPU_HH__
