//
// Created by zyy on 2021/5/7.
//
#include "arch/types.hh"
#include "config/the_isa.hh"
#include "cpu.hh"
#include "cpu/nemu/include/protocal/lockless_queue.h"

std::thread *ExecutionThread;

LocklessConcurrentQueue<ExecTraceEntry> *traceQueue;

std::atomic_bool nemuStop;

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
        icachePort(name() + ".icache_port", this),
        dcachePort(name() + ".dcache_port", this),
        maxInsts(params.max_insts_any_thread),
        setMask(numSets - 1),
        sets(numSets, MRUList{})
{
    setBootLoaderPath(params);
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

    ifetchReq = std::make_shared<Request>();
    dataReadReq = std::make_shared<Request>();
    dataWriteReq = std::make_shared<Request>();
    dataAmoReq = std::make_shared<Request>();

}

bool NemuCPU::dispatch(const ExecTraceEntry &entry)
{
    // manually pipeline jump address calculation and following instructions
    if (__glibc_unlikely(lastEntryType == -1)) {
        lastEntryType = entry.type;
        lastEntry = entry;
        return false; // just after startup
    }

    ExecTraceEntry tmpEntry = lastEntry;
    int tmp_type = lastEntryType;

    lastEntryType = entry.type;
    lastEntry = entry;

    switch (tmp_type)
    {
    case ProtoInstType::MemRead:
        __attribute__ ((fallthrough));
    case ProtoInstType::MemWrite:
        insertMemTrace(tmpEntry);
        break;
        /*
    case ProtoInstType::MemRead:
        // process fetch + load
        processFetch(tmpEntry.fetchAddr);
        processLoad(tmpEntry.memAddr, tmpEntry.fetchAddr.v);
        break;
    case ProtoInstType::MemWrite:
        // process fetch + write
        processFetch(tmpEntry.fetchAddr);
        processStore(tmpEntry.memAddr, tmpEntry.fetchAddr.v);
        break;
        */
    case ProtoInstType::EndOfStream:
        return true;
    default:
        // process fetch
        // processFetch(tmpEntry.fetchAddr);
        break;
    }
    instCount++;

    if (__glibc_unlikely(instCount >= maxInsts && cpuState == CPUState::Running)) {
        warn("GEM5 reach max insts, notify NEMU to stop\n");
        nemuStop.store(true);
        cpuState = CPUState::Stopping;
    }

    return false;
}

void NemuCPU::tick()
{
    bool eos = false;

    for (;;) {
        ExecTraceEntry entry = traceQueue->pop();
        // DPRINTF(NemuCPU, "fetchVaddr: 0x%#lx\n", entry.fetchAddr.v);
        DPRINTF(NemuCPU,
                "ID = %llu, T = %d, FV = 0x%016lx, FP = 0x%016lx, MV = 0x%016lx, MP = 0x%016lx\n",
                entry.id,
                entry.type,
                entry.fetchAddr.v,
                entry.fetchAddr.p,
                entry.memAddr.v,
                entry.memAddr.p);
        eos = dispatch(entry);
        if (__glibc_unlikely(eos)) {
            break;
        }
    }

    sendMemAccToCaches();

    if (__glibc_unlikely(eos)) {
        // assert(cpuState == CPUState::Stopping);
        warn("GEM5 received EOS from NEMU\n");
        cpuState = Stopped;
        schedule(*execCompleteEvent, curTick());
    }
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
    nemuStop.store(false);
    cpuState = CPUState::Running;
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
    ExecutionThread = new std::thread(cpu_exec, maxInsts);

    schedule(tickEvent, clockEdge(Cycles(0)));
}

void NemuCPU::setupFetchRequest(const RequestPtr &req)
{
    // TODO: impl
}


void NemuCPU::processFetch(const VPAddress &addr_pair)
{
    if (__glibc_unlikely(
        addr_pair.p < 0x80000000 || addr_pair.p >= 0x280000000)) {
        return;
    }
    // DPRINTF(NemuCPU, "VA: 0x%016lx, PA: 0x%016lx\n",
    //         addr_pair.v, addr_pair.p);
    Addr vaddr = addr_pair.v & ((~0UL) << 2);
    Addr paddr = addr_pair.p & ((~0UL) << 2);
    DPRINTF(NemuCPU, "Fetch VA: 0x%016lx, PA: 0x%016lx\n", vaddr, paddr);
    ifetchReq->setVirt(vaddr, sizeof(TheISA::MachInst),
                       Request::INST_FETCH, instRequestorId(), addr_pair.v);
    ifetchReq->setPaddr(paddr);
    Packet pkt = Packet(ifetchReq, MemCmd::ReadReq);

    pkt.dataStatic(&dummyInst);

    sendPacket(icachePort, &pkt);
    assert(!pkt.isError());
}

void NemuCPU::processLoad(const VPAddress &addr_pair, Addr pc)
{
    if (__glibc_unlikely(
        addr_pair.p < 0x80000000 || addr_pair.p >= 0x280000000)) {
        return;
    }
    const RequestPtr &req = dataReadReq;
    req->taskId(taskId());

    Addr vaddr = addr_pair.v & ((~0UL) << 3);
    Addr paddr = addr_pair.p & ((~0UL) << 3);
    DPRINTF(NemuCPU, "Read  VA: 0x%016lx, PA: 0x%016lx\n", vaddr, paddr);
    req->setVirt(vaddr, 8, 0, dataRequestorId(), pc);
    req->setPaddr(paddr);

    Packet pkt(req, Packet::makeReadCmd(req));
    pkt.dataStatic(dummyData);
    sendPacket(dcachePort, &pkt);
}

void NemuCPU::processStore(const VPAddress &addr_pair, Addr pc)
{
    if (__glibc_unlikely(
        addr_pair.p < 0x80000000 || addr_pair.p >= 0x280000000)) {
        return;
    }
    const RequestPtr &req = dataWriteReq;
    req->taskId(taskId());

    Addr vaddr = addr_pair.v & ((~0UL) << 3);
    Addr paddr = addr_pair.p & ((~0UL) << 3);
    DPRINTF(NemuCPU, "Write VA: 0x%016lx, PA: 0x%016lx\n", vaddr, paddr);
    req->setVirt(vaddr, 8, 0, dataRequestorId(), pc);
    req->setPaddr(paddr);

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
    return 0;
    // do nothing
}

void NemuCPU::AtomicCPUDPort::recvFunctionalSnoop(PacketPtr pkt)
{
    // do nothing
}

bool NemuCPU::inSameBlcok(Addr blk_addr, Addr addr)
{
    return (blk_addr - addr) <= 64;
}

void NemuCPU::setBootLoaderPath(const NemuCPUParams &params)
{
    extern const char *img_file;
    img_file = params.gcpt_file.c_str();
}

Addr
NemuCPU::extractSet(Addr addr)
{
    addr = (addr >> 6) & setMask;
    return addr;
}

void
NemuCPU::insertMemTrace(const ExecTraceEntry &entry)
{
    MRUList &set = sets.at(extractSet(entry.memAddr.p));
    std::pair<MRUIter, bool> p = set.push_front(entry);
    if (!p.second) { // duplication found
        set.relocate(set.begin(), p.first);
    } else if (set.size() > assoc){
        set.pop_back();
    }
}

void
NemuCPU::sendMemAccToCaches()
{
    for (unsigned a = 0; a < assoc; a++) {
        for (auto &set: sets) {
            if (!set.empty()) {
                auto &entry = set.back();
                if (entry.type == ProtoInstType::MemRead) {
                    processLoad(entry.memAddr, entry.fetchAddr.v);
                } else {
                    processStore(entry.memAddr, entry.fetchAddr.v);
                }
                set.pop_back();
            }
        }
    }
}

