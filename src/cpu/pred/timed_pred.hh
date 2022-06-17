#ifndef __CPU_PRED_TIMED_PRED_HH__
#define __CPU_PRED_TIMED_PRED_HH__


#include <boost/dynamic_bitset.hpp>

#include "base/types.hh"
#include "cpu/inst_seq.hh"
#include "sim/sim_object.hh"

class TimedPredictor: public SimObject {
    public:
    virtual void tickStart() = 0;
    virtual void tick() = 0;
    virtual void putPCHistory(Addr pc, const boost::dynamic_bitset<> &history) = 0;

    virtual unsigned getDelay() = 0;


};

#endif // __CPU_PRED_TIMED_PRED_HH__