//
// Created by zyy on 19-6-11.
//

#ifndef __FF_FU_WRAPPER_HH__
#define __FF_FU_WRAPPER_HH__

#include "cpu/forwardflow/comm.hh"
#include "cpu/forwardflow/dq_pointer.hh"
#include "cpu/forwardflow/dyn_inst.hh"
#include "cpu/func_unit.hh"

namespace FF{

struct SingleFUWrapper {
    bool isPipelined;
    bool isSingleCycle;
    bool isLongLatency;
    bool writtenThisCycle;

    unsigned latency;
    unsigned cycleLeft;  // for long latency
    unsigned MaxPipeLatency;

    std::queue<DQPointer> pipelinedPointers;
    DQPointer longLatencyPointer;
    DQPointer oneCyclePointer;

    SingleFUWrapper(bool pipe, bool single_cycle, bool long_lat,
                    unsigned latency,
                    unsigned max_pipe_lat):
            isPipelined(pipe),
            isSingleCycle(single_cycle),
            isLongLatency(long_lat),
            latency(latency),
            MaxPipeLatency(max_pipe_lat){
        DQPointer inv;
        inv.valid = false;

        for (unsigned i = 0; i < MaxPipeLatency; i++) {
            pipelinedPointers.push(inv);
        }
    };
};


template<class Impl>
class FUWrapper {
    FuncUnit fu;
public:
    using DynInstPtr = BaseO3DynInst<Impl>*;
//    typedef typename Impl::DynInstPtr DynInstPtr;

    bool canServe(DynInstPtr &inst);

    bool consume(DynInstPtr &inst);

    bool transferPointer();
    bool transferValue();

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

    void setPtrQueue(Queue);

    void setValueQueue(Queue);

    DQPointer toWakeup;

    FUWrapper();
private:

    std::unordered_map<OpClass, SingleFUWrapper> wrappers;

//    std::vector<Value> buffer(num_fu);

    std::vector<unsigned> opLat{Num_OpClasses};
    std::vector<bool> canPipelined{Num_OpClasses};

    std::bitset<Num_OpClasses> capabilityList;

    unsigned MaxLatency{6};
    std::bitset<MaxLatency> wbScheduled;
    std::bitset<MaxLatency> wbScheduledNext;

    void setWakeup();

    void advance();

    void endCycle();

    DQPointer inv;
};

}
#endif //__FF_FU_WRAPPER_HH__
