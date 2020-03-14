//
// Created by zyy on 19-6-11.
//

#ifndef __FF_ARCH_REGFILE_HH__
#define __FF_ARCH_REGFILE_HH__

#include <random>
#include <tuple>
#include <unordered_map>

#include "cpu/forwardflow/comm.hh"
#include "cpu/forwardflow/dataflow_queue_common.hh"
#include "cpu/forwardflow/dq_pointer.hh"
#include "cpu/reg_class.hh"

//#include "cpu/forwardflow/dyn_inst.hh"

struct DerivFFCPUParams;

namespace FF {

using std::tuple;

template<class Impl>
class ArchState {
private:
    //Typedefs from Impl
    typedef typename Impl::CPUPol CPUPol;
    typedef typename Impl::DynInstPtr DynInstPtr;
//    using DynInstPtr = BaseO3DynInst<Impl>*;
    typedef typename Impl::O3CPU O3CPU;
    typedef typename CPUPol::DIEWC DIEWC;
    typedef typename CPUPol::DQTop DQTop;

//    const unsigned WritePorts, ReadPorts;
//
//    unsigned writes, reads;

    // data structures here:

    // arch reg file
    // newest value in RF?
    // arch reg pointers: last use and last def

    std::unordered_map<int, FFRegValue> intArchRF;
    std::unordered_map<int, FFRegValue> floatArchRF;

    using RenameMap = std::unordered_map<RegId, DQPointer>;
    RenameMap renameMap; // forward

    RenameMap parentMap; // forward

    using SBIndex = std::pair<RegClass, RegIndex>;
    using Scoreboard = std::unordered_map<SBIndex, bool>;
    Scoreboard scoreboard;

    using ReverseTable = std::unordered_map<SBIndex, DQPointer>;
    ReverseTable reverseTable; // indicate that sb xx is clear by dq xxx

    struct Checkpoint {
        RenameMap renameMap;
        RenameMap parentMap;
        Scoreboard scoreboard;
        ReverseTable reverseTable;
    };
    std::map<InstSeqNum, Checkpoint> cpts;
    // std::unordered_map<InstSeqNum, Checkpoint> cpts;

    const unsigned MaxCheckpoints;

    DIEWC *diewc;

    DQTop *dq;

    void commitInstInSB(DynInstPtr &inst, Scoreboard &sb, ReverseTable &rt, const RegId &dest);


    std::unordered_map<int, FFRegValue> hintIntRF;
    std::unordered_map<int, FFRegValue> hintFloatRF;

    ReverseTable hintRT;

    Scoreboard hintSB;

public:
    std::pair<bool, FFRegValue> commitInst(DynInstPtr &inst);

    void postExecInst(DynInstPtr &inst);

    // todo: update map to tell its parent or sibling where to forward
    std::list<PointerPair> recordAndUpdateMap(DynInstPtr &inst);

//    void clearCounters();
//
//    bool checkRWLimit();

    bool checkpointsFull();

    bool takeCheckpoint(DynInstPtr &inst);

    void recoverCPT(DynInstPtr &inst);

    void recoverCPT(InstSeqNum &num);

    explicit ArchState(DerivFFCPUParams *);

    uint64_t readIntReg(int reg_idx);
    void setIntReg(int reg_idx, uint64_t);

    double readFloatReg(int reg_idx);
    void setFloatReg(int reg_idx, double);

    uint64_t readFloatRegBits(int reg_idx);
    void setFloatRegBits(int reg_idx, uint64_t);

    const std::string name() {return "arch_state";}

    void setDIEWC(DIEWC *_diewc);

    void setDQ(DQTop *_dq);

    InstSeqNum getYoungestCPTBefore(InstSeqNum violator);

    void squashAll();

    void dumpMaps();

    // is LF source, is LF drain
    std::pair<bool, bool> forwardAfter(DynInstPtr &inst, std::list<DynInstPtr> &need_forward);

    void regStats();

    Stats::Vector numBusyOperands;
    Stats::Vector numDispInsts;
    Stats::Formula meanBusyOp[Impl::MaxOps];

    Stats::Scalar CombRename;
    Stats::Scalar RegReadCommitSB;
    Stats::Scalar RegWriteCommitSB;
    Stats::Scalar RegReadSpecSB;
    Stats::Scalar RegWriteSpecSB;
    Stats::Scalar RegReadMap;
    Stats::Scalar RegWriteMap;
    Stats::Scalar RegReadRT;
    Stats::Scalar RegWriteRT;
    Stats::Scalar RegReadSpecRT;
    Stats::Scalar RegWriteSpecRT;
    // checkpoint
    Stats::Scalar SRAMWriteMap;
    Stats::Scalar SRAMWriteSB;
    Stats::Scalar SRAMWriteRT;
    // recover
    Stats::Scalar SRAMReadMap;
    Stats::Scalar SRAMReadSB;
    Stats::Scalar SRAMReadRT;

    Stats::Scalar RegReadARF;
    Stats::Scalar RegWriteARF;
    Stats::Scalar RegReadSpecARF;
    Stats::Scalar RegWriteSpecARF;

private:
    std::mt19937 gen;

    void randomizeOp(DynInstPtr& inst);

    const bool decoupleOpPosition;

    const bool readyHint;

    void countChild(DQPointer parent_ptr, DynInstPtr &inst);
};

}

#endif //__FF_ARCH_REGFILE_HH__
