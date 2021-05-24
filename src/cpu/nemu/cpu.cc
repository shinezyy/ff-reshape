//
// Created by zyy on 2021/5/7.
//

#include "cpu.hh"
// #include "cpu/nemu/include/nemu_types.h"

void NemuCPU::wakeup(ThreadID tid)
{

}

Counter NemuCPU::totalInsts() const
{
    return 1;
}

Counter NemuCPU::totalOps() const
{
    return 1;
}

NemuCPU::NemuCPU(const NemuCPUParams &params) :
        BaseCPU(params),
        tickEvent([this] {tick();}, "NemuCPU tick",
        false, Event::CPU_Tick_Pri
        ),
        dataPort(name() + ".dcache_port", this),
        instPort(name() + ".icache_port", this)
{
    extern void init_monitor(int argc, char *argv[]);

    char *empty[1] = {nullptr};
    init_monitor(0, empty);

    assert (FullSystem);
    thread = new SimpleThread(this, 0, params.system, params.mmu,
                                params.isa[0]);

    thread->setStatus(ThreadContext::Halted);
    tc = thread->getTC();
    threadContexts.push_back(tc);
}

bool NemuCPU::NemuCpuPort::recvTimingResp(PacketPtr pkt)
{
    return false;
}

void NemuCPU::NemuCpuPort::recvReqRetry()
{

}

void NemuCPU::tick()
{
    extern uint64_t cpu_exec(uint64_t n);
    // cpu_exec(1024*1024);
    uint32_t detail_chunk = 65532;
    uint32_t icount;
    icount = cpu_exec(detail_chunk);
    commitInstCount += icount;
    if (icount > 0) {
        reschedule(tickEvent, curTick() + clockPeriod(), true);
    }
}

void NemuCPU::init()
{
    BaseCPU::init();
    if (numThreads != 1)
        fatal("Nemu: Multithreading not supported");

    warn("Initiated NEMU\n");

    tc->initMemProxies(tc);
}

void NemuCPU::activateContext(ThreadID tid)
{
    assert(tid == 0);
    assert(thread);

    assert(!tickEvent.scheduled());

    baseStats.numCycles +=
        ticksToCycles(thread->lastActivate - thread->lastSuspend);

    schedule(tickEvent, clockEdge(Cycles(0)));
}
