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
#include "cpu/timebuf.hh"
#include "params/FFFUPool.hh"

namespace FF{

struct SingleFUWrapper {
    bool isPipelined;
    bool isSingleCycle;
    bool isLongLatency;
    bool isLSU;
    bool writtenThisCycle;


    unsigned latency;
    unsigned MaxPipeLatency;

    bool hasPendingInst;
    InstSeqNum seq;

    struct PipelineStruct {
        bool valid;
        DQPointer pointer;
        InstSeqNum seq;
    };

    std::queue<PipelineStruct> pipelineQueue;

    DQPointer longLatencyPointer;
    unsigned cycleLeft;  // for long latency

    DQPointer oneCyclePointer;

    void init(bool pipe, bool single_cycle, bool long_lat,
                    unsigned latency, unsigned max_pipe_lat);

    struct SFUTimeReg {
        bool hasPendingInst;
        InstSeqNum seq;
        DQPointer oneCyclePointer;
        DQPointer longLatencyPointer;
        unsigned cycleLeft;
    };

    TimeBuffer<SFUTimeReg> *timeStruct;
    typename TimeBuffer<SFUTimeReg>::wire toNextCycle;
    typename TimeBuffer<SFUTimeReg>::wire fromLastCycle;

    void markWb() {
        assert(isLongLatency);
        assert(isLSU);
        cycleLeft = 1;
    }
};


template<class Impl>
class FUWrapper {
public:

//    using DynInstPtr = BaseO3DynInst<Impl>*;
    typedef typename Impl::DynInstPtr DynInstPtr;

    typedef typename Impl::CPUPol::LSQ LSQ;

    typedef typename Impl::CPUPol::DataflowQueues DQ;

    typedef typename Impl::CPUPol::DIEWC Exec;

private:
    FuncUnit fu;

    LSQ *ldstQueue;

    DQ *dq;

    Exec *exec;

public:
    void setLSQ(LSQ *lsq);

    void setDQ(DQ *_dq);

    void setExec(Exec *_exec);

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

    bool toExec;

    InstSeqNum seqToExec;

    typedef FFFUPoolParams Params;

    FUWrapper();

    void init(const Params *p, unsigned bank_id);

    void fillMyBitMap(std::vector<std::vector<bool>> &v, unsigned bank);

    void fillLatTable(std::unordered_map<OpClass, unsigned> &v);

    const std::string name() { return _name;}

    std::string _name;
private:

    std::unordered_map<OpClass, SingleFUWrapper> wrappers;
    std::unordered_map<InstSeqNum, DynInstPtr> insts;

//    std::vector<Value> buffer(num_fu);

    std::array<unsigned, Num_OpClasses> opLat; // {Num_OpClasses};
    std::array<bool, Num_OpClasses> canPipelined; // {Num_OpClasses};
    std::bitset<Num_OpClasses> capabilityList;

    std::bitset<Impl::MaxOpLatency> wbScheduled;
    std::bitset<Impl::MaxOpLatency> wbScheduledNext;

    unsigned wrapperID;

public:
    void advance();

    void endCycle();

    void executeInsts();

    void setWakeup();

private:

    DQPointer inv;
    unsigned numFU;
};

}
#endif //__FF_FU_WRAPPER_HH__
