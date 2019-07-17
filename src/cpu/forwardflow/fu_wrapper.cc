//
// Created by zyy on 19-6-11.
//

#include "cpu/forwardflow/fu_wrapper.hh"

#include <sstream>

#include "base/trace.hh"
#include "cpu/op_class.hh"
#include "debug/FUPipe.hh"
#include "debug/FUSched.hh"
#include "debug/FUW.hh"
#include "fu_wrapper.hh"
#include "params/FFFUPool.hh"
#include "params/FUDesc.hh"
#include "params/OpDesc.hh"
#include "sim/sim_object.hh"


namespace FF{

using namespace std;

template<class Impl>
bool FUWrapper<Impl>::canServe(DynInstPtr &inst) {
    assert(inst);
    DPRINTF(FUW, "FUW reach 0, opclass:%d\n", inst->opClass());
    auto lat = opLat[inst->opClass()];
    DPRINTF(FUW, "FUW reach 1\n");
    auto has_capability = capabilityList[inst->opClass()];
    DPRINTF(FUW, "FUW reach 2\n");
    auto wb_port_already_scheduled = wbScheduled[lat];
    DPRINTF(FUW, "FUW reach 3\n");
    return has_capability && !wb_port_already_scheduled;
}

template<class Impl>
bool FUWrapper<Impl>::consume(FUWrapper::DynInstPtr &inst) {

    auto lat = opLat[inst->opClass()];
    SingleFUWrapper &wrapper = wrappers[inst->opClass()];
    assert(lat == wrapper.latency);

    insts[inst->opClass()] = inst;
    inst->fuGranted = true;

    DPRINTF(FUW, "Consuming inst[%d]\n", inst->seqNum);

    DQPointer &dest = inst->pointers[0];

    wrapper.seq = inst->seqNum;
    wrapper.hasPendingInst = true;
    // schedule wake up
    if (wrapper.isSingleCycle) {
        // schedule wb port
        assert(!wbScheduledNext[0]);
        wbScheduledNext[0] = true;
        DPRINTF(FUW, "wbScheduledNext: ");
        if (Debug::FUW) {
            cout << wbScheduledNext << endl;
        }

        wrapper.oneCyclePointer = dest;
        DPRINTF(FUW, "add inst[%i] into 1-cycle wrapper (%i, %i)\n",
                inst->seqNum, wrapperID, inst->opClass());

    } else if (wrapper.isPipelined) {
        // schedule wb port
        assert(!wbScheduledNext[lat]);
        wbScheduledNext[lat] = true;

        DPRINTF(FUPipe, "wrapper(%i, %i) is pipelined, size:%u, will push\n",
                wrapperID, inst->opClass(), wrapper.pipelinedPointers.size());
        wrapper.pipelinedPointers.push(dest);
        wrapper.pipelinedValid.push(true);
        assert(!wrapper.writtenThisCycle);
        wrapper.writtenThisCycle = true;


        DPRINTF(FUW, "add inst[%i] into pipelined wrapper (%i, %i)\n",
                inst->seqNum, wrapperID, inst->opClass());
    } else {
        assert(wrapper.isLongLatency);
        wrapper.longLatencyPointer = dest;
        DPRINTF(FUW, "add inst[%i] into LL wrapper (%i, %i)\n",
                inst->seqNum, wrapperID, inst->opClass());
    }

    return true;
}

template<class Impl>
void FUWrapper<Impl>::tick() {
    setWakeup();
    executeInsts();
}

template<class Impl>
void FUWrapper<Impl>::setWakeup() {
    bool set_wb_this_cycle = false;
    int count = 0;
    for (auto &pair: wrappers) {
        SingleFUWrapper &wrapper = pair.second;

        if (wrapper.isSingleCycle && wrapper.hasPendingInst) {
            DPRINTF(FUW, "Wakeing up one cycle inst[%d]\n", wrapper.seq);
            toWakeup = wrapper.oneCyclePointer;
            count += 1;
        }
        if (wrapper.isLongLatency && wrapper.hasPendingInst &&
                wrapper.cycleLeft == 1 &&
                wrapper.longLatencyPointer.valid) {
            DPRINTF(FUW, "Wakeing up LL inst[%d]\n", wrapper.seq);
            toWakeup = wrapper.longLatencyPointer;
            count += 1;
        }
        if (wrapper.isPipelined && wrapper.pipelinedValid.front()) {
            DPRINTF(FUW, "Wakeing up pipelined inst[%d]\n", wrapper.seq);
            toWakeup = wrapper.pipelinedPointers.front();
            count += 1;
        }
        assert(count <= 1);
        if (count) {
            assert(!set_wb_this_cycle);
            set_wb_this_cycle = true;
            opToWakeup = pair.first;
            toExec = true;
        }
    }
    if (wbScheduled[0]) {
        DPRINTF(FUW, "one instruction should be executed and its children"
                " should be waken up\n");
        assert(count > 0);
    } else {
        DPRINTF(FUSched, "wbScheduled[0]: %d, wbScheduled now: ", wbScheduled[0]);
        if (Debug::FUSched) {
            cout << wbScheduled << endl;
        }
    }
}

template<class Impl>
void FUWrapper<Impl>::startCycle() {
    toWakeup.valid = false;
    toExec = false;
    wbScheduled = wbScheduledNext;
    wbScheduledNext = wbScheduled >> 1;  // expect false shifted in
    DPRINTF(FUSched, "wbScheduled at start: ");
    if (Debug::FUSched) {
        cout << wbScheduled << endl;
    }
    DPRINTF(FUSched, "wbScheduledNext at start: ");
    if (Debug::FUSched) {
        cout << wbScheduledNext << endl;
    }
    for (auto &pair: wrappers) {
        SingleFUWrapper &wrapper = pair.second;
        wrapper.writtenThisCycle = false;
        wrapper.oneCyclePointer.valid = false;
    }
    advance();
}

template<class Impl>
void FUWrapper<Impl>::advance() {
    for (auto &pair: wrappers) {
        SingleFUWrapper &wrapper = pair.second;
        if (wrapper.isLongLatency && wrapper.hasPendingInst && !wrapper.isLSU) {
            wrapper.cycleLeft = std::max(wrapper.cycleLeft + 1, (unsigned) 0);
            if (wrapper.cycleLeft <= 1 && !wbScheduledNext[1]) {
                wbScheduledNext[1] = true;
            }
        }
        if (wrapper.isPipelined) {
            DPRINTF(FUPipe, "wrapper(%i, %i) is pipelined, size:%u, will pop\n",
                    wrapperID, pair.first, wrapper.pipelinedPointers.size());
            assert(wrapper.pipelinedPointers.size() == wrapper.MaxPipeLatency);
            wrapper.pipelinedPointers.pop();
            wrapper.pipelinedValid.pop();
        }
    }
}

template<class Impl>
void FUWrapper<Impl>::endCycle() {
    for (auto &pair: wrappers) {
        SingleFUWrapper &wrapper = pair.second;
        if (wrapper.isPipelined) {
            if (!wrapper.writtenThisCycle) {
                DPRINTF(FUPipe, "wrapper(%i, %i) is pipelined, size:%u, will push\n",
                        wrapperID, pair.first, wrapper.pipelinedPointers.size());
                wrapper.pipelinedPointers.push(inv);
                wrapper.pipelinedValid.push(false);
            } else {
                DPRINTF(FUPipe, "wrapper(%i, %i) has been written, size:%u, will not push\n",
                        wrapperID, pair.first, wrapper.pipelinedPointers.size());
            }
        }
    }
}

template<class Impl>
void FUWrapper<Impl>::init(const Params *p, unsigned bank)
{
    numFU = 0;

    wrapperID = bank;

    std::ostringstream s;
    s << "fuWrapper" << bank;
    _name = s.str();

    //  Iterate through the list of FUDescData structures
    //
    const std::vector<FUDesc *> &paramList = p->FUList;
    FUDDiterator i = paramList.begin() + bank;

    //
    //  Don't bother with this if we're not going to create any FU's
    //
    if ((*i)->number) {
        assert((*i)->number == 1);
        //
        //  Create the FuncUnit object from this structure
        //   - add the capabilities listed in the FU's operation
        //     description
        //
        //  We create the first unit, then duplicate it as needed
        //
        FuncUnit *fu = new FuncUnit;

        OPDDiterator j = (*i)->opDescList.begin();
        OPDDiterator end = (*i)->opDescList.end();
        for (; j != end; ++j) {
            // indicate that this pool has this capability
            capabilityList.set((*j)->opClass);
            opLat[(*j)->opClass] = static_cast<unsigned int>((*j)->opLat);

            // Add each of the FU's that will have this capability to the
            // appropriate queue.
            for (int k = 0; k < (*i)->number; ++k)
                wrappers[(*j)->opClass].init(
                        (*j)->pipelined && (!((*j)->opLat == 1)),
                        (*j)->opLat == 1,
                        (*j)->opLat > 1,
                        static_cast<unsigned int>((*j)->opLat),
                        20
                        ); // todo: fix

            // indicate that this FU has the capability
            fu->addCapability((*j)->opClass, (*j)->opLat, (*j)->pipelined);

            // todo: check here

            if (!(*j)->pipelined)
                canPipelined[(*j)->opClass] = false;
        }

        numFU++;

        //  Add the appropriate number of copies of this FU to the list
        std::ostringstream s;
        s << (*i)->name() + "(" << bank << ",0)";
        fu->name = s.str();

        for (int c = 1; c < (*i)->number; ++c) {
            numFU++;
            FuncUnit *fu2 = new FuncUnit(*fu);
            std::ostringstream s;

            s << (*i)->name() << "(" << bank << "," << c << ")";
            fu2->name = s.str();
        }
    }
    wbScheduledNext = 0;
    wbScheduled = 0;
}

template<class Impl>
FUWrapper<Impl>::FUWrapper()
{
    inv.valid = false;
}

template<class Impl>
void FUWrapper<Impl>::fillMyBitMap(std::vector<std::vector<bool>> &v,
        unsigned bank)
{
    for (unsigned i = 0; i < Num_OpClasses; i++) {
        v[i][bank] = capabilityList[i];
    }
}

template<class Impl>
void FUWrapper<Impl>::executeInsts()
{
    if (toExec) {
        DPRINTF(FUW, "toExec is valid, execute now!\n");
        exec->executeInst(insts[opToWakeup]);
    }
    DPRINTF(FUSched, "wbScheduled at end: ");
    if (Debug::FUSched) {
        cout << wbScheduled << endl;
    }
    DPRINTF(FUSched, "wbScheduledNext at end: ");
    if (Debug::FUSched) {
        cout << wbScheduledNext << endl;
    }
}

template<class Impl>
void FUWrapper<Impl>::setLSQ(typename Impl::CPUPol::LSQ *lsq)
{
    ldstQueue = lsq;
}

template<class Impl>
void FUWrapper<Impl>::setDQ(DQ *_dq)
{
    dq = _dq;
}

template<class Impl>
void FUWrapper<Impl>::setExec(Exec *_exec)
{
    exec = _exec;
}

void
SingleFUWrapper::init(
        bool pipe, bool single_cycle, bool long_lat,
        unsigned _latency, unsigned max_pipe_lat)
{
    DQPointer inv;
    inv.valid = false;

    seq = 0;
    isPipelined = pipe;
    isSingleCycle = single_cycle;
    isLongLatency = long_lat;
    latency = _latency;
    MaxPipeLatency = max_pipe_lat;
    for (unsigned i = 0; i < MaxPipeLatency; i++) {
        pipelinedPointers.push(inv);
        pipelinedValid.push(false);
    }
}

}

#include "cpu/forwardflow/isa_specific.hh"

template class FF::FUWrapper<FFCPUImpl>;
