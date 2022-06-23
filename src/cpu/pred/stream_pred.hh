#ifndef __CPU_PRED_STREAM_PRED_HH__
#define __CPU_PRED_STREAM_PRED_HH__

#include <vector>

#include <boost/dynamic_bitset.hpp>

#include "base/statistics.hh"
#include "base/types.hh"
#include "cpu/inst_seq.hh"
#include "cpu/pred/stream_struct.hh"
#include "cpu/pred/timed_pred.hh"
#include "cpu/static_inst.hh"
#include "params/StreamPredictor.hh"
#include "sim/sim_object.hh"

class StreamPredictor : public TimedPredictor {
    public:
    typedef StreamPredictorParams Params;

    private:
    const unsigned delay{2};

    public:
    StreamPredictor(const Params &p);

    void tickStart() override;
    void tick() override;
    void putPCHistory(Addr pc, const boost::dynamic_bitset<> &history) override;
    unsigned getDelay() override { return delay; }

    std::pair<Addr, StreamPrediction> getStreamS1();

    void update(const PredictionID id, Addr stream_start_pc, Addr control_pc, Addr target, bool is_conditional,
                bool is_indirect, bool actually_taken, const boost::dynamic_bitset<> &history) override;
};


#endif // __CPU_PRED_STREAM_PRED_HH__
