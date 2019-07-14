//
// Created by zyy on 19-6-11.
//

#ifndef __FF_FU_WRAPPER_HH__
#define __FF_FU_WRAPPER_HH__

#include <queue>
#include <unordered_map>

#include "cpu/forwardflow/comm.hh"
#include "cpu/forwardflow/dq_pointer.hh"

//#include "cpu/forwardflow/dyn_inst.hh"
#include "cpu/func_unit.hh"
#include "params/FFFUPool.hh"

namespace FF{

struct SingleFUWrapper {
    bool isPipelined;
    bool isSingleCycle;
    bool isLongLatency;
    bool isLSU;
    bool writtenThisCycle;

    unsigned latency;
    unsigned cycleLeft;  // for long latency
    unsigned MaxPipeLatency;

    std::queue<DQPointer> pipelinedPointers;
    DQPointer longLatencyPointer;
    DQPointer oneCyclePointer;

    void init(bool pipe, bool single_cycle, bool long_lat,
                    unsigned latency, unsigned max_pipe_lat);

    void markWb() {
        assert(isLongLatency);
        assert(isLSU);
        cycleLeft = 1;
    }
};


template<class Impl>
class FUWrapper {
    FuncUnit fu;
public:
//    using DynInstPtr = BaseO3DynInst<Impl>*;
    typedef typename Impl::DynInstPtr DynInstPtr;

    bool canServe(DynInstPtr &inst);

    bool consume(DynInstPtr &inst);

//    bool transferPointer();
//    bool transferValue();

    void tick();
    // tick will
    // clear this cycle
    // check status of buffers
    // assert no output hazard
    // check input and schedule, response to requests
    // "execute" tasks scheduled in last cycle
    //
    // write back ptr (wake up), write back value
    // (use routes acquired by last cycle) (two actions can be swapped)

    bool pointerTransferred;
    bool valueTransferred;

    void startCycle();

    DQPointer toWakeup;

    typedef FFFUPoolParams Params;

    FUWrapper();

    void init(const Params *p, unsigned bank_id);

    const std::string name() { return "FUWrapper";}
private:

    std::unordered_map<OpClass, SingleFUWrapper> wrappers;

//    std::vector<Value> buffer(num_fu);

    std::array<unsigned, Num_OpClasses> opLat; // {Num_OpClasses};
    std::array<bool, Num_OpClasses> canPipelined; // {Num_OpClasses};
    std::bitset<Num_OpClasses> capabilityList;

    std::bitset<Impl::MaxOpLatency> wbScheduled;
    std::bitset<Impl::MaxOpLatency> wbScheduledNext;

    void setWakeup();

    void advance();

    void endCycle();

    DQPointer inv;
    unsigned numFU;

};

}
#endif //__FF_FU_WRAPPER_HH__
