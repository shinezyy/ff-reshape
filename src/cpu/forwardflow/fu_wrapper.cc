//
// Created by zyy on 19-6-11.
//

#include "fu_wrapper.hh"
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
        if (wrapper.isLongLatency) {
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
FUWrapper<Impl>::FUWrapper() {
    inv.valid = false;
}

}
