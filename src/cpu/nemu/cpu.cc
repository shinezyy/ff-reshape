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
    extern void cpu_exec(uint64_t n);
    cpu_exec(1);
}
