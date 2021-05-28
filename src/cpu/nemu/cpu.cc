//
// Created by zyy on 2021/5/7.
//

#include "cpu.hh"

#include "cpu/nemu/include/protocal/instr_trace.h"
#include "cpu/nemu/include/protocal/lockless_queue.h"

std::thread *ExecutionThread;

LocklessConcurrentQueue<ExecTraceEntry> *traceQueue;

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
        execCompleteEvent(nullptr),
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

    DPRINTF(NemuCPU, "Created the NemuCPU object\n");
}

bool NemuCPU::NemuCpuPort::recvTimingResp(PacketPtr pkt)
{
    return false;
}

void NemuCPU::NemuCpuPort::recvReqRetry()
{

}

static uint64_t cnt = 0;

void NemuCPU::tick()
{
    if (cnt > 1000000) {
        DPRINTF(NemuCPU, "Tick\n");
        cnt = 0;
    } else {
        cnt++;
    }

    for (int i = 10000; i > 0; i--) {
        ExecTraceEntry entry = traceQueue->pop();
        if (entry.type == ProtoInstType::EndOfStream) {
            schedule(*execCompleteEvent, curTick());
            break;
        } else {

        }
    }

    reschedule(tickEvent, curTick() + clockPeriod(), true);
}

void NemuCPU::init()
{
    BaseCPU::init();
    if (numThreads != 1)
        fatal("Nemu: Multithreading not supported");

    DPRINTF(NemuCPU, "Initiated NEMU\n");

    tc->initMemProxies(tc);
    traceQueue = new LocklessConcurrentQueue<ExecTraceEntry>;
    execCompleteEvent = new CountedExitEvent("end of all NEMU instances.", nNEMU);
}

void NemuCPU::activateContext(ThreadID tid)
{
    assert(tid == 0);
    assert(thread);

    assert(!tickEvent.scheduled());

    DPRINTF(NemuCPU, "Activate NEMU\n");

    baseStats.numCycles +=
        ticksToCycles(thread->lastActivate - thread->lastSuspend);

    extern uint64_t cpu_exec(uint64_t n);
    ExecutionThread = new std::thread(cpu_exec, -1);

    schedule(tickEvent, clockEdge(Cycles(0)));
}
