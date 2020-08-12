#ifndef __MEM_DEP_PRED_HH__
#define __MEM_DEP_PRED_HH__


#include <map>
#include <vector>

#include "cpu/pred/sat_counter.hh"
#include "params/MemDepPredictor.hh"
#include "sim/sim_object.hh"

struct MemPredCell
{
    SignedSatCounter conf;
    unsigned distance{};

    MemPredCell (unsigned counter_bits):
        conf(counter_bits)
    {}
};

struct MemPredHistory
{
    bool bypass;
    bool pcBypass;
    bool pathBypass;

    bool pathSensitive;
    unsigned storeDistance;
    using FoldedPC = unsigned;
    FoldedPC path;
};


class MemDepPredictor: public SimObject
{
  public:
    typedef MemDepPredictorParams Params;

    MemDepPredictor(const Params *p);

    const std::string _name;

    const std::string name() const {return _name;}

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

    MemPredTable pcTable;
    MemPredTable pathTable;

    bool predict(Addr load_pc, FoldedPC path, void *&hist);

    bool predict(Addr load_pc, void *&hist);

    void recordPath(Addr control_pc, bool isCall);

    const unsigned callShamt{2};
    const unsigned branchShamt{1};
    unsigned controlPath{};

    void update(Addr load_pc, bool should_bypass,
            unsigned actual_dist, void* &hist);

    void squash(void* &hist);

  private:

    Addr genPathKey(Addr pc, FoldedPC path);

    Addr extractIndex(Addr key, bool isPath);

    Addr extractTag(Addr key, bool isPath);

    std::pair<bool, MemPredCell *> find(MemPredTable &table, Addr key, bool isPath);

    MemPredCell *allocate(MemPredTable &table, Addr key, bool isPath);

    void decrement(Addr pc, FoldedPC path);

    void decrement(MemPredTable &table, Addr key, bool alloc, bool isPath);

    void increment(MemPredTable &table, Addr key, unsigned dist, bool isPath);

    FoldedPC getPath();
};

#endif // __MEM_DEP_PRED_HH__
