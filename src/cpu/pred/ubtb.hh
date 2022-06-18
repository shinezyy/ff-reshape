#ifndef __CPU_PRED_UBTB_HH__
#define __CPU_PRED_UBTB_HH__


#include "base/types.hh"
#include "cpu/inst_seq.hh"
#include "cpu/pred/stream_struct.hh"
#include "cpu/pred/timed_pred.hh"
#include "params/StreamUBTB.hh"
#include "sim/sim_object.hh"

class StreamUBTB : public TimedPredictor {
    public:
    typedef StreamUBTBParams Params;

    private:
    const unsigned delay{1};

    public:
    StreamUBTB(const Params &p);
    void tickStart() override;
    void tick() override;
    void putPCHistory(Addr pc, const boost::dynamic_bitset<> &history) override;
    unsigned getDelay() override { return delay; }

    StreamPrediction getStream();

    void update(const InstSeqNum sn, Addr control_pc, Addr target, bool is_conditional, bool is_indirect,
                        bool actually_taken, std::shared_ptr<void> bp_history) override;
};


#endif  // __CPU_PRED_UBTB_HH__
