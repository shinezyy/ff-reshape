#ifndef __CPU_O3_LOOPBUFFER_HH__
#define __CPU_O3_LOOPBUFFER_HH__


#include <cinttypes>
#include <list>
#include <map>

#include "base/random.hh"
#include "base/types.hh"
#include "params/LoopBuffer.hh"
#include "sim/sim_object.hh"

enum LRTxnState {
    Invalid = 0,
    Observing,
    Recording,
    Recorded,
    Aborted
};

struct ForwardBranch {
    Addr branch;
    Addr target;
    ForwardBranch () {};
    ForwardBranch (Addr branch_, Addr target_):
        branch(branch_),
        target(target_)
    {
    }
};

struct ExpectedForwardBranch {
    bool valid;
    ForwardBranch pair;

    void invalidate() {
        valid = false;
    }

    void set(const ForwardBranch &forwardBranch) {
        pair = forwardBranch;
        valid = true;
    }
};

struct ForwardBranchState {
    bool valid{};
    bool firstLap{};
    unsigned recordIndex{};
    unsigned observingIndex{};
    std::vector<ForwardBranch> forwardBranches;

    void clear() {
        valid = false;
        recordIndex = 0;
        observingIndex = 0;
        forwardBranches.clear();
    }
};

struct LoopRecordTransaction {
    static const unsigned recordThreshold = 2;
    LRTxnState state;
    Addr targetPC;
    Addr branchPC;
    Addr expectedPC;
    unsigned offset;
    unsigned count{};
    std::shared_ptr<ForwardBranchState> forwardBranchState;

    LoopRecordTransaction () {
        state = Invalid;
        reset();
    }

    void reset() { // reset @ use
        count = 0;
        offset = 0;
        expectedPC = 0;
        forwardBranchState =
            std::make_shared<ForwardBranchState>();
        forwardBranchState->clear();
    }

    void abort() {
        state = Aborted;
    }
};

struct LoopEntry {
    uint8_t *instPayload;
    bool valid{};
    bool fetched{};
    uint32_t used{};
    Addr branchPC;

    std::shared_ptr<ForwardBranchState> forwardBranchState;
};

struct InstSupplyState {
    bool valid{};
    Addr start; // target
    Addr end; // branch
    unsigned offset;
    uint8_t *buf;
    uint8_t tempBuf[4];
    Addr lastPC;
    Addr expectedPC;
    ExpectedForwardBranch expectedForwardBranch;
    unsigned forwardBranchIndex;

    void invalidate();
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

    uint64_t totalUsed{};

  public:

    LoopBuffer(const LoopBufferParams *params);

    ~LoopBuffer();

    const std::string _name;

    const std::string name() const {return _name;}

    const unsigned entrySize;

    const Addr mask;

    const unsigned evictRange{6};

    const bool enable;

    const bool loopFiltering;

    const unsigned maxForwardBranches;

    void processNewControl(Addr branch_pc, Addr target);

    void updateControl(Addr target);

    bool hasPendingRecordTxn();

    uint8_t* getPendingEntryPtr();

    void setFetched(Addr target);

    Addr getPendingEntryTarget();

    void clearPending();

    uint8_t* getBufferedLine(Addr target_pc);

    Addr getBufferedLineBranchPC(Addr target_pc);

    Addr align(Addr addr) {return addr & mask;}

    // loop identification

    LoopRecordTransaction txn;

    void probe(Addr branch_pc, Addr target_pc, bool pred_taken);

    void recordInst(uint8_t *building_inst, Addr pc, unsigned inst_size);

    static bool isBackward(Addr branch_pc, Addr target_pc);

    static bool isForward(Addr branch_pc, Addr target_pc);

    bool inRange(Addr target, Addr fetch_pc);

    InstSupplyState instSupply;

    void switchTo(Addr target_pc);

    bool canProvide(Addr pc);

    bool canContinueOnPC(Addr fetch_addr, Addr inst_pc);

    bool canContinueOnNPC(Addr cpc, Addr npc, bool is_taken);

    uint8_t* getInst(Addr fetch_addr, Addr inst_pc);

    void notifyLastInstSize(Addr fetch_addr, Addr inst_pc, unsigned inst_size);

    void padding_2();
};

#endif //__CPU_O3_LOOPBUFFER_HH__
