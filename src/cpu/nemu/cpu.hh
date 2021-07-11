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
#include "cpu/nemu/include/protocal/instr_trace.h"
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
    // class NemuCpuPort : public RequestPort
    // {
    //   public:
    //     NemuCpuPort(const std::string &_name, NemuCPU *_cpu)
    //             : RequestPort(_name, _cpu), cpu(_cpu)
    //     { }

    //   protected:
    //     /** KVM cpu pointer for finishMMIOPending() callback */
    //     NemuCPU *cpu;

    //     bool recvTimingResp(PacketPtr pkt) override;

    //     void recvReqRetry() override;
    // };

    /** Port for data requests */
    // NemuCpuPort dataPort;

    /** Unused dummy port for the instruction interface */
    // NemuCpuPort instPort;

    void wakeup(ThreadID tid) override;

    Counter totalInsts() const override;

    Counter totalOps() const override;

    void tick();

    void activateContext(ThreadID thread_num) override;
 private:

   void setupFetchRequest(const RequestPtr &req);

   bool dispatch(const ExecTraceEntry &entry);

   ExecTraceEntry lastEntry;
   int lastEntryType;

   void processFetch(const VPAddress &addr_pair);

   void processLoad(const VPAddress &addr_pair, Addr pc);

   void processStore(const VPAddress &addr_pair, Addr pc);

   void sendPacket(RequestPort &port, const PacketPtr &pkt);

   /**
     * An AtomicCPUPort overrides the default behaviour of the
     * recvAtomicSnoop and ignores the packet instead of panicking. It
     * also provides an implementation for the purely virtual timing
     * functions and panics on either of these.
     */
   class AtomicCPUPort : public RequestPort
   {

   public:
     AtomicCPUPort(const std::string &_name, NemuCPU*_cpu)
         : RequestPort(_name, _cpu)
     {
     }

   protected:
     bool
     recvTimingResp(PacketPtr pkt)
     {
       panic("Atomic CPU doesn't expect recvTimingResp!\n");
     }

     void
     recvReqRetry()
     {
       panic("Atomic CPU doesn't expect recvRetry!\n");
     }

    };

    class AtomicCPUDPort : public AtomicCPUPort
    {

      public:
        AtomicCPUDPort(const std::string &_name, NemuCPU *_cpu)
            : AtomicCPUPort(_name, _cpu), cpu(_cpu)
        {
            cacheBlockMask = ~(cpu->cacheLineSize() - 1);
        }

        bool isSnooping() const { return true; }

        Addr cacheBlockMask;
      protected:
        NemuCPU *cpu;

        virtual Tick recvAtomicSnoop(PacketPtr pkt);
        virtual void recvFunctionalSnoop(PacketPtr pkt);
    };

    AtomicCPUPort icachePort;
    AtomicCPUDPort dcachePort;
    Port &getDataPort() override {return dcachePort;}
    Port &getInstPort() override {return icachePort;}


    RequestPtr ifetchReq;
    RequestPtr dataReadReq;
    RequestPtr dataWriteReq;
    RequestPtr dataAmoReq;

    TheISA::MachInst dummyInst;

    uint8_t dummyData[64];

    bool inSameBlcok(Addr blk_addr, Addr addr);

    uint64_t instCount{};

    const uint64_t maxInsts;
};


#endif //__GEM5_NEMUCPU_HH__
