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
#include "debug/FUW2.hh"
#include "debug/RSProbe1.hh"
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
    DPRINTF(FUW2, "FUW , opclass:%d\n", inst->opClass());
    auto lat = opLat[inst->opClass()];
    auto has_capability = capabilityList[inst->opClass()];
    bool fu_free = true;

    if (has_capability) {
        const auto &wrapper = wrappers[inst->opClass()];
        fu_free = !wrapper.isLongLatency || !wrapper.hasPendingInst;
    }

    DPRINTF(FUSched, "wbScheduled now in %s: ", __func__);
    if (Debug::FUSched) {
        cout << wbScheduled << endl;
    }
    bool wb_port_already_scheduled = wbScheduled[lat - 1] || wbScheduledNext[lat - 2];
    return has_capability && !wb_port_already_scheduled && fu_free;
}

template<class Impl>
bool FUWrapper<Impl>::consume(FUWrapper::DynInstPtr &inst)
{

    auto lat = opLat[inst->opClass()];
    SingleFUWrapper &wrapper = wrappers[inst->opClass()];
    assert(lat == wrapper.latency);

    insts[inst->seqNum] = inst;
    inst->fuGranted = true;

    DPRINTF(FUW||Debug::RSProbe1,
            "w(%i, %i) Consuming inst[%d]\n", wrapperID, inst->opClass(), inst->seqNum);

    DQPointer &dest = inst->pointers[0];

    std::array<DQPointer, 2> to_wake;
    to_wake[SrcPtr].valid = false;
    to_wake[DestPtr].valid = false;

    if (dest.valid) {
        if (!(inst->isLoad() || inst->isStoreConditional() || inst->isForwarder())) {
            DPRINTFR(FUW, "to wake up (%i) (%i %i) (%i)\n",
                     dest.valid, dest.bank, dest.index, dest.op);
            to_wake[DestPtr] = dest;
        } else {
            DPRINTFR(FUW, "(let loads/SC/forwarder forget) to wake up (%i) (%i %i) (%i)\n",
                     dest.valid, dest.bank, dest.index, dest.op);
        }
    }
    if (inst->numDestRegs() > 0) {
        if (!(inst->isLoad() || inst->isStoreConditional() || inst->isForwarder())) {
            to_wake[SrcPtr] = inst->dqPosition;
            if (!dest.valid && !inst->isForwarder()) {
                inst->destReforward = true;
                DPRINTFR(FUW, "to wake up itself "
                        " who has children but not dispatched yet\n");
            } else {
                DPRINTFR(FUW, "to wake up itself "
                        " in case that current child was squashed\n");
            }
        } else {
            DPRINTFR(FUW, "(let loads/SC/forwarder forget) to wake up (%i) (%i %i) (%i)\n",
                    dest.valid, dest.bank, dest.index, dest.op);
        }
    } else {
        DPRINTFR(FUW, "but wake up nobody\n");
    }

    // schedule wake up
    if (wrapper.isSingleCycle) {
        assert(!wrapper.hasPendingInst);

        wrapper.hasPendingInst = true;  // execute in one cycle
        wrapper.seq = inst->seqNum;

        // schedule wb port
        assert(!wbScheduled[0]);
        wbScheduled[0] = true;
        wrapper.oneCyclePointer = to_wake;

        DPRINTF(FUW, "Add inst[%i] into 1-cycle wrapper (%i, %i)\n",
                inst->seqNum, wrapperID, inst->opClass());


    } else if (wrapper.isPipelined) {
        // schedule wb port
        assert(!wbScheduledNext[lat-2]);
        wbScheduledNext[lat-2] = true;
        wbScheduled[lat-1] = true;

        DPRINTF(FUPipe, "wrapper(%i, %i) is pipelined, size:%u, will push\n",
                wrapperID, inst->opClass(), wrapper.pipelineQueue.size());
        wrapper.pipelineQueue.push_back({true, to_wake, inst->seqNum});

        assert(!wrapper.writtenThisCycle);
        wrapper.writtenThisCycle = true;

        DPRINTF(FUW, "Add inst[%i] into pipelined wrapper (%i, %i) with lat %u\n",
                inst->seqNum, wrapperID, inst->opClass(), wrapper.latency);

    } else {
        assert(wrapper.isLongLatency);
        if (wrapper.hasPendingInst) {
            DPRINTF(FUW, "w(%i, %i) hasPendingInst on consuming new inst\n",
                    wrapperID, inst->opClass());
            assert(!wrapper.hasPendingInst);
        }
        assert(!wbScheduledNext[lat-2]);

        if (lat <= Impl::MaxOpLatency) {
            wbScheduled[lat-1] = true;
        }

//        wrapper.toNextCycle->hasPendingInst = true;
//        wrapper.toNextCycle->seq = inst->seqNum;
//        wrapper.toNextCycle->longLatencyPointer = to_wake;
//        wrapper.toNextCycle->cycleLeft = lat - 1;

        wrapper.hasPendingInst = true;
        wrapper.seq = inst->seqNum;
        wrapper.cycleLeft = lat;
        wrapper.longLatencyPointer = to_wake;

        DPRINTF(FUW, "Add inst[%i] into LL wrapper (%i, %i)\n",
                inst->seqNum, wrapperID, inst->opClass());
    }

    return true;
}

template<class Impl>
void FUWrapper<Impl>::tick() {
    panic("should not be called!\n");
}

template<class Impl>
void FUWrapper<Impl>::setWakeup() {
    int count_overall = 0;
    for (auto &pair: wrappers) {
        int count = 0;
        DPRINTF(FUW2, "waking in wrapper (%d, %d)\n", wrapperID, pair.first);
        SingleFUWrapper &wrapper = pair.second;

        // todo: has pending inst should be cleared every cycle
        if (wrapper.isSingleCycle && wrapper.hasPendingInst) {
            DPRINTF(FUW, "w(%i, %i) Waking up children of one cycle inst[%d]\n",
                    wrapperID, pair.first, wrapper.seq);
            toWakeup = wrapper.oneCyclePointer;
            seqToExec = wrapper.seq;
            count += 1;
        }
        if (wrapper.isLongLatency && wrapper.hasPendingInst) {
            if (wrapper.cycleLeft == 1) {
                DPRINTF(FUW, "w(%i, %i) Waking up children of LL inst[%d]\n",
                        wrapperID, pair.first, wrapper.seq);
                toWakeup = wrapper.longLatencyPointer;
                seqToExec = wrapper.seq;
                count += 1;
            } else {
                DPRINTF(FUW, "w(%i, %i) LL inst[%llu] has %i cycles left\n",
                        wrapperID, pair.first, wrapper.seq, wrapper.cycleLeft);
                assert(wrapper.cycleLeft > 1);
                // keep
                wrapper.toNextCycle->longLatencyPointer = wrapper.longLatencyPointer;
                wrapper.toNextCycle->seq = wrapper.seq;
                wrapper.toNextCycle->hasPendingInst = true;
                // decrement
                wrapper.toNextCycle->cycleLeft = wrapper.cycleLeft - 1;
            }
        }


        if (wrapper.isPipelined && wrapper.pipelineQueue.front().valid) {
            DPRINTF(FUW, "w(%i, %i) Waking up children of pipelined inst[%d]\n",
                    wrapperID, pair.first, wrapper.pipelineQueue.front().seq);
            if (Debug::FUSched) {
                cout << wbScheduled << endl;
            }
            toWakeup = wrapper.pipelineQueue.front().pointer;
            seqToExec = wrapper.pipelineQueue.front().seq;
            count += 1;
        }
        assert(count <= 1);
        if (count) {
            assert(!count_overall);
            toExec = true;
        }
        count_overall += count;
    }
    DPRINTF(FUSched, "wbScheduled now: ");
    if (Debug::FUSched) {
        cout << wbScheduled << endl;
    }
    if (wbScheduled[0]) {
        DPRINTF(FUW, "one instruction should be executed and its children"
                " should be waken up\n");

        const DQPointer &src = toWakeup[SrcPtr];
        const DQPointer &dest = toWakeup[DestPtr];

        if (src.valid) {
            DPRINTF(FUW, "To wakeup source: (%i %i) (%i)\n",
                    src.bank, src.index, src.op);

        } else {
            DPRINTF(FUW, "Don't wakeup source: (%i) (%i %i) (%i)\n",
                    src.valid, src.bank, src.index, src.op);
        }
        if (dest.valid) {
            DPRINTF(FUW, "To wakeup dest: (%i %i) (%i)\n",
                    dest.bank, dest.index, dest.op);

        } else {
            DPRINTF(FUW, "Don't wakeup dest: (%i) (%i %i) (%i)\n",
                    dest.valid, dest.bank, dest.index, dest.op);
        }

        assert(count_overall > 0);
    }
}

template<class Impl>
void FUWrapper<Impl>::startCycle() {
    toWakeup[SrcPtr].valid = false;
    toWakeup[DestPtr].valid = false;
    toExec = false;
    seqToExec = 0;

    wbScheduled = wbScheduledNext;
    wbScheduledNext = wbScheduled >> 1;  // expect false shifted in
    DPRINTF(FUSched, "wbScheduled at start: ");
    if (Debug::FUSched) {
        cout << wbScheduled << endl;
    }

    // read signals


    DPRINTF(FUSched, "wbScheduledNext at start: ");
    if (Debug::FUSched) {
        cout << wbScheduledNext << endl;
    }
    for (auto &pair: wrappers) {
        SingleFUWrapper &wrapper = pair.second;
        wrapper.writtenThisCycle = false;

#define wrapperReadFromLast(field) \
        do { wrapper.field = wrapper.fromLastCycle->field;} while (0)

        wrapperReadFromLast(hasPendingInst);
        wrapperReadFromLast(seq);
        wrapperReadFromLast(oneCyclePointer);
        wrapperReadFromLast(longLatencyPointer);
        wrapperReadFromLast(cycleLeft);
#undef wrapperReadFromLast
    }

    for (auto &pair: wrappers) {
        SingleFUWrapper &wrapper = pair.second;

        if (wrapper.isLongLatency && wrapper.hasPendingInst && !wrapper.isLSU) {
            if (wrapper.cycleLeft <= 20 && !wbScheduled[wrapper.cycleLeft - 1]) {
                wbScheduledNext[wrapper.cycleLeft - 2] = true;
            }
        }

        if (wrapper.isPipelined) {
            DPRINTF(FUPipe, "wrapper(%i, %i) is pipelined, size:%u, will pop\n",
                    wrapperID, pair.first, wrapper.pipelineQueue.size());
            assert(wrapper.pipelineQueue.size() == wrapper.latency);
            // pop to prepare for incoming pointers
            wrapper.pipelineQueue.pop_front();
        }
    }
}

template<class Impl>
void FUWrapper<Impl>::advance() {
    for (auto &pair: wrappers) {
        SingleFUWrapper &wrapper = pair.second;
        wrapper.timeStruct->advance();
    }
}

template<class Impl>
void FUWrapper<Impl>::endCycle() {
    for (auto &pair: wrappers) {
        SingleFUWrapper &wrapper = pair.second;
        // maintain pipelined bitset
        if (wrapper.isPipelined) {
            if (!wrapper.writtenThisCycle) {
                DPRINTF(FUPipe, "wrapper(%i, %i) is pipelined, size:%u, will push\n",
                        wrapperID, pair.first, wrapper.pipelineQueue.size());
                wrapper.pipelineQueue.push_back({false, {inv, inv}, 0});
            } else {
                DPRINTF(FUPipe, "wrapper(%i, %i) has been written, size:%u, will not push\n",
                        wrapperID, pair.first, wrapper.pipelineQueue.size());
            }
        }
    }
    advance();
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
void FUWrapper<Impl>::fillLatTable(std::unordered_map<OpClass, unsigned> &v)
{
    for (unsigned i = 0; i < Num_OpClasses; i++) {
        auto opc = static_cast<OpClass>(i);
        if (capabilityList[i] && !v.count(opc)) {
            v[opc] = opLat[i];
        }
    }
}

template<class Impl>
void FUWrapper<Impl>::executeInsts()
{
    if (toExec) {
        DPRINTF(FUW, "toExec is valid, execute now!\n");
        assert(insts.count(seqToExec));
        assert(insts[seqToExec]);
        exec->executeInst(insts[seqToExec]);
        insts.erase(seqToExec);

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

template<class Impl>
void FUWrapper<Impl>::dumpWBSchedule() const
{
    DPRINTF(FUSched, "wbScheduled dump: ");
    if (Debug::FUSched) {
        cout << wbScheduled << endl;
    }
}

template<class Impl>
void FUWrapper<Impl>::squash(InstSeqNum squash_seq)
{
    for (auto &pair: wrappers) {
        auto &fu = pair.second;
        if (fu.isLongLatency && fu.hasPendingInst &&
                fu.seq > squash_seq) {
            DPRINTF(FUW, "Squashing LL inst[%llu] in w(%i, %i)\n",
                    fu.seq, wrapperID, pair.first);
            fu.toNextCycle->hasPendingInst = false;
            fu.toNextCycle->cycleLeft = 0;
            fu.toNextCycle->seq = 0;

            insts.erase(fu.seq);

            if (fu.cycleLeft > 1 && fu.cycleLeft <= 19) {
                assert(wbScheduledNext[fu.cycleLeft - 2] || wbScheduled[fu.cycleLeft - 1]);
                wbScheduledNext[fu.cycleLeft - 2] = false;

            } else if (fu.cycleLeft == 20){
                // here are some corner cases that I don't want to handle
                wbScheduledNext[fu.cycleLeft - 2] = false;
            }
        }

        if (fu.isPipelined) {
            unsigned cycle_left = 1;
            // note that head has already been read in this tick
            for (auto &element: fu.pipelineQueue) {
                if (cycle_left > 1 && element.valid && element.seq > squash_seq) {
                    DPRINTF(FUW, "Squashing pipe inst[%llu] in w(%i, %i)\n",
                            element.seq, wrapperID, pair.first);
                    element.pointer[SrcPtr].valid = false;
                    element.pointer[DestPtr].valid = false;
                    element.valid = false;
                    insts.erase(element.seq);

                    assert(wbScheduledNext[cycle_left - 2]);
                    wbScheduledNext[cycle_left - 2] = false;
                }
                cycle_left++;
            }
        }

        // no need to handle single-cycle instructions, right?
    }
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
    for (unsigned i = 0; i < latency; i++) {
        pipelineQueue.push_back({false, {inv, inv}, 0});
    }
    timeStruct = new TimeBuffer<SFUTimeReg>(2, 2);
    toNextCycle = timeStruct->getWire(0);
    fromLastCycle = timeStruct->getWire(-1);
}

}

#include "cpu/forwardflow/isa_specific.hh"

template class FF::FUWrapper<FFCPUImpl>;
