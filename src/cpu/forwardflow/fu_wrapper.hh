//
// Created by zyy on 19-6-11.
//

#ifndef __FF_FU_WRAPPER_HH__
#define __FF_FU_WRAPPER_HH__

#include <deque>
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

    bool active;
    bool writtenThisCycle;
    bool hasPendingInst;
    bool isLSU;
    unsigned pipeInstCount;

    unsigned op;
    unsigned latency;
    unsigned MaxPipeLatency;

    struct SFUTimeReg {
        bool hasPendingInst;
        InstSeqNum seq;
        std::array<WKPointer, 2> oneCyclePointer;
        std::array<WKPointer, 2> longLatencyPointer;
        unsigned cycleLeft;
    };
    SFUTimeReg timeReg;
    SFUTimeReg *toNextCycle;
    SFUTimeReg *fromLastCycle;

    InstSeqNum seq;
    std::array<WKPointer, 2> oneCyclePointer;
    std::array<WKPointer, 2> longLatencyPointer;
    unsigned cycleLeft;  // for long latency

    struct PipelineStruct {
        bool valid;
        std::array<WKPointer, 2> pointer;
        InstSeqNum seq;
    };

    std::deque<PipelineStruct> pipelineQueue;

    void init(bool pipe, bool single_cycle, bool long_lat,
                    unsigned latency, unsigned max_pipe_lat, unsigned op_);

    void markWb() {
        assert(isLongLatency);
        assert(isLSU);
        cycleLeft = 1;
    }
    bool checkActive();
};


template<class Impl>
class FUWrapper {
public:

#ifdef __JETBRAINS_IDE__
    using DynInstPtr = BaseO3DynInst<Impl>*;
#else
    typedef typename Impl::DynInstPtr DynInstPtr;
#endif

    typedef typename Impl::CPUPol::LSQ LSQ;

    typedef typename Impl::CPUPol::DataflowQueues DQ;

    typedef typename Impl::CPUPol::DIEWC Exec;

private:

    LSQ *ldstQueue;

    DQ *dq;

    Exec *exec;

public:
    void setLSQ(LSQ *lsq);

    void setDQ(DQ *_dq);

    void setExec(Exec *_exec);

    bool canServe(DynInstPtr &inst, InstSeqNum &waitee);

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

    using WBPair = std::array<WKPointer, 2>;

    struct WBInfo {
        WBPair wbPair;
        InstSeqNum seq;

        WKPointer& operator [] (int index) {
            return wbPair[index];
        }

        explicit WBInfo ()
        {
            wbPair[0].valid = false;
            wbPair[1].valid = false;
            seq = 0;
        }

        WBInfo(const WBPair &p, InstSeqNum _seq)
        {
            wbPair = p;
            seq = _seq;
        }
    };

    WBPair toWakeup;

    std::list<WBInfo> wbQueue;

    using WBPos = typename std::list<WBInfo>::iterator;

    struct ExecInfo {
        InstSeqNum seq;
        WBPos wbPos;

        ExecInfo(InstSeqNum _seq, const WBPos &p)
        {
            seq = _seq;
            wbPos = p;
        }
    };

    std::list<ExecInfo> execQueue;

    typedef FFFUPoolParams Params;

    FUWrapper();

    void init(const Params *p, unsigned gid, unsigned bank_id);

    void fillMyBitMap(std::vector<std::vector<bool>> &v, unsigned bank);

    void fillLatTable(std::unordered_map<OpClass, unsigned> &v);

    std::string name() const { return _name;}

    std::string _name;
private:

    std::vector<SingleFUWrapper> wrappersVec;
    std::unordered_map<OpClass, SingleFUWrapper*> wrappers;
    std::unordered_map<InstSeqNum, DynInstPtr> insts;

//    std::vector<Value> buffer(num_fu);

    std::array<unsigned, Num_OpClasses> opLat; // {Num_OpClasses};
    std::array<bool, Num_OpClasses> canPipelined; // {Num_OpClasses};
    std::bitset<Num_OpClasses> capabilityList;
    std::bitset<Num_OpClasses> usedCapa;

    unsigned wrapperID;

public:
    void endCycle();

    void executeInsts();

    void setWakeup();

    void setWakeupPostExec();

    void squash(InstSeqNum squash_seq);

private:

    WKPointer inv;
    unsigned numFU;

    bool active;

    unsigned groupID;

    int setInstWakeup(
            const WBPair &to_wakeup, InstSeqNum seq);
};

}
#endif //__FF_FU_WRAPPER_HH__
