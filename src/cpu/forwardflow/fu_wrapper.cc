//
// Created by zyy on 19-6-11.
//

#include "cpu/forwardflow/fu_wrapper.hh"

#include <sstream>

#include "cpu/op_class.hh"
#include "fu_wrapper.hh"
#include "params/FFFUPool.hh"
#include "params/FUDesc.hh"
#include "params/OpDesc.hh"
#include "sim/sim_object.hh"

namespace FF{

template<class Impl>
bool FUWrapper<Impl>::canServe(DynInstPtr &inst) {
    auto lat = opLat[inst->opClass()];
    auto has_capability = capabilityList[inst->opClass()];
    auto wb_port_already_scheduled = wbScheduled[lat];
    return has_capability && !wb_port_already_scheduled;
}

template<class Impl>
bool FUWrapper<Impl>::consume(FUWrapper::DynInstPtr &inst) {

    auto lat = opLat[inst->opClass()];
    SingleFUWrapper &wrapper = wrappers[inst->opClass()];
    assert(!wrapper.writtenThisCycle);
    assert(lat == wrapper.latency);
    wrapper.writtenThisCycle = true;

    DQPointer &dest = inst->pointers[0];
    if (dest.valid) {
        // schedule wake up
        if (wrapper.isSingleCycle) {
            // schedule wb port
            assert(!wbScheduledNext[lat]);
            wbScheduledNext[lat] = true;

            wrapper.oneCyclePointer = dest;
            assert(!wrapper.writtenThisCycle);
            wrapper.writtenThisCycle = true;

        } else if (wrapper.isPipelined) {
            // schedule wb port
            assert(!wbScheduledNext[lat]);
            wbScheduledNext[lat] = true;

            wrapper.pipelinedPointers.push(dest);
            assert(!wrapper.writtenThisCycle);
            wrapper.writtenThisCycle = true;

        } else {
            assert(wrapper.isLongLatency);
            wrapper.longLatencyPointer = dest;
            assert(!wrapper.writtenThisCycle);
            wrapper.writtenThisCycle = true;
        }
    }
    return true;
}

template<class Impl>
void FUWrapper<Impl>::tick() {
    setWakeup();
}

template<class Impl>
void FUWrapper<Impl>::setWakeup() {
    bool set_wb_this_cycle = false;
    for (auto &pair: wrappers) {
        SingleFUWrapper &wrapper = pair.second;

        int count = 0;
        if (wrapper.isSingleCycle && wrapper.oneCyclePointer.valid) {
            toWakeup = wrapper.oneCyclePointer;
            count += 1;
        }
        if (wrapper.isLongLatency && wrapper.cycleLeft == 1 &&
                wrapper.longLatencyPointer.valid) {
            toWakeup = wrapper.longLatencyPointer;
            count += 1;
        }
        if (wrapper.isPipelined && wrapper.pipelinedPointers.front().valid) {
            toWakeup = wrapper.pipelinedPointers.front();
            count += 1;
        }
        assert(count <= 1);
        if (count) {
            assert(!set_wb_this_cycle);
            set_wb_this_cycle = true;
        }
    }
}

template<class Impl>
void FUWrapper<Impl>::startCycle() {
    toWakeup.valid = false;
    wbScheduled = wbScheduledNext;
    wbScheduledNext = wbScheduled << 1;  // expect false shifted in
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
        if (wrapper.isLongLatency && !wrapper.isLSU) {
            wrapper.cycleLeft = std::max(wrapper.cycleLeft + 1, (unsigned) 0);
            if (wrapper.cycleLeft <= 1 && !wbScheduledNext[1]) {
                wbScheduledNext[1] = true;
            }
        }
        if (wrapper.isPipelined) {
            wrapper.pipelinedPointers.pop();
        }
    }
}

template<class Impl>
void FUWrapper<Impl>::endCycle() {
    for (auto &pair: wrappers) {
        SingleFUWrapper &wrapper = pair.second;
        if (wrapper.isPipelined && !wrapper.writtenThisCycle) {
            wrapper.pipelinedPointers.push(inv);
        }
    }
}

template<class Impl>
void FUWrapper<Impl>::init(const Params *p, unsigned bank)
{
    numFU = 0;
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

            // Add each of the FU's that will have this capability to the
            // appropriate queue.
            for (int k = 0; k < (*i)->number; ++k)
                wrappers[(*j)->opClass].init(
                        (*j)->pipelined,
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
        fu->name = (*i)->name() + "(0)";

        for (int c = 1; c < (*i)->number; ++c) {
            std::ostringstream s;
            numFU++;
            FuncUnit *fu2 = new FuncUnit(*fu);

            s << (*i)->name() << "(" << c << ")";
            fu2->name = s.str();
        }
    }
}

template<class Impl>
FUWrapper<Impl>::FUWrapper()
{
    inv.valid = false;
}

void
SingleFUWrapper::init(
        bool pipe, bool single_cycle, bool long_lat,
        unsigned _latency, unsigned max_pipe_lat)
{
    DQPointer inv;
    inv.valid = false;

    isPipelined = pipe;
    isSingleCycle = single_cycle;
    isLongLatency = long_lat;
    latency = _latency;
    MaxPipeLatency = max_pipe_lat;
    for (unsigned i = 0; i < MaxPipeLatency; i++) {
        pipelinedPointers.push(inv);
    }
}

}

#include "cpu/forwardflow/isa_specific.hh"

template class FF::FUWrapper<FFCPUImpl>;
