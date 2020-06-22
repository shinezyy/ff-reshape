#ifndef __CPU_O3_LOOPBUFFER_HH__
#define __CPU_O3_LOOPBUFFER_HH__


#include <cinttypes>
#include <list>
#include <map>

#include "base/random.hh"
#include "base/types.hh"
#include "params/LoopBuffer.hh"
#include "sim/sim_object.hh"

struct LoopRecordTransaction {
    bool pending{};
    Addr backwardBranchPC;
};

struct LoopEntry {
    uint8_t *instPayload;
    bool valid{};
    bool fetched{};
    uint32_t used{};

};

class LoopBuffer : public SimObject
{
    unsigned numEntries;

    using LoopTable = std::map<Addr, LoopEntry>;

    using LTit = LoopTable::iterator;

    using OrderedList = std::list<LTit>;

    using OrderIt = OrderedList::iterator;

    using OrderRIt = OrderedList::reverse_iterator;

    LoopTable table;

    OrderedList rank;

    bool pending{};

    Addr pendingTarget{};
  public:

    LoopBuffer(LoopBufferParams *params);

    ~LoopBuffer();

    const std::string _name;

    const std::string name() const {return _name;}

    const unsigned entrySize;

    const Addr mask;

    const unsigned evictRange{10};

    void processNewControl(Addr target);

    void updateControl(Addr target);

    bool hasPendingRecordTxn();

    uint8_t* getPendingEntryPtr();

    void setFetched(Addr target);

    Addr getPendingEntryTarget();

    void clearPending();

    uint8_t* getBufferedLine(Addr branch_pc);

    Addr align(Addr addr) {return addr & mask;}
};

#endif //__CPU_O3_LOOPBUFFER_HH__
