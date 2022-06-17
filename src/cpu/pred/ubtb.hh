#ifndef __CPU_PRED_UBTB_HH__
#define __CPU_PRED_UBTB_HH__


#include "base/types.hh"
#include "cpu/inst_seq.hh"
#include "cpu/pred/stream_struct.hh"
#include "cpu/pred/timed_pred.hh"
#include "sim/sim_object.hh"

class StreamUBTB : public TimedPredictor {
    private:
    const unsigned delay{1};

    public:
    void tickStart() override;
    void tick() override;
    void putPCHistory(Addr pc, const boost::dynamic_bitset<> &history) override;
    unsigned getDelay() override { return delay; }

    StreamPrediction getStream();
};


#endif  // __CPU_PRED_UBTB_HH__
