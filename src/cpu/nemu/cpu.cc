//
// Created by zyy on 2021/5/7.
//

#include "cpu.hh"

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
