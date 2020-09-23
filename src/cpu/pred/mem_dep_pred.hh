#ifndef __MEM_DEP_PRED_HH__
#define __MEM_DEP_PRED_HH__


#include <map>
#include <vector>

#include "base/random.hh"
#include "base/types.hh"
#include "cpu/forwardflow/dq_pointer.hh"
#include "cpu/inst_seq.hh"
#include "cpu/pred/sat_counter.hh"
#include "debug/NoSQPred.hh"
#include "params/MemDepPredictor.hh"
#include "sim/sim_object.hh"

struct DistancePair
{
    unsigned snDistance{};
    unsigned dqDistance{};

    DistancePair() = default;

    DistancePair(unsigned sn, unsigned dq)
    : snDistance(sn), dqDistance(dq) {}

    DistancePair &operator = (const DistancePair &pair) {
        snDistance = pair.snDistance;
        dqDistance = pair.dqDistance;
        return *this;
    }
};

struct MemPredCell
{
    SignedSatCounter conf;
    DistancePair distPair;

    explicit MemPredCell (unsigned counter_bits):
        conf(counter_bits)
    {}
};

struct MemPredHistory
{
    bool bypass;
    bool pcBypass;
    bool pathBypass;

    bool pathSensitive;
    DistancePair distPair;

    bool predecessorIsLoad;

    using FoldedPC = unsigned;
    FoldedPC path;
};

struct SSBFCell
{
    using SSN = InstSeqNum;
    SSN lastStore{};
    uint8_t size;
    uint8_t offset;
    BasePointer lastStorePosition;
    BasePointer predecessorPosition;
};

template <class Container>
void checkAndRandEvict(Container &set, unsigned randRange)
{
    if (set.size() >= randRange) { // eviction
        DPRINTF(NoSQPred, "Doing Eviction\n");
        unsigned evicted = random_mt.random<unsigned>(0, randRange - 1);
        auto it = set.begin(), e = set.end();
        while (evicted) {
            assert(it != e);
            it++;
            evicted--;
        }
        assert(it != e);
        DPRINTF(NoSQPred, "Evicting key: %lu\n", it->first);
        set.erase(it);
    }
}

class TSSBF: public SimObject
{
  private:
    // T-SSBF
    const unsigned TagBits;
    const uint64_t TagMask;

    const unsigned Size;
    const unsigned Assoc;
    const unsigned Depth;
    const unsigned IndexBits;
    const uint64_t IndexMask;

  public:
    const unsigned addrShamt{3};
    const unsigned offsetMask{0x7};
  private:

    using SSBFSet = std::map<Addr, SSBFCell>;
    using SSBFTable = std::vector<SSBFSet>;

    SSBFTable table;

  public:
    typedef MemDepPredictorParams Params;
    explicit TSSBF(const Params *p);

    SSBFCell *find(Addr key);

    InstSeqNum findYoungestInSet(Addr key);

    SSBFCell *allocate(Addr key);

  private:
    Addr extractIndex(Addr key) const;
    Addr extractTag(Addr key) const;

    void checkAndRandEvictOldest(SSBFSet &set);
};

class MemDepPredictor: public SimObject
{
  public:
    typedef MemDepPredictorParams Params;

    explicit MemDepPredictor(const Params *p);

    const std::string _name;

    const std::string name() const override {return _name;}

  private:
    const unsigned pcShamt{2};

    const unsigned PCTableSize;
    const unsigned PCTableAssoc;
    const unsigned PCTableDepth;
    const unsigned PCTableIndexBits;
    const unsigned PCTableIndexMask;

    const unsigned PathTableSize;
    const unsigned PathTableAssoc;
    const unsigned PathTableDepth;
    const unsigned PathTableIndexBits;
    const unsigned PathTableIndexMask;

    const unsigned DistanceBits;
    const unsigned ShamtBits;
    const unsigned StoreSizeBits;
    const unsigned ConfidenceBits;

    const unsigned TagBits;
    const unsigned TagMask;

    const unsigned HistoryLen;
    const unsigned PathMask;
    const unsigned BranchPathLen;
    const unsigned CallPathLen;

  public:
    enum MemDepMissPred {
        Correct = 0,
        FalsePositive,
        FalseNegative,
        WrongSource
    };

    using FoldedPC = unsigned;

    using MemPredSet = std::map<Addr, MemPredCell>;

    using MemPredTable = std::vector<MemPredSet>;

    SSBFCell *debug;

  private:
    MemPredTable pcTable;
    MemPredTable pathTable;

  public:
    TSSBF tssbf;

    std::pair<bool, DistancePair> predict(Addr load_pc, FoldedPC path, MemPredHistory *&hist);

    std::pair<bool, DistancePair> predict(Addr load_pc, MemPredHistory *&hist);

    void recordPath(Addr control_pc, bool isCall);

    const unsigned callShamt{2};
    const unsigned branchShamt{1};
    unsigned controlPath{};

    void update(Addr load_pc, bool should_bypass,
                unsigned sn_dist, unsigned dq_dist,
                MemPredHistory *&hist);

    void squash(MemPredHistory* &hist);

    void clear();

    void commitStore(Addr eff_addr, InstSeqNum sn, const BasePointer &position);

    InstSeqNum lookupAddr(Addr eff_addr);

    bool checkAddr(InstSeqNum load_sn, bool pred_bypass, Addr eff_addr_low,
                   Addr eff_addr_high, uint8_t size, InstSeqNum nvul);

    void commitLoad(Addr eff_addr, InstSeqNum sn, BasePointer &position);

  private:

    Addr genPathKey(Addr pc, FoldedPC path) const;

    Addr extractIndex(Addr key, bool isPath);

    Addr extractTag(Addr key, bool isPath);

    std::pair<bool, MemPredCell *> find(MemPredTable &table, Addr key, bool isPath);

    MemPredCell *allocate(MemPredTable &table, Addr key, bool isPath);

    void decrement(Addr pc, FoldedPC path);

    void decrement(MemPredTable &table, Addr key, bool alloc, bool isPath);

    void increment(MemPredTable &table, Addr key, const DistancePair &pair, bool isPath);

    FoldedPC getPath() const;

    Addr shiftAddr(Addr addr);
};

#endif // __MEM_DEP_PRED_HH__
