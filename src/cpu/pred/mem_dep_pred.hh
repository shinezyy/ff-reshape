#ifndef __MEM_DEP_PRED_HH__
#define __MEM_DEP_PRED_HH__


#include <map>
#include <unordered_map>
#include <vector>

#include <boost/dynamic_bitset.hpp>

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
    bool patternBypass;

    bool pathSensitive;
    bool localSensitive;
    DistancePair distPair;

    using FoldedPC = uint64_t ;
    FoldedPC path;

    boost::dynamic_bitset<> localHistory;
    InstSeqNum inst;
    bool willSquash;

    int32_t predictionValue;

    bool updatedHistory;
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
    const Addr offsetMask{0x7};
  private:

    using SSBFSet = std::map<Addr, SSBFCell>;
    using SSBFTable = std::vector<SSBFSet>;

    SSBFTable table;

    std::vector<unsigned long> tableAccCount;

  public:
    typedef MemDepPredictorParams Params;
    explicit TSSBF(const Params *p);

    SSBFCell *find(Addr key);

    InstSeqNum findYoungestInSet(Addr key);

    SSBFCell *allocate(Addr key);

    void touch(Addr key);

    void dump();

  private:
    Addr extractIndex(Addr key) const;
    Addr extractTag(Addr key) const;

    void checkAndRandEvictOldest(SSBFSet &set);
};

class SimpleSSBF: public SimObject
{
  private:
    const unsigned Size;
    const unsigned IndexBits;
    const Addr IndexMask;

    std::vector<InstSeqNum> SSBFTable;

    std::vector<unsigned long> tableAccCount;

    unsigned hash(Addr key);

  public:
    const unsigned addrShamt{3};

    typedef MemDepPredictorParams Params;
    explicit SimpleSSBF(const Params *p);

  public:
    InstSeqNum &find(Addr key);

    void touch(Addr key);

    void dump();
};

class MisPredTable
{
    const unsigned size{200};

    struct MisPredPair {
        Addr pc;
        uint64_t count;
        uint64_t fpCount;
        uint64_t fnCount;
    };

    std::list<MisPredPair> misPredRank;

    using RankIt = std::list<MisPredPair>::iterator;

    std::unordered_map<Addr, RankIt> misPredTable;

  public:
    void dump() const;

    void record(Addr pc, bool fn);
};

struct LocalPredCell
{
    const unsigned historyLen;

    DistancePair distPair;

    bool recentUsed{false};
    bool recentTouched{false};
    bool active{false};
    boost::dynamic_bitset<> history;
    InstSeqNum lastUpdate;

    unsigned count{0};
    int numSpeculativeBits{0};

    std::vector<SignedSatCounter> weights;

    int32_t theta;

    LocalPredCell()
            : historyLen(12),
              history(historyLen),
              weights(historyLen + 1, SignedSatCounter(5, 0)),
              theta(static_cast<int32_t>(1.93 * historyLen + 14.0))
    {}

    static int b2s(bool bypass);

    void fit(MemPredHistory *history, bool should_bypass);

    int32_t predict();
};

class LocalPredictor: public SimObject
{
  public:
    using Table = std::map<Addr, LocalPredCell>;

    typedef MemDepPredictorParams Params;
    explicit LocalPredictor(const Params *p);
  private:
    const unsigned historyLen{12};

    const bool perceptron{false};

    Table instTable;

    Table::iterator pointer;

    const unsigned size{32};

    const unsigned predTableSize{256};

    const unsigned indexMask{256-1};

    const unsigned resetCount{16};

    const unsigned activeThres{16};

    unsigned touchCount{0};

    unsigned useCount{0};

    void clearUseBit();

    void clearTouchBit();

    std::vector<SignedSatCounter> predTable;

    unsigned extractIndex(Addr pc, const boost::dynamic_bitset<> &hist) const;

    Table::iterator evictOneInst();

  public:
    void recordMispred(Addr pc, bool should_bypass, unsigned int sn_dist, unsigned int dq_dist, MemPredHistory *hist);

    void recordCorrect(Addr pc, bool should_bypass, unsigned int sn_dist, unsigned int dq_dist,
                       MemPredHistory *&history);

    // valid, bypass, pair
    std::tuple<bool, bool, DistancePair> predict(Addr pc, MemPredHistory* &history);

    void update(Addr pc, bool should_bypass,
                unsigned sn_dist, unsigned dq_dist, MemPredHistory* &history);

    const std::string _name;

    const std::string name() const override {return _name;}

    void recordSquash(Addr pc, MemPredHistory *&history);

};

struct MetaCell
{
    float pcMissRate{0.5};
    float pathMissRate{0.5};
    float patternMissRate{0.5};
};

class MetaPredictor: public SimObject
{
  public:
    typedef MemDepPredictorParams Params;

    explicit MetaPredictor (const Params *p)
            : SimObject(p),
              table(size),
              squashFactor(p->SquashFactor),
              _name("MetaPredictor")

    {}

    const unsigned pcShamt{2};

    const unsigned size{16};

    const unsigned indexMask{size - 1};

    std::vector<MetaCell> table;

    void record(Addr load_pc, bool should, bool pc, bool path, bool pattern, bool pattern_sensitive,
                bool will_squash);

    enum WhichPredictor{
        UsePC = 0,
        UsePath,
        UsePattern
    };

    const unsigned squashFactor;

    WhichPredictor choose(Addr load_pc);

    std::map<Addr, bool> blackList;

    const std::string _name;

    const std::string name() const override {return _name;}

};

class MemDepPredictor: public SimObject
{
  public:
    typedef MemDepPredictorParams Params;

    explicit MemDepPredictor(const Params *p);

    const std::string _name;

    const std::string name() const override {return _name;}

    void touchSSBF(Addr eff_addr, InstSeqNum ssn);

    void completeStore(Addr eff_addr, InstSeqNum ssn);

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

    using FoldedPC = uint64_t;

    using MemPredSet = std::map<Addr, MemPredCell>;

    using MemPredTable = std::vector<MemPredSet>;

    SSBFCell *debug;

  private:
    MemPredTable pcTable;
    MemPredTable pathTable;

    LocalPredictor localPredictor;

    MetaPredictor meta;

  public:
    TSSBF tssbf;

    SimpleSSBF sssbf;

    std::pair<bool, DistancePair> predict(Addr load_pc, FoldedPC path, MemPredHistory *&hist);

    std::pair<bool, DistancePair> predict(Addr load_pc, MemPredHistory *&hist);

    void recordPath(Addr control_pc, bool is_call, bool pred_taken);

    const unsigned callShamt{2};
    const unsigned branchShamt{1};
    FoldedPC controlPath{};

    void checkSilentViolation(
            InstSeqNum load_sn, Addr load_pc, Addr load_addr, uint8_t load_size,
            SSBFCell *last_store_cell,
            unsigned sn_dist, unsigned dq_dist,
            MemPredHistory *&hist);

    void recordCorrect(Addr pc, bool should_bypass, unsigned int sn_dist, unsigned int dq_dist,
                       MemPredHistory *&history);

    void update(Addr load_pc, bool should_bypass,
                unsigned sn_dist, unsigned dq_dist,
                MemPredHistory *&hist);

    void squash(MemPredHistory* &hist);

    void clear();

    void commitStore(Addr eff_addr, uint8_t eff_size,
                     InstSeqNum sn, const BasePointer &position);

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

    MisPredTable misPredTable;

  public:
    void dumpTopMisprediction() const;

    void squashLoad(Addr pc, MemPredHistory *&hist);
};

#endif // __MEM_DEP_PRED_HH__
