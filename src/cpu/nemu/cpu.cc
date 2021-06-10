//
// Created by zyy on 2021/5/7.
//

#include "cpu.hh"

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
        icachePort(name() + ".dcache_port", this),
        dcachePort(name() + ".icache_port", this)
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
    lastEntryType = -1;
}

static uint64_t cnt = 0;

bool NemuCPU::dispatch(const ExecTraceEntry &entry)
{
    // manually pipeline jump address calculation and following instructions
    if (__glibc_unlikely(lastEntryType == -1)) {
        lastEntryType = entry.type;
        lastEntry = entry;
        return false; // just after startup
    }

    ExecTraceEntry tmpEntry = lastEntry;

    switch (lastEntryType)
    {
    case ProtoInstType::MemRead:
        // process fetch + load
        processFetch(tmpEntry.fetchAddr);
        processLoad(tmpEntry.memAddr);
        break;
    case ProtoInstType::MemWrite:
        // process fetch + write
        processFetch(tmpEntry.fetchAddr);
        processStore(tmpEntry.memAddr);
        break;
    case ProtoInstType::EndOfStream:
        return true;
    default:
        // process fetch
        processFetch(tmpEntry.fetchAddr);
        break;
    }
    return false;
}

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
        // DPRINTF(NemuCPU, "fetchVaddr: 0x%#lx\n", entry.fetchAddr.v);
        DPRINTF(NemuCPU,
                "T = %d, FV = 0x%016X, FP = 0x%016X, MV = 0x%016X, MP = 0x%016X\n",
                entry.type,
                entry.fetchAddr.v,
                entry.fetchAddr.p,
                entry.memAddr.v,
                entry.memAddr.p);
        bool eos = dispatch(entry);
        if (__glibc_unlikely(eos)) {
            schedule(*execCompleteEvent, curTick());
            break;
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
    lastEntryType = -1;
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

void NemuCPU::setupFetchRequest(const RequestPtr &req)
{
    // TODO: impl
}


void NemuCPU::processFetch(const VPAddress &addr_pair)
{
    // TODO: impl
    Packet pkt = Packet(ifetchReq, MemCmd::ReadReq);

    pkt.dataStatic(&dummyInst);

    sendPacket(icachePort, &pkt);
    assert(!pkt.isError());
}

void NemuCPU::processLoad(const VPAddress &addr_pair)
{
    // TODO: impl

    const RequestPtr &req = dataReadReq;
    req->taskId(taskId());

    Packet pkt(req, Packet::makeReadCmd(req));
    pkt.dataStatic(dummyData);
    sendPacket(dcachePort, &pkt);
}

void NemuCPU::processStore(const VPAddress &addr_pair)
{
    // TODO: impl
    const RequestPtr &req = dataWriteReq;
    req->taskId(taskId());
    Packet pkt(req, Packet::makeWriteCmd(req));
    pkt.dataStatic(dummyData);
    sendPacket(dcachePort, &pkt);
}

void NemuCPU::sendPacket(RequestPort &port, const PacketPtr &pkt)
{
    port.sendAtomic(pkt);
}

Tick NemuCPU::AtomicCPUDPort::recvAtomicSnoop(PacketPtr pkt)
{
    panic("Snoop is not supported\n");
}

void NemuCPU::AtomicCPUDPort::recvFunctionalSnoop(PacketPtr pkt)
{
    panic("Snoop is not supported\n");
}