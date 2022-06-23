#ifndef __CPU_PRED_TIMED_PRED_HH__
#define __CPU_PRED_TIMED_PRED_HH__


#include <boost/dynamic_bitset.hpp>

#include "base/types.hh"
#include "cpu/inst_seq.hh"
#include "params/TimedPredictor.hh"
#include "sim/sim_object.hh"

class TimedPredictor: public SimObject {
    public:

    typedef TimedPredictorParams Params;

    TimedPredictor(const Params &params);

    virtual void tickStart() {}
    virtual void tick() {}
    virtual void putPCHistory(Addr pc, const boost::dynamic_bitset<> &history) {}

    virtual unsigned getDelay() {return 0;}

    virtual void update(const PredictionID pred_id, Addr stream_start_pc, Addr control_pc, Addr target,
                        bool is_conditional, bool is_indirect, bool actually_taken,
                        const boost::dynamic_bitset<> &history)
    {
    }
};

#endif // __CPU_PRED_TIMED_PRED_HH__
