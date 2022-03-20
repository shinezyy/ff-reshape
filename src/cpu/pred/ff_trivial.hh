#ifndef GEM5_FF_TRIVIAL_H
#define GEM5_FF_TRIVIAL_H

#include <deque>
#include <map>
#include <queue>
#include <random>
#include <utility>

#include "base/statistics.hh"
#include "base/types.hh"
#include "cpu/inst_seq.hh"
#include "cpu/pred/btb.hh"
#include "cpu/pred/ff_bpred_unit.hh"
#include "cpu/pred/tage_base.hh"
#include "cpu/static_inst.hh"
#include "params/FFTrivialBP.hh"
#include "params/TAGE.hh"

class FFTrivialBP : public FFBPredUnit
{
public:

    FFTrivialBP(const FFTrivialBPParams &params);

    Addr lookup(ThreadID tid, const TheISA::PCState &pc, const StaticInstPtr &inst, void * &bp_history) override;

    void update(ThreadID tid, const TheISA::PCState &pc,
                void *bp_history, bool squashed,
                const StaticInstPtr &inst,
                Addr pred_DBB, Addr corr_DBB) override;

    void squash(ThreadID tid, void *bp_history) override;

private:
    TAGEBase *tage;

    struct TAGEState {
        TAGEState(TAGEBase &tage) : info(tage.makeBranchInfo())
        {}
        virtual ~TAGEState()
        {
            delete info;
        }
        TAGEBase::BranchInfo *info;
        bool taken;
        Addr predPC;
    };

    struct BPState {
        unsigned int preRunCount;
        TheISA::PCState pc;
        std::deque<TAGEState *> inflight;
    } state;

private:
    void specLookup(int numInst, ThreadID tid);
    void resetSpecLookup(const TheISA::PCState &pc0);

private:
    unsigned int numLookAhead;

    DefaultBTB BTB;

    // ICache is brought from cpu/pred/btb.cc
    class ICache
    {
    private:
        struct ICEntry
        {
            ICEntry()
                : tag(0), inst(nullptr), valid(false)
            {}

            /** The entry's tag. */
            Addr tag;

            /** The entry's target. */
            StaticInstPtr inst;

            TheISA::PCState pcState;

            /** The entry's thread id. */
            ThreadID tid;

            /** Whether or not the entry is valid. */
            bool valid;
        };

    public:
        /** Creates a BTB with the given number of entries, number of bits per
         *  tag, and instruction offset amount.
         *  @param numEntries Number of entries for the BTB.
         *  @param instShiftAmt Offset amount for instructions to ignore alignment.
         */
        ICache(unsigned numEntries,
                unsigned instShiftAmt, unsigned numThreads);

        void reset();

        /** Looks up an address in the BTB. Must call valid() first on the address.
         *  @param inst_PC The address of the branch to look up.
         *  @param tid The thread id.
         *  @return Returns pointer to static inst.
         */
        std::pair<StaticInstPtr, TheISA::PCState> lookup(Addr instPC, ThreadID tid);

        /** Checks if a branch is in the BTB.
         *  @param inst_PC The address of the branch to look up.
         *  @param tid The thread id.
         *  @return Whether or not the branch exists in the BTB.
         */
        bool valid(Addr instPC, ThreadID tid);

        /** Updates the BTB with the target of a branch.
         *  @param inst_PC The address of the branch being updated.
         *  @param target_PC The target address of the branch.
         *  @param tid The thread id.
         */
        void update(Addr instPC, StaticInstPtr inst, const TheISA::PCState &pcState,
                    ThreadID tid);

    private:
        /** Returns the index into the BTB, based on the branch's PC.
         *  @param inst_PC The branch to look up.
         *  @return Returns the index into the BTB.
         */
        inline unsigned getIndex(Addr instPC, ThreadID tid);

        /** Returns the tag bits of a given address.
         *  @param inst_PC The branch's address.
         *  @return Returns the tag bits.
         */
        inline Addr getTag(Addr instPC);

        /** The actual I$. */
        std::vector<ICEntry> set;

        /** The number of entries in the BTB. */
        unsigned numEntries;

        /** The index mask. */
        unsigned idxMask;

        /** The number of tag bits per entry. */
        unsigned tagBits;

        /** Number of bits to shift PC when calculating index. */
        unsigned instShiftAmt;

        /** Number of bits to shift PC when calculating tag. */
        unsigned tagShiftAmt;

        /** Log2 NumThreads used for hashing threadid */
        unsigned log2NumThreads;
    }
    icache;

    struct FFTrivialBPStats : public Stats::Group
    {
        FFTrivialBPStats(Stats::Group *parent);

        Stats::Scalar icache_lookups;

        Stats::Scalar btb_lookups;

        Stats::Scalar icache_hits;

        Stats::Scalar btb_hits;

        Stats::Formula icache_hit_ratio;

        Stats::Formula btb_hit_ratio;

    } stats;
};

#endif //GEM5_FF_TRIVIAL_H
