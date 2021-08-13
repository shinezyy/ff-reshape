#ifndef __MEM_DUMMY_MEMORY_HH__
#define __MEM_DUMMY_MEMORY_HH__

#include <list>

#include "mem/port.hh"
#include "mem/simple_mem.hh"
#include "params/SimpleMemory.hh"

/**
 * The simple memory is a basic single-ported memory controller with
 * a configurable throughput and latency.
 *
 * @sa  \ref gem5MemorySystem "gem5 Memory System"
 */
class DummyMemory : public SimpleMemory
{
  public:

    DummyMemory(const SimpleMemoryParams &p):
        SimpleMemory(p)
    {
    }

    void init() override
    {
        SimpleMemory::init();
    }

    Tick recvAtomic(PacketPtr pkt) override
    {
        if (pkt->needsResponse()) {
            pkt->makeResponse();
        }
        return 0;
    }

};

#endif //__MEM_SIMPLE_MEMORY_HH__
